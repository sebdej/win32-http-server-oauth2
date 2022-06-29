cmake_minimum_required(VERSION 3.20)

project(Tests VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(HttpServer src/main.cpp src/http_server.cpp src/http_server.h)

find_package(spdlog CONFIG REQUIRED)

target_link_libraries(HttpServer PRIVATE spdlog::spdlog_header_only)
target_link_libraries(HttpServer PRIVATE httpapi.lib shlwapi.lib)