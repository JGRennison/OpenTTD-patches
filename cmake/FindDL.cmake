include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_LIBRARIES "dl")

check_cxx_source_compiles("
    #include <dlfcn.h>
    int main() {
        Dl_info info;
        return dladdr(0, &info);
    }"
    DL_FOUND
)

check_cxx_source_compiles("
    #include <dlfcn.h>
    #include <link.h>
    int main() {
        Dl_info info;
        struct link_map *lm = nullptr;
        return dladdr1(0, &info, (void **)&lm, RTLD_DL_LINKMAP);
    }"
    DL_FOUND2
)

if (DL_FOUND)
    add_compile_options(
        -DWITH_DL
    )
    link_libraries(dl)
    if (DL_FOUND2)
        add_compile_options(
            -DWITH_DL2
        )
    endif (DL_FOUND2)
endif (DL_FOUND)

set(CMAKE_REQUIRED_LIBRARIES "")
