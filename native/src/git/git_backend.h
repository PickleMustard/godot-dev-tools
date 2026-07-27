#ifndef GIT_BACKEND_H
#define GIT_BACKEND_H

#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <git2.h>

namespace godot {

class GitBackend : public RefCounted {
	GDCLASS(GitBackend, RefCounted);

protected:
	static void _bind_methods();

private:
	git_repository *repo = nullptr;
	mutable Ref<Mutex> repo_mutex;
	Ref<Thread> bg_thread;
	mutable Dictionary cached_ahead_behind;
	String ssh_key_passphrase;

	void close_repository();
	git_tree *resolve_head_tree() const;
	TypedArray<Dictionary> diff_to_array(git_diff *diff) const;
	git_reference *lookup_branch_ref(const String &name) const;
	String get_current_branch_raw_name() const;
	int merge_annotated_into_current(const String &source_display_name, git_annotated_commit *their_head,
			const String &target, String *out_error_message);
	bool checkout_ref_and_move_head(git_reference *ref, const String &local_branch_name);

	static int credentials_cb(git_credential **out, const char *url, const char *username_from_url,
			unsigned int allowed_types, void *payload);

	void fetch_worker(String remote_name);
	void pull_worker(String remote_name);

public:
	GitBackend();
	~GitBackend();

	bool open_repository(const String &path);
	bool init_repository(const String &path);

	String get_current_branch() const;
	String get_head_oid_hex() const;
	Dictionary get_status() const;
	TypedArray<Dictionary> get_diff_head() const;
	TypedArray<Dictionary> get_staged_diff() const;
	TypedArray<Dictionary> get_unstaged_diff() const;
	TypedArray<Dictionary> get_commit_history(int max_count, bool include_refs = true) const;

	TypedArray<Dictionary> list_branches() const;
	bool checkout_branch(const String &name);
	bool checkout_remote_branch(const String &remote_ref_name);
	bool create_branch(const String &name, bool checkout_after);
	int merge_branch(const String &source, const String &target);
	bool commit_staged(const String &message) const;
	bool stage_file(const String &path) const;
	bool unstage_file(const String &path) const;

	String get_repository_state() const;
	Dictionary get_rebase_progress() const;

	TypedArray<Dictionary> list_stashes() const;
	bool apply_stash(int index, bool pop) const;
	bool drop_stash(int index) const;

	Dictionary get_ahead_behind(const String &remote_name = "origin") const;
	bool start_fetch(const String &remote_name = "origin");
	bool start_pull(const String &remote_name = "origin");
	bool is_remote_op_busy() const;

	String get_config_string(const String &key, const String &default_value = "") const;
	bool set_config_string(const String &key, const String &value) const;

	TypedArray<Dictionary> list_remotes() const;
	bool set_remote_url(const String &name, const String &url) const;
	bool remove_remote(const String &name) const;

	void set_ssh_passphrase(const String &passphrase);
};

} // namespace godot

#endif // GIT_BACKEND_H
