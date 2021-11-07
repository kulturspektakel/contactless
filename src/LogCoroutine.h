#include <AceRoutine.h>
#include <Arduino.h>
#include <SD.h>
#include "proto/transaction.pb.h"

using namespace ace_routine;

class LogCoroutine : public Coroutine {
 private:
  uint8_t data[CardTransaction_size];
  char filename[13];
  File dir;
  File file;
  boolean isLogFile();

 public:
  int logsToUpload = 0;
  int runCoroutine() override;
  CardTransaction transaction = CardTransaction_init_zero;
  void addProduct(int i);
  void writeLog();
};