#include "git_backend.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/mutex_lock.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace godot;

namespace {

String delta_status_to_string(git_delta_t status) {
	switch (status) {
		case GIT_DELTA_ADDED:
			return String("added");
		case GIT_DELTA_DELETED:
			return String("deleted");
		case GIT_DELTA_RENAMED:
			return String("renamed");
		case GIT_DELTA_COPIED:
			return String("copied");
		case GIT_DELTA_TYPECHANGE:
			return String("typechange");
		case GIT_DELTA_IGNORED:
			return String("ignored");
		case GIT_DELTA_UNTRACKED:
			return String("untracked");
		case GIT_DELTA_UNREADABLE:
			return String("unreadable");
		case GIT_DELTA_CONFLICTED:
			return String("conflicted");
		case GIT_DELTA_UNMODIFIED:
			return String("unmodified");
		default:
			return String("modified");
	}
}

String status_flags_to_string(unsigned int status, bool staged) {
	if (staged) {
		if (status & GIT_STATUS_INDEX_NEW) {
			return String("added");
		}
		if (status & GIT_STATUS_INDEX_DELETED) {
			return String("deleted");
		}
		if (status & GIT_STATUS_INDEX_RENAMED) {
			return String("renamed");
		}
		if (status & GIT_STATUS_INDEX_TYPECHANGE) {
			return String("typechange");
		}
		return String("modified");
	}
	if (status & GIT_STATUS_WT_DELETED) {
		return String("deleted");
	}
	if (status & GIT_STATUS_WT_RENAMED) {
		return String("renamed");
	}
	if (status & GIT_STATUS_WT_TYPECHANGE) {
		return String("typechange");
	}
	return String("modified");
}

std::string oid_to_hex(const git_oid &oid) {
	char buf[64] = { 0 };
	git_oid_tostr(buf, sizeof(buf), &oid);
	return std::string(buf);
}

struct DiffAccumulator {
	TypedArray<Dictionary> files;
	Dictionary current_file;
	Array current_hunks;
	Dictionary current_hunk;
	Array current_lines;
	bool has_file = false;
	bool has_hunk = false;

	void flush_hunk() {
		if (has_hunk) {
			current_hunk["lines"] = current_lines;
			current_hunks.push_back(current_hunk);
			current_hunk = Dictionary();
			current_lines = Array();
			has_hunk = false;
		}
	}

	void flush_file() {
		flush_hunk();
		if (has_file) {
			current_file["hunks"] = current_hunks;
			files.push_back(current_file);
			current_file = Dictionary();
			current_hunks = Array();
			has_file = false;
		}
	}
};

int diff_file_cb(const git_diff_delta *delta, float, void *payload) {
	DiffAccumulator *acc = static_cast<DiffAccumulator *>(payload);
	acc->flush_file();
	acc->current_file["old_path"] = String::utf8(delta->old_file.path ? delta->old_file.path : "");
	acc->current_file["new_path"] = String::utf8(delta->new_file.path ? delta->new_file.path : "");
	acc->current_file["status"] = delta_status_to_string(delta->status);
	acc->has_file = true;
	return 0;
}

int diff_hunk_cb(const git_diff_delta *, const git_diff_hunk *hunk, void *payload) {
	DiffAccumulator *acc = static_cast<DiffAccumulator *>(payload);
	acc->flush_hunk();
	acc->current_hunk["header"] = String::utf8(hunk->header, (int)hunk->header_len);
	acc->current_hunk["old_start"] = hunk->old_start;
	acc->current_hunk["old_lines"] = hunk->old_lines;
	acc->current_hunk["new_start"] = hunk->new_start;
	acc->current_hunk["new_lines"] = hunk->new_lines;
	acc->has_hunk = true;
	return 0;
}

int diff_line_cb(const git_diff_delta *, const git_diff_hunk *, const git_diff_line *line, void *payload) {
	DiffAccumulator *acc = static_cast<DiffAccumulator *>(payload);
	Dictionary line_dict;
	char origin_str[2] = { line->origin, '\0' };
	line_dict["origin"] = String(origin_str);
	line_dict["content"] = String::utf8(line->content, (int)line->content_len);
	acc->current_lines.push_back(line_dict);
	return 0;
}

int stash_foreach_cb(size_t index, const char *message, const git_oid *stash_id, void *payload) {
	TypedArray<Dictionary> *stashes = static_cast<TypedArray<Dictionary> *>(payload);
	Dictionary d;
	d["index"] = (int)index;
	d["message"] = String::utf8(message ? message : "");
	d["oid"] = String(oid_to_hex(*stash_id).c_str());
	stashes->push_back(d);
	return 0;
}

} // namespace

void GitBackend::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open_repository", "path"), &GitBackend::open_repository);
	ClassDB::bind_method(D_METHOD("init_repository", "path"), &GitBackend::init_repository);
	ClassDB::bind_method(D_METHOD("get_current_branch"), &GitBackend::get_current_branch);
	ClassDB::bind_method(D_METHOD("get_head_oid_hex"), &GitBackend::get_head_oid_hex);
	ClassDB::bind_method(D_METHOD("get_status"), &GitBackend::get_status);
	ClassDB::bind_method(D_METHOD("get_diff_head"), &GitBackend::get_diff_head);
	ClassDB::bind_method(D_METHOD("get_staged_diff"), &GitBackend::get_staged_diff);
	ClassDB::bind_method(D_METHOD("get_unstaged_diff"), &GitBackend::get_unstaged_diff);
	ClassDB::bind_method(D_METHOD("get_commit_history", "max_count", "include_refs"), &GitBackend::get_commit_history, DEFVAL(true));

	ClassDB::bind_method(D_METHOD("list_branches"), &GitBackend::list_branches);
	ClassDB::bind_method(D_METHOD("checkout_branch", "name"), &GitBackend::checkout_branch);
	ClassDB::bind_method(D_METHOD("checkout_remote_branch", "remote_ref_name"), &GitBackend::checkout_remote_branch);
	ClassDB::bind_method(D_METHOD("create_branch", "name", "checkout_after"), &GitBackend::create_branch);
	ClassDB::bind_method(D_METHOD("merge_branch", "source", "target"), &GitBackend::merge_branch);
	ClassDB::bind_method(D_METHOD("commit_staged", "message"), &GitBackend::commit_staged);
	ClassDB::bind_method(D_METHOD("stage_file", "path"), &GitBackend::stage_file);
	ClassDB::bind_method(D_METHOD("unstage_file", "path"), &GitBackend::unstage_file);

	ClassDB::bind_method(D_METHOD("get_repository_state"), &GitBackend::get_repository_state);
	ClassDB::bind_method(D_METHOD("get_rebase_progress"), &GitBackend::get_rebase_progress);

	ClassDB::bind_method(D_METHOD("list_stashes"), &GitBackend::list_stashes);
	ClassDB::bind_method(D_METHOD("apply_stash", "index", "pop"), &GitBackend::apply_stash);
	ClassDB::bind_method(D_METHOD("drop_stash", "index"), &GitBackend::drop_stash);

	ClassDB::bind_method(D_METHOD("get_ahead_behind", "remote_name"), &GitBackend::get_ahead_behind, DEFVAL(String("origin")));
	ClassDB::bind_method(D_METHOD("start_fetch", "remote_name"), &GitBackend::start_fetch, DEFVAL(String("origin")));
	ClassDB::bind_method(D_METHOD("start_pull", "remote_name"), &GitBackend::start_pull, DEFVAL(String("origin")));
	ClassDB::bind_method(D_METHOD("is_remote_op_busy"), &GitBackend::is_remote_op_busy);

	ClassDB::bind_method(D_METHOD("get_config_string", "key", "default_value"), &GitBackend::get_config_string, DEFVAL(String("")));
	ClassDB::bind_method(D_METHOD("set_config_string", "key", "value"), &GitBackend::set_config_string);

	ClassDB::bind_method(D_METHOD("list_remotes"), &GitBackend::list_remotes);
	ClassDB::bind_method(D_METHOD("set_remote_url", "name", "url"), &GitBackend::set_remote_url);
	ClassDB::bind_method(D_METHOD("remove_remote", "name"), &GitBackend::remove_remote);

	ClassDB::bind_method(D_METHOD("set_ssh_passphrase", "passphrase"), &GitBackend::set_ssh_passphrase);

	ADD_SIGNAL(MethodInfo("fetch_finished",
			PropertyInfo(Variant::BOOL, "ok"),
			PropertyInfo(Variant::STRING, "error_message")));
	ADD_SIGNAL(MethodInfo("pull_finished",
			PropertyInfo(Variant::BOOL, "ok"),
			PropertyInfo(Variant::STRING, "error_message"),
			PropertyInfo(Variant::INT, "merge_result")));
}

GitBackend::GitBackend() {
	repo_mutex.instantiate();
}

GitBackend::~GitBackend() {
	if (bg_thread.is_valid() && bg_thread->is_started()) {
		bg_thread->wait_to_finish();
	}
	MutexLock lock(**repo_mutex);
	close_repository();
}

void GitBackend::close_repository() {
	if (repo) {
		git_repository_free(repo);
		repo = nullptr;
	}
}

bool GitBackend::open_repository(const String &path) {
	if (bg_thread.is_valid() && bg_thread->is_started()) {
		bg_thread->wait_to_finish();
	}
	MutexLock lock(**repo_mutex);
	close_repository();

	CharString utf8_path = path.utf8();
	int rc = git_repository_open_ext(&repo, utf8_path.get_data(), 0, nullptr);
	if (rc != 0) {
		repo = nullptr;
		const git_error *err = git_error_last();
		UtilityFunctions::push_warning("GitBackend: failed to open repository at '", path, "': ", (err && err->message) ? err->message : "unknown error");
		return false;
	}
	return true;
}

bool GitBackend::init_repository(const String &path) {
	if (bg_thread.is_valid() && bg_thread->is_started()) {
		bg_thread->wait_to_finish();
	}
	MutexLock lock(**repo_mutex);
	close_repository();

	CharString utf8_path = path.utf8();
	git_repository *new_repo = nullptr;
	int rc = git_repository_init(&new_repo, utf8_path.get_data(), 0);
	if (rc != 0) {
		const git_error *err = git_error_last();
		UtilityFunctions::push_warning("GitBackend: failed to init repository at '", path, "': ", (err && err->message) ? err->message : "unknown error");
		return false;
	}
	git_repository_free(new_repo);
	return true;
}

git_tree *GitBackend::resolve_head_tree() const {
	if (!repo) {
		return nullptr;
	}

	git_reference *head_ref = nullptr;
	if (git_repository_head(&head_ref, repo) != 0) {
		// Unborn HEAD (no commits yet) or missing HEAD — callers treat a null
		// tree as "empty tree", which is what an unborn repo's diff should be.
		return nullptr;
	}

	git_tree *tree = nullptr;
	git_object *head_obj = nullptr;
	if (git_reference_peel(&head_obj, head_ref, GIT_OBJECT_COMMIT) == 0 && head_obj) {
		git_commit_tree(&tree, reinterpret_cast<git_commit *>(head_obj));
		git_object_free(head_obj);
	}
	git_reference_free(head_ref);
	return tree;
}

String GitBackend::get_current_branch() const {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return String("");
	}

	int detached = git_repository_head_detached(repo);

	git_reference *head_ref = nullptr;
	int rc = git_repository_head(&head_ref, repo);

	if (rc != 0) {
		// Unborn branch: HEAD is a symbolic ref to a branch with no commits yet.
		String branch_name("main");
		git_reference *symbolic_head = nullptr;
		if (git_reference_lookup(&symbolic_head, repo, "HEAD") == 0) {
			const char *target = git_reference_symbolic_target(symbolic_head);
			if (target) {
				String target_str = String::utf8(target);
				String prefix("refs/heads/");
				if (target_str.begins_with(prefix)) {
					branch_name = target_str.substr(prefix.length());
				}
			}
			git_reference_free(symbolic_head);
		}
		return branch_name + String(" (no commits yet)");
	}

	String result("(unknown)");
	if (detached == 1) {
		const git_oid *oid = git_reference_target(head_ref);
		if (oid) {
			result = String("(detached @ ") + String(oid_to_hex(*oid).substr(0, 7).c_str()) + String(")");
		}
	} else {
		const char *name = nullptr;
		if (git_branch_name(&name, head_ref) == 0 && name) {
			result = String::utf8(name);
		}
	}

	git_reference_free(head_ref);
	return result;
}

String GitBackend::get_repository_state() const {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return String("none");
	}
	switch (git_repository_state(repo)) {
		case GIT_REPOSITORY_STATE_MERGE:
			return String("merge");
		case GIT_REPOSITORY_STATE_REVERT:
		case GIT_REPOSITORY_STATE_REVERT_SEQUENCE:
			return String("revert");
		case GIT_REPOSITORY_STATE_CHERRYPICK:
		case GIT_REPOSITORY_STATE_CHERRYPICK_SEQUENCE:
			return String("cherry-pick");
		case GIT_REPOSITORY_STATE_BISECT:
			return String("bisect");
		case GIT_REPOSITORY_STATE_REBASE:
		case GIT_REPOSITORY_STATE_REBASE_INTERACTIVE:
		case GIT_REPOSITORY_STATE_REBASE_MERGE:
			return String("rebase");
		case GIT_REPOSITORY_STATE_APPLY_MAILBOX:
		case GIT_REPOSITORY_STATE_APPLY_MAILBOX_OR_REBASE:
			return String("apply-mailbox");
		default:
			return String("none");
	}
}

Dictionary GitBackend::get_rebase_progress() const {
	MutexLock lock(**repo_mutex);
	Dictionary result;
	result["in_progress"] = false;
	if (!repo) {
		return result;
	}

	git_rebase *rebase = nullptr;
	if (git_rebase_open(&rebase, repo, nullptr) != 0) {
		return result;
	}

	size_t total = git_rebase_operation_entrycount(rebase);
	size_t current = git_rebase_operation_current(rebase);
	result["in_progress"] = true;
	result["total"] = (int)total;
	result["current"] = (current == GIT_REBASE_NO_OPERATION) ? 0 : (int)current;

	git_rebase_free(rebase);
	return result;
}

String GitBackend::get_head_oid_hex() const {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return String("");
	}
	git_oid oid;
	if (git_reference_name_to_id(&oid, repo, "HEAD") != 0) {
		return String("");
	}
	return String(oid_to_hex(oid).c_str());
}

Dictionary GitBackend::get_status() const {
	MutexLock lock(**repo_mutex);
	Dictionary result;
	Array staged;
	Array unstaged;
	Array untracked;
	result["staged"] = staged;
	result["unstaged"] = unstaged;
	result["untracked"] = untracked;

	if (!repo) {
		return result;
	}

	git_status_options opts = GIT_STATUS_OPTIONS_INIT;
	opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
	opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS |
			GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX | GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR;

	git_status_list *status_list = nullptr;
	if (git_status_list_new(&status_list, repo, &opts) != 0) {
		return result;
	}

	size_t count = git_status_list_entrycount(status_list);
	for (size_t i = 0; i < count; i++) {
		const git_status_entry *entry = git_status_byindex(status_list, i);
		if (!entry) {
			continue;
		}
		unsigned int s = entry->status;

		if (s & (GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED | GIT_STATUS_INDEX_DELETED | GIT_STATUS_INDEX_RENAMED | GIT_STATUS_INDEX_TYPECHANGE)) {
			const git_diff_delta *delta = entry->head_to_index;
			if (delta) {
				Dictionary d;
				d["path"] = String::utf8(delta->new_file.path ? delta->new_file.path : delta->old_file.path);
				d["status"] = status_flags_to_string(s, true);
				staged.push_back(d);
			}
		}
		if (s & (GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_DELETED | GIT_STATUS_WT_TYPECHANGE | GIT_STATUS_WT_RENAMED)) {
			const git_diff_delta *delta = entry->index_to_workdir;
			if (delta) {
				Dictionary d;
				d["path"] = String::utf8(delta->new_file.path ? delta->new_file.path : delta->old_file.path);
				d["status"] = status_flags_to_string(s, false);
				unstaged.push_back(d);
			}
		}
		if (s & GIT_STATUS_WT_NEW) {
			const git_diff_delta *delta = entry->index_to_workdir;
			if (delta && delta->new_file.path) {
				Dictionary d;
				d["path"] = String::utf8(delta->new_file.path);
				d["status"] = String("untracked");
				untracked.push_back(d);
			}
		}
	}

	git_status_list_free(status_list);

	result["staged"] = staged;
	result["unstaged"] = unstaged;
	result["untracked"] = untracked;
	return result;
}

TypedArray<Dictionary> GitBackend::diff_to_array(git_diff *diff) const {
	DiffAccumulator acc;
	if (diff) {
		git_diff_foreach(diff, diff_file_cb, nullptr, diff_hunk_cb, diff_line_cb, &acc);
	}
	acc.flush_file();
	return acc.files;
}

TypedArray<Dictionary> GitBackend::get_diff_head() const {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return TypedArray<Dictionary>();
	}

	git_tree *head_tree = resolve_head_tree();

	git_diff_options diff_opts = GIT_DIFF_OPTIONS_INIT;
	git_diff *diff = nullptr;
	git_diff_tree_to_workdir_with_index(&diff, repo, head_tree, &diff_opts);

	TypedArray<Dictionary> result = diff_to_array(diff);

	if (diff) {
		git_diff_free(diff);
	}
	if (head_tree) {
		git_tree_free(head_tree);
	}
	return result;
}

TypedArray<Dictionary> GitBackend::get_staged_diff() const {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return TypedArray<Dictionary>();
	}

	git_tree *head_tree = resolve_head_tree();

	git_diff_options diff_opts = GIT_DIFF_OPTIONS_INIT;
	git_diff *diff = nullptr;
	git_diff_tree_to_index(&diff, repo, head_tree, nullptr, &diff_opts);

	TypedArray<Dictionary> result = diff_to_array(diff);

	if (diff) {
		git_diff_free(diff);
	}
	if (head_tree) {
		git_tree_free(head_tree);
	}
	return result;
}

TypedArray<Dictionary> GitBackend::get_unstaged_diff() const {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return TypedArray<Dictionary>();
	}

	git_diff_options diff_opts = GIT_DIFF_OPTIONS_INIT;
	git_diff *diff = nullptr;
	git_diff_index_to_workdir(&diff, repo, nullptr, &diff_opts);

	TypedArray<Dictionary> result = diff_to_array(diff);

	if (diff) {
		git_diff_free(diff);
	}
	return result;
}

TypedArray<Dictionary> GitBackend::get_commit_history(int max_count, bool include_refs) const {
	MutexLock lock(**repo_mutex);
	TypedArray<Dictionary> result;
	if (!repo || max_count <= 0) {
		return result;
	}

	// oid hex -> (name, kind) refs ("branch"/"remote"/"tag") pointing at that commit.
	std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> refs_by_oid;
	if (include_refs) {
		git_reference_iterator *ref_iter = nullptr;
		if (git_reference_iterator_new(&ref_iter, repo) == 0) {
			git_reference *ref = nullptr;
			while (git_reference_next(&ref, ref_iter) == 0) {
				const char *ref_name = git_reference_name(ref);
				bool is_branch = ref_name && strncmp(ref_name, "refs/heads/", 11) == 0;
				bool is_tag = ref_name && strncmp(ref_name, "refs/tags/", 10) == 0;
				bool is_remote = ref_name && strncmp(ref_name, "refs/remotes/", 13) == 0;
				// Skip the remote's symbolic HEAD alias (e.g. refs/remotes/origin/HEAD) —
				// it just duplicates whichever branch the remote's default points at.
				bool is_symbolic_head = is_remote && ref_name && strlen(ref_name) >= 5 &&
						strcmp(ref_name + strlen(ref_name) - 5, "/HEAD") == 0;
				if ((is_branch || is_tag || is_remote) && !is_symbolic_head) {
					git_oid target_oid;
					bool have_oid = false;
					if (git_reference_type(ref) == GIT_REFERENCE_DIRECT) {
						const git_oid *direct = git_reference_target(ref);
						if (direct) {
							target_oid = *direct;
							have_oid = true;
						}
					}
					if (!have_oid) {
						git_object *peeled = nullptr;
						if (git_reference_peel(&peeled, ref, GIT_OBJECT_COMMIT) == 0 && peeled) {
							const git_oid *peeled_oid = git_object_id(peeled);
							if (peeled_oid) {
								target_oid = *peeled_oid;
								have_oid = true;
							}
							git_object_free(peeled);
						}
					}
					if (have_oid) {
						std::string key = oid_to_hex(target_oid);
						int prefix_len = is_branch ? 11 : (is_tag ? 10 : 13);
						std::string kind = is_branch ? "branch" : (is_tag ? "tag" : "remote");
						refs_by_oid[key].push_back(std::make_pair(std::string(ref_name + prefix_len), kind));
					}
				}
				git_reference_free(ref);
			}
			git_reference_iterator_free(ref_iter);
		}
	}

	git_revwalk *walker = nullptr;
	if (git_revwalk_new(&walker, repo) != 0) {
		return result;
	}
	git_revwalk_sorting(walker, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
	if (git_revwalk_push_head(walker) != 0) {
		git_revwalk_free(walker);
		return result;
	}

	struct CommitData {
		std::string hash;
		std::string short_hash;
		std::string summary;
		std::string author_name;
		std::string author_email;
		int64_t time = 0;
		int lane = 0;
		std::vector<int> parent_lanes;
		std::vector<std::pair<std::string, std::string>> refs;
	};

	std::vector<CommitData> commits;
	std::vector<git_oid> active_lanes;
	std::vector<bool> lane_active;
	// oid hex -> (commit index, parent slot index) waiting to learn that oid's eventual lane.
	std::unordered_map<std::string, std::vector<std::pair<int, int>>> pending_patches;

	git_oid oid;
	int emitted = 0;
	while (emitted < max_count && git_revwalk_next(&oid, walker) == 0) {
		git_commit *commit = nullptr;
		if (git_commit_lookup(&commit, repo, &oid) != 0) {
			continue;
		}

		int placed_lane = -1;
		for (size_t i = 0; i < active_lanes.size(); i++) {
			if (lane_active[i] && git_oid_equal(&active_lanes[i], &oid)) {
				placed_lane = (int)i;
				break;
			}
		}
		// Free any other lanes that were also converging on this same commit.
		for (size_t i = 0; i < active_lanes.size(); i++) {
			if ((int)i != placed_lane && lane_active[i] && git_oid_equal(&active_lanes[i], &oid)) {
				lane_active[i] = false;
			}
		}
		if (placed_lane == -1) {
			for (size_t i = 0; i < lane_active.size(); i++) {
				if (!lane_active[i]) {
					placed_lane = (int)i;
					break;
				}
			}
			if (placed_lane == -1) {
				placed_lane = (int)active_lanes.size();
				active_lanes.push_back(oid);
				lane_active.push_back(true);
			} else {
				active_lanes[placed_lane] = oid;
				lane_active[placed_lane] = true;
			}
		}

		int commit_index = (int)commits.size();

		CommitData data;
		data.hash = oid_to_hex(oid);
		data.short_hash = data.hash.substr(0, 7);
		const char *summary = git_commit_summary(commit);
		data.summary = summary ? summary : "";
		const git_signature *author = git_commit_author(commit);
		if (author) {
			data.author_name = author->name ? author->name : "";
			data.author_email = author->email ? author->email : "";
			data.time = (int64_t)author->when.time;
		}
		data.lane = placed_lane;

		auto refs_it = refs_by_oid.find(data.hash);
		if (refs_it != refs_by_oid.end()) {
			data.refs = refs_it->second;
		}

		unsigned int parent_count = git_commit_parentcount(commit);
		data.parent_lanes.assign(parent_count, -1);

		for (unsigned int p = 0; p < parent_count; p++) {
			const git_oid *parent_oid = git_commit_parent_id(commit, p);
			if (!parent_oid) {
				continue;
			}
			std::string parent_hex = oid_to_hex(*parent_oid);

			int target_lane;
			if (p == 0) {
				target_lane = placed_lane;
				active_lanes[target_lane] = *parent_oid;
				lane_active[target_lane] = true;
			} else {
				target_lane = -1;
				for (size_t i = 0; i < lane_active.size(); i++) {
					if (!lane_active[i]) {
						target_lane = (int)i;
						break;
					}
				}
				if (target_lane == -1) {
					target_lane = (int)active_lanes.size();
					active_lanes.push_back(*parent_oid);
					lane_active.push_back(true);
				} else {
					active_lanes[target_lane] = *parent_oid;
					lane_active[target_lane] = true;
				}
			}
			pending_patches[parent_hex].push_back(std::make_pair(commit_index, (int)p));
		}

		if (parent_count == 0) {
			lane_active[placed_lane] = false;
		}

		auto patch_it = pending_patches.find(data.hash);
		if (patch_it != pending_patches.end()) {
			for (auto &target : patch_it->second) {
				commits[target.first].parent_lanes[target.second] = placed_lane;
			}
			pending_patches.erase(patch_it);
		}

		commits.push_back(data);
		git_commit_free(commit);
		emitted++;
	}

	git_revwalk_free(walker);

	for (auto &c : commits) {
		Dictionary d;
		d["hash"] = String(c.hash.c_str());
		d["short_hash"] = String(c.short_hash.c_str());
		d["summary"] = String::utf8(c.summary.c_str());
		d["author_name"] = String::utf8(c.author_name.c_str());
		d["author_email"] = String(c.author_email.c_str());
		d["time"] = c.time;
		d["lane"] = c.lane;
		Array parent_lanes_arr;
		for (int pl : c.parent_lanes) {
			parent_lanes_arr.push_back(pl);
		}
		d["parent_lanes"] = parent_lanes_arr;
		Array refs_arr;
		for (const auto &r : c.refs) {
			Dictionary ref_d;
			ref_d["name"] = String::utf8(r.first.c_str());
			ref_d["kind"] = String(r.second.c_str());
			refs_arr.push_back(ref_d);
		}
		d["refs"] = refs_arr;
		result.push_back(d);
	}

	return result;
}

git_reference *GitBackend::lookup_branch_ref(const String &name) const {
	if (!repo) {
		return nullptr;
	}
	String refname = String("refs/heads/") + name;
	git_reference *ref = nullptr;
	git_reference_lookup(&ref, repo, refname.utf8().get_data());
	return ref;
}

String GitBackend::get_current_branch_raw_name() const {
	if (!repo) {
		return String("");
	}

	if (git_repository_head_unborn(repo) == 1) {
		String branch_name("main");
		git_reference *symbolic_head = nullptr;
		if (git_reference_lookup(&symbolic_head, repo, "HEAD") == 0) {
			const char *target = git_reference_symbolic_target(symbolic_head);
			if (target) {
				String target_str = String::utf8(target);
				String prefix("refs/heads/");
				if (target_str.begins_with(prefix)) {
					branch_name = target_str.substr(prefix.length());
				}
			}
			git_reference_free(symbolic_head);
		}
		return branch_name;
	}

	git_reference *head_ref = nullptr;
	if (git_repository_head(&head_ref, repo) != 0) {
		return String("");
	}

	String result("");
	if (git_repository_head_detached(repo) != 1) {
		const char *name = nullptr;
		if (git_branch_name(&name, head_ref) == 0 && name) {
			result = String::utf8(name);
		}
	}

	git_reference_free(head_ref);
	return result;
}

TypedArray<Dictionary> GitBackend::list_branches() const {
	MutexLock lock(**repo_mutex);
	TypedArray<Dictionary> result;
	if (!repo) {
		return result;
	}

	git_branch_iterator *iter = nullptr;
	if (git_branch_iterator_new(&iter, repo, GIT_BRANCH_ALL) != 0) {
		return result;
	}

	struct BranchEntry {
		std::string name;
		bool is_current;
		bool is_remote;
		std::string target_oid;
	};

	std::vector<BranchEntry> entries;
	git_reference *ref = nullptr;
	git_branch_t type;
	while (git_branch_next(&ref, &type, iter) == 0) {
		const char *name = nullptr;
		if (git_branch_name(&name, ref) == 0 && name) {
			std::string name_str(name);
			bool is_symbolic_head = name_str.size() >= 5 && name_str.compare(name_str.size() - 5, 5, "/HEAD") == 0;
			if (is_symbolic_head) {
				git_reference_free(ref);
				continue;
			}
			BranchEntry entry;
			entry.name = name;
			entry.is_current = git_branch_is_head(ref) == 1;
			entry.is_remote = type == GIT_BRANCH_REMOTE;

			git_oid target_oid;
			bool have_oid = false;
			if (git_reference_type(ref) == GIT_REFERENCE_DIRECT) {
				const git_oid *direct = git_reference_target(ref);
				if (direct) {
					target_oid = *direct;
					have_oid = true;
				}
			}
			if (!have_oid) {
				git_object *peeled = nullptr;
				if (git_reference_peel(&peeled, ref, GIT_OBJECT_COMMIT) == 0 && peeled) {
					const git_oid *peeled_oid = git_object_id(peeled);
					if (peeled_oid) {
						target_oid = *peeled_oid;
						have_oid = true;
					}
					git_object_free(peeled);
				}
			}
			if (have_oid) {
				entry.target_oid = oid_to_hex(target_oid);
			}

			entries.push_back(entry);
		}
		git_reference_free(ref);
	}
	git_branch_iterator_free(iter);

	std::sort(entries.begin(), entries.end(), [](const BranchEntry &a, const BranchEntry &b) {
		if (a.is_remote != b.is_remote) {
			return !a.is_remote;
		}
		return a.name < b.name;
	});

	for (const auto &entry : entries) {
		Dictionary d;
		d["name"] = String::utf8(entry.name.c_str());
		d["is_current"] = entry.is_current;
		d["is_remote"] = entry.is_remote;
		d["target_oid"] = String(entry.target_oid.c_str());
		result.push_back(d);
	}

	return result;
}

bool GitBackend::checkout_ref_and_move_head(git_reference *ref, const String &local_branch_name) {
	git_object *target_obj = nullptr;
	if (git_reference_peel(&target_obj, ref, GIT_OBJECT_COMMIT) != 0) {
		UtilityFunctions::push_warning("GitBackend: could not resolve commit for branch '", local_branch_name, "'.");
		return false;
	}

	git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
	checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;

	int rc = git_checkout_tree(repo, target_obj, &checkout_opts);
	git_object_free(target_obj);

	if (rc != 0) {
		const git_error *err = git_error_last();
		UtilityFunctions::push_warning("GitBackend: checkout of '", local_branch_name, "' failed (uncommitted changes conflict?): ",
				(err && err->message) ? err->message : "unknown error");
		return false;
	}

	String refname = String("refs/heads/") + local_branch_name;
	rc = git_repository_set_head(repo, refname.utf8().get_data());
	if (rc != 0) {
		const git_error *err = git_error_last();
		UtilityFunctions::push_warning("GitBackend: failed to move HEAD to '", local_branch_name, "': ",
				(err && err->message) ? err->message : "unknown error");
		return false;
	}
	return true;
}

bool GitBackend::checkout_branch(const String &name) {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return false;
	}

	git_reference *branch_ref = lookup_branch_ref(name);
	if (!branch_ref) {
		UtilityFunctions::push_warning("GitBackend: branch '", name, "' not found.");
		return false;
	}

	bool ok = checkout_ref_and_move_head(branch_ref, name);
	git_reference_free(branch_ref);
	return ok;
}

bool GitBackend::checkout_remote_branch(const String &remote_ref_name) {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return false;
	}

	int slash = remote_ref_name.find("/");
	if (slash == -1) {
		UtilityFunctions::push_warning("GitBackend: '", remote_ref_name, "' is not a valid remote-tracking branch name.");
		return false;
	}
	String local_name = remote_ref_name.substr(slash + 1);

	String tracking_refname = String("refs/remotes/") + remote_ref_name;
	git_reference *remote_ref = nullptr;
	if (git_reference_lookup(&remote_ref, repo, tracking_refname.utf8().get_data()) != 0) {
		UtilityFunctions::push_warning("GitBackend: remote-tracking branch '", remote_ref_name, "' not found.");
		return false;
	}

	// If a local branch with this name already exists, just check it out directly.
	git_reference *existing_local = lookup_branch_ref(local_name);
	if (existing_local) {
		bool ok = checkout_ref_and_move_head(existing_local, local_name);
		git_reference_free(existing_local);
		git_reference_free(remote_ref);
		return ok;
	}

	git_object *remote_commit_obj = nullptr;
	if (git_reference_peel(&remote_commit_obj, remote_ref, GIT_OBJECT_COMMIT) != 0) {
		git_reference_free(remote_ref);
		UtilityFunctions::push_warning("GitBackend: could not resolve commit for '", remote_ref_name, "'.");
		return false;
	}

	git_reference *new_local_ref = nullptr;
	int rc = git_branch_create(&new_local_ref, repo, local_name.utf8().get_data(),
			reinterpret_cast<git_commit *>(remote_commit_obj), /*force=*/0);
	git_object_free(remote_commit_obj);

	if (rc != 0) {
		const git_error *err = git_error_last();
		git_reference_free(remote_ref);
		UtilityFunctions::push_warning("GitBackend: failed to create local branch '", local_name, "' from '", remote_ref_name, "': ",
				(err && err->message) ? err->message : "unknown error");
		return false;
	}

	git_branch_set_upstream(new_local_ref, remote_ref_name.utf8().get_data());
	git_reference_free(remote_ref);

	bool ok = checkout_ref_and_move_head(new_local_ref, local_name);
	git_reference_free(new_local_ref);
	return ok;
}

bool GitBackend::create_branch(const String &name, bool checkout_after) {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return false;
	}
	if (git_repository_head_unborn(repo) == 1) {
		UtilityFunctions::push_warning("GitBackend: cannot create branch '", name, "': repository has no commits yet.");
		return false;
	}

	git_reference *head_ref = nullptr;
	if (git_repository_head(&head_ref, repo) != 0) {
		return false;
	}
	git_object *head_commit_obj = nullptr;
	if (git_reference_peel(&head_commit_obj, head_ref, GIT_OBJECT_COMMIT) != 0) {
		git_reference_free(head_ref);
		return false;
	}
	git_reference_free(head_ref);

	git_reference *new_ref = nullptr;
	int rc = git_branch_create(&new_ref, repo, name.utf8().get_data(),
			reinterpret_cast<git_commit *>(head_commit_obj), /*force=*/0);
	git_object_free(head_commit_obj);

	if (rc != 0) {
		const git_error *err = git_error_last();
		UtilityFunctions::push_warning("GitBackend: failed to create branch '", name, "': ",
				(err && err->message) ? err->message : "unknown error");
		return false;
	}
	git_reference_free(new_ref);

	if (checkout_after) {
		checkout_branch(name);
	}
	return true;
}

int GitBackend::merge_branch(const String &source, const String &target) {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return -1;
	}

	if (get_current_branch_raw_name() != target) {
		if (!checkout_branch(target)) {
			return -1;
		}
	}

	git_reference *source_ref = lookup_branch_ref(source);
	if (!source_ref) {
		UtilityFunctions::push_warning("GitBackend: source branch '", source, "' not found.");
		return -1;
	}

	git_annotated_commit *their_head = nullptr;
	if (git_annotated_commit_from_ref(&their_head, repo, source_ref) != 0) {
		git_reference_free(source_ref);
		return -1;
	}

	int result = merge_annotated_into_current(source, their_head, target, nullptr);

	git_annotated_commit_free(their_head);
	git_reference_free(source_ref);
	return result;
}

int GitBackend::merge_annotated_into_current(const String &source_display_name, git_annotated_commit *their_head,
		const String &target, String *out_error_message) {
	git_merge_analysis_t analysis;
	git_merge_preference_t preference;
	const git_annotated_commit *heads[1] = { their_head };
	if (git_merge_analysis(&analysis, &preference, repo, heads, 1) != 0) {
		return -1;
	}

	int result = -1;

	if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE) {
		UtilityFunctions::push_warning("GitBackend: '", target, "' already contains all commits from '", source_display_name, "'; nothing to merge.");
		result = 1;
	} else if (analysis & GIT_MERGE_ANALYSIS_FASTFORWARD) {
		git_reference *target_ref = lookup_branch_ref(target);
		const git_oid *source_oid = git_annotated_commit_id(their_head);
		git_reference *new_ref = nullptr;
		int rc = target_ref ? git_reference_set_target(&new_ref, target_ref, source_oid, "merge: Fast-forward") : -1;
		if (rc == 0) {
			git_commit *source_commit = nullptr;
			git_tree *ff_tree = nullptr;
			if (git_commit_lookup(&source_commit, repo, source_oid) == 0) {
				git_commit_tree(&ff_tree, source_commit);
			}
			git_checkout_options ff_opts = GIT_CHECKOUT_OPTIONS_INIT;
			ff_opts.checkout_strategy = GIT_CHECKOUT_SAFE | GIT_CHECKOUT_FORCE;
			if (ff_tree) {
				git_checkout_tree(repo, reinterpret_cast<git_object *>(ff_tree), &ff_opts);
				git_tree_free(ff_tree);
			}
			if (source_commit) {
				git_commit_free(source_commit);
			}
			git_reference_free(new_ref);
			result = 0;
		} else {
			const git_error *err = git_error_last();
			String msg = (err && err->message) ? String::utf8(err->message) : String("unknown error");
			UtilityFunctions::push_warning("GitBackend: fast-forward merge failed: ", msg);
			if (out_error_message) {
				*out_error_message = String("Fast-forward merge failed: ") + msg;
			}
		}
		if (target_ref) {
			git_reference_free(target_ref);
		}
	} else if (analysis & GIT_MERGE_ANALYSIS_NORMAL) {
		git_reference *target_ref_before = lookup_branch_ref(target);
		git_oid target_tip_oid = { 0 };
		if (target_ref_before) {
			const git_oid *tip = git_reference_target(target_ref_before);
			if (tip) {
				target_tip_oid = *tip;
			}
			git_reference_free(target_ref_before);
		}

		git_merge_options merge_opts = GIT_MERGE_OPTIONS_INIT;
		git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
		checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;

		int rc = git_merge(repo, heads, 1, &merge_opts, &checkout_opts);
		if (rc != 0) {
			const git_error *err = git_error_last();
			String msg = (err && err->message) ? String::utf8(err->message) : String("unknown error");
			UtilityFunctions::push_warning("GitBackend: merge failed: ", msg);
			if (out_error_message) {
				*out_error_message = String("Merge failed: ") + msg;
			}
			git_repository_state_cleanup(repo);
		} else {
			git_index *index = nullptr;
			git_repository_index(&index, repo);
			if (index && git_index_has_conflicts(index)) {
				UtilityFunctions::push_warning("GitBackend: merge of '", source_display_name, "' into '", target,
						"' produced conflicts; resolve manually with the git CLI, then retry.");
				if (out_error_message) {
					*out_error_message = String("Merge of '") + source_display_name + String("' into '") + target +
							String("' produced conflicts; resolve manually with the git CLI, then retry.");
				}
				git_checkout_options reset_opts = GIT_CHECKOUT_OPTIONS_INIT;
				reset_opts.checkout_strategy = GIT_CHECKOUT_FORCE;
				git_checkout_head(repo, &reset_opts);
				git_repository_state_cleanup(repo);
			} else if (index) {
				git_oid tree_oid;
				git_index_write_tree(&tree_oid, index);
				git_tree *merged_tree = nullptr;
				git_tree_lookup(&merged_tree, repo, &tree_oid);

				git_commit *target_commit = nullptr;
				git_commit_lookup(&target_commit, repo, &target_tip_oid);
				git_commit *source_commit = nullptr;
				git_commit_lookup(&source_commit, repo, git_annotated_commit_id(their_head));

				git_signature *sig = nullptr;
				git_signature_default(&sig, repo);

				String msg = String("Merge branch '") + source_display_name + String("' into ") + target;
				const git_commit *parents[2] = { target_commit, source_commit };
				git_oid new_commit_oid;
				rc = (sig && target_commit && source_commit && merged_tree)
						? git_commit_create(&new_commit_oid, repo, "HEAD", sig, sig, nullptr,
								  msg.utf8().get_data(), merged_tree, 2, parents)
						: -1;
				if (rc == 0) {
					result = 0;
				} else {
					const git_error *err = git_error_last();
					String err_msg = (err && err->message) ? String::utf8(err->message) : String("unknown error");
					UtilityFunctions::push_warning("GitBackend: failed to create merge commit: ", err_msg);
					if (out_error_message) {
						*out_error_message = String("Failed to create merge commit: ") + err_msg;
					}
				}

				if (sig) {
					git_signature_free(sig);
				}
				if (target_commit) {
					git_commit_free(target_commit);
				}
				if (source_commit) {
					git_commit_free(source_commit);
				}
				if (merged_tree) {
					git_tree_free(merged_tree);
				}
				git_repository_state_cleanup(repo);
			}
			if (index) {
				git_index_free(index);
			}
		}
	} else {
		UtilityFunctions::push_warning("GitBackend: cannot merge '", source_display_name, "' into '", target, "' (unsupported merge state).");
		if (out_error_message) {
			*out_error_message = String("Cannot merge '") + source_display_name + String("' into '") + target + String("' (unsupported merge state).");
		}
	}

	return result;
}

bool GitBackend::commit_staged(const String &message) const {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return false;
	}
	String trimmed = message.strip_edges();
	if (trimmed.is_empty()) {
		UtilityFunctions::push_warning("GitBackend: commit message is empty.");
		return false;
	}

	git_commit_create_options opts = GIT_COMMIT_CREATE_OPTIONS_INIT;
	git_oid new_commit_oid;
	int rc = git_commit_create_from_stage(&new_commit_oid, repo, trimmed.utf8().get_data(), &opts);

	if (rc == GIT_EUNCHANGED) {
		UtilityFunctions::push_warning("GitBackend: nothing staged to commit.");
		return false;
	}
	if (rc != 0) {
		const git_error *err = git_error_last();
		UtilityFunctions::push_warning("GitBackend: commit failed: ", (err && err->message) ? err->message : "unknown error");
		return false;
	}
	return true;
}

bool GitBackend::stage_file(const String &path) const {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return false;
	}

	git_index *index = nullptr;
	if (git_repository_index(&index, repo) != 0) {
		return false;
	}

	CharString utf8_path = path.utf8();
	int rc = git_index_add_bypath(index, utf8_path.get_data());
	if (rc != 0) {
		// Path doesn't exist on disk — this is a staged deletion, not an add/modify.
		rc = git_index_remove_bypath(index, utf8_path.get_data());
	}
	if (rc != 0) {
		const git_error *err = git_error_last();
		UtilityFunctions::push_warning("GitBackend: failed to stage '", path, "': ", (err && err->message) ? err->message : "unknown error");
		git_index_free(index);
		return false;
	}

	rc = git_index_write(index);
	git_index_free(index);
	if (rc != 0) {
		UtilityFunctions::push_warning("GitBackend: failed to write index after staging '", path, "'.");
		return false;
	}
	return true;
}

bool GitBackend::unstage_file(const String &path) const {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return false;
	}

	CharString utf8_path = path.utf8();
	char *path_cstr = const_cast<char *>(utf8_path.get_data());
	git_strarray pathspec;
	pathspec.strings = &path_cstr;
	pathspec.count = 1;

	if (git_repository_head_unborn(repo) == 1) {
		// No HEAD commit to reset to yet — unstaging just drops the index entry.
		git_index *index = nullptr;
		if (git_repository_index(&index, repo) != 0) {
			return false;
		}
		int rc = git_index_remove_bypath(index, utf8_path.get_data());
		if (rc == 0) {
			rc = git_index_write(index);
		}
		git_index_free(index);
		return rc == 0;
	}

	git_reference *head_ref = nullptr;
	if (git_repository_head(&head_ref, repo) != 0) {
		return false;
	}

	git_object *head_obj = nullptr;
	if (git_reference_peel(&head_obj, head_ref, GIT_OBJECT_COMMIT) != 0 || !head_obj) {
		git_reference_free(head_ref);
		return false;
	}
	git_reference_free(head_ref);

	int rc = git_reset_default(repo, head_obj, &pathspec);
	git_object_free(head_obj);
	if (rc != 0) {
		const git_error *err = git_error_last();
		UtilityFunctions::push_warning("GitBackend: failed to unstage '", path, "': ", (err && err->message) ? err->message : "unknown error");
		return false;
	}
	return true;
}

TypedArray<Dictionary> GitBackend::list_stashes() const {
	MutexLock lock(**repo_mutex);
	TypedArray<Dictionary> result;
	if (!repo) {
		return result;
	}
	git_stash_foreach(repo, stash_foreach_cb, &result);
	return result;
}

bool GitBackend::apply_stash(int index, bool pop) const {
	MutexLock lock(**repo_mutex);
	if (!repo || index < 0) {
		return false;
	}

	git_stash_apply_options opts = GIT_STASH_APPLY_OPTIONS_INIT;
	int rc = pop ? git_stash_pop(repo, (size_t)index, &opts) : git_stash_apply(repo, (size_t)index, &opts);
	if (rc != 0) {
		const git_error *err = git_error_last();
		UtilityFunctions::push_warning("GitBackend: failed to ", pop ? "pop" : "apply", " stash@{", index, "}: ",
				(err && err->message) ? err->message : "unknown error");
		return false;
	}
	return true;
}

bool GitBackend::drop_stash(int index) const {
	MutexLock lock(**repo_mutex);
	if (!repo || index < 0) {
		return false;
	}

	int rc = git_stash_drop(repo, (size_t)index);
	if (rc != 0) {
		const git_error *err = git_error_last();
		UtilityFunctions::push_warning("GitBackend: failed to drop stash@{", index, "}: ",
				(err && err->message) ? err->message : "unknown error");
		return false;
	}
	return true;
}

int GitBackend::credentials_cb(git_credential **out, const char *, const char *username_from_url,
		unsigned int allowed_types, void *payload) {
	if (!(allowed_types & GIT_CREDENTIAL_SSH_KEY)) {
		return GIT_PASSTHROUGH;
	}
	const char *user = username_from_url ? username_from_url : "git";
	GitBackend *self = static_cast<GitBackend *>(payload);

	if (self && self->repo) {
		git_config *cfg = nullptr;
		if (git_repository_config(&cfg, self->repo) == 0) {
			const char *key_path = nullptr;
			if (git_config_get_string(&key_path, cfg, "devtools.sshkeypath") == 0 && key_path && key_path[0] != '\0') {
				std::string priv_path(key_path);
				std::string pub_path = priv_path + ".pub";
				CharString utf8_passphrase = self->ssh_key_passphrase.utf8();
				const char *passphrase = utf8_passphrase.length() > 0 ? utf8_passphrase.get_data() : nullptr;
				int rc = git_credential_ssh_key_new(out, user, pub_path.c_str(), priv_path.c_str(), passphrase);
				git_config_free(cfg);
				if (rc == 0) {
					return 0;
				}
			} else {
				git_config_free(cfg);
			}
		}
	}

	if (git_credential_ssh_key_from_agent(out, user) == 0) {
		return 0;
	}
	return GIT_PASSTHROUGH;
}

Dictionary GitBackend::get_ahead_behind(const String &remote_name) const {
	if (!repo_mutex->try_lock()) {
		Dictionary stale = cached_ahead_behind.duplicate();
		stale["stale"] = true;
		return stale;
	}

	Dictionary result;
	result["ahead"] = 0;
	result["behind"] = 0;
	result["has_upstream"] = false;
	result["stale"] = false;

	if (!repo) {
		repo_mutex->unlock();
		return result;
	}

	git_reference *head_ref = nullptr;
	if (git_repository_head(&head_ref, repo) != 0) {
		repo_mutex->unlock();
		cached_ahead_behind = result;
		return result;
	}

	git_reference *upstream_ref = nullptr;
	if (git_branch_upstream(&upstream_ref, head_ref) != 0) {
		git_reference_free(head_ref);
		repo_mutex->unlock();
		cached_ahead_behind = result;
		return result;
	}

	const git_oid *local_oid = git_reference_target(head_ref);
	const git_oid *upstream_oid = git_reference_target(upstream_ref);
	if (local_oid && upstream_oid) {
		size_t ahead = 0;
		size_t behind = 0;
		if (git_graph_ahead_behind(&ahead, &behind, repo, local_oid, upstream_oid) == 0) {
			result["ahead"] = (int)ahead;
			result["behind"] = (int)behind;
			result["has_upstream"] = true;
		}
	}

	git_reference_free(upstream_ref);
	git_reference_free(head_ref);
	repo_mutex->unlock();
	cached_ahead_behind = result;
	return result;
}

bool GitBackend::is_remote_op_busy() const {
	return bg_thread.is_valid() && bg_thread->is_alive();
}

bool GitBackend::start_fetch(const String &remote_name) {
	if (!repo) {
		return false;
	}
	if (bg_thread.is_valid() && bg_thread->is_started()) {
		if (bg_thread->is_alive()) {
			return false;
		}
		bg_thread->wait_to_finish();
	}
	if (!bg_thread.is_valid()) {
		bg_thread.instantiate();
	}
	bg_thread->start(callable_mp(this, &GitBackend::fetch_worker).bind(remote_name));
	return true;
}

bool GitBackend::start_pull(const String &remote_name) {
	if (!repo) {
		return false;
	}
	if (bg_thread.is_valid() && bg_thread->is_started()) {
		if (bg_thread->is_alive()) {
			return false;
		}
		bg_thread->wait_to_finish();
	}
	if (!bg_thread.is_valid()) {
		bg_thread.instantiate();
	}
	bg_thread->start(callable_mp(this, &GitBackend::pull_worker).bind(remote_name));
	return true;
}

void GitBackend::fetch_worker(String remote_name) {
	MutexLock lock(**repo_mutex);

	bool ok = false;
	String error_message;

	if (!repo) {
		error_message = "No repository open.";
	} else {
		git_remote *remote = nullptr;
		if (git_remote_lookup(&remote, repo, remote_name.utf8().get_data()) != 0) {
			error_message = String("Remote '") + remote_name + String("' not found.");
		} else {
			git_fetch_options opts = GIT_FETCH_OPTIONS_INIT;
			opts.callbacks.credentials = &GitBackend::credentials_cb;
			opts.callbacks.payload = this;

			int rc = git_remote_fetch(remote, nullptr, &opts, "fetch");
			if (rc == 0) {
				ok = true;
			} else {
				const git_error *err = git_error_last();
				error_message = (err && err->message) ? String::utf8(err->message) : String("fetch failed");
			}
			git_remote_free(remote);
		}
	}

	if (!ok) {
		UtilityFunctions::push_warning("GitBackend: fetch from '", remote_name, "' failed: ", error_message);
	}

	call_deferred("emit_signal", "fetch_finished", ok, error_message);
}

void GitBackend::pull_worker(String remote_name) {
	MutexLock lock(**repo_mutex);

	bool ok = false;
	String error_message;
	int merge_result = -1;

	if (!repo) {
		error_message = "No repository open.";
	} else if (git_repository_head_detached(repo) == 1) {
		error_message = "Cannot pull while HEAD is detached.";
	} else {
		String branch = get_current_branch_raw_name();
		if (branch.is_empty()) {
			error_message = "Cannot determine current branch.";
		} else {
			git_remote *remote = nullptr;
			if (git_remote_lookup(&remote, repo, remote_name.utf8().get_data()) != 0) {
				error_message = String("Remote '") + remote_name + String("' not found.");
			} else {
				git_fetch_options opts = GIT_FETCH_OPTIONS_INIT;
				opts.callbacks.credentials = &GitBackend::credentials_cb;
				opts.callbacks.payload = this;
				int rc = git_remote_fetch(remote, nullptr, &opts, "fetch");
				git_remote_free(remote);

				if (rc != 0) {
					const git_error *err = git_error_last();
					error_message = (err && err->message) ? String::utf8(err->message) : String("fetch failed");
				} else {
					String tracking_ref_name = String("refs/remotes/") + remote_name + String("/") + branch;
					git_reference *tracking_ref = nullptr;
					if (git_reference_lookup(&tracking_ref, repo, tracking_ref_name.utf8().get_data()) != 0) {
						error_message = "No upstream tracking ref for current branch.";
					} else {
						git_annotated_commit *their_head = nullptr;
						if (git_annotated_commit_from_ref(&their_head, repo, tracking_ref) != 0) {
							error_message = "Could not resolve fetched remote branch.";
						} else {
							merge_result = merge_annotated_into_current(remote_name + String("/") + branch, their_head, branch, &error_message);
							ok = merge_result != -1;
							git_annotated_commit_free(their_head);
						}
						git_reference_free(tracking_ref);
					}
				}
			}
		}
	}

	if (!ok) {
		UtilityFunctions::push_warning("GitBackend: pull from '", remote_name, "' failed: ", error_message);
	}

	call_deferred("emit_signal", "pull_finished", ok, error_message, merge_result);
}

String GitBackend::get_config_string(const String &key, const String &default_value) const {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return default_value;
	}
	git_config *cfg = nullptr;
	if (git_repository_config(&cfg, repo) != 0) {
		return default_value;
	}
	String result = default_value;
	const char *value = nullptr;
	if (git_config_get_string(&value, cfg, key.utf8().get_data()) == 0 && value) {
		result = String::utf8(value);
	}
	git_config_free(cfg);
	return result;
}

bool GitBackend::set_config_string(const String &key, const String &value) const {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return false;
	}
	git_config *cfg = nullptr;
	if (git_repository_config(&cfg, repo) != 0) {
		return false;
	}
	int rc = git_config_set_string(cfg, key.utf8().get_data(), value.utf8().get_data());
	git_config_free(cfg);
	if (rc != 0) {
		const git_error *err = git_error_last();
		UtilityFunctions::push_warning("GitBackend: failed to set config '", key, "': ", (err && err->message) ? err->message : "unknown error");
		return false;
	}
	return true;
}

TypedArray<Dictionary> GitBackend::list_remotes() const {
	MutexLock lock(**repo_mutex);
	TypedArray<Dictionary> result;
	if (!repo) {
		return result;
	}

	git_strarray remotes = { nullptr, 0 };
	if (git_remote_list(&remotes, repo) != 0) {
		return result;
	}

	for (size_t i = 0; i < remotes.count; i++) {
		const char *name = remotes.strings[i];
		git_remote *remote = nullptr;
		if (git_remote_lookup(&remote, repo, name) != 0) {
			continue;
		}
		Dictionary d;
		d["name"] = String::utf8(name);
		const char *url = git_remote_url(remote);
		d["url"] = url ? String::utf8(url) : String();
		const char *push_url = git_remote_pushurl(remote);
		d["push_url"] = push_url ? String::utf8(push_url) : String();
		result.push_back(d);
		git_remote_free(remote);
	}

	git_strarray_dispose(&remotes);
	return result;
}

bool GitBackend::set_remote_url(const String &name, const String &url) const {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return false;
	}

	CharString utf8_name = name.utf8();
	CharString utf8_url = url.utf8();

	git_remote *existing = nullptr;
	if (git_remote_lookup(&existing, repo, utf8_name.get_data()) == 0) {
		git_remote_free(existing);
		int rc = git_remote_set_url(repo, utf8_name.get_data(), utf8_url.get_data());
		if (rc != 0) {
			const git_error *err = git_error_last();
			UtilityFunctions::push_warning("GitBackend: failed to set URL for remote '", name, "': ", (err && err->message) ? err->message : "unknown error");
			return false;
		}
		return true;
	}

	git_remote *created = nullptr;
	int rc = git_remote_create(&created, repo, utf8_name.get_data(), utf8_url.get_data());
	if (rc != 0) {
		const git_error *err = git_error_last();
		UtilityFunctions::push_warning("GitBackend: failed to create remote '", name, "': ", (err && err->message) ? err->message : "unknown error");
		return false;
	}
	git_remote_free(created);
	return true;
}

bool GitBackend::remove_remote(const String &name) const {
	MutexLock lock(**repo_mutex);
	if (!repo) {
		return false;
	}

	int rc = git_remote_delete(repo, name.utf8().get_data());
	if (rc != 0) {
		const git_error *err = git_error_last();
		UtilityFunctions::push_warning("GitBackend: failed to remove remote '", name, "': ", (err && err->message) ? err->message : "unknown error");
		return false;
	}
	return true;
}

void GitBackend::set_ssh_passphrase(const String &passphrase) {
	ssh_key_passphrase = passphrase;
}
