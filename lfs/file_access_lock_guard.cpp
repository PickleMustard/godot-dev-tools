#include "file_access_lock_guard.h"

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

Error FileAccessLockGuard::open_internal(const String &p_path, int p_mode_flags) {
	if (p_mode_flags & FileAccess::WRITE) {
		String candidate;
		if (get_access_type() == FileAccess::ACCESS_RESOURCES) {
			// p_path arrives unfixed here (still "res://..."), exactly what
			// LfsLockManager's path normalization already expects.
			candidate = p_path;
		} else if (ProjectSettings::get_singleton()) {
			// ACCESS_FILESYSTEM: some editor code globalizes to an absolute
			// path before opening. localize_path() returns the path unchanged
			// if it doesn't fall under the project -- only consult the lock
			// table for paths that resolve back into res://.
			String localized = ProjectSettings::get_singleton()->localize_path(p_path);
			if (localized.begins_with("res://")) {
				candidate = localized;
			}
		}

		if (!candidate.is_empty()) {
			String normalized = normalize_relative_path(candidate);
			// Don't interfere with the import cache / editor metadata.
			if (!normalized.begins_with(".godot/") && normalized != ".godot") {
				LfsLockManager *lock_mgr = LfsLockManager::get_singleton();
				if (lock_mgr && lock_mgr->is_locked_by_other(normalized)) {
					lock_mgr->notify_write_blocked(normalized, lock_mgr->get_lock_owner_name(normalized));
					return ERR_UNAUTHORIZED;
				}
			}
		}
	}

	return FileAccessLockGuardBase::open_internal(p_path, p_mode_flags);
}
