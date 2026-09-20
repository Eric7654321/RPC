#include "add.pb.h"
#include "check.h"
#include "dispatcher.h"
#include "epoll_server.h"
#include "rpc_client_pool.h"

#include <cstdio>
#include <atomic>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

int main() {
    using namespace std::chrono_literals;
    Dispatcher dispatcher;
    std::atomic<int> slowCalls{0};
    dispatcher.registerMethod<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>(
        "AddService.Add", [](const mini_rpc_test::AddRequest& request,
                             mini_rpc_test::AddResponse* response) {
            response->set_sum(request.a() + request.b());
        });
    dispatcher.registerMethod<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>(
        "AddService.SlowOnce", [&slowCalls](const mini_rpc_test::AddRequest& request,
                                             mini_rpc_test::AddResponse* response) {
            if (++slowCalls == 1) std::this_thread::sleep_for(80ms);
            response->set_sum(request.a() + request.b());
        });
    EpollServer server([&dispatcher](const Frame& frame) { return dispatcher.dispatch(frame); });
    server.listen(0);
    std::thread worker([&server] { server.run(); });

    char path[] = "/tmp/mini_rpc_services_XXXXXX";
    const int configFd = mkstemp(path);
    if (configFd < 0) throw std::runtime_error("mkstemp failed");
    close(configFd);
    {
        std::ofstream config(path);
        config << "# service ipv4 port\nAddService 127.0.0.1 " << server.port() << "\n";
    }
    ServiceRegistry registry = ServiceRegistry::loadFile(path);
    std::remove(path);
    EXPECT_EQ(registry.resolve("AddService.Add").port, server.port());

    bool missingFailed = false;
    try {
        registry.resolve("Missing.Add");
    } catch (const std::out_of_range&) {
        missingFailed = true;
    }
    EXPECT_EQ(missingFailed, true);

    {
        RpcClientPool pool(std::move(registry), 2);
        mini_rpc_test::AddRequest request;
        request.set_a(19);
        request.set_b(23);
        for (int i = 0; i < 4; ++i) {
            const auto response = pool.call<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>(
                "AddService.Add", request);
            EXPECT_EQ(response.sum(), 42);
        }
        // Add 是純計算；第一次逾時後即使 server 已執行，第二次執行仍安全。
        auto retried = pool.callIdempotent<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>(
            "AddService.SlowOnce", request, 2, 60ms);
        EXPECT_EQ(retried.sum(), 42);
        EXPECT_EQ(slowCalls.load(), 2);
    }

    server.stop();
    worker.join();
}
