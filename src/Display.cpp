#include "Display.h"
#include <ESP8266WiFi.h>
#include <TimeLib.h>
#include "Buildnumber.h"
#include "StateManager.h"

extern const int TOKEN_VALUE;
extern const int YEAR;
extern const char* WIFI_SSID;

byte U_UMLAUT[8] = {
    // clang-format off
    B01010,
    B00000,
    B10001,
    B10001,
    B10001,
    B10011,
    B01101,
    B00000,
    // clang-format on
};

Display::Display(StateManager& sm) : stateManager(sm) {
  lcd = new LiquidCrystal_I2C(PCF8574_address::PCF8574_ADDR_A21_A11_A01);
}

Display::~Display() {
  delete lcd;
}

void Display::setup() {
  lcd->begin(16, 2);
  lcd->backlight();
  lcd->createChar(0, U_UMLAUT);
}

void Display::loop() {
  if (messageUntil > 0 && messageUntil < millis()) {
    // reset view
    messageUntil = 0;
    updateScreen(true);
  }
}

void Display::message(const char* message, int duration) {
  messageUntil = millis() + duration;
  lcd->clear();
  lcd->setCursor(0, 0);
  lcd->print(message);
  if (strlen(message) > 16) {
    lcd->setCursor(0, 1);
    char lineTwo[17];
    memcpy(lineTwo, &message[16], 16);
    lcd->print(lineTwo);
  }
}

void Display::messageWithPrice(const char* m, int price, int duration) {
  char line[33] = "                                ";
  memcpy(line, m, strlen(m));
  if (strlen(m) < 11) {
    formatCurrency(line, price);
  } else {
    formatCurrency(&line[16], price);
  }
  message(line, duration);
}

void Display::updateScreen(bool clear) {
  if (!clear && messageUntil > millis()) {
    // message is active, don't update screen
    return;
  }

  clearMessage();

  char display[17];
  if (stateManager.state.transaction.mode ==
      TransactionMessage_Mode_TIME_ENTRY) {
    lcd->setCursor(0, 0);
    lcd->print("Datum/Uhrzeit?  ");

    size_t length = strlen(stateManager.state.entry);
    display[0] = length > 0 ? stateManager.state.entry[0] : '_';
    display[1] = length > 1 ? stateManager.state.entry[1] : '_';
    display[2] = '.';
    display[3] = length > 2 ? stateManager.state.entry[2] : '_';
    display[4] = length > 3 ? stateManager.state.entry[3] : '_';
    display[5] = '.';
    itoa(YEAR, display + 6, DEC);
    display[10] = ' ';
    display[11] = length > 4 ? stateManager.state.entry[4] : '_';
    display[12] = length > 5 ? stateManager.state.entry[5] : '_';
    display[13] = ':';
    display[14] = length > 6 ? stateManager.state.entry[6] : '_';
    display[15] = length > 7 ? stateManager.state.entry[7] : '_';

    lcd->setCursor(0, 1);
    lcd->print(display);
  } else if (stateManager.state.transaction.mode ==
             TransactionMessage_Mode_TOP_UP) {
    lcd->setCursor(0, 0);
    strcpy(display, "Aufladen        ");
    formatCurrency(display, stateManager.state.value);
    lcd->print(display);
    lcd->setCursor(0, 1);
    char a[9];
    sprintf(a, "%d Pfandr", abs(stateManager.state.tokens));
    lcd->print(a);
    lcd->write(0);
    lcd->print("ckgabe ");
  } else if (stateManager.state.transaction.mode ==
             TransactionMessage_Mode_CHARGE) {
    lcd->setCursor(0, 0);
    if (stateManager.state.manualEntry) {
      strcpy(display, "Manuell         ");
    } else {
      strcpy(display, "Preis           ");
    }
    formatCurrency(display, stateManager.state.value);
    lcd->print(display);
    lcd->setCursor(0, 1);
    if (stateManager.state.tokens < 0) {
      display[0] = '-';
    } else {
      display[0] = '+';
    }
    itoa(abs(stateManager.state.tokens), display + 1, DEC);
    strcpy(display + 2, " Pfand        ");
    formatCurrency(display, stateManager.state.tokens * TOKEN_VALUE);
    lcd->print(display);
  } else if (stateManager.state.transaction.mode ==
             TransactionMessage_Mode_CASHOUT) {
    lcd->clear();
    lcd->setCursor(0, 0);
    lcd->print("Barauszahlung?");
  } else if (stateManager.state.transaction.mode ==
             TransactionMessage_Mode_INIT_CARD) {
    lcd->clear();
    lcd->setCursor(0, 0);
    lcd->print("Karte");
    lcd->setCursor(0, 1);
    lcd->print("initialisiern?");
  }
}

void Display::formatCurrency(char arr[17], int value) {
  int absValue = abs(value);
  if (absValue >= 1000) {
    arr[10] = value < 0 ? '-' : ' ';
    itoa(absValue / 1000 % 10, arr + 11, DEC);
  } else {
    arr[10] = ' ';
    arr[11] = value < 0 ? '-' : ' ';
  }
  itoa(absValue / 100 % 10, arr + 12, DEC);
  arr[13] = '.';
  itoa(absValue / 10 % 10, arr + 14, DEC);
  itoa(absValue % 10, arr + 15, DEC);
}

void Display::showBalance(int duration) {
  messageUntil = millis() + duration;
  lcd->setCursor(0, 0);
  char print[] = "Guthaben       ";
  formatCurrency(print, stateManager.state.transaction.balanceBefore);
  lcd->print(print);
  lcd->setCursor(0, 1);
  sprintf(print, "%d Pfandmarken   ",
          stateManager.state.transaction.tokensBefore);
  lcd->print(print);
}

void Display::clearMessage() {
  messageUntil = 0;
}

void Display::showCashout(int cashout) {
  messageUntil = millis() + 5000;
  lcd->setCursor(0, 0);
  lcd->print("OK                ");
  lcd->setCursor(0, 1);
  char print[] = "Auszahlen      ";
  formatCurrency(print, cashout);
  lcd->print(print);
}

void Display::showDebug(bool startDebugMode, int pendingLogs) {
  if (startDebugMode) {
    currentDebugPage = 0;
  }
  if (currentDebugPage < 0) {
    return;
  }
  char m[33];
  int NUMBER_OF_PAGES = 7;

  switch (currentDebugPage) {
    case 0:
      snprintf(m, 33, "ID:             %s",
               WiFi.macAddress().substring(9).c_str());
      break;
    case 1:
      snprintf(m, 33, "Products:       %s",
               stateManager.state.config.name != '\0'
                   ? stateManager.state.config.name
                   : "none");
      break;
    case 2:
      snprintf(m, 33, "Time:           %02d.%02d.%04d %02d:%02d", day(),
               month(), year(), hour(), minute());
      break;
    case 3:
      snprintf(m, 33, "WiFi: %s       %s",
               WiFi.status() == WL_CONNECTED ? "on " : "off", WIFI_SSID);
      break;
    case 4:
      snprintf(m, 33, "Logs pending:   %d", pendingLogs);
      break;
    case 5:
      snprintf(m, 33, "Software version%d", BUILD_NUMBER);
      break;
    case 6:
      snprintf(m, 33, "Reload config...");
      stateManager.reload();
      break;
  }
  message(m, 5000);

  currentDebugPage++;
  if (currentDebugPage == NUMBER_OF_PAGES) {
    currentDebugPage = -1;
    clearMessage();
  }
}
