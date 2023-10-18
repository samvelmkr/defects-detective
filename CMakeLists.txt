cmake_minimum_required(VERSION 3.0)
project(Analyzer)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX17_STANDARD_COMPILE_OPTION}")
link_libraries(stdc++fs)

add_subdirectory(src)
