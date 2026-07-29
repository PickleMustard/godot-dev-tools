#include "lfs_lock_manager.h"

#include "core/object/callable_method_pointer.h"
#include "core/object/class_db.h"
#include "core/os/thread.h"

LfsLockManager *LfsLockManager::singleton = nullptr;

void LfsLockManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("replace_locks", "server_locks"), &LfsLockManager::replace_locks);
	ClassDB::bind_method(D_METHOD("get_all_locks"), &LfsLockManager::get_all_locks);

	ClassDB::bind_method(D_METHOD("is_locked", "relative_path"), &LfsLockManager::is_locked);
	ClassDB::bind_method(D_METHOD("is_locked_by_other", "relative_path"), &LfsLockManager::is_locked_by_other);
	ClassDB::bind_method(D_METHOD("is_locked_by_me", "relative_path"), &LfsLockManager::is_locked_by_me);
	ClassDB::bind_method(D_METHOD("is_lock_confirmed_mine", "relative_path"), &LfsLockManager::is_lock_confirmed_mine);
	ClassDB::bind_method(D_METHOD("get_lock_for_path", "relative_path"), &LfsLockManager::get_lock_for_path);
	ClassDB::bind_method(D_METHOD("get_lock_owner_name", "relative_path"), &LfsLockManager::get_lock_owner_name);
	ClassDB::bind_method(D_METHOD("get_my_locked_paths"), &LfsLockManager::get_my_locked_paths);

	ClassDB::bind_method(D_METHOD("record_self_created_lock", "lock_id", "relative_path"), &LfsLockManager::record_self_created_lock);
	ClassDB::bind_method(D_METHOD("record_self_deleted_lock", "lock_id"), &LfsLockManager::record_self_deleted_lock);

	ClassDB::bind_method(D_METHOD("set_local_identity", "user_name", "credential_username"), &LfsLockManager::set_local_identity);

	ClassDB::bind_method(D_METHOD("set_max_locks", "max_locks"), &LfsLockManager::set_max_locks);
	ClassDB::bind_method(D_METHOD("get_max_locks"), &LfsLockManager::get_max_locks);
	ClassDB::bind_method(D_METHOD("get_my_lock_count"), &LfsLockManager::get_my_lock_count);
	ClassDB::bind_method(D_METHOD("can_acquire_more_locks"), &LfsLockManager::can_acquire_more_locks);

	ClassDB::bind_method(D_METHOD("enqueue_pending_lock", "relative_path"), &LfsLockManager::enqueue_pending_lock);
	ClassDB::bind_method(D_METHOD("enqueue_pending_unlock", "lock_id", "relative_path"), &LfsLockManager::enqueue_pending_unlock);
	ClassDB::bind_method(D_METHOD("get_pending_actions"), &LfsLockManager::get_pending_actions);
	ClassDB::bind_method(D_METHOD("clear_pending_action", "relative_path"), &LfsLockManager::clear_pending_action);

	ADD_SIGNAL(MethodInfo("locks_changed"));
	ADD_SIGNAL(MethodInfo("write_blocked",
			PropertyInfo(Variant::STRING, "path"),
			PropertyInfo(Variant::STRING, "owner_name")));
}

LfsLockManager *LfsLockManager::get_singleton() {
	return singleton;
}

String LfsLockManager::_normalize_path(const String &path) {
	String result = path;
	if (result.begins_with("res://")) {
		result = result.substr(6);
	}
	while (result.begins_with("/")) {
		result = result.substr(1);
	}
	return result;
}

void LfsLockManager::replace_locks(const Array &server_locks) {
	{
		MutexLock lock(_state_mutex);
		_locks_by_path.clear();
		for (int i = 0; i < server_locks.size(); i++) {
			Dictionary raw = server_locks[i];
			String relative_path = _normalize_path(raw.get("path", ""));
			if (relative_path.is_empty()) {
				continue;
			}
			Dictionary owner = raw.get("owner", Dictionary());
			Dictionary entry;
			entry["id"] = raw.get("id", "");
			entry["path"] = relative_path;
			entry["owner_name"] = owner.get("name", "");
			entry["locked_at"] = raw.get("locked_at", "");
			_locks_by_path[relative_path] = entry;
		}
	}
	emit_signal("locks_changed");
}

Array LfsLockManager::get_all_locks() const {
	MutexLock lock(_state_mutex);
	Array result;
	for (const KeyValue<String, Dictionary> &kv : _locks_by_path) {
		result.push_back(kv.value);
	}
	return result;
}

bool LfsLockManager::_is_locked_unlocked(const String &normalized_path) const {
	return _locks_by_path.has(normalized_path);
}

bool LfsLockManager::is_locked(const String &relative_path) const {
	MutexLock lock(_state_mutex);
	return _is_locked_unlocked(_normalize_path(relative_path));
}

bool LfsLockManager::is_lock_confirmed_mine(const String &relative_path) const {
	MutexLock lock(_state_mutex);
	return _self_created_lock_ids.has(_normalize_path(relative_path));
}

bool LfsLockManager::_is_locked_by_me_unlocked(const String &normalized_path) const {
	if (_self_created_lock_ids.has(normalized_path)) {
		return true;
	}
	const Dictionary *entry = _locks_by_path.getptr(normalized_path);
	if (entry == nullptr) {
		return false;
	}
	String owner_name = String(entry->get("owner_name", "")).strip_edges();
	if (owner_name.is_empty()) {
		return false;
	}
	if (!_local_user_name.is_empty() && owner_name.nocasecmp_to(_local_user_name) == 0) {
		return true;
	}
	if (!_local_credential_username.is_empty() && owner_name.nocasecmp_to(_local_credential_username) == 0) {
		return true;
	}
	return false;
}

bool LfsLockManager::is_locked_by_me(const String &relative_path) const {
	MutexLock lock(_state_mutex);
	return _is_locked_by_me_unlocked(_normalize_path(relative_path));
}

bool LfsLockManager::is_locked_by_other(const String &relative_path) const {
	MutexLock lock(_state_mutex);
	String normalized = _normalize_path(relative_path);
	return _is_locked_unlocked(normalized) && !_is_locked_by_me_unlocked(normalized);
}

Dictionary LfsLockManager::get_lock_for_path(const String &relative_path) const {
	MutexLock lock(_state_mutex);
	const Dictionary *entry = _locks_by_path.getptr(_normalize_path(relative_path));
	if (entry == nullptr) {
		return Dictionary();
	}
	return *entry;
}

String LfsLockManager::get_lock_owner_name(const String &relative_path) const {
	MutexLock lock(_state_mutex);
	const Dictionary *entry = _locks_by_path.getptr(_normalize_path(relative_path));
	if (entry == nullptr) {
		return String();
	}
	return entry->get("owner_name", "");
}

Array LfsLockManager::_get_my_locked_paths_unlocked() const {
	Array result;
	for (const KeyValue<String, Dictionary> &kv : _locks_by_path) {
		if (_is_locked_by_me_unlocked(kv.key)) {
			result.push_back(kv.key);
		}
	}
	return result;
}

Array LfsLockManager::get_my_locked_paths() const {
	MutexLock lock(_state_mutex);
	return _get_my_locked_paths_unlocked();
}

void LfsLockManager::record_self_created_lock(const String &lock_id, const String &relative_path) {
	{
		MutexLock lock(_state_mutex);
		_self_created_lock_ids[_normalize_path(relative_path)] = lock_id;
	}
	emit_signal("locks_changed");
}

void LfsLockManager::record_self_deleted_lock(const String &lock_id) {
	{
		MutexLock lock(_state_mutex);
		String matched_path;
		for (const KeyValue<String, String> &kv : _self_created_lock_ids) {
			if (kv.value == lock_id) {
				matched_path = kv.key;
				break;
			}
		}
		if (!matched_path.is_empty()) {
			_self_created_lock_ids.erase(matched_path);
			_locks_by_path.erase(matched_path);
		}
	}
	emit_signal("locks_changed");
}

void LfsLockManager::set_local_identity(const String &user_name, const String &credential_username) {
	MutexLock lock(_state_mutex);
	_local_user_name = user_name.strip_edges();
	_local_credential_username = credential_username.strip_edges();
}

void LfsLockManager::set_max_locks(int max_locks) {
	MutexLock lock(_state_mutex);
	_max_locks = max_locks;
}

int LfsLockManager::get_max_locks() const {
	MutexLock lock(_state_mutex);
	return _max_locks;
}

int LfsLockManager::get_my_lock_count() const {
	MutexLock lock(_state_mutex);
	return _get_my_locked_paths_unlocked().size();
}

bool LfsLockManager::can_acquire_more_locks() const {
	MutexLock lock(_state_mutex);
	if (_max_locks <= 0) {
		return true;
	}
	return _get_my_locked_paths_unlocked().size() < _max_locks;
}

void LfsLockManager::_clear_pending_action_unlocked(const String &normalized_path) {
	Array kept;
	for (int i = 0; i < _pending_actions.size(); i++) {
		Dictionary action = _pending_actions[i];
		if (String(action.get("path", "")) != normalized_path) {
			kept.push_back(action);
		}
	}
	_pending_actions = kept;
}

void LfsLockManager::enqueue_pending_lock(const String &relative_path) {
	MutexLock lock(_state_mutex);
	String normalized = _normalize_path(relative_path);
	_clear_pending_action_unlocked(normalized);
	Dictionary action;
	action["type"] = "lock";
	action["path"] = normalized;
	action["lock_id"] = "";
	_pending_actions.push_back(action);
}

void LfsLockManager::enqueue_pending_unlock(const String &lock_id, const String &relative_path) {
	MutexLock lock(_state_mutex);
	String normalized = _normalize_path(relative_path);
	_clear_pending_action_unlocked(normalized);
	Dictionary action;
	action["type"] = "unlock";
	action["path"] = normalized;
	action["lock_id"] = lock_id;
	_pending_actions.push_back(action);
}

Array LfsLockManager::get_pending_actions() const {
	MutexLock lock(_state_mutex);
	return _pending_actions.duplicate();
}

void LfsLockManager::clear_pending_action(const String &relative_path) {
	MutexLock lock(_state_mutex);
	_clear_pending_action_unlocked(_normalize_path(relative_path));
}

void LfsLockManager::notify_write_blocked(const String &relative_path, const String &owner_name) {
	if (Thread::is_main_thread()) {
		emit_signal("write_blocked", relative_path, owner_name);
	} else {
		callable_mp(this, &LfsLockManager::_notify_write_blocked_main).call_deferred(relative_path, owner_name);
	}
}

void LfsLockManager::_notify_write_blocked_main(const String &relative_path, const String &owner_name) {
	emit_signal("write_blocked", relative_path, owner_name);
}

LfsLockManager::LfsLockManager() {
	singleton = this;
}

LfsLockManager::~LfsLockManager() {
	singleton = nullptr;
}
