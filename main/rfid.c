#include "rfid.h"
#include <esp_log.h>
#include <mbedtls/base64.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_auth_headers.h"
#include "local_config.h"
#include "mbedtls/sha1.h"
#include "mfrc522.h"
#include "state_machine.h"

static const char* TAG = "rfid";
static gpio_num_t NUM_CS_PIN = 21;
ultralight_card_info_t current_card = {0};

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

static bool is_privilege_token(mfrc522_uid* uid) {
  for (int i = 0; i < MAX_PRIVILEGE_TOKENS; i++) {
    if (uid->size == privilege_tokens[i].size &&
        memcmp(uid->uidByte, privilege_tokens[i].bytes, privilege_tokens[i].size) == 0) {
      return true;
    }
  }
  return false;
}

static bool read_card(spi_device_handle_t spi, mfrc522_uid* uid, bool skip_security) {
  if (PICC_GetType(uid->sak) == PICC_TYPE_MIFARE_UL) {
    ultralight_card_info_t new_card = {0};
    // read counter
    uint8_t command[] = {0x39, 0x00, 0x1A, 0x7F};  // Read Counter 00 + CRC
    uint8_t backData[8];                           // needs to be at least 8 bytes
    uint8_t backLen = sizeof(backData);
    if (PCD_TransceiveData(spi, command, sizeof(command), backData, &backLen, NULL, 0, true) !=
        STATUS_OK) {
      ESP_LOGE(TAG, "Reading counter failed");
      return false;
    }

    uint8_t size = 18;
    uint8_t payload[16 + 2 + 16];  // two 16 byte blocks + CRC

    if (MIFARE_Read(spi, 9, payload, &size) != STATUS_OK ||
        MIFARE_Read(spi, 13, payload + 16, &size) != STATUS_OK) {
      ESP_LOGE(TAG, "Reading payload failed");
      return false;
    }
    payload[PAYLOAD_LENGTH] = 0x3D;  // add padding for base64

    uint8_t decoded_payload[PAYLOAD_LENGTH + 1];
    size_t size_decoded = 0;
    int decode_error = mbedtls_base64_decode(
        decoded_payload, sizeof(decoded_payload), &size_decoded, payload, PAYLOAD_LENGTH + 1
    );
    if (decode_error != 0) {
      ESP_LOGE(TAG, "Decoding payload failed %d", decode_error);
      return false;
    } else if (size_decoded != 17) {
      ESP_LOGE(TAG, "Decoded payload has wrong size %d", size_decoded);
      ESP_LOG_BUFFER_HEX(TAG, decoded_payload, size_decoded);
      return false;
    }

    new_card.counter = *((uint16_t*)backData);
    uint16_t counter_from_payload = *(uint16_t*)(decoded_payload + OFFSET_COUNTER);
    if (new_card.counter != counter_from_payload) {
      ESP_LOGE(
          TAG, "Counter mismatch: %d (card) != %d (payload)", new_card.counter, counter_from_payload
      );
      return false;
    }
    memcpy(new_card.id, uid->uidByte, LENGTH_ID);
    new_card.deposit = *(uint8_t*)(decoded_payload + OFFSET_DEPOSIT);
    new_card.balance = *(uint16_t*)(decoded_payload + OFFSET_BALANCE);
    memcpy(new_card.signature, decoded_payload + OFFSET_SIGNATURE, LENGTH_SIGNATURE);

    // verify signature
    if (!skip_security) {
      uint8_t hash[20];
      calculate_signature_ultralight(hash, &new_card);

      if (memcmp(hash, new_card.signature, LENGTH_SIGNATURE) != 0) {
        ESP_LOGE(TAG, "Signature mismatch: hash != signature");
        ESP_LOG_BUFFER_HEX(TAG, hash, 5);
        ESP_LOG_BUFFER_HEX(TAG, new_card.signature, 5);
        return false;
      }
    }

    current_card = new_card;
    return true;
  } else {
    ESP_LOGE(TAG, "Unsupported card type: %d", PICC_GetType(uid->sak));
    ESP_LOG_BUFFER_HEX(TAG, uid->uidByte, uid->size);
  }
  return false;
}

static void calculate_password(mfrc522_uid* uid, uint8_t* password, uint8_t* pack) {
  size_t len = LENGTH_ID + SALT_LENGTH;
  char data[len];
  uint8_t hash[20];
  memcpy(data, uid->uidByte, LENGTH_ID);
  memcpy(&data[LENGTH_ID], SALT, SALT_LENGTH);
  create_sha1_hash(data, sizeof(data), hash);
  memcpy(password, &hash[16], 4);
  memcpy(pack, &hash[14], 2);
}

static bool write_card(spi_device_handle_t spi, mfrc522_uid* uid, ultralight_card_info_t* card) {
  // authenticate
  uint8_t password[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t pack[2] = {0x00, 0x00};
  uint8_t pack_read[2] = {0x01, 0x01};  // initialize with different values than pack
  calculate_password(uid, password, pack);

  if (PCD_NTAG216_Auth(spi, password, pack_read) != STATUS_OK) {
    ESP_LOGE(TAG, "Authentication failed");
    return false;
  }

  // validate PACK
  if (memcmp(pack, pack_read, 2) != 0) {
    ESP_LOGE(
        TAG, "PACK mismatch: %02x%02x != %02x%02x", pack[0], pack[1], pack_read[0], pack_read[1]
    );
    return false;
  }

  ESP_LOGI(TAG, "Authentication successful");

  // calculate payload
  size_t len = LENGTH_ID + LENGTH_COUNTER + LENGTH_DEPOSIT + LENGTH_BALANCE + LENGTH_SIGNATURE;
  uint8_t buffer[len];

  memcpy(buffer, &card->id, LENGTH_ID);
  memcpy(buffer + OFFSET_COUNTER, &card->counter, LENGTH_COUNTER);
  memcpy(buffer + OFFSET_DEPOSIT, &card->deposit, LENGTH_DEPOSIT);
  memcpy(buffer + OFFSET_BALANCE, &card->balance, LENGTH_BALANCE);
  // TODO overflow?!?!
  calculate_signature_ultralight(buffer + OFFSET_SIGNATURE, card);

  ESP_LOG_BUFFER_HEX(TAG, buffer, len);

  // encode payload to base64
  uint8_t write_data[PAYLOAD_LENGTH + 2];
  size_t base64_len = sizeof(write_data);
  if (mbedtls_base64_encode(write_data, base64_len, &base64_len, buffer, len) != 0) {
    ESP_LOGE(TAG, "Encoding payload failed");
    return false;
  }
  if (base64_len != PAYLOAD_LENGTH + 1) {
    ESP_LOGE(TAG, "Base64 length mismatch: %d != %d", base64_len, PAYLOAD_LENGTH);
    ESP_LOG_BUFFER_HEX(TAG, write_data, base64_len);
    return false;
  }
  write_data[PAYLOAD_LENGTH] = 0xFE;  // override padding =

  // write payload
  for (size_t i = 2; i < base64_len / 4; i++) {  // skip first two bytes, because ID did not
                                                 // change
    if (MIFARE_Ultralight_Write(spi, i + 9, &write_data[4 * i], 4) != STATUS_OK) {
      ESP_LOGE(TAG, "Writing payload failed at block %d", i);
      return false;
    }
  }

  int counter_diff = card->counter - current_card.counter;
  if (counter_diff < 0) {
    ESP_LOGE(TAG, "Counter decreased");
    return false;
  } else if (counter_diff > 3) {
    ESP_LOGE(TAG, "Counter diff to high: %d", counter_diff);
    return false;
  }
  ESP_LOGI(TAG, "Incrementing counter by %d", counter_diff);
  while (counter_diff > 0) {
    uint8_t command[] = {0xA5, 0x00, 0x01, 0x00, 0x00, 0x00};  // Increment counter 00
    if (PCD_MIFARE_Transceive(spi, command, sizeof(command), false) != STATUS_OK) {
      ESP_LOGE(TAG, "Incrementing counter failed");
      return false;
    }
    counter_diff--;
  }

  ESP_LOGI(TAG, "Write successful");
  return true;
}

void rfid(void* params) {
  spi_device_handle_t spi;
  spi_bus_config_t buscfg = {
      .miso_io_num = 37,
      .mosi_io_num = 35,
      .sclk_io_num = 36,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
  };
  spi_device_interface_config_t devcfg = {
      .clock_speed_hz = 5000000,
      .mode = 0,
      .spics_io_num = NUM_CS_PIN,
      .queue_size = 7,
  };
  mfrc522_uid uid;

  ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_DISABLED));
  ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &devcfg, &spi));

  PCD_Init(spi, NUM_CS_PIN);

  ESP_LOGI(TAG, "Start scanning for tags");

  while (1) {
    PICC_HaltA(spi);
    PCD_StopCrypto1(spi);

    // wait for card
    if (!PICC_IsNewCardPresent(spi)) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }

    if (PICC_Select(spi, &uid, 0) != STATUS_OK) {
      continue;
    }

    if (is_privilege_token(&uid)) {
      xQueueSend(state_events, (void*)&(event_t){PRIVILEGE_TOKEN_DETECTED}, portMAX_DELAY);
      continue;
    }

    if (!read_card(
            spi, &uid, current_state.mode == WRITE_FAILED || current_state.mode == PRIVILEGED_REPAIR
        )) {
      // TODO: show error, card not readable
      continue;
    }

    ESP_LOGI(TAG, "New card present");
    xQueueSend(state_events, (void*)&(event_t){CARD_DETECTED}, portMAX_DELAY);
    // yield so the status will be updated
    taskYIELD();

    if (current_state.mode != WRITE_CARD) {
      continue;
    }

    if (memcmp(&uid.uidByte, &current_state.data_to_write.id, LENGTH_ID) != 0) {
      ESP_LOGE(TAG, "Card changed during write process");
      continue;
    }

    bool success = false;
    for (int retries = 0; retries < 3 && !success; retries++) {
      ESP_LOGI(TAG, "Write attempt %d", retries);

      if (!write_card(spi, &uid, &current_state.data_to_write)) {
        ESP_LOGE(TAG, "Writing card failed");
        continue;
      }

      ESP_LOGI(TAG, "Card written successfully");
      if (!read_card(spi, &uid, current_state.mode == WRITE_FAILED)) {
        ESP_LOGE(TAG, "Rereading card failed");
        continue;
      }

      if (current_card.deposit != current_state.data_to_write.deposit ||
          current_card.balance != current_state.data_to_write.balance) {
        // reread mismatch
        ESP_LOGE(
            TAG,
            "Reread mismatch: Balance (%d != %d), deposit (%d != %d)",
            current_card.balance,
            current_state.data_to_write.balance,
            current_card.deposit,
            current_state.data_to_write.deposit
        );
        continue;
      }
      success = true;
    }

    xQueueSend(
        state_events,
        (void*)&(event_t){success ? WRITE_SUCCESSFUL : WRITE_UNSUCCESSFUL},
        portMAX_DELAY
    );
  }
}
