#ifndef LFS_SCANNER_H
#define LFS_SCANNER_H

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/callable.h"
#include "core/variant/variant.h"

#include <functional>

class LfsScanner : public RefCounted {
	GDCLASS(LfsScanner, RefCounted);

protected:
	static void _bind_methods();

public:
	static const int DEFAULT_SAMPLE_BYTES = 8192;

	static PackedStringArray scan_binary_extensions(const String &root_path, int sample_bytes = DEFAULT_SAMPLE_BYTES);
	static PackedStringArray scan_files_matching_patterns(const String &root_path, const PackedStringArray &patterns);
	static void walk_files(const String &dir_path, const Callable &visit_file);

	// C++-internal overload (not script-bound) for other core LFS classes that
	// need to walk the tree without paying the Callable/Variant marshalling cost.
	static void walk_files(const String &dir_path, const std::function<void(const String &file_path, const String &file_name)> &visit_file);
};

#endif // LFS_SCANNER_H
