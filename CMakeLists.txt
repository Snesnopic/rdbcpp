cmake_minimum_required(VERSION 3.5...4.1.2)
project(rdbcpp LANGUAGES CXX VERSION 1.0.1)

if(MSVC)
    # force static msvc runtime linkage (/mt for release, /mtd for debug)
    if(POLICY CMP0091)
        cmake_policy(SET CMP0091 NEW)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(rdbcpp main.cpp)
target_compile_definitions(rdbcpp PRIVATE RDBCPP_VERSION="${PROJECT_VERSION}")
