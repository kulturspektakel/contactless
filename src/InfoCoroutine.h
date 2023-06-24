#include <AceRoutine.h>
#include <Arduino.h>

using namespace ace_routine;

class InfoCoroutine : public Coroutine {
 private:
  unsigned long time;

 public:
  int runCoroutine() override;
  char line1[17];
  char line2[17];
};