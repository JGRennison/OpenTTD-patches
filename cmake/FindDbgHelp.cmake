include(CheckCXXSourceCompiles)

check_cxx_source_compiles("
    #include <windows.h>
    #include <dbghelp.h>
    int main() {
        STACKFRAME64 frame;
        IMAGEHLP_SYMBOL64 *sym_info;
        IMAGEHLP_MODULE64 module;
        IMAGEHLP_LINE64 line;
        return 0;
    }"
    DBGHELP_FOUND
)

if (DBGHELP_FOUND)
    add_compile_options(
        -DWITH_DBGHELP
    )
endif (DBGHELP_FOUND)
