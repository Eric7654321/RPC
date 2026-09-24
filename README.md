# Mini RPC

以 C++17、Linux epoll 與 Protocol Buffers 實作的迷你 RPC 框架。專案包含 TCP 訊框編解碼、服務方法分派、同步呼叫介面、請求與回應配對、逾時處理、本地服務設定及簡易連線池。

## 執行範例

需要 CMake、支援 C++17 的編譯器，以及 Protobuf 3 開發套件與 `protoc`。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/add_demo
```

範例在本機啟動服務，註冊 `AddService.Add`，再由客戶端呼叫：

```cpp
RpcClient client("127.0.0.1", server.port());
AddServiceStub addService(client);
int sum = addService.add(1, 2);  // 3
```

`AddServiceStub` 將整數參數轉成 Protobuf request，呼叫 RPC client，並從 response 取出結果。完整程式位於 [`examples/add_demo.cpp`](examples/add_demo.cpp)。

## 目錄結構

| 目錄 | 內容 |
|---|---|
| `include/mini_rpc/` | 框架的公開標頭；使用時以 `#include "mini_rpc/rpc_client.h"` 引入 |
| `src/` | 與公開標頭同名的 `.cpp` 實作 |
| `proto/` | RPC 協定與範例服務的 Protobuf 定義；產生的 C++ 檔案放在 `build/` |
| `examples/` | 可執行的 Add 範例與該範例專用的 stub |
| `tests/` | 各元件測試與測試專用的 `check.h` |

## 架構

1. Client 將方法名與 Protobuf 參數序列化為 `RequestEnvelope`。
2. Codec 加上 10-byte 訊框標頭：`type`（2 bytes）、`seq`（4 bytes）、`bodyLen`（4 bytes），整數採 network byte order，body 上限為 64 KiB。
3. epoll server 為每條 TCP 連線保留輸入與輸出 buffer，組出完整訊框後交給 `Dispatcher`。
4. `Dispatcher` 依 `服務名.方法名` 查找 handler，解析參數並封裝 `ResponseEnvelope`；回應沿用請求的 `seq`。
5. Client 以 `seq` 對應該連線上等待的 `std::promise`／`std::future`。逾時後到達的回應會被忽略。

[`proto/rpc.proto`](proto/rpc.proto) 定義 RPC envelope；[`proto/add.proto`](proto/add.proto) 定義範例服務的參數與結果。新服務可定義自己的 Protobuf message，並以 `Dispatcher::registerMethod<Request, Response>()` 註冊 handler。框架提供具型別的 `RpcClient::call<Request, Response>()`；服務專用 stub 可在其上封裝較簡潔的介面。

## 本地服務設定與連線池

[`config/services.example.conf`](config/services.example.conf) 以 `服務名 IPv4 port` 格式設定服務位址。`ServiceRegistry` 讀取設定後，`RpcClientPool` 依方法名的服務前綴選擇位址，並為每個服務按需建立指定數量的長連線，輪流使用。

```cpp
auto registry = ServiceRegistry::loadFile("config/services.example.conf");
RpcClientPool pool(std::move(registry), 2);

mini_rpc_test::AddRequest request;
request.set_a(1);
request.set_b(2);
auto response = pool.call<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>(
    "AddService.Add", request);
```

設定檔中的埠號需與執行中的服務一致。`rpc_client_pool_test` 以實際監聽埠建立臨時設定檔，驗證設定讀取、服務路由與多次呼叫。

## 呼叫語意與範圍

- 同一條連線可同時處理多個未完成呼叫。`seq` 是連線內的 request ID，負責配對請求與回應；它不是跨連線的操作識別碼。
- 預設逾時為 3 秒，涵蓋送出與等待回應。逾時只表示客戶端未在期限內取得結果，不能據此判斷服務端是否執行。
- 一般 `call()` 不自動重試。對可重複執行的冪等操作，可明確使用 `RpcClientPool::callIdempotent()` 在逾時後重試；每次嘗試各有自己的逾時。此介面不提供服務端去重或「恰好執行一次」保證。
- 送出途中若失敗，client 會關閉該連線，避免後續請求接在不完整的訊框後面。連線中斷、格式錯誤與 handler 錯誤會回報失敗。
- 目前僅支援數字 IPv4 位址與本機 loopback 服務。服務位址由靜態設定檔提供；連線池不會自動替換失效連線。Server handler 在單一 epoll event loop 執行，耗時方法會阻塞其他連線。

測試涵蓋 buffer、訊框的半包與黏包、epoll 傳輸、方法分派、並行呼叫配對、逾時回應，以及服務設定與連線池。
