#include "lfs_scanner.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

#include <functional>

namespace {

using VisitFn = std::function<void(const String &, const String &)>;

bool is_nested_repo_root(const String &dir_path) {
	String git_path = dir_path.path_join(".git");
	return FileAccess::exists(git_path) || DirAccess::dir_exists_absolute(git_path);
}

bool should_skip_dir(const String &dir_name, const String &dir_path) {
	if (dir_name.begins_with(".") || dir_name == "addons") {
		return true;
	}
	return is_nested_repo_root(dir_path);
}

void walk_files_impl(const String &dir_path, const VisitFn &visit_file) {
	Ref<DirAccess> dir = DirAccess::open(dir_path);
	if (dir.is_null()) {
		return;
	}

	dir->list_dir_begin();
	String entry = dir->get_next();
	while (!entry.is_empty()) {
		if (entry == "." || entry == "..") {
			entry = dir->get_next();
			continue;
		}
		String full_path = dir_path.path_join(entry);
		if (dir->current_is_dir()) {
			if (!should_skip_dir(entry, full_path)) {
				walk_files_impl(full_path, visit_file);
			}
		} else {
			visit_file(full_path, entry);
		}
		entry = dir->get_next();
	}
	dir->list_dir_end();
}

bool is_valid_utf8(const PackedByteArray &bytes) {
	int i = 0;
	int n = bytes.size();
	while (i < n) {
		uint8_t b0 = bytes[i];
		int seq_len = 0;
		uint32_t codepoint = 0;

		if ((b0 & 0x80) == 0) {
			seq_len = 1;
			codepoint = b0;
		} else if ((b0 & 0xE0) == 0xC0) {
			seq_len = 2;
			codepoint = b0 & 0x1F;
		} else if ((b0 & 0xF0) == 0xE0) {
			seq_len = 3;
			codepoint = b0 & 0x0F;
		} else if ((b0 & 0xF8) == 0xF0) {
			seq_len = 4;
			codepoint = b0 & 0x07;
		} else {
			return false;
		}

		if (i + seq_len > n) {
			// Truncated multi-byte sequence at the edge of the sample window —
			// a cut boundary artifact, not evidence of invalid UTF-8.
			break;
		}

		for (int k = 1; k < seq_len; k++) {
			uint8_t bk = bytes[i + k];
			if ((bk & 0xC0) != 0x80) {
				return false;
			}
			codepoint = (codepoint << 6) | (bk & 0x3F);
		}

		if (seq_len == 2 && codepoint < 0x80) {
			return false;
		}
		if (seq_len == 3 && codepoint < 0x800) {
			return false;
		}
		if (seq_len == 4 && codepoint < 0x10000) {
			return false;
		}
		if (codepoint > 0x10FFFF) {
			return false;
		}
		if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
			return false;
		}

		i += seq_len;
	}

	return true;
}

bool is_binary_sample(const PackedByteArray &bytes) {
	for (int i = 0; i < bytes.size(); i++) {
		if (bytes[i] == 0) {
			return true;
		}
	}
	return !is_valid_utf8(bytes);
}

} // namespace

void LfsScanner::_bind_methods() {
	ClassDB::bind_static_method("LfsScanner", D_METHOD("scan_binary_extensions", "root_path", "sample_bytes"), &LfsScanner::scan_binary_extensions, DEFVAL(DEFAULT_SAMPLE_BYTES));
	ClassDB::bind_static_method("LfsScanner", D_METHOD("scan_files_matching_patterns", "root_path", "patterns"), &LfsScanner::scan_files_matching_patterns);
	ClassDB::bind_static_method("LfsScanner", D_METHOD("walk_files", "dir_path", "visit_file"), &LfsScanner::walk_files);
}

void LfsScanner::walk_files(const String &dir_path, const Callable &visit_file) {
	walk_files_impl(dir_path, [&visit_file](const String &file_path, const String &file_name) {
		Variant path_v = file_path;
		Variant name_v = file_name;
		const Variant *args[2] = { &path_v, &name_v };
		Variant ret;
		Callable::CallError ce;
		visit_file.callp(args, 2, ret, ce);
	});
}

void LfsScanner::walk_files(const String &dir_path, const VisitFn &visit_file) {
	walk_files_impl(dir_path, visit_file);
}

PackedStringArray LfsScanner::scan_binary_extensions(const String &root_path, int sample_bytes) {
	HashMap<String, String> representative_file_by_ext;
	walk_files_impl(root_path, [&representative_file_by_ext](const String &file_path, const String &file_name) {
		String ext = file_name.get_extension().to_lower();
		if (ext.is_empty()) {
			return;
		}
		if (!representative_file_by_ext.has(ext)) {
			representative_file_by_ext[ext] = file_path;
		}
	});

	Vector<String> result;
	for (const KeyValue<String, String> &kv : representative_file_by_ext) {
		Ref<FileAccess> file = FileAccess::open(kv.value, FileAccess::READ);
		if (file.is_null()) {
			continue;
		}
		int64_t length = file->get_length();
		PackedByteArray bytes = file->get_buffer(MIN((int64_t)sample_bytes, length));
		file->close();
		if (is_binary_sample(bytes)) {
			result.push_back(kv.key);
		}
	}

	result.sort();
	return result;
}

PackedStringArray LfsScanner::scan_files_matching_patterns(const String &root_path, const PackedStringArray &patterns) {
	HashSet<String> pattern_set;
	for (int i = 0; i < patterns.size(); i++) {
		pattern_set.insert(patterns[i].to_lower());
	}

	String root_normalized = root_path;
	if (!root_normalized.ends_with("/")) {
		root_normalized += "/";
	}

	Vector<String> result;
	walk_files_impl(root_path, [&pattern_set, &root_normalized, &result](const String &file_path, const String &file_name) {
		String ext = file_name.get_extension().to_lower();
		if (ext.is_empty()) {
			return;
		}
		if (pattern_set.has("*." + ext)) {
			result.push_back(file_path.trim_prefix(root_normalized));
		}
	});

	result.sort();
	return result;
}
