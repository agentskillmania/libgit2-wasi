/*
 * Copyright (C) the libgit2 contributors. All rights reserved.
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */

#include <stdio.h>
#include <git2.h>
#include "common.h"
#include "cmd.h"
#include "error.h"
#include "progress.h"

#define COMMAND_NAME "fetch"

static int quiet;
static char *remote_name_arg;
static cli_progress progress = CLI_PROGRESS_INIT;

static const cli_opt_spec opts[] = {
	CLI_COMMON_OPT,

	{ CLI_OPT_TYPE_SWITCH, "quiet", 'q', &quiet, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "suppress progress" },
	{ CLI_OPT_TYPE_ARG, "repository", 0, &remote_name_arg, 0,
	  CLI_OPT_USAGE_DEFAULT, "repository", "remote name (default: origin)" },
	{ 0 }
};

/* WASI has no system CA bundle, accept all certificates. */
static int cert_check_cb(git_cert *cert, int valid, const char *host, void *payload)
{
	GIT_UNUSED(cert);
	GIT_UNUSED(valid);
	GIT_UNUSED(host);
	GIT_UNUSED(payload);
	return 0;
}

static void print_help(void)
{
	cli_opt_usage_fprint(stdout, PROGRAM_NAME, COMMAND_NAME, opts, 0);
	printf("\n");
	printf("Download objects and refs from another repository.\n");
	printf("\n");
	printf("Options:\n");
	cli_opt_help_fprint(stdout, opts);
}

int cmd_fetch(int argc, char **argv)
{
	git_repository *repo = NULL;
	git_remote *remote = NULL;
	git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
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

	if (!remote_name_arg)
		remote_name_arg = "origin";

	if (git_remote_lookup(&remote, repo, remote_name_arg) < 0) {
		ret = cli_error_git();
		goto done;
	}

	fetch_opts.callbacks.certificate_check = cert_check_cb;

	if (!quiet) {
		fetch_opts.callbacks.payload = &progress;
	}

	if (git_remote_fetch(remote, NULL, &fetch_opts, "fetch") < 0) {
		ret = cli_error_git();
		goto done;
	}

	if (!quiet) {
		const git_transfer_progress *stats = git_remote_stats(remote);
		printf("From %s\n", git_remote_url(remote));
		if (stats->received_objects > 0)
			printf(" * [new ref]     %u objects received\n",
				(unsigned)stats->received_objects);
		else
			printf("Already up to date.\n");
	}

done:
	cli_progress_dispose(&progress);
	git_remote_free(remote);
	git_repository_free(repo);
	return ret;
}
