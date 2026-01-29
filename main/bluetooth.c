#include "bluetooth.h"
#include "constants.h"
#include "esp_nimble_hci.h"
#include "event_group.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

// Service and characteristic UUIDs
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(
    0xbc,
    0x9a,
    0x78,
    0x56,
    0x34,
    0x12,
    0x34,
    0x12,
    0x34,
    0x12,
    0x34,
    0x12,
    0x78,
    0x56,
    0x34,
    0x12
);

static const ble_uuid128_t char_uuid = BLE_UUID128_INIT(
    0x21,
    0x43,
    0x65,
    0x87,
    0xa9,
    0xcb,
    0x21,
    0x43,
    0x21,
    0x43,
    0x21,
    0x43,
    0x21,
    0x43,
    0x65,
    0x87
);

static uint16_t char_val_handle;
static char char_value[64] = "Hello World";

// Characteristic access callback
static int char_access_cb(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt* ctxt,
    void* arg
) {
  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
      ESP_LOGI(BLUETOOTH_TASK, "Characteristic read");
      os_mbuf_append(ctxt->om, char_value, strlen(char_value));
      return 0;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
      ESP_LOGI(BLUETOOTH_TASK, "Characteristic write");
      uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
      if (len >= sizeof(char_value)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }
      os_mbuf_copydata(ctxt->om, 0, len, char_value);
      char_value[len] = '\0';
      ESP_LOGI(BLUETOOTH_TASK, "New value: %s", char_value);
      return 0;

    default:
      return BLE_ATT_ERR_UNLIKELY;
  }
}

// GATT service definition
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &service_uuid.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {
                 .uuid = &char_uuid.u,
                 .access_cb = char_access_cb,
                 .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                 .val_handle = &char_val_handle,
             },
             {0}  // Terminate array
         }},
    {0}  // Terminate array
};

// GAP event handler
static int gap_event_cb(struct ble_gap_event* event, void* arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      ESP_LOGI(
          BLUETOOTH_TASK,
          "Connection %s; status=%d",
          event->connect.status == 0 ? "established" : "failed",
          event->connect.status
      );
      break;

    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(BLUETOOTH_TASK, "Disconnect; reason=%d", event->disconnect.reason);
      // Restart advertising
      ble_gap_adv_start(
          BLE_OWN_ADDR_PUBLIC,
          NULL,
          BLE_HS_FOREVER,
          &(struct ble_gap_adv_params){},
          gap_event_cb,
          NULL
      );
      break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
      ESP_LOGI(BLUETOOTH_TASK, "Advertise complete; reason=%d", event->adv_complete.reason);
      break;

    default:
      break;
  }
  return 0;
}

// Start advertising
static void start_advertising(void) {
  struct ble_gap_adv_params adv_params;
  struct ble_hs_adv_fields fields;
  int rc;

  memset(&fields, 0, sizeof(fields));

  // Set device name (shortened to fit with UUID)
  EventBits_t bits = xEventGroupGetBits(event_group);
  if (bits & DEVICE_ID_LOADED) {
    fields.name = (uint8_t*)DEVICE_ID;
    fields.name_len = strlen(DEVICE_ID);
    fields.name_is_complete = 1;
  }

  // Set service UUID
  fields.uuids128 = &service_uuid;
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;

  rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(BLUETOOTH_TASK, "Error setting advertisement fields; rc=%d", rc);
    return;
  }

  // Start advertising
  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  rc =
      ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, gap_event_cb, NULL);
  if (rc != 0) {
    ESP_LOGE(BLUETOOTH_TASK, "Error enabling advertisement; rc=%d", rc);
    return;
  }

  ESP_LOGI(BLUETOOTH_TASK, "Advertising started");
}

// NimBLE host configuration callback
static void on_ble_sync(void) {
  int rc;

  rc = ble_hs_util_ensure_addr(0);
  assert(rc == 0);

  start_advertising();
}

void ble_host_task(void* param) {
  nimble_port_run();  // This function will return only when nimble_port_stop() is executed.
  nimble_port_freertos_deinit();
}

void bluetooth(void* params) {
  ESP_ERROR_CHECK(nimble_port_init());
  ble_hs_cfg.sync_cb = on_ble_sync;
  ble_hs_cfg.reset_cb = NULL;

  // Register GATT services
  int rc = ble_gatts_count_cfg(gatt_svcs);
  if (rc != 0) {
    ESP_LOGE(BLUETOOTH_TASK, "Error counting GATT services; rc=%d", rc);
    return;
  }

  rc = ble_gatts_add_svcs(gatt_svcs);
  if (rc != 0) {
    ESP_LOGE(BLUETOOTH_TASK, "Error adding GATT services; rc=%d", rc);
    return;
  }

  nimble_port_freertos_init(ble_host_task);
  vTaskDelete(NULL);
}