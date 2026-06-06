#pragma once

void display(void* params);

// Number of menu items currently visible. MENU_PRINT_CONFIG is hidden unless a
// printer is connected, so the count shrinks by one when it isn't.
int main_menu_visible_count(void);

typedef enum {
  MENU_CONFIG,
  MENU_UPDATE,
  MENU_DEVICE,
  MENU_SOUND,
  MENU_WIFI,
  MENU_PRINTER,
  MENU_UPLOADS,
  MENU_INITIALIZE_CARD,
  MENU_USB,
  MENU_BATTERY,
  MENU_VERSION,
  MENU_BATTERY_TEST,
  MENU_BUZZER_TEST,
  // MENU_PRINT_CONFIG must stay last (before MENU_COUNT): it is hidden unless a
  // printer is connected, which we do by shrinking the visible count by one.
  MENU_PRINT_CONFIG,

  // keep last
  MENU_COUNT
} main_menu_t;