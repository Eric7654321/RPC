#pragma once

#include <iostream>
#include <string>
#include <cstdlib>
#include "buffer.h"

inline void expectContent(const Buffer& b, const std::string& expect, int line) {
    std::string actual(b.peek(), b.readableBytes());
    if (actual != expect) {
        std::cerr << "FAIL line " << line
                  << ": expect \"" << expect << "\" got \"" << actual << "\"\n";
        std::exit(1);
    }
}
#define EXPECT_CONTENT(b, s) expectContent((b), (s), __LINE__)

#define EXPECT_EQ(a, b) \
    do { if ((a) != (b)) { \
        std::cerr << "FAIL line " << __LINE__ << ": " #a " = " << (a) \
                  << ", expected " << (b) << "\n"; \
        std::exit(1); \
    } } while (0)
