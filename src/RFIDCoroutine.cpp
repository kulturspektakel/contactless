#include "RFIDCoroutine.h"
#include <ArduinoLog.h>
#include <Hash.h>
#include "ConfigCoroutine.h"
#include "DisplayCoroutine.h"
#include "LogCoroutine.h"
#include "ModeChangerCoroutine.h"
#include "base64.hpp"

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
static unsigned char ACCESS_BITS[4] = {0x78, 0x77, 0x88, 0x43};
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

    for (int i = 0; i < mfrc522.uid.size; i++) {
      unsigned char nib1 = (mfrc522.uid.uidByte[i] >> 4) & 0x0F;
      unsigned char nib2 = (mfrc522.uid.uidByte[i] >> 0) & 0x0F;
      cardId[i * 2 + 0] = nib1 < 0xA ? '0' + nib1 : 'A' + nib1 - 0xA;
      cardId[i * 2 + 1] = nib2 < 0xA ? '0' + nib2 : 'A' + nib2 - 0xA;
    }
    cardId[mfrc522.uid.size * 2] = '\0';
    Log.infoln("[RFID] Detected card ID: %s", cardId);
    if (modeChangerCoroutine.isModeChanger()) {
      continue;
    }

    hasToWriteLog = false;
    messageStart = millis();

    switch (mainCoroutine.mode) {
      case CHARGE_MANUAL:
      case CHARGE_LIST:
      case TOP_UP: {
        if (!readBalance()) {
          continue;
        }

        if (mainCoroutine.balance) {
          int i = mainCoroutine.mode == TOP_UP ? -1 : 1;
          cardValueAfter.deposit += mainCoroutine.balance.deposit * i;
          cardValueAfter.total -=
              mainCoroutine.balance.total * i +
              mainCoroutine.balance.deposit * i * TOKEN_VALUE;

          if (cardValueAfter.deposit < 0) {
            displayCoroutine.show("Nicht genug", "Pfandmarken");
            break;
          } else if (cardValueAfter.total < 0) {
            displayCoroutine.show("Nicht genug", "Guthaben");
            break;
          } else if (cardValueAfter.total > 9999 ||
                     cardValueAfter.deposit > 9) {
            displayCoroutine.show("Kartenlimit", "überschritten");
            break;
          } else if (!writeBalance(cardValueAfter)) {
            continue;
          }
          hasToWriteLog = true;
        }
        // show balance
        char deposit[16];
        sprintf(deposit, "%d Pfandmarke%c", cardValueAfter.deposit,
                cardValueAfter.deposit == 1 ? ' ' : 'n');
        displayCoroutine.show("Guthaben", deposit, 0, cardValueAfter.total);
        break;
      }
      case INITIALIZE_CARD: {
        if (mfrc522.PICC_GetType(mfrc522.uid.sak) ==
            mfrc522.PICC_TYPE_MIFARE_UL) {
          // write NDEF
          unsigned char writeData[6][4] = {
              // clang-format off
              {0xE1, 0x10, 0x06, 0x00}, // 03: OTP NDEF
              {0x03, 0x29, 0xD1, 0x01}, // 04:
              {0x25, 0x55, 0x04, 0x6B}, // 05:    k
              {0x75, 0x6C, 0x74, 0x2E}, // 06: ult.
              {0x63, 0x61, 0x73, 0x68}, // 07: cash
              {0x2F, 0x24, 0x24, 0x2F}, // 08: /$$/
              // clang-format on
          };

          for (int i = 0; i < 6; i++) {
            mfrc522.MIFARE_Ultralight_Write(i + 3, writeData[i], 4);
          }

          ultralightCounter = 0;
          writeBalance(Balance_default);

          // write password and PACK
          unsigned char password[4];
          unsigned char pack[2];
          calculatePassword(password, pack);
          Log.error("%x %x %x %x", password[0], password[1], password[2],
                    password[3]);
          size_t lastPage = 0x13;
          mfrc522.MIFARE_Ultralight_Write(lastPage - 1, password, 4);
          mfrc522.MIFARE_Ultralight_Write(lastPage, pack, 4);
          unsigned char buffer[] = {0x04 /* strong modulation */, 0x00, 0x00,
                                    0x04 /* lock from page 0x04 */};
          mfrc522.MIFARE_Ultralight_Write(lastPage - 3, buffer, 4);

          // if (!readBalance() || cardValueBefore != Balance_default) {
          //   // Write was not successful
          //   continue;
          // }

        } else if (mfrc522.PICC_GetType(mfrc522.uid.sak) ==
                   mfrc522.PICC_TYPE_MIFARE_1K) {
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
          calculateHash(writeData[5] + 5, Balance_default);

          // initialize card
          for (int i = 0; i < 6; i++) {
            if (!authenticateAndWrite(i + 1, writeData[i], &KEY_INIT)) {
              continue;
            }
          }

          // lock all blocks
          unsigned char trailer[16];
          memcpy(&trailer[0], KEY_A2.keyByte, sizeof(KEY_A2.keyByte));
          memcpy(&trailer[6], ACCESS_BITS, sizeof(ACCESS_BITS));
          memcpy(&trailer[10], KEY_B.keyByte, sizeof(KEY_B.keyByte));
          for (int i = 7; i < 64; i += 4) {
            if (!authenticateAndWrite(i, trailer, &KEY_INIT)) {
              continue;
            }
          }
          cardValueAfter.deposit = 0;
          cardValueAfter.total = 0;
          if (!writeBalance(cardValueAfter)) {
            continue;
          }
        }

        displayCoroutine.show("Initialisierung", "OK");
        break;
      }
      case CASH_OUT: {
        if (!readBalance()) {
          continue;
        }
        cardValueAfter.deposit = 0;
        cardValueAfter.total = 0;
        if (!writeBalance(cardValueAfter)) {
          continue;
        }
        hasToWriteLog = true;
        mainCoroutine.mode = TOP_UP;
        displayCoroutine.show("Auszahlen", "R\6ckgabe +", 0,
                              cardValueBefore.total, cardValueBefore.deposit);
        break;
      }
    }

    // wait until card is gone
    unsigned char size = 18;
    unsigned char target[16];
    while (mfrc522.MIFARE_Read(6, target, &size) == MFRC522::STATUS_OK) {
      COROUTINE_DELAY(333);
    }

    if (hasToWriteLog) {
      // delaying writing to log until card is gone
      logCoroutine.writeLog();
    }
    unsigned long messageShownFor = millis() - messageStart;
    // always display message for at least 2 seconds
    displayCoroutine.clearMessageIn(
        messageShownFor < 2000 ? 2000 - messageShownFor : 0);
    mainCoroutine.resetBalance();
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

void RFIDCoroutine::calculatePassword(byte* password, byte* pack) {
  char data[mfrc522.uid.size + strlen(SALT)];
  uint8_t hash[20];
  memcpy(data, mfrc522.uid.uidByte, mfrc522.uid.size);
  memcpy(&data[mfrc522.uid.size], SALT, strlen(SALT));
  sha1(data, sizeof(data), hash);
  memcpy(password, &hash[16], 4);
  memcpy(pack, &hash[14], 2);
}

boolean RFIDCoroutine::readBalance() {
  Balance balance;

  if (mfrc522.PICC_GetType(mfrc522.uid.sak) == mfrc522.PICC_TYPE_MIFARE_UL) {
    unsigned char command[] = {0x39, 0x00, 0x7F,
                               0x1A};  // Read Counter 00 + CRC
    ultralightCounter = 0;
    byte backLen = sizeof(ultralightCounter);
    if (MFRC522::STATUS_OK !=
        mfrc522.PCD_TransceiveData(command, sizeof(command),
                                   (byte*)&ultralightCounter, &backLen)) {
      return false;
    }

    unsigned char size = 23;
    unsigned char payload[size];
    if (MFRC522::STATUS_OK != mfrc522.MIFARE_Read(9, payload, &size)) {
      return false;
    }
    unsigned char buffer[17];
    decode_base64(payload, buffer);

    uint8_t* deposit = (uint8_t*)(buffer + 9);
    uint16_t* balance = (uint16_t*)(buffer + 10);
    // uint8_t d = *deposit;
    // uint16_t b = *balance;
    balance = {
        .deposit = reinterpret_cast<std::uintptr_t>(deposit),
        .total = reinterpret_cast<std::uintptr_t>(balance),
    };
    // calculateSignatureUltralight( ultralightCounter);
    // TODO verify signature

  } else {
    MFRC522::StatusCode status = mfrc522.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_B, 6, &KEY_B, &(mfrc522.uid));
    unsigned char size = 18;
    unsigned char buffer[size];

    // read balance
    if (status == MFRC522::STATUS_OK) {
      status = mfrc522.MIFARE_Read(6, buffer, &size);
    }

    if (status != MFRC522::STATUS_OK) {
      Log.errorln("[RFID] Card not readable");
      displayCoroutine.show("Karte nicht", "lesbar", 2000);
      return false;
    }

    balance = {
        .deposit = (buffer[4] - '0'),
        .total = (buffer[0] - '0') * 1000 + (buffer[1] - '0') * 100 +
                 (buffer[2] - '0') * 10 + (buffer[3] - '0'),
    };

    // validate hash
    unsigned char hash[11];
    calculateHash(hash, balance);
    for (int i = 0; i < 10; i++) {
      if (hash[i] != buffer[i + 5]) {
        return false;
      }
    }
  }

  cardValueBefore = balance;
  cardValueAfter = balance;
  return true;
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

void RFIDCoroutine::calculateSignatureUltralight(unsigned char* target,
                                                 Balance balance,
                                                 uint16_t counter) {
  unsigned int len = mfrc522.uid.size +  // ID
                     2 +                 // count
                     1 +                 // deposit
                     2 +                 // balance
                     strlen(SALT);

  unsigned char buffer[len];
  unsigned int written = constructPayload(buffer, balance, counter);
  memcpy(buffer + written, SALT, strlen(SALT));
  unsigned char hash[20];
  sha1(buffer, len, hash);
  memcpy(target, hash, 5);
}

unsigned int RFIDCoroutine::constructPayload(unsigned char* target,
                                             Balance balance,
                                             uint16_t counter) {
  unsigned char* start = &target[0];
  memcpy(target, mfrc522.uid.uidByte, mfrc522.uid.size);
  target += mfrc522.uid.size;
  memcpy(target, &counter, 2);
  target += 2;
  memcpy(target, &balance.deposit, 1);
  target += 1;
  memcpy(target, &balance.total, 2);
  target += 2;

  return target - start;
}

boolean RFIDCoroutine::writeBalance(Balance balance) {
  if (mfrc522.PICC_GetType(mfrc522.uid.sak) == mfrc522.PICC_TYPE_MIFARE_UL) {
    if (false) {  // TODO
      unsigned char password[4];
      unsigned char pack[2];
      calculatePassword(password, pack);
      unsigned char packRead[2];
      if (mfrc522.PCD_NTAG216_AUTH(password, packRead) != MFRC522::STATUS_OK ||
          memcmp(packRead, pack, 2) != 0) {
        return false;
      }
    }

    unsigned int len = mfrc522.uid.size +  // ID
                       2 +                 // count
                       1 +                 // deposit
                       2 +                 // balance
                       5;                  // signature

    ultralightCounter++;
    unsigned char buffer[len];

    unsigned char incrementCounter[] = {0xA5,  // INCR_CNT
                                        0x00,  // Counter 0
                                        0x01,  // increment by 1
                                        0x00, 0x00, 0x00};

    // write counter: 0xA5, 0x00, 0x01, 0x00, 0x00, 0x00
    unsigned int written = constructPayload(buffer, balance, ultralightCounter);
    calculateSignatureUltralight(buffer + written, balance, ultralightCounter);

    unsigned int base64len = encode_base64_length(len);
    unsigned char writeData[base64len];
    encode_base64(buffer, len, writeData);
    writeData[base64len - 1] = 0xFE;  // override padding =

    for (int i = 0; i < base64len; i++) {
      Log.error("%c ", writeData[i]);
    }
    Log.error("\n");

    for (int i = 0; i < base64len / 4; i++) {
      if (mfrc522.MIFARE_Ultralight_Write(i + 9, &writeData[4 * i], 4) !=
          MFRC522::STATUS_OK) {
        return false;
      }
    }
  } else {
    unsigned char writeData[16];
    writeData[0] = ((balance.total / 1000) % 10) + '0';
    writeData[1] = ((balance.total / 100) % 10) + '0';
    writeData[2] = ((balance.total / 10) % 10) + '0';
    writeData[3] = (balance.total % 10) + '0';
    writeData[4] = (balance.deposit % 10) + '0';
    writeData[15] = 0xfe;
    calculateHash(writeData + 5, balance);
    if (!authenticateAndWrite(BLOCK_ADDRESS, writeData, &KEY_B)) {
      return false;
    }
  }
  return true;
}

void RFIDCoroutine::resetReader() {
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  cardId[0] = '\0';
  ultralightCounter = 0;
}
