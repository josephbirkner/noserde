set(NOSERDE_CPM_FILE "${CMAKE_SOURCE_DIR}/cmake/CPM.cmake")
if(NOT EXISTS "${NOSERDE_CPM_FILE}")
  message(FATAL_ERROR
    "Missing cmake/CPM.cmake. Initialize submodules before configuring: \n"
    "  git submodule update --init --recursive"
  )
endif()

include("${NOSERDE_CPM_FILE}")

CPMAddPackage(
  NAME tl_expected
  GITHUB_REPOSITORY TartanLlama/expected
  GIT_TAG v1.2.0
  DOWNLOAD_ONLY YES
)

CPMAddPackage(
  NAME sfl
  GITHUB_REPOSITORY slavenf/sfl-library
  GIT_TAG 2.1.2
  DOWNLOAD_ONLY YES
)

CPMAddPackage(
  NAME bitsery
  GITHUB_REPOSITORY fraillt/bitsery
  GIT_TAG v5.2.3
  DOWNLOAD_ONLY YES
)

set(NOSERDE_DEP_INCLUDE_DIRS "")

set(_tl_expected_include "${tl_expected_SOURCE_DIR}/include")
if(NOT EXISTS "${_tl_expected_include}/tl/expected.hpp")
  message(FATAL_ERROR "tl::expected headers not found at ${_tl_expected_include}")
endif()
list(APPEND NOSERDE_DEP_INCLUDE_DIRS "${_tl_expected_include}")

set(_sfl_include "${sfl_SOURCE_DIR}/include")
if(NOT EXISTS "${_sfl_include}/sfl/segmented_vector.hpp")
  message(FATAL_ERROR "sfl headers not found at ${_sfl_include}")
endif()
list(APPEND NOSERDE_DEP_INCLUDE_DIRS "${_sfl_include}")

set(_bitsery_include "${bitsery_SOURCE_DIR}/include")
if(NOT EXISTS "${_bitsery_include}/bitsery/serializer.h")
  message(FATAL_ERROR "bitsery headers not found at ${_bitsery_include}")
endif()
list(APPEND NOSERDE_DEP_INCLUDE_DIRS "${_bitsery_include}")
