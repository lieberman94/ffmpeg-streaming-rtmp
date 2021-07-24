cmake_minimum_required(VERSION 3.16)
project(CXXDEMO)

set(CMAKE_CXX_STANDARD 14)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
        libavformat
        libavcodec
        libavutil
        )

add_executable(CXXDEMO main.cpp)

target_link_libraries(${PROJECT_NAME}
        PkgConfig::LIBAV
        )