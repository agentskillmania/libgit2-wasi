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

#define COMMAND_NAME "diff"

static int cached, stat_only;
static char **pathspecs;

static const cli_opt_spec opts[] = {
	CLI_COMMON_OPT,

	{ CLI_OPT_TYPE_SWITCH, "cached", 0, &cached, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "show staged changes" },
	{ CLI_OPT_TYPE_SWITCH, "stat",   0, &stat_only, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "show stat summary only" },
	{ CLI_OPT_TYPE_ARGS, "pathspec", 0, &pathspecs, 0,
	  CLI_OPT_USAGE_DEFAULT, "pathspec", "limit to paths" },
	{ 0 }
};

static void print_help(void)
{
	cli_opt_usage_fprint(stdout, PROGRAM_NAME, COMMAND_NAME, opts, 0);
	printf("\n");
	printf("Show changes between commits, commit and working tree, etc.\n");
	printf("\n");
	printf("Options:\n");
	cli_opt_help_fprint(stdout, opts);
}

int cmd_diff(int argc, char **argv)
{
	git_repository *repo = NULL;
	git_diff *diff = NULL;
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

	if (cached) {
		/* Staged: HEAD tree vs index */
		git_reference *head_ref = NULL;
		git_commit *head_commit = NULL;
		git_tree *head_tree = NULL;

		if (git_repository_head(&head_ref, repo) < 0) {
			/* No HEAD yet — show index vs empty */
			git_reference_free(head_ref);
			if (git_diff_index_to_workdir(&diff, repo, NULL, NULL) < 0) {
				ret = cli_error_git();
				goto done;
			}
		} else {
			if (git_commit_lookup(&head_commit, repo,
				    git_reference_target(head_ref)) < 0 ||
			    git_commit_tree(&head_tree, head_commit) < 0 ||
			    git_diff_tree_to_index(&diff, repo, head_tree, NULL, NULL) < 0) {
				git_tree_free(head_tree);
				git_commit_free(head_commit);
				git_reference_free(head_ref);
				ret = cli_error_git();
				goto done;
			}
			git_tree_free(head_tree);
			git_commit_free(head_commit);
			git_reference_free(head_ref);
		}
	} else {
		/* Working tree: index vs workdir */
		if (git_diff_index_to_workdir(&diff, repo, NULL, NULL) < 0) {
			ret = cli_error_git();
			goto done;
		}
	}

	if (stat_only) {
		git_diff_stats *stats = NULL;
		git_buf buf = GIT_BUF_INIT;
		if (git_diff_get_stats(&stats, diff) < 0 ||
		    git_diff_stats_to_buf(&buf, stats, GIT_DIFF_STATS_FULL, 80) < 0) {
			git_diff_stats_free(stats);
			ret = cli_error_git();
			goto done;
		}
		printf("%s\n", buf.ptr);
		git_buf_dispose(&buf);
		git_diff_stats_free(stats);
	} else {
		{
			git_buf buf = GIT_BUF_INIT;
			if (git_diff_to_buf(&buf, diff, GIT_DIFF_FORMAT_PATCH) == 0 && buf.ptr)
				printf("%s", buf.ptr);
			git_buf_dispose(&buf);
		}
	}

done:
	git_diff_free(diff);
	git_repository_free(repo);
	return ret;
}
