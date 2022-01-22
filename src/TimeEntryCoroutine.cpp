#include "TimeEntryCoroutine.h"
#include <ArduinoLog.h>
#include <TimeLib.h>
#include "DisplayCoroutine.h"
#include "KeypadCoroutine.h"
#include "MainCoroutine.h"
#include "proto/config.pb.h"

extern MainCoroutine mainCoroutine;
extern KeypadCoroutine keypadCoroutine;
extern DisplayCoroutine displayCoroutine;

int TimeEntryCoroutine::runCoroutine() {
  COROUTINE_BEGIN();

  COROUTINE_AWAIT(mainCoroutine.mode == TIME_ENTRY);

  while (!value[9]) {
    COROUTINE_AWAIT(keypadCoroutine.currentKey != '\0' || value[9]);
    if (value[9]) {
      break;
    } else if (keypadCoroutine.currentKey == 'C' && strlen(value) > 0) {
      // clear last key
      value[strlen(value) - 1] = '\0';
    } else if (keypadCoroutine.currentKey == 'D') {
      // clear all
      memset(&value[0], 0, sizeof(value));
    } else if (keypadCoroutine.currentKey >= '0' &&
               keypadCoroutine.currentKey <= '9') {
      value[strlen(value)] = keypadCoroutine.currentKey;
    } else {
      // ignore other keys
      continue;
    }
    displayCoroutine.requiresUpdate = true;
  }
  Log.infoln("[TimeEntry] %s", value);
  displayCoroutine.requiresUpdate = true;

  COROUTINE_DELAY_SECONDS(1);

  int day = (value[0] - '0') * 10 + (value[1] - '0');
  int month = (value[2] - '0') * 10 + (value[3] - '0');
  int year = (value[4] - '0') * 10 + (value[5] - '0') + 2000;
  int hours = (value[6] - '0') * 10 + (value[7] - '0');
  int minutes = (value[8] - '0') * 10 + (value[9] - '0');

  setTime(hours, minutes,
          0,  // seconds
          day, month, year);

  Log.infoln("[TimeEntry] set time to  %d.%d.%d %d:%d", day, month, year, hours,
             minutes);

  COROUTINE_END();
}

void TimeEntryCoroutine::dateFromHTTP(char* date) {
  // Sun, 17 Nov 2019 18:00:48 GMT
  TimeElements tm = {
      0,                                              // Second
      (date[20] - '0') * 10 + (date[21] - '0'),       // Minute
      (date[17] - '0') * 10 + (date[18] - '0'),       // Hour
      0,                                              // Wday
      (date[5] - '0') * 10 + (date[6] - '0'),         // Day
      0,                                              // Month
      (date[14] - '0') * 10 + (date[15] - '0') + 30,  // Year
  };

  if (date[8] == 'J' && date[9] == 'a') {  // January
    tm.Month = 1;
  } else if (date[8] == 'F') {  // February
    tm.Month = 2;
  } else if (date[8] == 'M' && date[10] == 'r') {  // March
    tm.Month = 3;
  } else if (date[8] == 'A' && date[9] == 'p') {  // April
    tm.Month = 4;
  } else if (date[8] == 'M' && date[10] == 'y') {  // May
    tm.Month = 5;
  } else if (date[8] == 'J' && date[10] == 'n') {  // June
    tm.Month = 6;
  } else if (date[8] == 'J' && date[10] == 'l') {  // July
    tm.Month = 7;
  } else if (date[8] == 'A' && date[9] == 'u') {  // August
    tm.Month = 8;
  } else if (date[8] == 'S') {  // September
    tm.Month = 9;
  } else if (date[8] == 'O') {  // October
    tm.Month = 10;
  } else if (date[8] == 'N') {  // November
    tm.Month = 11;
  } else if (date[8] == 'D') {  // December
    tm.Month = 12;
  }

  time_t unixtime = makeTime(tm);
  deviceTimeIsUtc = true;
  breakTime(unixtime, tm);
  sprintf(value, "%02d%02d%02d%02d%02d", tm.Day, tm.Month, tm.Year - 30,
          tm.Hour, tm.Minute);
}
