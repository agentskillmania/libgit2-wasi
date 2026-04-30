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

#define COMMAND_NAME "branch"

static int delete_branch, list_branches;
static char *branch_name;

static const cli_opt_spec opts[] = {
	CLI_COMMON_OPT,

	{ CLI_OPT_TYPE_SWITCH, "delete", 'd', &delete_branch, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "delete a branch" },
	{ CLI_OPT_TYPE_SWITCH, "list",   0,  &list_branches,  1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "list branches" },
	{ CLI_OPT_TYPE_ARG, "branch", 0, &branch_name, 0,
	  CLI_OPT_USAGE_DEFAULT, "branch", "branch name" },
	{ 0 }
};

static void print_help(void)
{
	cli_opt_usage_fprint(stdout, PROGRAM_NAME, COMMAND_NAME, opts, 0);
	printf("\n");
	printf("List, create, or delete branches.\n");
	printf("\n");
	printf("Options:\n");
	cli_opt_help_fprint(stdout, opts);
}

static int list_branches_cmd(git_repository *repo)
{
	git_branch_iterator *iter = NULL;
	git_reference *ref = NULL;
	git_branch_t type;
	int ret = 0;

	if (git_branch_iterator_new(&iter, repo, GIT_BRANCH_LOCAL) < 0)
		return cli_error_git();

	while (git_branch_next(&ref, &type, iter) == 0) {
		const char *name = git_reference_shorthand(ref);
		int is_head = (git_branch_is_head(ref) == 1);
		printf("%s %s\n", is_head ? "*" : " ", name);
		git_reference_free(ref);
		ref = NULL;
	}

	git_reference_free(ref);
	git_branch_iterator_free(iter);
	return ret;
}

static int create_branch(git_repository *repo, const char *name)
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

	if (git_branch_create(&ref, repo, name, commit, 0) < 0) {
		ret = cli_error_git();
		goto done;
	}

done:
	git_reference_free(ref);
	git_commit_free(commit);
	git_reference_free(head);
	return ret;
}

static int delete_branch_cmd(git_repository *repo, const char *name)
{
	git_reference *ref = NULL;
	char oid_str[8];

	if (git_branch_lookup(&ref, repo, name, GIT_BRANCH_LOCAL) < 0)
		return cli_error_git();

	if (git_branch_is_head(ref)) {
		fprintf(stderr, "git2: cannot delete branch '%s' checked out\n", name);
		git_reference_free(ref);
		return 1;
	}

	git_oid_tostr(oid_str, sizeof(oid_str), git_reference_target(ref));
	printf("Deleted branch %s (was %s).\n", name, oid_str);

	if (git_branch_delete(ref) < 0) {
		git_reference_free(ref);
		return cli_error_git();
	}

	git_reference_free(ref);
	return 0;
}

int cmd_branch(int argc, char **argv)
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

	if (delete_branch && branch_name) {
		ret = delete_branch_cmd(repo, branch_name);
	} else if (branch_name) {
		ret = create_branch(repo, branch_name);
	} else {
		ret = list_branches_cmd(repo);
	}

	git_repository_free(repo);
	return ret;
}
