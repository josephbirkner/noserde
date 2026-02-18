#pragma once

#include <cstdint>

enum class Kind : std::uint8_t {
  Int = 0,
  Real = 1,
};

[[noserde]] struct Inner {
  std::int16_t score;
  bool enabled;
};

[[noserde]] struct Example {
  bool flag;
  std::int32_t id;
  Inner inner;

  union Value {
    std::int32_t as_int;
    double as_real;
  } value;

  Kind kind;
};
