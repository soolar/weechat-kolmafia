cmake_minimum_required(VERSION 2.6)

project(weechat-kolmafia)

set(weechat-kolmafia_VERSION_MAJOR 0)
set(weechat-kolmafia_VERSION_MINOR 1)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

#include_directories("/usr/include/jsoncpp")

add_library(weechat-kolmafia SHARED weechat-kolmafia.cpp weechat-kolmafia-config.cpp jsoncpp.cpp)

target_link_libraries(weechat-kolmafia /usr/lib/x86_64-linux-gnu/libcurl.so /usr/lib/x86_64-linux-gnu/libhtmlcxx.so)

