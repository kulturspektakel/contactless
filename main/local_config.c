#include "local_config.h"
#include "configs.pb.h"
#include "constants.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "pb_decode.h"

static const char* TAG = "local_config";
int32_t lists_count = 0;
DeviceConfig active_config = DeviceConfig_init_default;
AllLists_privilege_tokens_t privilege_tokens[MAX_PRIVILEGE_TOKENS];
int32_t all_lists_checksum = -1;
product_list_t* product_lists = NULL;

bool pb_from_file_stream(pb_istream_t* stream, uint8_t* buffer, size_t count) {
  FILE* file = (FILE*)stream->state;
  int ret = fread(buffer, count, 1, file);
  if (ret < 0) {
    return false;
  } else if (ret == 0) {
    stream->bytes_left = 0;
    return false;
  }
  return true;
}

int32_t read_product_list_id() {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(NVS_DEVICE_CONFIG, NVS_READONLY, &nvs_handle));
  int32_t product_list_id;
  if (nvs_get_i32(nvs_handle, NVS_PRODUCT_LIST, &product_list_id) != ESP_OK) {
    ESP_LOGE(TAG, "failed to read product list id");
    product_list_id = -1;
  }
  nvs_close(nvs_handle);
  return product_list_id;
}

static bool decode_product_list(pb_istream_t* stream, const pb_field_t* field, void** arg) {
  DeviceConfig product_list = DeviceConfig_init_default;
  if (!pb_decode(stream, DeviceConfig_fields, &product_list)) {
    ESP_LOGE(TAG, "failed to decode product list");
    return false;
  }
  lists_count++;

  if (product_list.list_id == (*(*(int32_t**)arg))) {
    ESP_LOGI(TAG, "Loaded product list: %s", product_list.name);
    active_config = product_list;
  }

  product_list_t* new_product_lists =
      (product_list_t*)pvPortMalloc(lists_count * sizeof(product_list_t));
  if (product_lists != NULL) {
    memcpy(new_product_lists, product_lists, (lists_count - 1) * sizeof(product_list_t));
    vPortFree(product_lists);
  }
  product_lists = new_product_lists;

  product_lists[lists_count - 1] = (product_list_t){
      .id = product_list.list_id,
  };
  strncpy(product_lists[lists_count - 1].name, product_list.name, MAX_LIST_NAME_LENGTH);

  pb_release(DeviceConfig_fields, &product_list);
  return true;
}

static AllLists read_local_config(pb_callback_t callback) {
  FILE* config_file = fopen(CONFIG_FILE, "r");
  AllLists all_lists = AllLists_init_default;
  if (config_file != NULL) {
    pb_istream_t file_stream = {
        .callback = pb_from_file_stream,
        .state = config_file,
        .bytes_left = SIZE_MAX,
    };

    all_lists.product_list = callback;

    if (pb_decode(&file_stream, AllLists_fields, &all_lists)) {
      all_lists_checksum = all_lists.checksum;
      pb_release(AllLists_fields, &all_lists);
    } else {
      ESP_LOGE(TAG, "failed to decode protobuf: %s", file_stream.errmsg);
    }
    fclose(config_file);
  } else {
    ESP_LOGW(TAG, "failed to open config file");
  }
  return all_lists;
}

void select_list(int list_id) {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(NVS_DEVICE_CONFIG, NVS_READWRITE, &nvs_handle));
  ESP_ERROR_CHECK(nvs_set_i32(nvs_handle, NVS_PRODUCT_LIST, list_id));
  ESP_ERROR_CHECK(nvs_commit(nvs_handle));
  nvs_close(nvs_handle);
  xEventGroupSetBits(event_group, LOCAL_CONFIG_UPDATED);
}

void local_config(void* params) {
  while (1) {
    int32_t product_list_id = read_product_list_id();
    active_config.list_id = -1;

    if (product_lists != NULL) {
      vPortFree(product_lists);
    }

    // load active product list, products and privilege tokens
    AllLists all_lists = read_local_config((pb_callback_t){
        .funcs.decode = decode_product_list,
        .arg = &product_list_id,
    });
    all_lists_checksum = all_lists.checksum;
    memcpy(privilege_tokens, all_lists.privilege_tokens, sizeof(privilege_tokens));

    if (active_config.list_id == -1 && lists_count > 0) {
      ESP_LOGI(TAG, "No product list selected, selecting first list");
      select_list(product_lists[0].id);
      continue;
    }

    xEventGroupClearBits(event_group, LOCAL_CONFIG_UPDATED);
    if (active_config.list_id > -1) {
      xEventGroupSetBits(event_group, LOCAL_CONFIG_LOADED | DISPLAY_NEEDS_UPDATE);
    }
    xEventGroupWaitBits(event_group, LOCAL_CONFIG_UPDATED, pdTRUE, pdTRUE, portMAX_DELAY);
  }
  vTaskDelete(NULL);
}
