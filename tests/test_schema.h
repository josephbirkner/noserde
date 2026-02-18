#pragma once

#include <cstdint>
#include <noserde.hpp>

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
  noserde::variant<std::int32_t, double> value;

  Kind kind;
};
