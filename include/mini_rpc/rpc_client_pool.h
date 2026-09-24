#pragma once

#include "mini_rpc/rpc_client.h"
#include "mini_rpc/service_registry.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// 每個服務按需建立固定數量的長連線，呼叫時輪流選取。
class RpcClientPool {
public:
    explicit RpcClientPool(ServiceRegistry registry, size_t connectionsPerService = 2);

    template <typename Request, typename Response>
    Response call(const std::string& method, const Request& request,
                  std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
        return pickClient(method)->call<Request, Response>(method, request, timeout);
    }

    // 只供呼叫方確認為冪等的操作使用。每次嘗試各有自己的 timeout；
    // 前一次可能已在 server 執行，只是回應逾時。
    template <typename Request, typename Response>
    Response callIdempotent(const std::string& method, const Request& request,
                            size_t maxAttempts = 2,
                            std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
        if (maxAttempts == 0) throw std::invalid_argument("maxAttempts must be positive");
        for (size_t attempt = 0; attempt < maxAttempts; ++attempt) {
            try {
                return call<Request, Response>(method, request, timeout);
            } catch (const RpcTimeout&) {
                if (attempt + 1 == maxAttempts) throw;
            }
        }
        throw std::logic_error("unreachable retry state");
    }

private:
    std::shared_ptr<RpcClient> pickClient(const std::string& method);

    struct Entries {
        std::vector<std::shared_ptr<RpcClient>> clients;
        size_t next = 0;
    };
    ServiceRegistry registry_;
    size_t connectionsPerService_;
    std::mutex mutex_;
    std::unordered_map<std::string, Entries> pools_;
};
