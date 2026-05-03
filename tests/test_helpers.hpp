#pragma once

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

namespace wfc_test {

inline int g_failures = 0;
inline int g_total = 0;

#define WFC_CHECK(cond)                                                     \
    do {                                                                    \
        ++::wfc_test::g_total;                                              \
        if (!(cond)) {                                                      \
            ++::wfc_test::g_failures;                                       \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__             \
                      << " " #cond "\n";                                    \
        }                                                                   \
    } while (0)

#define WFC_CHECK_EQ(a, b)                                                  \
    do {                                                                    \
        ++::wfc_test::g_total;                                              \
        auto _a = (a); auto _b = (b);                                       \
        if (!(_a == _b)) {                                                  \
            ++::wfc_test::g_failures;                                       \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__             \
                      << " " #a " == " #b ": got " << _a                    \
                      << " expected " << _b << "\n";                        \
        }                                                                   \
    } while (0)

inline int report() {
    int passed = g_total - g_failures;
    std::cerr << passed << "/" << g_total << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}

} // namespace wfc_test
