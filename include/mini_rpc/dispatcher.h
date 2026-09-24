#pragma once

#include "mini_rpc/codec.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>

// 把 wire method 映射成具型別的 Protobuf handler。
class Dispatcher {
public:
    template <typename Request, typename Response>
    void registerMethod(const std::string& name,
                        std::function<void(const Request&, Response*)> handler) {
        if (name.empty() || !handler) throw std::invalid_argument("method or handler is empty");
        auto invoke = [handler = std::move(handler)](const std::string& payload,
                                                     std::string* result,
                                                     std::string* error) {
            Request request;
            if (!request.ParseFromString(payload)) {
                *error = "bad_payload";
                return;
            }
            Response response;
            handler(request, &response);
            if (!response.SerializeToString(result)) *error = "bad_response";
        };
        if (!methods_.emplace(name, std::move(invoke)).second) {
            throw std::invalid_argument("duplicate method: " + name);
        }
    }

    Frame dispatch(const Frame& request) const;

private:
    using Method = std::function<void(const std::string&, std::string*, std::string*)>;
    std::unordered_map<std::string, Method> methods_;
};
