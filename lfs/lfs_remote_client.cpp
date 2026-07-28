#include "lfs_remote_client.h"

#include "core/error/error_list.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/http_client.h"
#include "core/io/json.h"
#include "core/io/stream_peer_tls.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/object/callable_method_pointer.h"
#include "scene/main/http_request.h"

void LfsRemoteClient::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_credential_provider"), &LfsRemoteClient::get_credential_provider);
	ClassDB::bind_method(D_METHOD("set_credential_provider", "provider"), &LfsRemoteClient::set_credential_provider);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "credential_provider", PROPERTY_HINT_RESOURCE_TYPE, "LfsCredentialProvider"), "set_credential_provider", "get_credential_provider");

	ClassDB::bind_method(D_METHOD("setup", "provider"), &LfsRemoteClient::setup);
	ClassDB::bind_method(D_METHOD("base_batch_url", "remote_url"), &LfsRemoteClient::base_batch_url);
	ClassDB::bind_method(D_METHOD("check_objects", "remote_url", "objects", "operation"), &LfsRemoteClient::check_objects, DEFVAL(String("download")));
	ClassDB::bind_method(D_METHOD("download_object", "oid", "href", "header", "dest_path"), &LfsRemoteClient::download_object);
	ClassDB::bind_method(D_METHOD("upload_object", "oid", "href", "header", "bytes_path"), &LfsRemoteClient::upload_object);

	ADD_SIGNAL(MethodInfo("batch_result_ready",
			PropertyInfo(Variant::STRING, "operation"),
			PropertyInfo(Variant::DICTIONARY, "response"),
			PropertyInfo(Variant::STRING, "error")));
	ADD_SIGNAL(MethodInfo("download_finished",
			PropertyInfo(Variant::STRING, "oid"),
			PropertyInfo(Variant::STRING, "dest_path"),
			PropertyInfo(Variant::BOOL, "ok"),
			PropertyInfo(Variant::STRING, "error")));
	ADD_SIGNAL(MethodInfo("upload_finished",
			PropertyInfo(Variant::STRING, "oid"),
			PropertyInfo(Variant::BOOL, "ok"),
			PropertyInfo(Variant::STRING, "error")));
}

LfsRemoteClient::LfsRemoteClient() {
}

LfsRemoteClient::~LfsRemoteClient() {
	_join_all_upload_threads();
}

void LfsRemoteClient::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (credential_provider.is_null()) {
				credential_provider.instantiate();
			}

			_batch_request = memnew(HTTPRequest);
			add_child(_batch_request);
			_batch_request->connect("request_completed", callable_mp(this, &LfsRemoteClient::_on_batch_request_completed));

			_download_request = memnew(HTTPRequest);
			add_child(_download_request);
			_download_request->connect("request_completed", callable_mp(this, &LfsRemoteClient::_on_download_request_completed));
		} break;
		case NOTIFICATION_EXIT_TREE: {
			_join_all_upload_threads();
		} break;
	}
}

void LfsRemoteClient::_join_all_upload_threads() {
	for (KeyValue<String, Thread *> &kv : _upload_threads) {
		if (kv.value->is_started()) {
			kv.value->wait_to_finish();
		}
		memdelete(kv.value);
	}
	_upload_threads.clear();
}

Ref<LfsCredentialProvider> LfsRemoteClient::get_credential_provider() const {
	return credential_provider;
}

void LfsRemoteClient::set_credential_provider(const Ref<LfsCredentialProvider> &provider) {
	credential_provider = provider;
}

void LfsRemoteClient::setup(const Ref<LfsCredentialProvider> &provider) {
	credential_provider = provider;
}

String LfsRemoteClient::base_batch_url(const String &remote_url) const {
	String url = remote_url.strip_edges();
	if (url.is_empty()) {
		return String();
	}
	if (!url.ends_with(".git")) {
		url += ".git";
	}
	return url + "/info/lfs/objects/batch";
}

void LfsRemoteClient::check_objects(const String &remote_url, const Array &objects, const String &operation) {
	String default_url = base_batch_url(remote_url);
	if (default_url.is_empty()) {
		emit_signal("batch_result_ready", operation, Dictionary(), "Remote URL is empty.");
		return;
	}

	Callable on_complete = callable_mp(this, &LfsRemoteClient::_on_check_objects_auth_complete).bind(operation, objects, default_url);
	credential_provider->request_auth(remote_url, operation, on_complete);
}

void LfsRemoteClient::_on_check_objects_auth_complete(bool ok, Dictionary headers, String error, String batch_url_override, String operation, Array objects, String default_url) {
	if (!ok) {
		emit_signal("batch_result_ready", operation, Dictionary(), error);
		return;
	}

	String url = batch_url_override.is_empty() ? default_url : batch_url_override;

	Dictionary body_dict;
	body_dict["operation"] = operation;
	Array transfers;
	transfers.push_back("basic");
	body_dict["transfers"] = transfers;
	body_dict["objects"] = objects;
	String body = JSON::stringify(body_dict);

	Dictionary merged;
	merged["Accept"] = "application/vnd.git-lfs+json";
	merged["Content-Type"] = "application/vnd.git-lfs+json";
	Array header_keys = headers.keys();
	for (int i = 0; i < header_keys.size(); i++) {
		merged[header_keys[i]] = headers[header_keys[i]];
	}

	Error err = _batch_request->request(url, _headers_dict_to_array(merged), HTTPClient::METHOD_POST, body);
	if (err != OK) {
		emit_signal("batch_result_ready", operation, Dictionary(), vformat("Failed to start batch request (error %d).", (int)err));
	}
}

void LfsRemoteClient::_on_batch_request_completed(int result, int response_code, const PackedStringArray &headers, const PackedByteArray &body) {
	if (result != HTTPRequest::RESULT_SUCCESS || response_code < 200 || response_code >= 300) {
		emit_signal("batch_result_ready", "", Dictionary(), vformat("Batch request failed (result %d, HTTP %d).", result, response_code));
		return;
	}
	Variant parsed = JSON::parse_string(String::utf8(reinterpret_cast<const char *>(body.ptr()), body.size()));
	if (parsed.get_type() != Variant::DICTIONARY) {
		emit_signal("batch_result_ready", "", Dictionary(), "Batch response was not valid JSON.");
		return;
	}
	Dictionary parsed_dict = parsed;
	emit_signal("batch_result_ready", parsed_dict.get("transfer", "basic"), parsed_dict, "");
}

void LfsRemoteClient::download_object(const String &oid, const String &href, const Dictionary &header, const String &dest_path) {
	Error err = DirAccess::make_dir_recursive_absolute(dest_path.get_base_dir());
	if (err != OK && err != ERR_ALREADY_EXISTS) {
		emit_signal("download_finished", oid, dest_path, false, "Could not create destination directory.");
		return;
	}
	_pending_download_oid = oid;
	_pending_download_dest = dest_path;
	_download_request->set_download_file(dest_path);
	Error request_err = _download_request->request(href, _headers_dict_to_array(header), HTTPClient::METHOD_GET);
	if (request_err != OK) {
		emit_signal("download_finished", oid, dest_path, false, vformat("Failed to start download (error %d).", (int)request_err));
	}
}

void LfsRemoteClient::_on_download_request_completed(int result, int response_code, const PackedStringArray &headers, const PackedByteArray &body) {
	String oid = _pending_download_oid;
	String dest_path = _pending_download_dest;
	_pending_download_oid = String();
	_pending_download_dest = String();
	if (result != HTTPRequest::RESULT_SUCCESS || response_code < 200 || response_code >= 300) {
		emit_signal("download_finished", oid, dest_path, false, vformat("Download failed (result %d, HTTP %d).", result, response_code));
		return;
	}
	emit_signal("download_finished", oid, dest_path, true, "");
}

void LfsRemoteClient::upload_object(const String &oid, const String &href, const Dictionary &header, const String &bytes_path) {
	if (!FileAccess::exists(bytes_path)) {
		emit_signal("upload_finished", oid, false, vformat("Local object bytes not found at '%s'.", bytes_path));
		return;
	}

	UploadJob *job = memnew(UploadJob);
	job->self = this;
	job->oid = oid;
	job->href = href;
	job->header = header;
	job->bytes_path = bytes_path;

	Thread *thread = memnew(Thread);
	_upload_threads[oid] = thread;
	thread->start(&LfsRemoteClient::_upload_thread_trampoline, job);
}

void LfsRemoteClient::_upload_thread_trampoline(void *p_userdata) {
	UploadJob *job = static_cast<UploadJob *>(p_userdata);
	PackedByteArray bytes = FileAccess::get_file_as_bytes(job->bytes_path);
	Dictionary result = _upload_via_http_client(job->href, job->header, bytes);

	LfsRemoteClient *self = job->self;
	String oid = job->oid;
	memdelete(job);

	// Deferred via a bound Callable rather than call_deferred("_finish_upload", ...)
	// since this is a private, ClassDB-unbound method.
	callable_mp(self, &LfsRemoteClient::_finish_upload).call_deferred(oid, result);
}

void LfsRemoteClient::_finish_upload(const String &oid, const Dictionary &result) {
	Thread *thread = _upload_threads.has(oid) ? _upload_threads[oid] : nullptr;
	if (thread) {
		thread->wait_to_finish();
		memdelete(thread);
		_upload_threads.erase(oid);
	}
	emit_signal("upload_finished", oid, result.get("ok", false), result.get("error", ""));
}

Dictionary LfsRemoteClient::_upload_via_http_client(const String &href, const Dictionary &header, const PackedByteArray &bytes) {
	bool use_ssl = href.begins_with("https://");
	String rest = href.trim_prefix("https://").trim_prefix("http://");
	int slash_idx = rest.find("/");
	String host = slash_idx < 0 ? rest : rest.substr(0, slash_idx);
	String path = slash_idx < 0 ? String("/") : rest.substr(slash_idx);
	int port = use_ssl ? 443 : 80;

	int colon_idx = host.find(":");
	if (colon_idx >= 0) {
		port = host.substr(colon_idx + 1).to_int();
		host = host.substr(0, colon_idx);
	}

	Ref<HTTPClient> client = Ref<HTTPClient>(HTTPClient::create());
	Error connect_err = client->connect_to_host(host, port, use_ssl ? TLSOptions::client() : Ref<TLSOptions>());
	if (connect_err != OK) {
		Dictionary result;
		result["ok"] = false;
		result["error"] = vformat("Could not connect to '%s'.", host);
		return result;
	}

	while (client->get_status() == HTTPClient::STATUS_CONNECTING || client->get_status() == HTTPClient::STATUS_RESOLVING) {
		client->poll();
		OS::get_singleton()->delay_usec(10000);
	}

	if (client->get_status() != HTTPClient::STATUS_CONNECTED) {
		Dictionary result;
		result["ok"] = false;
		result["error"] = vformat("Connection to '%s' failed.", host);
		return result;
	}

	PackedStringArray request_headers;
	request_headers.push_back(vformat("Content-Length: %d", bytes.size()));
	Array header_keys = header.keys();
	for (int i = 0; i < header_keys.size(); i++) {
		request_headers.push_back(vformat("%s: %s", header_keys[i], header[header_keys[i]]));
	}

	Error request_err = client->request(HTTPClient::METHOD_PUT, path, request_headers, bytes.ptr(), bytes.size());
	if (request_err != OK) {
		Dictionary result;
		result["ok"] = false;
		result["error"] = "Failed to send upload request.";
		return result;
	}

	while (client->get_status() == HTTPClient::STATUS_REQUESTING) {
		client->poll();
		OS::get_singleton()->delay_usec(10000);
	}

	if (client->get_status() != HTTPClient::STATUS_BODY && client->get_status() != HTTPClient::STATUS_CONNECTED) {
		Dictionary result;
		result["ok"] = false;
		result["error"] = vformat("Upload request failed (client status %d).", (int)client->get_status());
		return result;
	}

	int response_code = client->get_response_code();
	while (client->get_status() == HTTPClient::STATUS_BODY) {
		client->poll();
		if (client->has_response()) {
			client->read_response_body_chunk();
		} else {
			break;
		}
	}

	Dictionary result;
	if (response_code < 200 || response_code >= 300) {
		result["ok"] = false;
		result["error"] = vformat("Upload failed (HTTP %d).", response_code);
		return result;
	}
	result["ok"] = true;
	result["error"] = "";
	return result;
}

PackedStringArray LfsRemoteClient::_headers_dict_to_array(const Dictionary &headers) {
	PackedStringArray result;
	Array keys = headers.keys();
	for (int i = 0; i < keys.size(); i++) {
		result.push_back(vformat("%s: %s", keys[i], headers[keys[i]]));
	}
	return result;
}
