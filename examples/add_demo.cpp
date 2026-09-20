#include "add.pb.h"
#include "dispatcher.h"
#include "epoll_server.h"
#include "rpc_client.h"
#include "add_service_stub.h"

#include <iostream>
#include <thread>

int main() {
    Dispatcher dispatcher;
    dispatcher.registerMethod<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>(
        "AddService.Add", [](const mini_rpc_test::AddRequest& request,
                             mini_rpc_test::AddResponse* response) {
            response->set_sum(request.a() + request.b());
        });

    EpollServer server([&dispatcher](const Frame& frame) { return dispatcher.dispatch(frame); });
    server.listen(0);
    std::thread worker([&server] { server.run(); });

    {
        RpcClient client("127.0.0.1", server.port());
        AddServiceStub addService(client);
        std::cout << "AddService.Add(1, 2) = " << addService.add(1, 2) << '\n';
    }

    server.stop();
    worker.join();
}
