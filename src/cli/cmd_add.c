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

#define COMMAND_NAME "add"

static int verbose, force, all_flag, update_flag;
static char **pathspecs;

static const cli_opt_spec opts[] = {
	CLI_COMMON_OPT,

	{ CLI_OPT_TYPE_SWITCH, "verbose", 'v', &verbose, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "be verbose" },
	{ CLI_OPT_TYPE_SWITCH, "force",   'f', &force, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "allow adding otherwise ignored files" },
	{ CLI_OPT_TYPE_SWITCH, "all",     'A', &all_flag, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "add changes from all tracked and untracked files" },
	{ CLI_OPT_TYPE_SWITCH, "update",  'u', &update_flag, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "update tracked files" },
	{ CLI_OPT_TYPE_LITERAL },
	{ CLI_OPT_TYPE_ARGS, "pathspec", 0, &pathspecs, 0,
	  CLI_OPT_USAGE_DEFAULT, "pathspec", "files to add" },
	{ 0 }
};

static void print_help(void)
{
	cli_opt_usage_fprint(stdout, PROGRAM_NAME, COMMAND_NAME, opts, 0);
	printf("\n");
	printf("Add file contents to the index.\n");
	printf("\n");
	printf("Options:\n");
	cli_opt_help_fprint(stdout, opts);
}

/*
 * Manually stage a file: read it, write blob to ODB, create index entry.
 * This avoids git_index_add_bypath which crashes on WASI due to
 * write_file_stream's fake_wstream path.
 */
static int stage_file(git_index *index, git_repository *repo, const char *path)
{
	git_odb *odb = NULL;
	git_oid blob_id;
	git_str content = GIT_STR_INIT;
	git_index_entry entry = {{0}};
	int error;

	if (git_repository_odb(&odb, repo) < 0)
		return -1;

	if (git_futils_readbuffer(&content, path) < 0) {
		git_odb_free(odb);
		return -1;
	}

	error = git_odb_write(&blob_id, odb, content.ptr, content.size,
		GIT_OBJECT_BLOB);
	git_str_dispose(&content);
	git_odb_free(odb);

	if (error < 0)
		return error;

	/* Build a minimal index entry */
	entry.path = path;
	entry.id = blob_id;
	entry.mode = GIT_FILEMODE_BLOB;
	entry.flags = 0; /* stage 0 = normal entry */

	return git_index_add(index, &entry);
}

int cmd_add(int argc, char **argv)
{
	git_repository *repo = NULL;
	git_index *index = NULL;
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

	if (git_repository_index(&index, repo) < 0) {
		ret = cli_error_git();
		goto done;
	}

	if (all_flag || update_flag) {
		git_strarray arr = { NULL, 0 };

		if (update_flag) {
			if (git_index_update_all(index, &arr, NULL, NULL) < 0)
				ret = cli_error_git();
		} else {
			if (git_index_add_all(index, &arr, 0, NULL, NULL) < 0)
				ret = cli_error_git();
		}
	} else {
		char **p;
		if (!pathspecs || !pathspecs[0]) {
			ret = cli_error_usage("Nothing specified, nothing added.");
			goto done;
		}
		for (p = pathspecs; *p; p++) {
			if (stage_file(index, repo, *p) < 0) {
				ret = cli_error_git();
				goto done;
			}
			if (verbose)
				printf("add '%s'\n", *p);
		}
	}

	if (ret == 0 && git_index_write(index) < 0)
		ret = cli_error_git();

done:
	git_index_free(index);
	git_repository_free(repo);
	return ret;
}
