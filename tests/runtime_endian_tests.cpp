#include <noserde.hpp>

#include <array>
#include <cassert>
#include <cstdint>

enum class Tiny : std::uint16_t {
  A = 0x1234,
};

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

  static_assert(noserde::wire_sizeof<bool>() == 1);
  static_assert(noserde::wire_sizeof<std::uint64_t>() == 8);
  static_assert(noserde::max_size(4, 2, 9, 3) == 9);

  return 0;
}
