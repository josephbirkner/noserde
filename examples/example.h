#pragma once

#include <cstdint>
#include <noserde.hpp>

enum class Kind : std::uint8_t {
  Int = 0,
  Real = 1,
};

[[noserde]] struct Example {
  bool flag;
  std::int32_t id;
  noserde::variant<std::int32_t, double> value;

  Kind kind;
};
