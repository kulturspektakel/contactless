#include "rfid.h"
#include <esp_log.h>
#include <mbedtls/base64.h>
#include <string.h>
#include "constant_time_internal.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_auth_headers.h"
#include "local_config.h"
#include "mbedtls/sha1.h"
#include "pn532.h"
#include "state_machine.h"

typedef struct {
  uint8_t bytes[10];
  uint8_t length;
} byte_array_t;

static uint8_t NDEF_KEY_A[6] = {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7};
static uint8_t PAGE_4[16] =
    {0x00, 0x00, 0x03, 0x2B, 0xD1, 0x01, 0x27, 0x55, 0x04, 0x6B, 0x75, 0x6C, 0x74, 0x2E, 0x63, 0x61
};
ultralight_card_info_t current_card = {0};

#define MINIMUM_CARD_TIME 1500
#define OFFSET_COUNTER LENGTH_ID
#define OFFSET_DEPOSIT LENGTH_ID + LENGTH_COUNTER
#define OFFSET_BALANCE LENGTH_ID + LENGTH_COUNTER + LENGTH_DEPOSIT
#define OFFSET_SIGNATURE LENGTH_ID + LENGTH_COUNTER + LENGTH_DEPOSIT + LENGTH_BALANCE

#define PAYLOAD_LENGTH 23

static void calculate_signature_ultralight(uint8_t* target, ultralight_card_info_t* card) {
  size_t len = LENGTH_ID +       // ID
               LENGTH_COUNTER +  // count
               LENGTH_DEPOSIT +  // deposit
               LENGTH_BALANCE +  // balance
               SALT_LENGTH;
  char hash_input[len];
  memcpy(hash_input, &card->id, LENGTH_ID);
  memcpy(hash_input + OFFSET_COUNTER, &card->counter, LENGTH_COUNTER);
  memcpy(hash_input + OFFSET_DEPOSIT, &card->deposit, LENGTH_DEPOSIT);
  memcpy(hash_input + OFFSET_BALANCE, &card->balance, LENGTH_BALANCE);
  memcpy(hash_input + OFFSET_SIGNATURE, SALT, SALT_LENGTH);
  create_sha1_hash(hash_input, len, target);
}

static bool is_privilege_token(byte_array_t* uid) {
  for (int i = 0; i < MAX_PRIVILEGE_TOKENS; i++) {
    if (uid->length == privilege_tokens[i].size &&
        memcmp(uid->bytes, privilege_tokens[i].bytes, privilege_tokens[i].size) == 0) {
      return true;
    }
  }
  return false;
}

static int mbedtls_base64_encode_url_safe(
    unsigned char* dst,
    size_t dlen,
    size_t* olen,
    const unsigned char* src,
    size_t slen
) {
  int ret = mbedtls_base64_encode(dst, dlen, olen, src, slen);
  if (ret != 0) {
    return ret;
  }
  for (size_t i = 0; i < *olen; i++) {
    if (dst[i] == '+') {
      dst[i] = '-';
    } else if (dst[i] == '/') {
      dst[i] = '_';
    }
  }
  return 0;
}

static event_t read_card(byte_array_t* uid) {
  uint8_t payload[PAYLOAD_LENGTH + 1];

  if (!mfu_read_page(9, payload, 16) || !mfu_read_page(13, payload + 16, PAYLOAD_LENGTH - 16)) {
    ESP_LOGE(RFID_TASK, "Reading payload failed");
    return CARD_DETECTED_NOT_READABLE;
  }
  payload[PAYLOAD_LENGTH] = '=';  // add padding for base64

  // Convert web-safe base64 to standard base64
  for (size_t i = 0; i < PAYLOAD_LENGTH; i++) {
    if (payload[i] == '-') {
      payload[i] = '+';
    } else if (payload[i] == '_') {
      payload[i] = '/';
    } else if (mbedtls_ct_base64_dec_value(payload[i]) < 0) {
      // fix invalid characters, so base64 decoding doesn't fail
      payload[i] = '0';
    }
  }

  uint8_t decoded_payload[17];
  size_t size_decoded = 0;
  int decode_error = mbedtls_base64_decode(
      decoded_payload, sizeof(decoded_payload), &size_decoded, payload, sizeof(payload)
  );
  if (decode_error != 0) {
    ESP_LOGE(
        RFID_TASK, "Decoding payload failed. Error %d, decoded %d", decode_error, size_decoded
    );
    ESP_LOG_BUFFER_HEX(RFID_TASK, decoded_payload, PAYLOAD_LENGTH);
    return CARD_DETECTED_NOT_READABLE;
  } else if (size_decoded != sizeof(decoded_payload)) {
    ESP_LOGE(RFID_TASK, "Decoded payload has wrong size %d", size_decoded);
    ESP_LOG_BUFFER_HEX(RFID_TASK, decoded_payload, size_decoded);
    return CARD_DETECTED_NOT_READABLE;
  }

  ultralight_card_info_t new_card = {0};
  // The value from the physical counter is stored in new_card, so that cards can be repaired
  // based on the actual counter value in write_card. The value from the payload is only used for
  // verification.
  if (!mfu_read_counter(0, &new_card.counter)) {
    return CARD_DETECTED_NOT_READABLE;
  }
  uint16_t counter_from_payload = *(uint16_t*)(decoded_payload + OFFSET_COUNTER);
  memcpy(new_card.id, uid->bytes, LENGTH_ID);
  new_card.deposit = *(uint8_t*)(decoded_payload + OFFSET_DEPOSIT);
  new_card.balance = *(uint16_t*)(decoded_payload + OFFSET_BALANCE);
  memcpy(new_card.signature, decoded_payload + OFFSET_SIGNATURE, LENGTH_SIGNATURE);

  current_card = new_card;

  // verify counter
  if (new_card.counter != counter_from_payload) {
    ESP_LOGE(
        RFID_TASK,
        "Counter mismatch: %d (card) != %d (payload)",
        new_card.counter,
        counter_from_payload
    );
    return CARD_DETECTED_SKIPPED_SECUIRTY;
  }

  // verify signature
  uint8_t hash[20];
  calculate_signature_ultralight(hash, &new_card);

  if (memcmp(hash, new_card.signature, LENGTH_SIGNATURE) != 0) {
    ESP_LOGE(RFID_TASK, "Signature mismatch: hash != signature");
    ESP_LOG_BUFFER_HEX(RFID_TASK, hash, 5);
    ESP_LOG_BUFFER_HEX(RFID_TASK, new_card.signature, 5);
    return CARD_DETECTED_SKIPPED_SECUIRTY;
  }

  return CARD_DETECTED_OK;
}

static void calculate_password(byte_array_t* uid, uint8_t* password, uint8_t* pack) {
  size_t len = LENGTH_ID + SALT_LENGTH;
  char data[len];
  uint8_t hash[20];
  memcpy(data, uid->bytes, LENGTH_ID);
  memcpy(&data[LENGTH_ID], SALT, SALT_LENGTH);
  create_sha1_hash(data, sizeof(data), hash);
  memcpy(password, &hash[16], 4);
  memcpy(pack, &hash[14], 2);
}

static bool write_card(byte_array_t* uid, ultralight_card_info_t* card) {
  // authenticate
  uint8_t password[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t pack[2] = {0x00, 0x00};
  uint8_t pack_read[2] = {0x01, 0x01};  // initialize with different values than pack
  calculate_password(uid, password, pack);

  if (!ntag2xx_authenticate(password, pack_read)) {
    ESP_LOGE(RFID_TASK, "Authentication failed");
    return false;
  }

  // validate PACK
  if (memcmp(pack, pack_read, 2) != 0) {
    ESP_LOGE(
        RFID_TASK,
        "PACK mismatch: %02x%02x != %02x%02x",
        pack[0],
        pack[1],
        pack_read[0],
        pack_read[1]
    );
    return false;
  }

  ESP_LOGI(RFID_TASK, "Authentication successful");

  // calculate payload
  size_t len = LENGTH_ID + LENGTH_COUNTER + LENGTH_DEPOSIT + LENGTH_BALANCE + LENGTH_SIGNATURE;
  uint8_t buffer[len];

  memcpy(buffer, &card->id, LENGTH_ID);
  memcpy(buffer + OFFSET_COUNTER, &card->counter, LENGTH_COUNTER);
  memcpy(buffer + OFFSET_DEPOSIT, &card->deposit, LENGTH_DEPOSIT);
  memcpy(buffer + OFFSET_BALANCE, &card->balance, LENGTH_BALANCE);
  // TODO overflow?!?!
  calculate_signature_ultralight(buffer + OFFSET_SIGNATURE, card);

  ESP_LOGI(
      RFID_TASK,
      "Write: balance %hu, deposit %hu, counter %hu",
      card->balance,
      card->deposit,
      card->counter
  );

  // encode payload to base64
  uint8_t write_data[PAYLOAD_LENGTH + 2];
  size_t base64_len = sizeof(write_data);
  if (mbedtls_base64_encode_url_safe(write_data, base64_len, &base64_len, buffer, len) != 0) {
    ESP_LOGE(RFID_TASK, "Encoding payload failed");
    return false;
  }
  if (base64_len != PAYLOAD_LENGTH + 1) {
    ESP_LOGE(RFID_TASK, "Base64 length mismatch: %d != %d", base64_len, PAYLOAD_LENGTH);
    ESP_LOG_BUFFER_HEX(RFID_TASK, write_data, base64_len);
    return false;
  }
  write_data[PAYLOAD_LENGTH] = 0xFE;  // override padding =

  // write payload
  for (size_t i = 2; i < base64_len / 4; i++) {  // skip first two bytes, because ID did not
                                                 // change
    if (!mfu_write_page(i + 9, &write_data[4 * i])) {
      ESP_LOGE(RFID_TASK, "Writing payload failed at block %d", i);
      return false;
    }
  }

  int counter_diff = card->counter - current_card.counter;
  if (counter_diff < 0) {
    ESP_LOGE(RFID_TASK, "Counter decreased: %d", counter_diff);
    return false;
  } else if (counter_diff > 3) {
    ESP_LOGE(RFID_TASK, "Counter diff to high: %d", counter_diff);
    return false;
  }
  ESP_LOGI(RFID_TASK, "Incrementing counter by %d", counter_diff);
  if (!mfu_increment_counter(0, counter_diff)) {
    ESP_LOGE(RFID_TASK, "Incrementing counter failed");
    return false;
  }

  ESP_LOGI(RFID_TASK, "Write successful");
  return true;
}

bool is_old_card(byte_array_t* uid) {
  if (uid->length == 4) {
    uint8_t buffer[16];
    uint8_t block_addr = 4;

    if (mfc_authenticate_block(uid->bytes, uid->length, block_addr, 0, NDEF_KEY_A) &&
        mfc_read_data_block(block_addr, buffer) && memcmp(buffer, PAGE_4, sizeof(PAGE_4)) == 0) {
      ESP_LOGI(RFID_TASK, "Old card present");
      return true;
    }
  }
  return false;
}

void rfid(void* params) {
  // if (!pn532_init(35, 37, 48, 47, I2C_NUM_1)) {  // RevA
  if (!pn532_init(39, 38, 48, 47, I2C_NUM_0)) {  // RevE
    ESP_LOGE(RFID_TASK, "PN532 init failed");
    trigger_event(FATAL_ERROR);
  }

  if (!pn532_sam_configuration()) {
    ESP_LOGE(RFID_TASK, "SAM configuration failed");
    trigger_event(FATAL_ERROR);
  }

  uint32_t versiondata = pn532_get_firmware_version();
  // Got ok data, print it out!
  ESP_LOGI(RFID_TASK, "Found chip PN5%lx", (versiondata >> 24) & 0xFF);
  ESP_LOGI(
      RFID_TASK, "Firmware ver. %ld.%ld", (versiondata >> 16) & 0xFF, (versiondata >> 8) & 0xFF
  );

  byte_array_t uid = {};

  ESP_LOGI(RFID_TASK, "Start scanning for tags");
  int64_t card_seen_at = 0;

  while (1) {
    if (card_seen_at > 0) {
      iso14443a_in_deselect();
      while (iso14443a_in_auto_poll(1)) {
        taskYIELD();
      }
      // card removed
      int64_t card_seen_for = (esp_timer_get_time() - card_seen_at) / 1000;
      ESP_LOGI(RFID_TASK, "card seen for %lld", card_seen_for);
      card_seen_at = 0;
      vTaskDelay(
          (card_seen_for < MINIMUM_CARD_TIME ? (MINIMUM_CARD_TIME - card_seen_for) : 0) /
          portTICK_PERIOD_MS
      );
      trigger_event(CARD_REMOVED);
    }

    // wait for card
    if (!iso14443a_read_passive_target_id(PN532_MIFARE_ISO14443A, uid.bytes, &uid.length, 0)) {
      continue;
    }

    ESP_LOGI(RFID_TASK, "Card detected:");
    ESP_LOG_BUFFER_HEX(RFID_TASK, uid.bytes, uid.length);

    // reset current card
    ultralight_card_info_t new_card = {0};
    current_card = new_card;
    card_seen_at = esp_timer_get_time();

    if (is_privilege_token(&uid)) {
      trigger_event(PRIVILEGE_TOKEN_DETECTED);
      continue;
    }

    if (is_old_card(&uid)) {
      trigger_event(CARD_DETECTED_OLD_CARD);
      continue;
    }

    if (uid.length != 7) {
      ESP_LOGE(RFID_TASK, "Invalid UID length: %d", uid.length);
      continue;
    }

    ESP_LOGI(RFID_TASK, "New card present");
    event_t read_status = read_card(&uid);
    if (read_status == CARD_DETECTED_NOT_READABLE) {
      // don't proceed if card is not readable
      continue;
    }
    trigger_event(read_status);

    if (current_state.mode != WRITE_CARD) {
      continue;
    }

    if (memcmp(uid.bytes, current_state.data_to_write.id, LENGTH_ID) != 0) {
      ESP_LOGE(RFID_TASK, "Card changed during write process");
      continue;
    }

    if (!write_card(&uid, &current_state.data_to_write)) {
      ESP_LOGE(RFID_TASK, "Writing card failed");
      trigger_event(WRITE_UNSUCCESSFUL);
      continue;
    }
    ESP_LOGI(RFID_TASK, "Card written successfully");

    if (read_card(&uid) != CARD_DETECTED_OK) {
      ESP_LOGE(RFID_TASK, "Rereading card failed");
      trigger_event(WRITE_UNSUCCESSFUL);
      continue;
    }
    if (current_card.deposit != current_state.data_to_write.deposit ||
        current_card.balance != current_state.data_to_write.balance) {
      // reread mismatch
      ESP_LOGE(
          RFID_TASK,
          "Reread mismatch: Balance (%d != %d), deposit (%d != %d)",
          current_card.balance,
          current_state.data_to_write.balance,
          current_card.deposit,
          current_state.data_to_write.deposit
      );
      trigger_event(WRITE_UNSUCCESSFUL);
      continue;
    }

    trigger_event(WRITE_SUCCESSFUL);
  }
}
