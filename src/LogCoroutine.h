#include <AceRoutine.h>
#include <Arduino.h>
#include <SD.h>
#include "proto/logmessage.pb.h"

using namespace ace_routine;

class LogCoroutine : public Coroutine {
 private:
  uint8_t data[LogMessage_size];
  char filename[13];
  File dir;
  File file;
  boolean isLogFile();

 public:
  bool hasFilesToUpload = false;
  int runCoroutine() override;
  LogMessage logMessage = LogMessage_init_zero;
  int addProduct(int i);
  void writeLog(LogMessage_Order_PaymentMethod paymentMethod);
};