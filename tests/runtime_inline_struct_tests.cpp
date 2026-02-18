#include "tests/inline_struct_schema.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

template <typename U>
concept has_get = requires(U& u) {
  u.template get<InlineWords>();
};

static_assert(!has_get<InlineDemo::payload_union_ref>, "u.get<Alt>() must not be generated");
static_assert(noserde::has_record_data_traits_v<InlineDemo>);

int main() {
  noserde::Buffer<InlineDemo, 1> buf;

  InlineDemo::Data seed{};
  seed.meta.x = static_cast<std::int16_t>(-9);
  seed.meta.enabled = true;
  seed.payload = InlineWords::Data{0x11223344U, 0x55667788U};
  seed.marker = static_cast<std::uint8_t>(0xAB);
  auto r = buf.emplace(seed);

  auto words = r.payload.as<InlineWords>();
  assert(static_cast<std::uint32_t>(words.hi) == 0x11223344U);
  assert(static_cast<std::uint32_t>(words.lo) == 0x55667788U);

  const auto bytes = buf.bytes();
  const auto marker_off = InlineDemo::__layout::marker_offset;
  assert(std::to_integer<std::uint8_t>(bytes[marker_off]) == 0xAB);

  const auto payload_off = InlineDemo::__layout::payload_payload_offset;
  assert(std::to_integer<std::uint8_t>(bytes[payload_off + 0]) == 0x44);
  assert(std::to_integer<std::uint8_t>(bytes[payload_off + 1]) == 0x33);
  assert(std::to_integer<std::uint8_t>(bytes[payload_off + 2]) == 0x22);
  assert(std::to_integer<std::uint8_t>(bytes[payload_off + 3]) == 0x11);

  r.payload.emplace<double>(2.0);
  auto as_double = r.payload.as<double>();
  assert(static_cast<double>(as_double) == 2.0);

  auto cr = static_cast<const noserde::Buffer<InlineDemo, 1>&>(buf)[0];
  auto cdouble = cr.payload.as<double>();
  assert(static_cast<double>(cdouble) == 2.0);

  return 0;
}
