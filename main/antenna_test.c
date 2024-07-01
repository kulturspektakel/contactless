#include "antenna_test.h"
#include "constants.h"
#include "esp_log.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "pn532.h"
#include "rfid.h"

antenna_test_state_t antenna_test_status = ANTENNA_TEST_NOT_STARTED;

void antenna_test(void* params) {
  ESP_LOGI(ANTENNA_TEST_TASK, "Starting test...");
  antenna_test_status = ANTRNNA_TEST_RUNNING;
  xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);

  vTaskSuspend(xTaskGetHandle(RFID_TASK));
  for (int i = ANTENNA_TEST_150MA; i >= 0; i--) {
    ESP_LOGI(ANTENNA_TEST_TASK, "Testing %d", i);
    int result = pn532_antenna_test(false, i);
    if (result < 0) {
      antenna_test_status = ANTENNA_TEST_FAILED;
      break;
    }

    bool andet_up = (result & (1 << 6)) != 0;
    bool andet_bot = (result & (1 << 7)) != 0;

    if (andet_bot) {
      antenna_test_status = ANTENNA_TEST_TOO_LOW;
      break;
    } else if (andet_up) {
      antenna_test_status = i + 1;
      break;
    }
  }
  vTaskResume(xTaskGetHandle(RFID_TASK));

  if (antenna_test_status == ANTRNNA_TEST_RUNNING) {
    // passed all tests
    antenna_test_status = ANTENNA_TEST_45MA;
  }

  ESP_LOGI(ANTENNA_TEST_TASK, "Test completed");
  xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
  vTaskDelete(NULL);
}
