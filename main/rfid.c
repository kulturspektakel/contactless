#include "rfid.h"
#include <esp_log.h>
#include "rc522.h"

static const char* TAG = "rfid";
static rc522_handle_t scanner;

static void rc522_handler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data) {
  rc522_event_data_t* data = (rc522_event_data_t*)event_data;
  ESP_LOGI(TAG, "Tag scanned (sn: )");

  //   switch (event_id) {
  //     case RC522_EVENT_TAG_SCANNED: {
  //       rc522_tag_t* tag = (rc522_tag_t*)data->ptr;
  //       ESP_LOGI(TAG, "Tag scanned (sn: %" PRIu64 ")", tag->serial_number);
  //     } break;
  //   }
}

void rfid(void* params) {
  rc522_config_t config = {
      .transport = RC522_TRANSPORT_SPI,
      .spi.host = VSPI_HOST,
      .spi.miso_gpio = 21,
      .spi.mosi_gpio = 19,
      .spi.sck_gpio = 18,
      .spi.sda_gpio = 22,
  };
  // IRQ = 17

  gpio_set_direction(config.spi.miso_gpio, GPIO_MODE_INPUT);
  gpio_set_direction(config.spi.mosi_gpio, GPIO_MODE_OUTPUT);
  gpio_set_direction(config.spi.sck_gpio, GPIO_MODE_OUTPUT);
  gpio_set_direction(config.spi.sda_gpio, GPIO_MODE_OUTPUT);

  rc522_create(&config, &scanner);
  rc522_register_events(scanner, RC522_EVENT_ANY, rc522_handler, NULL);
  rc522_start(scanner);
  while (1) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
