#include "DisplayCoroutine.h"
#include <ArduinoLog.h>
#include "KeypadCoroutine.h"
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
extern KeypadCoroutine keypadCoroutine;
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
  initialized = true;

  while (true) {
    COROUTINE_YIELD();
    if (messageUntil > 0) {
      // displaying message
      if (
          // message expires
          messageUntil < millis() ||
          // clearable message + key press
          (keypadCoroutine.currentKey && keyClearsMessage)) {
        // clear message
        requiresUpdate = true;
        messageUntil = 0;
      }
      continue;
    }

    if (!requiresUpdate) {
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
      case CHARGE_LIST:
        show(mainCoroutine.mode == CHARGE_MANUAL ? "Manuell" : "Preis",
             mainCoroutine.balance.deposit < 0 ? "R\6ckgabe" : "Pfand", 0,
             mainCoroutine.balance.total, mainCoroutine.balance.deposit);
        break;
      case TOP_UP:
        show("Aufladen",
             -mainCoroutine.balance.deposit > 0 ? "Pfand" : "R\6ckgabe", 0,
             mainCoroutine.balance.total, mainCoroutine.balance.deposit);
        break;
      case CASH_OUT:
        show("Karte auszahlen?");
        break;
      case INITIALIZE_CARD:
        show("Karte", "initialisieren");
        break;
      case CHARGE_WITHOUT_CARD:
        show("1) Crew  2) Bar", "3) Gutschein");
        break;
      default:
        show("Home");
        break;
    }
  }

  COROUTINE_END();
}

void DisplayCoroutine::clearMessageIn(unsigned long ms) {
  messageUntil = millis() + ms;
}

void DisplayCoroutine::show(
    const char* _line1,
    const char* _line2,
    int duration,  // negative duration means clearable but keypress
    int total,
    int deposit) {
  keyClearsMessage = duration < 0;
  if (duration != 0) {
    messageUntil = millis() + (duration > 0 ? duration : duration * -1);
  }
  lcd.clear();
  char line1[strlen(_line1) + 1];
  asciinize(line1, _line1);
  lcd.write(line1);

  char line2[17] = "";
  bool totalSecondLine = false;

  if (total != NOT_SET) {
    /*
    ┌────────────────┐
    │Helles      3.50│
    │                │
    └────────────────┘
    ┌────────────────┐
    │Zitronenlimo    │
    │            3.00│
    └────────────────┘
    */
    char price[6];
    snprintf(price, 6, "%5.2f", ((double)total) / 100);
    totalSecondLine = !_line2 && deposit == NOT_SET && strlen(line1) > 10;
    lcd.setCursor(11, totalSecondLine ? 1 : 0);
    lcd.write(price);
  }

  if (!_line2 && deposit == NOT_SET && strlen(_line1) > 16) {
    /*
    ┌────────────────┐
    │Süßkartoffel mit│
    │Avocado     3.50│
    └────────────────┘
    */
    size_t offset = line1[16] == ' ' ? 17 : 16;  // remove leading space
    strncpy(line2, &_line1[offset], 16);
  } else if (_line2 && deposit == NOT_SET) {
    /*
    ┌────────────────┐
    │Software-Update │
    │erfolgreich     │
    └────────────────┘
    */
    asciinize(line2, _line2);
  } else if (deposit != NOT_SET) {
    /*
    ┌────────────────┐
    │Guthaben   10.00│
    │1 Rückgabe -2.00│
    └────────────────┘
    */

    char price[7];
    snprintf(price, 7, "%.2f", ((double)deposit * TOKEN_VALUE) / 100);
    size_t remaining = 16 - 2 - strlen(price);
    size_t fill = strlen(_line2) > remaining ? 0 : remaining - strlen(_line2);
    snprintf(line2, 17, "%.1d %.*s%*s%s", deposit < 0 ? deposit * -1 : deposit,
             remaining, _line2, fill, "", price);
  }

  for (int i = strlen(line2); i < 16; i++) {
    line2[i] = ' ';  // fill up with spaces
  }

  lcd.setCursor(0, 1);
  lcd.write(line2, totalSecondLine ? 10 : 16);
}

void DisplayCoroutine::asciinize(char* target, const char* str) {
  bool isUnicode = false;
  size_t j = 0;
  for (size_t i = 0; i < strlen(str) && i < 16; i++) {
    if (str[i] == 0xc3) {
      // unicode character first byte
      isUnicode = true;
      continue;
    } else if (!isUnicode) {
      // regular character
      ((char*)target)[j] = str[i];
      j++;
      continue;
    }

    switch (str[i]) {
      case 0x84:  // Ä
        ((char*)target)[j] = '\1';
        break;
      case 0x96:  // Ö
        ((char*)target)[j] = '\2';
        break;
      case 0x9c:  // Ü
        ((char*)target)[j] = '\3';
        break;
      case 0xa4:  // ä
        ((char*)target)[j] = '\4';
        break;
      case 0xb6:  // ö
        ((char*)target)[j] = '\5';
        break;
      case 0xbc:  // ü
        ((char*)target)[j] = '\6';
        break;
      case 0x9f:  // ß
        ((char*)target)[j] = '\7';
        break;
      case 0xa2:  // â
      case 0xa1:  // á
      case 0xa0:  // à
        ((char*)target)[j] = 'a';
        break;
      case 0xaa:  // ê
      case 0xa9:  // é
      case 0xa8:  // è
        ((char*)target)[j] = 'e';
        break;
      case 0xb3:  // ó
      case 0xb2:  // ò
      case 0xb4:  // ô
        ((char*)target)[j] = 'o';
        break;
      case 0xbb:  // û
      case 0xba:  // ú
      case 0xb9:  // ù
        ((char*)target)[j] = 'u';
        break;
    }
    j++;
    isUnicode = false;
  }
  ((char*)target)[j] = '\0';
}