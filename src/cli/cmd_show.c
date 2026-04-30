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

#define COMMAND_NAME "show"

static char *object_arg;

static const cli_opt_spec opts[] = {
	CLI_COMMON_OPT,

	{ CLI_OPT_TYPE_ARG, "object", 0, &object_arg, 0,
	  CLI_OPT_USAGE_DEFAULT, "object", "object to show" },
	{ 0 }
};

static void print_help(void)
{
	cli_opt_usage_fprint(stdout, PROGRAM_NAME, COMMAND_NAME, opts, 0);
	printf("\n");
	printf("Show various types of objects.\n");
	printf("\n");
	printf("Options:\n");
	cli_opt_help_fprint(stdout, opts);
}

static void format_date(const git_time *when, char *buf, size_t buflen)
{
	snprintf(buf, buflen, "%" PRId64 " %+03d%02d",
		when->time, when->offset / 60, abs(when->offset % 60));
}

static int show_commit(git_repository *repo, const git_oid *oid)
{
	git_commit *commit = NULL;
	git_tree *tree = NULL, *parent_tree = NULL;
	git_diff *diff = NULL;
	const git_signature *author;
	const char *msg;
	char oid_str[GIT_OID_SHA1_SIZE + 1];
	char date_buf[64];
	int ret = 0;

	if (git_commit_lookup(&commit, repo, oid) < 0)
		return cli_error_git();

	author = git_commit_author(commit);
	msg = git_commit_message(commit);

	git_oid_tostr(oid_str, sizeof(oid_str), git_commit_id(commit));
	format_date(&author->when, date_buf, sizeof(date_buf));

	printf("commit %s\n", oid_str);
	printf("Author: %s <%s>\n", author->name, author->email);
	printf("Date:   %s\n", date_buf);
	printf("\n");

	/* Indent message by 4 spaces */
	{
		const char *p = msg;
		while (*p) {
			if (p == msg || *(p - 1) == '\n')
				printf("    ");
			if (*p == '\n' && *(p + 1) == '\0')
				break;
			putchar(*p++);
		}
		printf("\n");
	}

	/* Diff */
	if (git_commit_tree(&tree, commit) < 0) {
		ret = cli_error_git();
		goto done;
	}

	if (git_commit_parentcount(commit) > 0) {
		git_commit *parent = NULL;
		if (git_commit_parent(&parent, commit, 0) < 0 ||
		    git_commit_tree(&parent_tree, parent) < 0) {
			git_commit_free(parent);
			ret = cli_error_git();
			goto done;
		}
		git_commit_free(parent);

		if (git_diff_tree_to_tree(&diff, repo, parent_tree, tree, NULL) < 0) {
			ret = cli_error_git();
			goto done;
		}
	} else {
		if (git_diff_tree_to_tree(&diff, repo, NULL, tree, NULL) < 0) {
			ret = cli_error_git();
			goto done;
		}
	}

	{
		git_buf buf = GIT_BUF_INIT;
		if (git_diff_to_buf(&buf, diff, GIT_DIFF_FORMAT_PATCH) < 0) {
			git_buf_dispose(&buf);
			ret = cli_error_git();
			goto done;
		}
		if (buf.ptr)
			printf("%s", buf.ptr);
		git_buf_dispose(&buf);
	}

done:
	git_diff_free(diff);
	git_tree_free(parent_tree);
	git_tree_free(tree);
	git_commit_free(commit);
	return ret;
}

int cmd_show(int argc, char **argv)
{
	git_repository *repo = NULL;
	git_object *obj = NULL;
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

	if (!object_arg)
		object_arg = "HEAD";

	if (git_revparse_single(&obj, repo, object_arg) < 0) {
		ret = cli_error_git();
		goto done;
	}

	switch (git_object_type(obj)) {
	case GIT_OBJECT_COMMIT:
		ret = show_commit(repo, git_object_id(obj));
		break;
	default:
		fprintf(stderr, "git2: unsupported object type\n");
		ret = 1;
		break;
	}

done:
	git_object_free(obj);
	git_repository_free(repo);
	return ret;
}
