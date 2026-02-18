#include "tests/pod_schema.h"

#include <noserde.hpp>

#include <bitsery/adapter/buffer.h>
#include <bitsery/deserializer.h>
#include <bitsery/serializer.h>
#include <bitsery/traits/vector.h>

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <type_traits>
#include <vector>

static_assert(noserde::is_native_pod_wire_type_v<glm::fvec3>);
static_assert(noserde::wire_sizeof<glm::fvec3>() == sizeof(glm::fvec3));
static_assert(noserde::schema_record_sizeof<glm::fvec3>() == sizeof(glm::fvec3));
static_assert(std::is_same_v<noserde::Buffer<glm::fvec3>::ref, glm::fvec3&>);
static_assert(noserde::Buffer<glm::fvec3, 8>::kPageSizeBytes == 8 * sizeof(glm::fvec3));

#if defined(__EMSCRIPTEN__) || defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)
static_assert(std::endian::native == std::endian::little,
              "wasm native POD fast-path assumes little-endian");
#endif

int main() {
  noserde::Buffer<glm::fvec3, 2> points;
  points.emplace(1.0F, 2.0F, 3.0F);
  auto& second = points.emplace_back();
  second = glm::fvec3{-4.0F, 5.0F, 6.0F};

  assert(points.size() == 2);
  assert(points.byte_size() == 2 * sizeof(glm::fvec3));
  assert(points[0].x == 1.0F);
  assert(points[0].y == 2.0F);
  assert(points[0].z == 3.0F);
  assert(points[1].x == -4.0F);
  assert(points[1].y == 5.0F);
  assert(points[1].z == 6.0F);

  const auto raw = points.bytes();
  assert(raw.size() == points.byte_size());
  float first_x = 0.0F;
  std::memcpy(&first_x, raw.data(), sizeof(float));
  assert(first_x == 1.0F);

  noserde::Buffer<glm::fvec3, 2> from_bytes;
  auto assign_result = from_bytes.assign_bytes(std::span<const std::byte>(raw.data(), raw.size()));
  assert(assign_result.has_value());
  assert(from_bytes.size() == 2);
  assert(from_bytes[1].x == -4.0F);
  assert(from_bytes[1].y == 5.0F);
  assert(from_bytes[1].z == 6.0F);

  std::vector<std::uint8_t> raw_u8(raw.size());
  std::memcpy(raw_u8.data(), raw.data(), raw.size());
  noserde::Buffer<glm::fvec3, 2> from_u8;
  auto assign_u8_result = from_u8.assign_bytes(std::span<const std::uint8_t>(raw_u8.data(), raw_u8.size()));
  assert(assign_u8_result.has_value());
  assert(from_u8[0].x == 1.0F);

  std::vector<std::uint8_t> blob;
  const std::size_t written = bitsery::quickSerialization(
      bitsery::OutputBufferAdapter<std::vector<std::uint8_t>>(blob), points);
  blob.resize(written);
  assert(!blob.empty());

  noserde::Buffer<glm::fvec3, 2> decoded;
  const auto [decode_error, decode_completed] = bitsery::quickDeserialization(
      bitsery::InputBufferAdapter<std::vector<std::uint8_t>>(blob.begin(), blob.end()), decoded);
  assert(decode_error == bitsery::ReaderError::NoError);
  assert(decode_completed);
  assert(decoded.size() == 2);
  assert(decoded[0].z == 3.0F);
  assert(decoded[1].x == -4.0F);

  std::vector<std::uint8_t> tampered = blob;
  tampered[0] ^= 0xFFU;
  noserde::Buffer<glm::fvec3, 2> rejected;
  const auto [tampered_error, tampered_completed] = bitsery::quickDeserialization(
      bitsery::InputBufferAdapter<std::vector<std::uint8_t>>(tampered.begin(), tampered.end()),
      rejected);
  assert(tampered_error == bitsery::ReaderError::InvalidData);
  assert(!tampered_completed);
  assert(rejected.empty());

  using FlatVec3Buffer = noserde::Buffer<glm::fvec3, 4, noserde::vector_byte_storage>;
  FlatVec3Buffer flat;
  flat.emplace(7.0F, 8.0F, 9.0F);
  flat.emplace(10.0F, 11.0F, 12.0F);

  std::vector<std::uint8_t> flat_blob;
  const std::size_t flat_written = bitsery::quickSerialization(
      bitsery::OutputBufferAdapter<std::vector<std::uint8_t>>(flat_blob), flat);
  flat_blob.resize(flat_written);

  FlatVec3Buffer flat_decoded;
  const auto [flat_error, flat_completed] = bitsery::quickDeserialization(
      bitsery::InputBufferAdapter<std::vector<std::uint8_t>>(flat_blob.begin(), flat_blob.end()),
      flat_decoded);
  assert(flat_error == bitsery::ReaderError::NoError);
  assert(flat_completed);
  assert(flat_decoded.size() == 2);
  assert(flat_decoded[1].y == 11.0F);

  const std::filesystem::path path = std::filesystem::temp_directory_path() / "noserde_pod_roundtrip.bin";
  auto write_result = noserde::write_binary(path, points);
  assert(write_result.has_value());

  noserde::Buffer<glm::fvec3, 2> file_loaded;
  auto read_result = noserde::read_binary(path, file_loaded);
  assert(read_result.has_value());
  assert(file_loaded.size() == points.size());
  assert(file_loaded[0].x == points[0].x);
  assert(file_loaded[1].z == points[1].z);
  std::filesystem::remove(path);

  noserde::Buffer<PodEnvelope> records;
  auto rec = records.emplace_back();
  rec.point = glm::fvec3{10.0F, 11.0F, 12.0F};
  rec.tagged.emplace<glm::fvec3>(glm::fvec3{1.0F, 2.0F, 3.0F});
  rec.raw.emplace<glm::fvec3>(glm::fvec3{4.0F, 5.0F, 6.0F});

  auto* tagged = rec.tagged.get_if<glm::fvec3>();
  assert(tagged != nullptr);
  const glm::fvec3 tagged_value = static_cast<glm::fvec3>(*tagged);
  assert(tagged_value.x == 1.0F);
  assert(tagged_value.y == 2.0F);
  assert(tagged_value.z == 3.0F);

  const glm::fvec3 raw_value = static_cast<glm::fvec3>(rec.raw.as<glm::fvec3>());
  assert(raw_value.x == 4.0F);
  assert(raw_value.y == 5.0F);
  assert(raw_value.z == 6.0F);

  return 0;
}
