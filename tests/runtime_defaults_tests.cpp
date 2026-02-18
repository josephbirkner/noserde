#include "tests/defaults_schema.h"

#include <cassert>
#include <cstdint>
#include <type_traits>

template <typename U>
concept has_get = requires(U& u) {
  u.template get<std::int32_t>();
};

static_assert(!has_get<DefaultsExample::tagged_variant_ref>, "u.get<Alt>() must not be generated");
static_assert(noserde::has_record_data_traits_v<DefaultsExample>);

int main() {
  noserde::Buffer<DefaultsExample> buf;

  auto r0 = buf.emplace_back();
  assert(static_cast<bool>(r0.flag));
  assert(static_cast<std::int32_t>(r0.count) == 7);
  assert(static_cast<std::int32_t>(r0.point.x) == 11);
  assert(static_cast<std::int32_t>(r0.point.y) == -3);
  assert(r0.tagged.holds_alternative<Vec2D>());
  auto* t0 = r0.tagged.get_if<Vec2D>();
  assert(t0 != nullptr);
  assert(static_cast<std::int32_t>(t0->x) == 4);
  assert(static_cast<std::int32_t>(t0->y) == 5);
  auto u0 = r0.raw.as<Vec2D>();
  assert(static_cast<std::int32_t>(u0.x) == 9);
  assert(static_cast<std::int32_t>(u0.y) == 8);

  auto r1 = buf.emplace(
      false,
      static_cast<std::int32_t>(123),
      Vec2D::Data{1, 2},
      DefaultsExample::tagged_data{std::int32_t{42}},
      DefaultsExample::raw_data{float{1.5f}});
  assert(!static_cast<bool>(r1.flag));
  assert(static_cast<std::int32_t>(r1.count) == 123);
  assert(static_cast<std::int32_t>(r1.point.x) == 1);
  assert(static_cast<std::int32_t>(r1.point.y) == 2);
  assert(r1.tagged.holds_alternative<std::int32_t>());
  auto* t1 = r1.tagged.get_if<std::int32_t>();
  assert(t1 != nullptr);
  assert(static_cast<std::int32_t>(*t1) == 42);
  auto u1 = r1.raw.as<float>();
  assert(static_cast<float>(u1) == 1.5f);

  auto r2 = buf.emplace();
  assert(static_cast<bool>(r2.flag));
  assert(static_cast<std::int32_t>(r2.count) == 7);
  assert(static_cast<std::int32_t>(r2.point.x) == 11);
  assert(static_cast<std::int32_t>(r2.point.y) == -3);
  assert(r2.tagged.holds_alternative<Vec2D>());
  auto* t2 = r2.tagged.get_if<Vec2D>();
  assert(t2 != nullptr);
  assert(static_cast<std::int32_t>(t2->x) == 4);
  assert(static_cast<std::int32_t>(t2->y) == 5);
  auto u2 = r2.raw.as<Vec2D>();
  assert(static_cast<std::int32_t>(u2.x) == 9);
  assert(static_cast<std::int32_t>(u2.y) == 8);

  return 0;
}
