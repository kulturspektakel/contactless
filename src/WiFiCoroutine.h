#include <AceRoutine.h>
#include <Arduino.h>

using namespace ace_routine;

class WiFiCoroutine : public Coroutine {
 public:
  int runCoroutine() override;
};