#include "lfs_status_scanner.h"

#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/templates/hash_set.h"

#include "lfs_manifest.h"
#include "lfs_object_store.h"
#include "lfs_pointer.h"
#include "lfs_quarantine.h"
#include "lfs_scanner.h"

namespace {
// GitAttributesUtil (GDScript) owns the full pattern-tracking file, but this
// one-line transform is duplicated here (matching GitAttributesUtil::extension_to_pattern
// exactly, including the defensive leading-dot strip and lowercasing) so this
// class doesn't need to reach back into GDScript for it — see CLAUDE.md's LFS
// migration notes.
String extension_to_pattern(const String &extension) {
	return "*." + extension.trim_prefix(".").to_lower();
}
} // namespace

void LfsStatusScanner::_bind_methods() {
	ClassDB::bind_static_method("LfsStatusScanner", D_METHOD("classify_file", "project_root", "relative_path", "is_pattern_tracked", "manifest"), &LfsStatusScanner::classify_file);
	ClassDB::bind_static_method("LfsStatusScanner", D_METHOD("scan", "project_root", "tracked_patterns", "manifest"), &LfsStatusScanner::scan);
	ClassDB::bind_static_method("LfsStatusScanner", D_METHOD("aggregate_by_pattern", "file_statuses"), &LfsStatusScanner::aggregate_by_pattern);
	ClassDB::bind_static_method("LfsStatusScanner", D_METHOD("is_expected_lfs_divergence", "project_root", "relative_path", "patterns", "manifest"), &LfsStatusScanner::is_expected_lfs_divergence);

	BIND_ENUM_CONSTANT(CLEAN);
	BIND_ENUM_CONSTANT(PENDING_TO_LFS);
	BIND_ENUM_CONSTANT(PENDING_FROM_LFS);
	BIND_ENUM_CONSTANT(TRACKED_OK);
	BIND_ENUM_CONSTANT(TRACKED_REMOTE_MISSING);
	BIND_ENUM_CONSTANT(QUARANTINED);
}

Dictionary LfsStatusScanner::classify_file(const String &project_root, const String &relative_path, bool is_pattern_tracked, const Dictionary &manifest) {
	String absolute_path = project_root.path_join(relative_path);

	if (FileAccess::exists(absolute_path + LfsQuarantine::SUFFIX)) {
		Dictionary d;
		d["status"] = QUARANTINED;
		return d;
	}

	if (!FileAccess::exists(absolute_path)) {
		Dictionary d;
		d["status"] = CLEAN;
		return d;
	}

	Ref<FileAccess> prefix_file = FileAccess::open(absolute_path, FileAccess::READ);
	if (prefix_file.is_null()) {
		Dictionary d;
		d["status"] = CLEAN;
		return d;
	}
	PackedByteArray prefix = prefix_file->get_buffer(MIN(prefix_file->get_length(), (int64_t)STATUS_PREFIX_SAMPLE_BYTES));
	prefix_file->close();

	if (LfsPointer::looks_like_pointer(prefix)) {
		String full_text;
		Ref<FileAccess> text_file = FileAccess::open(absolute_path, FileAccess::READ);
		if (text_file.is_valid()) {
			full_text = text_file->get_as_text();
			text_file->close();
		}
		Dictionary pointer = LfsPointer::parse(full_text);
		Dictionary d;
		if (bool(pointer.get("valid", false))) {
			d["status"] = TRACKED_OK;
			d["oid"] = pointer.get("oid", "");
			d["size"] = pointer.get("size", -1);
		} else {
			d["status"] = TRACKED_OK;
		}
		return d;
	}

	if (is_pattern_tracked) {
		if (LfsManifest::is_current(project_root, relative_path, manifest)) {
			Dictionary entry = manifest.get(relative_path, Dictionary());
			Dictionary d;
			d["status"] = TRACKED_OK;
			d["oid"] = entry.get("oid", "");
			d["size"] = entry.get("size", -1);
			return d;
		}
		Dictionary d;
		d["status"] = PENDING_TO_LFS;
		return d;
	}

	if (manifest.has(relative_path)) {
		Dictionary d;
		d["status"] = PENDING_FROM_LFS;
		return d;
	}

	Dictionary d;
	d["status"] = CLEAN;
	return d;
}

Array LfsStatusScanner::scan(const String &project_root, const PackedStringArray &tracked_patterns, const Dictionary &manifest) {
	HashSet<String> candidate_paths;

	for (const String &path : LfsScanner::scan_files_matching_patterns(project_root, tracked_patterns)) {
		candidate_paths.insert(path);
	}
	Array manifest_keys = manifest.keys();
	for (int i = 0; i < manifest_keys.size(); i++) {
		candidate_paths.insert(manifest_keys[i]);
	}
	for (const String &quarantined_relative : LfsQuarantine::list_quarantined(project_root)) {
		candidate_paths.insert(LfsQuarantine::real_path_for_quarantined(quarantined_relative));
	}

	Array results;
	for (const String &relative_path : candidate_paths) {
		String pattern = extension_to_pattern(relative_path.get_extension());
		bool is_tracked = tracked_patterns.has(pattern);
		Dictionary classification = classify_file(project_root, relative_path, is_tracked, manifest);
		classification["path"] = relative_path;
		classification["pattern"] = pattern;
		results.push_back(classification);
	}

	return results;
}

Dictionary LfsStatusScanner::aggregate_by_pattern(const Array &file_statuses) {
	Dictionary aggregate;
	for (int i = 0; i < file_statuses.size(); i++) {
		Dictionary e = file_statuses[i];
		String pattern = e.get("pattern", "");
		if (pattern.is_empty()) {
			continue;
		}
		if (!aggregate.has(pattern)) {
			Dictionary bucket;
			bucket["total"] = 0;
			bucket["pending_to_lfs"] = 0;
			bucket["pending_from_lfs"] = 0;
			bucket["remote_missing"] = 0;
			bucket["quarantined"] = 0;
			bucket["ok"] = 0;
			aggregate[pattern] = bucket;
		}
		Dictionary bucket = aggregate[pattern];
		bucket["total"] = int(bucket["total"]) + 1;
		int status = int(e.get("status", (int)CLEAN));
		switch (status) {
			case PENDING_TO_LFS:
				bucket["pending_to_lfs"] = int(bucket["pending_to_lfs"]) + 1;
				break;
			case PENDING_FROM_LFS:
				bucket["pending_from_lfs"] = int(bucket["pending_from_lfs"]) + 1;
				break;
			case TRACKED_REMOTE_MISSING:
				bucket["remote_missing"] = int(bucket["remote_missing"]) + 1;
				break;
			case QUARANTINED:
				bucket["quarantined"] = int(bucket["quarantined"]) + 1;
				break;
			case TRACKED_OK:
				bucket["ok"] = int(bucket["ok"]) + 1;
				break;
			default:
				break;
		}
		aggregate[pattern] = bucket;
	}
	return aggregate;
}

bool LfsStatusScanner::is_expected_lfs_divergence(const String &project_root, const String &relative_path, const PackedStringArray &patterns, const Dictionary &manifest) {
	String pattern = extension_to_pattern(relative_path.get_extension());
	if (!patterns.has(pattern)) {
		return false;
	}
	return LfsManifest::is_current(project_root, relative_path, manifest);
}
