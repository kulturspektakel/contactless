#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_uploader.h"
#include "time.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"

static const char* TAG = "display";

void battery(u8g2_t* u8g2, int current) {
  int y = 0;
  int h = 4;
  int w = 10;
  int x = 127 - w;
  int min = 0;
  int max = 1000;
  u8g2_DrawLine(u8g2, x, y, x, y + h);
  u8g2_DrawLine(u8g2, x, y, x + w - 2, y);
  u8g2_DrawLine(u8g2, x + w - 2, y, x + w - 2, y + h);
  u8g2_DrawLine(u8g2, x, y + h, x + w - 2, y + h);
  u8g2_DrawLine(u8g2, x + w, y + 1, x + w, y + h - 1);

  // 100% overruns the bar
  int bar_width = (w - 2) * (current - min) / (max - min);
  u8g2_DrawBox(u8g2, x + 1, y + 1, bar_width, h - 1);
}

void wifi_strength(u8g2_t* u8g2, bool needs_update) {
  static int8_t rssi;
  int x = 0;
  int y = 4;
  int bars = 0;

  if (needs_update) {
    wifi_ap_record_t wifidata;
    if (esp_wifi_sta_get_ap_info(&wifidata) == ESP_OK) {
      rssi = wifidata.rssi;
    }
  }

  if (rssi > -55) {
    bars = 4;
  } else if (rssi > -66) {
    bars = 3;
  } else if (rssi > -77) {
    bars = 2;
  } else {
    bars = 1;
  }

  for (int step = 0; step < 4; step++) {
    u8g2_DrawLine(u8g2, x + (step * 2), y, x + (step * 2), step < bars ? y - 1 - step : y);
  }
}

void pending_uploads(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_tiny5_tr);
  char pending[4];
  sprintf(pending, "%3d", log_count);
  // render right aligned
  u8g2_uint_t w = u8g2_GetStrWidth(u8g2, pending);
  u8g2_DrawStr(u8g2, 112 - w, 5, pending);
  u8g2_SetFont(u8g2, u8g2_font_m2icon_5_tf);
  u8g2_DrawStr(u8g2, 110 - w, 5, "B");
}

void time_display(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_tiny5_tr);
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  char time_buffer[6];
  strftime(time_buffer, sizeof(time_buffer), "%H:%M", &timeinfo);
  u8g2_DrawStr(u8g2, 10, 5, time_buffer);
}

void display(void* params) {
  // 2 second delay to allow other tasks to start
  vTaskDelay(2000 / portTICK_PERIOD_MS);

  u8g2_esp32_hal_t u8g2_esp32_hal = {
      .clk = 14,
      .mosi = 13,
      .cs = 15,
      .dc = 33,
      .reset = 32,
  };
  u8g2_esp32_hal_init(u8g2_esp32_hal);

  u8g2_t u8g2;
  u8g2_Setup_ssd1309_128x64_noname0_f(
      &u8g2, U8G2_R0, u8g2_esp32_spi_byte_cb, u8g2_esp32_gpio_and_delay_cb
  );

  u8g2_InitDisplay(&u8g2);
  u8g2_SetPowerSave(&u8g2, 0);

  int c = 0;
  while (1) {
    u8g2_ClearBuffer(&u8g2);

    battery(&u8g2, c++);
    wifi_strength(&u8g2, c == 10);
    pending_uploads(&u8g2);
    time_display(&u8g2);

    u8g2_SendBuffer(&u8g2);
    if (c > 1000) {
      c = 0;
    }
  }
}
