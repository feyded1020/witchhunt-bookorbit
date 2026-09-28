#pragma once
// FontDecompressor.cpp times getBitmap() with micros(), which the shared shim does not provide.
// Wrapped here rather than added there: no other host test links code that needs it.
#include "../shims/Arduino.h"

inline unsigned long micros() { return 0; }
