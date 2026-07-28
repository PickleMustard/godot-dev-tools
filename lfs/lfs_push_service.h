#ifndef LFS_PUSH_SERVICE_H
#define LFS_PUSH_SERVICE_H

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

class LfsRemoteClient;

// Uploads LFS objects for TRACKED_REMOTE_MISSING entries via the Batch API.
// Only pushes objects, never commits/refs -- GitBackend has no push() at
// all, so a normal `git push` of the actual commits stays the user's
// existing external workflow.
class LfsPushService : public RefCounted {
	GDCLASS(LfsPushService, RefCounted);

protected:
	static void _bind_methods();

private:
	String project_root;
	Ref<LfsRemoteClient> remote_client;
	String remote_url;

	Array _entries;
	int _index = 0;
	int _pushed_count = 0;
	Array _failed_paths;

	void _push_next();
	void _on_push_batch_result(String operation, Dictionary response, String error, String relative_path, String oid);
	void _on_push_upload_finished(String oid, bool ok, String error, String relative_path);
	String _resolve_bytes_path(const String &relative_path, const String &oid) const;
	void _fail_step(const String &relative_path);

public:
	void setup(const String &p_project_root, const Ref<LfsRemoteClient> &p_remote_client, const String &p_remote_url);

	// entries: Array[{"path": String, "oid": String, "size": int}]
	void start(const Array &entries);
};

#endif // LFS_PUSH_SERVICE_H
