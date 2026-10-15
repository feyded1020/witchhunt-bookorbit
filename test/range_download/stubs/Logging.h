#pragma once
// Host stub: logging prints nothing, but the arguments are still type-checked against the format and
// count as used.
#include <cstdio>
#define LOG_STUB_(format, ...)                     \
  do {                                             \
    if (false) std::printf(format, ##__VA_ARGS__); \
  } while (0)
#define LOG_ERR(origin, format, ...) LOG_STUB_(format, ##__VA_ARGS__)
#define LOG_INF(origin, format, ...) LOG_STUB_(format, ##__VA_ARGS__)
#define LOG_DBG(origin, format, ...) LOG_STUB_(format, ##__VA_ARGS__)
