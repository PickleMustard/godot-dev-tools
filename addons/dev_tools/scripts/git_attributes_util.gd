@tool
class_name GitAttributesUtil
extends RefCounted

const LFS_SUFFIX := "filter=lfs diff=lfs merge=lfs -text"


static func get_lfs_patterns(gitattributes_path: String) -> PackedStringArray:
	var result := PackedStringArray()
	var lines := _read_lines(gitattributes_path)
	for line in lines:
		var stripped := line.strip_edges()
		if stripped.is_empty() or stripped.begins_with("#"):
			continue
		if not stripped.contains("filter=lfs"):
			continue
		var tokens := stripped.split(" ", false)
		if tokens.size() > 0:
			result.append(tokens[0])
	return result


static func is_pattern_tracked(gitattributes_path: String, pattern: String) -> bool:
	return get_lfs_patterns(gitattributes_path).has(pattern)


static func set_pattern_tracked(gitattributes_path: String, pattern: String, tracked: bool) -> void:
	var lines := _read_lines(gitattributes_path)
	var new_lines: Array = []
	var found := false

	for line in lines:
		var stripped: String = line.strip_edges()
		if stripped.is_empty():
			continue
		var tokens := stripped.split(" ", false)
		var is_match: bool = tokens.size() > 0 and tokens[0] == pattern and stripped.contains("filter=lfs")
		if is_match:
			found = true
			if tracked:
				new_lines.append(line)
		else:
			new_lines.append(line)

	if tracked and not found:
		new_lines.append("%s %s" % [pattern, LFS_SUFFIX])

	var out_text := "\n".join(new_lines)
	if not out_text.is_empty():
		out_text += "\n"

	var file := FileAccess.open(gitattributes_path, FileAccess.WRITE)
	if file == null:
		return
	file.store_string(out_text)
	file.close()


static func extension_to_pattern(extension: String) -> String:
	return "*." + extension.trim_prefix(".").to_lower()


static func pattern_to_extension(pattern: String) -> String:
	return pattern.trim_prefix("*.").to_lower()


static func _read_lines(path: String) -> PackedStringArray:
	if not FileAccess.file_exists(path):
		return PackedStringArray()
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return PackedStringArray()
	var text := file.get_as_text()
	file.close()
	return text.split("\n")
