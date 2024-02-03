#include "esp_log.h"
#include "esp_timer.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "local_config.h"
#include "log_uploader.h"
#include "power_management.h"
#include "rfid.h"
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
static void animation_timer_cb(TimerHandle_t timer) {
  xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
}

static void battery(u8g2_t* u8g2) {
  int offset = DISPLAY_WIDTH - 1;

  if (usb_voltage > 1000) {
    // charger icon
    u8g2_DrawHLine(u8g2, offset - 2, 1, 2);
    u8g2_DrawHLine(u8g2, offset - 2, 3, 2);
    u8g2_DrawBox(u8g2, offset - 6, 0, 4, 5);
    u8g2_DrawVLine(u8g2, offset - 7, 1, 3);
    u8g2_DrawHLine(u8g2, offset - 10, 2, 3);
  } else {
    // battery icon
    int BATTERY_WIDTH = 8;
    int percentage = battery_percentage();
    u8g2_DrawFrame(u8g2, offset - BATTERY_WIDTH - 2, 0, 8, 5);
    int bar_width = (percentage * (BATTERY_WIDTH - 1)) / 100;
    u8g2_DrawBox(u8g2, offset - BATTERY_WIDTH - 1, 1, bar_width, 3);
    u8g2_DrawVLine(u8g2, offset - 1, 1, 3);

    // percentage string
    u8g2_SetFont(u8g2, u8g2_font_tiny5_tr);
    char buffer[10];
    snprintf(buffer, sizeof(buffer), "%d%%", percentage);
    u8g2_uint_t w = u8g2_GetStrWidth(u8g2, buffer);
    u8g2_DrawStr(u8g2, DISPLAY_WIDTH - w - 13, 5, buffer);
  }
}

static bool animation_tick(int ms, int64_t* last_time_ms) {
  if (animation_timer == NULL) {
    animation_timer =
        xTimerCreate("animation_timer", pdMS_TO_TICKS(ms), pdFALSE, NULL, animation_timer_cb);
    xTimerReset(animation_timer, 0);
  } else if (xTimerIsTimerActive(animation_timer) == pdFALSE) {
    xTimerChangePeriod(animation_timer, pdMS_TO_TICKS(ms), 0);
  }
  int64_t now = esp_timer_get_time() / 1000;
  if (now - *last_time_ms >= ms - 10) {  // 10ms tolerance
    *last_time_ms = now;
    return true;
  }
  return false;
}

static void wifi_strength(u8g2_t* u8g2) {
  static int skip = -1;
  static int64_t last_time_ms = 0;
  int x = 0;
  int y = 4;
  int bars = 0;

  switch (wifi_status) {
    case CONNECTING:
      if (animation_tick(200, &last_time_ms)) {
        skip = (skip + 1) % 5;
      }
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
  sprintf(pending, "%3d", current_state.log_files_to_upload);
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
  if (text != NULL) {
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawStr(u8g2, offset + 11, DISPLAY_HEIGHT - 2, text);
    u8g2_SetDrawColor(u8g2, 1);
  }
}

static void keypad_legend(u8g2_t* u8g2, bool with_navigation) {
  u8g2_DrawBox(u8g2, 0, DISPLAY_HEIGHT - LEGEND_HEIGHT, DISPLAY_WIDTH, LEGEND_HEIGHT);
  if (with_navigation) {
    keypad_legend_letter(u8g2, "B", NULL, 26);
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawTriangle(u8g2, 37, DISPLAY_HEIGHT - 5, 39, DISPLAY_HEIGHT - 2, 42, DISPLAY_HEIGHT - 5);
    u8g2_DrawLine(u8g2, 39, DISPLAY_HEIGHT - 6, 39, DISPLAY_HEIGHT - 7);
    u8g2_SetDrawColor(u8g2, 1);
    keypad_legend_letter(u8g2, "#", "OK", 52);
    keypad_legend_letter(u8g2, "A", NULL, 0);
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawTriangle(u8g2, 10, DISPLAY_HEIGHT - 4, 13, DISPLAY_HEIGHT - 8, 17, DISPLAY_HEIGHT - 4);
    u8g2_DrawLine(u8g2, 13, DISPLAY_HEIGHT - 3, 13, DISPLAY_HEIGHT - 5);
    u8g2_SetDrawColor(u8g2, 1);
  }
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

static void charge_total(u8g2_t* u8g2, int y) {
  char deposit[21];
  if (current_state.cart.deposit < 0) {
    snprintf(deposit, sizeof(deposit), "%d Rückgabe", current_state.cart.deposit * -1);
  } else {
    snprintf(deposit, sizeof(deposit), "%d Pfand", current_state.cart.deposit);
  }
  u8g2_DrawUTF8(u8g2, 0, y, deposit);
  draw_amount(u8g2, current_state.cart.deposit * 200, y);
  u8g2_DrawHLine(u8g2, 0, y + 2, DISPLAY_WIDTH);
  u8g2_DrawStr(u8g2, 0, y + 12, "Summe");
  draw_amount(u8g2, current_total(), y + 12);
}

static void charge_list(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_profont11_tf);
  int LINE_HEIGHT = 11;

  for (int i = 0; i < current_state.cart.item_count && i < 3; i++) {
    char product[18];
    if (i == 2 && current_state.cart.item_count > 3) {
      int sum = 0;
      int amount = 0;
      for (int j = 2; j < current_state.cart.item_count; j++) {
        sum += current_state.cart.items[j].amount * current_state.cart.items[j].product.price;
        amount += current_state.cart.items[j].amount;
      }
      snprintf(product, sizeof(product), "+%d weitere", amount);
      u8g2_DrawUTF8(u8g2, 0, 17 + (i * LINE_HEIGHT), product);
      draw_amount(u8g2, sum, 17 + (i * LINE_HEIGHT));
      break;
    }

    snprintf(
        product,
        sizeof(product),
        "%ld %.15s",
        current_state.cart.items[i].amount,
        current_state.cart.items[i].product.name
    );
    int w = u8g2_GetUTF8Width(u8g2, product);
    if (w > DISPLAY_WIDTH - 40) {
      product[16] = 0x85;  // ellipsis
    }
    u8g2_DrawUTF8(u8g2, 0, 17 + (i * LINE_HEIGHT), product);
    draw_amount(
        u8g2,
        current_state.cart.items[i].amount * current_state.cart.items[i].product.price,
        17 + (i * LINE_HEIGHT)
    );
  }

  charge_total(u8g2, DISPLAY_HEIGHT - 13);
}

static void scrollable_list(
    u8g2_t* u8g2,
    void (*callback)(u8g2_t*, int, int, int),
    int total,
    int selected,
    int active
) {
  u8g2_SetFont(u8g2, u8g2_font_profont11_tf);
  int ROWS = 3;
  int min = total < ROWS ? total : 3;
  bool scrollable = total > ROWS;
  for (int i = 0; i < min; i++) {
    int offset = 0;
    if (selected == 0) {
      offset = -1;
    } else if (selected == total - 1 && total > 2) {
      offset = 1;
    }

    int current = selected + i - 1 - offset;
    int scrollbar_offset = scrollable ? 4 : 0;
    // highlight active item
    if (offset == i - 1) {
      if (current == active) {
        u8g2_DrawRBox(u8g2, 0, 8 + (i * 15), DISPLAY_WIDTH - scrollbar_offset - 1, 15, 3);
      } else {
        u8g2_DrawRFrame(u8g2, 0, 8 + (i * 15), DISPLAY_WIDTH - scrollbar_offset - 1, 15, 3);
        u8g2_DrawHLine(u8g2, 2, 21 + (i * 15), DISPLAY_WIDTH - scrollbar_offset - 5);
        u8g2_DrawVLine(u8g2, DISPLAY_WIDTH - scrollbar_offset - 3, 9 + (i * 15), 13);
      }
    }

    if (current == active && i == offset + 1) {
      u8g2_SetDrawColor(u8g2, 0);
    }
    callback(u8g2, current, 4, 18 + (i * 15));
    if (current == active && i == offset + 1) {
      u8g2_SetDrawColor(u8g2, 1);
    }
  }

  // scrollbar
  if (!scrollable) {
    return;  // no scrollbar needed
  }
  int rail_height = DISPLAY_HEIGHT - 7 /* status_bar */ - 11 /* bottom*/;
  for (int i = 0; i < rail_height; i = i + 3) {
    u8g2_DrawPixel(u8g2, DISPLAY_WIDTH - 2, i + 7 /* status_bar */);
  }
  int handle_height = ((float)ROWS / total) * rail_height;
  int hidden_top = selected - 1;
  if (hidden_top < 0) {
    hidden_top = 0;
  }
  float percentage = ((float)hidden_top) / (total - 3);
  if (percentage > 1) {
    percentage = 1;
  }
  int handle_y = percentage * (rail_height - handle_height);
  u8g2_DrawBox(u8g2, DISPLAY_WIDTH - 3, 7 /* status_bar*/ + handle_y, 3, handle_height);
}

static void product_list_cb(u8g2_t* u8g2, int i, int x, int y) {
  char number[3];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
  snprintf(number, sizeof(number), "%02d", i + 1);
#pragma GCC diagnostic pop
  u8g2_SetFont(u8g2, u8g2_font_tiny5_tr);
  u8g2_DrawStr(u8g2, DISPLAY_WIDTH - 17, y - 1, number);
  u8g2_SetFont(u8g2, u8g2_font_profont11_tf);
  int w = u8g2_GetUTF8Width(u8g2, active_config.products[i].name);
  if (w > DISPLAY_WIDTH - 22) {
    char name[18];
    strncpy(name, active_config.products[i].name, 16);
    name[16] = 0x85;  // ellipsis
    name[17] = '\0';
    u8g2_DrawUTF8(u8g2, x, y, name);
  } else {
    u8g2_DrawUTF8(u8g2, x, y, active_config.products[i].name);
  }
}

static void product_list(u8g2_t* u8g2) {
  scrollable_list(
      u8g2,
      product_list_cb,
      active_config.products_count,
      current_state.product_selection.current_index,
      current_state.product_selection.second_digit > -1
          ? current_state.product_selection.current_index
          : -1
  );
}

static void main_menu_cb(u8g2_t* u8g2, int i, int x, int y) {
  u8g2_DrawUTF8(u8g2, x, y, current_state.main_menu.items[i].name);
}

static void main_menu(u8g2_t* u8g2) {
  int active_config_index = -1;
  for (int i = 0; i < current_state.main_menu.count; i++) {
    if (current_state.main_menu.items[i].list_id == active_config.list_id) {
      active_config_index = i;
      break;
    }
  }

  scrollable_list(
      u8g2,
      main_menu_cb,
      current_state.main_menu.count,
      current_state.main_menu.active_item,
      active_config_index
  );
}

static void write_card(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_profont11_tf);

  int y = DISPLAY_HEIGHT / 2;
  int h = 10;
  int p = 5;
  u8g2_DrawRFrame(u8g2, p, y, DISPLAY_WIDTH - 2 * p, h, 2);
  static int64_t last_animation_tick = 0;
  static int progress = 0;
  if (animation_tick(50, &last_animation_tick)) {
    progress = (progress + 1) % 100;
  }
  u8g2_DrawBox(u8g2, p + 1, y + 1, (DISPLAY_WIDTH - 2 * p - 2) * progress / 100, h - 2);
}

static void card_balance(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_profont29_tf);
  char balance[6];
  snprintf(balance, sizeof(balance), "%2.2f0", ((float)current_card.balance) / 100);
  balance[2] = ',';
  int w = u8g2_GetStrWidth(u8g2, balance);
  int e = 8;  // euro symbol width
  int h = 36;
  u8g2_DrawStr(u8g2, (DISPLAY_WIDTH - w) / 2 - e, h, balance);

  // Euro symbol
  u8g2_DrawUTF8(u8g2, DISPLAY_WIDTH / 2 + w / 2 + 4 - e, h, "€");
  // u8g2_SetDrawColor(u8g2, 0);
  // u8g2_DrawBox(u8g2, DISPLAY_WIDTH / 2 + w / 2 + 7 - e, h - 15, 11, 11);
  // u8g2_SetDrawColor(u8g2, 1);
  // u8g2_DrawBox(u8g2, DISPLAY_WIDTH / 2 + w / 2 + 2 - e, h - 9, 10, 3);
  // u8g2_DrawBox(u8g2, DISPLAY_WIDTH / 2 + w / 2 + 2 - e, h - 13, 10, 3);

  u8g2_SetFont(u8g2, u8g2_font_profont11_tf);
  char* str = "0 Pfandmarken";
  str[0] = '0' + current_card.deposit;
  if (current_card.deposit == 1) {
    str[12] = '\0';
  }
  w = u8g2_GetStrWidth(u8g2, str);
  u8g2_DrawStr(u8g2, (DISPLAY_WIDTH - w) / 2, 52, str);
}

static void charge_without_card(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_profont11_tf);
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

static void enter_amount(u8g2_t* u8g2, int amount) {
  char str[7];
  snprintf(str, sizeof(str), "%2.2f", ((float)current_state.manual_amount) / 100);
  int w = 39;
  int y = 7;
  int x = DISPLAY_WIDTH - w;
  u8g2_DrawStr(u8g2, DISPLAY_WIDTH - u8g2_GetStrWidth(u8g2, str) - 5, y + 10, str);
  u8g2_DrawRFrame(u8g2, x, y, w, 13, 3);
  static int64_t last_animation_tick = 0;
  static bool cursor = false;
  if (animation_tick(500, &last_animation_tick)) {
    cursor = !cursor;
  }
  if (cursor) {
    u8g2_DrawLine(u8g2, x + w - 4, y + 2, x + w - 4, y + 10);
  }
}

static void charge_manual(u8g2_t* u8g2) {
  u8g2_SetFont(u8g2, u8g2_font_profont11_tf);
  u8g2_DrawStr(u8g2, 0, 17, "Manuell");
  enter_amount(u8g2, current_state.manual_amount);
  charge_total(u8g2, DISPLAY_HEIGHT - LEGEND_HEIGHT - 14);
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
        keypad_legend(&u8g2, false);
        break;
      case PRODUCT_LIST:
        status_bar(&u8g2);
        product_list(&u8g2);
        keypad_legend(&u8g2, true);
        break;
      case MAIN_MENU:
        status_bar(&u8g2);
        main_menu(&u8g2);
        keypad_legend(&u8g2, true);
        break;
      case WRITE_CARD:
        status_bar(&u8g2);
        write_card(&u8g2);
        break;
      case CARD_BALANCE:
        status_bar(&u8g2);
        card_balance(&u8g2);
        break;
      case CHARGE_MANUAL:
        status_bar(&u8g2);
        charge_manual(&u8g2);
        keypad_legend(&u8g2, false);
        break;
      default:
        break;
    }

    u8g2_SendBuffer(&u8g2);
    xEventGroupWaitBits(event_group, DISPLAY_NEEDS_UPDATE, pdTRUE, pdTRUE, portMAX_DELAY);
  }
}
