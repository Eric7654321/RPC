#include "dispatcher.h"
#include "rpc.pb.h"

#include <exception>

Frame Dispatcher::dispatch(const Frame& frame) const {
    mini_rpc::ResponseEnvelope response;
    if (frame.type != 1) {
        response.set_error_code("wrong_frame_type");
    } else {
        mini_rpc::RequestEnvelope request;
        if (!request.ParseFromString(frame.body) || request.method().empty()) {
            response.set_error_code("bad_request");
        } else {
            auto method = methods_.find(request.method());
            if (method == methods_.end()) {
                response.set_error_code("unknown_method");
            } else {
                std::string payload;
                std::string error;
                try {
                    method->second(request.payload(), &payload, &error);
                    if (error.empty()) response.set_payload(std::move(payload));
                    else response.set_error_code(std::move(error));
                } catch (const std::exception& ex) {
                    response.set_error_code("handler_error");
                    response.set_error_message(ex.what());
                } catch (...) {
                    response.set_error_code("handler_error");
                }
            }
        }
    }
    std::string body;
    if (!response.SerializeToString(&body)) throw std::runtime_error("response envelope serialization failed");
    return Frame{2, frame.seq, std::move(body)};
}
