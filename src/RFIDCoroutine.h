#include <AceRoutine.h>
#include <Arduino.h>
#include <MFRC522.h>
#include <MainCoroutine.h>

using namespace ace_routine;

class RFIDCoroutine : public Coroutine {
  void calculateHash(unsigned char* target, Balance balance);
  boolean authenticateAndRead(int block,
                              unsigned char target[18],
                              MFRC522::MIFARE_Key* key);
  boolean authenticateAndWrite(int block,
                               unsigned char writeData[16],
                               MFRC522::MIFARE_Key* key);
  boolean readBalance();
  boolean writeBalance(Balance balance);
  bool hasToWriteLog = false;
  unsigned long messageStart = 0;

 public:
  boolean isModeChanger;
  Balance cardValueBefore;
  Balance cardValueAfter;
  int runCoroutine() override;
  void resetReader();
  char cardId[15] = "";  // 7*2+1
};