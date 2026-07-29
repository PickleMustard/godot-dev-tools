#ifndef LFS_MANIFEST_H
#define LFS_MANIFEST_H

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/dictionary.h"

class LfsManifest : public RefCounted {
	GDCLASS(LfsManifest, RefCounted);

protected:
	static void _bind_methods();

private:
	static String _manifest_path(const String &project_root);

public:
	static const char *MANIFEST_FILENAME;

	static Dictionary load_manifest(const String &project_root);
	static void save_manifest(const String &project_root, const Dictionary &manifest);
	static void record_migrated(const String &project_root, const String &relative_path, const String &oid, int64_t size);
	static void remove_entry(const String &project_root, const String &relative_path);
	static bool is_current(const String &project_root, const String &relative_path, const Dictionary &manifest);
};

#endif // LFS_MANIFEST_H
