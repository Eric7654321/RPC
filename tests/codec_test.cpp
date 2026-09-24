#include "mini_rpc/codec.h"
#include "check.h"
#include <arpa/inet.h>
#include <cstring>
#include <string>

// 驗證 TCP 封包切分、長度防護與二進位 body。

static const char* toStr(DecodeResult result) {
    switch (result) {
        case DecodeResult::kSuccess:
            return "kSuccess";
        case DecodeResult::kNeedMore:
            return "kNeedMore";
        case DecodeResult::kError:
            return "kError";
    }
    return "unknown";
}

// 1. 半包：一個完整 frame 被拆成兩次送達
static void testPartialFrame() {
    Buffer encoded;
    // - encodeFrame 出一個 frame 到暫存 Buffer
    encodeFrame(&encoded, 1, 42, "hello world");
    const size_t totalLen = encoded.readableBytes();

    // - 只把前 5 bytes 餵進 decode 用的 Buffer → 期望 kNeedMore
    Buffer incoming;
    incoming.append(encoded.peek(), 5);

    Frame frame;

    DecodeResult result = decodeFrame(incoming, &frame);

    EXPECT_EQ(
        std::string(toStr(result)),
        std::string("kNeedMore")
    );

    // 資料不完整時，decoder 不可以消耗任何資料
    EXPECT_EQ(incoming.readableBytes(), 5u);
    // Arrange：模擬 TCP 第二次送來剩餘資料
    incoming.append(
        encoded.peek() + 5,
        totalLen - 5
    );
    // Act：再次嘗試解包
    result = decodeFrame(incoming, &frame);
    // Assert：現在應該得到完整 frame
    EXPECT_EQ(
        std::string(toStr(result)),
        std::string("kSuccess")
    );
    EXPECT_EQ(frame.type, 1u);
    EXPECT_EQ(frame.seq, 42u);
    EXPECT_EQ(frame.body, std::string("hello world"));
    // 成功解出 frame 後，該 frame 的資料應全部被消耗
    EXPECT_EQ(incoming.readableBytes(), 0u);
}

// 2. 黏包：兩個完整 frame 一次到達
static void testTwoFramesAtOnce() {
    Buffer incoming;
    encodeFrame(&incoming, 1, 101, "a");
    encodeFrame(&incoming, 2, 202, "second body");

    Frame frame;
    EXPECT_EQ(std::string(toStr(decodeFrame(incoming, &frame))), std::string("kSuccess"));
    EXPECT_EQ(frame.type, 1u);
    EXPECT_EQ(frame.seq, 101u);
    EXPECT_EQ(frame.body, std::string("a"));

    EXPECT_EQ(std::string(toStr(decodeFrame(incoming, &frame))), std::string("kSuccess"));
    EXPECT_EQ(frame.type, 2u);
    EXPECT_EQ(frame.seq, 202u);
    EXPECT_EQ(frame.body, std::string("second body"));
    EXPECT_EQ(incoming.readableBytes(), 0u);
    EXPECT_EQ(std::string(toStr(decodeFrame(incoming, &frame))), std::string("kNeedMore"));
}

// 3. body 超長：惡意或壞掉的封包
static void testOversizedBody() {
    char header[kHeaderLen] = {};
    const uint32_t networkBodyLen = htonl(kMaxBodyLen + 1);
    std::memcpy(header + 6, &networkBodyLen, sizeof(networkBodyLen));

    Buffer incoming;
    incoming.append(header, sizeof(header));
    Frame frame;
    EXPECT_EQ(std::string(toStr(decodeFrame(incoming, &frame))), std::string("kError"));
    // 壞 header 留在 buffer；transport 收到 kError 時應關閉這條連線。
    EXPECT_EQ(incoming.readableBytes(), kHeaderLen);
}

static void testBinaryBody() {
    const std::string body("ab\0cd", 5);
    Buffer incoming;
    encodeFrame(&incoming, 3, 303, body);

    Frame frame;
    EXPECT_EQ(std::string(toStr(decodeFrame(incoming, &frame))), std::string("kSuccess"));
    EXPECT_EQ(frame.type, 3u);
    EXPECT_EQ(frame.seq, 303u);
    EXPECT_EQ(frame.body.size(), 5u);
    EXPECT_EQ(frame.body, body);
    EXPECT_EQ(incoming.readableBytes(), 0u);
}

int main() {
    testPartialFrame();
    testTwoFramesAtOnce();
    testOversizedBody();
    testBinaryBody();
    std::cout << "codec_test: all passed\n";
    return 0;
}
