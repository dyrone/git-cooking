#include "builtin.h"
#include "gettext.h"
#include "parse-options.h"
#include "config.h"
#include "pathspec.h"
#include "setup.h"
#include "object-store.h"
#include "object-file.h"
#include "object-name.h"
#include "hex.h"
#include "attach.h"
#include "refs.h"
#include "tree-walk.h"
#include "tree.h"

static const char * const git_attach_usage[] = {
	N_("git attach add [--commitish] <file>"),
	NULL
};

static const char *const git_attach_add_usage[] = { 
	N_("git attach add [--commitish] <file>"),
	NULL
};

static int write_attach_ref(const char *ref, struct object_id *attach_tree,
			    const char *msg)
{
	const char *attachments_ref;
	struct object_id commit_oid;
	struct object_id parent_oid;
	struct tree_desc desc;
	struct name_entry entry;
	struct tree *tree = NULL;
	struct commit_list *parents = NULL;
	struct strbuf buf = STRBUF_INIT;

	if (!ref)
		attachments_ref = default_attachments_ref();

	if (!read_ref(attachments_ref, &parent_oid)) {
		struct commit *parent = lookup_commit(the_repository, &parent_oid);
		if (repo_parse_commit(the_repository, parent))
			die("Failed to find/parse commit %s", attachments_ref);
		commit_list_insert(parent, &parents);
	}

	tree = repo_get_commit_tree(the_repository,
				    lookup_commit(the_repository, &parent_oid));
	init_tree_desc(&desc, tree->buffer, tree->size);

	while (tree_entry(&desc, &entry)) {
		switch (object_type(entry.mode)) {
		case OBJ_TREE:
			fprintf(stderr, "entry.path: %s\n", entry.path);
			continue;
		default:
			continue;
		}
	}

	if (commit_tree(msg, strlen(msg), attach_tree, parents, &commit_oid,
			NULL, NULL))
		die(_("Failed to commit attachment tree to database"));

	strbuf_addstr(&buf, msg);
	update_ref(buf.buf, attachments_ref, &commit_oid, NULL, 0,
		   UPDATE_REFS_DIE_ON_ERR);

	strbuf_release(&buf);
	return 0;
}

/*
 *  * Returns 0 on success, -1 if no attachments, 1 on failure
 */
static int write_attach_tree(const struct pathspec *pathspec,
			     struct object_id *attach_tree,
			     struct object_id *attach_commit)
{
	struct strbuf file_buf = STRBUF_INIT;
	struct strbuf attach_tree_buf = STRBUF_INIT;
	struct dir_struct dir = DIR_INIT;
	struct object_id oid;
	struct stat st;
	unsigned flags = HASH_FORMAT_CHECK | HASH_WRITE_OBJECT;

	dir.flags |= DIR_SHOW_OTHER_DIRECTORIES | DIR_SHOW_IGNORED_TOO |
		     DIR_KEEP_UNTRACKED_CONTENTS;
	fill_directory(&dir, the_repository->index, pathspec);


	// 有没有现成的API解决这个问题， 将dir转换为tree？
	for (int i = 0; i < dir.nr; i++) {
		strbuf_reset(&file_buf);

		if (lstat(dir.entries[i]->name, &st))
			die_errno(_("fail to stat file '%s'"),
				  dir.entries[i]->name);

		if (!(S_ISREG(st.st_mode))) {
			warning(_("attachment is not a regular file, skipped '%s'"),
				dir.entries[i]->name);
			continue;
		}

		if (strbuf_read_file(&file_buf, dir.entries[i]->name, 0) < 0)
			die(_("unable to read filepath '%s'"),
			    dir.entries[i]->name);

		if (write_object_file_flags(file_buf.buf, file_buf.len,
					    OBJ_BLOB, &oid, flags))
			die(_("unable to add blob from %s to database"),
			    dir.entries[i]->name);

		strbuf_addf(&attach_tree_buf, "%o %s%c", 0100644,
			    dir.entries[i]->name, '\0');
		strbuf_add(&attach_tree_buf, oid.hash, the_hash_algo->rawsz);
	}

	if (attach_tree_buf.len <= 0)
		die(_("no attachments found in given pathspec"));
	else
		write_object_file(attach_tree_buf.buf, attach_tree_buf.len,
				  OBJ_TREE, attach_tree);

	strbuf_release(&file_buf);
	strbuf_release(&attach_tree_buf);
	return 0;
}

static int add(int argc, const char **argv, const char *prefix)
{
	struct pathspec pathspec;
	struct object_id attach_tree;
	struct object_id attach_commit;
	char *attach_commitish = NULL;
	char *attachments_msg = "Attachments updated by 'git attach add'";

	struct option options[] = {
		OPT_STRING(0, "commit", &attach_commitish, N_("commit"),
			   N_("the commit which the attachments reference to")),
		OPT_END()
	};

	parse_options(argc, argv, prefix, options, git_attach_add_usage,
			     PARSE_OPT_KEEP_ARGV0);
	attach_commitish = attach_commitish ? attach_commitish : "HEAD";

	if (repo_get_oid_commit(the_repository, attach_commitish,
				&attach_commit))
		die(_("unable to find commit %s"), attach_commitish);

	parse_pathspec(&pathspec, 0,
		       PATHSPEC_PREFER_FULL | PATHSPEC_SYMLINK_LEADING_PATH,
		       prefix, argv + 1);
	if (!pathspec.nr)
		die(_("nothing specified, nothing to attach"));

	if (write_attach_tree(&pathspec, &attach_tree, &attach_commit))
		die(_("unable to write attach tree object"));

	if (write_attach_ref(NULL, &attach_tree, attachments_msg))
		die(_("unable to write attach ref"));

	clear_pathspec(&pathspec);
	return 0;
}

int cmd_attach(int argc, const char **argv, const char *prefix)
{
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = { OPT_SUBCOMMAND("add", &fn, add),
				    OPT_END() };

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, options, git_attach_usage,
			     PARSE_OPT_SUBCOMMAND_OPTIONAL);

	if (!fn) {
		error(_("subcommand `%s' not implement yet"), argv[0]);
		usage_with_options(git_attach_usage, options);
	}

	return !!fn(argc, argv, prefix);
}
