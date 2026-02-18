# noserde

> Deterministic, zero-copy-friendly C++ records.
> Your schema is a header file.

`noserde` is a generator-first binary layout system for a constrained C++20 schema subset. It generates fixed, dense, canonical little-endian wire layouts and typed views over raw bytes.

If you like FlatBuffers/Cap'n Proto for "read without parsing", or you use bitsery and wish "deserialize" was a bulk read, `noserde` lives in that space for **fixed-size records**.

- Write `struct [[noserde]]` in a normal header (no macro DSL)
- Build with CMake: the Python generator emits shadow headers under `<build dir>/noserde_generated/`
- Use `noserde::Buffer<T>` and mutate fields through `T::Ref` / `T::ConstRef`

**The trick:** you keep `#include "schemas/foo.h"` in your code; the build puts the generated include directory before the source tree, so that path resolves to the generated header.

**Status:** experimental. The subset is intentional; expect rough edges.

## Quick Start

### 1) Define a schema

```cpp
// schemas/example.h
#pragma once
#include <cstdint>
#include <noserde.hpp>

enum class Kind : std::uint8_t { Int = 0, Real = 1 };

struct [[noserde]] Example {
  bool flag;
  std::int32_t id;
  noserde::variant<std::int32_t, double> value;
  Kind kind;
};
```

### 2) Attach generation to a target

```cmake
# If this repo is vendored as a subdir:
add_subdirectory(path/to/noserde)

add_executable(app
  src/main.cpp
  schemas/example.h
)
target_link_libraries(app PRIVATE noserde::runtime)
```

Listing the schema header in `target_sources` and linking `noserde::runtime` is enough.

### 3) Write/read records (zero-copy views over canonical bytes)

```cpp
#include <noserde.hpp>
#include "schemas/example.h" // shadowed by <build dir>/noserde_generated/schemas/example.h

noserde::Buffer<Example> buf;

auto r = buf.emplace_back();   // appends one record (zeroed + defaults)
r.flag = true;
r.id = 123;
r.value.emplace<double>(3.5);
r.kind = Kind::Real;

auto r0 = buf[0];              // typed view into the underlying bytes (no copy)
auto id = static_cast<std::int32_t>(r0.id);
(void)id;

auto bytes = buf.bytes();      // contiguous canonical bytes (copy)
(void)bytes;
```

Binary dump/load is a trivial header + payload:

```cpp
noserde::write_binary("example.nsbin", buf);

noserde::Buffer<Example> replay;
noserde::read_binary("example.nsbin", replay);
```

## Feature Tour (Nested Records + variant + union_)

```cpp
// schemas/telemetry.h
#pragma once
#include <cstdint>
#include <noserde.hpp>

enum class Channel : std::uint8_t {
  Temperature = 0,
  Pressure = 1,
};

struct [[noserde]] TelemetryHeader {
  std::uint16_t device_id;
  bool critical;
};

struct [[noserde]] TelemetryPacked {
  std::uint32_t hi;
  std::uint32_t lo;
};

struct [[noserde]] TelemetryFrame {
  std::uint64_t ts_ns;
  TelemetryHeader header;
  noserde::variant<std::int32_t, float, TelemetryPacked> payload;
  noserde::union_<std::uint32_t, float> scratch;
  Channel channel;
};
```

```cpp
#include <noserde.hpp>
#include "schemas/telemetry.h"

noserde::Buffer<TelemetryFrame, 512> frames;

auto f = frames.emplace_back();
f.ts_ns = 1717171717000000000ULL;
f.header.device_id = 42;
f.header.critical = true;
f.channel = Channel::Temperature;

f.payload.emplace<TelemetryPacked>(); // record alternatives: default emplace() in v1
if (auto* packed = f.payload.get_if<TelemetryPacked>()) {
  packed->hi = 0xDEADBEEFU;
  packed->lo = 0x01234567U;
}

f.scratch.emplace<std::uint32_t>(0x3F800000U);
const auto scratch_f = static_cast<float>(f.scratch.as<float>());
(void)scratch_f;
```

## Schema Subset

Inside `struct [[noserde]] ...` blocks:

- `bool` (stored as `u8`)
- fixed-width signed/unsigned integers
- `float`, `double` (IEEE-754 assumed)
- enums (stored as underlying type)
- nested tagged structs
- named inline structs (`struct Name { ... } field;`)
- gated native POD field types (for example `glm::fvec3` on little-endian targets)
- `noserde::variant<T...>` (tagged/discriminated)
- `noserde::union_<T...>` (untagged overlay)
- field default initializers (`field = expr`)

Out of scope:

- arrays/containers/strings
- pointers/references
- bitfields
- inheritance/templates/method bodies in generated structs
- C++ `union` declarations (use `noserde::variant` or `noserde::union_`)

## How It Works

- You author a schema header that contains `struct [[noserde]]` declarations.
- CMake runs `tools/noserde_gen.py` to generate a mirrored header under `<build dir>/noserde_generated/`.
- The generated include directory is added before the source tree, so including your original path picks the generated header.
- The generated types are "wire views": fields are `noserde::scalar_ref<T>` / `scalar_cref<T>` and nested record refs, so reads/writes go straight to the underlying bytes.
- Layout is dense and unaligned-safe: loads/stores use `memcpy`, and bytes are always canonical little-endian.

## Generated API (What Shows Up In Your IDE)

For each tagged struct `T`, the generated header provides:

- `T::__layout` with byte offsets and `size_bytes`
- `T::Data` value type (handy for construction/copy)
- `T::Ref` / `T::ConstRef` views into bytes
- `T::noserde_size_bytes` and `T::noserde_schema_hash`
- `T::assign_data(T::Ref, const T::Data&)`

For each `variant`/`union_` field, it also generates a small proxy type:

- `variant` refs: `index()`, `holds_alternative<T>()`, `get_if<T>()`, `emplace<T>()`, `visit(...)`
- `union_` refs: `as<T>()`, `emplace<T>()`

## Storage Options

`noserde::Buffer<T, RecordsPerPage>` defaults to segmented storage:

- `sfl::segmented_vector<uint8_t, PageBytes>`
- `PageBytes = RecordsPerPage * record_sizeof(T)`

For contiguous `std::vector<uint8_t>` storage:

```cpp
using Flat = noserde::Buffer<Example, 256, noserde::vector_byte_storage>;
```

Native POD fast path (little-endian only):

```cpp
#include <cstdint>
#include <glm/glm.hpp>

noserde::Buffer<glm::fvec3> points;
points.emplace(1.0f, 2.0f, 3.0f); // direct glm::fvec3&, no Ref wrapper construction

noserde::Buffer<std::int64_t> ids;
ids.emplace(42); // arithmetic POD mode is enabled too
```

## Binary Format

`noserde::write_binary` / `noserde::read_binary` uses a tiny file format:

- 8B magic: `NSRDBIN1`
- u64 schema hash
- u64 record size (bytes)
- u64 record count
- u64 payload size (bytes)
- payload bytes (`record_count * record_size`)

All integer fields are little-endian.

## Bitsery Integration

`noserde::Buffer<T>` can be serialized directly with bitsery. The adapter writes:

- u64 schema hash
- u64 record size
- a size-prefixed raw payload byte container

```cpp
#include <noserde.hpp>
#include "schemas/example.h"

#include <bitsery/adapter/buffer.h>
#include <bitsery/deserializer.h>
#include <bitsery/serializer.h>
#include <bitsery/traits/vector.h>

noserde::Buffer<Example> src;
// ... fill src ...

std::vector<std::uint8_t> wire;
const std::size_t written = bitsery::quickSerialization(
    bitsery::OutputBufferAdapter<std::vector<std::uint8_t>>(wire), src);
wire.resize(written);

noserde::Buffer<Example> dst;
const auto [error, completed] = bitsery::quickDeserialization(
    bitsery::InputBufferAdapter<std::vector<std::uint8_t>>(wire.begin(), wire.end()), dst);
```

## Endianness

Buffer bytes are canonical little-endian, and noserde is little-endian only.

- little-endian hosts: zero conversion overhead in the hot path
- big-endian hosts: not supported

For gated native POD types (including arithmetic POD mode and `Buffer<glm::fvec3>`), noserde provides a memcpy-free `load_le_ref<T>()` API on little-endian targets (including WebAssembly targets like `wasm32`).

## Build And Test (This Repo)

Dependencies are mandatory and resolved via `cmake/CPM.cmake` (a submodule):

- `tl::expected`
- `sfl`
- `bitsery`

Optional:

- `glm` (enabled by default with `-DNOSERDE_WITH_GLM=ON`, used for POD integration tests)

Initialize submodules first:

```bash
git submodule update --init --recursive
```

Build and run tests:

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

Example run on this repo (`records=200000`, `iterations=40`):

| Metric | `std::vector<RawExample>` | `noserde::Buffer<Example>` | Speedup |
|---|---:|---:|---:|
| Serialize throughput | 2339.43 MiB/s | 3470.89 MiB/s | 1.48x |
| Deserialize throughput (default segmented storage) | 4387.10 MiB/s | 24353.21 MiB/s | 5.55x |
| Deserialize throughput (`noserde::vector_byte_storage`) | 4387.10 MiB/s | 45525.89 MiB/s | 10.38x |

Results depend on CPU, compiler, and dataset shape. Re-run `noserde_bitsery_benchmark` on your target machine for decision-grade numbers.

## Generator

The generator is `tools/noserde_gen.py` (Python stdlib only):

- `--in <schema header>`
- `--out <file.h>`
- `--check`

Generated headers include:

- source path
- generator version
- format version
- deterministic digest (`sha256(generator_version + format_version + source_bytes)`)

If the digest is unchanged, writes are skipped.

To check that generated files are up to date:

```bash
cmake --build build --target noserde_check
```

## Caveats

- The wire layout is packed (no padding). Access is safe because loads/stores use `memcpy`.
- Gated native POD fields/`Buffer<T>` use native object layout bytes (`sizeof(T)`), so treat them as ABI-coupled unless you control both producer and consumer.
- `noserde::union_` is untagged. Pair it with your own discriminator (often an enum field).
- The schema hash is a guardrail, not a full schema-evolution story (treat format changes as breaking unless you have a plan).

## License

BSD 3-Clause (see `LICENSE`).
