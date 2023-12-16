#include "rfid.h"
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mfrc522.h"

static const char* TAG = "rfid";
static mfrc522_handle_t handle;

// static void rc522_handler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data) {
//   rc522_event_data_t* data = (rc522_event_data_t*)event_data;

//   if (event_id != RC522_EVENT_TAG_SCANNED) {
//     // there are no other events, lol
//     ESP_LOGE(TAG, "Unknown event id: %ld", event_id);
//     return;
//   }

//   rc522_tag_t* tag = (rc522_tag_t*)data->ptr;
//   ESP_LOGI(TAG, "Tag scanned (sn: %" PRIu64 ")", tag->serial_number);
// }

void rfid(void* params) {
  mfrc522_config_t config = {
      .spi.host = VSPI_HOST,
      .spi.miso_gpio = 19,
      .spi.mosi_gpio = 23,
      .spi.sck_gpio = 18,
      .spi.sda_gpio = 5,
      // IRQ = 17
  };

  MFRC522Ptr_t mfrc = MFRC522_Init();
  PCD_Init(mfrc, spi0);

  ESP_LOGI(TAG, "Start scanning for tags");

  while (1) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
