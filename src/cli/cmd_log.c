/*
 * Copyright (C) the libgit2 contributors. All rights reserved.
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <git2.h>
#include "common.h"
#include "cmd.h"
#include "error.h"

#define COMMAND_NAME "log"

static int oneline, max_count = -1;
static char *format, *max_count_str;
static char **args;

static const cli_opt_spec opts[] = {
	CLI_COMMON_OPT,

	{ CLI_OPT_TYPE_SWITCH, "oneline", 0, &oneline, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "shorthand for '--format=oneline'" },
	{ CLI_OPT_TYPE_VALUE,  "format",  0, &format, 0,
	  CLI_OPT_USAGE_DEFAULT, "format", "pretty-print format" },
	{ CLI_OPT_TYPE_VALUE,  "max-count", 'n', &max_count_str, 0,
	  CLI_OPT_USAGE_DEFAULT, "n", "limit number of commits" },
	{ CLI_OPT_TYPE_ARGS,   "args",    0, &args, 0,
	  CLI_OPT_USAGE_DEFAULT, "args", "revision range or commit" },
	{ 0 }
};

static void print_help(void)
{
	cli_opt_usage_fprint(stdout, PROGRAM_NAME, COMMAND_NAME, opts, 0);
	printf("\n");
	printf("Show commit logs.\n");
	printf("\n");
	printf("Options:\n");
	cli_opt_help_fprint(stdout, opts);
}

static void fmt_date(const git_time *when, char *buf, size_t buflen)
{
	time_t t = (time_t)(when->time + when->offset * 60);
	struct tm tm;
	gmtime_r(&t, &tm);
	snprintf(buf, buflen, "%.3s %.3s %d %02d:%02d:%02d %d %+03d%02d",
		"SunMonTueWedThuFriSat" + (tm.tm_wday * 3),
		"JanFebMarAprMayJunJulAugSepOctNovDec" + (tm.tm_mon * 3),
		tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
		tm.tm_year + 1900,
		when->offset / 60, abs(when->offset % 60));
}

static void print_commit_default(const git_commit *commit, const char *refs_str)
{
	char oid_str[GIT_OID_SHA1_SIZE + 1];
	char date_buf[64];
	const git_signature *author;
	const char *msg;
	const char *p;

	git_oid_tostr(oid_str, sizeof(oid_str), git_commit_id(commit));
	author = git_commit_author(commit);
	msg = git_commit_message(commit);

	fmt_date(&author->when, date_buf, sizeof(date_buf));

	printf("commit %s", oid_str);
	if (refs_str)
		printf(" (%s)", refs_str);
	printf("\n");
	printf("Author: %s <%s>\n", author->name, author->email);
	{
		const git_signature *committer = git_commit_committer(commit);
		if (committer)
			printf("Committer: %s <%s>\n", committer->name, committer->email);
	}
	printf("Date:   %s\n", date_buf);
	printf("\n");

	p = msg;
	while (*p) {
		if (p == msg || *(p - 1) == '\n')
			printf("    ");
		if (*p == '\n' && *(p + 1) == '\0')
			break;
		putchar(*p++);
	}
	printf("\n\n");
}

static void print_commit_oneline(const git_commit *commit, const char *refs_str)
{
	char oid_str[8];
	const char *msg;
	char msgbuf[256];
	size_t len;

	git_oid_tostr(oid_str, sizeof(oid_str), git_commit_id(commit));
	msg = git_commit_message(commit);

	strncpy(msgbuf, msg, sizeof(msgbuf) - 1);
	msgbuf[sizeof(msgbuf) - 1] = '\0';
	len = strlen(msgbuf);
	if (len > 0 && msgbuf[len - 1] == '\n')
		msgbuf[len - 1] = '\0';

	printf("%s", oid_str);
	if (refs_str)
		printf(" (%s)", refs_str);
	printf(" %s\n", msgbuf);
}

int cmd_log(int argc, char **argv)
{
	git_repository *repo = NULL;
	git_revwalk *walker = NULL;
	git_reference *head_ref = NULL;
	git_commit *commit = NULL;
	cli_repository_open_options open_opts = { argv + 1, argc - 1 };
	cli_opt invalid_opt;
	git_oid oid;
	int ret = 0, count = 0;

	oneline = 0;
	max_count = -1;
	max_count_str = NULL;
	format = NULL;
	args = NULL;

	if (cli_opt_parse(&invalid_opt, opts, argv + 1, argc - 1, CLI_OPT_PARSE_GNU))
		return cli_opt_usage_error(COMMAND_NAME, opts, &invalid_opt);

	if (cli_opt__show_help) {
		print_help();
		return 0;
	}

	if (max_count_str)
		max_count = atoi(max_count_str);

	if (cli_repository_open(&repo, &open_opts) < 0)
		return cli_error_git();

	if (git_revwalk_new(&walker, repo) < 0) {
		ret = cli_error_git();
		goto done;
	}

	git_revwalk_sorting(walker, GIT_SORT_TIME);

	if (git_repository_head(&head_ref, repo) == 0) {
		/* head_ref obtained for decoration */
	}

	if (args && args[0]) {
		char *range = args[0];
		char *dots = strstr(range, "..");

		if (dots && dots != range && *(dots + 2) != '.') {
			/* Range syntax: left..right */
			git_object *left = NULL, *right = NULL;
			char *left_str = range;
			char *right_str = dots + 2;
			*dots = '\0';

			if (git_revparse_single(&left, repo, left_str) < 0 ||
			    git_revparse_single(&right, repo, right_str) < 0) {
				*dots = '.';
				ret = cli_error_git();
				goto done;
			}

			git_revwalk_hide(walker, git_object_id(left));
			git_revwalk_push(walker, git_object_id(right));
			git_object_free(left);
			git_object_free(right);
			*dots = '.';
		} else {
			/* Single commit */
			git_object *obj = NULL;
			if (git_revparse_single(&obj, repo, range) < 0) {
				ret = cli_error_git();
				goto done;
			}
			git_revwalk_push(walker, git_object_id(obj));
			git_object_free(obj);
		}
	} else if (head_ref) {
		oid = *git_reference_target(head_ref);
		git_revwalk_push(walker, &oid);
	} else {
		fprintf(stderr, "git2: your current branch does not have any commits yet\n");
		ret = 128;
		goto done;
	}

	while (git_revwalk_next(&oid, walker) == 0) {
		const char *ref_str = NULL;

		if (git_commit_lookup(&commit, repo, &oid) < 0) {
			ret = cli_error_git();
			goto done;
		}

		/* Show ref decoration on first commit */
		if (count == 0)
			ref_str = git_reference_shorthand(head_ref);

		if (oneline || (format && strcmp(format, "oneline") == 0))
			print_commit_oneline(commit, ref_str);
		else
			print_commit_default(commit, ref_str);

		git_commit_free(commit);
		commit = NULL;
		count++;

		if (max_count > 0 && count >= max_count)
			break;
	}

done:
	git_commit_free(commit);
	git_revwalk_free(walker);
	git_reference_free(head_ref);
	git_repository_free(repo);
	return ret;
}
