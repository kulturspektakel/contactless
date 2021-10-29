#include <AceRoutine.h>
#include <Arduino.h>
#include <Wire.h>

using namespace ace_routine;

class KeypadCoroutine : public Coroutine {
 public:
  int runCoroutine() override;
  char currentKey = '\0';

 private:
  char getKey();
  char lastKey = '\0';
};
