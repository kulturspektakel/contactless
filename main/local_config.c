#include "local_config.h"
#include "configs.pb.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kvec.h"
#include "nvs_flash.h"
#include "pb_decode.h"

static const char* TAG = "local_config";
DeviceConfig active_config = DeviceConfig_init_default;
int32_t all_lists_checksum = -1;

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
  ESP_ERROR_CHECK(nvs_open("device_config", NVS_READONLY, &nvs_handle));
  int32_t product_list_id;
  if (nvs_get_i32(nvs_handle, "product_list", &product_list_id) != ESP_OK) {
    ESP_LOGE(TAG, "failed to read product list id");
    product_list_id = -1;
  }
  nvs_close(nvs_handle);
  return product_list_id;
}

static bool load_active_product_list(pb_istream_t* stream, const pb_field_t* field, void** arg) {
  DeviceConfig product_list = DeviceConfig_init_default;
  if (!pb_decode(stream, DeviceConfig_fields, &product_list)) {
    ESP_LOGE(TAG, "failed to decode product list");
    return false;
  }

  int32_t* argg = *(int32_t**)arg;
  if (product_list.list_id == *argg) {
    ESP_LOGI(TAG, "Loaded product list: %s", product_list.name);
    active_config = product_list;
  }
  pb_release(DeviceConfig_fields, &product_list);
  return true;
}

static bool load_available_product_lists(
    pb_istream_t* stream,
    const pb_field_t* field,
    void** arg
) {
  DeviceConfig product_list = DeviceConfig_init_default;
  if (!pb_decode(stream, DeviceConfig_fields, &product_list)) {
    ESP_LOGE(TAG, "failed to decode product list");
    return false;
  }

  menu_item_t menu_item;
  menu_item.list_id = product_list.list_id;
  strcpy(menu_item.name, product_list.name);

  kvec_t(menu_item_t)* items = (kvec_t(menu_item_t)*)*arg;
  kv_push(menu_item_t, *items, menu_item);

  pb_release(DeviceConfig_fields, &product_list);
  return true;
}

static AllLists read_local_config(pb_callback_t callback) {
  FILE* config_file = fopen("/littlefs/config.cfg", "r");
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
    ESP_LOGE(TAG, "failed to open config file");
  }
  return all_lists;
}

void initialize_main_menu() {
  kvec_t(menu_item_t) array;
  kv_init(array);
  pb_callback_t callback = {.funcs.decode = load_available_product_lists, .arg = &array};
  read_local_config(callback);
  ESP_LOGI(TAG, "Loading available product lists %d", array.n);
}

void local_config(void* params) {
  while (1) {
    int32_t product_list_id = read_product_list_id();
    pb_callback_t callback = {.funcs.decode = load_active_product_list, .arg = &product_list_id};
    AllLists all_lists = read_local_config(callback);
    all_lists_checksum = all_lists.checksum;

    initialize_main_menu();

    xEventGroupClearBits(event_group, LOCAL_CONFIG_UPDATED);
    xEventGroupSetBits(event_group, LOCAL_CONFIG_LOADED);
    xEventGroupWaitBits(event_group, LOCAL_CONFIG_UPDATED, pdTRUE, pdTRUE, portMAX_DELAY);
  }
  vTaskDelete(NULL);
}
