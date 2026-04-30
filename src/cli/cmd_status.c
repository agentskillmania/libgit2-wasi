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

#define COMMAND_NAME "status"

static int short_format;
static char **pathspecs;

static const cli_opt_spec opts[] = {
	CLI_COMMON_OPT,

	{ CLI_OPT_TYPE_SWITCH, "short", 's', &short_format, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "show output in short format" },
	{ CLI_OPT_TYPE_LITERAL },
	{ CLI_OPT_TYPE_ARGS, "pathspec", 0, &pathspecs, 0,
	  CLI_OPT_USAGE_DEFAULT, "pathspec", "limit to specific paths" },
	{ 0 }
};

static void print_help(void)
{
	cli_opt_usage_fprint(stdout, PROGRAM_NAME, COMMAND_NAME, opts, 0);
	printf("\n");
	printf("Show the working tree status.\n");
	printf("\n");
	printf("Options:\n");
	cli_opt_help_fprint(stdout, opts);
}

static const char *status_letter_index(unsigned int s)
{
	if (s & GIT_STATUS_INDEX_NEW)        return "A";
	if (s & GIT_STATUS_INDEX_MODIFIED)   return "M";
	if (s & GIT_STATUS_INDEX_DELETED)    return "D";
	if (s & GIT_STATUS_INDEX_RENAMED)    return "R";
	if (s & GIT_STATUS_INDEX_TYPECHANGE) return "T";
	return " ";
}

static const char *status_letter_wt(unsigned int s)
{
	if (s & GIT_STATUS_WT_MODIFIED)      return "M";
	if (s & GIT_STATUS_WT_DELETED)       return "D";
	if (s & GIT_STATUS_WT_TYPECHANGE)    return "T";
	if (s & GIT_STATUS_WT_RENAMED)       return "R";
	if (s & GIT_STATUS_WT_NEW)           return "?";
	if (s & GIT_STATUS_WT_UNREADABLE)    return "!";
	if (s & GIT_STATUS_IGNORED)          return "!";
	if (s & GIT_STATUS_CONFLICTED)       return "U";
	return " ";
}

static const char *entry_path(const git_status_entry *e)
{
	if (e->index_to_workdir)
		return e->index_to_workdir->new_file.path;
	if (e->head_to_index)
		return e->head_to_index->new_file.path;
	return "?";
}

static int print_short(git_status_list *statuses)
{
	size_t i, count = git_status_list_entrycount(statuses);

	for (i = 0; i < count; i++) {
		const git_status_entry *e = git_status_byindex(statuses, i);
		unsigned int s = e->status;
		const char *path = entry_path(e);

		if (s & GIT_STATUS_WT_NEW)
			printf("?? %s\n", path);
		else if (s & GIT_STATUS_IGNORED)
			printf("!! %s\n", path);
		else
			printf("%s%s %s\n",
				status_letter_index(s),
				status_letter_wt(s), path);
	}

	return 0;
}

static int print_long(git_repository *repo, git_status_list *statuses)
{
	git_reference *head = NULL;
	size_t i, count = git_status_list_entrycount(statuses);
	int has_staged = 0, has_unstaged = 0, has_untracked = 0;

	/* Branch / commit info */
	if (git_repository_head(&head, repo) == 0) {
		printf("On branch %s\n", git_reference_shorthand(head));
	} else if (git_repository_head_unborn(repo)) {
		printf("On branch main\n\nNo commits yet\n");
	} else {
		printf("On branch main\n");
	}
	git_reference_free(head);

	/* Categorize */
	for (i = 0; i < count; i++) {
		unsigned int s = git_status_byindex(statuses, i)->status;
		if (s & (GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED |
		         GIT_STATUS_INDEX_DELETED | GIT_STATUS_INDEX_RENAMED |
		         GIT_STATUS_INDEX_TYPECHANGE))
			has_staged = 1;
		if (s & (GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_DELETED |
		         GIT_STATUS_WT_TYPECHANGE | GIT_STATUS_WT_RENAMED))
			has_unstaged = 1;
		if (s & GIT_STATUS_WT_NEW)
			has_untracked = 1;
	}

	/* Staged */
	if (has_staged) {
		printf("\nChanges to be committed:\n");
		printf("  (use \"git restore --staged <file>...\" to unstage)\n");
		for (i = 0; i < count; i++) {
			const git_status_entry *e = git_status_byindex(statuses, i);
			unsigned int s = e->status;
			const char *label, *path;
			if (!(s & (GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED |
			           GIT_STATUS_INDEX_DELETED | GIT_STATUS_INDEX_RENAMED |
			           GIT_STATUS_INDEX_TYPECHANGE)))
				continue;
			if (s & GIT_STATUS_INDEX_NEW)        label = "new file:";
			else if (s & GIT_STATUS_INDEX_MODIFIED)   label = "modified:";
			else if (s & GIT_STATUS_INDEX_DELETED)    label = "deleted:";
			else if (s & GIT_STATUS_INDEX_RENAMED)    label = "renamed:";
			else                                      label = "typechange:";
			path = entry_path(e);
			printf("\t%s   %s\n", label, path);
		}
	}

	/* Unstaged */
	if (has_unstaged) {
		printf("\nChanges not staged for commit:\n");
		printf("  (use \"git add <file>...\" to update what will be committed)\n");
		for (i = 0; i < count; i++) {
			const git_status_entry *e = git_status_byindex(statuses, i);
			unsigned int s = e->status;
			const char *label, *path;
			if (!(s & (GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_DELETED |
			           GIT_STATUS_WT_TYPECHANGE | GIT_STATUS_WT_RENAMED)))
				continue;
			if (s & GIT_STATUS_WT_MODIFIED)    label = "modified:";
			else if (s & GIT_STATUS_WT_DELETED) label = "deleted:";
			else if (s & GIT_STATUS_WT_RENAMED) label = "renamed:";
			else                               label = "typechange:";
			path = e->index_to_workdir ? e->index_to_workdir->new_file.path :
				(e->head_to_index ? e->head_to_index->new_file.path : "?");
			printf("\t%s   %s\n", label, path);
		}
	}

	/* Untracked */
	if (has_untracked) {
		printf("\nUntracked files:\n");
		printf("  (use \"git add <file>...\" to include in what will be committed)\n");
		for (i = 0; i < count; i++) {
			const git_status_entry *e = git_status_byindex(statuses, i);
			if (!(e->status & GIT_STATUS_WT_NEW))
				continue;
			printf("\t%s\n", entry_path(e));
		}
	}

	if (!has_staged && !has_unstaged && !has_untracked)
		printf("\nnothing to commit\n");

	return 0;
}

int cmd_status(int argc, char **argv)
{
	git_repository *repo = NULL;
	git_status_list *statuses = NULL;
	cli_repository_open_options open_opts = { argv + 1, argc - 1 };
	git_status_options status_opts = GIT_STATUS_OPTIONS_INIT;
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

	status_opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
	status_opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
	                    GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

	if (git_status_list_new(&statuses, repo, &status_opts) < 0) {
		ret = cli_error_git();
		goto done;
	}

	if (short_format)
		ret = print_short(statuses);
	else
		ret = print_long(repo, statuses);

done:
	git_status_list_free(statuses);
	git_repository_free(repo);
	return ret;
}
