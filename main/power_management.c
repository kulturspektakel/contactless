#include "power_management.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keypad.h"
#include "math.h"
#include "state_machine.h"

#define TAG "power_management"
#define USB_CHANNEL ADC_CHANNEL_0
#define BATTERY_CHANNEL ADC_CHANNEL_1
#define USB_PIN GPIO_NUM_1

// TODO: disabled because it needs to be a RTC GPIO pin (14 or 21)
// Reset and Power-Down: When low, internal current sources are switched off, the oscillator is
// disabled, and input pads are disconnected from the outside world. The internal reset phase
// starts on the negative edge on this pin.
#define RSTPDN_PIN GPIO_NUM_36

#define BATTERY_MAX 2070
#define BATTERY_MIN 1500

#define UPDATE_INTERVAL 60000
#define POWER_OFF_TIMEOUT 900000  // 15 minutes

#define LED_BLUE_PIN GPIO_NUM_40
#define LED_GREEN_PIN GPIO_NUM_41
#define LED_RED_PIN GPIO_NUM_42

int battery_voltage = 0;
int usb_voltage = 0;
static TaskHandle_t task_handle;
static TimerHandle_t power_off_timer_handle = NULL;

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
  // if (battery_voltage > BATTERY_MAX && usb_voltage > USB_VOLTAGE_THRESHOLD) {
  //   // green
  //   gpio_set_level(LED_BLUE_PIN, 1);
  //   gpio_set_level(LED_GREEN_PIN, 0);
  //   gpio_set_level(LED_RED_PIN, 1);
  // } else if (usb_voltage > USB_VOLTAGE_THRESHOLD) {
  //   // orange
  //   gpio_set_level(LED_BLUE_PIN, 0);
  //   gpio_set_level(LED_GREEN_PIN, 1);
  //   gpio_set_level(LED_RED_PIN, 1);
  // } else
  if (battery_voltage < 1700) {
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

static void power_off_timer_callback(TimerHandle_t xTimer) {
  ESP_LOGI(TAG, "Powering off");
  if (usb_voltage > USB_VOLTAGE_THRESHOLD) {
    ESP_LOGI(TAG, "USB still connected, not powering off");
    return;
  }

  trigger_event(ENTER_POWER_SAVE);
  gpio_config_t io_conf;
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL << RSTPDN_PIN);
  io_conf.pull_down_en = 1;
  io_conf.pull_up_en = 0;
  gpio_config(&io_conf);
  gpio_set_level(RSTPDN_PIN, 0);

  // Initialize and configure each RTC GPIO pin in a loop
  uint64_t rtc_gpio_mask = 0;
  for (int i = 0; i < 4; i++) {
    // input
    gpio_num_t pin = KEYPAD_ROWS[i];
    gpio_isr_handler_remove(pin);
    gpio_reset_pin(pin);
    rtc_gpio_init(pin);
    rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_dis(pin);
    rtc_gpio_pulldown_en(pin);
    rtc_gpio_mask |= (1ULL << pin);

    // output
    pin = KEYPAD_COLS[i];
    gpio_isr_handler_remove(pin);
    gpio_reset_pin(pin);
    rtc_gpio_init(pin);
    rtc_gpio_set_direction(pin, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(pin, 1);
  }

  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_enable_ext1_wakeup(rtc_gpio_mask, ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_deep_sleep_start();
}

void reset_power_off_timer() {
  if (power_off_timer_handle != NULL) {
    xTimerReset(power_off_timer_handle, pdMS_TO_TICKS(POWER_OFF_TIMEOUT));
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

  TimerHandle_t voltage_update_timer = xTimerCreate(
      "voltage_update_timer", pdMS_TO_TICKS(UPDATE_INTERVAL), pdTRUE, 0, gpio_interrupt_handler
  );
  if (voltage_update_timer != NULL) {
    xTimerStart(voltage_update_timer, pdMS_TO_TICKS(UPDATE_INTERVAL));
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
    int old_usb_voltage = usb_voltage;
    read_voltages();

    if (old_usb_voltage < USB_VOLTAGE_THRESHOLD && usb_voltage > USB_VOLTAGE_THRESHOLD) {
      // USB was just plugged in, start power off timer
      if (power_off_timer_handle == NULL) {
        power_off_timer_handle = xTimerCreate(
            "power_off_timer",
            pdMS_TO_TICKS(POWER_OFF_TIMEOUT),
            pdFALSE,
            0,
            power_off_timer_callback
        );
      }
      reset_power_off_timer();
    } else if (old_usb_voltage > USB_VOLTAGE_THRESHOLD && usb_voltage < USB_VOLTAGE_THRESHOLD) {
      // USB was just unplugged, disable power off timer
      xTimerDelete(power_off_timer_handle, 0);
      power_off_timer_handle = NULL;
    }

    ESP_LOGI(TAG, "USB %dmV, battery %dmV", usb_voltage, battery_voltage);
    xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
  }
}
