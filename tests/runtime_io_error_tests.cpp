#include "tests/nested_union_schema.h"
#include "tests/test_schema.h"

#include <noserde.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

std::vector<std::byte> read_all_bytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  in.seekg(0, std::ios::end);
  const auto size = static_cast<std::size_t>(in.tellg());
  in.seekg(0, std::ios::beg);

  std::vector<std::byte> data(size);
  if (size > 0) {
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
  }
  return data;
}

void write_all_bytes(const std::filesystem::path& path, const std::vector<std::byte>& data) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

void store_u64_le(std::vector<std::byte>& data, std::size_t offset, std::uint64_t value) {
  for (std::size_t i = 0; i < sizeof(std::uint64_t); ++i) {
    data[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
  }
}

}  // namespace

int main() {
  const std::filesystem::path base = std::filesystem::temp_directory_path() / "noserde_io_errors";
  const std::filesystem::path good = base.string() + "_good.bin";
  const std::filesystem::path bad_magic = base.string() + "_bad_magic.bin";
  const std::filesystem::path bad_header = base.string() + "_bad_header.bin";
  const std::filesystem::path truncated = base.string() + "_truncated.bin";

  std::filesystem::remove(good);
  std::filesystem::remove(bad_magic);
  std::filesystem::remove(bad_header);
  std::filesystem::remove(truncated);

  {
    noserde::Buffer<Example> missing;
    const auto missing_result = noserde::read_binary(base.string() + "_missing.bin", missing);
    assert(!missing_result.has_value());
    assert(missing_result.error() == noserde::io_error::open_failed);
  }

  noserde::Buffer<Example> src;
  auto r = src.emplace_back();
  r.flag = true;
  r.id = 77;
  r.kind = Kind::Int;
  r.value.emplace<std::int32_t>(33);

  const auto write_result = noserde::write_binary(good, src);
  assert(write_result.has_value());

  {
    noserde::Buffer<Node> wrong_schema;
    const auto mismatch = noserde::read_binary(good, wrong_schema);
    assert(!mismatch.has_value());
    assert(mismatch.error() == noserde::io_error::schema_mismatch);
  }

  const auto good_bytes = read_all_bytes(good);

  {
    auto data = good_bytes;
    data[0] = static_cast<std::byte>('X');
    write_all_bytes(bad_magic, data);

    noserde::Buffer<Example> dst;
    const auto result = noserde::read_binary(bad_magic, dst);
    assert(!result.has_value());
    assert(result.error() == noserde::io_error::invalid_header);
  }

  {
    auto data = good_bytes;
    const std::uint64_t payload_size = noserde::load_le<std::uint64_t>(good_bytes.data() + 32);
    store_u64_le(data, 32, payload_size + 1U);
    write_all_bytes(bad_header, data);

    noserde::Buffer<Example> dst;
    const auto result = noserde::read_binary(bad_header, dst);
    assert(!result.has_value());
    assert(result.error() == noserde::io_error::invalid_header);
  }

  {
    auto data = good_bytes;
    data.pop_back();
    write_all_bytes(truncated, data);

    noserde::Buffer<Example> dst;
    const auto result = noserde::read_binary(truncated, dst);
    assert(!result.has_value());
    assert(result.error() == noserde::io_error::truncated_payload);
  }

  {
    noserde::Buffer<Example> dst;
    std::vector<std::byte> bad_payload(Example::noserde_size_bytes - 1);
    const auto assign = dst.assign_bytes(bad_payload);
    assert(!assign.has_value());
    assert(assign.error() == noserde::io_error::payload_size_mismatch);
  }

  std::filesystem::remove(good);
  std::filesystem::remove(bad_magic);
  std::filesystem::remove(bad_header);
  std::filesystem::remove(truncated);

  return 0;
}
