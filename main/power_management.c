#include "power_management.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "power_management"
#define BATTERY_VOLTAGE_PIN GPIO_NUM_1
#define USB_VOLTAGE_PIN GPIO_NUM_2
#define LED_BLUE_PIN GPIO_NUM_40
#define LED_GREEN_PIN GPIO_NUM_41
#define LED_RED_PIN GPIO_NUM_42

void power_management(void* params) {
  gpio_config_t io_conf;
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (1ULL << BATTERY_VOLTAGE_PIN) | (1ULL << USB_VOLTAGE_PIN);
  io_conf.pull_down_en = 0;
  io_conf.pull_up_en = 1;
  gpio_config(&io_conf);

  io_conf.pin_bit_mask = (1ULL << LED_BLUE_PIN) | (1ULL << LED_GREEN_PIN) | (1ULL << LED_RED_PIN);
  io_conf.mode = GPIO_MODE_OUTPUT;
  gpio_config(&io_conf);

  while (true) {
    int battery_voltage = gpio_get_level(BATTERY_VOLTAGE_PIN);
    int usb_voltage = gpio_get_level(USB_VOLTAGE_PIN);

    ESP_LOGI(TAG, "battery_voltage: %d, usb_voltage: %d", battery_voltage, usb_voltage);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
