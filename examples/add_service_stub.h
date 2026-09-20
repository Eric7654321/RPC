#pragma once

#include "add.pb.h"
#include "rpc_client.h"

#include <chrono>
#include <cstdint>

// 服務專用 stub 將一般參數轉成 Protobuf request，並取出 response。
class AddServiceStub {
public:
    explicit AddServiceStub(RpcClient& client) : client_(client) {}

    int32_t add(int32_t a, int32_t b,
                std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
        mini_rpc_test::AddRequest request;
        request.set_a(a);
        request.set_b(b);
        return client_.call<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>(
            "AddService.Add", request, timeout).sum();
    }

private:
    RpcClient& client_;
};
