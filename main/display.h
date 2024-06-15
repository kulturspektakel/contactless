#pragma once

void display(void* params);

typedef enum {
  MENU_CONFIG,
  MENU_UPDATE,
  MENU_DEVICE,
  MENU_WIFI,
  MENU_UPLOADS,
  MENU_USB,
  MENU_BATTERY,
  MENU_VERSION,

  // keep last
  MENU_COUNT
} main_menu_t;