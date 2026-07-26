#ifndef GIT_BACKEND_H
#define GIT_BACKEND_H

#include <godot_cpp/classes/ref_counted.hpp>
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

	void close_repository();
	git_tree *resolve_head_tree() const;
	TypedArray<Dictionary> diff_to_array(git_diff *diff) const;

public:
	GitBackend();
	~GitBackend();

	bool open_repository(const String &path);
	bool init_repository(const String &path);

	String get_current_branch() const;
	Dictionary get_status() const;
	TypedArray<Dictionary> get_diff_head() const;
	TypedArray<Dictionary> get_staged_diff() const;
	TypedArray<Dictionary> get_unstaged_diff() const;
	TypedArray<Dictionary> get_commit_history(int max_count) const;
};

} // namespace godot

#endif // GIT_BACKEND_H
