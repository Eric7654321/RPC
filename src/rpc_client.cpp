#include "rpc_client.h"

#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

RpcRemoteError::RpcRemoteError(std::string code, std::string message)
    : std::runtime_error(code + (message.empty() ? "" : ": " + message)), code_(std::move(code)) {}

RpcClient::RpcClient(const std::string& ipv4, uint16_t port) {
    fd_ = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0) throw std::runtime_error(std::string("socket: ") + std::strerror(errno));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, ipv4.c_str(), &address.sin_addr) != 1 ||
        connect(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        const int savedErrno = errno;
        ::close(fd_);
        throw std::runtime_error(std::string("connect: ") + std::strerror(savedErrno));
    }
    reader_ = std::thread([this] { readLoop(); });
}

RpcClient::~RpcClient() {
    shutdown(fd_, SHUT_RDWR);
    if (reader_.joinable()) reader_.join();
    ::close(fd_);
}

Frame RpcClient::exchange(const std::string& body, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    auto promise = std::make_shared<std::promise<Frame>>();
    std::future<Frame> future = promise->get_future();
    uint32_t seq;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        if (closed_) throw std::runtime_error("connection closed");
        // 找尚未被 in-flight request 使用的 ID；逾時後的舊回應會被丟棄。
        do {
            seq = nextSeq_++;
        } while (seq == 0 || pending_.count(seq) != 0);
        pending_.emplace(seq, promise);
    }

    Buffer encoded;
    try {
        encodeFrame(&encoded, 1, seq, body);
    } catch (...) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending_.erase(seq);
        throw;
    }

    try {
        std::lock_guard<std::mutex> lock(sendMutex_);
        while (encoded.readableBytes() > 0) {
            const auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::steady_clock::duration::zero()) {
                throw RpcTimeout("RPC send timed out");
            }
            ssize_t sent = send(fd_, encoded.peek(), encoded.readableBytes(), MSG_NOSIGNAL | MSG_DONTWAIT);
            if (sent > 0) {
                encoded.retrieve(static_cast<size_t>(sent));
                continue;
            }
            if (sent < 0 && errno == EINTR) continue;
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
                int waitMs = static_cast<int>(std::min<int64_t>(
                    std::max<int64_t>(1, wait.count()), std::numeric_limits<int>::max()));
                pollfd writable{fd_, POLLOUT, 0};
                int ready = poll(&writable, 1, waitMs);
                if (ready > 0) continue;
                if (ready == 0) throw RpcTimeout("RPC send timed out");
                if (errno == EINTR) continue;
                throw std::runtime_error(std::string("poll send: ") + std::strerror(errno));
            }
            throw std::runtime_error(std::string("send: ") + std::strerror(errno));
        }
    } catch (...) {
        // 可能已送出 frame 的前半段；這條 byte stream 不可再承載下一筆 request。
        shutdown(fd_, SHUT_RDWR);
        failAll("connection closed during send");
        throw;
    }

    if (future.wait_until(deadline) != std::future_status::ready) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending_.erase(seq);
        throw RpcTimeout("RPC call timed out");
    }
    return future.get();
}

void RpcClient::readLoop() {
    Buffer incoming;
    char bytes[8192];
    for (;;) {
        ssize_t count = recv(fd_, bytes, sizeof(bytes), 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        incoming.append(bytes, static_cast<size_t>(count));
        for (;;) {
            Frame frame;
            DecodeResult result = decodeFrame(incoming, &frame);
            if (result == DecodeResult::kNeedMore) break;
            if (result == DecodeResult::kError) {
                failAll("bad frame from server");
                return;
            }
            std::shared_ptr<std::promise<Frame>> promise;
            {
                std::lock_guard<std::mutex> lock(pendingMutex_);
                auto it = pending_.find(frame.seq);
                if (it != pending_.end()) {
                    promise = it->second;
                    pending_.erase(it);
                }
            }
            if (promise) promise->set_value(std::move(frame));
        }
    }
    failAll("connection closed before response");
}

void RpcClient::failAll(const std::string& reason) {
    std::unordered_map<uint32_t, std::shared_ptr<std::promise<Frame>>> pending;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        closed_ = true;
        pending.swap(pending_);
    }
    for (auto& entry : pending) {
        entry.second->set_exception(std::make_exception_ptr(std::runtime_error(reason)));
    }
}
