cmake_minimum_required(VERSION 3.10)

project(hello_world)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpqxx -lpq")

INCLUDE(FindPkgConfig)

if("$ENV{ROS_DISTRO}" STRGREATER "debian")
    pkg_check_modules(HTTPLIB cpp-httplib REQUIRED)
else()
    find_package(httplib REQUIRED)
endif()

add_executable(chats chats.cc)

target_link_libraries(chats ${HTTPLIB_LIBRARIES} -lpqxx -lpq)