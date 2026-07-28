#include "lfs_https_credential_provider.h"

#include "core/core_bind.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/templates/list.h"

void LfsHttpsCredentialProvider::_bind_methods() {
	ClassDB::bind_static_method("LfsHttpsCredentialProvider", D_METHOD("_parse_https_url", "remote_url"), &LfsHttpsCredentialProvider::_parse_https_url);
}

bool LfsHttpsCredentialProvider::supports(const String &remote_url) {
	return remote_url.begins_with("https://") || remote_url.begins_with("http://");
}

void LfsHttpsCredentialProvider::request_auth(const String &remote_url, const String &operation, const Callable &on_complete) {
	Dictionary info = _parse_https_url(remote_url);
	if (info.is_empty()) {
		call_auth_result(on_complete, false, Dictionary(), "Could not parse HTTPS remote URL.");
		return;
	}

	List<String> arguments;
	arguments.push_back("credential");
	arguments.push_back("fill");
	Dictionary process = OS::get_singleton()->execute_with_pipe("git", arguments);
	if (process.is_empty() || !process.has("stdio")) {
		call_auth_result(on_complete, false, Dictionary(), "Could not start 'git credential fill' (is git installed and on PATH?).");
		return;
	}

	Ref<FileAccess> pipe = process["stdio"];

	String scheme = info.get("scheme", "");
	String host = info.get("host", "");
	String path = info.get("path", "");
	String username = info.get("username", "");

	String input = "protocol=" + scheme + "\nhost=" + host + "\n";
	if (!path.is_empty()) {
		input += "path=" + path + "\n";
	}
	if (!username.is_empty()) {
		input += "username=" + username + "\n";
	}
	pipe->store_string(input + "\n");

	String username_out;
	String password_out;
	while (!pipe->eof_reached()) {
		String line = pipe->get_line();
		if (line.is_empty()) {
			break;
		}
		int eq = line.find("=");
		if (eq <= 0) {
			continue;
		}
		String key = line.substr(0, eq);
		String value = line.substr(eq + 1);
		if (key == "username") {
			username_out = value;
		} else if (key == "password") {
			password_out = value;
		}
	}

	if (password_out.is_empty()) {
		call_auth_result(on_complete, false, Dictionary(), "'git credential fill' returned no password/token. Configure a git credential helper.");
		return;
	}

	String auth = core_bind::Marshalls::get_singleton()->utf8_to_base64(username_out + ":" + password_out);
	Dictionary headers;
	headers["Authorization"] = "Basic " + auth;

	call_auth_result(on_complete, true, headers, String());
}

Dictionary LfsHttpsCredentialProvider::_parse_https_url(const String &remote_url) {
	String url = remote_url.strip_edges();
	String scheme = "https";
	if (url.begins_with("https://")) {
		url = url.substr(8);
	} else if (url.begins_with("http://")) {
		scheme = "http";
		url = url.substr(7);
	} else {
		return Dictionary();
	}

	int slash_idx = url.find("/");
	String authority = slash_idx < 0 ? url : url.substr(0, slash_idx);
	String path = slash_idx < 0 ? String() : url.substr(slash_idx + 1);
	String username;
	int at_idx = authority.find("@");
	if (at_idx >= 0) {
		username = authority.substr(0, at_idx);
		authority = authority.substr(at_idx + 1);
	}
	if (authority.is_empty()) {
		return Dictionary();
	}

	Dictionary result;
	result["scheme"] = scheme;
	result["host"] = authority;
	result["path"] = path;
	result["username"] = username;
	return result;
}
