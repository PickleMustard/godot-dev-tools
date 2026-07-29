#include "lfs_quarantine.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"

#include "lfs_scanner.h"

const char *LfsQuarantine::SUFFIX = ".lfsbad";
const char *LfsQuarantine::REPAIR_PARTIAL_SUFFIX = ".part";

void LfsQuarantine::_bind_methods() {
	ClassDB::bind_static_method("LfsQuarantine", D_METHOD("quarantine_path_for", "path"), &LfsQuarantine::quarantine_path_for);
	ClassDB::bind_static_method("LfsQuarantine", D_METHOD("is_quarantined", "path"), &LfsQuarantine::is_quarantined);
	ClassDB::bind_static_method("LfsQuarantine", D_METHOD("real_path_for_quarantined", "quarantined_path"), &LfsQuarantine::real_path_for_quarantined);
	ClassDB::bind_static_method("LfsQuarantine", D_METHOD("quarantine_file", "absolute_path"), &LfsQuarantine::quarantine_file);
	ClassDB::bind_static_method("LfsQuarantine", D_METHOD("restore_file", "quarantined_absolute_path"), &LfsQuarantine::restore_file);
	ClassDB::bind_static_method("LfsQuarantine", D_METHOD("list_quarantined", "project_root"), &LfsQuarantine::list_quarantined);
}

String LfsQuarantine::quarantine_path_for(const String &path) {
	return path + SUFFIX;
}

bool LfsQuarantine::is_quarantined(const String &path) {
	return FileAccess::exists(path) && path.ends_with(SUFFIX);
}

String LfsQuarantine::real_path_for_quarantined(const String &quarantined_path) {
	return quarantined_path.trim_suffix(SUFFIX);
}

bool LfsQuarantine::quarantine_file(const String &absolute_path) {
	if (!FileAccess::exists(absolute_path)) {
		return false;
	}
	Ref<DirAccess> dir = DirAccess::open(absolute_path.get_base_dir());
	if (dir.is_null()) {
		return false;
	}
	String quarantined = quarantine_path_for(absolute_path);
	return dir->rename(absolute_path, quarantined) == OK;
}

bool LfsQuarantine::restore_file(const String &quarantined_absolute_path) {
	if (!FileAccess::exists(quarantined_absolute_path)) {
		return false;
	}
	Ref<DirAccess> dir = DirAccess::open(quarantined_absolute_path.get_base_dir());
	if (dir.is_null()) {
		return false;
	}
	String real_path = real_path_for_quarantined(quarantined_absolute_path);
	return dir->rename(quarantined_absolute_path, real_path) == OK;
}

PackedStringArray LfsQuarantine::list_quarantined(const String &project_root) {
	String root_normalized = project_root;
	if (!root_normalized.ends_with("/")) {
		root_normalized += "/";
	}

	Vector<String> result;
	LfsScanner::walk_files(project_root, [&result, &root_normalized](const String &file_path, const String &file_name) {
		if (file_name.ends_with(SUFFIX)) {
			result.push_back(file_path.trim_prefix(root_normalized));
		}
	});

	result.sort();
	return result;
}
