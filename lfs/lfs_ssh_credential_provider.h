#ifndef LFS_SSH_CREDENTIAL_PROVIDER_H
#define LFS_SSH_CREDENTIAL_PROVIDER_H

#include "core/variant/dictionary.h"

#include "lfs_credential_provider.h"

class GitBackend;

// SSH auth via `ssh ... git-lfs-authenticate`, per the git-lfs SSH auth
// protocol. Relies on ssh-agent (or an unencrypted key) for passphrase-
// protected keys -- BatchMode blocks any interactive prompt, surfacing a
// clear error instead of hanging.
class LfsSshCredentialProvider : public LfsCredentialProvider {
	GDCLASS(LfsSshCredentialProvider, LfsCredentialProvider);

protected:
	static void _bind_methods();

private:
	Ref<GitBackend> _git_backend;

public:
	LfsSshCredentialProvider() {}
	LfsSshCredentialProvider(const Ref<GitBackend> &git_backend) :
			_git_backend(git_backend) {}

	virtual bool supports(const String &remote_url) override;
	virtual void request_auth(const String &remote_url, const String &operation, const Callable &on_complete, const String &endpoint = "objects/batch") override;

	static Dictionary _parse_ssh_url(const String &remote_url);
};

#endif // LFS_SSH_CREDENTIAL_PROVIDER_H
