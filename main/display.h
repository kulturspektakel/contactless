#pragma once

void display(void* params);

typedef enum {
  MENU_CONFIG,
  MENU_UPDATE,
  MENU_DEVICE,
  MENU_SILENT_MODE,
  MENU_WIFI,
  MENU_UPLOADS,
  MENU_USB,
  MENU_BATTERY,
  MENU_VERSION,
  MENU_ANTENNA_TEST,
  MENU_BATTERY_TEST,

  // keep last
  MENU_COUNT
} main_menu_t;