#pragma once
// Host stub. Ported from Free-Ink/freeink-sdk f80a99c (libs/network/SecureNet/test/host/stubs).
#include <Arduino.h>
struct base64 {
  static String encode(const char*) { return String("stub"); }
};
