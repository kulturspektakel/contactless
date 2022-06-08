#include <AceRoutine.h>
#include <Arduino.h>

using namespace ace_routine;

class ChargeWithoutCatdCoroutine : public Coroutine {
 private:
  unsigned long starPress = 0;

 public:
  int runCoroutine() override;
};