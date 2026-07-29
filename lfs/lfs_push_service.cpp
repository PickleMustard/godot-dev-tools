#include "lfs_push_service.h"

#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/object/callable_method_pointer.h"

#include "lfs_object_store.h"
#include "lfs_pointer.h"
#include "lfs_remote_client.h"

void LfsPushService::_bind_methods() {
	ClassDB::bind_method(D_METHOD("setup", "project_root", "remote_client", "remote_url"), &LfsPushService::setup);
	ClassDB::bind_method(D_METHOD("start", "entries"), &LfsPushService::start);

	ADD_SIGNAL(MethodInfo("progress", PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("completed",
			PropertyInfo(Variant::INT, "pushed"),
			PropertyInfo(Variant::ARRAY, "failed_paths")));
}

void LfsPushService::setup(const String &p_project_root, LfsRemoteClient *p_remote_client, const String &p_remote_url) {
	project_root = p_project_root;
	remote_client = p_remote_client;
	remote_url = p_remote_url;
}

void LfsPushService::start(const Array &entries) {
	_entries = entries;
	_index = 0;
	_pushed_count = 0;
	_failed_paths.clear();
	_push_next();
}

void LfsPushService::_push_next() {
	if (_index >= _entries.size()) {
		emit_signal("completed", _pushed_count, _failed_paths);
		return;
	}

	Dictionary entry = _entries[_index];
	String relative_path = entry.get("path", "");
	String oid = entry.get("oid", "");
	int64_t size = int64_t(entry.get("size", -1));
	emit_signal("progress", vformat("Checking remote for '%s'...", relative_path));

	if (remote_url.is_empty() || remote_client == nullptr || oid.is_empty()) {
		_fail_step(relative_path);
		return;
	}

	Dictionary object;
	object["oid"] = oid;
	object["size"] = size;
	Array objects;
	objects.push_back(object);

	remote_client->connect("batch_result_ready", callable_mp(this, &LfsPushService::_on_push_batch_result).bind(relative_path, oid), Object::CONNECT_ONE_SHOT);
	remote_client->check_objects(remote_url, objects, "upload");
}

void LfsPushService::_on_push_batch_result(String operation, Dictionary response, String error, String relative_path, String oid) {
	if (!error.is_empty()) {
		_fail_step(relative_path);
		return;
	}

	String upload_href;
	Dictionary upload_header;
	bool needs_upload = false;
	Array objects = response.get("objects", Array());
	for (int i = 0; i < objects.size(); i++) {
		Dictionary o = objects[i];
		if (String(o.get("oid", "")) != oid) {
			continue;
		}
		Dictionary actions = o.get("actions", Dictionary());
		if (actions.has("upload")) {
			needs_upload = true;
			Dictionary up = actions["upload"];
			upload_href = up.get("href", "");
			upload_header = up.get("header", Dictionary());
		}
		break;
	}

	if (!needs_upload) {
		// Server already has this object -- nothing to do.
		_pushed_count += 1;
		_index += 1;
		_push_next();
		return;
	}

	if (upload_href.is_empty()) {
		_fail_step(relative_path);
		return;
	}

	String bytes_path = _resolve_bytes_path(relative_path, oid);
	if (bytes_path.is_empty()) {
		_fail_step(relative_path);
		return;
	}

	remote_client->connect("upload_finished", callable_mp(this, &LfsPushService::_on_push_upload_finished).bind(relative_path), Object::CONNECT_ONE_SHOT);
	remote_client->upload_object(oid, upload_href, upload_header, bytes_path);
}

String LfsPushService::_resolve_bytes_path(const String &relative_path, const String &oid) const {
	String cached = LfsObjectStore::object_path_for_oid(project_root, oid);
	if (FileAccess::exists(cached)) {
		return cached;
	}

	String absolute_path = project_root.path_join(relative_path);
	if (!FileAccess::exists(absolute_path)) {
		return String();
	}
	Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::READ);
	if (file.is_null()) {
		return String();
	}
	PackedByteArray prefix = file->get_buffer(MIN((int64_t)file->get_length(), (int64_t)256));
	file->close();
	if (LfsPointer::looks_like_pointer(prefix)) {
		return String();
	}
	return absolute_path;
}

void LfsPushService::_on_push_upload_finished(String oid, bool ok, String error, String relative_path) {
	if (ok) {
		_pushed_count += 1;
	} else {
		_failed_paths.push_back(relative_path);
	}
	_index += 1;
	_push_next();
}

void LfsPushService::_fail_step(const String &relative_path) {
	_failed_paths.push_back(relative_path);
	_index += 1;
	_push_next();
}
