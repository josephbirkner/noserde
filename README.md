# noserde

`noserde` is a generator-first binary-layout system for a constrained C++ schema subset.

## Goals

- deterministic fixed binary layout
- zero-copy-friendly typed access over contiguous bytes
- very fast dump/load (header + contiguous payload)
- no macro DSL in user code

## Supported Schema Subset

Inside `[[noserde]] struct ...` blocks:

- `bool` (stored as `u8`)
- fixed-width signed/unsigned integers
- `float`, `double`
- enums
- nested tagged structs
- unions
- named nested structs (`struct Name { ... } field;`) inside tagged structs/unions

Out of scope:

- arrays/containers/strings
- pointers/references
- bitfields
- inheritance/templates/method bodies in generated structs
- anonymous unions (`union { ... } field;`)

## Workflow

1. Author a source header with `.h.noserde` suffix.
2. Tag target structs with `[[noserde]]`.
3. Add the `.h.noserde` file to a target's source list and link that target with `noserde_runtime` (or `noserde::runtime`).
4. Run CMake configure. The generator creates mirrored `.h` files under `build/noserde_generated/`.
5. Include the generated `.h` in your code.

## Dependencies

Dependencies are mandatory and resolved via `cmake/CPM.cmake`:

- `tl::expected`
- `sfl`
- `bitsery`

If submodules are not initialized yet:

```bash
git submodule update --init --recursive
```

## Generator

`tools/noserde_gen.py` (Python stdlib only):

- `--in <file.h.noserde>`
- `--out <file.h>`
- `--check`

Generated headers include:

- source path
- generator version
- format version
- deterministic digest (`sha256(generator_version + format_version + source_bytes)`)

If digest is unchanged, writes are skipped.

## Runtime API

`include/noserde.hpp` provides:

- little-endian canonical scalar load/store helpers
- field proxy types
- `noserde::Buffer<T>`
- generated `T::Data` and `noserde::record_data_traits<T>`
- union proxy API (`index`, `holds_alternative<T>()`, `get_if<T>()`, `emplace<T>(...)`, `visit`)
- `Buffer::emplace(...)` (construct from `T::Data`-shape arguments)
- binary I/O (`write_binary`, `read_binary`)
- bitsery integration for `noserde::Buffer<T>` via standard `bitsery::quickSerialization` / `bitsery::quickDeserialization`

`noserde::Buffer<T, RecordsPerPage>` defaults to
`sfl::segmented_vector<uint8_t, PageBytes>` storage, where
`PageBytes = RecordsPerPage * record_sizeof(T)`.

For contiguous `std::vector<uint8_t>` storage, use:

```cpp
using FlatBuffer = noserde::Buffer<MyRecord, 256, noserde::vector_byte_storage>;
```

Named unions expose a nested type scope for inline alternatives, e.g. `MyStruct::MyUnion::MyNestedStruct`.

`u.get<T>()` is intentionally not generated.

## Flashy Usage Examples

### 1) Define a schema with nested named struct + union alternatives

```cpp
// schemas/telemetry.h.noserde
#pragma once
#include <cstdint>

enum class Channel : std::uint8_t {
  Temperature = 0,
  Pressure = 1,
};

[[noserde]] struct TelemetryHeader {
  std::uint16_t device_id;
  bool critical;
};

[[noserde]] struct TelemetryFrame {
  std::uint64_t ts_ns;
  TelemetryHeader header;

  union Payload {
    float as_f32;
    std::int32_t as_i32;
    struct Packed {
      std::uint32_t hi;
      std::uint32_t lo;
    } packed;
  } payload;

  Channel channel;
};
```

### 2) Attach schema generation to your target (automatic)

```cmake
add_executable(telemetry_app
  src/main.cpp
  schemas/telemetry.h.noserde
)
target_link_libraries(telemetry_app PRIVATE noserde_runtime)
```

Linking `noserde_runtime` + listing `.h.noserde` in target sources is enough.

### 3) Hot-path record writes and type-based union access

```cpp
#include <noserde.hpp>
#include "schemas/telemetry.h" // generated in build/noserde_generated/

noserde::Buffer<TelemetryFrame, 512> frames;

auto f = frames.emplace(
    1717171717000000000ULL,
    TelemetryHeader::Data{42, true},
    TelemetryFrame::payload_data{std::int32_t{17}},
    Channel::Temperature);

f.payload.emplace<TelemetryFrame::Payload::Packed>();
if (auto* packed = f.payload.get_if<TelemetryFrame::Payload::Packed>()) {
  packed->hi = 0xDEADBEEFU;
  packed->lo = 0x01234567U;
}

f.payload.emplace<float>(42.25f);
if (f.payload.holds_alternative<float>()) {
  const float c = static_cast<float>(*f.payload.get_if<float>());
  (void)c;
}
```

You can also pass a full data object:

```cpp
TelemetryFrame::Data seed{};
seed.ts_ns = 1717171718000000000ULL;
seed.header = TelemetryHeader::Data{7, false};
seed.payload = TelemetryFrame::Payload::Packed::Data{0x11111111U, 0x22222222U};
seed.channel = Channel::Pressure;
frames.emplace(seed);
```

### 4) Persist/reload, or decode at top speed

```cpp
#include <bitsery/adapter/buffer.h>
#include <bitsery/deserializer.h>
#include <bitsery/serializer.h>
#include <bitsery/traits/vector.h>

// Binary file round-trip (header + payload)
auto w = noserde::write_binary("frames.nsbin", frames);
if (!w) { /* handle io_error */ }

noserde::Buffer<TelemetryFrame> replay;
auto r = noserde::read_binary("frames.nsbin", replay);
if (!r) { /* handle io_error */ }

// Fast bitsery decode target (contiguous std::vector<uint8_t> storage)
using FastFrames = noserde::Buffer<TelemetryFrame, 512, noserde::vector_byte_storage>;
std::vector<std::uint8_t> wire;

const std::size_t written = bitsery::quickSerialization(
    bitsery::OutputBufferAdapter<std::vector<std::uint8_t>>(wire), frames);
wire.resize(written);

FastFrames decoded;
const auto [error, completed] = bitsery::quickDeserialization(
    bitsery::InputBufferAdapter<std::vector<std::uint8_t>>(wire.begin(), wire.end()), decoded);
if (error != bitsery::ReaderError::NoError || !completed) {
  // handle decode failure
}

// If you already have a raw payload buffer:
std::vector<std::uint8_t> raw_payload = {/* must be N * record_sizeof(TelemetryFrame) bytes */};
auto assigned = decoded.assign_bytes(raw_payload);
if (!assigned) { /* payload_size_mismatch */ }
```

## Bitsery Usage

`noserde::Buffer<T>` can be serialized directly with bitsery.

```cpp
#include <noserde.hpp>
#include <bitsery/adapter/buffer.h>
#include <bitsery/serializer.h>
#include <bitsery/deserializer.h>
#include <bitsery/traits/vector.h>

std::vector<std::uint8_t> wire;
noserde::Buffer<Example> src;
// ... fill src ...

const std::size_t written = bitsery::quickSerialization(
    bitsery::OutputBufferAdapter<std::vector<std::uint8_t>>(wire), src);
wire.resize(written);

noserde::Buffer<Example> dst;
const auto [error, completed] = bitsery::quickDeserialization(
    bitsery::InputBufferAdapter<std::vector<std::uint8_t>>(wire.begin(), wire.end()), dst);
```

## Endianness

Buffer bytes are always canonical little-endian.

- little-endian hosts: no conversion overhead in hot path
- big-endian hosts: conversion at load/store boundaries

This applies to ints, floats, enums, and union tag values.

## Build and Test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Benchmark

Build and run:

```bash
cmake --build build --target noserde_bitsery_benchmark
./build/noserde_bitsery_benchmark [records] [iterations]
```

Latest run on this repo (`records=200000`, `iterations=40`):

| Metric | `std::vector<RawStruct>` | `noserde::Buffer<Example>` | Speedup |
|---|---:|---:|---:|
| Serialize throughput | 2339.43 MiB/s | 3470.89 MiB/s | 1.48x |
| Deserialize throughput (default segmented storage) | 4387.10 MiB/s | 24353.21 MiB/s | 5.55x |
| Deserialize throughput (`noserde::vector_byte_storage`) | 4387.10 MiB/s | 45525.89 MiB/s | 10.38x |

Deserialize scaling (average of 3 runs per point):

| Records | Iterations | `std::vector` deserialize MiB/s | `noserde` segmented MiB/s | `noserde` vector MiB/s | Segmented speedup | Vector speedup |
|---:|---:|---:|---:|---:|---:|---:|
| 100,000 | 40 | 4055.34 | 28094.00 | 38907.57 | 7.09x | 9.79x |
| 200,000 | 20 | 3952.64 | 19476.94 | 28228.94 | 4.96x | 7.20x |
| 400,000 | 12 | 2814.16 | 8222.46 | 14248.87 | 2.99x | 5.10x |
| 800,000 | 8 | 3407.91 | 8062.28 | 13112.06 | 2.54x | 4.07x |
| 1,200,000 | 6 | 4599.85 | 7817.07 | 9958.36 | 1.76x | 2.25x |
| 1,600,000 | 5 | 3150.61 | 7792.46 | 9944.64 | 2.59x | 3.31x |

Trend: absolute throughput falls for all approaches as payload grows, but `noserde` stays ahead and `noserde::vector_byte_storage` remains the fastest option in this benchmark.

Results depend on CPU, compiler, and dataset shape. Re-run `noserde_bitsery_benchmark` on your target machine for decision-grade numbers.

Serialized blob sizes in that run:

- `std::vector<RawStruct>`: `5,000,004` bytes
- `noserde::Buffer<Example>`: `4,200,020` bytes

## Check Generated Files

```bash
cmake --build build --target noserde_check
```

## Serialization Caveats

- binary payload is canonical LE bytes
- binary header fields are also LE
- schema hash and record size are validated on load
- ABI assumptions are restricted to supported scalar widths and IEEE-754 for float/double
