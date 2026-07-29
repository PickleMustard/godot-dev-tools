#ifndef DIR_ACCESS_LOCK_GUARD_H
#define DIR_ACCESS_LOCK_GUARD_H

// Registered (see register_types.cpp) as the platform-default DirAccess
// creator for ACCESS_RESOURCES and ACCESS_FILESYSTEM. Vetoes rename()/
// remove() of files LfsLockManager reports as locked by someone else --
// the "Perforce-style" companion to FileAccessLockGuard, blocking moves
// and deletes of a locked file in addition to overwrites.
#ifdef WINDOWS_ENABLED
#include "drivers/windows/dir_access_windows.h"
using DirAccessLockGuardBase = DirAccessWindows;
#elif defined(MACOS_ENABLED)
#include "platform/macos/dir_access_macos.h"
using DirAccessLockGuardBase = DirAccessMacOS;
#else // Other Unix-likes (Linux, *BSD).
#include "drivers/unix/dir_access_unix.h"
using DirAccessLockGuardBase = DirAccessUnix;
#endif

class DirAccessLockGuard : public DirAccessLockGuardBase {
	GDSOFTCLASS(DirAccessLockGuard, DirAccessLockGuardBase);

	// Resolves p_path against get_current_dir() exactly like the platform
	// backends' own rename()/remove() do internally, then consults the lock
	// table. Returns OK to proceed, or the error to return immediately.
	Error _check_not_locked(const String &p_path);

public:
	virtual Error rename(String p_from, String p_to) override;
	virtual Error remove(String p_name) override;

	DirAccessLockGuard() {}
};

#endif // DIR_ACCESS_LOCK_GUARD_H
