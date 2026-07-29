#include "lfs_manifest.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/os/time.h"

#include "lfs_object_store.h"
#include "lfs_pointer.h"

const char *LfsManifest::MANIFEST_FILENAME = "dev_tools_lfs_manifest.json";

void LfsManifest::_bind_methods() {
	ClassDB::bind_static_method("LfsManifest", D_METHOD("load_manifest", "project_root"), &LfsManifest::load_manifest);
	ClassDB::bind_static_method("LfsManifest", D_METHOD("save_manifest", "project_root", "manifest"), &LfsManifest::save_manifest);
	ClassDB::bind_static_method("LfsManifest", D_METHOD("record_migrated", "project_root", "relative_path", "oid", "size"), &LfsManifest::record_migrated);
	ClassDB::bind_static_method("LfsManifest", D_METHOD("remove_entry", "project_root", "relative_path"), &LfsManifest::remove_entry);
	ClassDB::bind_static_method("LfsManifest", D_METHOD("is_current", "project_root", "relative_path", "manifest"), &LfsManifest::is_current);
}

String LfsManifest::_manifest_path(const String &project_root) {
	return LfsObjectStore::resolve_git_dir(project_root).path_join(MANIFEST_FILENAME);
}

Dictionary LfsManifest::load_manifest(const String &project_root) {
	String path = _manifest_path(project_root);
	if (!FileAccess::exists(path)) {
		return Dictionary();
	}
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (file.is_null()) {
		return Dictionary();
	}
	String text = file->get_as_text();
	file->close();
	Variant parsed = JSON::parse_string(text);
	if (parsed.get_type() == Variant::DICTIONARY) {
		return parsed;
	}
	return Dictionary();
}

void LfsManifest::save_manifest(const String &project_root, const Dictionary &manifest) {
	String path = _manifest_path(project_root);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null()) {
		return;
	}
	file->store_string(JSON::stringify(manifest, "\t"));
	file->close();
}

void LfsManifest::record_migrated(const String &project_root, const String &relative_path, const String &oid, int64_t size) {
	Dictionary manifest = load_manifest(project_root);
	Dictionary entry;
	entry["oid"] = oid;
	entry["size"] = size;
	entry["migrated_unix_time"] = Time::get_singleton()->get_unix_time_from_system();
	manifest[relative_path] = entry;
	save_manifest(project_root, manifest);
}

void LfsManifest::remove_entry(const String &project_root, const String &relative_path) {
	Dictionary manifest = load_manifest(project_root);
	if (manifest.has(relative_path)) {
		manifest.erase(relative_path);
		save_manifest(project_root, manifest);
	}
}

bool LfsManifest::is_current(const String &project_root, const String &relative_path, const Dictionary &manifest) {
	if (!manifest.has(relative_path)) {
		return false;
	}
	Dictionary entry = manifest[relative_path];
	String absolute_path = project_root.path_join(relative_path);
	if (!FileAccess::exists(absolute_path)) {
		return false;
	}
	Dictionary computed = LfsPointer::compute_sha256_and_size(absolute_path);
	if (computed.is_empty()) {
		return false;
	}
	return String(computed.get("oid", "")) == String(entry.get("oid", "")) && int64_t(computed.get("size", -1)) == int64_t(entry.get("size", -1));
}
