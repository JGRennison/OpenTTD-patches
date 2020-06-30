include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "")

check_cxx_source_compiles("
    #include <cxxabi.h>
    int main() {
        int status = -1;
        char *demangled = abi::__cxa_demangle(\"test\", 0, 0, &status);
        return 0;
    }"
    DEMANGLE_FOUND
)

if (DEMANGLE_FOUND)
    add_compile_options(
        -DWITH_DEMANGLE
    )
endif (DEMANGLE_FOUND)
