#include <AceRoutine.h>
#include <Arduino.h>

using namespace ace_routine;

enum BeepPattern {
  BEEP,
  ERROR,
};

class BuzzerCoroutine : public Coroutine {
 private:
  int beepCounter = 0;
  unsigned long lastTime = 0;

 public:
  int runCoroutine() override;
  void beep(BeepPattern pattern = BEEP);
};