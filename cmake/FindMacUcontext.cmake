include(CheckCXXSourceCompiles)

check_cxx_source_compiles("
#include <sys/ucontext.h>
int main() {
    ucontext_t context;
#if defined(__x86_64__)
    void *ptr = (void *) context.uc_mcontext->__ss.__rip;
#elif defined(__i386)
    void *ptr = (void *) context.uc_mcontext->__ss.__rip;
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
