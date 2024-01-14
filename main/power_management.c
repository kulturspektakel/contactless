#include "power_management.h"
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_event.h"
#include "esp_log.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "math.h"

#define TAG "power_management"
#define USB_CHANNEL ADC_CHANNEL_0
#define BATTERY_CHANNEL ADC_CHANNEL_1
#define USB_PIN GPIO_NUM_1

#define BATTERY_MAX 2070
#define BATTERY_MIN 1500

#define LED_BLUE_PIN GPIO_NUM_40
#define LED_GREEN_PIN GPIO_NUM_41
#define LED_RED_PIN GPIO_NUM_42

int battery_voltage = 0;
int usb_voltage = 0;
static TaskHandle_t task_handle;

int battery_percentage() {
  // https://www.desmos.com/calculator/jymu8kltny
  double percentage =
      1.2 - (1.2 / (1.0 + pow(((1.5 * fmax(0.0, battery_voltage - BATTERY_MIN)) / 580), 4.0)));
  if (percentage < 0) {
    percentage = 0;
  } else if (percentage > 1) {
    percentage = 1;
  }
  return (int)(percentage * 100);
}

static void adc_calibration_init(
    adc_unit_t unit,
    adc_channel_t channel,
    adc_atten_t atten,
    adc_cali_handle_t* out_handle
) {
  adc_cali_curve_fitting_config_t cali_config = {
      .unit_id = unit,
      .chan = channel,
      .atten = atten,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  adc_cali_create_scheme_curve_fitting(&cali_config, out_handle);
}

static void IRAM_ATTR gpio_interrupt_handler(void* args) {
  vTaskNotifyGiveFromISR(task_handle, NULL);
}

static void update_leds() {
  if (battery_voltage > BATTERY_MAX && usb_voltage > 1000) {
    // green
    gpio_set_level(LED_BLUE_PIN, 1);
    gpio_set_level(LED_GREEN_PIN, 0);
    gpio_set_level(LED_RED_PIN, 1);
  } else if (usb_voltage > 1000) {
    // orange
    gpio_set_level(LED_BLUE_PIN, 0);
    gpio_set_level(LED_GREEN_PIN, 1);
    gpio_set_level(LED_RED_PIN, 1);
  } else if (battery_voltage < 1700) {
    // red
    gpio_set_level(LED_BLUE_PIN, 1);
    gpio_set_level(LED_GREEN_PIN, 1);
    gpio_set_level(LED_RED_PIN, 0);
  } else {
    // turn off all LEDs
    gpio_set_level(LED_BLUE_PIN, 1);
    gpio_set_level(LED_GREEN_PIN, 1);
    gpio_set_level(LED_RED_PIN, 1);
  }
}

static void read_voltages() {
  adc_oneshot_unit_handle_t adc1_handle;
  adc_oneshot_unit_init_cfg_t init_config = {
      .unit_id = ADC_UNIT_1,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

  adc_oneshot_chan_cfg_t config = {
      .bitwidth = ADC_BITWIDTH_DEFAULT,
      .atten = ADC_ATTEN_DB_11,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, BATTERY_CHANNEL, &config));
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, USB_CHANNEL, &config));

  adc_cali_handle_t battery_cali_handle = NULL;
  adc_calibration_init(init_config.unit_id, BATTERY_CHANNEL, config.atten, &battery_cali_handle);
  adc_cali_handle_t usb_cali_handle = NULL;
  adc_calibration_init(init_config.unit_id, USB_CHANNEL, config.atten, &usb_cali_handle);

  battery_voltage = 0;
  usb_voltage = 0;
  int battery_voltage_tmp;
  int usb_voltage_tmp;
  for (int i = 0; i < 3; i++) {
    adc_oneshot_read(adc1_handle, BATTERY_CHANNEL, &battery_voltage_tmp);
    adc_cali_raw_to_voltage(battery_cali_handle, battery_voltage_tmp, &battery_voltage_tmp);
    adc_oneshot_read(adc1_handle, USB_CHANNEL, &usb_voltage_tmp);
    adc_cali_raw_to_voltage(usb_cali_handle, usb_voltage_tmp, &usb_voltage_tmp);
    ESP_LOGI(TAG, "Measurement %d USB %dmV, battery %dmV", i, usb_voltage_tmp, battery_voltage_tmp);

    // find max voltage
    battery_voltage = battery_voltage_tmp > battery_voltage ? battery_voltage_tmp : battery_voltage;
    usb_voltage = usb_voltage_tmp > usb_voltage ? usb_voltage_tmp : usb_voltage;

    // delay to allow ADC to settle
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }

  ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));

  update_leds();
}

void power_management(void* params) {
  task_handle = xTaskGetCurrentTaskHandle();
  read_voltages();

  gpio_install_isr_service(0);

  // setup LED pins
  gpio_config_t led_config = {
      .pin_bit_mask = (1ULL << LED_BLUE_PIN) | (1ULL << LED_GREEN_PIN) | (1ULL << LED_RED_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = 0,
      .pull_down_en = 0,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&led_config);

  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_NEGEDGE,
      .pin_bit_mask = (1ULL << USB_PIN),
      .mode = GPIO_MODE_INPUT,
      .pull_down_en = 1,
  };

  TimerHandle_t voltage_update_timer =
      xTimerCreate("voltage_update_timer", pdMS_TO_TICKS(60000), pdTRUE, 0, gpio_interrupt_handler);
  if (voltage_update_timer != NULL) {
    xTimerStart(voltage_update_timer, pdMS_TO_TICKS(60000));
  }

  while (true) {
    gpio_config(&io_conf);
    gpio_set_intr_type(USB_PIN, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(USB_PIN, gpio_interrupt_handler, (void*)USB_PIN);
    gpio_intr_enable(USB_PIN);

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    gpio_intr_disable(USB_PIN);
    ulTaskNotifyTake(pdTRUE, 0);  // clear remaining interrupt notifications

    vTaskDelay(200 / portTICK_PERIOD_MS);
    read_voltages();
    ESP_LOGI(TAG, "reading voltages: USB %dmV, battery %dmV", usb_voltage, battery_voltage);
    xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
  }
}
