#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace wfc {

namespace detail {

inline std::size_t popcount64(std::uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return static_cast<std::size_t>(__builtin_popcountll(x));
#elif defined(_MSC_VER)
    return static_cast<std::size_t>(__popcnt64(x));
#else
    std::size_t c = 0;
    while (x) { x &= x - 1; ++c; }
    return c;
#endif
}

inline std::size_t ctz64(std::uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return static_cast<std::size_t>(__builtin_ctzll(x));
#elif defined(_MSC_VER)
    unsigned long idx;
    _BitScanForward64(&idx, x);
    return static_cast<std::size_t>(idx);
#else
    std::size_t c = 0;
    while (((x >> c) & 1ULL) == 0) ++c;
    return c;
#endif
}

inline std::size_t words_for(std::size_t nbits) { return (nbits + 63) / 64; }

} // namespace detail

class Bitset; // forward

// Read-only view over a packed bitset stored elsewhere (e.g., the flat
// word buffer owned by Wave). Cheap to copy, never owns memory.
class ConstBitsetView {
public:
    ConstBitsetView() = default;
    ConstBitsetView(const std::uint64_t* words, std::size_t nbits)
        : words_(words), nbits_(nbits) {}

    std::size_t size() const { return nbits_; }
    std::size_t word_count() const { return detail::words_for(nbits_); }
    const std::uint64_t* words() const { return words_; }

    bool test(std::size_t i) const {
        return (words_[i >> 6] >> (i & 63)) & 1ULL;
    }

    std::size_t count() const {
        std::size_t c = 0;
        const std::size_t n = word_count();
        for (std::size_t i = 0; i < n; ++i) c += detail::popcount64(words_[i]);
        return c;
    }

    bool any() const {
        const std::size_t n = word_count();
        for (std::size_t i = 0; i < n; ++i) if (words_[i]) return true;
        return false;
    }

    std::size_t first_set() const {
        const std::size_t n = word_count();
        for (std::size_t i = 0; i < n; ++i)
            if (words_[i]) return i * 64 + detail::ctz64(words_[i]);
        return nbits_;
    }

    template <typename F>
    void for_each_set(F&& f) const {
        const std::size_t n = word_count();
        for (std::size_t w = 0; w < n; ++w) {
            std::uint64_t bits = words_[w];
            const std::size_t base = w * 64;
            while (bits) {
                const std::size_t bit = detail::ctz64(bits);
                f(base + bit);
                bits &= bits - 1;
            }
        }
    }

private:
    const std::uint64_t* words_ = nullptr;
    std::size_t nbits_ = 0;
};

// Mutable view over a packed bitset stored elsewhere. Same interface as
// ConstBitsetView plus the in-place mutators used during WFC propagation.
class BitsetView {
public:
    BitsetView() = default;
    BitsetView(std::uint64_t* words, std::size_t nbits)
        : words_(words), nbits_(nbits) {}

    operator ConstBitsetView() const { return {words_, nbits_}; }

    std::size_t size() const { return nbits_; }
    std::size_t word_count() const { return detail::words_for(nbits_); }
    const std::uint64_t* words() const { return words_; }
    std::uint64_t* words() { return words_; }

    bool test(std::size_t i) const {
        return (words_[i >> 6] >> (i & 63)) & 1ULL;
    }
    void set(std::size_t i)   { words_[i >> 6] |=  (1ULL << (i & 63)); }
    void clear(std::size_t i) { words_[i >> 6] &= ~(1ULL << (i & 63)); }

    std::size_t count() const { return ConstBitsetView(*this).count(); }
    bool any() const          { return ConstBitsetView(*this).any(); }
    std::size_t first_set() const { return ConstBitsetView(*this).first_set(); }

    template <typename F>
    void for_each_set(F&& f) const {
        ConstBitsetView(words_, nbits_).for_each_set(std::forward<F>(f));
    }

    void reset() {
        std::fill(words_, words_ + word_count(), 0ULL);
    }

    void set_only(std::size_t i) {
        reset();
        set(i);
    }

    // In-place intersection. Returns true if any bit was cleared.
    bool and_with(ConstBitsetView other) {
        bool changed = false;
        const std::size_t n = word_count();
        const std::uint64_t* o = other.words();
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint64_t before = words_[i];
            const std::uint64_t after = before & o[i];
            if (after != before) {
                words_[i] = after;
                changed = true;
            }
        }
        return changed;
    }

    void or_with(ConstBitsetView other) {
        const std::size_t n = word_count();
        const std::uint64_t* o = other.words();
        for (std::size_t i = 0; i < n; ++i) words_[i] |= o[i];
    }

private:
    std::uint64_t* words_ = nullptr;
    std::size_t nbits_ = 0;
};

// Owning packed bitset. Used for stand-alone bitsets (rules, scratch
// buffers, snapshots). Wave cells use BitsetView over a contiguous buffer,
// so they pay no per-cell allocation overhead.
class Bitset {
public:
    Bitset() = default;
    explicit Bitset(std::size_t nbits)
        : nbits_(nbits), words_(detail::words_for(nbits), 0ULL) {}

    // Copy construction from any view: useful for snapshots.
    Bitset(ConstBitsetView v)
        : nbits_(v.size()), words_(v.words(), v.words() + v.word_count()) {}

    static Bitset full(std::size_t nbits) {
        Bitset b(nbits);
        std::fill(b.words_.begin(), b.words_.end(), ~0ULL);
        b.trim_tail();
        return b;
    }

    operator ConstBitsetView() const { return {words_.data(), nbits_}; }
    operator BitsetView() { return {words_.data(), nbits_}; }

    std::size_t size() const { return nbits_; }
    std::size_t word_count() const { return words_.size(); }
    const std::uint64_t* words() const { return words_.data(); }
    std::uint64_t* words() { return words_.data(); }

    bool test(std::size_t i) const  { return view().test(i); }
    void set(std::size_t i)         { mut().set(i); }
    void clear(std::size_t i)       { mut().clear(i); }
    std::size_t count() const       { return view().count(); }
    bool any() const                { return view().any(); }
    std::size_t first_set() const   { return view().first_set(); }
    void reset()                    { mut().reset(); }
    void set_only(std::size_t i)    { mut().set_only(i); }

    template <typename F>
    void for_each_set(F&& f) const  { view().for_each_set(std::forward<F>(f)); }

    bool and_with(ConstBitsetView other) { return mut().and_with(other); }
    void or_with(ConstBitsetView other)  { mut().or_with(other); }

    bool operator==(const Bitset& other) const {
        return nbits_ == other.nbits_ && words_ == other.words_;
    }

private:
    ConstBitsetView view() const { return {words_.data(), nbits_}; }
    BitsetView mut()             { return {words_.data(), nbits_}; }

    void trim_tail() {
        if (words_.empty()) return;
        const std::size_t tail = nbits_ & 63;
        if (tail) words_.back() &= (1ULL << tail) - 1ULL;
    }

    std::size_t nbits_ = 0;
    std::vector<std::uint64_t> words_;
};

} // namespace wfc
