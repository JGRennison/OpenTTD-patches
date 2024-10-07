# Macro which contains all bits to setup the compile flags correctly.
#
# compile_flags()
#
macro(compile_flags)
    if(MSVC)
        # "If /Zc:rvalueCast is specified, the compiler follows section 5.4 of the
        # C++11 standard". We need C++11 for the way we use threads.
        add_compile_options(/Zc:rvalueCast)

        # Needed for __VA_OPT__() in macros.
        add_compile_options(/Zc:preprocessor)

        if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            add_compile_options(
                /MP # Enable multi-threaded compilation.
                /FC # Display the full path of source code files passed to the compiler in diagnostics.
            )
        endif()
    endif()

    # Our strings are UTF-8.
    if(MSVC)
        add_compile_options(/utf-8)
    else()
        add_compile_options(-finput-charset=utf-8)
    endif()

    # Add some -D flags for Debug builds. We cannot use add_definitions(), because
    # it does not appear to support the $<> tags.
    add_compile_options(
        "$<$<CONFIG:Debug>:-D_DEBUG>"
        "$<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>" # FORTIFY_SOURCE should only be used in non-debug builds (requires -O1+)
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR OPTION_DBG_ASSERTS)
        add_compile_options(-DDBG_ASSERTS)
    endif()
    if(MINGW)
        add_link_options(
            "$<$<NOT:$<CONFIG:Debug>>:-fstack-protector>" # Prevent undefined references when _FORTIFY_SOURCE > 0
        )
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            add_compile_options(
                "$<$<CONFIG:Debug>:-Wa,-mbig-obj>" # Switch to pe-bigobj-x86-64 as x64 Debug builds push pe-x86-64 to the limits (linking errors with ASLR, ...)
            )
        endif()
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            # Fix MinGW's incorrect assumption that the incoming stack at function calls is 16-byte aligned
            # The Win32 API/calling convention only requires and guarantees 4-byte alignment, leading to alignment problems with SSE/AVX/etc
            add_compile_options(-mincoming-stack-boundary=2 -mpreferred-stack-boundary=2)
        endif()
        add_compile_options(-Wno-stringop-overflow) # This warning false-positives on some MinGW versions so just turn it off
    endif()

    # Prepare a generator that checks if we are not a debug, and don't have asserts
    # on. We need this later on to set some compile options for stable releases.
    #set(IS_STABLE_RELEASE "$<AND:$<NOT:$<CONFIG:Debug>>,$<NOT:$<BOOL:${OPTION_USE_ASSERTS}>>>")

    if(MSVC)
        add_compile_options(
            /W3
            #/w34100 # 'identifier' : unreferenced formal parameter
            /w34189 # 'identifier' : local variable is initialized but not referenced
        )
        if(MSVC_VERSION GREATER 1929 AND MSVC_VERSION LESS 1937 AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            # Starting with version 19.30 (fixed in version 19.37), there is an optimisation bug, see #9966 for details
            # This flag disables the broken optimisation to work around the bug
            add_compile_options(/d2ssa-rse-)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        add_compile_options(
            -W
            -Wall
            -Wcast-qual
            -Wextra
            -Wsign-compare
            -Wundef
            -Wpointer-arith
            -Wwrite-strings
            -Wredundant-decls
            -Wformat-security
            -Wformat=2
            -Winit-self
            "$<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>"
            "$<$<COMPILE_LANGUAGE:CXX>:-Wsuggest-override>"

            # Often parameters are unused, which is fine.
            -Wno-unused-parameter
            # We use 'ABCD' multichar for SaveLoad chunks identifiers
            -Wno-multichar
        )

        if(OPTION_TRIM_PATH_PREFIX)
            add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}/=/")
        endif(OPTION_TRIM_PATH_PREFIX)

        if(NOT CMAKE_BUILD_TYPE)
            # Sensible default if no build type specified
            add_compile_options(-O2)
            if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
                add_compile_options(-DNDEBUG)
            endif()
        endif(NOT CMAKE_BUILD_TYPE)

        # Ninja processes the output so the output from the compiler
        # isn't directly to a terminal; hence, the default is
        # non-coloured output. We can override this to get nicely
        # coloured output, but since that might yield odd results with
        # IDEs, we extract it to an option.
        if(OPTION_FORCE_COLORED_OUTPUT)
            if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
                add_compile_options (-fdiagnostics-color=always)
            elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
                add_compile_options (-fcolor-diagnostics)
            endif()
        endif()

        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            include(CheckCXXCompilerFlag)
            check_cxx_compiler_flag("-flifetime-dse=1" LIFETIME_DSE_FOUND)

            add_compile_options(
                # GCC 4.2+ automatically assumes that signed overflows do
                # not occur in signed arithmetics, whereas we are not
                # sure that they will not happen. It furthermore complains
                # about its own optimized code in some places.
                "-fno-strict-overflow"

                # Prevent optimisation supposing enums are in a range specified by the standard
                # For details, see http://gcc.gnu.org/PR43680
                "-fno-tree-vrp"

                # -flifetime-dse=2 (default since GCC 6) doesn't play
                # well with our custom pool item allocator
                "$<$<BOOL:${LIFETIME_DSE_FOUND}>:-flifetime-dse=1>"

                # We have a fight between clang wanting std::move() and gcc not wanting it
                # and of course they both warn when the other compiler is happy
                "$<$<COMPILE_LANGUAGE:CXX>:-Wno-redundant-move>"
            )

            if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 11)
                add_compile_options(
                    # GCC >= 11 has false positives if operator new is inlined but operator delete isn't, or vice versa
                    "-Wno-mismatched-new-delete"
                )
            endif()
        endif()

        if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
            if (NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
                add_compile_options(
                    -fno-stack-check
                )

                include(CheckCXXCompilerFlag)
                check_cxx_compiler_flag("-mno-sse4" NO_SSE4_FOUND)

                if(NO_SSE4_FOUND)
                    add_compile_options(
                        # Don't use SSE4 for general sources to increase compatibility.
                        -mno-sse4
                    )
                endif()
            endif()
        endif()

        if(OPTION_COMPRESS_DEBUG)
            include(CheckCXXCompilerFlag)
            check_cxx_compiler_flag("-gz" GZ_FOUND)

            if(GZ_FOUND)
                # Compress debug sections.
                add_compile_options(-gz)
                set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -gz")
            endif()
        endif(OPTION_COMPRESS_DEBUG)

        if(OPTION_LTO)
            include(CheckCXXCompilerFlag)
            check_cxx_compiler_flag("-flto" LTO_FOUND)

            if(LTO_FOUND)
                # Enable LTO.
                add_compile_options(-flto)
                set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto")
            endif()
        endif(OPTION_LTO)

        if (OPTION_NO_WARN_UNINIT)
            add_compile_options(-Wno-maybe-uninitialized -Wno-uninitialized)
        endif (OPTION_NO_WARN_UNINIT)

        if (EMSCRIPTEN)
            add_compile_options(-Wno-deprecated-builtins)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
        add_compile_options(
            -Wall
            # warning #873: function ... ::operator new ... has no corresponding operator delete ...
            -wd873
            # warning #1292: unknown attribute "fallthrough"
            -wd1292
            # warning #1899: multicharacter character literal (potential portability problem)
            -wd1899
            # warning #2160: anonymous union qualifier is ignored
            -wd2160
        )
    else()
        message(FATAL_ERROR "No warning flags are set for this compiler yet; please consider creating a Pull Request to add support for this compiler.")
    endif()

    if(NOT WIN32 AND NOT HAIKU)
        # rdynamic is used to get useful stack traces from crash reports.
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")
    endif()

    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        # workaround for MacOS 10.13 and below which does not support std::variant, etc
        add_definitions(-D_LIBCPP_DISABLE_AVAILABILITY)
    endif()
endmacro()
