cmake_minimum_required(VERSION 3.10)

project(hello_world)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED On)
set(CMAKE_CXX_EXTENSIONS Off)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpqxx -lpq")

INCLUDE(FindPkgConfig)

if("$ENV{ROS_DISTRO}" STRGREATER "debian")
    pkg_check_modules(HTTPLIB cpp-httplib REQUIRED)
    set(httplib_path ${HTTPLIB_LIBRARIES})
else()
    find_package(httplib REQUIRED)
    set(httplib_path ${HTTPLIB_LIBRARY})
endif()

add_executable(chats chats.cc)

target_link_libraries(chats ${httplib_path} -lpqxx -lpq)