#include "rfid.h"
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mfrc522.h"

static const char* TAG = "rfid";

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

  ESP_ERROR_CHECK(spi_bus_initialize(VSPI_HOST, &buscfg, SPI_DMA_DISABLED));
  ESP_ERROR_CHECK(spi_bus_add_device(VSPI_HOST, &devcfg, &spi));
  PCD_Init(spi, 5);

  ESP_LOGI(TAG, "Start scanning for tags");

  while (1) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
    if (PICC_IsNewCardPresent(spi)) {
      ESP_LOGI(TAG, "New card present");
      PICC_ReadCardSerial(spi);
    }
  }
}
