# 强制使用clang++ 作为编译器

set(CMAKE_CXX_COMPILER "clang++")

# 最小 CMake 版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称和版本
project(Redis VERSION 1.0)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)


# Include header files from the include directory
include_directories(${CMAKE_SOURCE_DIR}/include)

# Add source files for the server
set(SERVER_SOURCES
    src/server.cpp
    src/hashtable.cpp
    src/zset.cpp
    src/avl.cpp
)

# Add source files for the client
set(CLIENT_SOURCES
    src/client.cpp
)

# Add server executable
add_executable(server ${SERVER_SOURCES})

# Add client executable
add_executable(client ${CLIENT_SOURCES})


# Link libraries to server
target_link_libraries(server
    pthread        # POSIX threads
    m              # Math library (if needed, some systems require it)
)

# Link libraries to client
target_link_libraries(client
    pthread        # POSIX threads
)

