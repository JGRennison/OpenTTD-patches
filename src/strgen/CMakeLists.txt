cmake_minimum_required(VERSION 3.16)

if (NOT HOST_BINARY_DIR)
    project(strgen)

    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")

    set(sourcefiles
            strgen.cpp
            strgen_base.cpp
            ../core/alloc_func.cpp
            ../core/format.cpp
            ../misc/getoptdata.cpp
            ../error.cpp
            ../string.cpp
            ../3rdparty/fmt/format.cc
    )
    add_definitions(-DSTRGEN)
    add_executable(strgen ${sourcefiles})

    export(TARGETS strgen NAMESPACE host FILE ${CMAKE_BINARY_DIR}/strgen.cmake)
    add_dependencies(tools strgen)
endif()

if(OPTION_TOOLS_ONLY)
    return()
endif()

# Source Files
add_files(strgen_base.cpp)

# Header Files
add_files(strgen.h)
