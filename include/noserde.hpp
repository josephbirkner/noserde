#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <bitsery/deserializer.h>
#include <bitsery/serializer.h>
#include <bitsery/traits/vector.h>
#include <sfl/segmented_vector.hpp>
#include <tl/expected.hpp>

namespace noserde {

// Schema-only placeholder types. Generated code does not use these directly,
// but keeping them complete improves editor parsing of source schema headers.
template <typename... TAlternatives>
struct variant {
  std::variant<std::monostate, TAlternatives...> storage{};

  variant() = default;

  template <typename T,
            typename Decayed = std::decay_t<T>,
            typename = std::enable_if_t<(std::is_same_v<Decayed, TAlternatives> || ...)>>
  variant(T&& value) : storage(std::in_place_type<Decayed>, std::forward<T>(value)) {}
};

template <typename... TAlternatives>
struct union_ {
  std::variant<std::monostate, TAlternatives...> storage{};

  union_() = default;

  template <typename T,
            typename Decayed = std::decay_t<T>,
            typename = std::enable_if_t<(std::is_same_v<Decayed, TAlternatives> || ...)>>
  union_(T&& value) : storage(std::in_place_type<Decayed>, std::forward<T>(value)) {}
};

enum class io_error {
  open_failed,
  write_failed,
  read_failed,
  invalid_header,
  schema_mismatch,
  payload_size_mismatch,
  truncated_payload,
};

inline constexpr std::string_view io_error_message(io_error error) {
  switch (error) {
    case io_error::open_failed:
      return "open_failed";
    case io_error::write_failed:
      return "write_failed";
    case io_error::read_failed:
      return "read_failed";
    case io_error::invalid_header:
      return "invalid_header";
    case io_error::schema_mismatch:
      return "schema_mismatch";
    case io_error::payload_size_mismatch:
      return "payload_size_mismatch";
    case io_error::truncated_payload:
      return "truncated_payload";
  }
  return "unknown";
}

template <typename T>
struct always_false : std::false_type {};

template <typename T>
inline constexpr bool always_false_v = always_false<T>::value;

namespace detail {

template <typename UInt>
constexpr UInt byteswap_unsigned(UInt value) {
  static_assert(std::is_unsigned_v<UInt>, "byteswap_unsigned expects unsigned type");
  if constexpr (sizeof(UInt) == 1) {
    return value;
  } else if constexpr (sizeof(UInt) == 2) {
    return static_cast<UInt>(((value & static_cast<UInt>(0x00ffU)) << 8U) |
                             ((value & static_cast<UInt>(0xff00U)) >> 8U));
  } else if constexpr (sizeof(UInt) == 4) {
    return static_cast<UInt>(((value & static_cast<UInt>(0x000000ffUL)) << 24U) |
                             ((value & static_cast<UInt>(0x0000ff00UL)) << 8U) |
                             ((value & static_cast<UInt>(0x00ff0000UL)) >> 8U) |
                             ((value & static_cast<UInt>(0xff000000UL)) >> 24U));
  } else if constexpr (sizeof(UInt) == 8) {
    return static_cast<UInt>(
        ((value & static_cast<UInt>(0x00000000000000ffULL)) << 56U) |
        ((value & static_cast<UInt>(0x000000000000ff00ULL)) << 40U) |
        ((value & static_cast<UInt>(0x0000000000ff0000ULL)) << 24U) |
        ((value & static_cast<UInt>(0x00000000ff000000ULL)) << 8U) |
        ((value & static_cast<UInt>(0x000000ff00000000ULL)) >> 8U) |
        ((value & static_cast<UInt>(0x0000ff0000000000ULL)) >> 24U) |
        ((value & static_cast<UInt>(0x00ff000000000000ULL)) >> 40U) |
        ((value & static_cast<UInt>(0xff00000000000000ULL)) >> 56U));
  } else {
    static_assert(always_false_v<UInt>, "Unsupported integer width for byteswap");
  }
}

template <typename UInt>
constexpr UInt host_to_le(UInt value) {
  static_assert(std::is_unsigned_v<UInt>, "host_to_le expects unsigned type");
  if constexpr (std::endian::native == std::endian::little) {
    return value;
  } else {
    return byteswap_unsigned(value);
  }
}

template <typename UInt>
constexpr UInt le_to_host(UInt value) {
  static_assert(std::is_unsigned_v<UInt>, "le_to_host expects unsigned type");
  if constexpr (std::endian::native == std::endian::little) {
    return value;
  } else {
    return byteswap_unsigned(value);
  }
}

template <typename T>
inline constexpr bool is_native_pod_wire_candidate_v =
    std::is_trivially_copyable_v<std::remove_cv_t<T>> &&
    std::is_standard_layout_v<std::remove_cv_t<T>> &&
    !std::is_same_v<std::remove_cv_t<T>, bool> &&
    !std::is_enum_v<std::remove_cv_t<T>> &&
    !std::is_integral_v<std::remove_cv_t<T>> &&
    !std::is_floating_point_v<std::remove_cv_t<T>>;

constexpr std::uint64_t fnv1a64(std::string_view text) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const char ch : text) {
    hash ^= static_cast<std::uint8_t>(ch);
    hash *= 1099511628211ULL;
  }
  return hash;
}

template <typename T>
consteval std::string_view native_type_signature() {
#if defined(__clang__) || defined(__GNUC__)
  return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
  return __FUNCSIG__;
#else
  return "noserde::detail::native_type_signature";
#endif
}

template <typename T>
consteval std::uint64_t native_type_schema_hash() {
  std::uint64_t hash = fnv1a64(native_type_signature<T>());
  hash ^= static_cast<std::uint64_t>(sizeof(std::remove_cv_t<T>));
  hash *= 1099511628211ULL;
  return hash;
}

template <typename TValue, std::size_t T_PageBytes>
struct segmented_storage_page_elements {
  static_assert(T_PageBytes > 0, "page size must be greater than zero");
  static_assert((T_PageBytes % sizeof(TValue)) == 0, "page size must be a multiple of element size");
  static constexpr std::size_t value = T_PageBytes / sizeof(TValue);
};

template <typename S>
inline constexpr bool is_bitsery_input_archive_v =
    requires(S& archive, bitsery::ReaderError error) {
      archive.adapter().error();
      archive.adapter().error(error);
    };

template <typename S>
void mark_bitsery_invalid_data(S& archive) {
  if constexpr (is_bitsery_input_archive_v<S>) {
    archive.adapter().error(bitsery::ReaderError::InvalidData);
  }
}

template <typename S>
bool read_bitsery_size_prefix_1b(S& archive, std::size_t& out_size) {
  std::uint8_t hb = 0;
  archive.adapter().template readBytes<1>(hb);
  if (archive.adapter().error() != bitsery::ReaderError::NoError) {
    return false;
  }

  if (hb < 0x80U) {
    out_size = hb;
    return true;
  }

  std::uint8_t lb = 0;
  archive.adapter().template readBytes<1>(lb);
  if (archive.adapter().error() != bitsery::ReaderError::NoError) {
    return false;
  }

  if ((hb & 0x40U) != 0U) {
    std::uint16_t lw = 0;
    archive.adapter().template readBytes<2>(lw);
    if (archive.adapter().error() != bitsery::ReaderError::NoError) {
      return false;
    }
    out_size = static_cast<std::size_t>((((hb & 0x3FU) << 8U) | lb) << 16U | lw);
    return true;
  }

  out_size = static_cast<std::size_t>(((hb & 0x7FU) << 8U) | lb);
  return true;
}

}  // namespace detail

inline constexpr std::size_t k_bitsery_max_payload_bytes = 0x3FFFFFFFU;

template <typename T>
T load_le(const std::byte* ptr) {
  static_assert(!std::is_reference_v<T>, "T must not be a reference");

  if constexpr (std::is_same_v<T, bool>) {
    return std::to_integer<std::uint8_t>(*ptr) != 0;
  } else if constexpr (std::is_enum_v<T>) {
    using U = std::underlying_type_t<T>;
    return static_cast<T>(load_le<U>(ptr));
  } else if constexpr (std::is_integral_v<T>) {
    using U = std::make_unsigned_t<T>;
    U value = 0;
    std::memcpy(&value, ptr, sizeof(U));
    value = detail::le_to_host(value);
    T out{};
    std::memcpy(&out, &value, sizeof(T));
    return out;
  } else if constexpr (std::is_floating_point_v<T>) {
    using U = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
    const U bits = load_le<U>(ptr);
    return std::bit_cast<T>(bits);
  } else if constexpr (detail::is_native_pod_wire_candidate_v<T>) {
    static_assert(std::endian::native == std::endian::little,
                  "native POD wire types are currently supported only on little-endian targets");
    std::array<std::byte, sizeof(T)> raw{};
    std::memcpy(raw.data(), ptr, raw.size());
    return std::bit_cast<T>(raw);
  } else {
    static_assert(always_false_v<T>,
                  "load_le supports bool, integral, enum, float/double, and gated native POD types");
  }
}

template <typename T>
void store_le(std::byte* ptr, T value) {
  static_assert(!std::is_reference_v<T>, "T must not be a reference");

  if constexpr (std::is_same_v<T, bool>) {
    *ptr = static_cast<std::byte>(value ? 1U : 0U);
  } else if constexpr (std::is_enum_v<T>) {
    using U = std::underlying_type_t<T>;
    store_le<U>(ptr, static_cast<U>(value));
  } else if constexpr (std::is_integral_v<T>) {
    using U = std::make_unsigned_t<T>;
    U native = 0;
    std::memcpy(&native, &value, sizeof(T));
    U wire = detail::host_to_le(native);
    std::memcpy(ptr, &wire, sizeof(U));
  } else if constexpr (std::is_floating_point_v<T>) {
    using U = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
    const U bits = std::bit_cast<U>(value);
    store_le<U>(ptr, bits);
  } else if constexpr (detail::is_native_pod_wire_candidate_v<T>) {
    static_assert(std::endian::native == std::endian::little,
                  "native POD wire types are currently supported only on little-endian targets");
    const auto raw = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
    std::memcpy(ptr, raw.data(), raw.size());
  } else {
    static_assert(always_false_v<T>,
                  "store_le supports bool, integral, enum, float/double, and gated native POD types");
  }
}

template <typename T, typename = void>
struct record_traits {};

template <typename T>
struct record_traits<T,
                     std::void_t<decltype(T::noserde_size_bytes),
                                 decltype(T::noserde_schema_hash),
                                 typename T::Ref,
                                 typename T::ConstRef>> {
  using ref = typename T::Ref;
  using const_ref = typename T::ConstRef;

  static constexpr std::size_t size_bytes = T::noserde_size_bytes;
  static constexpr std::uint64_t schema_hash = T::noserde_schema_hash;

  static ref make_ref(std::byte* ptr) { return T::make_ref(ptr); }
  static const_ref make_const_ref(const std::byte* ptr) { return T::make_const_ref(ptr); }
};

template <typename T, typename = void>
struct has_record_traits : std::false_type {};

template <typename T>
struct has_record_traits<T,
                         std::void_t<decltype(record_traits<T>::size_bytes),
                                     decltype(record_traits<T>::schema_hash),
                                     typename record_traits<T>::ref,
                                     typename record_traits<T>::const_ref>>
    : std::true_type {};

template <typename T>
inline constexpr bool has_record_traits_v = has_record_traits<T>::value;

template <typename T>
inline constexpr bool is_native_pod_wire_type_v = detail::is_native_pod_wire_candidate_v<T>;

template <typename T, typename = void>
struct record_data_traits {};

template <typename T>
struct record_data_traits<T,
                          std::void_t<typename T::Data,
                                      decltype(T::assign_data(std::declval<typename T::Ref>(),
                                                              std::declval<const typename T::Data&>()))>> {
  using data_type = typename T::Data;

  static void assign(typename T::Ref dst, const data_type& src) { T::assign_data(dst, src); }
};

template <typename T, typename = void>
struct has_record_data_traits : std::false_type {};

template <typename T>
struct has_record_data_traits<T, std::void_t<typename record_data_traits<T>::data_type>>
    : std::true_type {};

template <typename T>
inline constexpr bool has_record_data_traits_v = has_record_data_traits<T>::value;

template <typename T>
constexpr std::size_t record_sizeof() {
  static_assert(has_record_traits_v<T>, "Record type does not provide noserde::record_traits");
  return record_traits<T>::size_bytes;
}

template <typename T>
constexpr std::size_t schema_record_sizeof() {
  using U = std::remove_cv_t<T>;
  if constexpr (has_record_traits_v<U>) {
    return record_traits<U>::size_bytes;
  } else if constexpr (is_native_pod_wire_type_v<U>) {
    static_assert(std::endian::native == std::endian::little,
                  "native POD wire types are currently supported only on little-endian targets");
    return sizeof(U);
  } else {
    static_assert(always_false_v<U>,
                  "schema_record_sizeof requires generated record_traits<T> or gated native POD type");
  }
}

template <typename T>
constexpr std::uint64_t schema_hashof() {
  using U = std::remove_cv_t<T>;
  if constexpr (has_record_traits_v<U>) {
    return record_traits<U>::schema_hash;
  } else if constexpr (is_native_pod_wire_type_v<U>) {
    static_assert(std::endian::native == std::endian::little,
                  "native POD wire types are currently supported only on little-endian targets");
    return detail::native_type_schema_hash<U>();
  } else {
    static_assert(always_false_v<U>, "schema_hashof requires generated record_traits<T> or gated native POD type");
  }
}

template <typename T>
constexpr std::size_t wire_sizeof() {
  using U = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<U, bool>) {
    return 1;
  } else if constexpr (std::is_enum_v<U>) {
    return sizeof(std::underlying_type_t<U>);
  } else if constexpr (std::is_integral_v<U> || std::is_floating_point_v<U>) {
    return sizeof(U);
  } else if constexpr (has_record_traits_v<U>) {
    return record_traits<U>::size_bytes;
  } else if constexpr (is_native_pod_wire_type_v<U>) {
    static_assert(std::endian::native == std::endian::little,
                  "native POD wire types are currently supported only on little-endian targets");
    return sizeof(U);
  } else {
    static_assert(always_false_v<U>, "Unsupported field type for noserde wire layout");
  }
}

constexpr std::size_t max_size() { return 0; }

template <typename First, typename... Rest>
constexpr std::size_t max_size(First first, Rest... rest) {
  std::size_t result = static_cast<std::size_t>(first);
  ((result = result < static_cast<std::size_t>(rest) ? static_cast<std::size_t>(rest) : result), ...);
  return result;
}

inline void zero_bytes(std::byte* ptr, std::size_t count) {
  std::memset(ptr, 0, count);
}

template <typename T>
class scalar_ref {
 public:
  using value_type = T;

  explicit scalar_ref(std::byte* ptr = nullptr) : ptr_(ptr) {}

  scalar_ref& operator=(T value) {
    store_le<T>(ptr_, value);
    return *this;
  }

  scalar_ref& operator=(const scalar_ref& other) {
    *this = static_cast<T>(other);
    return *this;
  }

  operator T() const { return load_le<T>(ptr_); }

  T value() const { return static_cast<T>(*this); }

  bool operator==(T rhs) const { return value() == rhs; }
  bool operator!=(T rhs) const { return value() != rhs; }

 private:
  std::byte* ptr_;
};

template <typename T>
class scalar_cref {
 public:
  using value_type = T;

  explicit scalar_cref(const std::byte* ptr = nullptr) : ptr_(ptr) {}

  operator T() const { return load_le<T>(ptr_); }

  T value() const { return static_cast<T>(*this); }

  bool operator==(T rhs) const { return value() == rhs; }
  bool operator!=(T rhs) const { return value() != rhs; }

 private:
  const std::byte* ptr_;
};

template <typename T>
auto make_record_ref(std::byte* ptr) -> typename record_traits<T>::ref {
  return record_traits<T>::make_ref(ptr);
}

template <typename T>
auto make_record_const_ref(const std::byte* ptr) -> typename record_traits<T>::const_ref {
  return record_traits<T>::make_const_ref(ptr);
}

template <typename TValue, std::size_t T_PageBytes>
struct segmented_byte_storage {
  using type =
      sfl::segmented_vector<TValue, detail::segmented_storage_page_elements<TValue, T_PageBytes>::value>;
};

template <typename TValue, std::size_t T_PageBytes>
struct vector_byte_storage {
  using type = std::vector<TValue>;
};

template <template <typename, std::size_t> typename T_StoragePolicy>
struct is_vector_storage_policy : std::false_type {};

template <>
struct is_vector_storage_policy<vector_byte_storage> : std::true_type {};

template <typename T,
          std::size_t T_RecordsPerPage = 256,
          template <typename, std::size_t> typename T_ByteStoragePolicy = segmented_byte_storage,
          typename Enable = void>
class Buffer;

template <typename T,
          std::size_t T_RecordsPerPage,
          template <typename, std::size_t> typename T_ByteStoragePolicy>
class Buffer<T, T_RecordsPerPage, T_ByteStoragePolicy, std::enable_if_t<has_record_traits_v<T>>> {
  static_assert(record_traits<T>::size_bytes > 0, "record size must be greater than zero");
  static_assert(T_RecordsPerPage > 0, "records per page must be greater than zero");
  static_assert(T_RecordsPerPage <= (std::numeric_limits<std::size_t>::max() / record_traits<T>::size_bytes),
                "records-per-page causes page-size overflow");

 public:
  using value_type = T;
  using ref = typename record_traits<T>::ref;
  using const_ref = typename record_traits<T>::const_ref;

  static constexpr std::size_t kRecordSize = record_traits<T>::size_bytes;
  static constexpr std::uint64_t kSchemaHash = record_traits<T>::schema_hash;
  static constexpr std::size_t kPageBytes = T_RecordsPerPage * kRecordSize;
  static_assert(kPageBytes % kRecordSize == 0, "page size must be a multiple of record size");
  using storage_type = typename T_ByteStoragePolicy<std::uint8_t, kPageBytes>::type;

  static constexpr std::size_t kRecordsPerPage = T_RecordsPerPage;
  static constexpr std::size_t kPageSizeBytes = kPageBytes;

  Buffer() = default;

  [[nodiscard]] std::size_t size() const {
    const std::size_t stride = record_traits<T>::size_bytes;
    return stride == 0 ? 0 : bytes_.size() / stride;
  }

  [[nodiscard]] std::size_t byte_size() const { return bytes_.size(); }

  [[nodiscard]] bool empty() const { return size() == 0; }

  void clear() { bytes_.clear(); }

  ref emplace_back() {
    const std::size_t stride = record_traits<T>::size_bytes;
    const std::size_t old_size = bytes_.size();
    bytes_.resize(old_size + stride);
    for (std::size_t i = old_size; i < old_size + stride; ++i) {
      bytes_[i] = 0U;
    }
    ref out = record_traits<T>::make_ref(byte_ptr(old_size));
    if constexpr (has_record_data_traits_v<T>) {
      typename record_data_traits<T>::data_type defaults{};
      record_data_traits<T>::assign(out, defaults);
    }
    return out;
  }

  template <typename... Args>
  ref emplace(Args&&... args) {
    static_assert(has_record_data_traits_v<T>,
                  "Buffer::emplace requires generated T::Data and T::assign_data support");
    ref out = emplace_back();
    typename record_data_traits<T>::data_type data{std::forward<Args>(args)...};
    record_data_traits<T>::assign(out, data);
    return out;
  }

  ref operator[](std::size_t index) {
    const std::size_t stride = record_traits<T>::size_bytes;
    return record_traits<T>::make_ref(byte_ptr(index * stride));
  }

  const_ref operator[](std::size_t index) const {
    const std::size_t stride = record_traits<T>::size_bytes;
    return record_traits<T>::make_const_ref(byte_ptr(index * stride));
  }

  [[nodiscard]] std::vector<std::byte> bytes() const {
    std::vector<std::byte> out(bytes_.size());
    std::size_t offset = 0;
    while (offset < bytes_.size()) {
      const std::size_t chunk = std::min(kPageBytes, bytes_.size() - offset);
      std::memcpy(out.data() + offset, byte_ptr(offset), chunk);
      offset += chunk;
    }
    return out;
  }

  [[nodiscard]] tl::expected<void, io_error> assign_bytes(std::span<const std::byte> payload) {
    return assign_bytes_impl(payload);
  }

  [[nodiscard]] tl::expected<void, io_error> assign_bytes(std::span<const std::uint8_t> payload) {
    return assign_bytes_impl(payload);
  }

  template <typename T_BitseryInputAdapter>
  bool read_payload_from_bitsery(T_BitseryInputAdapter& adapter, std::size_t payload_size) {
    bytes_.resize(payload_size);
    if (payload_size == 0) {
      return true;
    }

    if constexpr (is_vector_storage_policy<T_ByteStoragePolicy>::value) {
      adapter.template readBuffer<1>(reinterpret_cast<std::uint8_t*>(byte_ptr(0)), payload_size);
      if (adapter.error() != bitsery::ReaderError::NoError) {
        bytes_.clear();
        return false;
      }
      return true;
    } else {
      std::size_t offset = 0;
      while (offset < payload_size) {
        const std::size_t chunk = std::min(kPageBytes, payload_size - offset);
        adapter.template readBuffer<1>(reinterpret_cast<std::uint8_t*>(byte_ptr(offset)), chunk);
        if (adapter.error() != bitsery::ReaderError::NoError) {
          bytes_.clear();
          return false;
        }
        offset += chunk;
      }
      return true;
    }
  }

 private:
  template <typename ByteType>
  [[nodiscard]] tl::expected<void, io_error> assign_bytes_impl(std::span<const ByteType> payload) {
    static_assert(sizeof(ByteType) == 1, "assign_bytes expects 1-byte payload elements");
    const std::size_t stride = record_traits<T>::size_bytes;
    if (stride == 0 || (payload.size() % stride) != 0) {
      return tl::make_unexpected(io_error::payload_size_mismatch);
    }

    bytes_.resize(payload.size());
    if (!payload.empty()) {
      std::size_t offset = 0;
      while (offset < payload.size()) {
        const std::size_t chunk = std::min(kPageBytes, payload.size() - offset);
        std::memcpy(byte_ptr(offset), payload.data() + offset, chunk);
        offset += chunk;
      }
    }
    return {};
  }

  std::byte* byte_ptr(std::size_t offset) {
    return reinterpret_cast<std::byte*>(&bytes_[offset]);
  }

  const std::byte* byte_ptr(std::size_t offset) const {
    return reinterpret_cast<const std::byte*>(&bytes_[offset]);
  }

  storage_type bytes_;
};

template <typename T,
          std::size_t T_RecordsPerPage,
          template <typename, std::size_t> typename T_ByteStoragePolicy>
class Buffer<T, T_RecordsPerPage, T_ByteStoragePolicy, std::enable_if_t<!has_record_traits_v<T>>> {
  static_assert(is_native_pod_wire_type_v<T>,
                "Buffer<T> requires generated noserde::record_traits<T> or a gated native POD type");
  static_assert(std::endian::native == std::endian::little,
                "native POD Buffer<T> is currently supported only on little-endian targets");
  static_assert(std::is_default_constructible_v<T>,
                "native POD Buffer<T> requires default-constructible T for resize/deserialize operations");
  static_assert(T_RecordsPerPage > 0, "records per page must be greater than zero");
  static_assert(T_RecordsPerPage <= (std::numeric_limits<std::size_t>::max() / sizeof(T)),
                "records-per-page causes page-size overflow");

 public:
  using value_type = T;
  using ref = T&;
  using const_ref = const T&;

  static constexpr std::size_t kRecordSize = sizeof(T);
  static constexpr std::uint64_t kSchemaHash = detail::native_type_schema_hash<T>();
  static constexpr std::size_t kPageBytes = T_RecordsPerPage * kRecordSize;
  static_assert(kPageBytes % kRecordSize == 0, "page size must be a multiple of record size");
  using storage_type = typename T_ByteStoragePolicy<T, kPageBytes>::type;

  static constexpr std::size_t kRecordsPerPage = T_RecordsPerPage;
  static constexpr std::size_t kPageSizeBytes = kPageBytes;

  Buffer() = default;

  [[nodiscard]] std::size_t size() const { return values_.size(); }

  [[nodiscard]] std::size_t byte_size() const { return values_.size() * sizeof(T); }

  [[nodiscard]] bool empty() const { return values_.empty(); }

  void clear() { values_.clear(); }

  ref emplace_back() {
    values_.emplace_back();
    return values_.back();
  }

  template <typename... Args>
  ref emplace(Args&&... args) {
    values_.emplace_back(std::forward<Args>(args)...);
    return values_.back();
  }

  ref operator[](std::size_t index) { return values_[index]; }

  const_ref operator[](std::size_t index) const { return values_[index]; }

  [[nodiscard]] std::vector<std::byte> bytes() const {
    std::vector<std::byte> out(byte_size());
    if (out.empty()) {
      return out;
    }

    if constexpr (is_vector_storage_policy<T_ByteStoragePolicy>::value) {
      std::memcpy(out.data(), values_.data(), out.size());
    } else {
      std::size_t offset_records = 0;
      while (offset_records < values_.size()) {
        const std::size_t chunk_records = std::min(kRecordsPerPage, values_.size() - offset_records);
        const std::size_t chunk_bytes = chunk_records * sizeof(T);
        std::memcpy(out.data() + (offset_records * sizeof(T)), value_ptr(offset_records), chunk_bytes);
        offset_records += chunk_records;
      }
    }
    return out;
  }

  [[nodiscard]] tl::expected<void, io_error> assign_bytes(std::span<const std::byte> payload) {
    return assign_bytes_impl(payload);
  }

  [[nodiscard]] tl::expected<void, io_error> assign_bytes(std::span<const std::uint8_t> payload) {
    return assign_bytes_impl(payload);
  }

  template <typename T_BitseryInputAdapter>
  bool read_payload_from_bitsery(T_BitseryInputAdapter& adapter, std::size_t payload_size) {
    if ((payload_size % sizeof(T)) != 0U) {
      values_.clear();
      return false;
    }

    const std::size_t record_count = payload_size / sizeof(T);
    values_.resize(record_count);
    if (payload_size == 0) {
      return true;
    }

    if constexpr (is_vector_storage_policy<T_ByteStoragePolicy>::value) {
      adapter.template readBuffer<1>(reinterpret_cast<std::uint8_t*>(values_.data()), payload_size);
      if (adapter.error() != bitsery::ReaderError::NoError) {
        values_.clear();
        return false;
      }
      return true;
    } else {
      std::size_t offset_records = 0;
      while (offset_records < record_count) {
        const std::size_t chunk_records = std::min(kRecordsPerPage, record_count - offset_records);
        const std::size_t chunk_bytes = chunk_records * sizeof(T);
        adapter.template readBuffer<1>(reinterpret_cast<std::uint8_t*>(value_ptr(offset_records)), chunk_bytes);
        if (adapter.error() != bitsery::ReaderError::NoError) {
          values_.clear();
          return false;
        }
        offset_records += chunk_records;
      }
      return true;
    }
  }

 private:
  template <typename ByteType>
  [[nodiscard]] tl::expected<void, io_error> assign_bytes_impl(std::span<const ByteType> payload) {
    static_assert(sizeof(ByteType) == 1, "assign_bytes expects 1-byte payload elements");
    if ((payload.size() % sizeof(T)) != 0U) {
      return tl::make_unexpected(io_error::payload_size_mismatch);
    }

    const std::size_t record_count = payload.size() / sizeof(T);
    values_.resize(record_count);
    if (payload.empty()) {
      return {};
    }

    if constexpr (is_vector_storage_policy<T_ByteStoragePolicy>::value) {
      std::memcpy(values_.data(), payload.data(), payload.size());
    } else {
      std::size_t offset_records = 0;
      while (offset_records < record_count) {
        const std::size_t chunk_records = std::min(kRecordsPerPage, record_count - offset_records);
        const std::size_t chunk_bytes = chunk_records * sizeof(T);
        std::memcpy(value_ptr(offset_records), payload.data() + (offset_records * sizeof(T)), chunk_bytes);
        offset_records += chunk_records;
      }
    }
    return {};
  }

  T* value_ptr(std::size_t record_index) { return &values_[record_index]; }

  const T* value_ptr(std::size_t record_index) const { return &values_[record_index]; }

  storage_type values_;
};

inline constexpr std::array<char, 8> k_binary_magic = {'N', 'S', 'R', 'D', 'B', 'I', 'N', '1'};
inline constexpr std::size_t k_binary_header_size = 8 + (sizeof(std::uint64_t) * 4);

template <typename T,
          std::size_t T_RecordsPerPage,
          template <typename, std::size_t> typename T_ByteStoragePolicy>
tl::expected<void, io_error> write_binary(const std::filesystem::path& path,
                                          const Buffer<T, T_RecordsPerPage, T_ByteStoragePolicy>& buffer) {
  static_assert(has_record_traits_v<T> || is_native_pod_wire_type_v<T>,
                "write_binary requires generated record_traits<T> or a gated native POD type");

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return tl::make_unexpected(io_error::open_failed);
  }

  std::array<std::byte, k_binary_header_size> header{};
  std::memcpy(header.data(), k_binary_magic.data(), k_binary_magic.size());

  store_le<std::uint64_t>(header.data() + 8, Buffer<T, T_RecordsPerPage, T_ByteStoragePolicy>::kSchemaHash);
  store_le<std::uint64_t>(header.data() + 16,
                          static_cast<std::uint64_t>(Buffer<T, T_RecordsPerPage, T_ByteStoragePolicy>::kRecordSize));
  store_le<std::uint64_t>(header.data() + 24, static_cast<std::uint64_t>(buffer.size()));
  store_le<std::uint64_t>(header.data() + 32, static_cast<std::uint64_t>(buffer.byte_size()));

  out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
  if (!out) {
    return tl::make_unexpected(io_error::write_failed);
  }

  const auto payload = buffer.bytes();
  out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
  if (!out) {
    return tl::make_unexpected(io_error::write_failed);
  }

  return {};
}

template <typename T,
          std::size_t T_RecordsPerPage,
          template <typename, std::size_t> typename T_ByteStoragePolicy>
tl::expected<void, io_error> read_binary(const std::filesystem::path& path,
                                         Buffer<T, T_RecordsPerPage, T_ByteStoragePolicy>& buffer) {
  static_assert(has_record_traits_v<T> || is_native_pod_wire_type_v<T>,
                "read_binary requires generated record_traits<T> or a gated native POD type");

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return tl::make_unexpected(io_error::open_failed);
  }

  std::array<std::byte, k_binary_header_size> header{};
  in.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
  if (!in) {
    return tl::make_unexpected(io_error::read_failed);
  }

  if (std::memcmp(header.data(), k_binary_magic.data(), k_binary_magic.size()) != 0) {
    return tl::make_unexpected(io_error::invalid_header);
  }

  const std::uint64_t schema_hash = load_le<std::uint64_t>(header.data() + 8);
  const std::uint64_t record_size = load_le<std::uint64_t>(header.data() + 16);
  const std::uint64_t record_count = load_le<std::uint64_t>(header.data() + 24);
  const std::uint64_t payload_size = load_le<std::uint64_t>(header.data() + 32);

  if (schema_hash != Buffer<T, T_RecordsPerPage, T_ByteStoragePolicy>::kSchemaHash ||
      record_size != static_cast<std::uint64_t>(Buffer<T, T_RecordsPerPage, T_ByteStoragePolicy>::kRecordSize)) {
    return tl::make_unexpected(io_error::schema_mismatch);
  }

  if (payload_size != record_size * record_count) {
    return tl::make_unexpected(io_error::invalid_header);
  }

  std::vector<std::uint8_t> payload(static_cast<std::size_t>(payload_size));
  if (!payload.empty()) {
    in.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    if (in.gcount() != static_cast<std::streamsize>(payload.size())) {
      return tl::make_unexpected(io_error::truncated_payload);
    }
  }

  return buffer.assign_bytes(payload);
}

}  // namespace noserde

namespace bitsery {

template <typename S,
          typename T,
          std::size_t T_RecordsPerPage,
          template <typename, std::size_t> typename T_ByteStoragePolicy>
void serialize(S& s, noserde::Buffer<T, T_RecordsPerPage, T_ByteStoragePolicy>& buffer) {
  static_assert(noserde::has_record_traits_v<T> || noserde::is_native_pod_wire_type_v<T>,
                "bitsery serialize(Buffer<T>) requires generated record_traits<T> or a gated native POD type");

  std::uint64_t schema_hash = noserde::Buffer<T, T_RecordsPerPage, T_ByteStoragePolicy>::kSchemaHash;
  std::uint64_t record_size =
      static_cast<std::uint64_t>(noserde::Buffer<T, T_RecordsPerPage, T_ByteStoragePolicy>::kRecordSize);
  if constexpr (noserde::detail::is_bitsery_input_archive_v<S>) {
    s.value8b(schema_hash);
    s.value8b(record_size);
    if (s.adapter().error() != bitsery::ReaderError::NoError) {
      buffer.clear();
      return;
    }

    const std::uint64_t expected_schema = noserde::Buffer<T, T_RecordsPerPage, T_ByteStoragePolicy>::kSchemaHash;
    const std::uint64_t expected_record_size =
        static_cast<std::uint64_t>(noserde::Buffer<T, T_RecordsPerPage, T_ByteStoragePolicy>::kRecordSize);
    if (schema_hash != expected_schema || record_size != expected_record_size) {
      buffer.clear();
      noserde::detail::mark_bitsery_invalid_data(s);
      return;
    }

    std::size_t payload_size = 0;
    if (!noserde::detail::read_bitsery_size_prefix_1b(s, payload_size)) {
      buffer.clear();
      return;
    }
    if (payload_size > noserde::k_bitsery_max_payload_bytes ||
        (payload_size % static_cast<std::size_t>(expected_record_size)) != 0U) {
      buffer.clear();
      noserde::detail::mark_bitsery_invalid_data(s);
      return;
    }

    if (!buffer.read_payload_from_bitsery(s.adapter(), payload_size)) {
      buffer.clear();
      return;
    }
  } else {
    std::vector<std::byte> payload{};
    payload = buffer.bytes();
    s.value8b(schema_hash);
    s.value8b(record_size);
    s.container1b(payload, noserde::k_bitsery_max_payload_bytes);
  }
}

}  // namespace bitsery
