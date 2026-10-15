#pragma once
// Host stub: the two WiFi calls SecureHttpClient's request trace makes.
struct FakeWiFi {
  int RSSI() const { return 0; }
  int getSleep() const { return 0; }
};
inline FakeWiFi WiFi;
