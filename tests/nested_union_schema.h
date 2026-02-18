#pragma once

#include <cstdint>

enum class Mode : std::uint8_t {
  Pair = 0,
  U64 = 1,
  F32 = 2,
  Node = 3,
  I32 = 4,
};

[[noserde]] struct Pair {
  std::int16_t x;
  std::int16_t y;
};

[[noserde]] struct Node {
  bool valid;

  union Payload {
    Pair as_pair;
    std::uint64_t as_u64;
    float as_f32;
  } payload;

  Mode mode;
};

[[noserde]] struct Envelope {
  Node node;

  union Choice {
    Pair as_pair;
    Node as_node;
    std::int32_t as_i32;
  } choice;

  std::uint16_t tail;
};
