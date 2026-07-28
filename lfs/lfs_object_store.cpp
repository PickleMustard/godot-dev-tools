#include "lfs_object_store.h"

#include "core/error/error_list.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"

const char *LfsObjectStore::LFS_OBJECTS_SUBPATH = "lfs/objects";

void LfsObjectStore::_bind_methods() {
	ClassDB::bind_static_method("LfsObjectStore", D_METHOD("resolve_git_dir", "project_root"), &LfsObjectStore::resolve_git_dir);
	ClassDB::bind_static_method("LfsObjectStore", D_METHOD("lfs_objects_dir", "project_root"), &LfsObjectStore::lfs_objects_dir);
	ClassDB::bind_static_method("LfsObjectStore", D_METHOD("object_path_for_oid", "project_root", "oid_hex"), &LfsObjectStore::object_path_for_oid);
	ClassDB::bind_static_method("LfsObjectStore", D_METHOD("has_cached_object", "project_root", "oid_hex", "expected_size"), &LfsObjectStore::has_cached_object, DEFVAL(-1));
	ClassDB::bind_static_method("LfsObjectStore", D_METHOD("store_object", "project_root", "oid_hex", "bytes"), &LfsObjectStore::store_object);
	ClassDB::bind_static_method("LfsObjectStore", D_METHOD("read_cached_object", "project_root", "oid_hex"), &LfsObjectStore::read_cached_object);
}

String LfsObjectStore::resolve_git_dir(const String &project_root) {
	String dotgit_path = project_root.path_join(".git");

	if (DirAccess::dir_exists_absolute(dotgit_path)) {
		return dotgit_path;
	}

	if (FileAccess::exists(dotgit_path)) {
		Ref<FileAccess> file = FileAccess::open(dotgit_path, FileAccess::READ);
		if (file.is_valid()) {
			String text = file->get_as_text();
			file->close();
			for (const String &line : text.split("\n")) {
				String stripped = line.strip_edges();
				if (stripped.begins_with("gitdir:")) {
					String raw_path = stripped.trim_prefix("gitdir:").strip_edges();
					if (raw_path.is_absolute_path()) {
						return raw_path.simplify_path();
					}
					return project_root.path_join(raw_path).simplify_path();
				}
			}
		}
	}

	return dotgit_path;
}

String LfsObjectStore::lfs_objects_dir(const String &project_root) {
	return resolve_git_dir(project_root).path_join(LFS_OBJECTS_SUBPATH);
}

String LfsObjectStore::object_path_for_oid(const String &project_root, const String &oid_hex) {
	if (oid_hex.length() < 4) {
		return String();
	}
	return lfs_objects_dir(project_root).path_join(oid_hex.substr(0, 2)).path_join(oid_hex.substr(2, 2)).path_join(oid_hex);
}

bool LfsObjectStore::has_cached_object(const String &project_root, const String &oid_hex, int64_t expected_size) {
	String path = object_path_for_oid(project_root, oid_hex);
	if (path.is_empty() || !FileAccess::exists(path)) {
		return false;
	}
	if (expected_size < 0) {
		return true;
	}
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (file.is_null()) {
		return false;
	}
	int64_t size = file->get_length();
	file->close();
	return size == expected_size;
}

bool LfsObjectStore::store_object(const String &project_root, const String &oid_hex, const PackedByteArray &bytes) {
	String path = object_path_for_oid(project_root, oid_hex);
	if (path.is_empty()) {
		return false;
	}
	String dir_path = path.get_base_dir();
	Error err = DirAccess::make_dir_recursive_absolute(dir_path);
	if (err != OK && err != ERR_ALREADY_EXISTS) {
		return false;
	}

	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null()) {
		return false;
	}
	file->store_buffer(bytes.ptr(), bytes.size());
	file->close();
	return true;
}

PackedByteArray LfsObjectStore::read_cached_object(const String &project_root, const String &oid_hex) {
	String path = object_path_for_oid(project_root, oid_hex);
	if (path.is_empty() || !FileAccess::exists(path)) {
		return PackedByteArray();
	}
	return FileAccess::get_file_as_bytes(path);
}
