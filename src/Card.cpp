#include "Card.h"
#include <ESP8266WiFi.h>
#include <Hash.h>
#include "StateManager.h"

extern const int TOKEN_VALUE;
extern const char* SALT;
extern MFRC522::MIFARE_Key KEY_B;
extern const std::vector<char*> MODE_CHANGER;

const int Card::BLOCK_ADDRESS = 6;
byte Card::ACCESS_BITS[4] = {0x78, 0x77, 0x88, 0x43};
MFRC522::MIFARE_Key Card::KEY_A1 = {{0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5}};
MFRC522::MIFARE_Key Card::KEY_A2 = {{0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7}};
MFRC522::MIFARE_Key Card::KEY_INIT = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};

Card::Card(StateManager& sm, int channelPin, int rstPin) : stateManager(sm) {
  mfrc522 = new MFRC522(channelPin, rstPin);
}

Card::~Card() {
  delete mfrc522;
}

void Card::setup() {
  mfrc522->PCD_Init();
}

void Card::loop() {
  if (stateManager.state.transaction.mode != TransactionMessage_Mode_TOP_UP &&
      stateManager.state.transaction.mode != TransactionMessage_Mode_CASHOUT &&
      stateManager.state.transaction.mode !=
          TransactionMessage_Mode_INIT_CARD &&
      stateManager.state.transaction.mode != TransactionMessage_Mode_CHARGE) {
    // non card reading mode
    return;
  }
  if (!mfrc522->PICC_IsNewCardPresent() || !mfrc522->PICC_ReadCardSerial()) {
    // No card present
    return;
  }

  if (stateManager.state.transaction.mode ==
      TransactionMessage_Mode_INIT_CARD) {
    initCard();
  } else {
    handleCard();
  }

  closeCard();
}

void Card::closeCard() {
  // Reset reader, so a new card can be read
  mfrc522->PICC_HaltA();
  mfrc522->PCD_StopCrypto1();
}

void Card::handleCard() {
  setCardID();

  if (isModeChanger()) {
    if (stateManager.state.transaction.mode == TransactionMessage_Mode_CHARGE ||
        stateManager.state.transaction.mode == TransactionMessage_Mode_TOP_UP) {
      stateManager.toggleChargerMode();
    }
  } else if (readBalance()) {
    if (stateManager.state.value == 0 && stateManager.state.tokens == 0 &&
        stateManager.state.transaction.mode !=
            TransactionMessage_Mode_CASHOUT) {
      stateManager.showBalance();
    } else {
      tryTransaction();
    }
  } else {
    stateManager.error("Karte nicht     lesbar");
  }
}

void Card::setCardID() {
  for (int i = 0; i < 4; i++) {
    byte nib1 = (mfrc522->uid.uidByte[i] >> 4) & 0x0F;
    byte nib2 = (mfrc522->uid.uidByte[i] >> 0) & 0x0F;
    stateManager.state.transaction.card[i * 2 + 0] =
        nib1 < 0xA ? '0' + nib1 : 'A' + nib1 - 0xA;
    stateManager.state.transaction.card[i * 2 + 1] =
        nib2 < 0xA ? '0' + nib2 : 'A' + nib2 - 0xA;
  }
  stateManager.state.transaction.card[8] = '\0';
}

boolean Card::isModeChanger() {
  for (auto const& elem : MODE_CHANGER) {
    if (strcmp(stateManager.state.transaction.card, elem) == 0) {
      return true;
    }
  }
  return false;
}

boolean Card::writeBalance(int newValue, int newTokens) {
  unsigned char writeData[16] = {
      static_cast<unsigned char>(((newValue / 1000) % 10) + _ASCII_OFFSET),
      static_cast<unsigned char>(((newValue / 100) % 10) + _ASCII_OFFSET),
      static_cast<unsigned char>(((newValue / 10) % 10) + _ASCII_OFFSET),
      static_cast<unsigned char>((newValue % 10) + _ASCII_OFFSET),
      static_cast<unsigned char>((newTokens % 10) + _ASCII_OFFSET),
      0x5f,
      0x5f,
      0x5f,
      0x5f,
      0x5f,
      0x5f,
      0x5f,
      0x5f,
      0x5f,
      0x5f,
      0xfe,
  };
  calculateHash(writeData + 5, newValue, newTokens);
  // we can write directly, because we are already authenticated from previous
  // read
  return (MFRC522::StatusCode)mfrc522->MIFARE_Write(BLOCK_ADDRESS, writeData,
                                                    16) == MFRC522::STATUS_OK;
}

boolean Card::readBalance() {
  byte buffer[18];
  if (authenticateAndRead(6, buffer, &KEY_B)) {
    int value = (buffer[0] - _ASCII_OFFSET) * 1000 +
                (buffer[1] - _ASCII_OFFSET) * 100 +
                (buffer[2] - _ASCII_OFFSET) * 10 + (buffer[3] - _ASCII_OFFSET);
    int tokens = (buffer[4] - _ASCII_OFFSET);
    unsigned char hash[11];
    // hash[10] = 0;
    calculateHash(hash, value, tokens);
    // check hash
    for (int i = 0; i < 11; i++) {
      if (hash[i] != buffer[i + 5]) {
        return false;
      }
    }
    stateManager.state.transaction.balanceBefore = value;
    stateManager.state.transaction.tokensBefore = tokens;
    return true;
  } else {
    return false;
  }
}

boolean Card::tryTransaction() {
  int newValue = stateManager.state.transaction.balanceBefore;
  int newTokens = stateManager.state.transaction.tokensBefore;

  if ((stateManager.state.transaction.mode == TransactionMessage_Mode_CHARGE &&
       stateManager.state.transaction.tokensBefore + stateManager.state.tokens <
           0) ||
      (stateManager.state.transaction.mode == TransactionMessage_Mode_TOP_UP &&
       stateManager.state.transaction.tokensBefore + stateManager.state.tokens <
           0)) {
    stateManager.error("Nicht genug     Pfandmarken");
    return false;
  }

  newTokens += stateManager.state.tokens;
  newValue -= stateManager.state.tokens * TOKEN_VALUE;

  if (stateManager.state.transaction.mode == TransactionMessage_Mode_CASHOUT) {
    newValue = 0;
    newTokens = 0;
  } else if (stateManager.state.transaction.mode ==
             TransactionMessage_Mode_TOP_UP) {
    newValue += stateManager.state.value;
  } else {
    if (newValue - stateManager.state.value < 0) {
      stateManager.error("Nicht genug     Guthaben");
      return false;
    } else {
      newValue -= stateManager.state.value;
    }
  }

  if (newTokens > 9 || newValue > (9999 - newTokens * TOKEN_VALUE)) {
    stateManager.error("Kartenlimit     erreicht");
    return false;
  }

  if (!writeBalance(newValue, newTokens)) {
    stateManager.error("Karte nicht     schreibbar");
    return false;
  }

  if (stateManager.state.transaction.mode == TransactionMessage_Mode_CASHOUT) {
    stateManager.cashoutCard();
  } else {
    stateManager.chargeCard(newValue, newTokens);
  }

  return true;
}

void Card::initCard() {
  setCardID();

  unsigned char writeData[6][16] = {
      // clang-format off
      {0x14, 0x01, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1},  // 1: NDEF message
      {0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1},  // 2: NDEF message
      {},                                                                                                // 3: access
      {0x00, 0x00, 0x03, 0x2B, 0xD1, 0x01, 0x27, 0x55, 0x04, 0x6B, 0x75, 0x6C, 0x74, 0x2E, 0x63, 0x61},  // 4: kult.ca
      {0x73, 0x68, 0x2F, 0x24, 0x24, 0x24, 0x2F, stateManager.state.transaction.card[0], stateManager.state.transaction.card[1], stateManager.state.transaction.card[2], stateManager.state.transaction.card[3], stateManager.state.transaction.card[4], stateManager.state.transaction.card[5], stateManager.state.transaction.card[6], stateManager.state.transaction.card[7], 0x2F},  // 5: sh/$$$/________/
      {0x30, 0x30, 0x30, 0x30, 0x30, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0x5F, 0xFE},  // 6: 00000__________
      // clang-format on
  };

  concat(writeData[2], KEY_A1.keyByte, ACCESS_BITS, KEY_B.keyByte);
  calculateHash(writeData[5] + 5, 0, 0);

  // initialize card
  for (int i = 0; i < 6; i++) {
    if (!authenticateAndWrite(i + 1, writeData[i], &KEY_INIT)) {
      return;
    }
  }

  // lock all blocks
  unsigned char trailer[16];
  concat(trailer, KEY_A2.keyByte, ACCESS_BITS, KEY_B.keyByte);
  for (int i = 7; i < 64; i += 4) {
    if (!authenticateAndWrite(i, trailer, &KEY_INIT)) {
      return;
    }
  }
  stateManager.cardInitialized();
}

boolean Card::authenticateAndWrite(int block,
                                   unsigned char writeData[16],
                                   MFRC522::MIFARE_Key* key) {
  MFRC522::StatusCode status;
  status = (MFRC522::StatusCode)mfrc522->PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_B, block, key, &(mfrc522->uid));
  if (status == MFRC522::STATUS_OK) {
    status = (MFRC522::StatusCode)mfrc522->MIFARE_Write(block, writeData, 16);
    if (status != MFRC522::STATUS_OK) {
      stateManager.error("Karte nicht     schreibbar");
      return false;
    }
  } else {
    stateManager.error("Karte nicht     lesbar");
    return false;
  }
  return true;
}

boolean Card::authenticateAndRead(int block,
                                  unsigned char target[18],
                                  MFRC522::MIFARE_Key* key) {
  MFRC522::StatusCode status;
  status = (MFRC522::StatusCode)mfrc522->PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_B, block, key, &(mfrc522->uid));
  if (status == MFRC522::STATUS_OK) {
    byte size = 18;
    status = (MFRC522::StatusCode)mfrc522->MIFARE_Read(block, target, &size);

    if (status != MFRC522::STATUS_OK) {
      return false;
    }
  } else {
    return false;
  }
  return true;
}

void Card::concat(byte target[16],
                  byte (&keyA)[6],
                  byte (&access)[4],
                  const byte (&keyB)[6]) {
  int i = 0;
  for (int j = 0; i < 6; i++, j++) {
    target[i] = keyA[j];
  }
  for (int j = 0; i < 10; i++, j++) {
    target[i] = access[j];
  }
  for (int j = 0; i < 16; i++, j++) {
    target[i] = keyB[j];
  }
}

void Card::calculateHash(unsigned char* target, int value, int tokens) {
  int len = strlen(stateManager.state.transaction.card) + strlen(SALT) + 6;
  char buffer[len];
  snprintf(buffer, len, "%d%d%s%s", value, tokens,
           stateManager.state.transaction.card, SALT);
  String hash = sha1(buffer, strlen(buffer));
  hash.toCharArray((char*)target, 11);
  target[10] = 0xFE;
}
