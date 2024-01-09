#include "power_management.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_event.h"
#include "esp_log.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "power_management"
#define BATTERY_VOLTAGE ADC1_CHANNEL_0
#define USB_VOLTAGE ADC1_CHANNEL_1
#define LED_BLUE_PIN GPIO_NUM_40
#define LED_GREEN_PIN GPIO_NUM_41
#define LED_RED_PIN GPIO_NUM_42

static int adc_raw[2];
int battery_voltage = 0;
int usb_voltage = 0;

void power_management(void* params) {
  adc_oneshot_unit_handle_t adc1_handle;
  adc_oneshot_unit_init_cfg_t init_config1 = {
      .unit_id = ADC_UNIT_1,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

  adc_oneshot_chan_cfg_t config = {
      .bitwidth = ADC_BITWIDTH_DEFAULT,
      .atten = ADC_ATTEN_DB_11,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0, &config));
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_1, &config));

  while (true) {
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_0, &adc_raw[0]));
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_1, &adc_raw[1]));
    ESP_LOGI(TAG, "ADC channel 0: %d, channel 1: %d", adc_raw[0], adc_raw[1]);
    usb_voltage = adc_raw[0];
    battery_voltage = adc_raw[1];

    xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
}
