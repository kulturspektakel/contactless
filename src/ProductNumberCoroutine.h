#include <AceRoutine.h>
#include <Arduino.h>

using namespace ace_routine;

class ProductNumberCoroutine : public Coroutine {
 public:
  int runCoroutine() override;
  char entry[3];
};