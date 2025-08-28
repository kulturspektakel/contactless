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
static uint8_t PAGE_4[16] = {
    0x00,
    0x00,
    0x03,
    0x2B,
    0xD1,
    0x01,
    0x27,
    0x55,
    0x04,
    0x6B,
    0x75,
    0x6C,
    0x74,
    0x2E,
    0x63,
    0x61
};
ultralight_card_info_t current_card = {0};

#define MINIMUM_CARD_TIME 1500
#define OFFSET_COUNTER LENGTH_ID
#define OFFSET_VAILD_UNTIL LENGTH_ID
#define OFFSET_DEPOSIT LENGTH_ID + LENGTH_COUNTER
#define OFFSET_BALANCE LENGTH_ID + LENGTH_COUNTER + LENGTH_DEPOSIT
#define OFFSET_SIGNATURE LENGTH_ID + LENGTH_COUNTER + LENGTH_DEPOSIT + LENGTH_BALANCE

#define PAYLOAD_RAW_LENGTH \
  LENGTH_ID + LENGTH_COUNTER + LENGTH_DEPOSIT + LENGTH_BALANCE + LENGTH_SIGNATURE
#define PAYLOAD_BASE64_LENGTH 23

#define SIGNATURE_INPUT_LENGTH \
  LENGTH_ID + LENGTH_COUNTER + LENGTH_DEPOSIT + LENGTH_BALANCE + SALT_LENGTH

static void calculate_signature_ultralight(uint8_t* target, ultralight_card_info_t* card) {
  char hash_input[SIGNATURE_INPUT_LENGTH] = {0};
  memcpy(hash_input, &card->id, LENGTH_ID);
  if (card->type == REGULAR) {
    memcpy(hash_input + OFFSET_COUNTER, &card->data.regular.counter, LENGTH_COUNTER);
    memcpy(hash_input + OFFSET_DEPOSIT, &card->data.regular.deposit, LENGTH_DEPOSIT);
    memcpy(hash_input + OFFSET_BALANCE, &card->data.regular.balance, LENGTH_BALANCE);
  } else {
    memcpy(hash_input + OFFSET_VAILD_UNTIL, &card->data.crew.valid_until, LENGTH_VAILD_UNTIL);
  }
  memcpy(hash_input + OFFSET_SIGNATURE, SALT, SALT_LENGTH);
  create_sha1_hash(hash_input, SIGNATURE_INPUT_LENGTH, target);
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

static int is_alpha_numeric(char c) {
  return (c >= 'A' && c <= 'Z') ||  // Uppercase letters
         (c >= 'a' && c <= 'z') ||  // Lowercase letters
         (c >= '0' && c <= '9');    // Base64 special characters
}

static event_t read_card(byte_array_t* uid) {
  ultralight_card_info_t new_card = {0};
  memcpy(new_card.id, uid->bytes, LENGTH_ID);

  // read /$$/ prefix and payload
  uint8_t data[4 + PAYLOAD_BASE64_LENGTH + 1];
  if (!mfu_read_page(8, data, 16) || !mfu_read_page(12, data + 16, sizeof(data) - 16)) {
    ESP_LOGE(RFID_TASK, "Reading payload failed");
    return CARD_DETECTED_NOT_READABLE;
  }

  // check if card is uninitialized
  char zeros[sizeof(data)] = {0};
  ESP_LOG_BUFFER_HEX(RFID_TASK, data, sizeof(data));
  if (memcmp(data, zeros, sizeof(data)) == 0) {
    ESP_LOGI(RFID_TASK, "Card is empty");
    current_card = new_card;
    return CARD_DETECTED_UNINITIALIZED;
  }

  uint8_t* payload = data + 4;  // skip header

  // Convert web-safe base64 to standard base64
  payload[PAYLOAD_BASE64_LENGTH] = '=';  // add padding for base64
  for (size_t i = 0; i < PAYLOAD_BASE64_LENGTH; i++) {
    if (payload[i] == '-') {
      payload[i] = '+';
    } else if (payload[i] == '_') {
      payload[i] = '/';
    } else if (!is_alpha_numeric(payload[i])) {
      ESP_LOGE(RFID_TASK, "Invalid character in payload: %c", payload[i]);
      // fix invalid characters, so base64 decoding doesn't fail
      payload[i] = '0';
    }
  }

  uint8_t decoded_payload[PAYLOAD_RAW_LENGTH];
  size_t size_decoded = 0;
  int decode_error = mbedtls_base64_decode(
      decoded_payload, PAYLOAD_RAW_LENGTH, &size_decoded, payload, PAYLOAD_BASE64_LENGTH + 1
  );
  if (decode_error != 0) {
    ESP_LOGE(
        RFID_TASK, "Decoding payload failed. Error %d, decoded %d", decode_error, size_decoded
    );
    ESP_LOG_BUFFER_HEX(RFID_TASK, decoded_payload, PAYLOAD_BASE64_LENGTH);
    return CARD_DETECTED_INVALID;
  } else if (size_decoded != PAYLOAD_RAW_LENGTH) {
    ESP_LOGE(RFID_TASK, "Decoded payload has wrong size %d", size_decoded);
    ESP_LOG_BUFFER_HEX(RFID_TASK, decoded_payload, size_decoded);
    return CARD_DETECTED_INVALID;
  }

  // read card type
  if (memcmp(data, "/$$/", 4) == 0) {
    new_card.type = REGULAR;
    ESP_LOGI(RFID_TASK, "Regular card detected");
  } else if (memcmp(data, "/$c/", 4) == 0) {
    new_card.type = CREW;
    ESP_LOGI(RFID_TASK, "Crew card detected");
  } else {
    ESP_LOGE(RFID_TASK, "Invalid prefix: %c%c%c%c", data[0], data[1], data[2], data[3]);
    return CARD_DETECTED_INVALID;
  }

  ESP_LOG_BUFFER_HEX(RFID_TASK, decoded_payload, PAYLOAD_RAW_LENGTH);

  memcpy(new_card.signature, decoded_payload + OFFSET_SIGNATURE, LENGTH_SIGNATURE);

  if (new_card.type == REGULAR) {
    // The value from the physical counter is stored in new_card, so that cards can be repaired
    // based on the actual counter value in write_card. The value from the payload is only used for
    // verification.
    if (!mfu_read_counter(0, &new_card.data.regular.counter)) {
      return CARD_DETECTED_NOT_READABLE;
    }
    new_card.data.regular.deposit = *(uint8_t*)(decoded_payload + OFFSET_DEPOSIT);
    new_card.data.regular.balance = *(uint16_t*)(decoded_payload + OFFSET_BALANCE);
  } else if (new_card.type == CREW) {
    new_card.data.crew.valid_until = *(uint16_t*)(decoded_payload + OFFSET_VAILD_UNTIL);
  }

  // card was sucessfully read, but not yet verified
  current_card = new_card;

  if (new_card.type == REGULAR) {
    card_error_t error =
        validate_values(new_card.data.regular.balance, new_card.data.regular.deposit);
    if (error != NONE) {
      ESP_LOGE(RFID_TASK, "Card values invalid: %d", error);
      return CARD_DETECTED_INVALID;
    }

    // verify counter
    uint16_t counter_from_payload = *(uint16_t*)(decoded_payload + OFFSET_COUNTER);
    if (new_card.data.regular.counter != counter_from_payload) {
      ESP_LOGE(
          RFID_TASK,
          "Counter mismatch: %d (card) != %d (payload)",
          new_card.data.regular.counter,
          counter_from_payload
      );
      return CARD_DETECTED_SKIPPED_SECUIRTY;
    }
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

static void calculate_password(uint8_t* uid, uint8_t* password, uint8_t* pack) {
  size_t len = LENGTH_ID + SALT_LENGTH;
  char data[len];
  uint8_t hash[20];
  memcpy(data, uid, LENGTH_ID);
  memcpy(&data[LENGTH_ID], SALT, SALT_LENGTH);
  create_sha1_hash(data, sizeof(data), hash);
  memcpy(password, &hash[16], 4);
  memcpy(pack, &hash[14], 2);
}

static bool encode_payload(uint8_t* buffer, uint8_t* write_data) {
  size_t base64_len = PAYLOAD_BASE64_LENGTH + 2;
  if (mbedtls_base64_encode_url_safe(
          write_data, base64_len, &base64_len, buffer, PAYLOAD_RAW_LENGTH
      ) != 0) {
    ESP_LOGE(RFID_TASK, "Encoding payload failed");
    return false;
  }
  if (base64_len != PAYLOAD_BASE64_LENGTH + 1) {
    ESP_LOGE(RFID_TASK, "Base64 length mismatch: %d != %d", base64_len, PAYLOAD_BASE64_LENGTH);
    ESP_LOG_BUFFER_HEX(RFID_TASK, write_data, base64_len);
    return false;
  }
  write_data[PAYLOAD_BASE64_LENGTH] = 0xFE;  // override padding =
  return true;
}

static bool regular_card_payload(ultralight_card_info_t* card, uint8_t* write_data) {
  uint8_t buffer[PAYLOAD_RAW_LENGTH] = {0};
  memcpy(buffer, &card->id, LENGTH_ID);
  memcpy(buffer + OFFSET_COUNTER, &card->data.regular.counter, LENGTH_COUNTER);
  memcpy(buffer + OFFSET_DEPOSIT, &card->data.regular.deposit, LENGTH_DEPOSIT);
  memcpy(buffer + OFFSET_BALANCE, &card->data.regular.balance, LENGTH_BALANCE);
  calculate_signature_ultralight(buffer + OFFSET_SIGNATURE, card);
  return encode_payload(buffer, write_data);
}

static bool crew_card_payload(ultralight_card_info_t* card, uint8_t* write_data) {
  uint8_t buffer[PAYLOAD_RAW_LENGTH] = {0};
  memcpy(buffer, &card->id, LENGTH_ID);
  memcpy(buffer + OFFSET_VAILD_UNTIL, &card->data.crew.valid_until, LENGTH_VAILD_UNTIL);
  calculate_signature_ultralight(buffer + OFFSET_SIGNATURE, card);
  ESP_LOG_BUFFER_HEX(RFID_TASK, buffer, PAYLOAD_RAW_LENGTH);
  return encode_payload(buffer, write_data);
}

static bool authenticate_and_validate_pack(
    ultralight_card_info_t* card,
    bool is_initilization,
    bool is_uninitialized
) {
  // authenticate
  uint8_t password[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t pack[2] = {0x00, 0x00};
  uint8_t pack_read[2] = {0x01, 0x01};  // initialize with different values than pack
  calculate_password(card->id, password, pack);

  if (!is_uninitialized && !ntag2xx_authenticate(password, pack_read)) {
    ESP_LOGE(RFID_TASK, "Authentication failed");
    // when initializing, we try even if authentication fails, because
    return false;
  }

  if (is_initilization) {
    ESP_LOGI(RFID_TASK, "Authentication successful");
    return true;
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
  return true;
}

static bool write_card(ultralight_card_info_t* card) {
  if (!authenticate_and_validate_pack(card, false, false)) {
    return false;
  }
  size_t base64_len = PAYLOAD_BASE64_LENGTH + 2;  // padding + terminator
  uint8_t write_data[base64_len];
  if (!regular_card_payload(card, write_data)) {
    ESP_LOGE(RFID_TASK, "Creating payload failed");
    return false;
  }

  // write payload: skip first two bytes, because ID did not change
  for (size_t i = 2; i < (PAYLOAD_BASE64_LENGTH + 1) / 4; i++) {
    if (!mfu_write_page(i + 9, &write_data[4 * i])) {
      ESP_LOGE(RFID_TASK, "Writing payload failed at block %d", i);
      return false;
    }
  }

  if (card->type == REGULAR) {
    // only regular cards use counter
    int counter_diff = card->data.regular.counter - current_card.data.regular.counter;
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
  }

  ESP_LOGI(RFID_TASK, "Write successful");
  return true;
}

static bool initialize_card(ultralight_card_info_t* card, bool is_uninitialized) {
  if (!authenticate_and_validate_pack(card, true, is_uninitialized)) {
    return false;
  }

  // set payload
  uint8_t writeData[6][4] = {
      // clang-format off
      {0xE1, 0x10, 0x06, 0x00}, // 03: OTP NDEF
      {0x03, 0x29, 0xD1, 0x01},
      {0x25, 0x55, 0x04, 'k' }, 
      {'u',  'l',  't',  '.' }, 
      {'c',  'a',  's',  'h' }, 
      {'/',  '$',  '$',  '/' },
      // clang-format on
  };
  if (card->type == CREW) {
    writeData[5][2] = 'c';
  }

  uint8_t password[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t pack[4] = {0x00, 0x00, 0x00, 0x00};
  calculate_password(card->id, password, pack);

  for (size_t i = 0; i < sizeof(writeData) / sizeof(writeData[0]); i++) {
    if (!mfu_write_page(i + 3, writeData[i])) {
      ESP_LOGE(RFID_TASK, "Writing payload failed at page %d", i);
      return false;
    }
  }

  uint8_t write_data[PAYLOAD_BASE64_LENGTH + 1];
  if (card->type == REGULAR) {
    regular_card_payload(card, write_data);
  } else if (card->type == CREW) {
    crew_card_payload(card, write_data);
  } else {
    ESP_LOGE(RFID_TASK, "Invalid card type");
    return false;
  }
  for (size_t i = 0; i < sizeof(write_data) / 4; i++) {
    if (!mfu_write_page(i + 9, &write_data[4 * i])) {
      ESP_LOGE(RFID_TASK, "Writing payload failed at page %d", i);
      return false;
    } else {
      ESP_LOGI(RFID_TASK, "Writing payload page %d:", i);
      ESP_LOG_BUFFER_HEX(RFID_TASK, &write_data[4 * i], 4);
    }
  }

  if (!is_uninitialized) {
    ESP_LOGI(RFID_TASK, "Card initialized successfully");
    return true;
  }

  uint8_t lastPage = 0x13;

  // set configuration
  uint8_t cfg0[] = {0x04 /* strong modulation */, 0x00, 0x00, 0x04 /* lock from page 4 */};
  if (!mfu_write_page(lastPage - 3, cfg0)) {
    ESP_LOGE(RFID_TASK, "Writing CFG0 failed");
    return false;
  }
  uint8_t cfg1[] = {0x07 /* AUTHLIM = 7 */, 0x05, 0x00, 0x00};
  if (!mfu_write_page(lastPage - 2, cfg1)) {
    ESP_LOGE(RFID_TASK, "Writing CFG1 failed");
    return false;
  }

  // set pack
  if (!mfu_write_page(lastPage, pack)) {
    ESP_LOGE(RFID_TASK, "Writing PACK failed");
    return false;
  }

  // set password last
  if (!mfu_write_page(lastPage - 1, password)) {
    ESP_LOGE(RFID_TASK, "Writing PWD failed");
    return false;
  }

  ESP_LOGI(RFID_TASK, "Card initialized successfully and config written");
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
      ESP_LOGE(RFID_TASK, "Reading card failed");
      continue;
    }
    trigger_event(read_status);

    if (current_state.mode != WRITE_CARD && current_state.mode != WRITE_CARD_INITIALIZE) {
      continue;
    }

    if (memcmp(uid.bytes, current_state.data_to_write.id, LENGTH_ID) != 0) {
      ESP_LOGE(RFID_TASK, "Card changed during write process");
      continue;
    }

    bool success = false;
    for (int i = 0; i < 3; i++) {
      if (i > 0) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        // reread counter, because it might already be incremented
        mfu_read_counter(0, &current_card.data.regular.counter);
        ESP_LOGI(RFID_TASK, "Retrying... (%d)", i);
      }

      if (current_state.mode == WRITE_CARD_INITIALIZE &&
          !initialize_card(
              &current_state.data_to_write, read_status == CARD_DETECTED_UNINITIALIZED
          )) {
        ESP_LOGE(RFID_TASK, "Initialization card failed");
        // trigger_event(WRITE_UNSUCCESSFUL);
        continue;
      } else if (current_state.mode == WRITE_CARD && !write_card(&current_state.data_to_write)) {
        ESP_LOGE(RFID_TASK, "Writing card failed");
        // trigger_event(WRITE_UNSUCCESSFUL);
        continue;
      }
      ESP_LOGI(RFID_TASK, "Card written successfully");

      if (read_card(&uid) != CARD_DETECTED_OK) {
        ESP_LOGE(RFID_TASK, "Rereading card failed");
        // trigger_event(WRITE_UNSUCCESSFUL);
        continue;
      }
      if (current_card.type == REGULAR &&
          (current_card.data.regular.deposit != current_state.data_to_write.data.regular.deposit ||
           current_card.data.regular.balance != current_state.data_to_write.data.regular.balance)) {
        // reread mismatch
        ESP_LOGE(
            RFID_TASK,
            "Reread mismatch: Balance (%d != %d), deposit (%d != %d)",
            current_card.data.regular.balance,
            current_state.data_to_write.data.regular.balance,
            current_card.data.regular.deposit,
            current_state.data_to_write.data.regular.deposit
        );
        // trigger_event(WRITE_UNSUCCESSFUL);
        continue;
      } else if (current_card.type == CREW &&
                 current_card.data.crew.valid_until !=
                     current_state.data_to_write.data.crew.valid_until) {
        // reread mismatch
        ESP_LOGE(
            RFID_TASK,
            "Reread mismatch: Valid until (%d != %d)",
            current_card.data.crew.valid_until,
            current_state.data_to_write.data.crew.valid_until
        );
        // trigger_event(WRITE_UNSUCCESSFUL);
        continue;
      }

      success = true;
      break;
    }

    trigger_event(success ? WRITE_SUCCESSFUL : WRITE_UNSUCCESSFUL);
  }
}
