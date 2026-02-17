#include "tests/test_schema.h"

#include <noserde.hpp>

#include <bitsery/adapter/buffer.h>
#include <bitsery/brief_syntax/vector.h>
#include <bitsery/deserializer.h>
#include <bitsery/serializer.h>
#include <bitsery/traits/vector.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

struct RawInner {
  std::int16_t score{};
  bool enabled{};
};

struct RawExample {
  bool flag{};
  std::int32_t id{};
  RawInner inner{};
  std::uint32_t value_tag{};
  std::int32_t value_as_int{};
  double value_as_real{};
  Kind kind{};
};

namespace bitsery {

template <typename S>
void serialize(S& s, RawInner& v) {
  s.value2b(v.score);
  s.boolValue(v.enabled);
}

template <typename S>
void serialize(S& s, RawExample& v) {
  s.boolValue(v.flag);
  s.value4b(v.id);
  s.object(v.inner);
  s.value4b(v.value_tag);
  s.value4b(v.value_as_int);
  s.value8b(v.value_as_real);
  s.value1b(v.kind);
}

}  // namespace bitsery

namespace {

std::uint32_t next_lcg(std::uint32_t& state) {
  state = state * 1664525U + 1013904223U;
  return state;
}

void build_sources(std::size_t records, std::vector<RawExample>& raw, noserde::Buffer<Example>& noserde_buf) {
  raw.clear();
  raw.reserve(records);
  noserde_buf.clear();

  std::uint32_t rng = 0xC0FFEE42U;
  for (std::size_t i = 0; i < records; ++i) {
    const std::uint32_t r0 = next_lcg(rng);
    const std::uint32_t r1 = next_lcg(rng);
    const bool use_real = (r0 & 1U) != 0U;

    RawExample rec{};
    rec.flag = (r0 & 2U) != 0U;
    rec.id = static_cast<std::int32_t>(r0);
    rec.inner.score = static_cast<std::int16_t>(r1 & 0xFFFFU);
    rec.inner.enabled = (r1 & 4U) != 0U;
    rec.value_tag = use_real ? 1U : 0U;
    rec.value_as_int = static_cast<std::int32_t>(r1 ^ 0x5A5A5A5AU);
    rec.value_as_real = static_cast<double>(static_cast<std::int32_t>(r1 % 100000U)) / 31.0;
    rec.kind = use_real ? Kind::Real : Kind::Int;
    raw.push_back(rec);

    auto nr = noserde_buf.emplace_back();
    nr.flag = rec.flag;
    nr.id = rec.id;
    nr.inner.score = rec.inner.score;
    nr.inner.enabled = rec.inner.enabled;
    nr.kind = rec.kind;
    if (use_real) {
      nr.value.emplace<double>(rec.value_as_real);
    } else {
      nr.value.emplace<std::int32_t>(rec.value_as_int);
    }
  }
}

std::size_t serialize_raw(const std::vector<RawExample>& src, std::vector<std::uint8_t>& out) {
  out.clear();
  const std::size_t written =
      bitsery::quickSerialization(bitsery::OutputBufferAdapter<std::vector<std::uint8_t>>(out), src);
  out.resize(written);
  return written;
}

bool deserialize_raw(const std::vector<std::uint8_t>& in, std::vector<RawExample>& dst) {
  const auto [error, completed] = bitsery::quickDeserialization(
      bitsery::InputBufferAdapter<std::vector<std::uint8_t>>(in.begin(), in.end()), dst);
  return error == bitsery::ReaderError::NoError && completed;
}

std::size_t serialize_noserde(const noserde::Buffer<Example>& src, std::vector<std::uint8_t>& out) {
  out.clear();
  const std::size_t written =
      bitsery::quickSerialization(bitsery::OutputBufferAdapter<std::vector<std::uint8_t>>(out), src);
  out.resize(written);
  return written;
}

bool deserialize_noserde(const std::vector<std::uint8_t>& in, noserde::Buffer<Example>& dst) {
  const auto [error, completed] = bitsery::quickDeserialization(
      bitsery::InputBufferAdapter<std::vector<std::uint8_t>>(in.begin(), in.end()), dst);
  return error == bitsery::ReaderError::NoError && completed;
}

using FlatNoserdeBuffer = noserde::Buffer<Example, 256, noserde::vector_byte_storage>;

bool deserialize_noserde_flat(const std::vector<std::uint8_t>& in, FlatNoserdeBuffer& dst) {
  const auto [error, completed] = bitsery::quickDeserialization(
      bitsery::InputBufferAdapter<std::vector<std::uint8_t>>(in.begin(), in.end()), dst);
  return error == bitsery::ReaderError::NoError && completed;
}

std::uint64_t checksum_raw(const std::vector<RawExample>& records) {
  if (records.empty()) {
    return 0;
  }
  const RawExample& first = records.front();
  const RawExample& last = records.back();
  std::uint64_t sum = static_cast<std::uint64_t>(records.size());
  sum ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(first.id));
  sum ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(last.id)) << 17U;
  sum ^= static_cast<std::uint64_t>(first.value_tag) << 33U;
  sum ^= static_cast<std::uint64_t>(last.value_tag) << 41U;
  return sum;
}

std::uint64_t checksum_noserde(const noserde::Buffer<Example>& records) {
  if (records.empty()) {
    return 0;
  }
  const auto first = records[0];
  const auto last = records[records.size() - 1];
  std::uint64_t sum = static_cast<std::uint64_t>(records.size());
  sum ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(first.id)));
  sum ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(last.id))) << 17U;
  sum ^= static_cast<std::uint64_t>(first.value.index()) << 33U;
  sum ^= static_cast<std::uint64_t>(last.value.index()) << 41U;
  return sum;
}

std::uint64_t checksum_noserde_flat(const FlatNoserdeBuffer& records) {
  if (records.empty()) {
    return 0;
  }
  const auto first = records[0];
  const auto last = records[records.size() - 1];
  std::uint64_t sum = static_cast<std::uint64_t>(records.size());
  sum ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(first.id)));
  sum ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(last.id))) << 17U;
  sum ^= static_cast<std::uint64_t>(first.value.index()) << 33U;
  sum ^= static_cast<std::uint64_t>(last.value.index()) << 41U;
  return sum;
}

template <typename F>
double measure_seconds(std::size_t iterations, F&& fn) {
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < iterations; ++i) {
    fn();
  }
  const auto end = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(end - start).count();
}

double throughput_mib_per_s(std::size_t bytes_per_iteration, std::size_t iterations, double seconds) {
  const double total_bytes = static_cast<double>(bytes_per_iteration) * static_cast<double>(iterations);
  const double total_mib = total_bytes / (1024.0 * 1024.0);
  return total_mib / seconds;
}

}  // namespace

int main(int argc, char** argv) {
  std::size_t records = 200000;
  std::size_t iterations = 40;
  if (argc >= 2) {
    records = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
  }
  if (argc >= 3) {
    iterations = static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));
  }
  if (records == 0 || iterations == 0) {
    std::cerr << "records and iterations must be > 0\n";
    return 1;
  }

  std::vector<RawExample> raw_src;
  noserde::Buffer<Example> noserde_src;
  build_sources(records, raw_src, noserde_src);
  assert(raw_src.size() == records);
  assert(noserde_src.size() == records);

  std::vector<std::uint8_t> raw_blob;
  std::vector<std::uint8_t> noserde_blob;
  std::vector<RawExample> raw_dst;
  noserde::Buffer<Example> noserde_dst;
  FlatNoserdeBuffer noserde_flat_dst;

  std::size_t raw_bytes = serialize_raw(raw_src, raw_blob);
  std::size_t noserde_bytes = serialize_noserde(noserde_src, noserde_blob);
  assert(raw_bytes == raw_blob.size());
  assert(noserde_bytes == noserde_blob.size());

  assert(deserialize_raw(raw_blob, raw_dst));
  assert(deserialize_noserde(noserde_blob, noserde_dst));
  assert(deserialize_noserde_flat(noserde_blob, noserde_flat_dst));
  assert(raw_dst.size() == raw_src.size());
  assert(noserde_dst.size() == noserde_src.size());
  assert(noserde_flat_dst.size() == noserde_src.size());

  std::uint64_t sink = 0;

  const double raw_ser_s = measure_seconds(iterations, [&] {
    raw_bytes = serialize_raw(raw_src, raw_blob);
    sink ^= static_cast<std::uint64_t>(raw_bytes);
  });

  const double noserde_ser_s = measure_seconds(iterations, [&] {
    noserde_bytes = serialize_noserde(noserde_src, noserde_blob);
    sink ^= static_cast<std::uint64_t>(noserde_bytes);
  });

  const double raw_des_s = measure_seconds(iterations, [&] {
    const bool ok = deserialize_raw(raw_blob, raw_dst);
    assert(ok);
    sink ^= checksum_raw(raw_dst);
  });

  const double noserde_des_s = measure_seconds(iterations, [&] {
    const bool ok = deserialize_noserde(noserde_blob, noserde_dst);
    assert(ok);
    sink ^= checksum_noserde(noserde_dst);
  });

  const double noserde_flat_des_s = measure_seconds(iterations, [&] {
    const bool ok = deserialize_noserde_flat(noserde_blob, noserde_flat_dst);
    assert(ok);
    sink ^= checksum_noserde_flat(noserde_flat_dst);
  });

  const double raw_ser_mib_s = throughput_mib_per_s(raw_bytes, iterations, raw_ser_s);
  const double noserde_ser_mib_s = throughput_mib_per_s(noserde_bytes, iterations, noserde_ser_s);
  const double raw_des_mib_s = throughput_mib_per_s(raw_bytes, iterations, raw_des_s);
  const double noserde_des_mib_s = throughput_mib_per_s(noserde_bytes, iterations, noserde_des_s);
  const double noserde_flat_des_mib_s =
      throughput_mib_per_s(noserde_bytes, iterations, noserde_flat_des_s);

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "records=" << records << " iterations=" << iterations << "\n";
  std::cout << "raw_blob_bytes=" << raw_bytes << " noserde_blob_bytes=" << noserde_bytes << "\n";
  std::cout << "serialize_raw_mib_s=" << raw_ser_mib_s << "\n";
  std::cout << "serialize_noserde_mib_s=" << noserde_ser_mib_s << "\n";
  std::cout << "deserialize_raw_mib_s=" << raw_des_mib_s << "\n";
  std::cout << "deserialize_noserde_segmented_mib_s=" << noserde_des_mib_s << "\n";
  std::cout << "deserialize_noserde_vector_mib_s=" << noserde_flat_des_mib_s << "\n";
  std::cout << "serialize_speedup_x=" << (noserde_ser_mib_s / raw_ser_mib_s) << "\n";
  std::cout << "deserialize_segmented_speedup_x=" << (noserde_des_mib_s / raw_des_mib_s) << "\n";
  std::cout << "deserialize_vector_speedup_x=" << (noserde_flat_des_mib_s / raw_des_mib_s) << "\n";
  std::cout << "checksum_sink=" << sink << "\n";

  return 0;
}
