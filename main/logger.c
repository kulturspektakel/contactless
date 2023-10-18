#include "logger.h"
#include <dirent.h>
#include "device_id.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_uploader.h"
#include "nanopb/pb_encode.h"

static const char* TAG = "logger";
static const char* ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const char* LOG_DIR = "/littlefs/logs";

LogMessage log_message = LogMessage_init_default;

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

void write_log() {
  for (int i = 0; i < sizeof(log_message.client_id) - 1; i++) {
    log_message.client_id[i] = ALPHABET[esp_random() % strlen(ALPHABET)];
  }
  log_message.client_id[sizeof(log_message.client_id) - 1] = '\0';
  log_message.device_time = 1;            // TODO
  log_message.device_time_is_utc = true;  // TODO
  device_id(log_message.device_id);

  char filename[29];
  sprintf(filename, "%s/%.8s.log", LOG_DIR, log_message.client_id);
  ESP_LOGI(TAG, "Write log file %s", filename);

  FILE* log_file = fopen(filename, "w");
  if (log_file != NULL) {
    pb_ostream_t file_stream = {
        .callback = write_pb_to_file,
        .state = log_file,
        .max_size = SIZE_MAX,
        .bytes_written = 0,
    };

    if (pb_encode(&file_stream, LogMessage_fields, &log_message)) {
      ESP_LOGI(TAG, "Wrote log to %s", filename);
      log_count++;
      xTaskNotifyGive(xTaskGetHandle("log_uploader"));
      LogMessage l = LogMessage_init_default;
      log_message = l;
    } else {
      ESP_LOGE(TAG, "Failed to write log to %s", filename);
    }

    fclose(log_file);
  } else {
    ESP_LOGE(TAG, "Failed to open %s for writing", filename);
  }
}

void logger(void* params) {}