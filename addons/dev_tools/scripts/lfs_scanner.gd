@tool
class_name LfsScanner
extends RefCounted

const SKIP_DIR_NAMES := ["addons"]
const DEFAULT_SAMPLE_BYTES := 8192


static func scan_binary_extensions(root_path: String, sample_bytes: int = DEFAULT_SAMPLE_BYTES) -> PackedStringArray:
	var representative_file_by_ext: Dictionary = {}
	_walk(root_path, func(file_path: String, file_name: String) -> void:
		var ext := file_name.get_extension().to_lower()
		if ext.is_empty():
			return
		if not representative_file_by_ext.has(ext):
			representative_file_by_ext[ext] = file_path
	)

	var result: Array = []
	for ext in representative_file_by_ext.keys():
		var file_path: String = representative_file_by_ext[ext]
		var file := FileAccess.open(file_path, FileAccess.READ)
		if file == null:
			continue
		var length: int = file.get_length()
		var bytes := file.get_buffer(min(sample_bytes, length))
		file.close()
		if _is_binary_sample(bytes):
			result.append(ext)

	result.sort()
	return PackedStringArray(result)


static func scan_files_matching_patterns(root_path: String, patterns: PackedStringArray) -> PackedStringArray:
	var pattern_set: Dictionary = {}
	for pattern in patterns:
		pattern_set[String(pattern).to_lower()] = true

	var root_normalized := root_path
	if not root_normalized.ends_with("/"):
		root_normalized += "/"

	var result: Array = []
	_walk(root_path, func(file_path: String, file_name: String) -> void:
		var ext := file_name.get_extension().to_lower()
		if ext.is_empty():
			return
		if pattern_set.has("*." + ext):
			result.append(file_path.trim_prefix(root_normalized))
	)

	result.sort()
	return PackedStringArray(result)


static func _walk(dir_path: String, visit_file: Callable) -> void:
	var dir := DirAccess.open(dir_path)
	if dir == null:
		return

	dir.list_dir_begin()
	var entry := dir.get_next()
	while entry != "":
		if entry == "." or entry == "..":
			entry = dir.get_next()
			continue
		var full_path := dir_path.path_join(entry)
		if dir.current_is_dir():
			if not _should_skip_dir(entry):
				_walk(full_path, visit_file)
		else:
			visit_file.call(full_path, entry)
		entry = dir.get_next()
	dir.list_dir_end()


static func _should_skip_dir(dir_name: String) -> bool:
	return dir_name.begins_with(".") or SKIP_DIR_NAMES.has(dir_name)


static func _is_binary_sample(bytes: PackedByteArray) -> bool:
	for b in bytes:
		if b == 0:
			return true
	return not _is_valid_utf8(bytes)


static func _is_valid_utf8(bytes: PackedByteArray) -> bool:
	var i := 0
	var n := bytes.size()
	while i < n:
		var b0: int = bytes[i]
		var seq_len := 0
		var codepoint := 0

		if b0 & 0x80 == 0:
			seq_len = 1
			codepoint = b0
		elif b0 & 0xE0 == 0xC0:
			seq_len = 2
			codepoint = b0 & 0x1F
		elif b0 & 0xF0 == 0xE0:
			seq_len = 3
			codepoint = b0 & 0x0F
		elif b0 & 0xF8 == 0xF0:
			seq_len = 4
			codepoint = b0 & 0x07
		else:
			return false

		if i + seq_len > n:
			# Truncated multi-byte sequence at the edge of the sample window —
			# a cut boundary artifact, not evidence of invalid UTF-8.
			break

		for k in range(1, seq_len):
			var bk: int = bytes[i + k]
			if bk & 0xC0 != 0x80:
				return false
			codepoint = (codepoint << 6) | (bk & 0x3F)

		if seq_len == 2 and codepoint < 0x80:
			return false
		if seq_len == 3 and codepoint < 0x800:
			return false
		if seq_len == 4 and codepoint < 0x10000:
			return false
		if codepoint > 0x10FFFF:
			return false
		if codepoint >= 0xD800 and codepoint <= 0xDFFF:
			return false

		i += seq_len

	return true
