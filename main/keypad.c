#include "keypad.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char* TAG = "display";
const char KEYPAD[] = {
    // clang-format off
    '1', '2', '3', 'A',
    '4', '5', '6', 'B',
    '7', '8', '9', 'C',
    '*', '0', '#', 'D'
    // clang-format on
};
static const int KEYPAD_PINS[8] = {2, 4, 16, 17, 5, 18, 19, 21};

int64_t time_old_isr = 0;
QueueHandle_t keypad_queue;

void turnon_rows() {
  for (int i = 4; i < 8; i++) {  // Columns
    gpio_set_pull_mode(KEYPAD_PINS[i], GPIO_PULLDOWN_ONLY);
  }
  for (int i = 0; i < 4; i++) {  // Rows
    gpio_set_pull_mode(KEYPAD_PINS[i], GPIO_PULLUP_ONLY);
    gpio_intr_enable(KEYPAD_PINS[i]);
  }
}

void turnon_cols() {
  for (int i = 0; i < 4; i++) {  // Rows
    gpio_intr_disable(KEYPAD_PINS[i]);
    gpio_set_pull_mode(KEYPAD_PINS[i], GPIO_PULLDOWN_ONLY);
  }
  for (int i = 4; i < 8; i++) {  // Columns
    gpio_set_pull_mode(KEYPAD_PINS[i], GPIO_PULLUP_ONLY);
  }
}

static void IRAM_ATTR gpio_interrupt_handler(void* args) {
  int r = (int)(args);
  int64_t time_now_isr = esp_timer_get_time();

  // if (time_now_isr - time_old_isr >= 100000) {
  turnon_cols();
  for (int c = 4; c < 8; c++) {
    if (!gpio_get_level(KEYPAD_PINS[c])) {
      xQueueSendFromISR(keypad_queue, &KEYPAD[r * 4 + c - 4], NULL);
      break;
    }
  }
  turnon_rows();
  // }
  // time_old_isr = time_now_isr;
}

void keypad(void* params) {
  keypad_queue = xQueueCreate(5, sizeof(char));
  if (keypad_queue == NULL) {
    ESP_LOGI(TAG, "Failed to create keypad queue");
    // TODO fatal error
    return;
  }

  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_install_isr_service(ESP_INTR_FLAG_EDGE));
  for (int r = 0; r < 4; r++) {  // Rows
    gpio_intr_disable(KEYPAD_PINS[r]);
    gpio_set_direction(KEYPAD_PINS[r], GPIO_MODE_INPUT);
    gpio_set_intr_type(KEYPAD_PINS[r], GPIO_INTR_NEGEDGE);
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        gpio_isr_handler_add(KEYPAD_PINS[r], (void*)gpio_interrupt_handler, (void*)r)
    );
  }
  for (int c = 4; c < 8; c++) {  // Columns
    gpio_set_direction(KEYPAD_PINS[c], GPIO_MODE_INPUT);
  }

  turnon_rows();

  char key;
  while (true) {
    xQueueReceive(keypad_queue, &key, portMAX_DELAY);
    ESP_LOGI(TAG, "Keypressed %c", key);
  }
}