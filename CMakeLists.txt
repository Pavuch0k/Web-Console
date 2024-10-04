cmake_minimum_required(VERSION 3.10)
project(WebSocketServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS system thread serialization)

if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
endif()

add_executable(WebSocketServer main.cpp)

target_link_libraries(WebSocketServer
    ${Boost_LIBRARIES}
)

find_package(Threads REQUIRED)
target_link_libraries(WebSocketServer PRIVATE Threads::Threads)

target_link_libraries(WebSocketServer PRIVATE
    rt
)
