#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <noserde.hpp>

struct [[noserde]] PodEnvelope {
  glm::fvec3 point;
  noserde::variant<glm::fvec3, std::uint32_t> tagged;
  noserde::union_<glm::fvec3, std::uint32_t> raw;
};
