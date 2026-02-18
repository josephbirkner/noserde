#pragma once

#include <cstdint>
#include <noserde.hpp>

struct [[noserde]] Vec2D {
  std::int32_t x;
  std::int32_t y;
};

struct [[noserde]] DefaultsExample {
  bool flag = true;
  std::int32_t count = 7;
  Vec2D point = Vec2D(11, -3);
  noserde::variant<std::int32_t, Vec2D, double> tagged = Vec2D(4, 5);
  noserde::union_<std::uint32_t, float, Vec2D> raw = Vec2D(9, 8);
};
