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

#define COMMAND_NAME "reset"

static int soft, mixed, hard;
static char *commit_arg;
static char **paths;

static const cli_opt_spec opts[] = {
	CLI_COMMON_OPT,

	{ CLI_OPT_TYPE_SWITCH, "soft",  0, &soft,  1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "reset HEAD only" },
	{ CLI_OPT_TYPE_SWITCH, "mixed", 0, &mixed, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "reset HEAD and index (default)" },
	{ CLI_OPT_TYPE_SWITCH, "hard",  0, &hard,  1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "reset HEAD, index, and working tree" },
	{ CLI_OPT_TYPE_ARG, "commit", 0, &commit_arg, 0,
	  CLI_OPT_USAGE_DEFAULT, "commit", "commit to reset to" },
	{ CLI_OPT_TYPE_LITERAL },
	{ CLI_OPT_TYPE_ARGS, "path", 0, &paths, 0,
	  CLI_OPT_USAGE_DEFAULT, "path", "paths to reset" },
	{ 0 }
};

static int reset_paths(git_repository *repo, git_object *target, char **paths)
{
	git_commit *commit = NULL;
	git_tree *tree = NULL;
	git_index *index = NULL;
	char **p;
	int ret = 0;

	if (git_repository_index(&index, repo) < 0)
		return cli_error_git();

	if (git_object_type(target) == GIT_OBJECT_COMMIT) {
		commit = (git_commit *)target;
	} else {
		ret = cli_error("target is not a commit");
		goto done;
	}

	if (git_commit_tree(&tree, commit) < 0) {
		ret = cli_error_git();
		goto done;
	}

	for (p = paths; *p; p++) {
		git_tree_entry *entry = NULL;

		if (git_tree_entry_bypath(&entry, tree, *p) == 0) {
			git_index_entry idx_entry = {{0}};
			idx_entry.path = *p;
			idx_entry.id = *git_tree_entry_id(entry);
			idx_entry.mode = git_tree_entry_filemode(entry);
			if (git_index_add(index, &idx_entry) < 0) {
				git_tree_entry_free(entry);
				ret = cli_error_git();
				goto done;
			}
			git_tree_entry_free(entry);
		} else {
			if (git_index_remove_bypath(index, *p) < 0) {
				ret = cli_error_git();
				goto done;
			}
		}
	}

	if (git_index_write(index) < 0)
		ret = cli_error_git();

done:
	git_index_free(index);
	git_tree_free(tree);
	return ret;
}

static void print_help(void)
{
	cli_opt_usage_fprint(stdout, PROGRAM_NAME, COMMAND_NAME, opts, 0);
	printf("\n");
	printf("Reset current HEAD to the specified state.\n");
	printf("\n");
	printf("Options:\n");
	cli_opt_help_fprint(stdout, opts);
}

int cmd_reset(int argc, char **argv)
{
	git_repository *repo = NULL;
	git_object *target = NULL;
	git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
	git_reset_t reset_type;
	cli_repository_open_options open_opts = { argv + 1, argc - 1 };
	cli_opt invalid_opt;
	int ret = 0;

	soft = 0;
	mixed = 0;
	hard = 0;
	commit_arg = NULL;
	paths = NULL;

	if (cli_opt_parse(&invalid_opt, opts, argv + 1, argc - 1, CLI_OPT_PARSE_GNU))
		return cli_opt_usage_error(COMMAND_NAME, opts, &invalid_opt);

	if (cli_opt__show_help) {
		print_help();
		return 0;
	}

	if (cli_repository_open(&repo, &open_opts) < 0)
		return cli_error_git();

	if (!commit_arg)
		commit_arg = "HEAD";

	if (git_revparse_single(&target, repo, commit_arg) < 0) {
		ret = cli_error_git();
		goto done;
	}

	if (paths && paths[0]) {
		ret = reset_paths(repo, target, paths);
		goto done;
	}

	if (hard)
		reset_type = GIT_RESET_HARD;
	else if (soft)
		reset_type = GIT_RESET_SOFT;
	else
		reset_type = GIT_RESET_MIXED;

	if (hard) {
		checkout_opts.checkout_strategy = GIT_CHECKOUT_FORCE;
		if (git_reset(repo, target, reset_type, &checkout_opts) < 0)
			ret = cli_error_git();
	} else {
		if (git_reset(repo, target, reset_type, NULL) < 0)
			ret = cli_error_git();
	}

done:
	git_object_free(target);
	git_repository_free(repo);
	return ret;
}
