/**
 * Component guest wrapper for libgit2 CLI.
 * Converts WIT git interface args to argc/argv and calls git2_cli_main.
 * Sets the guest's working directory from the host-provided cwd before execution.
 */
#include "guest_git.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Forward declaration - renamed main() in libgit2 CLI */
extern int git2_cli_main(int argc, char **argv);

int32_t exports_agentskillmania_subcommand_git_execute(
    guest_git_string_t *cwd,
    guest_git_list_string_t *args)
{
    /* Sync guest cwd with host */
    if (cwd->len > 0) {
        char *s = malloc(cwd->len + 1);
        if (s) {
            memcpy(s, cwd->ptr, cwd->len);
            s[cwd->len] = '\0';
            chdir(s);
            free(s);
        }
    }

    int argc = (int)args->len;
    if (argc == 0) return 1;

    char **argv = malloc(sizeof(char *) * (size_t)(argc + 1));
    if (!argv) return 1;

    for (size_t i = 0; i < (size_t)argc; i++) {
	size_t len = args->ptr[i].len;
	argv[i] = malloc(len + 1);
	if (!argv[i]) {
	    for (size_t j = 0; j < i; j++) free(argv[j]);
	    free(argv);
	    return 1;
	}
	memcpy(argv[i], args->ptr[i].ptr, len);
	argv[i][len] = '\0';
    }
    argv[argc] = NULL;

    int rc = git2_cli_main(argc, argv);

    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);
    return rc;
}
