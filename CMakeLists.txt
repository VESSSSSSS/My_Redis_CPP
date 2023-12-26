# CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(my_program)

# 设置编译器标准
set(CMAKE_CXX_STANDARD 11)

# 添加可执行文件生成规则
add_executable(my_program
    src/hashtable.cpp
    test/server_hash_test.cpp
)
