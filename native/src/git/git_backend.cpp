#include "git_backend.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

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

} // namespace

void GitBackend::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open_repository", "path"), &GitBackend::open_repository);
	ClassDB::bind_method(D_METHOD("init_repository", "path"), &GitBackend::init_repository);
	ClassDB::bind_method(D_METHOD("get_current_branch"), &GitBackend::get_current_branch);
	ClassDB::bind_method(D_METHOD("get_status"), &GitBackend::get_status);
	ClassDB::bind_method(D_METHOD("get_diff_head"), &GitBackend::get_diff_head);
	ClassDB::bind_method(D_METHOD("get_staged_diff"), &GitBackend::get_staged_diff);
	ClassDB::bind_method(D_METHOD("get_unstaged_diff"), &GitBackend::get_unstaged_diff);
	ClassDB::bind_method(D_METHOD("get_commit_history", "max_count"), &GitBackend::get_commit_history);
}

GitBackend::GitBackend() {
}

GitBackend::~GitBackend() {
	close_repository();
}

void GitBackend::close_repository() {
	if (repo) {
		git_repository_free(repo);
		repo = nullptr;
	}
}

bool GitBackend::open_repository(const String &path) {
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

Dictionary GitBackend::get_status() const {
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

TypedArray<Dictionary> GitBackend::get_commit_history(int max_count) const {
	TypedArray<Dictionary> result;
	if (!repo || max_count <= 0) {
		return result;
	}

	// oid hex -> short ref names (branch heads + tags) pointing at that commit.
	std::unordered_map<std::string, std::vector<std::string>> refs_by_oid;
	{
		git_reference_iterator *ref_iter = nullptr;
		if (git_reference_iterator_new(&ref_iter, repo) == 0) {
			git_reference *ref = nullptr;
			while (git_reference_next(&ref, ref_iter) == 0) {
				const char *ref_name = git_reference_name(ref);
				bool is_branch = ref_name && strncmp(ref_name, "refs/heads/", 11) == 0;
				bool is_tag = ref_name && strncmp(ref_name, "refs/tags/", 10) == 0;
				if (is_branch || is_tag) {
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
						refs_by_oid[key].push_back(std::string(ref_name + (is_branch ? 11 : 10)));
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
		std::vector<std::string> refs;
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
		for (const std::string &r : c.refs) {
			refs_arr.push_back(String(r.c_str()));
		}
		d["refs"] = refs_arr;
		result.push_back(d);
	}

	return result;
}
