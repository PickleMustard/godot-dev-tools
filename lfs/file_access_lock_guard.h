#ifndef FILE_ACCESS_LOCK_GUARD_H
#define FILE_ACCESS_LOCK_GUARD_H

// Registered (see register_types.cpp) as the platform-default FileAccess
// creator for ACCESS_RESOURCES and ACCESS_FILESYSTEM, so it sits underneath
// every res://-relative or already-globalized-absolute file write the editor
// makes -- a single chokepoint instead of patching every editor call site
// that can open a file for writing. Vetoes WRITE-mode opens of files
// LfsLockManager reports as locked by someone else; everything else
// (reads, unlocked files, files locked by the caller) passes straight
// through to the real platform backend.
#ifdef WINDOWS_ENABLED
#include "drivers/windows/file_access_windows.h"
using FileAccessLockGuardBase = FileAccessWindows;
#else // Linux and macOS both use the Unix backend for FileAccess.
#include "drivers/unix/file_access_unix.h"
using FileAccessLockGuardBase = FileAccessUnix;
#endif

class FileAccessLockGuard : public FileAccessLockGuardBase {
	GDSOFTCLASS(FileAccessLockGuard, FileAccessLockGuardBase);

public:
	virtual Error open_internal(const String &p_path, int p_mode_flags) override;

	FileAccessLockGuard() {}
};

#endif // FILE_ACCESS_LOCK_GUARD_H
