#include "lfs_pointer.h"

#include "core/crypto/hashing_context.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"

namespace {
const char *SPEC_VERSION_LINE = "version https://git-lfs.github.com/spec/v1";
}

void LfsPointer::_bind_methods() {
	ClassDB::bind_static_method("LfsPointer", D_METHOD("looks_like_pointer", "bytes"), &LfsPointer::looks_like_pointer);
	ClassDB::bind_static_method("LfsPointer", D_METHOD("parse", "text"), &LfsPointer::parse);
	ClassDB::bind_static_method("LfsPointer", D_METHOD("build_pointer_text", "oid_hex", "size"), &LfsPointer::build_pointer_text);
	ClassDB::bind_static_method("LfsPointer", D_METHOD("compute_sha256_and_size", "file_path", "chunk_size"), &LfsPointer::compute_sha256_and_size, DEFVAL(READ_CHUNK_BYTES));
	ClassDB::bind_static_method("LfsPointer", D_METHOD("validate_file_against_pointer", "file_path", "pointer"), &LfsPointer::validate_file_against_pointer);
}

bool LfsPointer::looks_like_pointer(const PackedByteArray &bytes) {
	int sample_size = MIN(bytes.size(), POINTER_PREFIX_SAMPLE_BYTES);
	if (sample_size <= 0) {
		return false;
	}
	String prefix = String::utf8(reinterpret_cast<const char *>(bytes.ptr()), sample_size);
	return prefix.begins_with(SPEC_VERSION_LINE);
}

Dictionary LfsPointer::parse(const String &text) {
	Vector<String> lines = text.strip_edges().split("\n");
	if (lines.size() < 3) {
		return Dictionary();
	}
	if (lines[0].strip_edges() != String(SPEC_VERSION_LINE)) {
		return Dictionary();
	}

	String oid;
	int64_t size = -1;
	for (int i = 1; i < lines.size(); i++) {
		String stripped = lines[i].strip_edges();
		if (stripped.is_empty()) {
			continue;
		}
		Vector<String> tokens = stripped.split(" ", false, 1);
		if (tokens.size() != 2) {
			continue;
		}
		if (tokens[0] == "oid") {
			if (tokens[1].begins_with("sha256:")) {
				oid = tokens[1].trim_prefix("sha256:");
			}
		} else if (tokens[0] == "size") {
			if (tokens[1].is_valid_int()) {
				size = tokens[1].to_int();
			}
		}
	}

	if (oid.length() != 64 || size < 0) {
		return Dictionary();
	}

	Dictionary result;
	result["oid"] = oid;
	result["size"] = size;
	result["valid"] = true;
	return result;
}

String LfsPointer::build_pointer_text(const String &oid_hex, int64_t size) {
	return String(SPEC_VERSION_LINE) + "\noid sha256:" + oid_hex + "\nsize " + itos(size) + "\n";
}

Dictionary LfsPointer::compute_sha256_and_size(const String &file_path, int chunk_size) {
	Ref<FileAccess> file = FileAccess::open(file_path, FileAccess::READ);
	if (file.is_null()) {
		return Dictionary();
	}

	Ref<HashingContext> ctx;
	ctx.instantiate();
	ctx->start(HashingContext::HASH_SHA256);

	int64_t size = 0;
	while (!file->eof_reached()) {
		PackedByteArray chunk = file->get_buffer(chunk_size);
		if (chunk.is_empty()) {
			break;
		}
		ctx->update(chunk);
		size += chunk.size();
	}
	file->close();

	PackedByteArray digest = ctx->finish();

	Dictionary result;
	result["oid"] = String::hex_encode_buffer(digest.ptr(), digest.size());
	result["size"] = size;
	return result;
}

bool LfsPointer::validate_file_against_pointer(const String &file_path, const Dictionary &pointer) {
	if (!bool(pointer.get("valid", false))) {
		return false;
	}
	if (!FileAccess::exists(file_path)) {
		return false;
	}

	int64_t expected_size = pointer.get("size", -1);
	Ref<FileAccess> file = FileAccess::open(file_path, FileAccess::READ);
	if (file.is_null()) {
		return false;
	}
	int64_t actual_size = file->get_length();
	file->close();
	if (expected_size >= 0 && actual_size != expected_size) {
		return false;
	}

	Dictionary computed = compute_sha256_and_size(file_path);
	if (computed.is_empty()) {
		return false;
	}
	return String(computed.get("oid", "")) == String(pointer.get("oid", ""));
}
