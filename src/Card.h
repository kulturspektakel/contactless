#pragma once
#include <Arduino.h>
#include <MFRC522.h>
#define _ASCII_OFFSET 0x30

class StateManager;

class Card {
  static const int BLOCK_ADDRESS;
  static byte ACCESS_BITS[4];
  static MFRC522::MIFARE_Key KEY_A1;
  static MFRC522::MIFARE_Key KEY_A2;
  static MFRC522::MIFARE_Key KEY_INIT;

  StateManager& stateManager;
  MFRC522* mfrc522;
  unsigned char serNum[16];

  boolean tryTransaction();
  void handleCard();
  void initCard();
  boolean authenticateAndRead(int block,
                              unsigned char target[18],
                              MFRC522::MIFARE_Key* key);
  boolean authenticateAndWrite(int block,
                               unsigned char writeData[16],
                               MFRC522::MIFARE_Key* key);
  void concat(byte target[16],
              byte (&keyA)[6],
              byte (&access)[4],
              const byte (&keyB)[6]);
  void setCardID();
  void calculateHash(unsigned char* target, int value, int tokens);
  boolean isModeChanger();
  boolean readBalance();
  boolean writeBalance(int newValue, int newTokens);
  void closeCard();

 public:
  Card(StateManager& stateManager, int channelPin, int rstPin);
  ~Card();
  void setup();
  void loop();
};
