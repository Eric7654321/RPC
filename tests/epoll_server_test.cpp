#include "epoll_server.h"
#include "check.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {
int connectTo(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("socket failed");
    timeval timeout{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        ::close(fd);
        throw std::runtime_error("connect failed");
    }
    return fd;
}

void sendAll(int fd, const char* bytes, size_t length) {
    while (length > 0) {
        ssize_t sent = send(fd, bytes, length, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) throw std::runtime_error(std::string("send: ") + std::strerror(errno));
        bytes += sent;
        length -= static_cast<size_t>(sent);
    }
}

Frame receiveFrame(int fd, Buffer& incoming) {
    Frame frame;
    for (;;) {
        DecodeResult result = decodeFrame(incoming, &frame);
        if (result == DecodeResult::kSuccess) return frame;
        if (result == DecodeResult::kError) throw std::runtime_error("bad response frame");
        char bytes[4096];
        ssize_t count = recv(fd, bytes, sizeof(bytes), 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("response missing");
        incoming.append(bytes, static_cast<size_t>(count));
    }
}
}

int main() {
    EpollServer server([](const Frame& request) {
        return Frame{2, request.seq, "reply:" + request.body};
    });
    server.listen(0);
    std::thread worker([&server] { server.run(); });

    int fd = connectTo(server.port());
    Buffer encoded;
    encodeFrame(&encoded, 1, 101, "first");
    encodeFrame(&encoded, 1, 202, std::string("a\0b", 3));
    sendAll(fd, encoded.peek(), 5);
    sendAll(fd, encoded.peek() + 5, encoded.readableBytes() - 5);

    Buffer incoming;
    Frame first = receiveFrame(fd, incoming);
    Frame second = receiveFrame(fd, incoming);
    EXPECT_EQ(first.type, 2u);
    EXPECT_EQ(first.seq, 101u);
    EXPECT_EQ(first.body, std::string("reply:first"));
    EXPECT_EQ(second.seq, 202u);
    EXPECT_EQ(second.body, std::string("reply:a\0b", 9));
    ::close(fd);

    // 超長 header 必須讓 transport 關閉連線，不能卡住 event loop。
    int badFd = connectTo(server.port());
    char header[kHeaderLen] = {};
    const uint32_t oversized = htonl(kMaxBodyLen + 1);
    std::memcpy(header + 6, &oversized, sizeof(oversized));
    sendAll(badFd, header, sizeof(header));
    char byte;
    EXPECT_EQ(recv(badFd, &byte, 1, 0), 0);
    ::close(badFd);

    // 壞客戶端不能拖垮 server；新連線仍可處理 request。
    int goodFd = connectTo(server.port());
    Buffer request;
    encodeFrame(&request, 1, 303, "again");
    sendAll(goodFd, request.peek(), request.readableBytes());
    Frame third = receiveFrame(goodFd, incoming);
    EXPECT_EQ(third.seq, 303u);
    EXPECT_EQ(third.body, std::string("reply:again"));
    ::close(goodFd);

    // 對端只關閉寫入方向時，server 仍須先處理已收到的 request 並回應。
    int halfClosedFd = connectTo(server.port());
    Buffer lastRequest;
    encodeFrame(&lastRequest, 1, 404, "half-close");
    sendAll(halfClosedFd, lastRequest.peek(), lastRequest.readableBytes());
    EXPECT_EQ(shutdown(halfClosedFd, SHUT_WR), 0);
    Frame last = receiveFrame(halfClosedFd, incoming);
    EXPECT_EQ(last.seq, 404u);
    EXPECT_EQ(last.body, std::string("reply:half-close"));
    ::close(halfClosedFd);

    server.stop();
    worker.join();
}
