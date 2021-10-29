#include <AceRoutine.h>
#include <Arduino.h>

using namespace ace_routine;

class InfoCoroutine : public Coroutine {
 private:
  unsigned long lastKey = 0;

 public:
  int runCoroutine() override;
};