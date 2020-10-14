cmake_minimum_required(VERSION 3.6)

if(NOT BINARY_NAME)
    set(BINARY_NAME openttd)
endif()

project(${BINARY_NAME})

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Please run \"cmake ..\" from the bin directory")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.9)

# Use GNUInstallDirs to allow customisation
# but set our own default data dir
if(NOT CMAKE_INSTALL_DATADIR)
    set(CMAKE_INSTALL_DATADIR "share/games")
endif()
include(GNUInstallDirs)

include(Options)
set_options()
set_directory_options()

include(Static)
set_static_if_needed()

# Prefer -pthread over -lpthread, which is often the better option of the two.
set(CMAKE_THREAD_PREFER_PTHREAD YES)
# Make sure we have Threads available.
find_package(Threads REQUIRED)

find_package(ZLIB)
find_package(LibLZMA)
find_package(LZO)
find_package(PNG)

if(NOT WIN32)
    find_package(Allegro)
    find_package(Freetype)
    if(NOT APPLE)
        find_package(SDL2)
        if(NOT SDL2_FOUND)
            find_package(SDL)
        endif()
        find_package(Fluidsynth)
        find_package(Fontconfig)
        find_package(ICU OPTIONAL_COMPONENTS i18n lx)
        find_package(XDG_basedir)
    else()
        find_package(Iconv)

        find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
        find_library(AUDIOUNIT_LIBRARY AudioUnit)
        find_library(COCOA_LIBRARY Cocoa)

        find_package(MacUcontext)
    endif()
endif()

if(MSVC)
    find_package(Editbin REQUIRED)
else()
    find_package(Builtins)
endif()

if (UNIX)
    find_package(DL)
    find_package(Demangle)
    find_package(Sigaction)
    find_package(Sigaltstack)
    find_package(SelfDbg)
    find_package(Ucontext)
    find_package(BFD)
    find_package(Fcitx)
    if (Fcitx_FOUND)
        find_package(DBus1)
        find_package(X11)
    endif(Fcitx_FOUND)
endif (UNIX)

if (MINGW)
    find_package(Demangle)
    find_package(BFD)
    find_package(DbgHelp)
endif (MINGW)

find_package(SSE)
find_package(Xaudio2)

find_package(Grfcodec)

# IPO is only properly supported from CMake 3.9. Despite the fact we are
# CMake 3.5, still enable IPO if we detect we are 3.9+.
if(POLICY CMP0069)
    cmake_policy(SET CMP0069 NEW)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_FOUND)
endif()

show_options()

if(UNIX AND NOT APPLE AND NOT OPTION_DEDICATED)
    if(NOT SDL_FOUND AND NOT SDL2_FOUND)
        message(FATAL_ERROR "SDL or SDL2 is required for this platform")
    endif()
endif()
if(APPLE)
    if(NOT AUDIOTOOLBOX_LIBRARY)
        message(FATAL_ERROR "AudioToolbox is required for this platform")
    endif()
    if(NOT AUDIOUNIT_LIBRARY)
        message(FATAL_ERROR "AudioUnit is required for this platform")
    endif()
    if(NOT COCOA_LIBRARY)
        message(FATAL_ERROR "Cocoa is required for this platform")
    endif()
endif()

if(MSVC)
    # C++17 for MSVC
    set(CMAKE_CXX_STANDARD 17)
else()
    # C++11 for all other targets
    set(CMAKE_CXX_STANDARD 11)
endif()

set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

set(CMAKE_EXPORT_COMPILE_COMMANDS YES)

list(APPEND GENERATED_SOURCE_FILES "${CMAKE_BINARY_DIR}/generated/rev.cpp")
if(WIN32)
    list(APPEND GENERATED_SOURCE_FILES "${CMAKE_BINARY_DIR}/generated/ottdres.rc")
endif()

include(SourceList)
include(Endian)
add_endian_definition()

# Needed by rev.cpp
include_directories(${CMAKE_SOURCE_DIR}/src)
# Needed by everything that uses Squirrel
include_directories(${CMAKE_SOURCE_DIR}/src/3rdparty/squirrel/include)

include(MSVCFilters)

include(CompileFlags)
compile_flags()

add_executable(openttd WIN32 ${GENERATED_SOURCE_FILES})
set_target_properties(openttd PROPERTIES OUTPUT_NAME "${BINARY_NAME}")
# All other files are added via target_sources()

set(host_tools_list strgen settingsgen)

if(CMAKE_CROSSCOMPILING)
    # Pawn off the creation of the host utilities into its own dedicated space
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/host_tools)
    file(TO_NATIVE_PATH ${CMAKE_COMMAND} native_cmake_command)
    file(TO_NATIVE_PATH ${CMAKE_CURRENT_SOURCE_DIR} native_cmake_current_source_dir)
    execute_process(
        COMMAND "${native_cmake_command}" "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}" "${native_cmake_current_source_dir}"
                "-DCMAKE_C_COMPILER=/usr/bin/cc" "-DCMAKE_CXX_COMPILER=/usr/bin/c++"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/host_tools
    )

    add_custom_target(host_tools
        COMMAND ${CMAKE_COMMAND} --build . --target host_tools --config $<CONFIG>
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/host_tools
    )
    include(${CMAKE_CURRENT_BINARY_DIR}/host_tools/host_tools.cmake)

    foreach(tgt IN ITEMS ${host_tools_list})
        add_dependencies(host${tgt} host_tools)
    endforeach()

else()
    # Add an empty target, host tools are built inplace
    add_custom_target(host_tools
        DEPENDS ${host_tools_list}
    )
endif()

include(AddCustomXXXTimestamp)
add_subdirectory(${CMAKE_SOURCE_DIR}/src)
add_subdirectory(${CMAKE_SOURCE_DIR}/media/baseset)
add_subdirectory(${CMAKE_SOURCE_DIR}/bin)

if(NOT CMAKE_CROSSCOMPILING)
    foreach(tgt IN ITEMS ${host_tools_list})
        add_executable(host${tgt} ALIAS ${tgt})
    endforeach()

    export(TARGETS ${host_tools_list} NAMESPACE host FILE host_tools.cmake)
endif()

add_dependencies(openttd
    find_version)

target_link_libraries(openttd
    openttd::languages
    openttd::settings
    openttd::basesets
    openttd::script_api
    openttd::binfiles
    Threads::Threads
)

if(IPO_FOUND)
    set_target_properties(openttd PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE True)
    set_target_properties(openttd PROPERTIES INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL True)
    set_target_properties(openttd PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO True)
endif()
set_target_properties(openttd PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
process_compile_flags()

if(APPLE OR UNIX)
    add_definitions(-DUNIX)
endif()

include(LinkPackage)
link_package(PNG TARGET PNG::PNG ENCOURAGED)
link_package(ZLIB TARGET ZLIB::ZLIB ENCOURAGED)
link_package(LIBLZMA TARGET LibLZMA::LibLZMA ENCOURAGED)
link_package(LZO ENCOURAGED)
link_package(XDG_basedir)

if(NOT OPTION_DEDICATED)
    link_package(Fluidsynth)
    link_package(SDL)
    link_package(SDL2 TARGET SDL2::SDL2)
    link_package(Allegro)
    link_package(FREETYPE TARGET Freetype::Freetype)
    link_package(Fontconfig TARGET Fontconfig::Fontconfig)
    link_package(ICU_lx)
    link_package(ICU_i18n)
    link_package(Fcitx)
    link_package(DBus1)
    link_package(X11)
endif()

if(APPLE)
    link_package(Iconv TARGET Iconv::Iconv)

    target_link_libraries(openttd
        ${AUDIOTOOLBOX_LIBRARY}
        ${AUDIOUNIT_LIBRARY}
        ${COCOA_LIBRARY}
    )

    add_definitions(
        -DWITH_COCOA
        -DENABLE_COCOA_QUARTZ
    )
endif()

if(NOT PERSONAL_DIR STREQUAL "(not set)")
    add_definitions(
        -DWITH_PERSONAL_DIR
        -DPERSONAL_DIR="${PERSONAL_DIR}"
    )
endif()

if(NOT SHARED_DIR STREQUAL "(not set)")
    add_definitions(
        -DWITH_SHARED_DIR
        -DSHARED_DIR="${SHARED_DIR}"
    )
endif()

if(NOT GLOBAL_DIR STREQUAL "(not set)")
    add_definitions(
        -DGLOBAL_DATA_DIR="${GLOBAL_DIR}"
    )
endif()

link_package(SSE)

add_definitions_based_on_options()

if(WIN32)
    add_definitions(
        -DUNICODE
        -D_UNICODE
        -DWITH_UNISCRIBE
    )

    target_link_libraries(openttd
        ws2_32
        winmm
        imm32
        usp10
    )
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_definitions(-D_SQ64)
endif()

include(CreateRegression)
create_regression()

include(InstallAndPackage)

get_property(CFG_DEFS DIRECTORY . PROPERTY COMPILE_OPTIONS)
list(FILTER CFG_DEFS INCLUDE REGEX "^-D")

# list TRANSFORM requires 3.12 or later
#list(TRANSFORM CFG_DEFS REPLACE "^-D" "")
string(REGEX REPLACE "(^|;)-D" "\\1" CFG_DEFS "${CFG_DEFS}")

get_property(CFG_DEFS_2 DIRECTORY . PROPERTY COMPILE_DEFINITIONS)
list(APPEND CFG_DEFS ${CFG_DEFS_2})
list(FILTER CFG_DEFS EXCLUDE REGEX "_DIR=")

# Generate a target to determine version, which is execute every 'make' run
add_custom_target(find_version
        ${CMAKE_COMMAND}
                -DFIND_VERSION_BINARY_DIR=${CMAKE_BINARY_DIR}/generated
                -DCPACK_BINARY_DIR=${CMAKE_BINARY_DIR}
                -DCONFIGURE_DEFINES="${CFG_DEFS}"
                $<$<PLATFORM_ID:Windows>:-DWIN32=TRUE>
                -P "${CMAKE_SOURCE_DIR}/cmake/scripts/FindVersion.cmake"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        BYPRODUCTS ${GENERATED_SOURCE_FILES}
)
