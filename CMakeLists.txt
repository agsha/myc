cmake_minimum_required(VERSION 3.8)
project(myc)

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${CMAKE_CURRENT_SOURCE_DIR}")

set(CMAKE_CXX_STANDARD 17)
find_package(XMLRPC REQUIRED c++2 abyss-server client)
find_package(Libevent)
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

if (Libevent_FOUND)
#    include_directories(${LIBEVENT_INCLUDE_DIRS})

#    target_link_libraries (myc ${LIBEVENT_LIBRARIES})
#    message("found libevent yo ${LIBEVENT_INCLUDE_DIRS} ${LIBEVENT_LIBRARIES}")

endif(Libevent_FOUND)

if (LIBEV_FOUND)
    include_directories(${LIBEV_INCLUDE_DIRS})

    target_link_libraries (myc ${LIBEV_LIBRARIES})
    message("found libev yo ${LIBEV_INCLUDE_DIRS} ${LIBEV_LIBRARIES}")

endif(LIBEV_FOUND)
