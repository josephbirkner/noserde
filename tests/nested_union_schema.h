#pragma once

#include <cstdint>
#include <noserde.hpp>

enum class Mode : std::uint8_t {
  Pair = 0,
  U64 = 1,
  F32 = 2,
  Node = 3,
  I32 = 4,
};

struct [[noserde]] Pair {
  std::int16_t x;
  std::int16_t y;
};

struct [[noserde]] Node {
  bool valid;
  noserde::variant<Pair, std::uint64_t, float> payload;

  Mode mode;
};

struct [[noserde]] Envelope {
  Node node;
  noserde::variant<Pair, Node, std::int32_t> choice;

  std::uint16_t tail;
};
