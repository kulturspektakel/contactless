#include <AceRoutine.h>
#include <Arduino.h>

using namespace ace_routine;
extern const int TIMEZONE_OFFSET_MINUTES;

class TimeEntryCoroutine : public Coroutine {
 private:
  int utcOffsetMinutes = TIMEZONE_OFFSET_MINUTES;

 public:
  char value[11];
  void dateFromHTTP(char* date);
  int runCoroutine() override;
};