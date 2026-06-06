#include "printer.h"
#include "constants.h"
#include "local_config.h"
#include "esp_log.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "network_request.h"
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
#define SCAN_DURATION_MS 5000

// BLE operation state machine — callbacks set _REQUESTED states,
// printer task transitions to _ING after taking semaphore,
// callbacks transition away from _ING when operations complete.
typedef enum {
  BLE_OP_IDLE,
  BLE_OP_SCAN_REQUESTED,
  BLE_OP_SCANNING,
  BLE_OP_CONNECT_REQUESTED,
  BLE_OP_CONNECTING,
} ble_op_t;

// ISSC BLE module UUIDs (common in 58mm thermal printers like Netum 1809DD)
static const ble_uuid128_t printer_service_uuid = BLE_UUID128_INIT(
    0x55, 0xe4, 0x05, 0xd2, 0xaf, 0x9f, 0xa9, 0x8f,
    0xe5, 0x4a, 0x7d, 0xfe, 0x43, 0x53, 0x53, 0x49
);

// Custom 16-bit service exposed by these printers (GTW HS6622S and family).
// More likely than the 128-bit UUID to actually fit in the 31-byte adv packet.
static const ble_uuid16_t printer_service_uuid16 = BLE_UUID16_INIT(0x18F0);

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
static TaskHandle_t printer_task_handle = NULL;
static volatile ble_op_t ble_op = BLE_OP_IDLE;
// Set true once the printer task has run nimble_port_init(). Until then no BLE
// host API may be called — the menu is live well before the printer task
// finishes its startup wait, and calling into the uninitialized NimBLE host
// (e.g. ble_hs_is_enabled) faults. See printer_start_scan().
static volatile bool nimble_ready = false;
// Write-with-response flow control: printer_write() blocks on this until the
// GATT write completes, so we never issue the next ATT request before the
// previous one is acknowledged. A dedicated semaphore (not the task
// notification used for the scan/connect FSM) avoids any cross-signal.
static SemaphoreHandle_t write_sem = NULL;
static volatile int write_status = 0;

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
  write_sem = xSemaphoreCreateBinary();
}

// GATT write-completion callback. Runs in the NimBLE host context, so it must
// not log or touch flash-mapped .rodata (see the BLE-callback rule in
// CLAUDE.md) — it only records the status and releases printer_write().
static int write_done_cb(
    uint16_t conn_handle_cb,
    const struct ble_gatt_error* error,
    struct ble_gatt_attr* attr,
    void* arg
) {
  write_status = error->status;  // ble_gattc_error() always passes non-NULL
  xSemaphoreGive(write_sem);
  return 0;
}

// Issue one GATT write (Write Request) and block until the peer acknowledges it
// before returning. This is the flow control the printer needs: ATT allows only
// one outstanding request per connection, so firing writes back-to-back (the
// old fixed-vTaskDelay approach) dropped commands/characters whenever a prior
// write hadn't completed yet — especially under WiFi+BT coex. Returns false if
// the write can't be initiated, times out, or the peer reports an error.
static bool printer_write(const uint8_t* data, uint16_t len) {
  if (printer_status != PRINTER_CONNECTED || write_char_handle == 0) {
    return false;
  }

  for (int attempt = 0; attempt < 3; attempt++) {
    xSemaphoreTake(write_sem, 0);  // drain any stale completion before issuing
    int rc = ble_gattc_write_flat(conn_handle, write_char_handle, data, len, write_done_cb, NULL);
    if (rc != 0) {
      // Transient resource shortage (no proc/mbuf); let buffers free and retry.
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (xSemaphoreTake(write_sem, pdMS_TO_TICKS(2000)) != pdTRUE) {
      return false;  // no completion — connection likely gone
    }
    if (write_status == 0) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(20));  // peer returned an ATT error; retry
  }
  return false;
}

static void print_string(const char* str) {
  printer_write((const uint8_t*)str, strlen(str));
}

static void print_line(const char* text, FontSize size) {
  // Max chars per line based on font size (58mm printer, 384 dots width)
  int max_chars = (size == FONT_1X) ? 32 : (size == FONT_2X) ? 16 : 10;
  uint8_t scale = (uint8_t)size;

  // Assemble the whole line — position + font-size command, text, newline,
  // font reset — into one ATT write. Fewer round trips than separate writes,
  // and the printer consumes it as a single ESC/POS byte stream. Max content is
  // 4 + 3 + 32 + 1 + 3 = 43 bytes, well under the negotiated MTU.
  uint8_t buf[64];
  int n = 0;
  buf[n++] = 0x1B; buf[n++] = 0x24; buf[n++] = 0x00; buf[n++] = 0x00;  // ESC $ : column 0
  buf[n++] = 0x1D; buf[n++] = 0x21; buf[n++] = (scale << 4) | scale;   // GS ! : font size
  for (int i = 0; i < max_chars && text[i] != '\0'; i++) {
    buf[n++] = (uint8_t)text[i];
  }
  buf[n++] = '\n';
  buf[n++] = 0x1D; buf[n++] = 0x21; buf[n++] = 0x00;  // GS ! 0 : reset to 1x

  printer_write(buf, n);
}

static void get_datetime(time_t when, char* buf, size_t len) {
  struct tm timeinfo;
  localtime_r(&when, &timeinfo);
  strftime(buf, len, "%d.%m.%Y %H:%M", &timeinfo);
}

static void get_current_datetime(char* buf, size_t len) {
  get_datetime(time(NULL), buf, len);
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

  // ESC @ - Initialize printer, then let it settle after the reset.
  uint8_t init_cmd[] = {0x1B, 0x40};
  printer_write(init_cmd, sizeof(init_cmd));
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

// Print one product row at 2x font (16 cols). The number takes a 3-col prefix
// ("%2d ", space-padded so single digits align under double digits), leaving 13
// cols for the name. Names longer than that wrap onto continuation lines with a
// 3-space indent so the wrapped text lines up under the name. Breaks on spaces,
// but hard-breaks a single word that is itself wider than the field.
static void print_product_row(int number, const char* name) {
  const int field = 13;  // 16 cols (FONT_2X) minus the 3-col prefix
  char line[20];
  bool first = true;
  size_t pos = 0;
  size_t len = strlen(name);

  while (pos < len) {
    // Skip a single leading space left by the previous wrap point.
    if (!first && name[pos] == ' ') {
      pos++;
      if (pos >= len) {
        break;
      }
    }

    size_t remaining = len - pos;
    size_t take = remaining < (size_t)field ? remaining : (size_t)field;

    // If we're mid-name and breaking inside a word, back up to the last space
    // in this chunk so we wrap on a word boundary (unless the word is too long).
    if (take < remaining) {
      size_t brk = take;
      while (brk > 0 && name[pos + brk] != ' ') {
        brk--;
      }
      if (brk > 0) {
        take = brk;
      }
    }

    if (first) {
      snprintf(line, sizeof(line), "%2d %.*s", number, (int)take, name + pos);
    } else {
      snprintf(line, sizeof(line), "   %.*s", (int)take, name + pos);
    }
    print_line(line, FONT_2X);

    pos += take;
    first = false;
  }

  // A product with an empty name still gets its number on a line.
  if (first) {
    snprintf(line, sizeof(line), "%2d", number);
    print_line(line, FONT_2X);
  }
}

static void print_config(void) {
  if (printer_status != PRINTER_CONNECTED || write_char_handle == 0) {
    return;
  }

  // ESC @ - Initialize printer, then let it settle after the reset.
  uint8_t init_cmd[] = {0x1B, 0x40};
  printer_write(init_cmd, sizeof(init_cmd));
  vTaskDelay(pdMS_TO_TICKS(50));

  // Header: list name
  print_line(get_active_list_name(), FONT_2X);
  print_string("\n");

  // One row per product, numbered 1..N (index + 1, matching the UI).
  for (int i = 0; i < active_config.products_count; i++) {
    print_product_row(i + 1, active_config.products[i].name);
  }

  print_string("\n");

  // Footer (1x): the list's last-updated time if known, else the printout time.
  char line[40];
  char datetime[20];
  if (config_timestamp > 0) {
    get_datetime((time_t)config_timestamp, datetime, sizeof(datetime));
    snprintf(line, sizeof(line), "Stand: %s", datetime);
  } else {
    get_current_datetime(datetime, sizeof(datetime));
    snprintf(line, sizeof(line), "Gedruckt: %s", datetime);
  }
  print_line(line, FONT_1X);

  // Feed paper
  print_string("\n\n\n");

  ESP_LOGI(PRINTER_TASK, "Config list printed");
}

static void process_print_queue(void) {
  if (print_queue == NULL) {
    return;
  }

  PrintJob* job = NULL;
  if (xQueueReceive(print_queue, &job, 0) == pdTRUE && job != NULL) {
    if (printer_status == PRINTER_CONNECTED && write_char_handle != 0) {
      if (job->type == PRINT_JOB_CONFIG) {
        print_config();
      } else {
        print_receipt(job);
      }
    }
    vPortFree(job);
  }
}

void submit_print_job(LogMessage_Order_PaymentMethod payment) {
  if (print_queue == NULL) {
    return;
  }

  // Skip orders with no products (e.g. deposit-only / bottle-return transactions).
  if (current_state.cart.item_count == 0) {
    return;
  }

  PrintJob* job = pvPortMalloc(sizeof(PrintJob));
  if (job == NULL) {
    return;
  }

  job->type = PRINT_JOB_RECEIPT;
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

// Print the active list's product configuration (number -> name). Reads the
// active_config global at print time; no snapshot is taken. That's safe because
// config reloads are gated to is_safe_for_config_update() (cart empty + default
// mode) and this job is only ever submitted from the menu (MAIN_MENU mode).
void submit_config_print_job(void) {
  if (print_queue == NULL) {
    return;
  }

  PrintJob* job = pvPortMalloc(sizeof(PrintJob));
  if (job == NULL) {
    return;
  }

  job->type = PRINT_JOB_CONFIG;

  if (xQueueSendFromISR(print_queue, &job, NULL) != pdTRUE) {
    vPortFree(job);
  }
}

static void reconnect_timer_cb(TimerHandle_t timer) {
  ble_op = target_found ? BLE_OP_CONNECT_REQUESTED : BLE_OP_SCAN_REQUESTED;
  xTaskNotifyGive(printer_task_handle);
}

// Cancel the active scan and leave NimBLE initialized-but-idle.
// We used to call nimble_port_stop() + nimble_port_deinit() here to "save
// energy" when retries ran out, but the deinit/init cycle is broken in
// this IDF version: when start_bluetooth() later re-runs nimble_port_init(),
// vListInsert loops forever on a corrupted internal FreeRTOS list and the
// interrupt watchdog kills the device. The BLE controller does modem sleep
// on its own when there's no scan/connect/connection in progress, so the
// power cost of leaving the stack up is negligible.
static void stop_bluetooth(void) {
  ble_gap_disc_cancel();
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

// Called from BLE host callbacks — must not call ESP_LOGI / vprintf.
// Reading format strings from flash-mapped .rodata can fault inside the
// callback's cache-disable window during BLE/coex events.
static void schedule_reconnect(void) {
  if (retries_remaining <= 0) {
    printer_status = PRINTER_DISCONNECTED;
    xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
    stop_bluetooth();
    return;
  }

  retries_remaining--;

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
      struct ble_hs_adv_fields fields;
      bool has_printer_uuid = false;
      if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) == 0) {
        for (int i = 0; i < fields.num_uuids128 && !has_printer_uuid; i++) {
          if (ble_uuid_cmp(&fields.uuids128[i].u, &printer_service_uuid.u) == 0) {
            has_printer_uuid = true;
          }
        }
        for (int i = 0; i < fields.num_uuids16 && !has_printer_uuid; i++) {
          if (ble_uuid_cmp(&fields.uuids16[i].u, &printer_service_uuid16.u) == 0) {
            has_printer_uuid = true;
          }
        }
      }
      bool name_match = name && strcmp(name, TARGET_DEVICE_NAME) == 0;
      if (name_match || has_printer_uuid) {
        target_found = true;
        memcpy(&target_addr, &event->disc.addr, sizeof(target_addr));
        ble_op = BLE_OP_CONNECT_REQUESTED;
        xTaskNotifyGive(printer_task_handle);
      }
      break;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
      if (ble_op == BLE_OP_SCANNING) {
        ble_op = BLE_OP_IDLE;
        schedule_reconnect();
      }
      xTaskNotifyGive(printer_task_handle);
      break;

    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        conn_handle = event->connect.conn_handle;
        printer_status = PRINTER_CONNECTED;
        write_char_handle = 0;
        retries_remaining = MAX_STARTUP_RETRIES;
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
      ble_op = BLE_OP_IDLE;
      xTaskNotifyGive(printer_task_handle);
      break;

    case BLE_GAP_EVENT_DISCONNECT:
      printer_status = PRINTER_DISCONNECTED;
      conn_handle = BLE_HS_CONN_HANDLE_NONE;
      xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
      if (retries_remaining > 0) {
        ble_op = BLE_OP_CONNECT_REQUESTED;
        xTaskNotifyGive(printer_task_handle);
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

  // No ESP_LOGI after ble_gap_disc: starting the scan kicks the BLE radio,
  // which can trigger a PHY NVS write that briefly disables dcache — a
  // concurrent vprintf reading its format string from flash will fault.
  // NimBLE's own "GAP procedure initiated: discovery" log already confirms
  // scan start. The error log below is cheap to keep since it only fires
  // on rare init failures.
  int rc = ble_gap_disc(own_addr_type, SCAN_DURATION_MS, &params, gap_event_cb, NULL);
  if (rc != 0) {
    ESP_LOGE(PRINTER_TASK, "Failed to start scan: %d", rc);
    ble_op = BLE_OP_IDLE;
    xTaskNotifyGive(printer_task_handle);
    schedule_reconnect();
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
    ble_op = BLE_OP_IDLE;
    xTaskNotifyGive(printer_task_handle);
    schedule_reconnect();
  }
}

void printer_start_scan(int retries) {
  // Ignore manual scan requests until the printer task has initialized NimBLE.
  // start_bluetooth() -> ble_hs_is_enabled() crashes on an uninitialized host.
  if (!nimble_ready) {
    return;
  }
  retries_remaining = retries;
  start_bluetooth();
  ble_op = target_found ? BLE_OP_CONNECT_REQUESTED : BLE_OP_SCAN_REQUESTED;
  xTaskNotifyGive(printer_task_handle);
  xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
}

static void on_ble_sync(void) {
  ble_hs_util_ensure_addr(0);
  retries_remaining = MAX_STARTUP_RETRIES;
  ble_op = BLE_OP_SCAN_REQUESTED;
  xTaskNotifyGive(printer_task_handle);
}

static void ble_host_task(void* param) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

void printer(void* params) {
  printer_task_handle = xTaskGetCurrentTaskHandle();
  init_print_queue();
  xEventGroupWaitBits(
      event_group,
      STARTUP_BITS | RFID_INITIALIZED | INITIAL_FETCH_DONE,
      pdFALSE,
      pdTRUE,
      pdMS_TO_TICKS(30000)
  );
  ESP_ERROR_CHECK(nimble_port_init());
  ble_hs_cfg.sync_cb = on_ble_sync;
  nimble_port_freertos_init(ble_host_task);
  nimble_ready = true;

  while (1) {
    process_print_queue();

    if (ble_op == BLE_OP_SCAN_REQUESTED || ble_op == BLE_OP_CONNECT_REQUESTED) {
      // Serialize BLE scan/connect against HTTP. Concurrent BLE active-scan
      // or connect + mbedtls TLS handshake hits a cache-disable window that
      // faults mbedtls reading flash-mapped .rodata tables.
      xSemaphoreTake(network_request, portMAX_DELAY);
      while (ble_op == BLE_OP_SCAN_REQUESTED || ble_op == BLE_OP_CONNECT_REQUESTED) {
        if (ble_op == BLE_OP_SCAN_REQUESTED) {
          ble_op = BLE_OP_SCANNING;
          start_scan();
          while (ble_op == BLE_OP_SCANNING) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SCAN_DURATION_MS + 1000));
          }
          ble_gap_disc_cancel();
        }

        if (ble_op == BLE_OP_CONNECT_REQUESTED) {
          ble_op = BLE_OP_CONNECTING;
          connect_to_target();
          while (ble_op == BLE_OP_CONNECTING) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(11000));
          }
        }
      }
      xSemaphoreGive(network_request);
    }

    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
  }
}
