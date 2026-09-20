#include "buffer.h"
#include <algorithm>
#include <cassert>

Buffer::Buffer(size_t initial_size)
    : buffer_(initial_size), readerIndex_(0), writerIndex_(0) {}

size_t Buffer::readableBytes() const {
    return writerIndex_ - readerIndex_;
}

size_t Buffer::writableBytes() const {
    return buffer_.size() - writerIndex_;
}

const char* Buffer::peek() const {
    return buffer_.data() + readerIndex_;
}

void Buffer::retrieve(size_t len) {
    assert(len <= readableBytes());
    if (len < readableBytes()) {
        readerIndex_ += len;
    } else {
        retrieveAll();
    }
}

void Buffer::retrieveAll() {
    readerIndex_ = 0;
    writerIndex_ = 0;
}

void Buffer::append(const char* data, size_t len) {
    ensureWritable(len);
    std::copy(data, data + len, buffer_.begin() + writerIndex_);
    writerIndex_ += len;
}

char* Buffer::beginWrite() {
    return buffer_.data() + writerIndex_;
}

void Buffer::hasWritten(size_t len) {
    assert(len <= writableBytes());
    writerIndex_ += len;
}

void Buffer::ensureWritable(size_t len) {
    if (writableBytes() < len) {
        // 如果空間不夠，搬移或擴容
        if (readerIndex_ + writableBytes() >= len) {
            // 搬移
            std::copy(buffer_.begin() + readerIndex_, buffer_.begin() + writerIndex_, buffer_.begin());
            writerIndex_ -= readerIndex_;
            readerIndex_ = 0;
        } else {
            // 擴容
            buffer_.resize(writerIndex_ + len);
        }
    }
}