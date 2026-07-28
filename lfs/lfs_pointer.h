#ifndef LFS_POINTER_H
#define LFS_POINTER_H

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class LfsPointer : public RefCounted {
	GDCLASS(LfsPointer, RefCounted);

protected:
	static void _bind_methods();

public:
	static const int READ_CHUNK_BYTES = 65536;
	static const int POINTER_PREFIX_SAMPLE_BYTES = 64;

	static bool looks_like_pointer(const PackedByteArray &bytes);
	static Dictionary parse(const String &text);
	static String build_pointer_text(const String &oid_hex, int64_t size);
	static Dictionary compute_sha256_and_size(const String &file_path, int chunk_size = READ_CHUNK_BYTES);
	static bool validate_file_against_pointer(const String &file_path, const Dictionary &pointer);
};

#endif // LFS_POINTER_H
