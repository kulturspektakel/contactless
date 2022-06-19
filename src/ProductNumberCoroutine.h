#include <AceRoutine.h>
#include <Arduino.h>

using namespace ace_routine;

const uint8_t PRODUCT_NUMBER_LENGTH = 2;

class ProductNumberCoroutine : public Coroutine {
  uint8_t pointer = 1;

 public:
  int runCoroutine() override;
  char entry[PRODUCT_NUMBER_LENGTH + 1];
};