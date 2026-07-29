#ifndef LFS_LOCK_MANAGER_H
#define LFS_LOCK_MANAGER_H

#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

// In-memory lock-state authority for the LFS locking feature. Holds no I/O
// (that's LfsRemoteClient's job) -- purely a cache of "what does this
// session currently believe about locks" plus the small amount of policy
// (max-lock count, session-created-lock registry) both the GDScript UI and
// the in-module FileAccessLockGuard/DirAccessLockGuard veto need to query.
//
// Registered as an Engine singleton ("LfsLockManager") for convenient
// GDScript access (Engine.get_singleton("LfsLockManager")); the C++ veto
// guards in this module (see file_access_lock_guard.h, dir_access_lock_guard.h)
// hold a direct pointer via get_singleton() instead, since they compile
// against this header directly.
//
// Threading: FileAccess::open()/DirAccess::rename()/remove() -- the guards'
// chokepoints -- can be invoked from resource-loader worker threads, not
// just the main thread. All state access is therefore guarded by
// _state_mutex. Signal emission (locks_changed, write_blocked) must never
// happen while holding the lock (a connected handler could re-enter this
// object and deadlock), and must be marshaled to the main thread if
// triggered from elsewhere (see notify_write_blocked()).
class LfsLockManager : public Object {
	GDCLASS(LfsLockManager, Object);

protected:
	static void _bind_methods();

private:
	static LfsLockManager *singleton;

	// Guards every field below. Take it in every public method; never emit
	// a signal while holding it (release first, then emit_signal).
	mutable Mutex _state_mutex;

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

	// Unlocked variants for internal reuse -- callers must already hold
	// _state_mutex. Public methods must never call each other directly
	// (that would double-lock a non-recursive Mutex); they call these instead.
	bool _is_locked_unlocked(const String &normalized_path) const;
	bool _is_locked_by_me_unlocked(const String &normalized_path) const;
	Array _get_my_locked_paths_unlocked() const;
	void _clear_pending_action_unlocked(const String &normalized_path);

	void _notify_write_blocked_main(const String &relative_path, const String &owner_name);

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

	// Called by FileAccessLockGuard/DirAccessLockGuard when a write/rename/
	// remove is vetoed. Safe to call from any thread -- marshals to the main
	// thread before emitting the write_blocked signal if necessary.
	void notify_write_blocked(const String &relative_path, const String &owner_name);

	LfsLockManager();
	~LfsLockManager();
};

#endif // LFS_LOCK_MANAGER_H
