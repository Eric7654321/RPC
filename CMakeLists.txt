cmake_minimum_required(VERSION 3.16)
project(mini_rpc CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

find_package(Protobuf REQUIRED)
protobuf_generate_cpp(RPC_PROTO_SRCS RPC_PROTO_HDRS proto/rpc.proto)

add_library(rpc_core
    src/buffer.cpp
    src/codec.cpp
    src/epoll_server.cpp
    src/dispatcher.cpp
    src/rpc_client.cpp
    src/service_registry.cpp
    src/rpc_client_pool.cpp
    ${RPC_PROTO_SRCS}
    ${RPC_PROTO_HDRS}
)
target_include_directories(rpc_core PUBLIC include ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(rpc_core PUBLIC protobuf::libprotobuf)

enable_testing()

add_executable(buffer_test tests/buffer_test.cpp)
target_link_libraries(buffer_test PRIVATE rpc_core)
target_include_directories(buffer_test PRIVATE tests)
add_test(NAME buffer_test COMMAND buffer_test)

add_executable(codec_test tests/codec_test.cpp)
target_link_libraries(codec_test PRIVATE rpc_core)
target_include_directories(codec_test PRIVATE tests)
add_test(NAME codec_test COMMAND codec_test)

add_executable(epoll_server_test tests/epoll_server_test.cpp)
target_link_libraries(epoll_server_test PRIVATE rpc_core)
target_include_directories(epoll_server_test PRIVATE tests)
add_test(NAME epoll_server_test COMMAND epoll_server_test)

protobuf_generate_cpp(ADD_PROTO_SRCS ADD_PROTO_HDRS proto/add.proto)
add_executable(dispatcher_test tests/dispatcher_test.cpp ${ADD_PROTO_SRCS} ${ADD_PROTO_HDRS})
target_link_libraries(dispatcher_test PRIVATE rpc_core)
target_include_directories(dispatcher_test PRIVATE tests ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME dispatcher_test COMMAND dispatcher_test)

add_executable(rpc_client_test tests/rpc_client_test.cpp ${ADD_PROTO_SRCS} ${ADD_PROTO_HDRS})
target_link_libraries(rpc_client_test PRIVATE rpc_core)
target_include_directories(rpc_client_test PRIVATE tests ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME rpc_client_test COMMAND rpc_client_test)

add_executable(rpc_client_pool_test tests/rpc_client_pool_test.cpp ${ADD_PROTO_SRCS} ${ADD_PROTO_HDRS})
target_link_libraries(rpc_client_pool_test PRIVATE rpc_core)
target_include_directories(rpc_client_pool_test PRIVATE tests ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME rpc_client_pool_test COMMAND rpc_client_pool_test)

add_executable(add_demo examples/add_demo.cpp ${ADD_PROTO_SRCS} ${ADD_PROTO_HDRS})
target_link_libraries(add_demo PRIVATE rpc_core)
target_include_directories(add_demo PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
