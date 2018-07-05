
cmake_minimum_required(VERSION 2.8)


project(SoftwareRasterizer)

include_directories("externals/eigen")
include_directories("src")

add_subdirectory(example)
add_subdirectory(src)
