#include "antenna_test.h"
#include "constants.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"

antenna_test_state_t antenna_test_status = ANTENNA_TEST_NOT_STARTED;

void antenna_test(void* params) {
  ESP_LOGI(ANTENNA_TEST_TASK, "Starting test...");

  for (int i = ANTENNA_TEST_150MA; i >= 0; i--) {
    ESP_LOGI(ANTENNA_TEST_TASK, "Testing %d", i);
    // TODO set register to i
    // TODO read register

    if (andet_bot) {
      antenna_test_status = ANTENNA_TEST_TOO_LOW;
      break;
    } else if (andet_up) {
      antenna_test_status = i + 1;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  if (antenna_test_status == ANTENNA_TEST_NOT_STARTED) {
    // passed all tests
    antenna_test_status = ANTENNA_TEST_45MA;
  }

  ESP_LOGI(ANTENNA_TEST_TASK, "Test completed");
  xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
  vTaskDelete(NULL);
}
