cmake_minimum_required(VERSION 3.16)
project(udp_gateway C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# KCP 是 C 库(third_party/kcp),单独编成静态库,gateway 和 udp_client 都链它
add_library(kcp STATIC third_party/kcp/ikcp.c)
target_include_directories(kcp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/kcp)

# 共用的时间/地址小工具(net_utils),两个程序都要;tunnel_protocol.h 同走 common
add_library(net_utils STATIC common/net_utils.cpp)
target_include_directories(net_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)

# 只对我们自己的代码开警告,别管第三方的 C 库
add_compile_options(-Wall -Wextra)

# 客户端: main(入口) + tunnel_client(隧道收发)
add_executable(udp_client      client/main.cpp client/tunnel_client.cpp)
target_link_libraries(udp_client kcp net_utils)

# 网关拆成模块: main / gateway(中继) / session / session_manager /
# kcp_session / event_loop / kv_client(控制面)
add_executable(gateway
    gateway/main.cpp
    gateway/gateway.cpp
    gateway/session.cpp
    gateway/session_manager.cpp
    gateway/kcp_session.cpp
    gateway/event_loop.cpp
    gateway/kv_client.cpp)
target_link_libraries(gateway kcp net_utils)

# 测试用 UDP 回声服,当中继目标
add_executable(udp_echo_server test_server/udp_echo_server.cpp)
