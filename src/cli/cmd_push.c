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
#include "progress.h"

#define COMMAND_NAME "push"

static int quiet, set_upstream;
static char *remote_name_arg, *refspec_arg, *username, *password;
static cli_progress progress = CLI_PROGRESS_INIT;

static const cli_opt_spec opts[] = {
	CLI_COMMON_OPT,

	{ CLI_OPT_TYPE_SWITCH, "quiet", 'q', &quiet, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "suppress progress" },
	{ CLI_OPT_TYPE_SWITCH, "set-upstream", 'u', &set_upstream, 1,
	  CLI_OPT_USAGE_DEFAULT, NULL, "set upstream for git pull/status" },
	{ CLI_OPT_TYPE_VALUE,  "username", 0, &username, 0,
	  CLI_OPT_USAGE_DEFAULT, "user", "HTTP auth username" },
	{ CLI_OPT_TYPE_VALUE,  "password", 0, &password, 0,
	  CLI_OPT_USAGE_DEFAULT, "pass", "HTTP auth password" },
	{ CLI_OPT_TYPE_ARG, "remote", 0, &remote_name_arg, 0,
	  CLI_OPT_USAGE_DEFAULT, "remote", "remote name" },
	{ CLI_OPT_TYPE_ARG, "refspec", 0, &refspec_arg, 0,
	  CLI_OPT_USAGE_DEFAULT, "refspec", "refspec to push" },
	{ 0 }
};

static int cert_check_cb(git_cert *cert, int valid, const char *host, void *payload)
{
	GIT_UNUSED(cert);
	GIT_UNUSED(valid);
	GIT_UNUSED(host);
	GIT_UNUSED(payload);
	return 0;
}

static int credential_cb(git_credential **out, const char *url,
	const char *user_from_url, unsigned int allowed_types, void *payload)
{
	const char *user = user_from_url ? user_from_url : username;
	GIT_UNUSED(url);
	GIT_UNUSED(allowed_types);
	GIT_UNUSED(payload);
	if (!user || !password)
		return GIT_EAUTH;
	return git_credential_userpass_plaintext_new(out, user, password);
}

static int do_set_upstream(git_repository *repo, const char *remote, const char *refspec)
{
	git_reference *branch_ref = NULL;
	const char *local_branch = NULL;
	char refname[256];
	char upstream_name[256];
	int ret = -1;

	if (!refspec || strcmp(refspec, "HEAD") == 0) {
		git_reference *head_ref = NULL;
		if (git_repository_head(&head_ref, repo) < 0)
			return -1;
		local_branch = git_reference_shorthand(head_ref);
		git_reference_free(head_ref);
	} else {
		local_branch = refspec;
		if (strncmp(local_branch, "refs/heads/", 11) == 0)
			local_branch += 11;
	}

	snprintf(refname, sizeof(refname), "refs/heads/%s", local_branch);
	snprintf(upstream_name, sizeof(upstream_name), "%s/%s", remote, local_branch);

	if (git_reference_lookup(&branch_ref, repo, refname) == 0) {
		if (git_branch_set_upstream(branch_ref, upstream_name) == 0)
			ret = 0;
		git_reference_free(branch_ref);
	}

	return ret;
}

static void print_help(void)
{
	cli_opt_usage_fprint(stdout, PROGRAM_NAME, COMMAND_NAME, opts, 0);
	printf("\n");
	printf("Update remote refs along with associated objects.\n");
	printf("\n");
	printf("Options:\n");
	cli_opt_help_fprint(stdout, opts);
}

int cmd_push(int argc, char **argv)
{
	git_repository *repo = NULL;
	git_remote *remote = NULL;
	git_push_options push_opts = GIT_PUSH_OPTIONS_INIT;
	cli_repository_open_options open_opts = { argv + 1, argc - 1 };
	cli_opt invalid_opt;
	git_strarray refspecs = { NULL, 0 };
	int ret = 0;

	quiet = 0;
	set_upstream = 0;
	remote_name_arg = NULL;
	refspec_arg = NULL;
	username = NULL;
	password = NULL;

	if (cli_opt_parse(&invalid_opt, opts, argv + 1, argc - 1, CLI_OPT_PARSE_GNU))
		return cli_opt_usage_error(COMMAND_NAME, opts, &invalid_opt);

	if (cli_opt__show_help) {
		print_help();
		return 0;
	}

	if (cli_repository_open(&repo, &open_opts) < 0)
		return cli_error_git();

	if (!remote_name_arg)
		remote_name_arg = "origin";

	if (git_remote_lookup(&remote, repo, remote_name_arg) < 0) {
		ret = cli_error_git();
		goto done;
	}

	push_opts.callbacks.certificate_check = cert_check_cb;
	push_opts.callbacks.credentials = credential_cb;

	if (!quiet)
		push_opts.callbacks.payload = &progress;

	if (refspec_arg) {
		static char full_refspec[512];
		if (strchr(refspec_arg, ':')) {
			char *specs[] = { refspec_arg };
			refspecs.strings = specs;
		} else if (strncmp(refspec_arg, "refs/", 5) == 0) {
			snprintf(full_refspec, sizeof(full_refspec), "%s:%s", refspec_arg, refspec_arg);
			char *specs[] = { full_refspec };
			refspecs.strings = specs;
		} else {
			snprintf(full_refspec, sizeof(full_refspec),
			         "refs/heads/%s:refs/heads/%s", refspec_arg, refspec_arg);
			char *specs[] = { full_refspec };
			refspecs.strings = specs;
		}
		refspecs.count = 1;
	} else {
		git_reference *head_ref = NULL;
		const char *branch_name;
		static char default_refspec[256];

		if (git_repository_head(&head_ref, repo) == 0) {
			branch_name = git_reference_shorthand(head_ref);
			snprintf(default_refspec, sizeof(default_refspec),
			         "refs/heads/%s:refs/heads/%s", branch_name, branch_name);
			char *specs[] = { default_refspec };
			refspecs.strings = specs;
			refspecs.count = 1;
			git_reference_free(head_ref);
		}
	}

	if (git_remote_push(remote, &refspecs, &push_opts) < 0) {
		ret = cli_error_git();
		goto done;
	}

	if (!quiet)
		printf("Everything up-to-date\n");

	if (set_upstream && ret == 0) {
		git_reference *head_ref = NULL;
		const char *branch_name = refspec_arg;

		if (do_set_upstream(repo, remote_name_arg, refspec_arg) == 0) {
			if (!branch_name || strcmp(branch_name, "HEAD") == 0) {
				if (git_repository_head(&head_ref, repo) == 0)
					branch_name = git_reference_shorthand(head_ref);
			}
			if (branch_name) {
				if (strncmp(branch_name, "refs/heads/", 11) == 0)
					branch_name += 11;
				printf("Branch '%s' set up to track '%s/%s'.\n",
				       branch_name, remote_name_arg, branch_name);
			}
		}
		git_reference_free(head_ref);
	}

done:
	cli_progress_dispose(&progress);
	git_remote_free(remote);
	git_repository_free(repo);
	return ret;
}
