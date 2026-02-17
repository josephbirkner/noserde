#include "tests/test_schema.h"

#include <noserde.hpp>

#include <bitsery/adapter/buffer.h>
#include <bitsery/deserializer.h>
#include <bitsery/serializer.h>
#include <bitsery/traits/vector.h>

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
  noserde::Buffer<Example> src;
  {
    auto r = src.emplace_back();
    r.flag = true;
    r.id = 111;
    r.inner.score = -7;
    r.inner.enabled = true;
    r.value.emplace<std::int32_t>(12345);
    r.kind = Kind::Int;
  }
  {
    auto r = src.emplace_back();
    r.flag = false;
    r.id = -222;
    r.inner.score = 19;
    r.inner.enabled = false;
    r.value.emplace<double>(3.5);
    r.kind = Kind::Real;
  }

  std::vector<std::uint8_t> blob;
  const std::size_t written = bitsery::quickSerialization(
      bitsery::OutputBufferAdapter<std::vector<std::uint8_t>>(blob), src);
  blob.resize(written);
  assert(!blob.empty());

  noserde::Buffer<Example> dst;
  const auto [read_error, completed] = bitsery::quickDeserialization(
      bitsery::InputBufferAdapter<std::vector<std::uint8_t>>(blob.begin(), blob.end()), dst);
  assert(read_error == bitsery::ReaderError::NoError);
  assert(completed);
  assert(dst.size() == 2);

  {
    auto r = dst[0];
    assert(static_cast<bool>(r.flag));
    assert(static_cast<std::int32_t>(r.id) == 111);
    assert(static_cast<std::int16_t>(r.inner.score) == -7);
    assert(static_cast<bool>(r.inner.enabled));
    assert(r.value.holds_alternative<std::int32_t>());
    auto* v = r.value.get_if<std::int32_t>();
    assert(v != nullptr);
    assert(static_cast<std::int32_t>(*v) == 12345);
    assert(static_cast<Kind>(r.kind) == Kind::Int);
  }

  {
    auto r = dst[1];
    assert(!static_cast<bool>(r.flag));
    assert(static_cast<std::int32_t>(r.id) == -222);
    assert(static_cast<std::int16_t>(r.inner.score) == 19);
    assert(!static_cast<bool>(r.inner.enabled));
    assert(r.value.holds_alternative<double>());
    auto* v = r.value.get_if<double>();
    assert(v != nullptr);
    assert(static_cast<double>(*v) == 3.5);
    assert(static_cast<Kind>(r.kind) == Kind::Real);
  }

  std::vector<std::uint8_t> tampered = blob;
  tampered[0] ^= 0xFFU;

  noserde::Buffer<Example> rejected;
  const auto [tampered_error, tampered_completed] = bitsery::quickDeserialization(
      bitsery::InputBufferAdapter<std::vector<std::uint8_t>>(tampered.begin(), tampered.end()),
      rejected);
  assert(tampered_error == bitsery::ReaderError::InvalidData);
  assert(!tampered_completed);
  assert(rejected.empty());

  noserde::Buffer<Example> empty_src;
  std::vector<std::uint8_t> empty_blob;
  const std::size_t empty_written = bitsery::quickSerialization(
      bitsery::OutputBufferAdapter<std::vector<std::uint8_t>>(empty_blob), empty_src);
  empty_blob.resize(empty_written);

  noserde::Buffer<Example> empty_dst;
  const auto [empty_error, empty_completed] = bitsery::quickDeserialization(
      bitsery::InputBufferAdapter<std::vector<std::uint8_t>>(empty_blob.begin(), empty_blob.end()),
      empty_dst);
  assert(empty_error == bitsery::ReaderError::NoError);
  assert(empty_completed);
  assert(empty_dst.empty());

  using FlatBuffer = noserde::Buffer<Example, 256, noserde::vector_byte_storage>;
  FlatBuffer flat_src;
  {
    auto r = flat_src.emplace_back();
    r.flag = true;
    r.id = 9;
    r.inner.score = 5;
    r.inner.enabled = true;
    r.value.emplace<std::int32_t>(77);
    r.kind = Kind::Int;
  }

  std::vector<std::uint8_t> flat_blob;
  const std::size_t flat_written = bitsery::quickSerialization(
      bitsery::OutputBufferAdapter<std::vector<std::uint8_t>>(flat_blob), flat_src);
  flat_blob.resize(flat_written);

  FlatBuffer flat_dst;
  const auto [flat_error, flat_completed] = bitsery::quickDeserialization(
      bitsery::InputBufferAdapter<std::vector<std::uint8_t>>(flat_blob.begin(), flat_blob.end()),
      flat_dst);
  assert(flat_error == bitsery::ReaderError::NoError);
  assert(flat_completed);
  assert(flat_dst.size() == 1);
  auto flat_r = flat_dst[0];
  assert(static_cast<std::int32_t>(flat_r.id) == 9);
  auto* flat_int = flat_r.value.get_if<std::int32_t>();
  assert(flat_int != nullptr);
  assert(static_cast<std::int32_t>(*flat_int) == 77);

  return 0;
}
