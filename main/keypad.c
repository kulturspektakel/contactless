#include "keypad.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define INPUT_PIN 27
static const char* TAG = "display";

static void IRAM_ATTR gpio_interrupt_handler(void* args) {
  // notify the task that the interrupt occurred
  xTaskNotifyFromISR(xTaskGetHandle("keypad"), 1, eSetBits, NULL);
}

void keypad(void* params) {
  esp_rom_gpio_pad_select_gpio(INPUT_PIN);
  gpio_set_direction(INPUT_PIN, GPIO_MODE_INPUT);
  gpio_pulldown_en(INPUT_PIN);
  gpio_pullup_dis(INPUT_PIN);
  gpio_set_intr_type(INPUT_PIN, GPIO_INTR_POSEDGE);

  gpio_install_isr_service(0);
  gpio_isr_handler_add(INPUT_PIN, gpio_interrupt_handler, (void*)INPUT_PIN);

  while (true) {
    // wait for the interrupt to occur
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "INTERRRPUT ");
  }
}