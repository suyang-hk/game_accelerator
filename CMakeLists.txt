cmake_minimum_required(VERSION 3.16)
project(udp_gateway_phase1 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(udp_client      client/udp_client.cpp)
add_executable(gateway         gateway/gateway.cpp)
add_executable(udp_echo_server server/udp_echo_server.cpp)
