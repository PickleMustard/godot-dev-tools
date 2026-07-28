#ifndef LFS_QUARANTINE_H
#define LFS_QUARANTINE_H

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/variant.h"

class LfsQuarantine : public RefCounted {
	GDCLASS(LfsQuarantine, RefCounted);

protected:
	static void _bind_methods();

public:
	static const char *SUFFIX;
	static const char *REPAIR_PARTIAL_SUFFIX;

	static String quarantine_path_for(const String &path);
	static bool is_quarantined(const String &path);
	static String real_path_for_quarantined(const String &quarantined_path);
	static bool quarantine_file(const String &absolute_path);
	static bool restore_file(const String &quarantined_absolute_path);
	static PackedStringArray list_quarantined(const String &project_root);
};

#endif // LFS_QUARANTINE_H
