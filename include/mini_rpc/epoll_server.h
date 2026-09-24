#pragma once

#include "mini_rpc/codec.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

// 單執行緒、level-triggered epoll 的 framed TCP server。
// handler 在 event loop 執行；stop() 可由另一個執行緒呼叫。
class EpollServer {
public:
    using Handler = std::function<Frame(const Frame&)>;

    explicit EpollServer(Handler handler);
    ~EpollServer();
    EpollServer(const EpollServer&) = delete;
    EpollServer& operator=(const EpollServer&) = delete;

    void listen(uint16_t port);
    uint16_t port() const;
    void run();
    void stop();

private:
    struct Connection {
        Buffer input;
        Buffer output;
        bool peerClosed = false;
    };

    void acceptReady();
    void connectionReady(int fd, uint32_t events);
    bool readFrames(int fd, Connection& connection);
    bool flushOutput(int fd, Connection& connection);
    void updateInterest(int fd, const Connection& connection);
    void closeConnection(int fd);

    Handler handler_;
    int listenFd_ = -1;
    int epollFd_ = -1;
    int wakeFd_ = -1;
    std::atomic<bool> stopping_{false};
    std::unordered_map<int, std::unique_ptr<Connection>> connections_;
};
