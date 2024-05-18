#include "buzzer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define BUZZER_PIN GPIO_NUM_8

static QueueHandle_t beep_events;

typedef struct {
  int frequency;  // Frequency in Hz
  int duration;   // Duration in milliseconds
  int pause;      // Pause after the note in milliseconds
} Note;

Note low_battery[] = {{400, 150, 150}, {600, 150, 150}, {800, 150, 0}};

void trigger_beep(beep_type_t type) {
  xQueueSend(beep_events, &type, 0);
}

void play_tone(int tone, int duration) {
  ledc_timer_config_t ledc_timer = {
      .duty_resolution = LEDC_TIMER_13_BIT,
      .freq_hz = tone,
      .speed_mode = 0,
      .timer_num = LEDC_TIMER_0,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&ledc_timer);

  ledc_channel_config_t ledc_channel = {
      .channel = LEDC_CHANNEL_0,
      .duty = 4096,
      .gpio_num = GPIO_NUM_8,
      .speed_mode = 0,
      .hpoint = 0,
      .timer_sel = LEDC_TIMER_0
  };
  ledc_channel_config(&ledc_channel);

  vTaskDelay(1000 / duration / portTICK_PERIOD_MS);

  ledc_stop(0, LEDC_CHANNEL_0, 0);
}

void buzzer(void* params) {
  beep_events = xQueueCreate(1, sizeof(beep_type_t));

  gpio_config_t config = {
      .pin_bit_mask = (1ULL << BUZZER_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&config);

  beep_type_t type;
  while (1) {
    xQueueReceive(beep_events, &type, portMAX_DELAY);
    // gpio_set_level(BUZZER_PIN, 1);
    switch (type) {
      case BEEP_SHORT:
        vTaskDelay(150 / portTICK_PERIOD_MS);
        break;
      case BEEP_LONG:
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        break;
    }

    gpio_set_level(BUZZER_PIN, 0);
  }
}