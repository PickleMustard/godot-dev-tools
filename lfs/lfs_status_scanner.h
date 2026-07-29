#ifndef LFS_STATUS_SCANNER_H
#define LFS_STATUS_SCANNER_H

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class LfsStatusScanner : public RefCounted {
	GDCLASS(LfsStatusScanner, RefCounted);

public:
	enum Status {
		CLEAN,
		PENDING_TO_LFS,
		PENDING_FROM_LFS,
		TRACKED_OK,
		TRACKED_REMOTE_MISSING,
		QUARANTINED,
	};

protected:
	static void _bind_methods();

public:
	static const int STATUS_PREFIX_SAMPLE_BYTES = 256;

	static Dictionary classify_file(const String &project_root, const String &relative_path, bool is_pattern_tracked, const Dictionary &manifest);
	static Array scan(const String &project_root, const PackedStringArray &tracked_patterns, const Dictionary &manifest);
	static Dictionary aggregate_by_pattern(const Array &file_statuses);
	static bool is_expected_lfs_divergence(const String &project_root, const String &relative_path, const PackedStringArray &patterns, const Dictionary &manifest);
};

VARIANT_ENUM_CAST(LfsStatusScanner::Status);

#endif // LFS_STATUS_SCANNER_H
