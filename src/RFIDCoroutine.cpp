#include "RFIDCoroutine.h"
#include <ArduinoLog.h>
#include <Hash.h>
#include "ConfigCoroutine.h"
#include "DisplayCoroutine.h"
#include "LogCoroutine.h"
#include "ModeChangerCoroutine.h"

extern MainCoroutine mainCoroutine;
extern DisplayCoroutine displayCoroutine;
extern ConfigCoroutine configCoroutine;
extern LogCoroutine logCoroutine;
extern ModeChangerCoroutine modeChangerCoroutine;
extern const int TOKEN_VALUE;
extern const char* SALT;
extern MFRC522::MIFARE_Key KEY_B;

static MFRC522 mfrc522 = MFRC522(15, 2);
static MFRC522::MIFARE_Key KEY_A1 = {{0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5}};
static MFRC522::MIFARE_Key KEY_A2 = {{0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7}};
static MFRC522::MIFARE_Key KEY_INIT = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
static byte ACCESS_BITS[4] = {0x78, 0x77, 0x88, 0x43};
static const int BLOCK_ADDRESS = 6;

int RFIDCoroutine::runCoroutine() {
  COROUTINE_BEGIN();
  mfrc522.PCD_Init();

  while (true) {
    COROUTINE_YIELD();

    // Reset reader, so a new card can be read
    resetReader();

    COROUTINE_AWAIT(mfrc522.PICC_IsNewCardPresent() &&
                    mfrc522.PICC_ReadCardSerial());

    for (int i = 0; i < 4; i++) {
      byte nib1 = (mfrc522.uid.uidByte[i] >> 4) & 0x0F;
      byte nib2 = (mfrc522.uid.uidByte[i] >> 0) & 0x0F;
      cardId[i * 2 + 0] = nib1 < 0xA ? '0' + nib1 : 'A' + nib1 - 0xA;
      cardId[i * 2 + 1] = nib2 < 0xA ? '0' + nib2 : 'A' + nib2 - 0xA;
    }
    cardId[8] = '\0';
    Log.infoln("[RFID] Detected card ID: %s", cardId);
    if (modeChangerCoroutine.isModeChanger()) {
      continue;
    }

    switch (mainCoroutine.mode) {
      case CHARGE_MANUAL:
      case CHARGE_LIST:
      case TOP_UP: {
        if (!readBalance()) {
          break;
        }
        if (mainCoroutine.balance.total != 0 ||
            mainCoroutine.balance.deposit != 0) {
          int i = mainCoroutine.mode == TOP_UP ? -1 : 1;
          // TODO: here might be bugs
          cardValueAfter.deposit += mainCoroutine.balance.deposit * i;
          cardValueAfter.total -=
              mainCoroutine.balance.total * i +
              mainCoroutine.balance.deposit * TOKEN_VALUE * i;

          if (cardValueAfter.deposit < 0) {
            displayCoroutine.show("Nicht genug", "Pfandmarken", 2000);
            break;
          } else if (cardValueAfter.total < 0) {
            displayCoroutine.show("Nicht genug", "Guthaben", 2000);
            break;
          } else if (cardValueAfter.total > 9999 ||
                     cardValueAfter.deposit > 9) {
            displayCoroutine.show("Kartenlimit", "überschritten", 2000);
            break;
          } else if (!writeBalance(cardValueAfter)) {
            break;
          }
          logCoroutine.writeLog();
        }
        // show balance
        char deposit[16];
        sprintf(deposit, "%d Pfandmarke%c", cardValueAfter.deposit,
                cardValueAfter.deposit == 1 ? ' ' : 'n');
        displayCoroutine.show("Guthaben", deposit, 2000, cardValueAfter.total);
        mainCoroutine.resetBalance();
        break;
      }
      case INITIALIZE_CARD: {
        unsigned char writeData[6][16] = {
            // clang-format off
            {0x14, 0x01, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1},  // 1: NDEF message
            {0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1},  // 2: NDEF message
            {},                                                                                                // 3: access
            {0x00, 0x00, 0x03, 0x2B, 0xD1, 0x01, 0x27, 0x55, 0x04, 0x6B, 0x75, 0x6C, 0x74, 0x2E, 0x63, 0x61},  // 4: kult.ca
            {0x73, 0x68, 0x2F, 0x24, 0x24, 0x24, 0x2F, cardId[0], cardId[1], cardId[2], cardId[3], cardId[4], cardId[5], cardId[6], cardId[7], 0x2F},  // 5: sh/$$$/________/
            {0x30, 0x30, 0x30, 0x30, 0x30, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0xFE},  // 6: 00000__________
            // clang-format on
        };

        memcpy(&writeData[2][0], KEY_A1.keyByte, sizeof(KEY_A1.keyByte));
        memcpy(&writeData[2][6], ACCESS_BITS, sizeof(ACCESS_BITS));
        memcpy(&writeData[2][10], KEY_B.keyByte, sizeof(KEY_B.keyByte));
        calculateHash(writeData[5] + 5, {.deposit = 0, .total = 0});

        // initialize card
        for (int i = 0; i < 6; i++) {
          if (!authenticateAndWrite(i + 1, writeData[i], &KEY_INIT)) {
            break;
          }
        }

        // lock all blocks
        unsigned char trailer[16];
        memcpy(&trailer[0], KEY_A2.keyByte, sizeof(KEY_A2.keyByte));
        memcpy(&trailer[6], ACCESS_BITS, sizeof(ACCESS_BITS));
        memcpy(&trailer[10], KEY_B.keyByte, sizeof(KEY_B.keyByte));
        for (int i = 7; i < 64; i += 4) {
          if (!authenticateAndWrite(i, trailer, &KEY_INIT)) {
            break;
          }
        }
        cardValueAfter.deposit = 0;
        cardValueAfter.total = 0;
        if (!writeBalance(cardValueAfter)) {
          break;
        }
        displayCoroutine.show("Initialisierung", "OK", 2000);
        mainCoroutine.resetBalance();
        break;
      }
      case CASH_OUT: {
        if (!readBalance()) {
          break;
        }
        cardValueAfter.deposit = 0;
        cardValueAfter.total = 0;
        if (!writeBalance(cardValueAfter)) {
          break;
        }
        displayCoroutine.show("Auszahlen", nullptr, 3000,
                              cardValueBefore.total + cardValueBefore.deposit);
        logCoroutine.writeLog();
        mainCoroutine.resetBalance();
        break;
      }
    }
  }

  COROUTINE_END();
}

boolean RFIDCoroutine::authenticateAndWrite(int block,
                                            unsigned char writeData[16],
                                            MFRC522::MIFARE_Key* key) {
  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_B, block, key, &(mfrc522.uid));
  if (status == MFRC522::STATUS_OK) {
    return mfrc522.MIFARE_Write(block, writeData, 16) == MFRC522::STATUS_OK;
  }
  Log.errorln("[RFID] Card not writable");
  displayCoroutine.show("Karte nicht", "schreibbar", 2000);
  return false;
}

boolean RFIDCoroutine::authenticateAndRead(int block,
                                           unsigned char target[18],
                                           MFRC522::MIFARE_Key* key) {
  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_B, block, key, &(mfrc522.uid));
  if (status == MFRC522::STATUS_OK) {
    byte size = 18;
    return mfrc522.MIFARE_Read(block, target, &size) == MFRC522::STATUS_OK;
  }
  Log.errorln("[RFID] Card not readable");
  displayCoroutine.show("Karte nicht", "lesbar", 2000);
  return false;
}

boolean RFIDCoroutine::readBalance() {
  unsigned char buffer[18];
  if (authenticateAndRead(6, buffer, &KEY_B)) {
    Balance balance = {
        .deposit = (buffer[4] - '0'),
        .total = (buffer[0] - '0') * 1000 + (buffer[1] - '0') * 100 +
                 (buffer[2] - '0') * 10 + (buffer[3] - '0'),
    };
    unsigned char hash[11];
    calculateHash(hash, balance);
    for (int i = 0; i < 10; i++) {
      if (hash[i] != buffer[i + 5]) {
        return false;
      }
    }
    cardValueBefore = balance;
    cardValueAfter = balance;
    return true;
  }
  return false;
}

void RFIDCoroutine::calculateHash(unsigned char* target, Balance balance) {
  int len = strlen(cardId) + strlen(SALT) + 6;
  char buffer[len];
  snprintf(buffer, len, "%d%d%s%s", balance.total, balance.deposit, cardId,
           SALT);
  String hash = sha1(buffer, strlen(buffer));
  hash.toCharArray((char*)target, 11);
  target[10] = '\0';
}

boolean RFIDCoroutine::writeBalance(Balance balance) {
  unsigned char writeData[16];
  writeData[0] = ((balance.total / 1000) % 10) + '0';
  writeData[1] = ((balance.total / 100) % 10) + '0';
  writeData[2] = ((balance.total / 10) % 10) + '0';
  writeData[3] = (balance.total % 10) + '0';
  writeData[4] = (balance.deposit % 10) + '0';
  writeData[15] = 0xfe;

  calculateHash(writeData + 5, balance);
  return authenticateAndWrite(BLOCK_ADDRESS, writeData, &KEY_B);
}

void RFIDCoroutine::resetReader() {
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  cardId[0] = '\0';
}