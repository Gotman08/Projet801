// Unit tests for the Bitset utility — covers the bit-parallel operations
// used in WFC propagation. The for_each_set fast path was added for the
// solver hot loops, so we exercise its ordering and skip-empty-words
// behavior directly here.

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

} // namespace

int main() {
    // Empty bitset has no bits set.
    {
        Bitset b(20);
        WFC_CHECK_EQ(b.size(), 20u);
        WFC_CHECK_EQ(b.count(), 0u);
        WFC_CHECK(!b.any());
        WFC_CHECK_EQ(b.first_set(), 20u); // out-of-range sentinel
        WFC_CHECK(collect_set(b).empty());
    }

    // Bitset::full sets exactly nbits bits and trims the tail correctly.
    {
        Bitset b = Bitset::full(70);
        WFC_CHECK_EQ(b.count(), 70u);
        WFC_CHECK(b.any());
        WFC_CHECK_EQ(b.first_set(), 0u);
        // Last bit set is at position 69, never beyond.
        b.clear(69);
        WFC_CHECK_EQ(b.count(), 69u);
    }

    // set/clear/test consistency.
    {
        Bitset b(128);
        b.set(0); b.set(63); b.set(64); b.set(127);
        WFC_CHECK(b.test(0)); WFC_CHECK(b.test(63));
        WFC_CHECK(b.test(64)); WFC_CHECK(b.test(127));
        WFC_CHECK(!b.test(1)); WFC_CHECK(!b.test(126));
        WFC_CHECK_EQ(b.count(), 4u);

        b.clear(63);
        WFC_CHECK(!b.test(63));
        WFC_CHECK_EQ(b.count(), 3u);
    }

    // for_each_set visits set bits in ascending order, skipping zero words.
    {
        Bitset b(200);
        std::vector<std::size_t> expected = {3, 17, 64, 130, 199};
        for (auto i : expected) b.set(i);

        auto seen = collect_set(b);
        WFC_CHECK_EQ(seen.size(), expected.size());
        for (std::size_t i = 0; i < seen.size(); ++i)
            WFC_CHECK_EQ(seen[i], expected[i]);
    }

    // and_with reports `changed` correctly:
    //  - returns false when src is a superset of dst (no clearing happens)
    //  - returns true when src clears at least one bit of dst
    {
        Bitset a = Bitset::full(64);
        Bitset b = Bitset::full(64);
        WFC_CHECK(!a.and_with(b)); // identical, no change
        WFC_CHECK_EQ(a.count(), 64u);

        Bitset c(64);
        c.set(5); c.set(10);
        WFC_CHECK(a.and_with(c)); // clears almost everything
        WFC_CHECK_EQ(a.count(), 2u);
        WFC_CHECK(a.test(5)); WFC_CHECK(a.test(10));
    }

    // or_with accumulates without losing bits already set.
    {
        Bitset a(96);
        a.set(1); a.set(50);
        Bitset b(96);
        b.set(50); b.set(80);
        a.or_with(b);
        WFC_CHECK_EQ(a.count(), 3u);
        WFC_CHECK(a.test(1)); WFC_CHECK(a.test(50)); WFC_CHECK(a.test(80));
    }

    // set_only collapses to a single bit deterministically.
    {
        Bitset a(64);
        a.set(2); a.set(5); a.set(40);
        a.set_only(7);
        WFC_CHECK_EQ(a.count(), 1u);
        WFC_CHECK(a.test(7));
        WFC_CHECK_EQ(a.first_set(), 7u);
    }

    return wfc_test::report();
}
