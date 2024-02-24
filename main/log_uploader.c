#include "log_uploader.h"
#include <dirent.h>
#include "constants.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_auth_headers.h"
#include "state_machine.h"

static const char* TAG = "log_uploader";
#define MAX_ERRORS 3

int count_logs() {
  DIR* dir = opendir(LOG_DIR);

  if (dir == NULL) {
    ESP_LOGE(TAG, "Failed to open directory");
    return -1;
  }

  struct dirent* entry;
  int count = 0;
  while ((entry = readdir(dir))) {
    if (entry->d_type == DT_REG) {
      count++;
    }
  }
  closedir(dir);
  return count;
}

typedef enum {
  FILE_HANDLED,
  FILE_SKIPPED,
  HTTP_ISSUE,
} log_uploader_event_t;

log_uploader_event_t upload_file(char* filename) {
  ESP_LOGI(TAG, "Uploading %s", filename);
  FILE* f = fopen(filename, "r");

  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open %s for reading", filename);
    return FILE_SKIPPED;
  }

  esp_http_client_config_t config = {
      .host = API_HOST,
      .transport_type = HTTP_TRANSPORT_OVER_SSL,
      .path = "/$$$/log",
      .method = HTTP_METHOD_POST,
  };

  fseek(f, 0, SEEK_END);
  int file_size = ftell(f);
  if (file_size < 1) {
    ESP_LOGE(TAG, "File %s is corrupt. Deleting it.", filename);
    fclose(f);
    remove(filename);
    return FILE_HANDLED;
  }
  fseek(f, 0, SEEK_SET);
  char* buffer = pvPortMalloc(file_size);
  fread(buffer, file_size, 1, f);
  fclose(f);

  esp_http_client_handle_t client = esp_http_client_init(&config);
  http_auth_headers(client);
  esp_http_client_set_post_field(client, buffer, file_size);
  esp_err_t err = esp_http_client_perform(client);
  vPortFree(buffer);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Request failed (Error: %s)", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return HTTP_ISSUE;
  }

  int status_code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (status_code == 201 || status_code == 409) {
    ESP_LOGI(TAG, "Upload successful (HTTP %d), deleting log file %s", status_code, filename);
    return FILE_HANDLED;
  } else if (status_code == 400) {
    ESP_LOGE(TAG, "Bad request (HTTP 400), deleting log file %s", filename);
    return FILE_HANDLED;
  } else {
    ESP_LOGE(TAG, "Server error (HTTP %d), skipping file %s", status_code, filename);
    return FILE_SKIPPED;
  }
}

void maybe_create_log_dir() {
  DIR* dir = opendir(LOG_DIR);
  if (dir == NULL) {
    // create directory if it doesn't exist
    ESP_LOGI(TAG, "Creating directory %s", LOG_DIR);
    if (mkdir(LOG_DIR, 0777) != 0) {
      ESP_LOGE(TAG, "Failed to create directory");
      // TODO this is a fatal error
      vTaskDelete(NULL);
      return;
    }
  }
  closedir(dir);
}

static void retry_upload(TimerHandle_t xTimer) {
  xTaskNotifyGive(xTaskGetHandle("log_uploader"));
}

void log_uploader(void* params) {
  TimerHandle_t retry_timer = NULL;
  maybe_create_log_dir();

  // initial value
  current_state.log_files_to_upload = count_logs();
  xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
  ESP_LOGI(TAG, "Found %d logs", current_state.log_files_to_upload);

  while (1) {
    xEventGroupWaitBits(event_group, WIFI_CONNECTED, pdFALSE, pdTRUE, portMAX_DELAY);
    int error_count = 0;
    DIR* dir = opendir(LOG_DIR);
    struct dirent* entry;
    while ((entry = readdir(dir))) {
      if (entry->d_type != DT_REG) {
        // skip non-regular files (e.g. directories)
        continue;
      }

      char filename[29];
      sprintf(filename, "%s/%.12s", LOG_DIR, entry->d_name);
      ESP_LOGI(TAG, "Found log file %s", filename);
      log_uploader_event_t status = upload_file(filename);

      switch (status) {
        case FILE_SKIPPED:
          error_count++;
          break;
        case FILE_HANDLED:
          if (remove(filename) == 0) {
            current_state.log_files_to_upload--;
            if (current_state.log_files_to_upload < 0) {
              current_state.log_files_to_upload = 0;
            }
            xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
          }
          break;
        case HTTP_ISSUE:
          // force starting over
          error_count = MAX_ERRORS;
          break;
      }

      if (error_count >= MAX_ERRORS) {
        ESP_LOGE(TAG, "Stopping log uploader after %d error(s)", error_count);
        break;
      }
    }
    closedir(dir);

    if (error_count > 0) {
      // retry in 5 minutes
      if (retry_timer == NULL) {
        retry_timer =
            xTimerCreate("retry_timer", pdMS_TO_TICKS(300000), pdFALSE, NULL, retry_upload);
      }
      ESP_LOGI(
          TAG, "Encountered %d errors while uploading. Scheduling retry in 5 Minutes", error_count
      );
      xTimerReset(retry_timer, 0);
    }

    // waiting for new log files to be written or the retry timer to fire
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
}
