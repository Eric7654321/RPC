#include "rpc_client_pool.h"

#include <stdexcept>
#include <utility>

RpcClientPool::RpcClientPool(ServiceRegistry registry, size_t connectionsPerService)
    : registry_(std::move(registry)), connectionsPerService_(connectionsPerService) {
    if (connectionsPerService_ == 0) throw std::invalid_argument("pool size must be positive");
}

std::shared_ptr<RpcClient> RpcClientPool::pickClient(const std::string& method) {
    const ServiceEndpoint& endpoint = registry_.resolve(method);
    const std::string service = method.substr(0, method.find('.'));
    std::lock_guard<std::mutex> lock(mutex_);
    auto& entries = pools_[service];
    if (entries.clients.empty()) {
        // 保留已成功建立的連線；下次呼叫可繼續補足池容量。
        entries.clients.reserve(connectionsPerService_);
    }
    if (entries.clients.size() < connectionsPerService_) {
        entries.clients.push_back(std::make_shared<RpcClient>(endpoint.ipv4, endpoint.port));
    }
    return entries.clients[entries.next++ % entries.clients.size()];
}
