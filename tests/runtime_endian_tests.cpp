#include <noserde.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <type_traits>

enum class Tiny : std::uint16_t {
  A = 0x1234,
};

struct PodSample {
  std::uint32_t a;
  float b;
};

static_assert(noserde::is_native_pod_wire_type_v<std::int64_t>);
static_assert(noserde::is_native_pod_wire_type_v<double>);
static_assert(noserde::is_native_pod_wire_type_v<PodSample>);
static_assert(std::is_same_v<decltype(noserde::load_le_ref<PodSample>(std::declval<const std::byte*>())),
                             const PodSample&>);

int main() {
  {
    std::array<std::byte, 1> buf{};
    noserde::store_le<bool>(buf.data(), true);
    assert(std::to_integer<std::uint8_t>(buf[0]) == 1);
    assert(noserde::load_le<bool>(buf.data()));
  }

  {
    std::array<std::byte, 2> buf{};
    noserde::store_le<std::int16_t>(buf.data(), static_cast<std::int16_t>(-2));
    assert(std::to_integer<std::uint8_t>(buf[0]) == 0xFE);
    assert(std::to_integer<std::uint8_t>(buf[1]) == 0xFF);
    assert(noserde::load_le<std::int16_t>(buf.data()) == static_cast<std::int16_t>(-2));
  }

  {
    std::array<std::byte, 4> buf{};
    noserde::store_le<std::uint32_t>(buf.data(), 0x12345678U);
    assert(std::to_integer<std::uint8_t>(buf[0]) == 0x78);
    assert(std::to_integer<std::uint8_t>(buf[1]) == 0x56);
    assert(std::to_integer<std::uint8_t>(buf[2]) == 0x34);
    assert(std::to_integer<std::uint8_t>(buf[3]) == 0x12);
    assert(noserde::load_le<std::uint32_t>(buf.data()) == 0x12345678U);
  }

  {
    std::array<std::byte, 8> buf{};
    noserde::store_le<std::uint64_t>(buf.data(), 0x1122334455667788ULL);
    assert(std::to_integer<std::uint8_t>(buf[0]) == 0x88);
    assert(std::to_integer<std::uint8_t>(buf[1]) == 0x77);
    assert(std::to_integer<std::uint8_t>(buf[2]) == 0x66);
    assert(std::to_integer<std::uint8_t>(buf[3]) == 0x55);
    assert(std::to_integer<std::uint8_t>(buf[4]) == 0x44);
    assert(std::to_integer<std::uint8_t>(buf[5]) == 0x33);
    assert(std::to_integer<std::uint8_t>(buf[6]) == 0x22);
    assert(std::to_integer<std::uint8_t>(buf[7]) == 0x11);
    assert(noserde::load_le<std::uint64_t>(buf.data()) == 0x1122334455667788ULL);
  }

  {
    std::array<std::byte, 4> buf{};
    noserde::store_le<float>(buf.data(), 1.0f);
    assert(std::to_integer<std::uint8_t>(buf[0]) == 0x00);
    assert(std::to_integer<std::uint8_t>(buf[1]) == 0x00);
    assert(std::to_integer<std::uint8_t>(buf[2]) == 0x80);
    assert(std::to_integer<std::uint8_t>(buf[3]) == 0x3F);
    assert(noserde::load_le<float>(buf.data()) == 1.0f);
  }

  {
    std::array<std::byte, 8> buf{};
    noserde::store_le<double>(buf.data(), -2.5);
    assert(std::to_integer<std::uint8_t>(buf[0]) == 0x00);
    assert(std::to_integer<std::uint8_t>(buf[1]) == 0x00);
    assert(std::to_integer<std::uint8_t>(buf[2]) == 0x00);
    assert(std::to_integer<std::uint8_t>(buf[3]) == 0x00);
    assert(std::to_integer<std::uint8_t>(buf[4]) == 0x00);
    assert(std::to_integer<std::uint8_t>(buf[5]) == 0x00);
    assert(std::to_integer<std::uint8_t>(buf[6]) == 0x04);
    assert(std::to_integer<std::uint8_t>(buf[7]) == 0xC0);
    assert(noserde::load_le<double>(buf.data()) == -2.5);
  }

  {
    std::array<std::byte, 2> buf{};
    noserde::store_le<Tiny>(buf.data(), Tiny::A);
    assert(std::to_integer<std::uint8_t>(buf[0]) == 0x34);
    assert(std::to_integer<std::uint8_t>(buf[1]) == 0x12);
    assert(noserde::load_le<Tiny>(buf.data()) == Tiny::A);
  }

  {
    alignas(std::uint64_t) std::array<std::byte, sizeof(std::uint64_t)> buf{};
    noserde::store_le<std::uint64_t>(buf.data(), 0xABCDEF0123456789ULL);
    auto& ref = noserde::load_le_ref<std::uint64_t>(buf.data());
    assert(ref == 0xABCDEF0123456789ULL);
    ref = 0x1020304050607080ULL;
    assert(noserde::load_le<std::uint64_t>(buf.data()) == 0x1020304050607080ULL);
  }

  {
    alignas(PodSample) std::array<std::byte, sizeof(PodSample)> buf{};
    noserde::store_le<PodSample>(buf.data(), PodSample{0xCAFEBABEU, 1.25F});
    const auto& ref = noserde::load_le_ref<PodSample>(buf.data());
    assert(ref.a == 0xCAFEBABEU);
    assert(ref.b == 1.25F);

    auto& mut = noserde::load_le_ref<PodSample>(buf.data());
    mut.a = 0x12345678U;
    mut.b = -3.5F;
    const auto& reread = noserde::load_le_ref<PodSample>(buf.data());
    assert(reread.a == 0x12345678U);
    assert(reread.b == -3.5F);
  }

  {
    alignas(std::uint64_t) std::array<std::byte, sizeof(std::uint64_t)> buf{};
    noserde::scalar_ref<std::uint64_t> sref(buf.data());
    sref = 0x0102030405060708ULL;
    auto& by_ref = sref.ref();
    assert(by_ref == 0x0102030405060708ULL);
    by_ref = 0x8877665544332211ULL;
    assert(static_cast<std::uint64_t>(sref) == 0x8877665544332211ULL);

    const noserde::scalar_cref<std::uint64_t> cref(buf.data());
    const auto& c_by_ref = cref.ref();
    assert(c_by_ref == 0x8877665544332211ULL);
  }

  {
    std::array<std::byte, 9> buf{};
    noserde::store_le<std::uint64_t>(buf.data() + 1, 0x1122334455667788ULL);
    const noserde::scalar_cref<std::uint64_t> unaligned(buf.data() + 1);
    assert(static_cast<std::uint64_t>(unaligned) == 0x1122334455667788ULL);
  }

  {
    noserde::Buffer<std::int64_t, 4> values;
    values.emplace(11);
    values.emplace(-22);
    auto& v = values.emplace_back();
    v = 33;
    assert(values.size() == 3);
    assert(values[0] == 11);
    assert(values[1] == -22);
    assert(values[2] == 33);

    const auto raw = values.bytes();
    noserde::Buffer<std::int64_t, 4> roundtrip;
    const auto assigned = roundtrip.assign_bytes(std::span<const std::byte>(raw.data(), raw.size()));
    assert(assigned.has_value());
    assert(roundtrip.size() == 3);
    assert(roundtrip[0] == 11);
    assert(roundtrip[1] == -22);
    assert(roundtrip[2] == 33);
  }

  static_assert(noserde::wire_sizeof<bool>() == 1);
  static_assert(noserde::wire_sizeof<std::uint64_t>() == 8);
  static_assert(noserde::schema_record_sizeof<std::int64_t>() == sizeof(std::int64_t));
  static_assert(noserde::max_size(4, 2, 9, 3) == 9);

  return 0;
}
