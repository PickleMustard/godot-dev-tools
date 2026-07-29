#ifndef LFS_LOCK_MANAGER_H
#define LFS_LOCK_MANAGER_H

#include "core/object/object.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

// In-memory lock-state authority for the LFS locking feature. Holds no I/O
// (that's LfsRemoteClient's job) -- purely a cache of "what does this
// session currently believe about locks" plus the small amount of policy
// (max-lock count, session-created-lock registry) both the GDScript UI and
// the engine-internals open/save veto need to query.
//
// Registered as an Engine singleton ("LfsLockManager") so editor/ code can
// reach it via Engine::get_singleton_object() + Object::call() without a
// compile-time dependency on this module (see register_types.cpp and the
// veto call sites in editor/editor_node.cpp, editor/docks/filesystem_dock.cpp,
// editor/script/script_editor_plugin.cpp).
//
// Threading: every mutator/query here is expected to run on the main
// thread only -- GDScript calls it from call_deferred callbacks, and the
// engine veto call sites are all main-thread-only editor operations
// (open/save). No mutex is used; do not add one without first checking
// that assumption still holds.
class LfsLockManager : public Object {
	GDCLASS(LfsLockManager, Object);

protected:
	static void _bind_methods();

private:
	static LfsLockManager *singleton;

	// Keyed by normalized relative path (no "res://" prefix, no leading "/").
	HashMap<String, Dictionary> _locks_by_path;

	// Locks this session created, path -> lock_id. Primary "is this mine"
	// signal -- lost on editor restart, hence the identity fallback below.
	HashMap<String, String> _self_created_lock_ids;

	String _local_user_name;
	String _local_credential_username;

	int _max_locks = 10;

	// Each entry: {"type": "lock"|"unlock", "path": String, "lock_id": String}
	Array _pending_actions;

	static String _normalize_path(const String &path);

public:
	static LfsLockManager *get_singleton();

	void replace_locks(const Array &server_locks);
	Array get_all_locks() const;

	bool is_locked(const String &relative_path) const;
	bool is_locked_by_other(const String &relative_path) const;
	bool is_locked_by_me(const String &relative_path) const;
	bool is_lock_confirmed_mine(const String &relative_path) const;
	Dictionary get_lock_for_path(const String &relative_path) const;
	String get_lock_owner_name(const String &relative_path) const;
	Array get_my_locked_paths() const;

	void record_self_created_lock(const String &lock_id, const String &relative_path);
	void record_self_deleted_lock(const String &lock_id);

	void set_local_identity(const String &user_name, const String &credential_username);

	void set_max_locks(int max_locks);
	int get_max_locks() const;
	int get_my_lock_count() const;
	bool can_acquire_more_locks() const;

	void enqueue_pending_lock(const String &relative_path);
	void enqueue_pending_unlock(const String &lock_id, const String &relative_path);
	Array get_pending_actions() const;
	void clear_pending_action(const String &relative_path);

	LfsLockManager();
	~LfsLockManager();
};

#endif // LFS_LOCK_MANAGER_H
