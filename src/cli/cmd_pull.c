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
#include "progress.h"

#define COMMAND_NAME "pull"

static int quiet;
static char *remote_name_arg;
static cli_progress progress = CLI_PROGRESS_INIT;

static const cli_opt_spec opts[] = {
	CLI_COMMON_OPT,

	{ CLI_OPT_TYPE_SWITCH, "quiet", 'q', &quiet, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "suppress progress" },
	{ CLI_OPT_TYPE_ARG, "repository", 0, &remote_name_arg, 0,
	  CLI_OPT_USAGE_DEFAULT, "repository", "remote to pull from" },
	{ 0 }
};

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
	printf("Fetch from and integrate with another repository.\n");
	printf("\n");
	printf("Options:\n");
	cli_opt_help_fprint(stdout, opts);
}

int cmd_pull(int argc, char **argv)
{
	git_repository *repo = NULL;
	git_remote *remote = NULL;
	git_reference *head_ref = NULL, *remote_ref = NULL;
	git_annotated_commit *remote_head = NULL;
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

	/* Fetch */
	if (git_remote_lookup(&remote, repo, remote_name_arg) < 0) {
		ret = cli_error_git();
		goto done;
	}

	{
		git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
		fetch_opts.callbacks.certificate_check = cert_check_cb;
		if (!quiet)
			fetch_opts.callbacks.payload = &progress;
		if (git_remote_fetch(remote, NULL, &fetch_opts, "pull") < 0) {
			ret = cli_error_git();
			goto done;
		}
	}

	/* Merge: get remote branch and try fast-forward */
	{
		char remote_head_name[256];
		const char *branch_name;
		git_merge_analysis_t analysis;
		git_merge_preference_t preference;

		if (git_repository_head(&head_ref, repo) < 0) {
			ret = cli_error_git();
			goto done;
		}

		branch_name = git_reference_shorthand(head_ref);

		snprintf(remote_head_name, sizeof(remote_head_name), "refs/remotes/%s/%s",
			remote_name_arg, branch_name);

		if (git_reference_lookup(&remote_ref, repo,
				remote_head_name) < 0) {
			if (!quiet)
				printf("Already up to date.\n");
			goto done;
		}

		if (git_annotated_commit_from_ref(&remote_head, repo,
				remote_ref) < 0) {
			ret = cli_error_git();
			goto done;
		}

		if (git_merge_analysis(&analysis, &preference, repo,
				(const git_annotated_commit **)&remote_head, 1) < 0) {
			ret = cli_error_git();
			goto done;
		}

		if (analysis & GIT_MERGE_ANALYSIS_FASTFORWARD) {
			git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
			checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;

			if (git_checkout_tree(repo,
					(git_object *)git_annotated_commit_id(remote_head),
					&checkout_opts) < 0 ||
			    git_reference_set_target(&head_ref, head_ref,
					git_annotated_commit_id(remote_head),
					"pull: fast-forward") < 0) {
				ret = cli_error_git();
				goto done;
			}
			if (!quiet)
				printf("Fast-forward\n");
		} else if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE) {
			if (!quiet)
				printf("Already up to date.\n");
		} else {
			fprintf(stderr,
				"git2: cannot fast-forward, merge required (not supported)\n");
			ret = 1;
		}
	}

done:
	git_annotated_commit_free(remote_head);
	git_reference_free(remote_ref);
	git_reference_free(head_ref);
	cli_progress_dispose(&progress);
	git_remote_free(remote);
	git_repository_free(repo);
	return ret;
}
