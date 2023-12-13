add_files(
    mingw.condition_variable.h
    mingw.invoke.h
    mingw.mutex.h
    mingw.shared_mutex.h
    mingw.thread.h
    CONDITION MINGW
)

if (MINGW)
    string(CONCAT mingw_stdthreads_dir_docstring
           "Optional. When generating std-like headers , this variable can be set"
           "to manually specify the path to mingw-stdthreads directory containing"
           "original library headers.")
    set(MINGW_STDTHREADS_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
        CACHE PATH ${mingw_stdthreads_dir_docstring})

    # mingw-stdthreads is a header-only library, so make it a INTERFACE target
    add_library(mingw_stdthreads INTERFACE)
    target_include_directories(mingw_stdthreads INTERFACE "${PROJECT_SOURCE_DIR}")

    add_subdirectory(cmake_stdheaders_generator)
    target_link_libraries(mingw_stdthreads INTERFACE cmake_stdheaders_generator)
endif()
