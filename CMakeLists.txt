cmake_minimum_required(VERSION 3.20)
project(noserde LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
option(NOSERDE_WITH_GLM "Fetch optional GLM headers and enable native POD integration tests" ON)
include(cmake/deps.cmake)

option(NOSERDE_BUILD_BENCHMARKS "Build noserde benchmarks" ON)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(NOSERDE_GENERATOR "${CMAKE_CURRENT_SOURCE_DIR}/tools/noserde_gen.py")
if(NOT EXISTS "${NOSERDE_GENERATOR}")
  message(FATAL_ERROR "noserde generator not found: ${NOSERDE_GENERATOR}")
endif()

add_custom_target(noserde_check)
add_custom_target(noserde_codegen)
add_dependencies(noserde_check noserde_codegen)
set_property(GLOBAL PROPERTY NOSERDE_REGISTERED_SCHEMAS "")
set(NOSERDE_GENERATED_DIR "${CMAKE_BINARY_DIR}/noserde_generated")

function(noserde_generated_output_for_schema schema_abs out_var)
  file(RELATIVE_PATH schema_rel "${CMAKE_SOURCE_DIR}" "${schema_abs}")
  if(schema_rel MATCHES "^\\.\\.")
    message(FATAL_ERROR "noserde schema must be under source tree: ${schema_abs}")
  endif()
  if(schema_rel MATCHES "\\.noserde$")
    string(REGEX REPLACE "\\.noserde$" "" generated_rel "${schema_rel}")
  else()
    set(generated_rel "${schema_rel}")
  endif()
  set(${out_var} "${NOSERDE_GENERATED_DIR}/${generated_rel}" PARENT_SCOPE)
endfunction()

function(noserde_source_has_noserde_annotation source_abs out_var)
  if(NOT EXISTS "${source_abs}")
    set(${out_var} FALSE PARENT_SCOPE)
    return()
  endif()
  file(READ "${source_abs}" _source_text)
  if(_source_text MATCHES "\\[\\[noserde\\]\\]")
    set(${out_var} TRUE PARENT_SCOPE)
  else()
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(noserde_register_schema_files out_codegen_targets_var)
  set(_codegen_targets "")
  foreach(schema_file IN LISTS ARGN)
    get_filename_component(schema_abs "${schema_file}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT EXISTS "${schema_abs}")
      message(FATAL_ERROR "noserde schema does not exist: ${schema_file}")
    endif()
    if(NOT schema_abs MATCHES "\\.(h|hh|hpp|hxx)(\\.noserde)?$")
      message(FATAL_ERROR "noserde schema must be a header (.h/.hh/.hpp/.hxx, optional .noserde suffix): ${schema_abs}")
    endif()

    get_property(_registered GLOBAL PROPERTY NOSERDE_REGISTERED_SCHEMAS)
    list(FIND _registered "${schema_abs}" _schema_idx)
    if(NOT _schema_idx EQUAL -1)
      string(SHA1 _schema_hash "${schema_abs}")
      string(SUBSTRING "${_schema_hash}" 0 16 _schema_hash_short)
      list(APPEND _codegen_targets "noserde_codegen_${_schema_hash_short}")
      continue()
    endif()
    list(APPEND _registered "${schema_abs}")
    set_property(GLOBAL PROPERTY NOSERDE_REGISTERED_SCHEMAS "${_registered}")

    noserde_generated_output_for_schema("${schema_abs}" generated_file)
    set_source_files_properties("${generated_file}" PROPERTIES GENERATED TRUE)
    get_filename_component(_generated_dir "${generated_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${_generated_dir}")

    execute_process(
      COMMAND "${Python3_EXECUTABLE}" "${NOSERDE_GENERATOR}" --in "${schema_abs}" --out "${generated_file}"
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      RESULT_VARIABLE _bootstrap_result
      OUTPUT_VARIABLE _bootstrap_stdout
      ERROR_VARIABLE _bootstrap_stderr
    )
    if(NOT _bootstrap_result EQUAL 0)
      message(FATAL_ERROR
        "noserde configure-time generation failed for ${schema_abs}\n${_bootstrap_stderr}"
      )
    endif()

    string(SHA1 _schema_hash "${schema_abs}")
    string(SUBSTRING "${_schema_hash}" 0 16 _schema_hash_short)
    set(_schema_codegen_target "noserde_codegen_${_schema_hash_short}")

    add_custom_command(
      OUTPUT "${generated_file}"
      COMMAND "${Python3_EXECUTABLE}" "${NOSERDE_GENERATOR}" --in "${schema_abs}" --out "${generated_file}"
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      DEPENDS "${schema_abs}" "${NOSERDE_GENERATOR}"
      VERBATIM
    )
    add_custom_target("${_schema_codegen_target}" DEPENDS "${generated_file}")
    add_dependencies(noserde_codegen "${_schema_codegen_target}")

    add_custom_command(
      TARGET noserde_check
      COMMAND "${Python3_EXECUTABLE}" "${NOSERDE_GENERATOR}" --in "${schema_abs}" --out "${generated_file}" --check
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      VERBATIM
    )

    list(APPEND _codegen_targets "${_schema_codegen_target}")
  endforeach()

  if(_codegen_targets)
    list(REMOVE_DUPLICATES _codegen_targets)
  endif()
  set(${out_codegen_targets_var} "${_codegen_targets}" PARENT_SCOPE)
endfunction()

function(noserde_register_target_schemas target_name)
  if(NOT TARGET "${target_name}")
    message(FATAL_ERROR "noserde target not found: ${target_name}")
  endif()

  get_target_property(_target_dir "${target_name}" SOURCE_DIR)
  if(NOT _target_dir)
    set(_target_dir "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()

  set(_all_sources "")
  get_target_property(_target_sources "${target_name}" SOURCES)
  if(_target_sources)
    list(APPEND _all_sources ${_target_sources})
  endif()
  get_target_property(_iface_sources "${target_name}" INTERFACE_SOURCES)
  if(_iface_sources)
    list(APPEND _all_sources ${_iface_sources})
  endif()
  if(NOT _all_sources)
    return()
  endif()

  set(_schemas "")
  foreach(src IN LISTS _all_sources)
    if(src MATCHES "^\\$<")
      continue()
    endif()
    if(IS_ABSOLUTE "${src}")
      set(_candidate "${src}")
    else()
      set(_candidate "${_target_dir}/${src}")
    endif()

    if(NOT _candidate MATCHES "\\.(h|hh|hpp|hxx)(\\.noserde)?$")
      continue()
    endif()

    noserde_source_has_noserde_annotation("${_candidate}" _is_noserde_schema)
    if(_is_noserde_schema)
      list(APPEND _schemas "${_candidate}")
    endif()
  endforeach()

  if(_schemas)
    noserde_register_schema_files(_schema_codegen_targets ${_schemas})
    if(_schema_codegen_targets)
      add_dependencies("${target_name}" ${_schema_codegen_targets})
    endif()

    get_target_property(_target_type "${target_name}" TYPE)
    if(_target_type STREQUAL "INTERFACE_LIBRARY")
      target_include_directories("${target_name}" BEFORE INTERFACE "${NOSERDE_GENERATED_DIR}")
    else()
      target_include_directories("${target_name}" BEFORE PRIVATE "${NOSERDE_GENERATED_DIR}")
    endif()
  endif()
endfunction()

function(noserde_target_links_runtime target_name runtime_target out_var)
  set(_all_links "")

  get_target_property(_link_libs "${target_name}" LINK_LIBRARIES)
  if(_link_libs)
    list(APPEND _all_links ${_link_libs})
  endif()

  get_target_property(_iface_link_libs "${target_name}" INTERFACE_LINK_LIBRARIES)
  if(_iface_link_libs)
    list(APPEND _all_links ${_iface_link_libs})
  endif()

  string(JOIN ";" _joined_links ${_all_links})
  if(_joined_links MATCHES "(^|[;:>])${runtime_target}($|[;<])")
    set(${out_var} TRUE PARENT_SCOPE)
  else()
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(noserde_auto_register_linked_targets runtime_target)
  get_property(_all_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  if(NOT _all_targets)
    get_property(_all_targets GLOBAL PROPERTY TARGETS)
  endif()
  foreach(tgt IN LISTS _all_targets)
    if("${tgt}" STREQUAL "${runtime_target}" OR "${tgt}" STREQUAL "noserde_check")
      continue()
    endif()

    if(NOT TARGET "${tgt}")
      continue()
    endif()

    get_target_property(_is_alias "${tgt}" ALIASED_TARGET)
    if(_is_alias)
      continue()
    endif()

    noserde_target_links_runtime("${tgt}" "${runtime_target}" _uses_runtime)
    if(_uses_runtime)
      noserde_register_target_schemas("${tgt}")
    endif()
  endforeach()
endfunction()

function(noserde_enable_auto_registration runtime_target)
  set(NOSERDE_DEFERRED_RUNTIME_TARGET "${runtime_target}" PARENT_SCOPE)
  cmake_language(
    DEFER
    DIRECTORY "${CMAKE_SOURCE_DIR}"
    CALL noserde_auto_register_linked_targets "${NOSERDE_DEFERRED_RUNTIME_TARGET}"
  )
endfunction()

add_library(noserde_runtime INTERFACE)
add_library(noserde::runtime ALIAS noserde_runtime)
target_include_directories(noserde_runtime
  INTERFACE
    ${NOSERDE_DEP_INCLUDE_DIRS}
    "${NOSERDE_GENERATED_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_library(noserde_examples INTERFACE)
target_sources(noserde_examples INTERFACE
  examples/example.h
)
target_link_libraries(noserde_examples INTERFACE noserde_runtime)

if(BUILD_TESTING)
  add_executable(noserde_runtime_tests
    tests/runtime_tests.cpp
    tests/test_schema.h
  )
  target_link_libraries(noserde_runtime_tests PRIVATE noserde_runtime)
  add_test(NAME noserde_runtime_tests COMMAND noserde_runtime_tests)

  add_executable(noserde_runtime_nested_union_tests
    tests/runtime_nested_union_tests.cpp
    tests/nested_union_schema.h
  )
  target_link_libraries(noserde_runtime_nested_union_tests PRIVATE noserde_runtime)
  add_test(NAME noserde_runtime_nested_union_tests COMMAND noserde_runtime_nested_union_tests)

  add_executable(noserde_runtime_endian_tests
    tests/runtime_endian_tests.cpp
  )
  target_link_libraries(noserde_runtime_endian_tests PRIVATE noserde_runtime)
  add_test(NAME noserde_runtime_endian_tests COMMAND noserde_runtime_endian_tests)

  add_executable(noserde_runtime_inline_struct_tests
    tests/runtime_inline_struct_tests.cpp
    tests/inline_struct_schema.h
  )
  target_link_libraries(noserde_runtime_inline_struct_tests PRIVATE noserde_runtime)
  add_test(NAME noserde_runtime_inline_struct_tests COMMAND noserde_runtime_inline_struct_tests)

  add_executable(noserde_runtime_defaults_tests
    tests/runtime_defaults_tests.cpp
    tests/defaults_schema.h
  )
  target_link_libraries(noserde_runtime_defaults_tests PRIVATE noserde_runtime)
  add_test(NAME noserde_runtime_defaults_tests COMMAND noserde_runtime_defaults_tests)

  add_executable(noserde_runtime_io_error_tests
    tests/runtime_io_error_tests.cpp
    tests/test_schema.h
    tests/nested_union_schema.h
  )
  target_link_libraries(noserde_runtime_io_error_tests PRIVATE noserde_runtime)
  add_test(NAME noserde_runtime_io_error_tests COMMAND noserde_runtime_io_error_tests)

  add_executable(noserde_runtime_bitsery_tests
    tests/runtime_bitsery_tests.cpp
    tests/test_schema.h
  )
  target_link_libraries(noserde_runtime_bitsery_tests PRIVATE noserde_runtime)
  add_test(NAME noserde_runtime_bitsery_tests COMMAND noserde_runtime_bitsery_tests)

  if(NOSERDE_WITH_GLM)
    add_executable(noserde_runtime_pod_tests
      tests/runtime_pod_tests.cpp
      tests/pod_schema.h
    )
    target_link_libraries(noserde_runtime_pod_tests PRIVATE noserde_runtime)
    add_test(NAME noserde_runtime_pod_tests COMMAND noserde_runtime_pod_tests)
  endif()

  add_test(
    NAME noserde_generator_tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/generator_tests.py" "${NOSERDE_GENERATOR}" "${CMAKE_CURRENT_SOURCE_DIR}"
  )
endif()

if(NOSERDE_BUILD_BENCHMARKS)
  add_executable(noserde_bitsery_benchmark
    benchmarks/bitsery_buffer_vs_vector.cpp
    tests/test_schema.h
  )
  target_link_libraries(noserde_bitsery_benchmark PRIVATE noserde_runtime)
  if(MSVC)
    target_compile_options(noserde_bitsery_benchmark PRIVATE /O2)
  else()
    target_compile_options(noserde_bitsery_benchmark PRIVATE -O3)
  endif()
endif()

noserde_enable_auto_registration(noserde_runtime)
