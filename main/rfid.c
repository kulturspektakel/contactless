#include "rfid.h"
#include <esp_log.h>
#include <mbedtls/base64.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mfrc522.h"

static const char* TAG = "rfid";

static bool read_balance(spi_device_handle_t spi, mfrc522_uid* uid) {
  if (PICC_GetType(uid->sak) == PICC_TYPE_MIFARE_UL) {
    // read counter
    uint8_t command[] = {0x39, 0x00, 0x1A, 0x7F};  // Read Counter 00 + CRC
    uint8_t backData[8];                           // needs to be at least 8 bytes
    uint8_t backLen = sizeof(backData);
    if (PCD_TransceiveData(spi, command, sizeof(command), backData, &backLen, NULL, 0, true) !=
        STATUS_OK) {
      ESP_LOGE(TAG, "Reading counter failed");
      return false;
    }
    uint16_t counter = *((uint16_t*)backData);
    ESP_LOGI(TAG, "Counter: %d", counter);

    uint8_t size = 16 + 2 + 16;
    uint8_t payload[size];

    if (MIFARE_Read(spi, 9, payload, &size) != STATUS_OK ||
        MIFARE_Read(spi, 13, payload + 16, &size) != STATUS_OK) {
      ESP_LOGE(TAG, "Reading payload failed");
      return false;
    }

    uint8_t decodedPayload[17];
    mbedtls_base64_decode(decodedPayload, sizeof(decodedPayload), &size, payload, size);
    ESP_LOG_BUFFER_HEX(TAG, decodedPayload, sizeof(decodedPayload));

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
  PCD_Init(spi, 5);

  ESP_LOGI(TAG, "Start scanning for tags");

  while (1) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
    if (PICC_IsNewCardPresent(spi)) {
      PICC_Select(spi, &uid, 0);
      ESP_LOGI(TAG, "New card present %d", uid.size);
      read_balance(spi, &uid);
    }
  }
}
