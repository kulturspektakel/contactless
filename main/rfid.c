#include "rfid.h"
#include <esp_log.h>
#include <mbedtls/base64.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_auth_headers.h"
#include "mbedtls/sha1.h"
#include "mfrc522.h"

static const char* TAG = "rfid";
ultralight_card_info_t current_card = {0};

#define LENGTH_ID 7
#define LENGTH_COUNTER 2
#define LENGTH_DEPOSIT 1
#define LENGTH_BALANCE 2
#define LENGTH_SIGNATURE 5

#define OFFSET_COUNTER LENGTH_ID
#define OFFSET_DEPOSIT LENGTH_ID + LENGTH_COUNTER
#define OFFSET_BALANCE LENGTH_ID + LENGTH_COUNTER + LENGTH_DEPOSIT
#define OFFSET_SIGNATURE LENGTH_ID + LENGTH_COUNTER + LENGTH_DEPOSIT + LENGTH_BALANCE

void calculate_signature_ultralight(uint8_t* target, ultralight_card_info_t* card) {
  char* salt = alloc_slat();
  size_t len = LENGTH_SIGNATURE +  // ID
               LENGTH_COUNTER +    // count
               LENGTH_DEPOSIT +    // deposit
               LENGTH_BALANCE +    // balance
               strlen(salt);
  char hash_input[len];
  memcpy(hash_input, &card->id, 7);
  memcpy(hash_input + OFFSET_COUNTER, &card->counter, LENGTH_COUNTER);
  memcpy(hash_input + OFFSET_DEPOSIT, &card->deposit, LENGTH_DEPOSIT);
  memcpy(hash_input + OFFSET_BALANCE, &card->balance, LENGTH_BALANCE);
  memcpy(hash_input + OFFSET_SIGNATURE, salt, strlen(salt));
  free(salt);

  ESP_LOG_BUFFER_HEX(TAG, hash_input, len);
  create_sha1_hash(hash_input, len, target);
}

static bool read_balance(spi_device_handle_t spi, mfrc522_uid* uid) {
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

    uint8_t size = 18;  // two 16 byte blocks + CRC
    uint8_t payload[16 + 2 + 16];

    if (MIFARE_Read(spi, 9, payload, &size) != STATUS_OK ||
        MIFARE_Read(spi, 13, payload + 16, &size) != STATUS_OK) {
      ESP_LOGE(TAG, "Reading payload failed");
      return false;
    }

    ESP_LOG_BUFFER_HEX(TAG, payload, 23);

    uint8_t decoded_payload[23];
    size_t size_decoded = 0;
    int decode_error =
        mbedtls_base64_decode(decoded_payload, sizeof(decoded_payload), &size_decoded, payload, 23);
    if (decode_error != 0) {
      ESP_LOGE(TAG, "Decoding payload failed %d", decode_error);
      return false;
    }
    ESP_LOG_BUFFER_HEX(TAG, decoded_payload, size_decoded);
    ESP_LOGI(TAG, "Decoded payload size: %d", size_decoded);

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
    uint8_t hash[20];
    calculate_signature_ultralight(hash, &new_card);
    if (memcmp(hash, new_card.signature, LENGTH_SIGNATURE) != 0) {
      ESP_LOGE(TAG, "Signature mismatch");
      return false;
    }

    current_card = new_card;

    return true;
  }
  return false;
}

void rfid(void* params) {
  spi_device_handle_t spi;
  spi_bus_config_t buscfg = {
      .miso_io_num = 19,
      .mosi_io_num = 23,
      .sclk_io_num = 18,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1
  };
  spi_device_interface_config_t devcfg = {
      .clock_speed_hz = 5000000,
      .mode = 0,
      .spics_io_num = 5,
      .queue_size = 7,
  };
  mfrc522_uid uid;

  ESP_ERROR_CHECK(spi_bus_initialize(VSPI_HOST, &buscfg, SPI_DMA_DISABLED));
  ESP_ERROR_CHECK(spi_bus_add_device(VSPI_HOST, &devcfg, &spi));
  PCD_Init(spi, 5 /* cs_pin */);

  ESP_LOGI(TAG, "Start scanning for tags");

  while (1) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
    if (PICC_IsNewCardPresent(spi)) {
      PICC_Select(spi, &uid, 0);
      ESP_LOGI(TAG, "New card present %d", uid.size);
      if (!read_balance(spi, &uid)) {
        // TODO: show error, card not readable
        continue;
      }
    }
  }
}
