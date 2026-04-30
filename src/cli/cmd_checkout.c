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

#define COMMAND_NAME "checkout"

static int create_branch;
static char **args;

static const cli_opt_spec opts[] = {
	CLI_COMMON_OPT,

	{ CLI_OPT_TYPE_SWITCH, "branch", 'b', &create_branch, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "create and switch to new branch" },
	{ CLI_OPT_TYPE_ARGS, "args", 0, &args, 0,
	  CLI_OPT_USAGE_DEFAULT, "args", "branch name or file paths" },
	{ 0 }
};

static void print_help(void)
{
	cli_opt_usage_fprint(stdout, PROGRAM_NAME, COMMAND_NAME, opts, 0);
	printf("\n");
	printf("Switch branches or restore working tree files.\n");
	printf("\n");
	printf("Options:\n");
	cli_opt_help_fprint(stdout, opts);
}

static int switch_branch(git_repository *repo, const char *branch_name)
{
	git_reference *ref = NULL;
	git_object *obj = NULL;
	git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
	int ret = 0;

	checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;

	/* Try local branch first */
	if (git_branch_lookup(&ref, repo, branch_name, GIT_BRANCH_LOCAL) < 0) {
		git_reference_free(ref);
		ref = NULL;

		/* Try as a ref/commit */
		if (git_revparse_single(&obj, repo, branch_name) < 0) {
			fprintf(stderr, "git2: pathspec '%s' did not match any known branch or commit\n",
				branch_name);
			return 1;
		}

		if (git_checkout_tree(repo, obj, &checkout_opts) < 0) {
			ret = cli_error_git();
			goto done;
		}

		/* Detach HEAD at this commit */
		if (git_repository_set_head_detached(repo,
				git_object_id(obj)) < 0) {
			ret = cli_error_git();
			goto done;
		}

		printf("HEAD is now at %s\n", branch_name);
		goto done;
	}

	/* Branch found — lookup commit, checkout, switch HEAD */
	{
		git_commit *commit = NULL;
		if (git_commit_lookup(&commit, repo,
				git_reference_target(ref)) < 0) {
			ret = cli_error_git();
			git_commit_free(commit);
			goto done;
		}
		if (git_checkout_tree(repo, (git_object *)commit,
				&checkout_opts) < 0) {
			ret = cli_error_git();
			git_commit_free(commit);
			goto done;
		}
		git_commit_free(commit);
	}

	if (git_repository_set_head(repo,
			git_reference_name(ref)) < 0) {
		ret = cli_error_git();
		goto done;
	}

	printf("Switched to branch '%s'\n", branch_name);

done:
	git_object_free(obj);
	git_reference_free(ref);
	return ret;
}

static int create_and_switch(git_repository *repo, const char *branch_name)
{
	git_reference *ref = NULL, *head = NULL;
	git_commit *commit = NULL;
	git_oid head_oid;
	int ret = 0;

	if (git_repository_head(&head, repo) < 0)
		return cli_error_git();

	head_oid = *git_reference_target(head);

	if (git_commit_lookup(&commit, repo, &head_oid) < 0) {
		ret = cli_error_git();
		goto done;
	}

	if (git_branch_create(&ref, repo, branch_name, commit, 0) < 0) {
		ret = cli_error_git();
		goto done;
	}

done:
	git_reference_free(ref);
	git_commit_free(commit);
	git_reference_free(head);

	if (ret != 0)
		return ret;

	return switch_branch(repo, branch_name);
}

static int restore_files(git_repository *repo, char **paths)
{
	git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
	git_strarray patharray = { NULL, 0 };
	size_t count = 0, i;
	int ret = 0;

	while (paths[count])
		count++;

	patharray.strings = git__calloc(count, sizeof(char *));
	if (!patharray.strings)
		return cli_error_git();
	patharray.count = count;

	for (i = 0; i < count; i++)
		patharray.strings[i] = paths[i];

	checkout_opts.checkout_strategy = GIT_CHECKOUT_FORCE;
	checkout_opts.paths = patharray;

	if (git_checkout_head(repo, &checkout_opts) < 0)
		ret = cli_error_git();

	git__free(patharray.strings);
	return ret;
}

int cmd_checkout(int argc, char **argv)
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

	if (!args || !args[0]) {
		return cli_error_usage("no branch or file specified");
	}

	if (cli_repository_open(&repo, &open_opts) < 0)
		return cli_error_git();

	if (create_branch) {
		ret = create_and_switch(repo, args[0]);
	} else if (strcmp(args[0], "--") == 0) {
		/* git checkout -- <file>... : restore files from HEAD */
		if (!args[1]) {
			ret = cli_error_usage("no file specified after --");
		} else {
			ret = restore_files(repo, args + 1);
		}
	} else {
		ret = switch_branch(repo, args[0]);
	}

	git_repository_free(repo);
	return ret;
}
