#pragma once

#include <cstdint>

enum class Kind : std::uint8_t {
  Int = 0,
  Real = 1,
};

[[noserde]] struct Example {
  bool flag;
  std::int32_t id;

  union Value {
    std::int32_t as_int;
    double as_real;
  } value;

  Kind kind;
};
