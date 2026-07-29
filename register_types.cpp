#include "register_types.h"

#include "git/git_backend.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

#include <git2.h>

#include "lfs/dir_access_lock_guard.h"
#include "lfs/file_access_lock_guard.h"
#include "lfs/lfs_credential_provider.h"
#include "lfs/lfs_https_credential_provider.h"
#include "lfs/lfs_lock_manager.h"
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

static LfsLockManager *dev_tools_lfs_lock_manager = nullptr;

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
	GDREGISTER_CLASS(LfsLockManager);

	// Credential providers -- base before subclasses.
	GDREGISTER_CLASS(LfsCredentialProvider);
	GDREGISTER_CLASS(LfsHttpsCredentialProvider);
	GDREGISTER_CLASS(LfsSshCredentialProvider);

	// Network client + orchestration services.
	GDREGISTER_CLASS(LfsRemoteClient);
	GDREGISTER_CLASS(LfsPullGuard);
	GDREGISTER_CLASS(LfsPushService);
	GDREGISTER_CLASS(LfsRebuildService);

	// Lock-state singleton -- queried by the GDScript UI via
	// Engine.get_singleton("LfsLockManager"), and held directly by
	// FileAccessLockGuard/DirAccessLockGuard below since they compile
	// against this module's headers.
	dev_tools_lfs_lock_manager = memnew(LfsLockManager);
	Engine::get_singleton()->add_singleton(Engine::Singleton("LfsLockManager", LfsLockManager::get_singleton()));

	// Editor-only write/rename/delete veto for LFS-locked files. Overrides
	// the platform default FileAccess/DirAccess creators for ACCESS_RESOURCES
	// (res://) and ACCESS_FILESYSTEM (already-globalized absolute paths under
	// the project) -- a single chokepoint instead of patching every editor
	// call site that can open/rename/delete a file. Safe to install here:
	// OS::initialize() (which sets the platform's own FileAccess/DirAccess
	// make_default<...>()) runs at main.cpp's OS setup, strictly before
	// MODULE_INITIALIZATION_LEVEL_EDITOR, so nothing overwrites this after
	// the fact.
	FileAccess::make_default<FileAccessLockGuard>(FileAccess::ACCESS_RESOURCES);
	FileAccess::make_default<FileAccessLockGuard>(FileAccess::ACCESS_FILESYSTEM);
	DirAccess::make_default<DirAccessLockGuard>(DirAccess::ACCESS_RESOURCES);
	DirAccess::make_default<DirAccessLockGuard>(DirAccess::ACCESS_FILESYSTEM);
}

void uninitialize_dev_tools_git_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_EDITOR) {
		return;
	}

	if (dev_tools_lfs_lock_manager != nullptr) {
		Engine::get_singleton()->remove_singleton("LfsLockManager");
		memdelete(dev_tools_lfs_lock_manager);
		dev_tools_lfs_lock_manager = nullptr;
	}

	git_libgit2_shutdown();
}
