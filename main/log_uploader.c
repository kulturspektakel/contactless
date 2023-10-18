#include "log_uploader.h"
#include <dirent.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_auth_headers.h"

static const char* TAG = "log_uploader";
static const char* LOG_DIR = "/littlefs/logs";
int log_count = -1;

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
      .host = "api.kulturspektakel.de",
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
  char* buffer = malloc(file_size);
  fread(buffer, file_size, 1, f);
  fclose(f);

  esp_http_client_handle_t client = esp_http_client_init(&config);
  http_auth_headers(client);
  esp_http_client_set_post_field(client, buffer, file_size);
  esp_err_t err = esp_http_client_perform(client);
  free(buffer);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return HTTP_ISSUE;
  }

  int status_code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (status_code == 201 || status_code == 409) {
    ESP_LOGI(TAG, "Log received: %d", status_code);
    remove(filename);
    return FILE_HANDLED;
  } else if (status_code == 400) {
    ESP_LOGE(TAG, "Bad request, delete log");
    remove(filename);
    return FILE_HANDLED;
  } else {
    ESP_LOGE(TAG, "Some problem (%d): back off", status_code);
    return FILE_SKIPPED;
  }
}

void log_uploader(void* params) {
  // initial value
  log_count = count_logs();
  ESP_LOGI(TAG, "Found %d logs", log_count);

  while (1) {
    xEventGroupWaitBits(event_group, WIFI_CONNECTED, pdFALSE, pdTRUE, portMAX_DELAY);

    int consecutive_error_count = 0;
    DIR* dir = opendir(LOG_DIR);

    if (dir == NULL) {
      // create directory if it doesn't exist
      ESP_LOGI(TAG, "Creating directory %s", LOG_DIR);
      if (mkdir(LOG_DIR, 0777) == 0) {
        continue;
      }
      ESP_LOGE(TAG, "Failed to create directory");
      // TODO this is a fatal error
      vTaskDelete(NULL);
      return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir))) {
      if (entry->d_type != DT_REG) {
        // skip non-regular files (e.g. directories)
        continue;
      }

      char filename[29];
      sprintf(filename, "%s/%.12s", LOG_DIR, entry->d_name);
      log_uploader_event_t status = upload_file(filename);

      switch (status) {
        case FILE_SKIPPED:
          consecutive_error_count++;
          break;
        case FILE_HANDLED:
          log_count--;
          consecutive_error_count = 0;
          remove(filename);
          break;
        case HTTP_ISSUE:
          // force starting over
          consecutive_error_count = 3;
          break;
      }

      if (consecutive_error_count > 2) {
        ESP_LOGE(TAG, "Too many consecutive errors");
        break;
      }
    }
    closedir(dir);

    if (consecutive_error_count > 2) {
      // stopped because of too many errors, retry after 2 minutes
      vTaskDelay(120000 / portTICK_PERIOD_MS);
    } else if (log_count == 0) {
      // no logs left, wait for new ones
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    // else: there are still logs left, try again immediately
  }
}
