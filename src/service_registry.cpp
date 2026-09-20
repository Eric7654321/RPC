#include "service_registry.h"

#include <arpa/inet.h>
#include <fstream>
#include <sstream>
#include <stdexcept>

ServiceRegistry ServiceRegistry::loadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open service config: " + path);
    ServiceRegistry registry;
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);
        std::istringstream fields(line);
        std::string service, address, portText, extra;
        if (!(fields >> service)) continue;
        if (!(fields >> address >> portText) || (fields >> extra)) {
            throw std::runtime_error("bad service config line " + std::to_string(lineNumber));
        }
        in_addr parsedAddress{};
        if (inet_pton(AF_INET, address.c_str(), &parsedAddress) != 1) {
            throw std::runtime_error("bad IPv4 on service config line " + std::to_string(lineNumber));
        }
        size_t parsed = 0;
        unsigned long port;
        try {
            port = std::stoul(portText, &parsed);
        } catch (const std::exception&) {
            throw std::runtime_error("bad port on service config line " + std::to_string(lineNumber));
        }
        if (parsed != portText.size() || port == 0 || port > 65535) {
            throw std::runtime_error("bad port on service config line " + std::to_string(lineNumber));
        }
        if (!registry.services_.emplace(service, ServiceEndpoint{address, static_cast<uint16_t>(port)}).second) {
            throw std::runtime_error("duplicate service on config line " + std::to_string(lineNumber));
        }
    }
    if (file.bad()) throw std::runtime_error("cannot read service config: " + path);
    return registry;
}

const ServiceEndpoint& ServiceRegistry::resolve(const std::string& method) const {
    const auto dot = method.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == method.size()) {
        throw std::invalid_argument("method must be Service.Method");
    }
    auto service = services_.find(method.substr(0, dot));
    if (service == services_.end()) throw std::out_of_range("service not configured: " + method.substr(0, dot));
    return service->second;
}
