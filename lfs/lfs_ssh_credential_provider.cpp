#include "lfs_ssh_credential_provider.h"

#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/templates/list.h"

#include "git/git_backend.h"

void LfsSshCredentialProvider::_bind_methods() {
	ClassDB::bind_static_method("LfsSshCredentialProvider", D_METHOD("_parse_ssh_url", "remote_url"), &LfsSshCredentialProvider::_parse_ssh_url);
}

bool LfsSshCredentialProvider::supports(const String &remote_url) {
	return remote_url.begins_with("git@") || remote_url.begins_with("ssh://");
}

void LfsSshCredentialProvider::request_auth(const String &remote_url, const String &operation, const Callable &on_complete, const String &endpoint) {
	Dictionary info = _parse_ssh_url(remote_url);
	if (info.is_empty()) {
		call_auth_result(on_complete, false, Dictionary(), "Could not parse SSH remote URL.");
		return;
	}

	List<String> args_list;
	args_list.push_back("-o");
	args_list.push_back("BatchMode=yes");
	args_list.push_back("-o");
	args_list.push_back("StrictHostKeyChecking=accept-new");

	String key_path;
	if (_git_backend.is_valid()) {
		key_path = _git_backend->get_config_string("devtools.sshkeypath", "");
	}
	if (!key_path.is_empty()) {
		args_list.push_back("-i");
		args_list.push_back(key_path);
		args_list.push_back("-o");
		args_list.push_back("IdentitiesOnly=yes");
	}

	int64_t port = info.get("port", 22);
	if (port != 22) {
		args_list.push_back("-p");
		args_list.push_back(itos(port));
	}

	String user = info.get("user", "git");
	String host = info.get("host", "");
	String path = info.get("path", "");

	args_list.push_back(user + "@" + host);
	args_list.push_back("git-lfs-authenticate");
	args_list.push_back(path);
	args_list.push_back(operation);

	String output;
	int exit_code = 0;
	OS::get_singleton()->execute("ssh", args_list, &output, &exit_code, true);
	if (exit_code != 0) {
		call_auth_result(on_complete, false, Dictionary(), vformat("SSH auth failed (exit %d): %s", exit_code, output));
		return;
	}

	Variant parsed = JSON::parse_string(output);
	Dictionary parsed_dict = parsed;
	if (parsed.get_type() != Variant::DICTIONARY || !parsed_dict.has("header") || !parsed_dict.has("href")) {
		call_auth_result(on_complete, false, Dictionary(), "git-lfs-authenticate returned an unexpected response.");
		return;
	}

	String href = parsed_dict["href"];
	if (href.ends_with("/")) {
		href = href.substr(0, href.length() - 1);
	}

	call_auth_result(on_complete, true, parsed_dict["header"], String(), href + "/" + endpoint);
}

Dictionary LfsSshCredentialProvider::_parse_ssh_url(const String &remote_url) {
	String url = remote_url.strip_edges();
	String user = "git";
	String host;
	int64_t port = 22;
	String path;

	if (url.begins_with("ssh://")) {
		url = url.substr(6);
		int slash_idx = url.find("/");
		String authority = slash_idx < 0 ? url : url.substr(0, slash_idx);
		path = slash_idx < 0 ? String() : url.substr(slash_idx + 1);
		int at_idx = authority.find("@");
		if (at_idx >= 0) {
			user = authority.substr(0, at_idx);
			authority = authority.substr(at_idx + 1);
		}
		int colon_idx = authority.find(":");
		if (colon_idx >= 0) {
			port = authority.substr(colon_idx + 1).to_int();
			host = authority.substr(0, colon_idx);
		} else {
			host = authority;
		}
	} else {
		int at_idx = url.find("@");
		String rest = url;
		if (at_idx >= 0) {
			user = url.substr(0, at_idx);
			rest = url.substr(at_idx + 1);
		}
		int colon_idx = rest.find(":");
		if (colon_idx < 0) {
			return Dictionary();
		}
		host = rest.substr(0, colon_idx);
		path = rest.substr(colon_idx + 1);
	}

	if (host.is_empty() || path.is_empty()) {
		return Dictionary();
	}

	Dictionary result;
	result["user"] = user;
	result["host"] = host;
	result["port"] = port;
	result["path"] = path;
	return result;
}
