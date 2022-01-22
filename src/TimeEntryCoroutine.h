#include <AceRoutine.h>
#include <Arduino.h>

using namespace ace_routine;

class TimeEntryCoroutine : public Coroutine {
 public:
  bool deviceTimeIsUtc = false;

  char value[11];
  void dateFromHTTP(char* date);
  int runCoroutine() override;
};