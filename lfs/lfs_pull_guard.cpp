#include "lfs_pull_guard.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/object/callable_method_pointer.h"

#include "lfs_object_store.h"
#include "lfs_pointer.h"
#include "lfs_quarantine.h"
#include "lfs_remote_client.h"
#include "lfs_scanner.h"

void LfsPullGuard::_bind_methods() {
	ClassDB::bind_static_method("LfsPullGuard", D_METHOD("snapshot", "project_root", "tracked_patterns"), &LfsPullGuard::snapshot);
	ClassDB::bind_static_method("LfsPullGuard", D_METHOD("quarantine_changed_pointers", "project_root", "tracked_patterns", "pre_snapshot"), &LfsPullGuard::quarantine_changed_pointers);
	ClassDB::bind_method(D_METHOD("repair", "project_root", "remote_url", "remote_client", "quarantined_paths"), &LfsPullGuard::repair);

	ADD_SIGNAL(MethodInfo("repaired", PropertyInfo(Variant::STRING, "relative_path")));
	ADD_SIGNAL(MethodInfo("repair_completed",
			PropertyInfo(Variant::ARRAY, "repaired_paths"),
			PropertyInfo(Variant::ARRAY, "failed_paths")));
}

LfsPullGuard::~LfsPullGuard() {
	if (_active_thread) {
		if (_active_thread->is_started()) {
			_active_thread->wait_to_finish();
		}
		memdelete(_active_thread);
	}
}

Dictionary LfsPullGuard::_file_signature(const String &absolute_path) {
	if (!FileAccess::exists(absolute_path)) {
		return Dictionary();
	}
	Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::READ);
	if (file.is_null()) {
		return Dictionary();
	}
	int64_t size = file->get_length();
	file->close();
	Dictionary sig;
	sig["size"] = size;
	sig["mtime"] = (int64_t)FileAccess::get_modified_time(absolute_path);
	return sig;
}

bool LfsPullGuard::_looks_like_pointer_file(const String &absolute_path) {
	Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::READ);
	if (file.is_null()) {
		return false;
	}
	PackedByteArray prefix = file->get_buffer(MIN((int64_t)file->get_length(), (int64_t)256));
	file->close();
	return LfsPointer::looks_like_pointer(prefix);
}

bool LfsPullGuard::_write_bytes(const String &path, const PackedByteArray &bytes) {
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null()) {
		return false;
	}
	file->store_buffer(bytes.ptr(), bytes.size());
	file->close();
	return true;
}

Dictionary LfsPullGuard::snapshot(const String &project_root, const PackedStringArray &tracked_patterns) {
	PackedStringArray files = LfsScanner::scan_files_matching_patterns(project_root, tracked_patterns);
	Dictionary sig;
	for (int i = 0; i < files.size(); i++) {
		sig[files[i]] = _file_signature(project_root.path_join(files[i]));
	}
	return sig;
}

PackedStringArray LfsPullGuard::quarantine_changed_pointers(const String &project_root, const PackedStringArray &tracked_patterns, const Dictionary &pre_snapshot) {
	PackedStringArray files = LfsScanner::scan_files_matching_patterns(project_root, tracked_patterns);
	Vector<String> quarantined;

	for (int i = 0; i < files.size(); i++) {
		const String &relative_path = files[i];
		String absolute_path = project_root.path_join(relative_path);
		Dictionary current_sig = _file_signature(absolute_path);
		bool changed = !pre_snapshot.has(relative_path) || Dictionary(pre_snapshot[relative_path]) != current_sig;
		if (!changed) {
			continue;
		}
		if (!_looks_like_pointer_file(absolute_path)) {
			continue;
		}
		if (LfsQuarantine::quarantine_file(absolute_path)) {
			quarantined.push_back(relative_path);
		}
	}

	return quarantined;
}

void LfsPullGuard::repair(const String &p_project_root, const String &p_remote_url, LfsRemoteClient *p_remote_client, const PackedStringArray &quarantined_paths) {
	project_root = p_project_root;
	remote_url = p_remote_url;
	remote_client = p_remote_client;
	_paths.clear();
	for (int i = 0; i < quarantined_paths.size(); i++) {
		_paths.push_back(quarantined_paths[i]);
	}
	_index = 0;
	_repaired_paths.clear();
	_failed_paths.clear();
	_repair_next();
}

void LfsPullGuard::_repair_next() {
	if (_index >= _paths.size()) {
		emit_signal("repair_completed", _repaired_paths, _failed_paths);
		return;
	}

	String relative_path = _paths[_index];
	String quarantined_absolute = project_root.path_join(relative_path) + LfsQuarantine::SUFFIX;
	if (!FileAccess::exists(quarantined_absolute)) {
		_fail_repair_step(relative_path);
		return;
	}

	Ref<FileAccess> text_file = FileAccess::open(quarantined_absolute, FileAccess::READ);
	String text;
	if (text_file.is_valid()) {
		text = text_file->get_as_text();
		text_file->close();
	}
	Dictionary pointer = LfsPointer::parse(text);
	if (!bool(pointer.get("valid", false))) {
		_fail_repair_step(relative_path);
		return;
	}

	String oid = pointer["oid"];
	int64_t size = pointer["size"];

	if (LfsObjectStore::has_cached_object(project_root, oid, size)) {
		_finish_from_cache(relative_path, oid, size);
		return;
	}

	if (remote_url.is_empty() || remote_client == nullptr) {
		_fail_repair_step(relative_path);
		return;
	}

	Dictionary object;
	object["oid"] = oid;
	object["size"] = size;
	Array objects;
	objects.push_back(object);

	remote_client->connect("batch_result_ready", callable_mp(this, &LfsPullGuard::_on_repair_batch_result).bind(relative_path, oid, size), Object::CONNECT_ONE_SHOT);
	remote_client->check_objects(remote_url, objects, "download");
}

void LfsPullGuard::_on_repair_batch_result(String operation, Dictionary response, String error, String relative_path, String oid, int64_t size) {
	if (!error.is_empty()) {
		_fail_repair_step(relative_path);
		return;
	}

	String href;
	Dictionary header;
	Array objects = response.get("objects", Array());
	for (int i = 0; i < objects.size(); i++) {
		Dictionary o = objects[i];
		if (String(o.get("oid", "")) != oid) {
			continue;
		}
		Dictionary actions = o.get("actions", Dictionary());
		if (actions.has("download")) {
			Dictionary dl = actions["download"];
			href = dl.get("href", "");
			header = dl.get("header", Dictionary());
		}
		break;
	}

	if (href.is_empty()) {
		_fail_repair_step(relative_path);
		return;
	}

	String partial_dest = project_root.path_join(relative_path) + LfsQuarantine::SUFFIX + LfsQuarantine::REPAIR_PARTIAL_SUFFIX;
	remote_client->connect("download_finished", callable_mp(this, &LfsPullGuard::_on_repair_download_finished).bind(relative_path, oid, size), Object::CONNECT_ONE_SHOT);
	remote_client->download_object(oid, href, header, partial_dest);
}

void LfsPullGuard::_on_repair_download_finished(String oid_param, String dest_path, bool ok, String error, String relative_path, String oid, int64_t size) {
	if (!ok) {
		if (FileAccess::exists(dest_path)) {
			DirAccess::remove_absolute(dest_path);
		}
		_fail_repair_step(relative_path);
		return;
	}
	_start_validate_thread(relative_path, dest_path, oid, size, false);
}

void LfsPullGuard::_finish_from_cache(const String &relative_path, const String &oid, int64_t size) {
	_start_validate_thread(relative_path, LfsObjectStore::object_path_for_oid(project_root, oid), oid, size, true);
}

void LfsPullGuard::_start_validate_thread(const String &relative_path, const String &bytes_source_path, const String &oid, int64_t size, bool from_cache) {
	ValidateJob *job = memnew(ValidateJob);
	job->self = this;
	job->relative_path = relative_path;
	job->bytes_source_path = bytes_source_path;
	job->oid = oid;
	job->size = size;
	job->from_cache = from_cache;

	_active_thread = memnew(Thread);
	_active_thread->start(&LfsPullGuard::_validate_thread_trampoline, job);
}

void LfsPullGuard::_validate_thread_trampoline(void *p_userdata) {
	ValidateJob *job = static_cast<ValidateJob *>(p_userdata);

	Dictionary pointer;
	pointer["oid"] = job->oid;
	pointer["size"] = job->size;
	pointer["valid"] = true;
	bool valid = LfsPointer::validate_file_against_pointer(job->bytes_source_path, pointer);
	bool ok = false;

	if (valid) {
		if (!job->from_cache) {
			LfsObjectStore::store_object(job->self->project_root, job->oid, FileAccess::get_file_as_bytes(job->bytes_source_path));
			DirAccess::remove_absolute(job->bytes_source_path);
		}
		String real_path = job->self->project_root.path_join(job->relative_path);
		String quarantined_path = real_path + LfsQuarantine::SUFFIX;
		ok = _write_bytes(real_path, LfsObjectStore::read_cached_object(job->self->project_root, job->oid));
		if (ok) {
			DirAccess::remove_absolute(quarantined_path);
		}
	} else if (!job->from_cache && FileAccess::exists(job->bytes_source_path)) {
		DirAccess::remove_absolute(job->bytes_source_path);
	}

	LfsPullGuard *self = job->self;
	String relative_path = job->relative_path;
	memdelete(job);

	// Deferred via a bound Callable (not call_deferred("_finish_repair_step", ...))
	// since this is a private, ClassDB-unbound method — see LfsRemoteClient's
	// _finish_upload for the same pattern.
	callable_mp(self, &LfsPullGuard::_finish_repair_step).call_deferred(relative_path, ok);
}

void LfsPullGuard::_finish_repair_step(const String &relative_path, bool ok) {
	if (_active_thread) {
		_active_thread->wait_to_finish();
		memdelete(_active_thread);
		_active_thread = nullptr;
	}
	if (ok) {
		_repaired_paths.push_back(relative_path);
		emit_signal("repaired", relative_path);
	} else {
		_failed_paths.push_back(relative_path);
	}
	_index += 1;
	_repair_next();
}

void LfsPullGuard::_fail_repair_step(const String &relative_path) {
	_failed_paths.push_back(relative_path);
	_index += 1;
	_repair_next();
}
