include(CheckCXXSourceCompiles)

check_cxx_source_compiles("
    #include <signal.h>
    void *addr;
    int code;
    void handler(int sig, siginfo_t *si, void *context) {
        addr = si->si_addr;
        code = si->si_code;
    }
    int main() {
        struct sigaction sa;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sa.sa_sigaction = handler;
        sigaction(SIGSEGV, &sa, 0);
        return 0;
    }"
    SIGACTION_FOUND
)

if (SIGACTION_FOUND)
    add_compile_options(
        -DWITH_SIGACTION
    )
endif (SIGACTION_FOUND)
