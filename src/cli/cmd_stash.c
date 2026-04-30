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

#define COMMAND_NAME "stash"

static char *subcommand;

static const cli_opt_spec opts[] = {
	CLI_COMMON_OPT,

	{ CLI_OPT_TYPE_ARG, "subcommand", 0, &subcommand, 0,
	  CLI_OPT_USAGE_DEFAULT, "subcommand", "subcommand (list, pop, drop)" },
	{ 0 }
};

static void print_help(void)
{
	cli_opt_usage_fprint(stdout, PROGRAM_NAME, COMMAND_NAME, opts, 0);
	printf("\n");
	printf("Stash the changes in a dirty working directory.\n");
	printf("\n");
	printf("Options:\n");
	cli_opt_help_fprint(stdout, opts);
}

static int stash_cb(size_t index, const char *msg, const git_oid *id, void *payload)
{
	char oid_str[8];
	GIT_UNUSED(payload);
	git_oid_tostr(oid_str, sizeof(oid_str), id);
	printf("stash@{%zu}: %s%s\n", index, msg ? "On " : "", msg ? msg : "");
	return 0;
}

static int do_stash_push(git_repository *repo)
{
	git_signature *sig = NULL;
	git_oid stash_id;
	int ret = 0;

	if (git_signature_default(&sig, repo) < 0)
		return cli_error_git();

	if (git_stash_save(&stash_id, repo, sig, NULL, GIT_STASH_DEFAULT) < 0) {
		ret = cli_error_git();
		goto done;
	}

	{
		char oid_str[8];
		git_oid_tostr(oid_str, sizeof(oid_str), &stash_id);
		printf("Saved working directory and index state %s\n", oid_str);
	}

done:
	git_signature_free(sig);
	return ret;
}

static int do_stash_list(git_repository *repo)
{
	return git_stash_foreach(repo, stash_cb, NULL) < 0 ? cli_error_git() : 0;
}

static int do_stash_pop(git_repository *repo)
{
	git_stash_apply_options apply_opts = GIT_STASH_APPLY_OPTIONS_INIT;
	int ret = 0;

	if (git_stash_pop(repo, 0, &apply_opts) < 0)
		ret = cli_error_git();
	else
		printf("Dropped refs/stash\n");

	return ret;
}

static int do_stash_drop(git_repository *repo)
{
	if (git_stash_drop(repo, 0) < 0)
		return cli_error_git();
	return 0;
}

int cmd_stash(int argc, char **argv)
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

	if (!subcommand || strcmp(subcommand, "push") == 0)
		ret = do_stash_push(repo);
	else if (strcmp(subcommand, "list") == 0)
		ret = do_stash_list(repo);
	else if (strcmp(subcommand, "pop") == 0)
		ret = do_stash_pop(repo);
	else if (strcmp(subcommand, "drop") == 0)
		ret = do_stash_drop(repo);
	else
		ret = cli_error_usage("unknown stash subcommand: '%s'", subcommand);

	git_repository_free(repo);
	return ret;
}
