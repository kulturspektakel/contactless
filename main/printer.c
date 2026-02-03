#include "printer.h"
#include "constants.h"
#include "local_config.h"
#include "esp_log.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "state_machine.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TARGET_DEVICE_NAME "BlueTooth Printer"

typedef enum {
  FONT_1X = 0,
  FONT_2X = 1,
  FONT_3X = 2
} FontSize;
#define STARTUP_RETRY_INTERVAL_MS (15 * 1000)
#define MAX_STARTUP_RETRIES 5

// ISSC BLE module UUIDs (common in 58mm thermal printers like Netum 1809DD)
static const ble_uuid128_t printer_service_uuid = BLE_UUID128_INIT(
    0x55, 0xe4, 0x05, 0xd2, 0xaf, 0x9f, 0xa9, 0x8f,
    0xe5, 0x4a, 0x7d, 0xfe, 0x43, 0x53, 0x53, 0x49
);

static const ble_uuid128_t printer_write_char_uuid = BLE_UUID128_INIT(
    0xb3, 0x9b, 0x72, 0x34, 0xbe, 0xec, 0xd4, 0xa8,
    0xf4, 0x43, 0x41, 0x88, 0x43, 0x53, 0x53, 0x49
);

printer_status_t printer_status = PRINTER_DISCONNECTED;
QueueHandle_t print_queue = NULL;

static ble_addr_t target_addr;
static bool target_found = false;
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t write_char_handle = 0;
static TimerHandle_t reconnect_timer = NULL;
static int retries_remaining = MAX_STARTUP_RETRIES;

static void start_scan(void);
static void connect_to_target(void);
static void ble_host_task(void* param);

static uint8_t get_order_counter(void) {
  nvs_handle_t nvs_handle;
  uint8_t counter = 1;
  if (nvs_open(NVS_DEVICE_CONFIG, NVS_READONLY, &nvs_handle) == ESP_OK) {
    nvs_get_u8(nvs_handle, NVS_ORDER_COUNTER, &counter);
    nvs_close(nvs_handle);
  }
  return counter == 0 ? 1 : counter;
}

static void increment_order_counter(void) {
  nvs_handle_t nvs_handle;
  uint8_t counter = get_order_counter();
  counter = (counter % 99) + 1;  // 1-99, rolls over to 1
  if (nvs_open(NVS_DEVICE_CONFIG, NVS_READWRITE, &nvs_handle) == ESP_OK) {
    nvs_set_u8(nvs_handle, NVS_ORDER_COUNTER, counter);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
  }
}

void init_print_queue(void) {
  print_queue = xQueueCreate(1, sizeof(PrintJob*));
}

static void print_string(const char* str) {
  ble_gattc_write_flat(conn_handle, write_char_handle, (uint8_t*)str, strlen(str), NULL, NULL);
  vTaskDelay(pdMS_TO_TICKS(30));
}

static void print_line(const char* text, FontSize size) {
  // Max chars per line based on font size (58mm printer, 384 dots width)
  int max_chars = (size == FONT_1X) ? 32 : (size == FONT_2X) ? 16 : 10;

  char truncated[33];
  strncpy(truncated, text, max_chars);
  truncated[max_chars] = '\0';

  // Set position to column 0
  uint8_t pos_cmd[] = {0x1B, 0x24, 0x00, 0x00};
  ble_gattc_write_flat(conn_handle, write_char_handle, pos_cmd, sizeof(pos_cmd), NULL, NULL);
  vTaskDelay(pdMS_TO_TICKS(10));

  // Set font size using GS ! (supports 1-8x scaling)
  // GS ! n: bits 0-2 = width-1, bits 4-6 = height-1 (0=1x, 1=2x, 2=3x...)
  uint8_t scale = (uint8_t)size;
  uint8_t font_cmd[] = {0x1D, 0x21, (scale << 4) | scale};
  ble_gattc_write_flat(conn_handle, write_char_handle, font_cmd, sizeof(font_cmd), NULL, NULL);
  vTaskDelay(pdMS_TO_TICKS(10));

  // Print text with newline
  print_string(truncated);
  print_string("\n");

  // Reset to normal font
  uint8_t reset_cmd[] = {0x1D, 0x21, 0x00};
  ble_gattc_write_flat(conn_handle, write_char_handle, reset_cmd, sizeof(reset_cmd), NULL, NULL);
  vTaskDelay(pdMS_TO_TICKS(10));
}

static void get_current_datetime(char* buf, size_t len) {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(buf, len, "%d.%m.%Y %H:%M", &timeinfo);
}

static const char* get_active_list_name(void) {
  for (int i = 0; i < lists_count; i++) {
    if (product_lists[i].id == active_config.list_id) {
      return product_lists[i].name;
    }
  }
  return "";
}

static const char* payment_method_name(LogMessage_Order_PaymentMethod method) {
  switch (method) {
    case LogMessage_Order_PaymentMethod_CASH:
      return "Bar";
    case LogMessage_Order_PaymentMethod_BON:
      return "Bon";
    case LogMessage_Order_PaymentMethod_SUM_UP:
      return "SumUp";
    case LogMessage_Order_PaymentMethod_VOUCHER:
      return "Gutschein";
    case LogMessage_Order_PaymentMethod_FREE_CREW:
      return "Crew";
    case LogMessage_Order_PaymentMethod_FREE_BAND:
      return "Band";
    case LogMessage_Order_PaymentMethod_KULT_CARD:
      return "KultCard";
    default:
      return "Unbekannt";
  }
}

static void print_receipt(PrintJob* job) {
  if (printer_status != PRINTER_CONNECTED || write_char_handle == 0) {
    return;
  }

  // ESC @ - Initialize printer
  uint8_t init_cmd[] = {0x1B, 0x40};
  ble_gattc_write_flat(conn_handle, write_char_handle, init_cmd, sizeof(init_cmd), NULL, NULL);
  vTaskDelay(pdMS_TO_TICKS(50));

  uint8_t order_num = get_order_counter();
  char line[64];
  char datetime[20];
  get_current_datetime(datetime, sizeof(datetime));

  // Header section
  print_line(get_active_list_name(), FONT_2X);
  snprintf(line, sizeof(line), "%02d", order_num);
  print_line(line, FONT_3X);
  print_line(datetime, FONT_1X);

  // Spacing
  print_string("\n\n\n\n\n\n\n");

  // Order number (large)
  snprintf(line, sizeof(line), "%02d", order_num);
  print_line(line, FONT_3X);

  print_string("\n");

  // Cart items
  for (int i = 0; i < job->item_count; i++) {
    if (!job->items[i].has_product) {
      continue;
    }
    snprintf(line, sizeof(line), "%d %s",
             (int)job->items[i].amount,
             job->items[i].product.name);
    print_line(line, FONT_2X);
  }

  print_string("\n");

  // Footer
  snprintf(line, sizeof(line), "Bezahlung: %s", payment_method_name(job->payment_method));
  print_line(line, FONT_1X);
  print_line(datetime, FONT_1X);

  increment_order_counter();

  // Feed paper
  print_string("\n\n\n");

  ESP_LOGI(PRINTER_TASK, "Receipt printed");
}

static void process_print_queue(void) {
  if (print_queue == NULL) {
    return;
  }

  PrintJob* job = NULL;
  if (xQueueReceive(print_queue, &job, 0) == pdTRUE && job != NULL) {
    if (printer_status == PRINTER_CONNECTED && write_char_handle != 0) {
      print_receipt(job);
    }
    vPortFree(job);
  }
}

void submit_print_job(LogMessage_Order_PaymentMethod payment) {
  if (print_queue == NULL) {
    return;
  }

  PrintJob* job = pvPortMalloc(sizeof(PrintJob));
  if (job == NULL) {
    return;
  }

  job->payment_method = payment;
  job->item_count = current_state.cart.item_count;
  job->total = current_total();
  for (int i = 0; i < current_state.cart.item_count; i++) {
    job->items[i] = current_state.cart.items[i];
  }

  if (xQueueSendFromISR(print_queue, &job, NULL) != pdTRUE) {
    vPortFree(job);
  }
}

static void reconnect_timer_cb(TimerHandle_t timer) {
  if (target_found) {
    connect_to_target();
  } else {
    start_scan();
  }
}

static void stop_bluetooth(void) {
  ble_gap_disc_cancel();
  nimble_port_stop();
  nimble_port_deinit();
  ESP_LOGI(PRINTER_TASK, "Bluetooth disabled to save energy");
}

static void start_bluetooth(void) {
  if (ble_hs_is_enabled()) {
    return;
  }
  ESP_ERROR_CHECK(nimble_port_init());
  ble_hs_cfg.sync_cb = NULL;
  nimble_port_freertos_init(ble_host_task);
  ESP_LOGI(PRINTER_TASK, "Bluetooth re-enabled");
}

static void schedule_reconnect(void) {
  if (retries_remaining <= 0) {
    ESP_LOGI(PRINTER_TASK, "No retries remaining, waiting for manual trigger");
    stop_bluetooth();
    return;
  }

  retries_remaining--;
  ESP_LOGI(PRINTER_TASK, "Scheduling reconnect, %d retries remaining", retries_remaining);

  if (!reconnect_timer) {
    reconnect_timer =
        xTimerCreate("printer_reconnect", pdMS_TO_TICKS(STARTUP_RETRY_INTERVAL_MS), pdFALSE, NULL, reconnect_timer_cb);
  }
  xTimerStart(reconnect_timer, 0);
}

static char* extract_device_name(const struct ble_gap_disc_desc* disc) {
  struct ble_hs_adv_fields fields;
  static char name_buf[32];

  if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) {
    return NULL;
  }
  if (fields.name != NULL && fields.name_len > 0) {
    int len = fields.name_len < sizeof(name_buf) - 1 ? fields.name_len : sizeof(name_buf) - 1;
    memcpy(name_buf, fields.name, len);
    name_buf[len] = '\0';
    return name_buf;
  }
  return NULL;
}

static int gatt_chr_disc_cb(
    uint16_t cb_conn_handle,
    const struct ble_gatt_error* error,
    const struct ble_gatt_chr* chr,
    void* arg
) {
  if (error->status == 0 && chr != NULL) {
    write_char_handle = chr->val_handle;
    ESP_LOGI(PRINTER_TASK, "Found write characteristic, handle=%d", write_char_handle);
  }
  return 0;
}

static int gatt_svc_disc_cb(
    uint16_t cb_conn_handle,
    const struct ble_gatt_error* error,
    const struct ble_gatt_svc* service,
    void* arg
) {
  if (error->status == 0 && service != NULL) {
    ble_gattc_disc_chrs_by_uuid(
        cb_conn_handle,
        service->start_handle,
        service->end_handle,
        &printer_write_char_uuid.u,
        gatt_chr_disc_cb,
        NULL
    );
  }
  return 0;
}

static int gap_event_cb(struct ble_gap_event* event, void* arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
      char* name = extract_device_name(&event->disc);
      if (name) {
        ESP_LOGI(PRINTER_TASK, "Found: %s (RSSI=%d)", name, event->disc.rssi);
        if (strcmp(name, TARGET_DEVICE_NAME) == 0) {
          ESP_LOGI(PRINTER_TASK, "Target printer found!");
          target_found = true;
          memcpy(&target_addr, &event->disc.addr, sizeof(target_addr));
          connect_to_target();
        }
      }
      break;
    }

    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        conn_handle = event->connect.conn_handle;
        printer_status = PRINTER_CONNECTED;
        write_char_handle = 0;
        retries_remaining = MAX_STARTUP_RETRIES;
        ESP_LOGI(PRINTER_TASK, "Connected to printer");
        xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
        if (reconnect_timer) {
          xTimerStop(reconnect_timer, 0);
        }
        ble_gattc_disc_svc_by_uuid(conn_handle, &printer_service_uuid.u, gatt_svc_disc_cb, NULL);
      } else {
        printer_status = PRINTER_DISCONNECTED;
        xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
        schedule_reconnect();
      }
      break;

    case BLE_GAP_EVENT_DISCONNECT:
      printer_status = PRINTER_DISCONNECTED;
      conn_handle = BLE_HS_CONN_HANDLE_NONE;
      ESP_LOGI(PRINTER_TASK, "Disconnected");
      xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
      if (retries_remaining > 0) {
        connect_to_target();
      }
      break;

    default:
      break;
  }

  return 0;
}

static void start_scan(void) {
  printer_status = PRINTER_SCANNING;
  struct ble_gap_disc_params params = {0};
  params.passive = 0;
  params.filter_duplicates = 1;

  uint8_t own_addr_type;
  ble_hs_id_infer_auto(0, &own_addr_type);

  int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
  if (rc != 0) {
    ESP_LOGE(PRINTER_TASK, "Failed to start scan: %d", rc);
    schedule_reconnect();
  } else {
    ESP_LOGI(PRINTER_TASK, "Scanning for printer...");
  }
}

static void connect_to_target(void) {
  printer_status = PRINTER_CONNECTING;
  ble_gap_disc_cancel();

  uint8_t own_addr_type;
  ble_hs_id_infer_auto(0, &own_addr_type);

  int rc = ble_gap_connect(own_addr_type, &target_addr, 10000, NULL, gap_event_cb, NULL);
  if (rc != 0) {
    ESP_LOGE(PRINTER_TASK, "Failed to initiate connection: %d", rc);
    printer_status = PRINTER_DISCONNECTED;
    schedule_reconnect();
  }
}

void printer_start_scan(int retries) {
  retries_remaining = retries;
  start_bluetooth();

  if (target_found) {
    connect_to_target();
  } else {
    start_scan();
  }
  xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
}

static void on_ble_sync(void) {
  ble_hs_util_ensure_addr(0);
  printer_start_scan(MAX_STARTUP_RETRIES);
}

static void ble_host_task(void* param) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

void printer(void* params) {
  ESP_ERROR_CHECK(nimble_port_init());
  ble_hs_cfg.sync_cb = on_ble_sync;
  nimble_port_freertos_init(ble_host_task);

  while (1) {
    process_print_queue();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
