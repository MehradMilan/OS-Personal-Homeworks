#include <stdio.h>
#include <sys/resource.h>

int main() {
    struct rlimit lim;
    getrlimit(RLIMIT_STACK, &lim);
    printf("stack size: %lu\n", lim.rlim_cur);
    getrlimit(RLIMIT_NPROC, &lim);
    printf("process limit: %lu\n", lim.rlim_cur);
    getrlimit(RLIMIT_NOFILE, &lim);
    printf("max file descriptors: %lu\n", lim.rlim_cur);
    return 0;
}
