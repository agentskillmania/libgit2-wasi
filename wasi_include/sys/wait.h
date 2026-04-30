/*
 * WASI stub for sys/wait.h.
 *
 * WASI has no process management. These stubs allow unix/process.c to compile
 * but all functions return errors at runtime.
 */

#ifndef WASI_SYS_WAIT_H
#define WASI_SYS_WAIT_H

#include <sys/types.h>

#define WNOHANG 1
#define WUNTRACED 2

#define WIFEXITED(s) ((s) != 0)
#define WEXITSTATUS(s) ((s) & 0xff)
#define WIFSIGNALED(s) 0
#define WTERMSIG(s) 0

static inline pid_t waitpid(pid_t pid, int *status, int options)
{
    (void) pid;
    (void) status;
    (void) options;
    return -1;
}

#endif /* WASI_SYS_WAIT_H */
