#include <AceRoutine.h>
#include <Arduino.h>

using namespace ace_routine;

class ChargeManualCoroutine : public Coroutine {
 private:
  unsigned long lastKey = 0;
  int count = 0;

 public:
  int runCoroutine() override;
};