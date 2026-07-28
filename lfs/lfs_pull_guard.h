#ifndef LFS_PULL_GUARD_H
#define LFS_PULL_GUARD_H

#include "core/object/ref_counted.h"
#include "core/os/thread.h"
#include "core/string/ustring.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class LfsRemoteClient;

// Post-pull quarantine + repair. snapshot()/quarantine_changed_pointers() are
// synchronous and meant to run back-to-back on the main thread right after a
// pull finishes, closing the race window before Godot's own filesystem
// watcher can notice a bad pointer file. repair() is async (network +
// hashing) and runs afterward on its own schedule, one path at a time.
//
// snapshot()/quarantine_changed_pointers() take pre-resolved tracked_patterns
// rather than a gitattributes path, since GitAttributesUtil (which reads and
// parses .gitattributes) stays GDScript — see CLAUDE.md's LFS migration notes.
class LfsPullGuard : public RefCounted {
	GDCLASS(LfsPullGuard, RefCounted);

protected:
	static void _bind_methods();

private:
	struct ValidateJob {
		LfsPullGuard *self = nullptr;
		String relative_path;
		String bytes_source_path;
		String oid;
		int64_t size = 0;
		bool from_cache = false;
	};

	String project_root;
	String remote_url;
	LfsRemoteClient *remote_client = nullptr;

	Array _paths;
	int _index = 0;
	Array _repaired_paths;
	Array _failed_paths;
	Thread *_active_thread = nullptr;

	static Dictionary _file_signature(const String &absolute_path);
	static bool _looks_like_pointer_file(const String &absolute_path);
	static bool _write_bytes(const String &path, const PackedByteArray &bytes);

	void _repair_next();
	void _on_repair_batch_result(String operation, Dictionary response, String error, String relative_path, String oid, int64_t size);
	void _on_repair_download_finished(String oid_param, String dest_path, bool ok, String error, String relative_path, String oid, int64_t size);
	void _finish_from_cache(const String &relative_path, const String &oid, int64_t size);
	void _start_validate_thread(const String &relative_path, const String &bytes_source_path, const String &oid, int64_t size, bool from_cache);
	static void _validate_thread_trampoline(void *p_userdata);
	void _finish_repair_step(const String &relative_path, bool ok);
	void _fail_repair_step(const String &relative_path);

public:
	static Dictionary snapshot(const String &project_root, const PackedStringArray &tracked_patterns);
	static PackedStringArray quarantine_changed_pointers(const String &project_root, const PackedStringArray &tracked_patterns, const Dictionary &pre_snapshot);

	void repair(const String &p_project_root, const String &p_remote_url, LfsRemoteClient *p_remote_client, const PackedStringArray &quarantined_paths);

	~LfsPullGuard();
};

#endif // LFS_PULL_GUARD_H
