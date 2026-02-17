# noserde Implementation Brief

## 1) Product objective

Implement the initial `noserde` in `josephbirkner/noserde` as a generator-first, no-macro user experience:

* Input: C++ headers containing `[[noserde]]`-tagged structs.
* Output: normal C++ headers with generated layout/runtime wrappers.
* Runtime: deterministic fixed-layout byte storage, very fast dump/load, ergonomic typed access.

No extra tooling beyond CMake + Python stdlib + C++20 compiler.

## 2) Core goals

* Deterministic fixed binary layout for supported schema subset.
* Zero-copy-friendly runtime access over contiguous bytes.
* Constant-time-ish serialization/deserialization in practice (single contiguous write/read + small header).
* Natural API for fields (`r.flag = true;`, `static_cast<bool>(r.flag)`).
* Variant-like API for unions.

## 3) Supported schema subset

Supported inside `[[noserde]] struct ...`:

* `bool` (stored as `u8`)
* fixed-width signed/unsigned integers
* `float`, `double`
* enums
* nested structs
* unions

Out of scope:

* arrays/strings/containers
* pointers/references
* bitfields
* inheritance/templates/method bodies in generated structs

## 4) Parsing model

### 4.1 Parser responsibility

The parser should only do **targeted substitution** of tagged structs.

* Find `[[noserde]] struct Name { ... };` blocks.
* Parse enough of those blocks to build generation metadata.
* Replace each tagged block with generated C++.
* Leave all other text untouched.

### 4.2 Explicit non-responsibilities

The parser **does not need to parse** includes, enums, namespaces, unrelated structs, or other declarations globally.

* `#include`, `enum`, comments, and non-tagged declarations are passthrough.
* If tagged structs reference enum types declared elsewhere, generator uses the spelled type names as-is.

### 4.3 Practical implementation

Implement with tokenizer + brace-depth scanning:

1. Scan file text for attribute token `[[noserde]]`.
2. From each hit, parse only the following struct declaration and balanced body.
3. Build a tagged-struct AST for supported fields.
4. Generate replacement text.
5. Emit original file with substitutions.

This keeps parser complexity low and avoids building a full C++ parser.

## 5) Union API design (std::variant-like)

Generated union wrappers should mirror `std::variant` concepts where practical.

For union field `u` with alternatives (`Alt1`, `Alt2`, ...), generate API such as:

* `u.index()` -> active alternative index (`size_t`)
* `u.holds_alternative<Alt>()` -> bool
* `u.get_if<Alt>()` -> pointer/nullptr style result where feasible
* `u.emplace<Alt>(args...)` -> activate alt and construct/initialize payload
* `u.visit(visitor)` -> calls visitor with active alt representation
* `u.visit(visitor)` -> calls visitor with active alt representation
* ```
  static T& get_or_default_construct(std::variant<Types...>& v) {
      if (!std::holds_alternative<T>(v)) {
          v.template emplace<T>();
      }
      return std::get<T>(v);
  }
  ```
* `u.get<Alt>()` -> NOT possible because no exceptions allowed. 

### 5.1 Error model

We should make use of tl::expected, avoid exceptions.

### 5.2 Notes

* `valueless_by_exception()` is not necessary unless runtime semantics can truly produce it.
* Keep behavior deterministic: switching active alt clears payload bytes before writing.

## 6) Runtime library (`include/noserde.hpp`)

Implement a compact, stable runtime used by generated code:

* layout utilities
* byte buffer + typed views based on `sfl::segmented_vector<uint8_t, T_PageSize>`
* support for extremenly fast (de-)serialization of `noserde::Buffer<MyStruct>` in Bitsery
* record refs and property proxies
* union ref/proxy with variant-like operations
* binary IO helpers (`write_binary`, `read_binary`)

Generated code should be plain C++, no user-authored macro DSL required.

## 7) Generator (`tools/noserde_gen.py`)

### Requirements

* Python stdlib only (`argparse`, `hashlib`, `pathlib`, `re`, etc.)
* CLI:

  * `--in <file.h.noserde>`
  * `--out <file.h>`
  * `--check`
* Clear diagnostics with file and location when possible.

### SHA strategy

Embed at top of generated file:

* source path
* generator version
* digest: `sha256(generator_version + format_version + source_bytes)`

If existing output has same digest, skip rewriting.

## 8) CMake integration

* Configure-time generation is acceptable for v1.
* Discover `*.h.noserde`, run generator for each.
* Fail configure on generator error.
* Add optional check target for CI to verify generated files are current.

## 9) Tests

Add tests for:

* field read/write and proxy conversions
* nested struct behavior
* union variant-like operations (`index`, `holds_alternative`, `get`, `get_if`, `visit`, `emplace`)
* serialization roundtrip
* generator smoke test (input -> generated -> compile)

## 10) README expectations

Document:

* project goals (fixed layout, speed, zero-copy-friendly API)
* supported subset and constraints
* `.h.noserde` workflow
* variant-like union examples
* serialization caveats (ABI/endian compatibility assumptions)
* build/test/codegen instructions

## 11) Acceptance criteria

* Tagged-struct substitution works without global parsing of includes/enums.
* Generated union API is variant-like and tested.
* No external Python deps.
* Build + tests + CI pass.
* Generated headers are stable and only rewritten when content hash changes.

## 12) Endianness policy

`noserde` uses a **canonical little-endian in-memory representation**.

### 12.1 Buffer invariant

All numeric values in the underlying byte buffer are stored in little-endian order:

* signed/unsigned integers
* enum underlying values
* union tag/discriminant values
* floating-point values (`float`, `double`) by IEEE-754 bit pattern in LE
* `bool` stored as `u8` (`0`/`1`), endian-independent

There is no host-endian storage mode.

### 12.2 Fast path requirement

* **Little-endian hosts:** no conversion overhead on load/store.
* **Big-endian hosts:** convert at load/store boundaries while keeping buffer bytes LE.

Implementation should use compile-time branching on host endianness so LE platforms keep the hot path minimal.

### 12.3 Runtime helper contract

Route all numeric I/O through central helpers (or equivalent internal primitives):

* `load_le<T>(const std::byte*)`
* `store_le<T>(std::byte*, T)`

Rules:

* Integral types: byteswap only on big-endian hosts.
* Floating-point: bit-cast to integral, apply LE conversion, bit-cast back.
* Enums: convert via underlying integral type.
* Bool: store/read as `uint8_t` (`0`/`1`).

Use memcpy-safe unaligned access patterns; avoid reinterpret-cast based typed pointer dereference from byte buffers.

### 12.4 Serialization implication

Because memory representation is already canonical LE:

* payload serialization can be a contiguous raw write/read,
* file header numeric fields must also be encoded/decoded in LE.

### 12.5 Union-specific note

Union tag storage is LE and alternative payload reads/writes must use the same LE conversion helpers.

### 12.6 Tests to add

* canonical byte-sequence tests for representative values,
* roundtrip tests across all supported scalar widths + float/double + enums + union tags,
* header encoding tests (LE on disk),
* existing variant-like union API tests remain required.

### 12.7 Acceptance criterion addition

For identical logical values, produced buffer bytes must be identical across platforms, with LE canonical representation guaranteed.

## 13) Dependencies

* Python: none other than standard lib
* C++
  * `cmake/CPM.cmake` as submodule for dependency mgmt
  * in `cmake/deps.cmake`:
    * `tl::expected` for monadic error handling
    * `sfl` for segmented_vector
    * `bitsery` for bitsery support