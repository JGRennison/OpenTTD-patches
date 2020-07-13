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

if (DL_FOUND)
    add_compile_options(
        -DWITH_DL
    )
    link_libraries(dl)
endif (DL_FOUND)

set(CMAKE_REQUIRED_LIBRARIES "")
