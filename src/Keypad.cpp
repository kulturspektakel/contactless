#include "Keypad.h"
#include "StateManager.h"

#define COL0 4
#define COL1 5
#define COL2 6
#define COL3 7
#define ROW0 0
#define ROW1 1
#define ROW2 2
#define ROW3 3

const char keymap[4][5] = {"123A", "456B", "789C", "*0#D"};

// Hex byte statement for each port of PCF8574
const int hexData[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

// Hex data for each row of keypad in PCF8574
const int pcf8574RowData[4] = {
    hexData[ROW1] | hexData[ROW2] | hexData[ROW3] | hexData[COL0] |
        hexData[COL1] | hexData[COL2] | hexData[COL3],
    hexData[ROW0] | hexData[ROW2] | hexData[ROW3] | hexData[COL0] |
        hexData[COL1] | hexData[COL2] | hexData[COL3],
    hexData[ROW0] | hexData[ROW1] | hexData[ROW3] | hexData[COL0] |
        hexData[COL1] | hexData[COL2] | hexData[COL3],
    hexData[ROW0] | hexData[ROW1] | hexData[ROW2] | hexData[COL0] |
        hexData[COL1] | hexData[COL2] | hexData[COL3],
};

// Hex data for each col of keypad in PCF8574
const int col[4] = {hexData[COL0], hexData[COL1], hexData[COL2], hexData[COL3]};

Keypad::Keypad(StateManager& sm, int a) : stateManager(sm) {
  address = a;
}

void Keypad::setup() {
  pcf8574Write(address, 0xff);
  currentRow = 0;
}

void Keypad::loop() {
  char key = get_key();
  if (key != '\0') {
    handleKey(key);
  }

  if ((stateManager.state.transaction.mode == TransactionMessage_Mode_CHARGE ||
       stateManager.state.transaction.mode == TransactionMessage_Mode_TOP_UP) &&
      millis() - lastKeyTime > 25 * 1000) {
    // reset if no key pressed for some time
    stateManager.resetState();
  }
}

void Keypad::pcf8574Write(int addr, int data) {
  currentData = data;
  Wire.beginTransmission(addr);
  Wire.write(data);
  Wire.endTransmission();
}

int Keypad::pcf8574Read(int addr) {
  Wire.requestFrom(addr, 1);
  return Wire.read();
}

char Keypad::get_key() {
  static int lastKey;
  static bool keydown;
  int readData;
  int key = '\0';

  // Search row low
  pcf8574Write(address, pcf8574RowData[currentRow]);

  for (int r = 0; r < 4; r++) {
    // Read pcf8574 port data
    readData = pcf8574Read(address);
    // XOR to compare obtained data and current data and know if some column are
    // low
    readData ^= currentData;
    // Key pressed!
    if (col[r] == readData) {
      lastKey = keymap[currentRow][r];
      if (!keydown) {
        keydown = true;
        return lastKey;
      }
      return '\0';
    }
  }

  // Key was pressed and then released
  if (key == '\0' && lastKey != '\0') {
    lastKey = '\0';
    keydown = false;
    return lastKey;
  }

  // All PCF8574 ports high again
  pcf8574Write(address, 0xff);

  // Next row
  currentRow = (currentRow + 1) % 4;

  return key;
}

void Keypad::poundKey() {
  trippleHash[2] = trippleHash[1];
  trippleHash[1] = trippleHash[0];
  trippleHash[0] = millis();

  if (stateManager.state.transaction.mode != TransactionMessage_Mode_CHARGE ||
      stateManager.state.config.price1 == 0) {
    // don't allow mode switching, when we don't have products
    return;
  }

  if (trippleHash[0] - trippleHash[2] > 1000) {
    // tripple press not fast enough
    return;
  }

  trippleHash[0] = 0;
  trippleHash[1] = 0;
  trippleHash[2] = 0;
  bool newValue = !stateManager.state.manualEntry;
  stateManager.resetState();
  stateManager.state.manualEntry = newValue;
  stateManager.stateUpdated();
}

void Keypad::handleKey(char key) {
  lastKeyTime = millis();

  bool CHARGE =
      stateManager.state.transaction.mode == TransactionMessage_Mode_CHARGE;
  bool TOP_UP =
      stateManager.state.transaction.mode == TransactionMessage_Mode_TOP_UP;
  bool CASHOUT =
      stateManager.state.transaction.mode == TransactionMessage_Mode_CASHOUT;
  bool TIME_ENTRY =
      stateManager.state.transaction.mode == TransactionMessage_Mode_TIME_ENTRY;

  if (TIME_ENTRY) {
    size_t cur_len = strlen(stateManager.state.entry);
    if (isdigit(key)) {
      stateManager.state.entry[cur_len] = key;
      stateManager.state.entry[cur_len + 1] = '\0';
      if (cur_len == 7) {
        stateManager.receivedTime(stateManager.state.entry, false);
      }
    } else {
      stateManager.state.entry[cur_len - 1] = '\0';
    }
  } else if (CHARGE || TOP_UP) {
    if (isdigit(key)) {
      if (CHARGE && stateManager.state.config.price1 > 0 &&
          !stateManager.state.manualEntry) {
        stateManager.selectProduct(key - 49);
        return;
      } else if (stateManager.state.value < 1000) {
        stateManager.state.value =
            stateManager.state.value * 10 + (int)key - 48;
      }
    } else if (CHARGE && key == 'A' && stateManager.state.tokens < 9) {
      stateManager.state.tokens++;
    } else if (CHARGE && key == 'B' && stateManager.state.tokens > -9) {
      stateManager.state.tokens--;
    } else if (TOP_UP && key == 'A' && stateManager.state.tokens > -9) {
      stateManager.state.tokens--;
    } else if (TOP_UP && key == 'B' && stateManager.state.tokens < 0) {
      stateManager.state.tokens++;
    } else if (key == 'C') {
      if (CHARGE && stateManager.state.config.price1 > 0 &&
          !stateManager.state.manualEntry) {
        // in product mode, set to 0 immediatelly
        stateManager.state.value = 0;
      } else {
        stateManager.state.value /= 10;
      }
      stateManager.clearCart();
    } else if (TOP_UP && key == 'D' && stateManager.state.value == 0 &&
               stateManager.state.value == 0) {
      stateManager.state.transaction.mode = TransactionMessage_Mode_CASHOUT;
    } else if (key == 'D') {
      stateManager.state.entry[0] = '\0';
      stateManager.state.value = 0;
      stateManager.state.tokens = 0;
      if (stateManager.state.config.price1 > 0) {
        stateManager.state.manualEntry = false;
      }
      stateManager.clearCart();
    } else if (key == '#') {
      poundKey();
      return;
    } else if (key == '*') {
      // # * pressed in short time shows debug message
      bool startDebugMode = millis() - trippleHash[0] < 500;
      stateManager.showDebug(startDebugMode);
      return;
    } else {
      return;
    }
  } else if (CASHOUT) {
    stateManager.state.transaction.mode = TransactionMessage_Mode_TOP_UP;
  } else {
    return;
  }
  stateManager.stateUpdated();
}
