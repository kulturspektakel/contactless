#include "KeypadCoroutine.h"
#include <ArduinoLog.h>

#define COL0 4
#define COL1 5
#define COL2 6
#define COL3 7
#define ROW0 0
#define ROW1 1
#define ROW2 2
#define ROW3 3

static const char keymap[4][5] = {"123A", "456B", "789C", "*0#D"};

// Hex byte statement for each port of PCF8574
static const int hexData[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

// Hex data for each row of keypad in PCF8574
static const int pcf8574RowData[4] = {
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
static const int col[4] = {hexData[COL0], hexData[COL1], hexData[COL2],
                           hexData[COL3]};

int KeypadCoroutine::runCoroutine() {
  COROUTINE_BEGIN();
  Wire.begin();
  while (1) {
    COROUTINE_AWAIT(getKey() != '\0');
    currentKey = lastKey;
    COROUTINE_YIELD();
    currentKey = '\0';
    COROUTINE_AWAIT(getKey() == '\0');
    // key released
  }
  COROUTINE_END();
}

char KeypadCoroutine::getKey() {
  static int address = 0x20;

  for (int r = 0; r < 4; r++) {
    Wire.beginTransmission(address);
    Wire.write(pcf8574RowData[r]);
    int status = Wire.endTransmission();
    if (status != 0) {
      Log.errorln("[Keypad] transmission failed");
      reset();
    }
    for (int c = 0; c < 4; c++) {
      // Read pcf8574 port data
      Wire.requestFrom(address, 1);
      int readData = Wire.read();

      // XOR to compare obtained data and current data and know if some columns
      // are low
      readData ^= pcf8574RowData[r];

      if (col[c] == readData) {
        lastKey = keymap[r][c];
        return lastKey;
      }
    }
  }

  return '\0';
}
