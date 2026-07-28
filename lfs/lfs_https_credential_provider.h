#ifndef LFS_HTTPS_CREDENTIAL_PROVIDER_H
#define LFS_HTTPS_CREDENTIAL_PROVIDER_H

#include "core/variant/dictionary.h"

#include "lfs_credential_provider.h"

// HTTPS auth via the system `git credential fill` helper. Reuses whatever
// credential helper the user's git is already configured with (keychain,
// credential-manager, credential-store); this class never persists a secret
// itself.
class LfsHttpsCredentialProvider : public LfsCredentialProvider {
	GDCLASS(LfsHttpsCredentialProvider, LfsCredentialProvider);

protected:
	static void _bind_methods();

public:
	virtual bool supports(const String &remote_url) override;
	virtual void request_auth(const String &remote_url, const String &operation, const Callable &on_complete) override;

	static Dictionary _parse_https_url(const String &remote_url);
};

#endif // LFS_HTTPS_CREDENTIAL_PROVIDER_H
