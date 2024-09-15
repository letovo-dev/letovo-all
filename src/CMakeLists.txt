cmake_minimum_required(VERSION 3.10)

project(hello_world)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED On)
set(CMAKE_CXX_EXTENSIONS Off)

INCLUDE(FindPkgConfig)

if("$ENV{ROS_DISTRO}" STRGREATER "debian")
    pkg_check_modules(HTTPLIB cpp-httplib REQUIRED)
    set(httplib_path ${HTTPLIB_LIBRARIES})
else()
    find_package(httplib REQUIRED)
    set(httplib_path ${HTTPLIB_LIBRARY})
endif()

# ?
include(FetchContent)

fetchcontent_declare(jwt-cpp 
    GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
    GIT_TAG 08bcf77a687fb06e34138e9e9fa12a4ecbe12332 # v0.7.0 release
)
fetchcontent_declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.9.2
)
set(JWT_BUILD_EXAMPLES OFF CACHE BOOL "disable building examples" FORCE)
fetchcontent_makeavailable(jwt-cpp)
fetchContent_MakeAvailable(spdlog)


add_executable(server_starter examples.cpp)

target_link_libraries(server_starter ${httplib_path} jwt-cpp::jwt-cpp spdlog::spdlog -lpqxx -lpq)