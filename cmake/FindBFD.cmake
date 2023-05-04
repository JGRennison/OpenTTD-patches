include(CheckCXXSourceCompiles)

macro(test_compile_libbfd var libs)
    if (BFD_FOUND)
        return()
    endif ()

    set(CMAKE_REQUIRED_LIBRARIES ${libs})

    check_cxx_source_compiles("
        #define PACKAGE 1
        #define PACKAGE_VERSION 1
        #include <bfd.h>
        #include <unistd.h>
        int main() {
            bfd_init();
            bfd *abfd = bfd_openr(\"test\", \"test\");
            bfd_check_format(abfd, bfd_object);
            bfd_get_file_flags(abfd);
            bfd_map_over_sections(abfd, (void (*)(bfd*, asection*, void*)) 0, (void *) 0);
            asymbol *syms = 0;
            long symcount = bfd_read_minisymbols(abfd, false, (void**) &syms, (unsigned int *) 0);
            bfd_get_section_flags(abfd, (asection*) 0);
            bfd_get_section_vma(abfd, (asection*) 0);
            bfd_section_size(abfd, (asection*) 0);
            bfd_find_nearest_line(abfd, (asection*) 0, (asymbol **) 0, (bfd_vma) 0, (const char **) 0, (const char **) 0, (unsigned int *) 0);
            return (int) symcount;
        }"
        ${var}0
    )

    check_cxx_source_compiles("
        #define PACKAGE 1
        #define PACKAGE_VERSION 1
        #include <bfd.h>
        #include <unistd.h>
        int main() {
            bfd_init();
            bfd *abfd = bfd_openr(\"test\", \"test\");
            bfd_check_format(abfd, bfd_object);
            bfd_get_file_flags(abfd);
            bfd_map_over_sections(abfd, (void (*)(bfd*, asection*, void*)) 0, (void *) 0);
            asymbol *syms = 0;
            long symcount = bfd_read_minisymbols(abfd, false, (void**) &syms, (unsigned int *) 0);
            bfd_section_flags((asection*) 0);
            bfd_section_vma((asection*) 0);
            bfd_section_size((asection*) 0);
            bfd_find_nearest_line(abfd, (asection*) 0, (asymbol **) 0, (bfd_vma) 0, (const char **) 0, (const char **) 0, (unsigned int *) 0);
            return (int) symcount;
        }"
        ${var}1
    )

    if (${var}0)
        set(BFD_FOUND ON)
        add_compile_options(
            -DWITH_BFD0
        )
        link_libraries(${libs})
    elseif (${var}1)
        set(BFD_FOUND ON)
        add_compile_options(
            -DWITH_BFD1
        )
        link_libraries(${libs})
    endif ()

    if (BFD_FOUND OR UNIX)
        if (NOT (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
            if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
                add_compile_options(-gline-tables-only)
                set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -gline-tables-only")
            elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
                add_compile_options(-g1)
                set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -g1")
            endif ()
        endif ()
    endif ()

    set(CMAKE_REQUIRED_LIBRARIES "")
endmacro()

test_compile_libbfd("BFD_FOUND_A" "-lbfd;-lz")
test_compile_libbfd("BFD_FOUND_B" "-lbfd;-liberty;-lz")
test_compile_libbfd("BFD_FOUND_C" "-lbfd;-liberty;-lintl;-lz")
