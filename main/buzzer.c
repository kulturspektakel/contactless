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

Note LOW_BATTERY[] = {{698, 70, 20}, {349, 70, 400}, {698, 70, 20}, {349, 70, 0}};
Note WELCOME[] = {{523, 150, 20}, {659, 150, 20}, {784, 150, 20}, {1046, 500, 0}};

void trigger_beep(beep_type_t type) {
  xQueueSend(beep_events, &type, 0);
}

static void play_tone(int tone, int duration) {
  ledc_timer_config_t ledc_timer = {
      .duty_resolution = LEDC_TIMER_13_BIT,
      .freq_hz = tone,
      .speed_mode = 0,
      .timer_num = LEDC_TIMER_0,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&ledc_timer);

  ledc_channel_config_t ledc_channel = {
      .channel = LEDC_CHANNEL_4,
      .duty = 4096,
      .gpio_num = BUZZER_PIN,
      .speed_mode = 0,
      .hpoint = 0,
      .timer_sel = LEDC_TIMER_0
  };
  ledc_channel_config(&ledc_channel);

  vTaskDelay(duration / portTICK_PERIOD_MS);

  ledc_stop(0, LEDC_CHANNEL_4, 0);
}

static void play_melody(Note* notes, size_t size) {
  for (int i = 0; i < size / sizeof(Note); i++) {
    play_tone(notes[i].frequency, notes[i].duration);
    vTaskDelay(notes[i].pause / portTICK_PERIOD_MS);
  }
}

static void play_beep(int duration) {
  gpio_set_level(BUZZER_PIN, 1);
  vTaskDelay(duration / portTICK_PERIOD_MS);
  gpio_set_level(BUZZER_PIN, 0);
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
    // switch (type) {
    //   case BEEP_SHORT:
    //     play_beep(150);
    //     break;
    //   case BEEP_LONG:
    //     play_beep(1000);
    //     break;
    //   case BATTERY_EMPTY:
    //     play_melody(LOW_BATTERY, sizeof(LOW_BATTERY));
    //     break;
    //   case STARTUP:
    //     play_melody(WELCOME, sizeof(WELCOME));
    //     break;
    // }
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // clear queue, in case multiple beeps were triggered
    xQueueReceive(beep_events, &type, 0);
  }
}