#pragma once

#include "logmessage.pb.h"

extern LogMessage log_message;
void write_log(LogMessage_Order_PaymentMethod payment_method);