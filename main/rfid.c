#include "rfid.h"
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mfrc522.h"

static const char* TAG = "rfid";

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
      .host = VSPI_HOST,
      .miso_gpio = 19,
      .mosi_gpio = 23,
      .sck_gpio = 18,
      .sda_gpio = 5,
      .clock_speed_hz = 10000000,
      // IRQ = 17
  };

  MFRC522Ptr_t mfrc = MFRC522_Init();
  PCD_Init(mfrc, &config);

  ESP_ERROR_CHECK_WITHOUT_ABORT(PCD_SelfTest(mfrc));

  ESP_LOGI(TAG, "Start scanning for tags");

  while (1) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
    if (PICC_IsNewCardPresent(mfrc)) {
      ESP_LOGI(TAG, "New card present");
      PICC_ReadCardSerial(mfrc);
      uint8_t serial[5];
      for (int i = 0; i < 5; i++) {
        serial[i] = mfrc->uid.uidByte[i];
      }
      ESP_LOGI(
          TAG,
          "Serial: %02x %02x %02x %02x %02x",
          serial[0],
          serial[1],
          serial[2],
          serial[3],
          serial[4]
      );
    }
  }
}
