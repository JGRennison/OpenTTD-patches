include(CheckCXXSourceCompiles)

check_cxx_source_compiles("
    #include <unistd.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <sys/syscall.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    int main() {
        pid_t tid = syscall(SYS_gettid);
        int status;
        waitpid((pid_t) 0, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status);
    }"
    DBG_GDB_FOUND
)

if (DBG_GDB_FOUND)
    add_compile_options(
        -DWITH_DBG_GDB
    )
endif (DBG_GDB_FOUND)

check_cxx_source_compiles("
    #include <sys/prctl.h>
    int main() {
        return prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
    }"
    PRCTL_PT_FOUND
)

if (PRCTL_PT_FOUND)
    add_compile_options(
        -DWITH_PRCTL_PT
    )
endif (PRCTL_PT_FOUND)
