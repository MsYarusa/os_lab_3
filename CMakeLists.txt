cmake_minimum_required(VERSION 3.10)
project(lab_3)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)

add_executable(lab_3 main.cpp)

if(UNIX)
    target_link_libraries(lab_3 rt pthread)
endif()