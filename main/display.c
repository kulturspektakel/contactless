#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_uploader.h"
#include "state_machine.h"
#include "time.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"

static const char* TAG = "display";
#define logo_width 34
#define logo_height 34
static const uint8_t logo_bits[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0xFF,
    0xFF, 0xFF, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0xFF, 0xFF,
    0xFF, 0xFF, 0x03, 0xFF, 0xC0, 0x07, 0xF8, 0x03, 0xFF, 0xC0, 0x03, 0xFC, 0x03, 0xFF, 0xC0, 0x01,
    0xFE, 0x03, 0xFF, 0xC0, 0x00, 0xFF, 0x03, 0xFF, 0x40, 0x80, 0xFF, 0x03, 0xFF, 0x00, 0xC0, 0xFF,
    0x03, 0xFF, 0x00, 0xE0, 0xFF, 0x03, 0xFF, 0x00, 0xF0, 0xFF, 0x03, 0xFF, 0x00, 0xF8, 0xFF, 0x03,
    0xFF, 0x00, 0xFC, 0xFF, 0x03, 0xFF, 0x00, 0xFC, 0xFF, 0x03, 0xFF, 0x00, 0xF8, 0xFF, 0x03, 0xFF,
    0x00, 0xF0, 0xFF, 0x03, 0xFF, 0x00, 0xE0, 0xFF, 0x03, 0xFF, 0x00, 0xC0, 0xFF, 0x03, 0xFF, 0x40,
    0x80, 0xFF, 0x03, 0xFF, 0xC0, 0x00, 0xFF, 0x03, 0xFF, 0xC0, 0x01, 0xFE, 0x03, 0xFF, 0xC0, 0x03,
    0xFC, 0x03, 0xFF, 0xC0, 0x07, 0xF8, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0xFF,
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0x03,
    0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0x03,
};

static void battery(u8g2_t* u8g2, int current) {
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

static void wifi_strength(u8g2_t* u8g2, bool needs_update) {
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

static void pending_uploads(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_tiny5_tr);
  char pending[4];
  sprintf(pending, "%3d", log_count);
  // render right aligned
  u8g2_uint_t w = u8g2_GetStrWidth(u8g2, pending);
  u8g2_DrawStr(u8g2, 112 - w, 5, pending);
  u8g2_SetFont(u8g2, u8g2_font_m2icon_5_tf);
  u8g2_DrawStr(u8g2, 110 - w, 5, "B");
}

static void time_display(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_tiny5_tr);
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  char time_buffer[6];
  strftime(time_buffer, sizeof(time_buffer), "%H:%M", &timeinfo);
  u8g2_DrawStr(u8g2, 10, 5, time_buffer);
}

static void keypad_legend_letter(u8g2_t* u8g2, char* letter, char* text, int offset) {
  u8g2_SetDrawColor(u8g2, 0);
  u8g2_DrawRBox(u8g2, offset + 1, 56, 8, 7, 1);
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_tiny5_tr);
  u8g2_DrawStr(u8g2, offset + 3, 62, letter);
  u8g2_SetDrawColor(u8g2, 0);
  u8g2_DrawStr(u8g2, offset + 11, 62, text);
  u8g2_SetDrawColor(u8g2, 1);
}

static void keypad_legend(u8g2_t* u8g2) {
  u8g2_DrawBox(u8g2, 0, 55, 128, 9);
  keypad_legend_letter(u8g2, "A", "Up", 0);
  keypad_legend_letter(u8g2, "B", "Dn", 26);
  keypad_legend_letter(u8g2, "*", "OK", 52);
  keypad_legend_letter(u8g2, "D", "Abbrechen", 78);
}

static void status_bar(u8g2_t* u8g2) {
  battery(u8g2, 1);
  wifi_strength(u8g2, true);
  pending_uploads(u8g2);
  time_display(u8g2);
}

static void boot_screen(u8g2_t* u8g2) {
  u8g2_DrawXBM(u8g2, 47, 15, logo_width, logo_height, &logo_bits);
}

static void draw_amount(u8g2_t* u8g2, int amount, int y) {
  char amount_str[7];
  snprintf(amount_str, sizeof(amount_str), "%5.2f", ((float)amount) / 100);
  u8g2_uint_t w = u8g2_GetStrWidth(u8g2, amount_str);
  u8g2_DrawStr(u8g2, 128 - w, y, amount_str);
}

static void charge_list(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);

  for (int i = 0; i < current_state.cart.item_count && i < 3; i++) {
    char product[13];
    if (i == 2 && current_state.cart.item_count > 3) {
      int sum = 0;
      int amount = 0;
      for (int j = 2; j < current_state.cart.item_count; j++) {
        sum += current_state.cart.items[j].amount * current_state.cart.items[j].product.price;
        amount += current_state.cart.items[j].amount;
      }
      snprintf(product, sizeof(product), "+%d weitere", amount);
      u8g2_DrawStr(u8g2, 0, 17 + (i * 10), product);
      draw_amount(u8g2, sum, 17 + (i * 10));
      break;
    }

    snprintf(
        product,
        sizeof(product),
        "%ld %.10s",
        current_state.cart.items[i].amount,
        current_state.cart.items[i].product.name
    );
    u8g2_DrawStr(u8g2, 0, 17 + (i * 10), product);
    draw_amount(
        u8g2,
        current_state.cart.items[i].amount * current_state.cart.items[i].product.price,
        17 + (i * 10)
    );
  }

  char deposit[21];
  if (current_state.cart.deposit < 0) {
    snprintf(deposit, sizeof(deposit), "%d Rückgabe", current_state.cart.deposit * -1);
  } else {
    snprintf(deposit, sizeof(deposit), "%d Pfand", current_state.cart.deposit);
  }
  u8g2_DrawStr(u8g2, 0, 51, deposit);
  draw_amount(u8g2, current_state.cart.deposit * 200, 51);
  u8g2_DrawHLine(u8g2, 0, 53, 128);
  u8g2_DrawStr(u8g2, 0, 63, "Summe");
  draw_amount(u8g2, current_state.cart.total + (current_state.cart.deposit * 200), 63);
}

static void main_menu(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  const int number_of_items = 26;
  static int active_item = 1;
  active_item = (active_item + 1) % number_of_items;
  char items[26][19] = {
      "Ausschank",
      "Burger",
      "Café",
      "Churros",
      "Cocktail",
      "Craft Beer",
      "Crêpes",
      "Eis",
      "Falafel",
      "Flammkuchen",
      "Frittiererei",
      "Frühschoppen",
      "Grill",
      "Hot Dog",
      "Infobude",
      "Irish Pub",
      "Nachos",
      "Panini",
      "Partnervermittlung",
      "Pizza",
      "Spieße",
      "Waffel",
      "Wein",
      "Weißbierbar",
      "Weißbiergarten",
      "Wraps",
  };

  for (int i = 0; i < 3; i++) {
    int offset = active_item - 1;
    if (active_item == 0) {
      offset = 0;
    } else if (active_item == number_of_items - 1) {
      offset = number_of_items - 3;
    }

    u8g2_DrawStr(u8g2, 4, 18 + (i * 15), items[offset + i]);
    if (offset + i == active_item) {
      u8g2_DrawRFrame(u8g2, 0, 8 + (i * 15), 120, 15, 3);
      u8g2_DrawHLine(u8g2, 2, 21 + (i * 15), 116);
      u8g2_DrawVLine(u8g2, 118, 9 + (i * 15), 13);
    }
  }

  // scrollbar
  for (int i = 7; i < 53; i = i + 3) {
    u8g2_DrawPixel(u8g2, 124, i);
  }
  u8g2_DrawBox(u8g2, 123, 7 + ((float)active_item / (float)number_of_items) * 45, 3, 4);
}

static void write_card(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  u8g2_DrawStr(u8g2, 0, 17, "Bitte Karte");
  u8g2_DrawStr(u8g2, 0, 27, "auflegen");
  u8g2_DrawStr(u8g2, 0, 37, "zum Aufladen");
  u8g2_DrawStr(u8g2, 0, 47, "oder Abbrechen");
}

void display(void* params) {
  // 2 second delay to allow other tasks to start
  vTaskDelay(200 / portTICK_PERIOD_MS);

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

  while (1) {
    u8g2_ClearBuffer(&u8g2);

    switch (current_state.mode) {
      case MAIN_STARTING_UP:
        boot_screen(&u8g2);
        break;
      case CHARGE_LIST:
        status_bar(&u8g2);
        charge_list(&u8g2);
        break;
      case MAIN_MENU:
        status_bar(&u8g2);
        main_menu(&u8g2);
        keypad_legend(&u8g2);
        break;
      case WRITE_CARD:
        status_bar(&u8g2);
        write_card(&u8g2);
        break;
      default:
        break;
    }

    u8g2_SendBuffer(&u8g2);
    // TODO use event group to wait for state change
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
