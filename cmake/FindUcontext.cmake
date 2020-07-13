include(CheckCXXSourceCompiles)

check_cxx_source_compiles("
#include <ucontext.h>
int main() {
    ucontext_t context;
#if defined(__x86_64__)
    void *ptr = (void *) context.uc_mcontext.gregs[REG_RIP];
#elif defined(__i386)
    void *ptr = (void *) context.uc_mcontext.gregs[REG_EIP];
#else
#error Unknown arch
#endif
    return 0;
}"
    UCONTEXT_FOUND
)

if (UCONTEXT_FOUND)
    add_compile_options(
        -DWITH_UCONTEXT
    )
endif (UCONTEXT_FOUND)
