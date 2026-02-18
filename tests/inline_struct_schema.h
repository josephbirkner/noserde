#pragma once

#include <cstdint>
#include <noserde.hpp>

[[noserde]] struct InlineWords {
  std::uint32_t hi;
  std::uint32_t lo;
};

[[noserde]] struct InlineDemo {
  struct Meta {
    std::int16_t x;
    bool enabled;
  } meta;
  noserde::union_<InlineWords, double> payload;

  std::uint8_t marker;
};
