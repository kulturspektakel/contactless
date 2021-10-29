#include <AceRoutine.h>
#include <Arduino.h>
#include "SdFat.h"
#include "proto/transaction.pb.h"

using namespace ace_routine;

class LogCoroutine : public Coroutine {
 private:
  uint8_t data[TransactionMessage_size];
  char filename[13];
  sdfat::File file;
  boolean isLogFile();

 public:
  int logsToUpload = 0;
  int runCoroutine() override;
  TransactionMessage transaction = TransactionMessage_init_zero;
  void addProduct(int i);
  void writeLog();
};