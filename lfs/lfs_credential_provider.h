#ifndef LFS_CREDENTIAL_PROVIDER_H
#define LFS_CREDENTIAL_PROVIDER_H

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"

class GitBackend;

// Auth schema for the LFS Batch API. This base class is also the working
// no-auth implementation used for public remotes.
//
// request_auth() is deliberately async-shaped (Callable completion) rather
// than a plain return, since the SSH provider must exec `git-lfs-authenticate`
// and wait on it while the HTTPS provider resolves close to immediately.
// LfsRemoteClient only ever depends on this contract.
class LfsCredentialProvider : public RefCounted {
	GDCLASS(LfsCredentialProvider, RefCounted);

protected:
	static void _bind_methods();

public:
	virtual bool supports(const String &remote_url);

	// on_complete: Callable(ok: bool, headers: Dictionary, error: String, batch_url_override: String = "")
	// batch_url_override lets a provider replace the endpoint LfsRemoteClient
	// would otherwise derive from remote_url (needed for SSH remotes, where the
	// real endpoint only becomes known from the git-lfs-authenticate response).
	//
	// endpoint identifies which LFS API path is being authenticated for --
	// "objects/batch" (default) or "locks" -- so the SSH provider's
	// git-lfs-authenticate href can be suffixed correctly per-call instead of
	// hardcoding the batch path.
	virtual void request_auth(const String &remote_url, const String &operation, const Callable &on_complete, const String &endpoint = "objects/batch");

	// Shared helper: invokes an on_complete Callable with the fixed 4-arg
	// contract every provider uses, so subclasses don't each restate it.
	static void call_auth_result(const Callable &on_complete, bool ok, const Dictionary &headers, const String &error, const String &batch_url_override = String());

	// Selection seam: branches on remote_url scheme. git_backend is forwarded to
	// providers that need repo-local config (e.g. SSH key path).
	static Ref<LfsCredentialProvider> create_for_remote(const String &remote_url, const Ref<GitBackend> &git_backend = Ref<GitBackend>());

	virtual ~LfsCredentialProvider() {}
};

#endif // LFS_CREDENTIAL_PROVIDER_H
