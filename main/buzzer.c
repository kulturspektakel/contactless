#include "buzzer.h"
#include "constants.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define BUZZER_PIN GPIO_NUM_8

static QueueHandle_t beep_events;
silent_mode_t silent_mode = SILENT_MODE_OFF;

typedef struct {
  int frequency;  // Frequency in Hz
  int duration;   // Duration in milliseconds
  int pause;      // Pause after the note in milliseconds
} Note;

Note LOW_BATTERY[] = {{698, 70, 20}, {349, 70, 400}, {698, 70, 20}, {349, 70, 0}};
Note WELCOME[] = {{523, 150, 20}, {659, 150, 20}, {784, 150, 20}, {1046, 500, 0}};
Note POWER[] = {{349, 70, 20}, {698, 70, 20}};

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
  static gpio_config_t config = {
      .pin_bit_mask = (1ULL << BUZZER_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&config);
  gpio_set_level(BUZZER_PIN, 1);
  vTaskDelay(duration / portTICK_PERIOD_MS);
  gpio_set_level(BUZZER_PIN, 0);
}

static void persist_silent_mode(void* arg) {
  nvs_handle_t nvs_handle;
  nvs_open(NVS_DEVICE_CONFIG, NVS_READWRITE, &nvs_handle);
  nvs_set_u8(nvs_handle, NVS_SILENT_MODE, silent_mode);
  nvs_commit(nvs_handle);
  nvs_close(nvs_handle);
  vTaskDelete(NULL);
}

void toggle_silent_mode() {
  silent_mode = (silent_mode + 1) % _SILENT_MODE_COUNT;
  // needs to be done in a separate task, as this function is called from state machine during
  // critical sections
  xTaskCreate(persist_silent_mode, "persist_silent_mode", 2048, NULL, TASK_PRIO_NORMAL, NULL);
  xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
}

void buzzer(void* params) {
  beep_events = xQueueCreate(1, sizeof(beep_type_t));

  nvs_handle_t nvs_handle;
  nvs_open(NVS_DEVICE_CONFIG, NVS_READONLY, &nvs_handle);
  nvs_get_u8(nvs_handle, NVS_SILENT_MODE, &silent_mode);
  nvs_close(nvs_handle);

  beep_type_t type;
  while (1) {
    xQueueReceive(beep_events, &type, portMAX_DELAY);
    if (silent_mode == SILENT_MODE_ON) {
      continue;
    }

    switch (type) {
      case BEEP_SHORT:
        play_beep(150);
        break;
      case BEEP_LONG:
        play_beep(1000);
        break;
      case BATTERY_EMPTY:
        play_melody(LOW_BATTERY, sizeof(LOW_BATTERY));
        break;
      case STARTUP:
        play_melody(WELCOME, sizeof(WELCOME));
        break;
      case POWER_CONNECTED:
        play_melody(POWER, sizeof(POWER));
        break;
      case KEY_PRESS:
        if (silent_mode == SILENT_MODE_OFF_WITH_KEYPRESS) {
          play_beep(50);
          break;
        }
      default:
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    // clear queue, in case multiple beeps were triggered
    xQueueReceive(beep_events, &type, 0);
  }
}