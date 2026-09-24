#include "mini_rpc/buffer.h"
#include <cstring>
#include <iostream>
#include <ostream>
#include "check.h"

int main(){
    Buffer buffer;

    buffer.append("Hello, ", 7);
    const char* init = buffer.peek(); // 指向 "Hello, "

    std::cout << "Initial buffer content: " << std::string(init, buffer.readableBytes()) << std::endl;

    buffer.retrieve(2); // 消化掉 "He"
    std::cout << "After retrieving 2 bytes: " << std::string(buffer.peek(), buffer.readableBytes()) << std::endl;

    buffer.append("World!", 6); // 追加 "World!"
    std::cout << "After appending 'World!': " << std::string(buffer.peek(), buffer.readableBytes()) << std::endl;

    buffer.retrieveAll(); // 消化掉所有內容
    std::cout << "After retrieving all: " << buffer.readableBytes() << " bytes readable." << std::endl;

    buffer.append("3.1415926535897932384626433812795028841971693993791058742302", 60);
    std::cout << "After appending '3.1415926535897932384626433812795028841971693993791058742302': " << std::string(buffer.peek(), buffer.readableBytes()) << std::endl;

    Buffer b(8);
    b.append("ABCDEFGH", 8);
    EXPECT_EQ(b.writableBytes(), 0u);
    b.retrieve(6);                    // readable = "GH", prependable = 6
    b.append("12345", 5);             // writable=0, prependable=6 >= 5 → 走搬移
    EXPECT_CONTENT(b, "GH12345");

    Buffer b2(8);
    b2.append("ABCDEFGH", 8);
    b2.append("12345", 5);             // prependable=0，不夠搬 → 走 resize
    EXPECT_CONTENT(b2, "ABCDEFGH12345");
    EXPECT_EQ(b2.readableBytes(), 13u);

    Buffer b3(8);
    b3.append("ABCDEFGH", 8);
    EXPECT_EQ(b3.writableBytes(), 0u);
    b3.retrieveAll();
    EXPECT_EQ(b3.writableBytes(), 8u);   // 沒歸零的話這裡會是 0

    Buffer b4(16);
    b4.ensureWritable(8);
    memcpy(b4.beginWrite(), "ABCDEFGH", 8);   // 假裝這是 read() 寫進去的
    b4.hasWritten(8);
    EXPECT_CONTENT(b4, "ABCDEFGH");

    Buffer b5(8);
    b5.append("ABCDEFGH", 8);
    EXPECT_EQ(b5.writableBytes(), 0u);
    b5.ensureWritable(1);
    EXPECT_EQ(b5.writableBytes() >= 1u, true);

    std::cout << "all tests passed\n";
    return 0;
}
