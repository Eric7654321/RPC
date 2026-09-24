#include "mini_rpc/dispatcher.h"
#include "rpc.pb.h"
#include "add.pb.h"
#include "check.h"

#include <stdexcept>
#include <string>

namespace {
Frame makeRequest(uint32_t seq, const std::string& method, const std::string& payload) {
    mini_rpc::RequestEnvelope envelope;
    envelope.set_method(method);
    envelope.set_payload(payload);
    return Frame{1, seq, envelope.SerializeAsString()};
}

mini_rpc::ResponseEnvelope unpack(const Frame& frame, uint32_t seq) {
    EXPECT_EQ(frame.type, 2u);
    EXPECT_EQ(frame.seq, seq);
    mini_rpc::ResponseEnvelope response;
    EXPECT_EQ(response.ParseFromString(frame.body), true);
    return response;
}
}

int main() {
    Dispatcher dispatcher;
    dispatcher.registerMethod<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>(
        "AddService.Add", [](const mini_rpc_test::AddRequest& request,
                             mini_rpc_test::AddResponse* response) {
            response->set_sum(request.a() + request.b());
        });

    mini_rpc_test::AddRequest args;
    args.set_a(7);
    args.set_b(35);
    auto response = unpack(dispatcher.dispatch(makeRequest(41, "AddService.Add", args.SerializeAsString())), 41);
    EXPECT_EQ(response.error_code(), std::string());
    mini_rpc_test::AddResponse result;
    EXPECT_EQ(result.ParseFromString(response.payload()), true);
    EXPECT_EQ(result.sum(), 42);

    response = unpack(dispatcher.dispatch(makeRequest(42, "Missing.Method", args.SerializeAsString())), 42);
    EXPECT_EQ(response.error_code(), std::string("unknown_method"));

    response = unpack(dispatcher.dispatch(makeRequest(43, "AddService.Add", std::string("\xff", 1))), 43);
    EXPECT_EQ(response.error_code(), std::string("bad_payload"));

    response = unpack(dispatcher.dispatch(Frame{1, 44, std::string("\xff", 1)}), 44);
    EXPECT_EQ(response.error_code(), std::string("bad_request"));

    response = unpack(dispatcher.dispatch(Frame{9, 45, ""}), 45);
    EXPECT_EQ(response.error_code(), std::string("wrong_frame_type"));

    dispatcher.registerMethod<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>(
        "AddService.Fail", [](const mini_rpc_test::AddRequest&, mini_rpc_test::AddResponse*) {
            throw std::runtime_error("boom");
        });
    response = unpack(dispatcher.dispatch(makeRequest(46, "AddService.Fail", args.SerializeAsString())), 46);
    EXPECT_EQ(response.error_code(), std::string("handler_error"));
    EXPECT_EQ(response.error_message(), std::string("boom"));
}
