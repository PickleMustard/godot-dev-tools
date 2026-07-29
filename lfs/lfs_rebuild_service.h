#ifndef LFS_REBUILD_SERVICE_H
#define LFS_REBUILD_SERVICE_H

#include "core/object/ref_counted.h"
#include "core/os/thread.h"
#include "core/string/ustring.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class GitBackend;
class LfsRemoteClient;

// Drives "Rebuild LFS Tracking": migrates files matching newly-tracked
// patterns into LFS (stage-then-restore trick, see migrate_in_file) and
// files matching newly-untracked patterns back out (migrate_out_file).
// Caller (lfs_visualizer.gd) is expected to run scan_pending() on a
// background Thread, show a confirmation dialog with the counts, then call
// start() with the confirmed lists.
//
// scan_pending() takes pre-resolved tracked_patterns rather than a
// gitattributes path, since GitAttributesUtil stays GDScript — see
// CLAUDE.md's LFS migration notes.
class LfsRebuildService : public RefCounted {
	GDCLASS(LfsRebuildService, RefCounted);

protected:
	static void _bind_methods();

private:
	String project_root;
	Ref<GitBackend> git_backend;
	LfsRemoteClient *remote_client = nullptr;
	String remote_url;

	Array _to_migrate_in;
	Array _to_migrate_out;
	int _migrated_in_count = 0;
	int _migrated_out_count = 0;
	Array _failed_paths;
	int _pending_out_index = 0;
	Thread *_active_thread = nullptr;

	void _resolve_next_migrate_out();
	void _on_migrate_out_batch_result(String operation, Dictionary response, String error, String relative_path, String oid, int64_t size);
	void _on_migrate_out_download_finished(String oid, String dest_path, bool ok, String error, String relative_path);
	void _fail_migrate_out_step(const String &relative_path);
	void _apply_migrate_out(const String &relative_path, const PackedByteArray &real_bytes);
	void _finish_migrate_out_step(const String &relative_path, const Dictionary &result);
	void _run_migrate_in_thread();
	void _finish_migrate_in(int count, const Array &failed);

	struct MigrateOutJob {
		LfsRebuildService *self = nullptr;
		String relative_path;
		PackedByteArray real_bytes;
	};
	static void _migrate_out_thread_trampoline(void *p_userdata);

	struct MigrateInJob {
		LfsRebuildService *self = nullptr;
	};
	static void _migrate_in_thread_trampoline(void *p_userdata);

	static bool _write_text(const String &path, const String &text);
	static bool _write_bytes(const String &path, const PackedByteArray &bytes);

public:
	void setup(const String &p_project_root, const Ref<GitBackend> &p_git_backend, LfsRemoteClient *p_remote_client, const String &p_remote_url);

	// Safe to call from a background Thread.
	// Returns {"to_migrate_in": Array[String], "to_migrate_out": Array[String]}.
	Dictionary scan_pending(const PackedStringArray &tracked_patterns) const;

	// Call on the main thread after user confirmation. Emits `completed` when done.
	void start(const Array &to_migrate_in, const Array &to_migrate_out);

	// Stage-then-restore trick: index/HEAD ends up holding the pointer blob,
	// working tree keeps the real bytes -- relies on GitBackend::stage_file
	// re-reading disk content at call time.
	static Dictionary migrate_in_file(const String &project_root, const Ref<GitBackend> &git_backend, const String &relative_path);
	static Dictionary migrate_out_file(const String &project_root, const Ref<GitBackend> &git_backend, const String &relative_path, const PackedByteArray &real_bytes);

	~LfsRebuildService();
};

#endif // LFS_REBUILD_SERVICE_H
