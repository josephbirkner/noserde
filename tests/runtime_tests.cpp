#include "tests/test_schema.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <type_traits>

template <typename U>
concept has_get = requires(U& u) {
  u.template get<std::int32_t>();
};

static_assert(!has_get<Example::value_union_ref>, "u.get<Alt>() must not be generated");
static_assert(noserde::Buffer<Example, 3>::kRecordsPerPage == 3);
static_assert(noserde::Buffer<Example, 3>::kPageSizeBytes == 3 * Example::noserde_size_bytes);
static_assert(noserde::has_record_data_traits_v<Example>);

int main() {
  noserde::Buffer<Example> buffer;
  auto record = buffer.emplace(
      true,
      static_cast<std::int32_t>(0x12345678),
      Inner::Data{static_cast<std::int16_t>(-23), true},
      Example::value_data{std::int32_t{7}},
      Kind::Int);

  assert(record.value.index() == 0);
  assert(record.value.holds_alternative<std::int32_t>());

  auto* as_int = record.value.get_if<std::int32_t>();
  assert(as_int != nullptr);
  assert(static_cast<std::int32_t>(*as_int) == 7);
  assert(record.value.get_if<double>() == nullptr);

  std::int32_t visited_int = 0;
  record.value.visit([&](auto alt) {
    visited_int = static_cast<std::int32_t>(alt);
  });
  assert(visited_int == 7);

  const auto bytes = buffer.bytes();
  assert(bytes.size() == Example::noserde_size_bytes);

  const auto id_off = Example::__layout::id_offset;
  assert(std::to_integer<std::uint8_t>(bytes[id_off + 0]) == 0x78);
  assert(std::to_integer<std::uint8_t>(bytes[id_off + 1]) == 0x56);
  assert(std::to_integer<std::uint8_t>(bytes[id_off + 2]) == 0x34);
  assert(std::to_integer<std::uint8_t>(bytes[id_off + 3]) == 0x12);

  const auto tag_off = Example::__layout::value_tag_offset;
  assert(std::to_integer<std::uint8_t>(bytes[tag_off + 0]) == 0);
  assert(std::to_integer<std::uint8_t>(bytes[tag_off + 1]) == 0);
  assert(std::to_integer<std::uint8_t>(bytes[tag_off + 2]) == 0);
  assert(std::to_integer<std::uint8_t>(bytes[tag_off + 3]) == 0);

  record.value.emplace<double>(1.5);
  assert(record.value.index() == 1);
  assert(record.value.holds_alternative<double>());

  auto* as_real = record.value.get_if<double>();
  assert(as_real != nullptr);
  assert(static_cast<double>(*as_real) == 1.5);

  const auto bytes_after = buffer.bytes();
  assert(std::to_integer<std::uint8_t>(bytes_after[tag_off + 0]) == 1);
  assert(std::to_integer<std::uint8_t>(bytes_after[tag_off + 1]) == 0);
  assert(std::to_integer<std::uint8_t>(bytes_after[tag_off + 2]) == 0);
  assert(std::to_integer<std::uint8_t>(bytes_after[tag_off + 3]) == 0);

  const auto payload_off = Example::__layout::value_payload_offset;
  // 1.5 in IEEE-754 double is 0x3FF8000000000000, so canonical LE bytes end in F8 3F.
  assert(std::to_integer<std::uint8_t>(bytes_after[payload_off + 0]) == 0x00);
  assert(std::to_integer<std::uint8_t>(bytes_after[payload_off + 6]) == 0xF8);
  assert(std::to_integer<std::uint8_t>(bytes_after[payload_off + 7]) == 0x3F);

  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "noserde_runtime_roundtrip.bin";

  auto write_result = noserde::write_binary(path, buffer);
  assert(write_result.has_value());

  std::ifstream header_stream(path, std::ios::binary);
  std::array<std::uint8_t, noserde::k_binary_header_size> header{};
  header_stream.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
  assert(header_stream.gcount() == static_cast<std::streamsize>(header.size()));

  auto load_u64 = [&](std::size_t offset) -> std::uint64_t {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < sizeof(std::uint64_t); ++i) {
      value |= static_cast<std::uint64_t>(header[offset + i]) << (8 * i);
    }
    return value;
  };

  assert(header[0] == 'N');
  assert(header[1] == 'S');
  assert(header[2] == 'R');
  assert(header[3] == 'D');
  assert(header[4] == 'B');
  assert(header[5] == 'I');
  assert(header[6] == 'N');
  assert(header[7] == '1');
  assert(load_u64(16) == Example::noserde_size_bytes);
  assert(load_u64(24) == 1);
  assert(load_u64(32) == Example::noserde_size_bytes);

  noserde::Buffer<Example> loaded;
  auto read_result = noserde::read_binary(path, loaded);
  assert(read_result.has_value());

  assert(loaded.size() == 1);
  auto loaded_record = loaded[0];
  assert(static_cast<bool>(loaded_record.flag));
  assert(static_cast<std::int32_t>(loaded_record.id) == static_cast<std::int32_t>(0x12345678));
  assert(static_cast<std::int16_t>(loaded_record.inner.score) == static_cast<std::int16_t>(-23));
  assert(static_cast<bool>(loaded_record.inner.enabled));
  assert(static_cast<Kind>(loaded_record.kind) == Kind::Int);

  auto* loaded_real = loaded_record.value.get_if<double>();
  assert(loaded_real != nullptr);
  assert(static_cast<double>(*loaded_real) == 1.5);

  const auto loaded_bytes = loaded.bytes();
  const auto original_bytes = buffer.bytes();
  assert(loaded_bytes.size() == original_bytes.size());
  assert(std::equal(loaded_bytes.begin(), loaded_bytes.end(), original_bytes.begin()));

  std::filesystem::remove(path);

  // Cross-page record access with small records-per-page configuration.
  noserde::Buffer<Example, 2> paged;
  paged.emplace(
      false,
      static_cast<std::int32_t>(100),
      Inner::Data{static_cast<std::int16_t>(0), false},
      Example::value_data{std::int32_t{0}},
      Kind::Int);
  for (std::int32_t i = 1; i < 5; ++i) {
    auto rr = paged.emplace_back();
    rr.id = i + 100;
    rr.value.emplace<std::int32_t>(i);
  }

  assert(paged.size() == 5);
  for (std::int32_t i = 0; i < 5; ++i) {
    auto rr = paged[static_cast<std::size_t>(i)];
    assert(static_cast<std::int32_t>(rr.id) == i + 100);
    auto* v = rr.value.get_if<std::int32_t>();
    assert(v != nullptr);
    assert(static_cast<std::int32_t>(*v) == i);
  }

  return 0;
}
