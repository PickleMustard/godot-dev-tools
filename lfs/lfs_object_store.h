#ifndef LFS_OBJECT_STORE_H
#define LFS_OBJECT_STORE_H

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/variant.h"

class LfsObjectStore : public RefCounted {
	GDCLASS(LfsObjectStore, RefCounted);

protected:
	static void _bind_methods();

public:
	static const char *LFS_OBJECTS_SUBPATH;

	static String resolve_git_dir(const String &project_root);
	static String lfs_objects_dir(const String &project_root);
	static String object_path_for_oid(const String &project_root, const String &oid_hex);
	static bool has_cached_object(const String &project_root, const String &oid_hex, int64_t expected_size = -1);
	static bool store_object(const String &project_root, const String &oid_hex, const PackedByteArray &bytes);
	static PackedByteArray read_cached_object(const String &project_root, const String &oid_hex);
};

#endif // LFS_OBJECT_STORE_H
