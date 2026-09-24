#include "mini_rpc/dispatcher.h"
#include "mini_rpc/epoll_server.h"
#include "mini_rpc/rpc_client.h"
#include "add.pb.h"
#include "check.h"

#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>

using namespace std::chrono_literals;

int main() {
    Dispatcher dispatcher;
    dispatcher.registerMethod<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>(
        "AddService.Add", [](const mini_rpc_test::AddRequest& request,
                             mini_rpc_test::AddResponse* response) {
            response->set_sum(request.a() + request.b());
        });
    dispatcher.registerMethod<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>(
        "AddService.Slow", [](const mini_rpc_test::AddRequest& request,
                              mini_rpc_test::AddResponse* response) {
            std::this_thread::sleep_for(80ms);
            response->set_sum(request.a() + request.b());
        });

    EpollServer server([&dispatcher](const Frame& frame) { return dispatcher.dispatch(frame); });
    server.listen(0);
    std::thread worker([&server] { server.run(); });
    RpcClient client("127.0.0.1", server.port());

    mini_rpc_test::AddRequest args;
    args.set_a(19);
    args.set_b(23);
    auto first = client.call<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>("AddService.Add", args);
    EXPECT_EQ(first.sum(), 42);

    // 多個執行緒共用同一條 TCP 連線，每個 future 都要拿到自己的 seq 回應。
    auto callA = std::async(std::launch::async, [&client] {
        mini_rpc_test::AddRequest request;
        request.set_a(1);
        request.set_b(2);
        return client.call<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>("AddService.Add", request).sum();
    });
    auto callB = std::async(std::launch::async, [&client] {
        mini_rpc_test::AddRequest request;
        request.set_a(10);
        request.set_b(20);
        return client.call<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>("AddService.Add", request).sum();
    });
    EXPECT_EQ(callA.get(), 3);
    EXPECT_EQ(callB.get(), 30);

    try {
        client.call<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>("Missing.Method", args);
        throw std::runtime_error("unknown method should fail");
    } catch (const RpcRemoteError& error) {
        EXPECT_EQ(error.code(), std::string("unknown_method"));
    }

    try {
        client.call<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>("AddService.Slow", args, 10ms);
        throw std::runtime_error("slow call should time out");
    } catch (const RpcTimeout&) {
    }
    // 晚到的舊回應必須被忽略，不可交給下一個 call。
    auto afterTimeout = client.call<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>(
        "AddService.Add", args, 1s);
    EXPECT_EQ(afterTimeout.sum(), 42);

    server.stop();
    worker.join();
    bool closedCallFailed = false;
    try {
        client.call<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>("AddService.Add", args, 100ms);
    } catch (const std::runtime_error&) {
        closedCallFailed = true;
    }
    EXPECT_EQ(closedCallFailed, true);
}
