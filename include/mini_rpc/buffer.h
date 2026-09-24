#pragma once

#include <cstddef>
#include <vector>

class Buffer {
public:
    explicit Buffer(size_t initial_size = 1024);

    size_t readableBytes() const;   // writerIndex_ - readerIndex_
    size_t writableBytes() const;   // buffer_.size() - writerIndex_

    const char* peek() const;       // 指向 readable 區開頭，不移動索引

    void retrieve(size_t len);      // 消化掉 len bytes（只動索引）
    void retrieveAll();

    void append(const char* data, size_t len);

    char* beginWrite();             // 指向 writable 區開頭
    void hasWritten(size_t len);    // read() 完之後告訴 buffer 寫了多少

    void ensureWritable(size_t len);  // 空間不夠時：搬移 or 擴容

private:

    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};