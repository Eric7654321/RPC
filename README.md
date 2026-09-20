# mini RPC

一個 C++17 的教學用 RPC 雛形。`call()` 在程式裡像本地函式呼叫，實際上會經過序列化、TCP、服務分派與回應配對。

## 執行

需要 CMake、C++17 編譯器與 Protobuf 3（含 `protoc`）。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/add_demo
```

範例輸出：`AddService.Add(1, 2) = 3`。

## 資料怎麼走

1. Client 將方法名與 Protobuf 參數放進 `RequestEnvelope`。
2. Codec 在外面加上 10-byte header：`type`（2 bytes）、`seq`（4 bytes）、`bodyLen`（4 bytes）；整數皆為 network byte order，body 上限 64 KiB。
3. epoll server 為每條連線保留 input/output Buffer。收到完整 frame 後交給 `Dispatcher`。
4. `Dispatcher` 用方法名找到 handler，解析具型別的 Protobuf 參數，再包成 `ResponseEnvelope`。回應沿用 request 的 `seq`。
5. Client 的 reader thread 在該連線的 pending map 內依 `seq` 找到等待的 future。逾時後的晚到回應會被丟棄。

`proto/rpc.proto` 定義框架的 wire envelope；`proto/add.proto` 是示範服務。要加服務，先定義 request/response message，再以 `registerMethod<Request, Response>()` 註冊 handler。`examples/add_demo.cpp` 有完整可執行範例。

原本想要的 `client.call("AddService.Add", 1, 2)` 需要一層由服務定義產生或手寫的 stub，才知道兩個整數該如何轉成 Protobuf message。框架提供具型別的 `call<Request, Response>(method, request)`；範例中的 `AddServiceStub::add(1, 2)` 展示手寫 stub 如何包住它。

## 本地服務設定與連線池

`config/services.example.conf` 示範一行一個服務：`服務名 IPv4 port`。`ServiceRegistry::loadFile(path)` 讀入設定，`RpcClientPool` 根據方法名的服務前綴選位址，並為各服務按需建立最多兩條長連線，輪流使用。例如：

```cpp
auto registry = ServiceRegistry::loadFile("config/services.example.conf");
RpcClientPool pool(std::move(registry), 2);
auto result = pool.call<mini_rpc_test::AddRequest, mini_rpc_test::AddResponse>(
    "AddService.Add", request);
```

設定檔中的 `9000` 是示例埠；實際服務必須在該埠啟動。`rpc_client_pool_test` 會用測試服務的實際埠產生臨時設定檔，驗證設定讀取、路由及多次呼叫。這是靜態本地設定，不會探測新節點，也不會自動刷新壞掉的連線。

只有呼叫方能確認操作可重複執行時，才使用 `pool.callIdempotent<Request, Response>(method, request, maxAttempts, timeout)`。它只在 `RpcTimeout` 後重試；每次嘗試各有自己的逾時，最長等待時間可能接近 `maxAttempts × timeout`。例如 Add 是純計算，執行兩次仍得到相同結果。這個 API 不提供 server 去重，也不保證每個操作只執行一次。

## 目前語意

- 同一條連線可以同時有多個未完成呼叫；每個呼叫各有 `seq` 作為該連線內的 request ID。timeout 從開始呼叫算起，涵蓋送出與等待回應。
- 若送出途中失敗，client 關閉連線，因為 server 可能已收到半個 frame；完整送出後才逾時，晚到回應依 request ID 丟棄。
- TCP 斷線、壞 frame、遠端 handler 錯誤分別回報失敗。逾時不代表遠端沒有執行：只是 client 等不到結果。
- 一般 `call()` 不自動重試；只有明確呼叫 `callIdempotent()` 才會在逾時後重送。對可能改變狀態的方法，重試可能使操作執行兩次。`seq` 只用來配對單一連線上的回應：同一操作換連線重試會取得不同 `seq`，不同操作也可能有相同 `seq`。若要跨連線去重，需要獨立且在重試時保持不變的操作 ID。
- Server handler 目前在單一 epoll event loop 執行，耗時 handler 會阻塞其他連線。handler 仍在程式內註冊；本地設定檔只記錄 client 要連的服務位址，尚無動態服務發現或跨節點部署。
- 目前只支援數字 IPv4 地址，server 綁定 loopback，供本機學習與測試。
