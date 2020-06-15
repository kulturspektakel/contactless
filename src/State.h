#ifndef State_h
#define State_h

#include "proto/config.pb.h"
#include "proto/transaction.pb.h"

struct State {
  char entry[9];
  int value;
  int tokens;
  bool manualEntry;
  ConfigMessage config;
  TransactionMessage transaction;
};

#endif /* State_h */
