cmake_minimum_required(VERSION 3.10)

project(hello_world)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpqxx -lpq")

INCLUDE(FindPkgConfig)

pkg_check_modules(HTTPLIB cpp-httplib REQUIRED)
# pkg_check_modules(PQXX_LIB pqxx REQUIRED)
# find_package(httplib REQUIRED)



add_executable(main_exe test.cpp)

target_link_libraries(main_exe ${HTTPLIB_LIBRARIES} -lpqxx -lpq)

# target_include_directories(main_exe ${HTTPLIB_INCLUDE_DIR})