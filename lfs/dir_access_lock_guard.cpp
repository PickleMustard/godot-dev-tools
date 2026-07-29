#include "dir_access_lock_guard.h"

#include "core/config/project_settings.h"

#include "lfs_lock_manager.h"

namespace {

String normalize_relative_path(const String &p_path) {
	String result = p_path;
	if (result.begins_with("res://")) {
		result = result.substr(6);
	}
	while (result.begins_with("/")) {
		result = result.substr(1);
	}
	return result;
}

} // namespace

Error DirAccessLockGuard::_check_not_locked(const String &p_path) {
	String resolved = p_path;
	if (resolved.is_relative_path()) {
		resolved = get_current_dir().path_join(resolved);
	}

	String candidate;
	if (get_access_type() == DirAccess::ACCESS_RESOURCES) {
		// get_current_dir() already returns a "res://..."-rooted path for this
		// access type, so a path resolved against it is too.
		candidate = resolved;
	} else if (ProjectSettings::get_singleton()) {
		String localized = ProjectSettings::get_singleton()->localize_path(resolved);
		if (localized.begins_with("res://")) {
			candidate = localized;
		}
	}

	if (candidate.is_empty()) {
		return OK;
	}

	String normalized = normalize_relative_path(candidate);
	if (normalized.begins_with(".godot/") || normalized == ".godot") {
		return OK;
	}

	LfsLockManager *lock_mgr = LfsLockManager::get_singleton();
	if (lock_mgr && lock_mgr->is_locked_by_other(normalized)) {
		lock_mgr->notify_write_blocked(normalized, lock_mgr->get_lock_owner_name(normalized));
		return ERR_UNAUTHORIZED;
	}
	return OK;
}

Error DirAccessLockGuard::rename(String p_from, String p_to) {
	Error err = _check_not_locked(p_from);
	if (err != OK) {
		return err;
	}
	return DirAccessLockGuardBase::rename(p_from, p_to);
}

Error DirAccessLockGuard::remove(String p_name) {
	Error err = _check_not_locked(p_name);
	if (err != OK) {
		return err;
	}
	return DirAccessLockGuardBase::remove(p_name);
}
