#include <AceRoutine.h>
#include <Arduino.h>
#include <ArduinoLog.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

using namespace ace_routine;

#define NOT_SET -99999999

class DisplayCoroutine : public Coroutine {
 private:
  unsigned long messageUntil;
  bool keyClearsMessage = false;
  void asciinize(char* traget, const char* str);

 public:
  bool requiresUpdate = false;
  bool initialized = false;
  int runCoroutine() override;
  void show(const char* line1,
            const char* line2 = nullptr,
            int duration = 0,
            int total = NOT_SET,
            int deposit = NOT_SET);
};