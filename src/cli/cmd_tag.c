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

#define COMMAND_NAME "tag"

static int delete_tag, annotate;
static char *tag_name, *message;

static const cli_opt_spec opts[] = {
	CLI_COMMON_OPT,

	{ CLI_OPT_TYPE_SWITCH, "annotate", 'a', &annotate, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "create an annotated tag" },
	{ CLI_OPT_TYPE_VALUE, "message", 'm', &message, 0,
	  CLI_OPT_USAGE_DEFAULT, "message", "tag message" },
	{ CLI_OPT_TYPE_SWITCH, "delete", 'd', &delete_tag, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "delete a tag" },
	{ CLI_OPT_TYPE_ARG, "tagname", 0, &tag_name, 0,
	  CLI_OPT_USAGE_DEFAULT, "tagname", "tag name" },
	{ 0 }
};

static void print_help(void)
{
	cli_opt_usage_fprint(stdout, PROGRAM_NAME, COMMAND_NAME, opts, 0);
	printf("\n");
	printf("List, create, or delete tags.\n");
	printf("\n");
	printf("Options:\n");
	cli_opt_help_fprint(stdout, opts);
}

static int list_tags(git_repository *repo)
{
	git_strarray tags;
	size_t i;

	if (git_tag_list(&tags, repo) < 0)
		return cli_error_git();

	for (i = 0; i < tags.count; i++)
		printf("%s\n", tags.strings[i]);

	git_strarray_dispose(&tags);
	return 0;
}

static int create_tag(git_repository *repo, const char *name)
{
	git_reference *head = NULL;
	git_object *target = NULL;
	git_signature *tagger = NULL;
	git_oid tag_id;
	int ret = 0;

	if (git_repository_head(&head, repo) < 0)
		return cli_error_git();

	if (git_revparse_single(&target, repo, "HEAD") < 0) {
		ret = cli_error_git();
		goto done;
	}

	if (annotate || message) {
		if (git_signature_default(&tagger, repo) < 0) {
			ret = cli_error_git();
			goto done;
		}
		if (git_tag_create(&tag_id, repo, name, target, tagger,
		    message ? message : name, 0) < 0) {
			ret = cli_error_git();
			goto done;
		}
	} else {
		if (git_tag_create_lightweight(&tag_id, repo, name, target, 0) < 0) {
			ret = cli_error_git();
			goto done;
		}
	}

done:
	git_signature_free(tagger);
	git_object_free(target);
	git_reference_free(head);
	return ret;
}

static int delete_tag_cmd(git_repository *repo, const char *name)
{
	if (git_tag_delete(repo, name) < 0)
		return cli_error_git();
	printf("Deleted tag '%s'\n", name);
	return 0;
}

int cmd_tag(int argc, char **argv)
{
	git_repository *repo = NULL;
	cli_repository_open_options open_opts = { argv + 1, argc - 1 };
	cli_opt invalid_opt;
	int ret = 0;

	delete_tag = 0;
	annotate = 0;
	tag_name = NULL;
	message = NULL;

	if (cli_opt_parse(&invalid_opt, opts, argv + 1, argc - 1, CLI_OPT_PARSE_GNU))
		return cli_opt_usage_error(COMMAND_NAME, opts, &invalid_opt);

	if (cli_opt__show_help) {
		print_help();
		return 0;
	}

	if (cli_repository_open(&repo, &open_opts) < 0)
		return cli_error_git();

	if (delete_tag && tag_name)
		ret = delete_tag_cmd(repo, tag_name);
	else if (tag_name)
		ret = create_tag(repo, tag_name);
	else
		ret = list_tags(repo);

	git_repository_free(repo);
	return ret;
}
