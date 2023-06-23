cmake_minimum_required(VERSION 3.12)
project(hellocmake LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


add_executable(factorial factorial.cc)
add_executable(whileloop whileloop.cc)
add_executable(sfinae sfinae.cc)
