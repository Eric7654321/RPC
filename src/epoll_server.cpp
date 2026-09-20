#include "epoll_server.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <array>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>

namespace {
constexpr size_t kMaxQueuedOutput = 1024 * 1024;

void checkSystem(int result, const char* operation) {
    if (result < 0) throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}
}

EpollServer::EpollServer(Handler handler) : handler_(std::move(handler)) {
    epollFd_ = epoll_create1(EPOLL_CLOEXEC);
    checkSystem(epollFd_, "epoll_create1");
    wakeFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeFd_ < 0) {
        ::close(epollFd_);
        throw std::runtime_error(std::string("eventfd: ") + std::strerror(errno));
    }
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = wakeFd_;
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, wakeFd_, &event) < 0) {
        const int savedErrno = errno;
        ::close(wakeFd_);
        ::close(epollFd_);
        throw std::runtime_error(std::string("epoll_ctl wake: ") + std::strerror(savedErrno));
    }
}

EpollServer::~EpollServer() {
    for (const auto& entry : connections_) ::close(entry.first);
    if (listenFd_ >= 0) ::close(listenFd_);
    if (wakeFd_ >= 0) ::close(wakeFd_);
    if (epollFd_ >= 0) ::close(epollFd_);
}

void EpollServer::listen(uint16_t requestedPort) {
    if (listenFd_ >= 0) throw std::logic_error("already listening");
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    checkSystem(fd, "socket");
    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        ::close(fd);
        checkSystem(-1, "setsockopt");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(requestedPort);
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || ::listen(fd, 128) < 0) {
        const int savedErrno = errno;
        ::close(fd);
        throw std::runtime_error(std::string("bind/listen: ") + std::strerror(savedErrno));
    }
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = fd;
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event) < 0) {
        const int savedErrno = errno;
        ::close(fd);
        throw std::runtime_error(std::string("epoll_ctl listen: ") + std::strerror(savedErrno));
    }
    listenFd_ = fd;
}

uint16_t EpollServer::port() const {
    if (listenFd_ < 0) throw std::logic_error("not listening");
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    checkSystem(getsockname(listenFd_, reinterpret_cast<sockaddr*>(&address), &length), "getsockname");
    return ntohs(address.sin_port);
}

void EpollServer::run() {
    if (listenFd_ < 0) throw std::logic_error("not listening");
    std::array<epoll_event, 64> events{};
    while (!stopping_) {
        int count = epoll_wait(epollFd_, events.data(), static_cast<int>(events.size()), -1);
        if (count < 0) {
            if (errno == EINTR) continue;
            checkSystem(count, "epoll_wait");
        }
        for (int i = 0; i < count && !stopping_; ++i) {
            int fd = events[i].data.fd;
            if (fd == wakeFd_) {
                stopping_ = true;
                break;
            }
            if (fd == listenFd_) acceptReady();
            else connectionReady(fd, events[i].events);
        }
    }
    while (!connections_.empty()) closeConnection(connections_.begin()->first);
}

void EpollServer::stop() {
    if (stopping_.exchange(true)) return;
    const uint64_t one = 1;
    (void)::write(wakeFd_, &one, sizeof(one));
}

void EpollServer::acceptReady() {
    for (;;) {
        int fd = accept4(listenFd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            checkSystem(fd, "accept4");
        }
        epoll_event event{};
        event.events = EPOLLIN | EPOLLRDHUP;
        event.data.fd = fd;
        if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event) < 0) {
            ::close(fd);
            checkSystem(-1, "epoll_ctl client");
        }
        connections_.emplace(fd, std::make_unique<Connection>());
    }
}

void EpollServer::connectionReady(int fd, uint32_t events) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;
    if (events & EPOLLERR) {
        closeConnection(fd);
        return;
    }
    Connection& connection = *it->second;
    try {
        if ((events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) && !connection.peerClosed &&
            !readFrames(fd, connection)) {
            closeConnection(fd);
            return;
        }
        if (!flushOutput(fd, connection)) {
            closeConnection(fd);
            return;
        }
        if (connection.peerClosed && connection.output.readableBytes() == 0) {
            closeConnection(fd);
            return;
        }
        updateInterest(fd, connection);
    } catch (const std::exception&) {
        closeConnection(fd);
    }
}

bool EpollServer::readFrames(int fd, Connection& connection) {
    char bytes[8192];
    for (;;) {
        ssize_t count = recv(fd, bytes, sizeof(bytes), 0);
        if (count > 0) {
            connection.input.append(bytes, static_cast<size_t>(count));
            Frame request;
            for (;;) {
                DecodeResult result = decodeFrame(connection.input, &request);
                if (result == DecodeResult::kNeedMore) break;
                if (result == DecodeResult::kError) return false;
                Frame response = handler_(request);
                // 回應沿用 request id，client 才能配對。
                if (response.body.size() > kMaxBodyLen ||
                    connection.output.readableBytes() + kHeaderLen + response.body.size() > kMaxQueuedOutput) {
                    return false;
                }
                encodeFrame(&connection.output, response.type, request.seq, response.body);
            }
            continue;
        }
        if (count == 0) {
            connection.peerClosed = true;
            return true;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        return false;
    }
}

bool EpollServer::flushOutput(int fd, Connection& connection) {
    while (connection.output.readableBytes() > 0) {
        ssize_t count = send(fd, connection.output.peek(), connection.output.readableBytes(), MSG_NOSIGNAL);
        if (count > 0) {
            connection.output.retrieve(static_cast<size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return true;
        return false;
    }
    return true;
}

void EpollServer::updateInterest(int fd, const Connection& connection) {
    epoll_event event{};
    event.events = connection.peerClosed ? 0 : (EPOLLIN | EPOLLRDHUP);
    if (connection.output.readableBytes() > 0) event.events |= EPOLLOUT;
    event.data.fd = fd;
    checkSystem(epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &event), "epoll_ctl modify");
}

void EpollServer::closeConnection(int fd) {
    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    connections_.erase(fd);
}
