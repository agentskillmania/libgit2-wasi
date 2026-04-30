/*
 * WASI stub for pwd.h.
 *
 * WASI has no user database. Provide minimal stubs for libgit2's sysdir.c
 * which uses getpwuid_r() to find the user's home directory.
 * All functions return "not found" / failure.
 */

#ifndef WASI_PWD_H
#define WASI_PWD_H

#include <sys/types.h>
#include <unistd.h>

struct passwd {
    char *pw_name;
    char *pw_dir;
};

static int getpwuid_r(uid_t uid, struct passwd *pwd,
                      char *buf, size_t buflen,
                      struct passwd **result)
{
    (void) uid;
    (void) pwd;
    (void) buf;
    (void) buflen;
    *result = NULL;
    return 0;
}

#endif /* WASI_PWD_H */
