#pragma once

#include "buffer.h"
#include <string>
#include <cstdint>
#include <cstddef>

inline constexpr size_t   kHeaderLen  = 10;
inline constexpr uint32_t kMaxBodyLen = 64 * 1024;

enum class DecodeResult { kSuccess, kNeedMore, kError };

struct Frame {
    uint16_t    type;
    uint32_t    seq;
    std::string body;
};

DecodeResult decodeFrame(Buffer& buf, Frame* out);

void encodeFrame(Buffer* out, uint16_t type, uint32_t seq, const std::string& body);