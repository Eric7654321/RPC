#pragma once

#include "mini_rpc/codec.h"
#include "rpc.pb.h"

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

class RpcTimeout : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class RpcRemoteError : public std::runtime_error {
public:
    RpcRemoteError(std::string code, std::string message);
    const std::string& code() const { return code_; }
private:
    std::string code_;
};

// 一條 TCP 連線可同時承載多個 call；reader thread 依 seq 將回應送回各個 future。
class RpcClient {
public:
    RpcClient(const std::string& ipv4, uint16_t port);
    ~RpcClient();
    RpcClient(const RpcClient&) = delete;
    RpcClient& operator=(const RpcClient&) = delete;

    template <typename Request, typename Response>
    Response call(const std::string& method, const Request& request,
                  std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
        if (timeout.count() <= 0) throw std::invalid_argument("timeout must be positive");
        std::string payload;
        if (!request.SerializeToString(&payload)) throw std::runtime_error("request serialization failed");
        mini_rpc::RequestEnvelope envelope;
        envelope.set_method(method);
        envelope.set_payload(std::move(payload));
        Frame frame = exchange(envelope.SerializeAsString(), timeout);
        if (frame.type != 2) throw std::runtime_error("wrong response frame type");
        mini_rpc::ResponseEnvelope result;
        if (!result.ParseFromString(frame.body)) throw std::runtime_error("bad response envelope");
        if (!result.error_code().empty()) throw RpcRemoteError(result.error_code(), result.error_message());
        Response response;
        if (!response.ParseFromString(result.payload())) throw std::runtime_error("bad response payload");
        return response;
    }

private:
    Frame exchange(const std::string& body, std::chrono::milliseconds timeout);
    void readLoop();
    void failAll(const std::string& reason);

    int fd_ = -1;
    std::thread reader_;
    std::mutex sendMutex_;
    std::mutex pendingMutex_;
    bool closed_ = false;
    uint32_t nextSeq_ = 1;
    std::unordered_map<uint32_t, std::shared_ptr<std::promise<Frame>>> pending_;
};
