cmake_minimum_required(VERSION 3.9)
# Keep the version below in sync with the one in db.h
project(leveldb VERSION 1.22.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# for debug, need to add this header path
# because I have changed the directory structure
include_directories(leveldb/include)

add_subdirectory(leveldb)
add_subdirectory(mytest)