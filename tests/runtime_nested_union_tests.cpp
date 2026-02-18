#include "tests/nested_union_schema.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

template <typename U>
concept has_get = requires(U& u) {
  u.template get<Pair>();
};

static_assert(!has_get<Node::payload_variant_ref>, "u.get<Alt>() must not be generated");
static_assert(noserde::has_record_data_traits_v<Node>);
static_assert(noserde::has_record_data_traits_v<Envelope>);

int main() {
  // Record alternative in union + payload zeroing when switching alternatives.
  noserde::Buffer<Node> nodes;
  auto node = nodes.emplace(true, Node::payload_data{std::uint64_t{0xAABBCCDDEEFF0011ULL}}, Mode::U64);
  auto* u64 = node.payload.get_if<std::uint64_t>();
  assert(u64 != nullptr);
  assert(static_cast<std::uint64_t>(*u64) == 0xAABBCCDDEEFF0011ULL);

  node.payload.emplace<Pair>();
  auto* pair = node.payload.get_if<Pair>();
  assert(pair != nullptr);
  assert(static_cast<std::int16_t>(pair->x) == 0);
  assert(static_cast<std::int16_t>(pair->y) == 0);

  const auto node_bytes = nodes.bytes();
  const std::size_t node_payload = Node::__layout::payload_payload_offset;
  assert(std::to_integer<std::uint8_t>(node_bytes[node_payload + 4]) == 0);
  assert(std::to_integer<std::uint8_t>(node_bytes[node_payload + 5]) == 0);
  assert(std::to_integer<std::uint8_t>(node_bytes[node_payload + 6]) == 0);
  assert(std::to_integer<std::uint8_t>(node_bytes[node_payload + 7]) == 0);

  // Nested record + nested union operations with tiny page configuration.
  noserde::Buffer<Envelope, 1> envs;
  {
    Node::Data choice_node_data{};
    choice_node_data.valid = true;
    choice_node_data.mode = Mode::F32;
    choice_node_data.payload = 3.25f;

    auto e = envs.emplace(
        Node::Data{true, Node::payload_data{Pair::Data{11, static_cast<std::int16_t>(-12)}}, Mode::Pair},
        Envelope::choice_data{choice_node_data},
        static_cast<std::uint16_t>(101));
    auto* p = e.node.payload.get_if<Pair>();
    assert(p != nullptr);
    p->x = 11;
    p->y = -12;
  }

  {
    auto e = envs.emplace_back();
    e.tail = 202;
    e.choice.emplace<std::int32_t>(-55);
  }

  {
    auto e = envs.emplace_back();
    e.tail = 303;
    e.choice.emplace<Pair>();
    auto* p = e.choice.get_if<Pair>();
    assert(p != nullptr);
    p->x = -1;
    p->y = 2;
  }

  assert(envs.size() == 3);

  auto r0 = envs[0];
  assert(static_cast<std::uint16_t>(r0.tail) == 101);
  assert(r0.choice.holds_alternative<Node>());

  bool visited_node = false;
  r0.choice.visit([&](auto alt) {
    using Alt = std::decay_t<decltype(alt)>;
    if constexpr (std::is_same_v<Alt, typename noserde::record_traits<Node>::ref>) {
      visited_node = true;
      assert(static_cast<Mode>(alt.mode) == Mode::F32);
      auto* f32 = alt.payload.template get_if<float>();
      assert(f32 != nullptr);
      assert(static_cast<float>(*f32) == 3.25f);
    }
  });
  assert(visited_node);

  auto r1 = envs[1];
  assert(r1.choice.holds_alternative<std::int32_t>());
  auto* i32 = r1.choice.get_if<std::int32_t>();
  assert(i32 != nullptr);
  assert(static_cast<std::int32_t>(*i32) == -55);

  const auto& cenvs = envs;
  auto c2 = cenvs[2];
  assert(c2.choice.holds_alternative<Pair>());
  auto* cpair = c2.choice.get_if<Pair>();
  assert(cpair != nullptr);
  assert(static_cast<std::int16_t>(cpair->x) == -1);
  assert(static_cast<std::int16_t>(cpair->y) == 2);

  const auto env_bytes = envs.bytes();
  assert(env_bytes.size() == 3 * Envelope::noserde_size_bytes);

  // Third record starts at 2 * stride.
  const std::size_t base2 = 2 * Envelope::noserde_size_bytes;
  const std::size_t choice_tag = Envelope::__layout::choice_tag_offset;
  assert(std::to_integer<std::uint8_t>(env_bytes[base2 + choice_tag + 0]) == 0);
  assert(std::to_integer<std::uint8_t>(env_bytes[base2 + choice_tag + 1]) == 0);

  return 0;
}
