#include <AceRoutine.h>
#include <Arduino.h>
#include <MFRC522.h>
#include <MainCoroutine.h>

using namespace ace_routine;

class RFIDCoroutine : public Coroutine {
  void calculateHash(unsigned char* target, Balance balance);
  boolean authenticateAndWrite(int block,
                               unsigned char writeData[16],
                               MFRC522::MIFARE_Key* key);
  void calculatePassword(byte* password, byte* pack);
  void calculateSignatureUltralight(unsigned char* target,
                                    Balance balance,
                                    uint16_t counter);
  unsigned int constructPayload(unsigned char* target,
                                Balance balance,
                                uint16_t counter);
  boolean readBalance(bool skipSecurity = false);
  boolean writeBalance(Balance balance, bool needsAuthentication = true);
  bool hasToWriteLog = false;
  unsigned long messageStart = 0;

 public:
  boolean isModeChanger;
  Balance cardValueBefore;
  Balance cardValueAfter;
  int runCoroutine() override;
  void resetReader();
  char cardId[15] = "";  // 7*2+1
  uint16_t ultralightCounter = 0;
};