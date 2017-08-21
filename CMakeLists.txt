cmake_minimum_required(VERSION 3.0)
project(myc)
message(${CMAKE_CURRENT_LIST_DIR})
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_CXX_STANDARD 11)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1y")
find_program(XMLRPC_C_CONFIG NAMES xmlrpc-c-config)

# Check whether we found anything.

find_package(XMLRPC REQUIRED c++2 abyss-server client)
find_package(libev)
#set(SOURCE_FILES main.cpp latret.cpp latret.h NioPerfTest.cpp)
set(SOURCE_FILES main.cpp  NioPerfTest.cpp json.hpp latret.cpp latret.h conflicts.h)
#set(SOURCE_FILES evtest.cpp)
add_executable(myc ${SOURCE_FILES})
if (XMLRPC_FOUND)
    include_directories(${XMLRPC_INCLUDE_DIRS})

    target_link_libraries (myc ${XMLRPC_LIBRARIES})
    message("found xmlrpc ${XMLRPC_INCLUDE_DIRS} ${XMLRPC_LIBRARIES}")

endif(XMLRPC_FOUND)


if (LIBEV_FOUND)
    include_directories(${LIBEV_INCLUDE_DIRS})

    target_link_libraries (myc ${LIBEV_LIBRARIES})
    message("found libev yo ${LIBEV_INCLUDE_DIRS} ${LIBEV_LIBRARIES}")

endif(LIBEV_FOUND)
