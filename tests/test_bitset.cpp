// Comprehensive tests for the Bitset utility, covers the bit-parallel
// operations used in WFC propagation. Includes single-word, multi-word,
// and tail-trimming edge cases plus the BitsetView/ConstBitsetView wrappers.

#include "test_helpers.hpp"
#include "wfc/Bitset.hpp"

#include <vector>

using namespace wfc;

namespace {

std::vector<std::size_t> collect_set(const Bitset& b) {
    std::vector<std::size_t> out;
    b.for_each_set([&](std::size_t i) { out.push_back(i); });
    return out;
}

std::vector<std::size_t> collect_set(ConstBitsetView b) {
    std::vector<std::size_t> out;
    b.for_each_set([&](std::size_t i) { out.push_back(i); });
    return out;
}

} // namespace

int main() {
    // === Empty / zero-sized ===
    {
        Bitset b;
        WFC_CHECK_EQ(b.size(), 0u);
        WFC_CHECK_EQ(b.count(), 0u);
        WFC_CHECK(!b.any());
    }

    // === Single-word, basic ===
    {
        Bitset b(20);
        WFC_CHECK_EQ(b.size(), 20u);
        WFC_CHECK_EQ(b.word_count(), 1u);
        WFC_CHECK_EQ(b.count(), 0u);
        WFC_CHECK(!b.any());
        WFC_CHECK_EQ(b.first_set(), 20u); // out-of-range sentinel
        WFC_CHECK(collect_set(b).empty());
    }

    // === Bitset::full at non-multiple-of-64 size, tail trimmed ===
    {
        Bitset b = Bitset::full(70);
        WFC_CHECK_EQ(b.size(), 70u);
        WFC_CHECK_EQ(b.word_count(), 2u);
        WFC_CHECK_EQ(b.count(), 70u);
        WFC_CHECK(b.any());
        WFC_CHECK_EQ(b.first_set(), 0u);
        WFC_CHECK(b.test(0));
        WFC_CHECK(b.test(63));
        WFC_CHECK(b.test(64));
        WFC_CHECK(b.test(69));
        // Tail is trimmed: we don't read beyond size, but the count proves it.
        b.clear(69);
        WFC_CHECK_EQ(b.count(), 69u);
        WFC_CHECK(!b.test(69));
    }

    // === Bitset::full at exactly 64 bits (1 word, no tail) ===
    {
        Bitset b = Bitset::full(64);
        WFC_CHECK_EQ(b.word_count(), 1u);
        WFC_CHECK_EQ(b.count(), 64u);
        WFC_CHECK(b.test(0));
        WFC_CHECK(b.test(63));
    }

    // === Bitset::full at exactly 128 bits (2 words, no tail) ===
    {
        Bitset b = Bitset::full(128);
        WFC_CHECK_EQ(b.word_count(), 2u);
        WFC_CHECK_EQ(b.count(), 128u);
        for (std::size_t i = 0; i < 128; ++i) WFC_CHECK(b.test(i));
    }

    // === Bitset::full at small size (< 64) ===
    {
        Bitset b = Bitset::full(5);
        WFC_CHECK_EQ(b.count(), 5u);
        WFC_CHECK(b.test(0));
        WFC_CHECK(b.test(4));
    }

    // === set/clear/test single-word boundary positions ===
    {
        Bitset b(128);
        b.set(0);   b.set(63);  b.set(64); b.set(127);
        WFC_CHECK(b.test(0));   WFC_CHECK(b.test(63));
        WFC_CHECK(b.test(64));  WFC_CHECK(b.test(127));
        WFC_CHECK(!b.test(1));  WFC_CHECK(!b.test(126));
        WFC_CHECK_EQ(b.count(), 4u);

        b.clear(63);
        WFC_CHECK(!b.test(63));
        WFC_CHECK_EQ(b.count(), 3u);

        b.clear(0); b.clear(64); b.clear(127);
        WFC_CHECK_EQ(b.count(), 0u);
        WFC_CHECK(!b.any());
    }

    // === Multi-word bitset (1024 bits) ===
    {
        const std::size_t N = 1024;
        Bitset b(N);
        WFC_CHECK_EQ(b.word_count(), 16u);
        // Set every 17th bit.
        std::size_t expected = 0;
        for (std::size_t i = 0; i < N; i += 17) { b.set(i); ++expected; }
        WFC_CHECK_EQ(b.count(), expected);
        WFC_CHECK(b.any());
        WFC_CHECK_EQ(b.first_set(), 0u);

        auto seen = collect_set(b);
        WFC_CHECK_EQ(seen.size(), expected);
        for (std::size_t i = 0; i < seen.size(); ++i)
            WFC_CHECK_EQ(seen[i], i * 17);
    }

    // === for_each_set: visits in ascending order, skips empty words ===
    {
        Bitset b(200);
        std::vector<std::size_t> expected = {3, 17, 64, 130, 199};
        for (auto i : expected) b.set(i);

        auto seen = collect_set(b);
        WFC_CHECK_EQ(seen.size(), expected.size());
        for (std::size_t i = 0; i < seen.size(); ++i)
            WFC_CHECK_EQ(seen[i], expected[i]);
    }

    // === for_each_set: completely empty bitset visits nothing ===
    {
        Bitset b(500);
        int count = 0;
        b.for_each_set([&](std::size_t) { ++count; });
        WFC_CHECK_EQ(count, 0);
    }

    // === for_each_set: full bitset visits everything ===
    {
        Bitset b = Bitset::full(64);
        std::size_t expected_idx = 0;
        bool ok = true;
        b.for_each_set([&](std::size_t i) {
            if (i != expected_idx++) ok = false;
        });
        WFC_CHECK(ok);
        WFC_CHECK_EQ(expected_idx, 64u);
    }

    // === and_with: returns false when src is superset ===
    {
        Bitset a = Bitset::full(64);
        Bitset b = Bitset::full(64);
        WFC_CHECK(!a.and_with(b)); // identical, no change
        WFC_CHECK_EQ(a.count(), 64u);
    }

    // === and_with: clearing reports change ===
    {
        Bitset a = Bitset::full(64);
        Bitset c(64);
        c.set(5); c.set(10);
        WFC_CHECK(a.and_with(c)); // clears almost everything
        WFC_CHECK_EQ(a.count(), 2u);
        WFC_CHECK(a.test(5)); WFC_CHECK(a.test(10));
    }

    // === and_with: zero-mask wipes everything ===
    {
        Bitset a = Bitset::full(64);
        Bitset z(64);
        WFC_CHECK(a.and_with(z));
        WFC_CHECK_EQ(a.count(), 0u);
        WFC_CHECK(!a.any());
    }

    // === and_with on multi-word ===
    {
        Bitset a = Bitset::full(200);
        Bitset b(200);
        b.set(0); b.set(64); b.set(128); b.set(199);
        WFC_CHECK(a.and_with(b));
        WFC_CHECK_EQ(a.count(), 4u);
        WFC_CHECK(a.test(0)); WFC_CHECK(a.test(64));
        WFC_CHECK(a.test(128)); WFC_CHECK(a.test(199));
    }

    // === or_with: accumulates ===
    {
        Bitset a(96);
        a.set(1); a.set(50);
        Bitset b(96);
        b.set(50); b.set(80);
        a.or_with(b);
        WFC_CHECK_EQ(a.count(), 3u);
        WFC_CHECK(a.test(1));
        WFC_CHECK(a.test(50));
        WFC_CHECK(a.test(80));
    }

    // === or_with: with empty leaves unchanged ===
    {
        Bitset a(64);
        a.set(7); a.set(40);
        Bitset z(64);
        a.or_with(z);
        WFC_CHECK_EQ(a.count(), 2u);
        WFC_CHECK(a.test(7));  WFC_CHECK(a.test(40));
    }

    // === or_with: all-ones gives all-ones ===
    {
        Bitset a(64);
        a.set(2);
        Bitset f = Bitset::full(64);
        a.or_with(f);
        WFC_CHECK_EQ(a.count(), 64u);
    }

    // === set_only: collapses to single bit ===
    {
        Bitset a(64);
        a.set(2); a.set(5); a.set(40);
        a.set_only(7);
        WFC_CHECK_EQ(a.count(), 1u);
        WFC_CHECK(a.test(7));
        WFC_CHECK_EQ(a.first_set(), 7u);
    }

    // === set_only on multi-word bitset ===
    {
        Bitset a = Bitset::full(200);
        a.set_only(150);
        WFC_CHECK_EQ(a.count(), 1u);
        WFC_CHECK(a.test(150));
        WFC_CHECK(!a.test(0));
        WFC_CHECK(!a.test(151));
        WFC_CHECK_EQ(a.first_set(), 150u);
    }

    // === reset clears every bit ===
    {
        Bitset b = Bitset::full(150);
        b.reset();
        WFC_CHECK_EQ(b.count(), 0u);
        WFC_CHECK(!b.any());
    }

    // === Equality operator ===
    {
        Bitset a(70);
        Bitset b(70);
        WFC_CHECK(a == b); // both empty
        a.set(3);
        WFC_CHECK(!(a == b));
        b.set(3);
        WFC_CHECK(a == b);

        // Different sizes => not equal even with same set bits.
        Bitset c(80);
        c.set(3);
        WFC_CHECK(!(a == c));
    }

    // === ConstBitsetView from Bitset ===
    {
        Bitset b(96);
        b.set(2); b.set(70);
        ConstBitsetView v = b;
        WFC_CHECK_EQ(v.size(), 96u);
        WFC_CHECK_EQ(v.count(), 2u);
        WFC_CHECK(v.test(2));
        WFC_CHECK(v.test(70));
        WFC_CHECK(!v.test(3));
        WFC_CHECK_EQ(v.first_set(), 2u);

        auto seen = collect_set(v);
        WFC_CHECK_EQ(seen.size(), 2u);
        WFC_CHECK_EQ(seen[0], 2u);
        WFC_CHECK_EQ(seen[1], 70u);
    }

    // === BitsetView mutation persists in underlying Bitset ===
    {
        Bitset b(64);
        BitsetView v = b;
        v.set(0); v.set(33); v.set(63);
        WFC_CHECK_EQ(b.count(), 3u);
        WFC_CHECK(b.test(33));
    }

    // === Bitset(ConstBitsetView) snapshot ===
    {
        Bitset original(128);
        original.set(2); original.set(70); original.set(127);

        Bitset snap{ConstBitsetView(original)};
        WFC_CHECK_EQ(snap.size(), 128u);
        WFC_CHECK_EQ(snap.count(), 3u);
        WFC_CHECK(snap.test(2));
        WFC_CHECK(snap.test(70));
        WFC_CHECK(snap.test(127));

        // Mutate snap, verify original unchanged.
        snap.set(50);
        WFC_CHECK(snap.test(50));
        WFC_CHECK(!original.test(50));
    }

    // === count() on multi-word with mixed words ===
    {
        Bitset b(192);
        // word 0: bits 0..63 -> 64 set
        for (int i = 0; i < 64; ++i) b.set(i);
        // word 1: empty
        // word 2: 8 specific bits
        for (int i = 128; i < 136; ++i) b.set(i);
        WFC_CHECK_EQ(b.count(), 72u);
        WFC_CHECK_EQ(b.first_set(), 0u);
    }

    // === first_set on multi-word with low words empty ===
    {
        Bitset b(192);
        b.set(150);
        WFC_CHECK_EQ(b.first_set(), 150u);
    }

    // === for_each_set early iteration order across word boundaries ===
    {
        Bitset b(150);
        b.set(63); b.set(64); b.set(65);
        auto seen = collect_set(b);
        WFC_CHECK_EQ(seen.size(), 3u);
        WFC_CHECK_EQ(seen[0], 63u);
        WFC_CHECK_EQ(seen[1], 64u);
        WFC_CHECK_EQ(seen[2], 65u);
    }

    // === Bitset::full with size > 64 not multiple of 64 trims tail ===
    {
        Bitset b = Bitset::full(100);
        WFC_CHECK_EQ(b.count(), 100u);
        WFC_CHECK_EQ(b.word_count(), 2u);
    }

    // === Test that clearing already-zero bits is harmless ===
    {
        Bitset b(64);
        b.clear(5);
        WFC_CHECK_EQ(b.count(), 0u);
        WFC_CHECK(!b.test(5));
    }

    // === Re-set already-set bit is idempotent ===
    {
        Bitset b(64);
        b.set(10);
        b.set(10);
        WFC_CHECK_EQ(b.count(), 1u);
    }

    return wfc_test::report();
}
