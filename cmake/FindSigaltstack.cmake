include(CheckCXXSourceCompiles)

check_cxx_source_compiles("
    #include <signal.h>
    #include <stdlib.h>
    int main() {
        stack_t ss;
        ss.ss_sp = calloc(SIGSTKSZ, 1);
        ss.ss_size = SIGSTKSZ;
        ss.ss_flags = 0;
        sigaltstack(&ss, nullptr);
        return 0;
    }"
    SIGALTSTACK
)

if (SIGALTSTACK_FOUND)
    add_compile_options(
        -DWITH_SIGALTSTACK
    )
endif (SIGALTSTACK_FOUND)
