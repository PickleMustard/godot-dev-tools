#include "register_types.h"

#include "git/git_backend.h"

#include "core/object/class_db.h"

#include <git2.h>

#include "lfs/lfs_credential_provider.h"
#include "lfs/lfs_https_credential_provider.h"
#include "lfs/lfs_manifest.h"
#include "lfs/lfs_object_store.h"
#include "lfs/lfs_pointer.h"
#include "lfs/lfs_pull_guard.h"
#include "lfs/lfs_push_service.h"
#include "lfs/lfs_quarantine.h"
#include "lfs/lfs_rebuild_service.h"
#include "lfs/lfs_remote_client.h"
#include "lfs/lfs_scanner.h"
#include "lfs/lfs_ssh_credential_provider.h"
#include "lfs/lfs_status_scanner.h"

void initialize_dev_tools_git_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_EDITOR) {
		return;
	}

	git_libgit2_init();

	GDREGISTER_CLASS(GitBackend);

	// Schema/data-layer LFS classes (no inheritance dependencies).
	GDREGISTER_CLASS(LfsPointer);
	GDREGISTER_CLASS(LfsManifest);
	GDREGISTER_CLASS(LfsObjectStore);
	GDREGISTER_CLASS(LfsScanner);
	GDREGISTER_CLASS(LfsQuarantine);
	GDREGISTER_CLASS(LfsStatusScanner);

	// Credential providers -- base before subclasses.
	GDREGISTER_CLASS(LfsCredentialProvider);
	GDREGISTER_CLASS(LfsHttpsCredentialProvider);
	GDREGISTER_CLASS(LfsSshCredentialProvider);

	// Network client + orchestration services.
	GDREGISTER_CLASS(LfsRemoteClient);
	GDREGISTER_CLASS(LfsPullGuard);
	GDREGISTER_CLASS(LfsPushService);
	GDREGISTER_CLASS(LfsRebuildService);
}

void uninitialize_dev_tools_git_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_EDITOR) {
		return;
	}

	git_libgit2_shutdown();
}
