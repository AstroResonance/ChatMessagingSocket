cmake_minimum_required(VERSION 3.29)
project(ChatMessagingSocket)

set(CMAKE_CXX_STANDARD 20)

add_executable(ChatMessagingSocket main.cpp
        server.cpp
        client.cpp
        server.cpp
)
