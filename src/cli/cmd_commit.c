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

#define COMMAND_NAME "commit"

static char *message, *author_name, *author_email;
static int quiet, amend;

static const cli_opt_spec opts[] = {
	CLI_COMMON_OPT,

	{ CLI_OPT_TYPE_VALUE, "message", 'm', &message, 0,
	  CLI_OPT_USAGE_DEFAULT, "message", "commit message" },
	{ CLI_OPT_TYPE_VALUE, "author",  0,  &author_name, 0,
	  CLI_OPT_USAGE_HIDDEN, "name", "override commit author name" },
	{ CLI_OPT_TYPE_SWITCH, "quiet",  'q', &quiet, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "suppress commit summary" },
	{ CLI_OPT_TYPE_SWITCH, "amend",  0,  &amend, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "amend previous commit" },
	{ 0 }
};

static void print_help(void)
{
	cli_opt_usage_fprint(stdout, PROGRAM_NAME, COMMAND_NAME, opts, 0);
	printf("\n");
	printf("Record changes to the repository.\n");
	printf("\n");
	printf("Options:\n");
	cli_opt_help_fprint(stdout, opts);
}

int cmd_commit(int argc, char **argv)
{
	git_repository *repo = NULL;
	git_index *index = NULL;
	git_oid tree_id, commit_id;
	git_signature *sig = NULL;
	git_tree *tree = NULL;
	git_commit *parent = NULL;
	git_reference *ref = NULL;
	git_commit *head_commit = NULL;
	char message_buf[4096];
	const char *commit_message;
	cli_repository_open_options open_opts = { argv + 1, argc - 1 };
	cli_opt invalid_opt;
	int ret = 0, is_initial = 0;

	message = NULL;
	author_name = NULL;
	author_email = NULL;
	quiet = 0;
	amend = 0;

	if (cli_opt_parse(&invalid_opt, opts, argv + 1, argc - 1, CLI_OPT_PARSE_GNU))
		return cli_opt_usage_error(COMMAND_NAME, opts, &invalid_opt);

	if (cli_opt__show_help) {
		print_help();
		return 0;
	}

	commit_message = message;

	if (cli_repository_open(&repo, &open_opts) < 0)
		return cli_error_git();

	if (!commit_message && amend) {
		git_reference *head_ref = NULL;
		if (git_repository_head(&head_ref, repo) == 0) {
			if (git_commit_lookup(&head_commit, repo,
			        git_reference_target(head_ref)) == 0) {
				const char *msg = git_commit_message(head_commit);
				if (msg) {
					strncpy(message_buf, msg, sizeof(message_buf) - 1);
					message_buf[sizeof(message_buf) - 1] = '\0';
					commit_message = message_buf;
				}
			}
		}
		git_reference_free(head_ref);
	}

	if (!commit_message) {
		git_commit_free(head_commit);
		return cli_error_usage("switch `m' requires a value");
	}

	if (git_repository_index(&index, repo) < 0) {
		ret = cli_error_git();
		goto done;
	}

	/* Check for staged changes */
	if (git_index_entrycount(index) == 0) {
		ret = cli_error("nothing to commit");
		goto done;
	}

	/* Write tree from index */
	if (git_index_write_tree(&tree_id, index) < 0) {
		ret = cli_error_git();
		goto done;
	}

	/* For non-initial commits, check that tree differs from HEAD */
	if (!amend && !git_repository_head_unborn(repo)) {
		git_reference *head_ref = NULL;
		git_commit *head_commit = NULL;
		git_tree *head_tree = NULL;
		if (git_repository_head(&head_ref, repo) == 0) {
			if (git_commit_lookup(&head_commit, repo,
			        git_reference_target(head_ref)) == 0) {
				if (git_commit_tree(&head_tree, head_commit) == 0) {
					if (git_oid_equal(&tree_id,
					        git_tree_id(head_tree))) {
						git_tree_free(head_tree);
						git_commit_free(head_commit);
						git_reference_free(head_ref);
						ret = cli_error(
						    "nothing to commit");
						goto done;
					}
				}
				git_tree_free(head_tree);
				git_commit_free(head_commit);
			}
			git_reference_free(head_ref);
		}
	}

	if (git_tree_lookup(&tree, repo, &tree_id) < 0) {
		ret = cli_error_git();
		goto done;
	}

	/* Get signature */
	if (git_signature_default(&sig, repo) < 0) {
		ret = cli_error_git();
		goto done;
	}

	/* Determine if initial commit */
	is_initial = (git_repository_head_unborn(repo) == 1);

	if (is_initial) {
		/* Initial commit -- no parent */
		if (git_commit_create_v(&commit_id, repo, "HEAD", sig, sig,
		        NULL, commit_message, tree, 0) < 0) {
			ret = cli_error_git();
			goto done;
		}
	} else if (amend) {
		/* Amend: get current HEAD commit */
		if (git_repository_head(&ref, repo) < 0) {
			ret = cli_error_git();
			goto done;
		}
		if (git_commit_lookup(&parent, repo, git_reference_target(ref)) < 0) {
			ret = cli_error_git();
			goto done;
		}
		if (git_commit_create_v(&commit_id, repo, "HEAD", sig, sig,
		        NULL, commit_message, tree, 1, parent) < 0) {
			ret = cli_error_git();
			goto done;
		}
	} else {
		/* Normal commit with parent */
		if (git_repository_head(&ref, repo) < 0) {
			ret = cli_error_git();
			goto done;
		}
		if (git_commit_lookup(&parent, repo, git_reference_target(ref)) < 0) {
			ret = cli_error_git();
			goto done;
		}
		if (git_commit_create_v(&commit_id, repo, "HEAD", sig, sig,
		        NULL, commit_message, tree, 1, parent) < 0) {
			ret = cli_error_git();
			goto done;
		}
	}

	/* Write index to disk */
	git_index_write(index);

	if (!quiet) {
		const char *branch = "main";
		char short_oid[8];
		git_oid_tostr(short_oid, sizeof(short_oid), &commit_id);
		if (ref)
			branch = git_reference_shorthand(ref);
		else if (!is_initial) {
			git_reference *h = NULL;
			if (git_repository_head(&h, repo) == 0)
				branch = git_reference_shorthand(h);
			git_reference_free(h);
		}
		printf("[%s %s] %s\n", branch, short_oid, commit_message);
	}

done:
	git_reference_free(ref);
	git_commit_free(parent);
	git_commit_free(head_commit);
	git_tree_free(tree);
	git_signature_free(sig);
	git_index_free(index);
	git_repository_free(repo);
	return ret;
}
