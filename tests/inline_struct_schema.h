#pragma once

#include <cstdint>

[[noserde]] struct InlineDemo {
  struct Meta {
    std::int16_t x;
    bool enabled;
  } meta;

  union Payload {
    struct Words {
      std::uint32_t hi;
      std::uint32_t lo;
    } words;
    double as_double;
  } payload;

  std::uint8_t marker;
};
