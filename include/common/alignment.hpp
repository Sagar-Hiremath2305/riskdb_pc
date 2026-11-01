#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>
#include <memory>
#include <type_traits>

namespace riskdb {

// 64-byte alignment standard across modern x86-64 and ARM64 cache architectures
constexpr size_t CACHE_LINE_SIZE = 64;
constexpr size_t SIMD_ALIGNMENT = 64;

/**
 * @brief Allocates heap memory aligned to specified alignment boundary (default 64 bytes).
 */
inline void* aligned_alloc_bytes(size_t size, size_t alignment = SIMD_ALIGNMENT) {
    void* ptr = nullptr;
#if defined(_MSC_VER)
    ptr = _aligned_malloc(size, alignment);
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__)
    if (posix_memalign(&ptr, alignment, size) != 0) {
        ptr = nullptr;
    }
#else
    ptr = std::aligned_alloc(alignment, size);
#endif
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

/**
 * @brief Deallocates aligned memory.
 */
inline void aligned_free_bytes(void* ptr) noexcept {
    if (!ptr) return;
#if defined(_MSC_VER)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

/**
 * @brief STL-compatible allocator ensuring 64-byte cache alignment for vectors and buffers.
 */
template <typename T, size_t Alignment = SIMD_ALIGNMENT>
class AlignedAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template <typename U>
    struct rebind {
        using other = AlignedAllocator<U, Alignment>;
    };

    AlignedAllocator() noexcept = default;
    template <typename U>
    AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    T* allocate(std::size_t n) {
        if (n == 0) return nullptr;
        if (n > static_cast<std::size_t>(-1) / sizeof(T)) {
            throw std::bad_array_new_length();
        }
        void* const pv = aligned_alloc_bytes(n * sizeof(T), Alignment);
        return static_cast<T*>(pv);
    }

    void deallocate(T* const p, std::size_t) noexcept {
        aligned_free_bytes(p);
    }

    bool operator==(const AlignedAllocator&) const noexcept { return true; }
    bool operator!=(const AlignedAllocator&) const noexcept { return false; }
};

/**
 * @brief Cache-line padded container to avoid False Sharing in multi-threaded reductions.
 */
template <typename T>
struct alignas(CACHE_LINE_SIZE) Padded {
    T value;
    // Padding ensures that adjacent thread elements reside in distinct cache lines
    uint8_t pad[CACHE_LINE_SIZE > sizeof(T) ? (CACHE_LINE_SIZE - (sizeof(T) % CACHE_LINE_SIZE)) : 0];

    Padded() : value{} {}
    explicit Padded(const T& v) : value(v) {}
    Padded(T&& v) : value(std::move(v)) {}

    T& operator*() noexcept { return value; }
    const T& operator*() const noexcept { return value; }
    T* operator->() noexcept { return &value; }
    const T* operator->() const noexcept { return &value; }
};

} // namespace riskdb
