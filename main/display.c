#include "esp_log.h"
#include "esp_timer.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_uploader.h"
#include "power_management.h"
#include "state_machine.h"
#include "time.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"
#include "wifi_connect.h"

static const char* TAG = "display";
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define LEGEND_HEIGHT 9
#define LOGO_WIDTH 34
#define LOGO_HEIGHT 34
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
static TimerHandle_t animation_timer;
static void vTimerCallback(TimerHandle_t timer) {
  xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
}

static void battery(u8g2_t* u8g2) {
  if (usb_voltage > 0 && battery_voltage > 2000) {
    // pluged in / battery full
  } else if (usb_voltage > 0) {
    // charging

  } else {
    // running on battery
  }

  int offset = DISPLAY_WIDTH - 1;

  if (usb_voltage > 1000) {
    // charger
    u8g2_DrawHLine(u8g2, offset - 2, 1, 2);
    u8g2_DrawHLine(u8g2, offset - 2, 3, 2);
    u8g2_DrawBox(u8g2, offset - 6, 0, 4, 5);
    u8g2_DrawVLine(u8g2, offset - 7, 1, 3);
    u8g2_DrawHLine(u8g2, offset - 10, 2, 3);
  } else {
    // battery
    int BATTERY_WIDTH = 8;
    u8g2_DrawFrame(u8g2, offset - BATTERY_WIDTH - 2, 0, 8, 5);
    int bar_width =
        (battery_voltage - BATTERY_MIN) * (BATTERY_WIDTH - 1) / (BATTERY_MAX - BATTERY_MIN);
    u8g2_DrawBox(u8g2, offset - BATTERY_WIDTH - 1, 1, bar_width, 3);
    u8g2_DrawVLine(u8g2, offset - 1, 1, 3);
  }

  u8g2_SetFont(u8g2, u8g2_font_tiny5_tr);
  char buffer[10];
  int percentage = battery_voltage * 100 / BATTERY_MAX;
  if (percentage > 100) {
    percentage = 100;
  }
  snprintf(buffer, sizeof(buffer), "%d%%", percentage);
  u8g2_uint_t w = u8g2_GetStrWidth(u8g2, buffer);
  u8g2_DrawStr(u8g2, DISPLAY_WIDTH - w - 13, 5, buffer);
}

static int animation_tick(int ms) {
  static int64_t last_time_ms = 0;
  if (animation_timer == NULL) {
    animation_timer = xTimerCreate("animation_timer", pdMS_TO_TICKS(ms), pdTRUE, 0, vTimerCallback);
  } else {
    xTimerReset(animation_timer, pdMS_TO_TICKS(ms));
  }
  int64_t now = esp_timer_get_time() / 1000;
  if (now - last_time_ms >= ms) {
    last_time_ms = now;
  }
  return last_time_ms;
}

static void wifi_strength(u8g2_t* u8g2) {
  static int skip = -1;
  int x = 0;
  int y = 4;
  int bars = 0;

  switch (wifi_status) {
    case CONNECTING:
      skip = animation_tick(250) % 4;
      break;

    case CONNECTED:
      skip = -1;
      if (wifi_rssi > -55) {
        bars = 4;
      } else if (wifi_rssi > -66) {
        bars = 3;
      } else if (wifi_rssi > -77) {
        bars = 2;
      } else {
        bars = 1;
      }
      break;

    case DISCONNECTED:
      skip = -1;
      break;
  }

  for (int step = 0; step < 4; step++) {
    if (step == skip) {
      continue;
    }
    u8g2_DrawLine(u8g2, x + (step * 2), y, x + (step * 2), step < bars ? y - 1 - step : y);
  }
}

static void pending_uploads(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_tiny5_tr);
  char pending[4];
  sprintf(pending, "%3d", log_count);
  // render right aligned
  u8g2_uint_t w = u8g2_GetStrWidth(u8g2, pending);
  u8g2_DrawStr(u8g2, DISPLAY_WIDTH - 16 - w, 5, pending);
  u8g2_SetFont(u8g2, u8g2_font_m2icon_5_tf);
  u8g2_DrawStr(u8g2, DISPLAY_WIDTH - 18 - w, 5, "B");
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
  u8g2_DrawRBox(u8g2, offset + 1, DISPLAY_HEIGHT - LEGEND_HEIGHT + 1, LEGEND_HEIGHT - 1, 7, 1);
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_tiny5_tr);
  u8g2_DrawStr(u8g2, offset + 3, DISPLAY_HEIGHT - 2, letter);
  u8g2_SetDrawColor(u8g2, 0);
  u8g2_DrawStr(u8g2, offset + 11, DISPLAY_HEIGHT - 2, text);
  u8g2_SetDrawColor(u8g2, 1);
}

static void keypad_legend(u8g2_t* u8g2) {
  u8g2_DrawBox(u8g2, 0, DISPLAY_HEIGHT - LEGEND_HEIGHT, DISPLAY_WIDTH, LEGEND_HEIGHT);
  keypad_legend_letter(u8g2, "A", "Up", 0);
  keypad_legend_letter(u8g2, "B", "Dn", 26);
  keypad_legend_letter(u8g2, "*", "OK", 52);
  keypad_legend_letter(u8g2, "D", "Abbrechen", 78);
}

static void status_bar(u8g2_t* u8g2) {
  battery(u8g2);
  wifi_strength(u8g2);
  // pending_uploads(u8g2);
  time_display(u8g2);
}

static void boot_screen(u8g2_t* u8g2) {
  u8g2_DrawXBM(
      u8g2,
      DISPLAY_WIDTH / 2 - LOGO_WIDTH / 2,
      DISPLAY_HEIGHT / 2 - LOGO_HEIGHT / 2,
      LOGO_WIDTH,
      LOGO_HEIGHT,
      &logo_bits
  );
}

static void draw_amount(u8g2_t* u8g2, int amount, int y) {
  char amount_str[7];
  snprintf(amount_str, sizeof(amount_str), "%5.2f", ((float)amount) / 100);
  u8g2_uint_t w = u8g2_GetStrWidth(u8g2, amount_str);
  u8g2_DrawStr(u8g2, DISPLAY_WIDTH - w, y, amount_str);
}

static void charge_list(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  int LINE_HEIGHT = 11;

  for (int i = 0; i < current_state.cart.item_count && i < 3; i++) {
    char product[17];
    if (i == 2 && current_state.cart.item_count > 3) {
      int sum = 0;
      int amount = 0;
      for (int j = 2; j < current_state.cart.item_count; j++) {
        sum += current_state.cart.items[j].amount * current_state.cart.items[j].product.price;
        amount += current_state.cart.items[j].amount;
      }
      snprintf(product, sizeof(product), "+%d weitere", amount);
      u8g2_DrawStr(u8g2, 0, 17 + (i * LINE_HEIGHT), product);
      draw_amount(u8g2, sum, 17 + (i * LINE_HEIGHT));
      break;
    }

    snprintf(
        product,
        sizeof(product),
        "%ld %.14s",
        current_state.cart.items[i].amount,
        current_state.cart.items[i].product.name
    );
    if (strlen(product) > 16) {
      product[15] = '_';
    }
    u8g2_DrawStr(u8g2, 0, 17 + (i * LINE_HEIGHT), product);
    draw_amount(
        u8g2,
        current_state.cart.items[i].amount * current_state.cart.items[i].product.price,
        17 + (i * LINE_HEIGHT)
    );
  }

  char deposit[21];
  if (current_state.cart.deposit < 0) {
    snprintf(deposit, sizeof(deposit), "%d Rückgabe", current_state.cart.deposit * -1);
  } else {
    snprintf(deposit, sizeof(deposit), "%d Pfand", current_state.cart.deposit);
  }
  u8g2_DrawStr(u8g2, 0, DISPLAY_HEIGHT - 13, deposit);
  draw_amount(u8g2, current_state.cart.deposit * 200, DISPLAY_HEIGHT - 13);
  u8g2_DrawHLine(u8g2, 0, DISPLAY_HEIGHT - 11, DISPLAY_WIDTH);
  u8g2_DrawStr(u8g2, 0, DISPLAY_HEIGHT - 1, "Summe");
  draw_amount(
      u8g2, current_state.cart.total + (current_state.cart.deposit * 200), DISPLAY_HEIGHT - 1
  );
}

static void charge_list_two_digit(u8g2_t* u8g2) {
  char* str = "Produkt #__";
  if (current_state.product_list_first_digit > 0) {
    str[9] = current_state.product_list_first_digit + '0';
  }
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  u8g2_DrawStr(u8g2, 0, 20, str);
  keypad_legend(u8g2);
}

static void main_menu(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  for (int i = 0; i < 3; i++) {
    int offset = current_state.main_menu.active_item - 1;
    if (current_state.main_menu.active_item == 0) {
      offset = 0;
    } else if (current_state.main_menu.active_item == current_state.main_menu.count - 1) {
      offset = current_state.main_menu.count - 3;
    }

    u8g2_DrawStr(u8g2, 4, 18 + (i * 15), current_state.main_menu.items[offset + i].name);

    // highlight active item
    if (offset + i == current_state.main_menu.active_item) {
      u8g2_DrawRFrame(u8g2, 0, 8 + (i * 15), DISPLAY_WIDTH - 8, 15, 3);
      u8g2_DrawHLine(u8g2, 2, 21 + (i * 15), DISPLAY_WIDTH - 12);
      u8g2_DrawVLine(u8g2, DISPLAY_WIDTH - 10, 9 + (i * 15), 13);
    }
  }

  // scrollbar
  for (int i = 7; i < DISPLAY_HEIGHT - 11; i = i + 3) {
    u8g2_DrawPixel(u8g2, DISPLAY_WIDTH - 4, i);
  }
  u8g2_DrawBox(
      u8g2,
      DISPLAY_WIDTH - 5,
      7 + ((float)current_state.main_menu.active_item / (float)current_state.main_menu.count) * 45,
      3,
      4
  );
}

static void write_card(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  u8g2_DrawStr(u8g2, 0, 17, "Bitte Karte");
  u8g2_DrawStr(u8g2, 0, 27, "auflegen");
  u8g2_DrawStr(u8g2, 0, 37, "zum Aufladen");
  u8g2_DrawStr(u8g2, 0, 47, "oder Abbrechen");
}

static void charge_without_card(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  char label[13];
  for (int i = 0; i < 3; i++) {
    switch (i) {
      case 0:
        strncpy(label, "Crew", sizeof(label));
        break;
      case 1:
        strncpy(label, "Barzahlung", sizeof(label));
        break;
      case 2:
        strncpy(label, "Gutschein", sizeof(label));
        break;
    }
    u8g2_DrawRFrame(u8g2, 0, 9 + (i * 15), 13, 13, 2);
    char number[2];
    snprintf(number, sizeof(number), "%d", i + 1);
    u8g2_DrawStr(u8g2, 4, 19 + (i * 15), number);
    u8g2_DrawStr(u8g2, 18, 19 + (i * 15), label);
  }
}

void show_message(u8g2_t* u8g2) {
  int padding = 20;
  u8g2_DrawRBox(
      u8g2, padding, padding, DISPLAY_WIDTH - 2 * padding, DISPLAY_HEIGHT - 2 * padding, 3
  );
}

void display(void* params) {
  // 200ms delay to allow other tasks to start
  vTaskDelay(200 / portTICK_PERIOD_MS);

  u8g2_esp32_hal_t u8g2_esp32_hal = {
      .clk = 11,
      .mosi = 12,
      .cs = 10,
      .dc = 13,
      .reset = 9,
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
      case CHARGE_WITHOUT_CARD:
        status_bar(&u8g2);
        charge_without_card(&u8g2);
        keypad_legend(&u8g2);
        break;
      case CHARGE_LIST_TWO_DIGIT:
        status_bar(&u8g2);
        charge_list_two_digit(&u8g2);
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
    xEventGroupWaitBits(event_group, DISPLAY_NEEDS_UPDATE, pdFALSE, pdTRUE, portMAX_DELAY);
  }
}
