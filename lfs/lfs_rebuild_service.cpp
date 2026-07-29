#include "lfs_rebuild_service.h"

#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/object/callable_method_pointer.h"

#include "git/git_backend.h"
#include "lfs_manifest.h"
#include "lfs_object_store.h"
#include "lfs_pointer.h"
#include "lfs_remote_client.h"
#include "lfs_status_scanner.h"

void LfsRebuildService::_bind_methods() {
	ClassDB::bind_method(D_METHOD("setup", "project_root", "git_backend", "remote_client", "remote_url"), &LfsRebuildService::setup);
	ClassDB::bind_method(D_METHOD("scan_pending", "tracked_patterns"), &LfsRebuildService::scan_pending);
	ClassDB::bind_method(D_METHOD("start", "to_migrate_in", "to_migrate_out"), &LfsRebuildService::start);

	ClassDB::bind_static_method("LfsRebuildService", D_METHOD("migrate_in_file", "project_root", "git_backend", "relative_path"), &LfsRebuildService::migrate_in_file);
	ClassDB::bind_static_method("LfsRebuildService", D_METHOD("migrate_out_file", "project_root", "git_backend", "relative_path", "real_bytes"), &LfsRebuildService::migrate_out_file);

	ADD_SIGNAL(MethodInfo("progress", PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("completed",
			PropertyInfo(Variant::INT, "migrated_in"),
			PropertyInfo(Variant::INT, "migrated_out"),
			PropertyInfo(Variant::ARRAY, "failed_paths")));
}

LfsRebuildService::~LfsRebuildService() {
	if (_active_thread) {
		if (_active_thread->is_started()) {
			_active_thread->wait_to_finish();
		}
		memdelete(_active_thread);
	}
}

void LfsRebuildService::setup(const String &p_project_root, const Ref<GitBackend> &p_git_backend, LfsRemoteClient *p_remote_client, const String &p_remote_url) {
	project_root = p_project_root;
	git_backend = p_git_backend;
	remote_client = p_remote_client;
	remote_url = p_remote_url;
}

Dictionary LfsRebuildService::scan_pending(const PackedStringArray &tracked_patterns) const {
	Dictionary manifest = LfsManifest::load_manifest(project_root);
	Array statuses = LfsStatusScanner::scan(project_root, tracked_patterns, manifest);

	Array to_in;
	Array to_out;
	for (int i = 0; i < statuses.size(); i++) {
		Dictionary e = statuses[i];
		int status = int(e.get("status", -1));
		if (status == LfsStatusScanner::PENDING_TO_LFS) {
			to_in.push_back(e["path"]);
		} else if (status == LfsStatusScanner::PENDING_FROM_LFS) {
			to_out.push_back(e["path"]);
		}
	}

	Dictionary result;
	result["to_migrate_in"] = to_in;
	result["to_migrate_out"] = to_out;
	return result;
}

void LfsRebuildService::start(const Array &to_migrate_in, const Array &to_migrate_out) {
	_to_migrate_in = to_migrate_in;
	_to_migrate_out = to_migrate_out;
	_migrated_in_count = 0;
	_migrated_out_count = 0;
	_failed_paths.clear();
	_pending_out_index = 0;
	_resolve_next_migrate_out();
}

void LfsRebuildService::_resolve_next_migrate_out() {
	if (_pending_out_index >= _to_migrate_out.size()) {
		_run_migrate_in_thread();
		return;
	}

	String relative_path = _to_migrate_out[_pending_out_index];
	emit_signal("progress", vformat("Resolving real bytes for '%s'...", relative_path));

	Dictionary manifest = LfsManifest::load_manifest(project_root);
	Dictionary entry = manifest.get(relative_path, Dictionary());
	String oid = entry.get("oid", "");
	int64_t size = int64_t(entry.get("size", -1));

	if (oid.is_empty()) {
		_fail_migrate_out_step(relative_path);
		return;
	}

	if (LfsObjectStore::has_cached_object(project_root, oid, size)) {
		_apply_migrate_out(relative_path, LfsObjectStore::read_cached_object(project_root, oid));
		return;
	}

	if (remote_url.is_empty() || remote_client == nullptr) {
		_fail_migrate_out_step(relative_path);
		return;
	}

	Dictionary object;
	object["oid"] = oid;
	object["size"] = size;
	Array objects;
	objects.push_back(object);

	remote_client->connect("batch_result_ready", callable_mp(this, &LfsRebuildService::_on_migrate_out_batch_result).bind(relative_path, oid, size), Object::CONNECT_ONE_SHOT);
	remote_client->check_objects(remote_url, objects, "download");
}

void LfsRebuildService::_on_migrate_out_batch_result(String operation, Dictionary response, String error, String relative_path, String oid, int64_t size) {
	if (!error.is_empty()) {
		_fail_migrate_out_step(relative_path);
		return;
	}

	String action_href;
	Dictionary action_header;
	Array objects = response.get("objects", Array());
	for (int i = 0; i < objects.size(); i++) {
		Dictionary o = objects[i];
		if (String(o.get("oid", "")) != oid) {
			continue;
		}
		Dictionary actions = o.get("actions", Dictionary());
		if (actions.has("download")) {
			Dictionary dl = actions["download"];
			action_href = dl.get("href", "");
			action_header = dl.get("header", Dictionary());
		}
		break;
	}

	if (action_href.is_empty()) {
		_fail_migrate_out_step(relative_path);
		return;
	}

	String dest = LfsObjectStore::object_path_for_oid(project_root, oid);
	remote_client->connect("download_finished", callable_mp(this, &LfsRebuildService::_on_migrate_out_download_finished).bind(relative_path), Object::CONNECT_ONE_SHOT);
	remote_client->download_object(oid, action_href, action_header, dest);
}

void LfsRebuildService::_on_migrate_out_download_finished(String oid, String dest_path, bool ok, String error, String relative_path) {
	if (!ok) {
		_fail_migrate_out_step(relative_path);
		return;
	}
	_apply_migrate_out(relative_path, FileAccess::get_file_as_bytes(dest_path));
}

void LfsRebuildService::_fail_migrate_out_step(const String &relative_path) {
	_failed_paths.push_back(relative_path);
	_pending_out_index += 1;
	_resolve_next_migrate_out();
}

void LfsRebuildService::_apply_migrate_out(const String &relative_path, const PackedByteArray &real_bytes) {
	MigrateOutJob *job = memnew(MigrateOutJob);
	job->self = this;
	job->relative_path = relative_path;
	job->real_bytes = real_bytes;

	_active_thread = memnew(Thread);
	_active_thread->start(&LfsRebuildService::_migrate_out_thread_trampoline, job);
}

void LfsRebuildService::_migrate_out_thread_trampoline(void *p_userdata) {
	MigrateOutJob *job = static_cast<MigrateOutJob *>(p_userdata);
	Dictionary result = migrate_out_file(job->self->project_root, job->self->git_backend, job->relative_path, job->real_bytes);

	LfsRebuildService *self = job->self;
	String relative_path = job->relative_path;
	memdelete(job);

	callable_mp(self, &LfsRebuildService::_finish_migrate_out_step).call_deferred(relative_path, result);
}

void LfsRebuildService::_finish_migrate_out_step(const String &relative_path, const Dictionary &result) {
	if (_active_thread) {
		_active_thread->wait_to_finish();
		memdelete(_active_thread);
		_active_thread = nullptr;
	}
	if (bool(result.get("ok", false))) {
		_migrated_out_count += 1;
	} else {
		_failed_paths.push_back(relative_path);
	}
	_pending_out_index += 1;
	_resolve_next_migrate_out();
}

void LfsRebuildService::_run_migrate_in_thread() {
	MigrateInJob *job = memnew(MigrateInJob);
	job->self = this;

	_active_thread = memnew(Thread);
	_active_thread->start(&LfsRebuildService::_migrate_in_thread_trampoline, job);
}

void LfsRebuildService::_migrate_in_thread_trampoline(void *p_userdata) {
	MigrateInJob *job = static_cast<MigrateInJob *>(p_userdata);
	LfsRebuildService *self = job->self;
	memdelete(job);

	Array failed;
	int count = 0;
	for (int i = 0; i < self->_to_migrate_in.size(); i++) {
		String relative_path = self->_to_migrate_in[i];
		Dictionary result = migrate_in_file(self->project_root, self->git_backend, relative_path);
		if (bool(result.get("ok", false))) {
			count += 1;
		} else {
			failed.push_back(relative_path);
		}
	}

	callable_mp(self, &LfsRebuildService::_finish_migrate_in).call_deferred(count, failed);
}

void LfsRebuildService::_finish_migrate_in(int count, const Array &failed) {
	if (_active_thread) {
		_active_thread->wait_to_finish();
		memdelete(_active_thread);
		_active_thread = nullptr;
	}
	_migrated_in_count += count;
	for (int i = 0; i < failed.size(); i++) {
		_failed_paths.push_back(failed[i]);
	}
	emit_signal("completed", _migrated_in_count, _migrated_out_count, _failed_paths);
}

Dictionary LfsRebuildService::migrate_in_file(const String &project_root, const Ref<GitBackend> &git_backend, const String &relative_path) {
	String absolute_path = project_root.path_join(relative_path);
	if (!FileAccess::exists(absolute_path)) {
		Dictionary d;
		d["ok"] = false;
		d["error"] = "File not found.";
		return d;
	}

	Dictionary computed = LfsPointer::compute_sha256_and_size(absolute_path);
	if (computed.is_empty()) {
		Dictionary d;
		d["ok"] = false;
		d["error"] = "Could not hash file.";
		return d;
	}
	String oid = computed["oid"];
	int64_t size = computed["size"];

	PackedByteArray real_bytes = FileAccess::get_file_as_bytes(absolute_path);
	if (!LfsObjectStore::store_object(project_root, oid, real_bytes)) {
		Dictionary d;
		d["ok"] = false;
		d["error"] = "Could not cache object bytes.";
		return d;
	}

	if (!_write_text(absolute_path, LfsPointer::build_pointer_text(oid, size))) {
		Dictionary d;
		d["ok"] = false;
		d["error"] = "Could not write pointer file.";
		return d;
	}

	if (!git_backend->stage_file(relative_path)) {
		_write_bytes(absolute_path, real_bytes);
		Dictionary d;
		d["ok"] = false;
		d["error"] = vformat("Could not stage pointer for '%s'.", relative_path);
		return d;
	}

	if (!_write_bytes(absolute_path, real_bytes)) {
		Dictionary d;
		d["ok"] = false;
		d["error"] = "Could not restore real bytes after staging.";
		return d;
	}

	LfsManifest::record_migrated(project_root, relative_path, oid, size);
	Dictionary d;
	d["ok"] = true;
	d["oid"] = oid;
	d["size"] = size;
	return d;
}

Dictionary LfsRebuildService::migrate_out_file(const String &project_root, const Ref<GitBackend> &git_backend, const String &relative_path, const PackedByteArray &real_bytes) {
	String absolute_path = project_root.path_join(relative_path);
	if (!_write_bytes(absolute_path, real_bytes)) {
		Dictionary d;
		d["ok"] = false;
		d["error"] = "Could not write real bytes.";
		return d;
	}
	if (!git_backend->stage_file(relative_path)) {
		Dictionary d;
		d["ok"] = false;
		d["error"] = vformat("Could not stage '%s'.", relative_path);
		return d;
	}
	LfsManifest::remove_entry(project_root, relative_path);
	Dictionary d;
	d["ok"] = true;
	return d;
}

bool LfsRebuildService::_write_text(const String &path, const String &text) {
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null()) {
		return false;
	}
	file->store_string(text);
	file->close();
	return true;
}

bool LfsRebuildService::_write_bytes(const String &path, const PackedByteArray &bytes) {
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null()) {
		return false;
	}
	file->store_buffer(bytes.ptr(), bytes.size());
	file->close();
	return true;
}
