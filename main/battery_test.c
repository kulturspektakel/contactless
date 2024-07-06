#include "battery_test.h"
#include <logmessage.pb.h>
#include "constants.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_writer.h"

#define INTERVAL 180000

int battery_test_data_points = 0;

void battery_test(void* params) {
  battery_test_data_points = 0;
  bool test_started = false;

  while (1) {
    vTaskDelay(INTERVAL / portTICK_PERIOD_MS);

    bool usb_connected = xEventGroupGetBits(event_group) & USB_CONNECTED;
    if (usb_connected && !test_started) {
      // Test not started
      continue;
    } else if (usb_connected && test_started) {
      // USB was connected during test, restart
      esp_restart();
      break;
    }

    test_started = true;

    xTaskNotifyGive(xTaskGetHandle(POWER_MANAGEMENT_TASK));

    // wait for measurement to be taken
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    LogMessage* log = pvPortMalloc(sizeof(LogMessage));
    *log = (LogMessage)LogMessage_init_default;
    log->has_order = false;
    log->has_card_transaction = false;
    battery_test_data_points++;

    xQueueSend(log_queue, &log, NULL);

    xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
  }

  vTaskDelete(NULL);
}