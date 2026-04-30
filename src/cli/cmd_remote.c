/*
 * Copyright (C) the libgit2 contributors. All rights reserved.
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */

#include <stdio.h>
#include <string.h>
#include <git2.h>
#include "common.h"
#include "cmd.h"
#include "error.h"

#define COMMAND_NAME "remote"

static int verbose;
static char **args;

static const cli_opt_spec opts[] = {
	CLI_COMMON_OPT,

	{ CLI_OPT_TYPE_SWITCH, "verbose", 'v', &verbose, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "show remote URLs" },
	{ CLI_OPT_TYPE_ARGS, "args", 0, &args, 0,
	  CLI_OPT_USAGE_DEFAULT, "args", "subcommand and arguments" },
	{ 0 }
};

static void print_help(void)
{
	cli_opt_usage_fprint(stdout, PROGRAM_NAME, COMMAND_NAME, opts, 0);
	printf("\n");
	printf("Manage remote repositories.\n");
	printf("\n");
	printf("Options:\n");
	cli_opt_help_fprint(stdout, opts);
}

static int list_remotes(git_repository *repo, int show_url)
{
	git_strarray remotes;
	size_t i;

	if (git_remote_list(&remotes, repo) < 0)
		return cli_error_git();

	for (i = 0; i < remotes.count; i++) {
		if (show_url) {
			git_remote *remote = NULL;
			if (git_remote_lookup(&remote, repo, remotes.strings[i]) < 0) {
				printf("%s\n", remotes.strings[i]);
				continue;
			}
			printf("%s\t%s (fetch)\n", remotes.strings[i],
				git_remote_url(remote));
			printf("%s\t%s (push)\n", remotes.strings[i],
				git_remote_pushurl(remote) ?
				git_remote_pushurl(remote) :
				git_remote_url(remote));
			git_remote_free(remote);
		} else {
			printf("%s\n", remotes.strings[i]);
		}
	}

	git_strarray_dispose(&remotes);
	return 0;
}

static int add_remote(git_repository *repo, const char *name, const char *url)
{
	git_remote *remote = NULL;
	if (git_remote_create(&remote, repo, name, url) < 0)
		return cli_error_git();
	git_remote_free(remote);
	return 0;
}

static int remove_remote(git_repository *repo, const char *name)
{
	if (git_remote_delete(repo, name) < 0)
		return cli_error_git();
	return 0;
}

int cmd_remote(int argc, char **argv)
{
	git_repository *repo = NULL;
	cli_repository_open_options open_opts = { argv + 1, argc - 1 };
	cli_opt invalid_opt;
	int ret = 0;

	if (cli_opt_parse(&invalid_opt, opts, argv + 1, argc - 1, CLI_OPT_PARSE_GNU))
		return cli_opt_usage_error(COMMAND_NAME, opts, &invalid_opt);

	if (cli_opt__show_help) {
		print_help();
		return 0;
	}

	if (cli_repository_open(&repo, &open_opts) < 0)
		return cli_error_git();

	/* Parse subcommand from args */
	if (args && args[0]) {
		if (strcmp(args[0], "add") == 0 && args[1] && args[2]) {
			ret = add_remote(repo, args[1], args[2]);
		} else if (strcmp(args[0], "rm") == 0 && args[1]) {
			ret = remove_remote(repo, args[1]);
		} else if (strcmp(args[0], "remove") == 0 && args[1]) {
			ret = remove_remote(repo, args[1]);
		} else {
			ret = list_remotes(repo, verbose);
		}
	} else {
		ret = list_remotes(repo, verbose);
	}

	git_repository_free(repo);
	return ret;
}
