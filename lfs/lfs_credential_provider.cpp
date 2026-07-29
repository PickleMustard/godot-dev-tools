#include "lfs_credential_provider.h"

#include "core/object/class_db.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

#include "git/git_backend.h"
#include "lfs_https_credential_provider.h"
#include "lfs_ssh_credential_provider.h"

void LfsCredentialProvider::_bind_methods() {
	ClassDB::bind_method(D_METHOD("supports", "remote_url"), &LfsCredentialProvider::supports);
	ClassDB::bind_method(D_METHOD("request_auth", "remote_url", "operation", "on_complete", "endpoint"), &LfsCredentialProvider::request_auth, DEFVAL(String("objects/batch")));
	ClassDB::bind_static_method("LfsCredentialProvider", D_METHOD("create_for_remote", "remote_url", "git_backend"), &LfsCredentialProvider::create_for_remote, DEFVAL(Ref<GitBackend>()));
}

bool LfsCredentialProvider::supports(const String &remote_url) {
	return true;
}

void LfsCredentialProvider::request_auth(const String &remote_url, const String &operation, const Callable &on_complete, const String &endpoint) {
	call_auth_result(on_complete, true, Dictionary(), String());
}

// static
void LfsCredentialProvider::call_auth_result(const Callable &on_complete, bool ok, const Dictionary &headers, const String &error, const String &batch_url_override) {
	// Fixed 4-arg contract (ok, headers, error, batch_url_override) so a
	// callable_mp-bound C++ member function can be used as the completion
	// target without needing GDScript-style default-argument support.
	Array args;
	args.push_back(ok);
	args.push_back(headers);
	args.push_back(error);
	args.push_back(batch_url_override);
	on_complete.callv(args);
}

Ref<LfsCredentialProvider> LfsCredentialProvider::create_for_remote(const String &remote_url, const Ref<GitBackend> &git_backend) {
	if (remote_url.begins_with("https://") || remote_url.begins_with("http://")) {
		Ref<LfsHttpsCredentialProvider> provider;
		provider.instantiate();
		return provider;
	}
	if (remote_url.begins_with("git@") || remote_url.begins_with("ssh://")) {
		Ref<LfsSshCredentialProvider> provider;
		provider.instantiate(git_backend);
		return provider;
	}
	Ref<LfsCredentialProvider> provider;
	provider.instantiate();
	return provider;
}
