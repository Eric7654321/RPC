#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

struct ServiceEndpoint {
    std::string ipv4;
    uint16_t port;
};

// 本地設定檔中的服務名 → 位址；這裡不做動態服務發現。
class ServiceRegistry {
public:
    static ServiceRegistry loadFile(const std::string& path);
    const ServiceEndpoint& resolve(const std::string& method) const;

private:
    std::unordered_map<std::string, ServiceEndpoint> services_;
};
