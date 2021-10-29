#include <AceRoutine.h>
#include <Arduino.h>

using namespace ace_routine;

class ModeChangerCoroutine : public Coroutine {
 private:
  unsigned long lastModeChange = 0;

 public:
  int runCoroutine() override;
  bool isModeChanger();
};