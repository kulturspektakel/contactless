#include "DisplayCoroutine.h"
#include <ArduinoLog.h>
#include "MainCoroutine.h"
#include "TimeEntryCoroutine.h"

extern const int TOKEN_VALUE;

static byte UPPER_CASE_A_UMLAUT[8] = {
    // clang-format off
    B10001,
    B00100,
    B01010,
    B10001,
    B11111,
    B10001,
    B10001,
    B00000
    // clang-format on
};

static byte UPPER_CASE_O_UMLAUT[8] = {
    // clang-format off
    B10001,
    B01110,
    B10001,
    B10001,
    B10001,
    B10001,
    B01110,
    B00000
    // clang-format on
};

static byte UPPER_CASE_U_UMLAUT[8] = {
    // clang-format off
    B10001,
    B00000,
    B10001,
    B10001,
    B10001,
    B10001,
    B01110,
    B00000
    // clang-format on
};

static byte LOWER_CASE_A_UMLAUT[8] = {
    // clang-format off
    B01010,
    B00000,
    B01110,
    B00001,
    B01111,
    B10001,
    B01111,
    B00000
    // clang-format on
};

static byte LOWER_CASE_O_UMLAUT[8] = {
    // clang-format off
    B01010,
    B00000,
    B01110,
    B10001,
    B10001,
    B10001,
    B01110,
    B00000
    // clang-format on
};

static byte LOWER_CASE_U_UMLAUT[8] = {
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

static byte LOWER_CASE_SHARP_S[8] = {
    // clang-format off
    B01110,
    B10001,
    B10001,
    B10110,
    B10001,
    B10001,
    B10110,
    B10000
    // clang-format on
};

extern MainCoroutine mainCoroutine;
extern TimeEntryCoroutine timeEntryCoroutine;
static hd44780_I2Cexp lcd(0x27);

int DisplayCoroutine::runCoroutine() {
  COROUTINE_BEGIN();
  lcd.begin(16, 2);
  lcd.createChar(1, UPPER_CASE_A_UMLAUT);
  lcd.createChar(2, UPPER_CASE_O_UMLAUT);
  lcd.createChar(3, UPPER_CASE_U_UMLAUT);
  lcd.createChar(4, LOWER_CASE_A_UMLAUT);
  lcd.createChar(5, LOWER_CASE_O_UMLAUT);
  lcd.createChar(6, LOWER_CASE_U_UMLAUT);
  lcd.createChar(7, LOWER_CASE_SHARP_S);
  lcd.clear();
  Log.traceln("[Display] init");

  while (true) {
    if (messageUntil > millis()) {
      // displaying message
      COROUTINE_YIELD();
      continue;
    }

    if (messageUntil > 0 && messageUntil < millis()) {
      requiresUpdate = true;
      messageUntil = 0;
    }

    if (!requiresUpdate) {
      COROUTINE_YIELD();
      continue;
    }

    requiresUpdate = false;

    switch (mainCoroutine.mode) {
      case TIME_ENTRY:
        char line1[17];
        sprintf(line1, "%c%c.%c%c.20%c%c %c%c:%c%c",
                timeEntryCoroutine.value[0] ?: '_',
                timeEntryCoroutine.value[1] ?: '_',
                timeEntryCoroutine.value[2] ?: '_',
                timeEntryCoroutine.value[3] ?: '_',
                timeEntryCoroutine.value[4] ?: '_',
                timeEntryCoroutine.value[5] ?: '_',
                timeEntryCoroutine.value[6] ?: '_',
                timeEntryCoroutine.value[7] ?: '_',
                timeEntryCoroutine.value[8] ?: '_',
                timeEntryCoroutine.value[9] ?: '_');
        show("Datum/Uhrzeit?", line1);
        break;
      case CHARGE_MANUAL:
        show("Manuell", "Pfand", mainCoroutine.balance.total,
             mainCoroutine.balance.deposit * TOKEN_VALUE);
        break;
      case CHARGE_LIST:
        show("Preis", "Pfand", mainCoroutine.balance.total,
             mainCoroutine.balance.deposit * TOKEN_VALUE);
        break;
      case TOP_UP:
        show("Aufladen", "Pfand", mainCoroutine.balance.total,
             mainCoroutine.balance.deposit * TOKEN_VALUE);
        break;
      case CASH_OUT:
        show("Karte auszahlen?");
        break;
      default:
        show("Home");
        break;
    }
  }

  COROUTINE_END();
}

void DisplayCoroutine::show(const char* line1,
                            const char* line2,
                            int price1,
                            int price2,
                            int duration) {
  if (duration > 0) {
    messageUntil = millis() + duration;
  }
  lcd.clear();
  asciinize(line1);
  if (line2) {
    asciinize(line2);
  }
  lcd.write(line1);

  if (!line2 && price2 == -1 && strlen(line1) > 10) {
    price2 = price1;
    price1 = -1;
  }

  if (price1 != -1) {
    char price[6];
    snprintf(price, 6, "%5.2f", ((double)price1) / 100);
    lcd.setCursor(11, 0);
    lcd.write(price);
  }

  if (line2) {
    lcd.setCursor(0, 1);
    lcd.write(line2);
  } else if (strlen(line1) > 16) {
    // break first line into second line
    lcd.setCursor(0, 1);
    size_t offset = line1[16] == ' ' ? 17 : 16;  // remove leading space
    lcd.write(&line1[offset],
              strlen(line1) > (offset + 10) ? 10 : strlen(line1) - offset);
  }

  if (price2 != -1) {
    char price[6];
    snprintf(price, 7, "%6.2f", ((double)price2) / 100);
    lcd.setCursor(10, 1);
    lcd.write(price);
  }
}

void DisplayCoroutine::asciinize(const char* str) {
  bool isUnicode = false;
  size_t j = 0;
  for (size_t i = 0; i < strlen(str); i++) {
    if (str[i] == 0xc3) {
      // unicode character first byte
      isUnicode = true;
      continue;
    } else if (!isUnicode) {
      // regular character
      ((char*)str)[j] = str[i];
      j++;
      continue;
    }

    switch (str[i]) {
      case 0x84:  // Ä
        ((char*)str)[j] = '\1';
        break;
      case 0x96:  // Ö
        ((char*)str)[j] = '\2';
        break;
      case 0x9c:  // Ü
        ((char*)str)[j] = '\3';
        break;
      case 0xa4:  // ä
        ((char*)str)[j] = '\4';
        break;
      case 0xb6:  // ö
        ((char*)str)[j] = '\5';
        break;
      case 0xbc:  // ü
        ((char*)str)[j] = '\6';
        break;
      case 0x9f:  // ß
        ((char*)str)[j] = '\7';
        break;
      case 0xa2:  // â
      case 0xa1:  // á
      case 0xa0:  // à
        ((char*)str)[j] = 'a';
        break;
      case 0xaa:  // ê
      case 0xa9:  // é
      case 0xa8:  // è
        ((char*)str)[j] = 'e';
        break;
      case 0xb3:  // ó
      case 0xb2:  // ò
      case 0xb4:  // ô
        ((char*)str)[j] = 'o';
        break;
      case 0xbb:  // û
      case 0xba:  // ú
      case 0xb9:  // ù
        ((char*)str)[j] = 'u';
        break;
    }
    j++;
    isUnicode = false;
  }
  ((char*)str)[j] = '\0';
}