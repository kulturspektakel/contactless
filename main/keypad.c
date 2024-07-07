#include "keypad.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "state_machine.h"

static const char* TAG = "keypad";
const event_t KEYPAD[] = {
    // clang-format off
    KEY_1, KEY_2, KEY_3, KEY_A,
    KEY_4, KEY_5, KEY_6, KEY_B,
    KEY_7, KEY_8, KEY_9, KEY_C,
    KEY_STAR, KEY_0, KEY_HASH, KEY_D,
    // clang-format on
};
QueueHandle_t keypad_queue;

typedef struct {
  event_t key;
  int64_t time;
} key_event_t;

void turnon_rows() {
  for (int i = 0; i < 4; i++) {  // Columns
    gpio_set_pull_mode(KEYPAD_COLS[i], GPIO_PULLDOWN_ONLY);
  }
  for (int i = 0; i < 4; i++) {  // Rows
    gpio_set_pull_mode(KEYPAD_ROWS[i], GPIO_PULLUP_ONLY);
    gpio_intr_enable(KEYPAD_ROWS[i]);
  }
}

void turnon_cols() {
  for (int i = 0; i < 4; i++) {  // Rows
    gpio_intr_disable(KEYPAD_ROWS[i]);
    gpio_set_pull_mode(KEYPAD_ROWS[i], GPIO_PULLDOWN_ONLY);
  }
  for (int i = 0; i < 4; i++) {  // Columns
    gpio_set_pull_mode(KEYPAD_COLS[i], GPIO_PULLUP_ONLY);
  }
}

static void IRAM_ATTR gpio_interrupt_handler(void* args) {
  static int64_t time_old_isr = 0;
  uint8_t r = *(uint8_t*)args;
  int64_t time_now_isr = esp_timer_get_time();

  if (time_now_isr - time_old_isr >= 200000) {  // 200ms debounce
    turnon_cols();
    for (int c = 0; c < 4; c++) {
      if (!gpio_get_level(KEYPAD_COLS[c])) {
        xQueueSendFromISR(keypad_queue, &KEYPAD[r * 4 + c], NULL);
        break;
      }
    }
    turnon_rows();
    time_old_isr = time_now_isr;
  }
}

void keypad(void* params) {
  keypad_queue = xQueueCreate(5, sizeof(event_t));
  if (keypad_queue == NULL) {
    ESP_LOGI(TAG, "Failed to create keypad queue");
    trigger_event(FATAL_ERROR);
    return;
  }

  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_install_isr_service(ESP_INTR_FLAG_EDGE));
  for (uint8_t r = 0; r < 4; r++) {  // Rows

    rtc_gpio_deinit(KEYPAD_ROWS[r]);
    gpio_intr_disable(KEYPAD_ROWS[r]);
    gpio_set_direction(KEYPAD_ROWS[r], GPIO_MODE_INPUT);
    gpio_set_intr_type(KEYPAD_ROWS[r], GPIO_INTR_NEGEDGE);
    uint8_t* arg = pvPortMalloc(sizeof(uint8_t));
    *arg = r;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_isr_handler_add(KEYPAD_ROWS[r], gpio_interrupt_handler, arg)
    );
  }
  for (int c = 0; c < 4; c++) {  // Columns
    rtc_gpio_deinit(KEYPAD_COLS[c]);
    gpio_set_direction(KEYPAD_COLS[c], GPIO_MODE_INPUT);
  }

  turnon_rows();

  event_t key;
  key_event_t history[3];

  while (true) {
    xQueueReceive(keypad_queue, &key, portMAX_DELAY);
    history[2] = history[1];
    history[1] = history[0];
    key_event_t event = {key, esp_timer_get_time()};
    history[0] = event;
    trigger_event(key);
    ESP_LOGI(TAG, "keypress %d", key);

    if (esp_timer_get_time() - history[2].time < 1000000) {
      if (history[0].key == KEY_D && history[1].key == KEY_D && history[2].key == KEY_D) {
        trigger_event(KEY_TRIPPLE_D);
      }
    }
  }
}