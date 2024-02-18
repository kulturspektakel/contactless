#include "log_writer.h"
#include <dirent.h>
#include <time.h>
#include "device_id.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logmessage.pb.h"
#include "nanopb/pb_encode.h"
#include "power_management.h"
#include "state_machine.h"

static const char* TAG = "logger";
static const char* ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const char* LOG_DIR = "/littlefs/logs";

QueueHandle_t log_queue;

bool write_pb_to_file(pb_ostream_t* stream, const uint8_t* buffer, size_t count) {
  FILE* file = (FILE*)stream->state;
  int ret = fwrite(buffer, count, 1, file);
  if (ret < 0) {
    return false;
  } else if (ret == 0) {
    stream->bytes_written = 0;
    return false;
  }
  return true;
}

void log_writer(void* params) {
  log_queue = xQueueCreate(1, sizeof(LogMessage*));
  LogMessage* log_message = NULL;

  while (true) {
    if (xQueueReceive(log_queue, &log_message, portMAX_DELAY)) {
      if (log_message == NULL) {
        ESP_LOGE(TAG, "Received NULL log message");
        continue;
      }
      for (int i = 0; i < sizeof(log_message->client_id) - 1; i++) {
        log_message->client_id[i] = ALPHABET[esp_random() % strlen(ALPHABET)];
      }
      log_message->client_id[sizeof(log_message->client_id) - 1] = '\0';
      log_message->device_time = time(NULL);
      log_message->device_time_is_utc = true;
      log_message->has_battery_voltage = true;
      log_message->battery_voltage = battery_voltage;
      log_message->has_usb_voltage = true;
      log_message->usb_voltage = usb_voltage;
      device_id(log_message->device_id);

      char filename[29];  // TODO: use strlen(LOG_DIR)
      sprintf(filename, "%s/%.8s.log", LOG_DIR, log_message->client_id);
      ESP_LOGI(TAG, "Writing log file %s", filename);

      FILE* log_file = fopen(filename, "w");
      if (log_file != NULL) {
        pb_ostream_t file_stream = {
            .callback = write_pb_to_file,
            .state = log_file,
            .max_size = SIZE_MAX,
            .bytes_written = 0,
        };

        // if (pb_encode(&file_stream, LogMessage_fields, log_message)) {
        //   ESP_LOGI(TAG, "Wrote log to %s", filename);
        //   current_state.log_files_to_upload++;
        //   xTaskNotifyGive(xTaskGetHandle("log_uploader"));
        // } else {
        //   ESP_LOGE(TAG, "Failed to write log to %s", filename);
        // }

        fclose(log_file);
      } else {
        ESP_LOGE(TAG, "Failed to open %s for writing", filename);
      }

      vPortFree(log_message);
    }
  }
}
