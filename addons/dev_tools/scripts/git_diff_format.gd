@tool
class_name GitDiffFormat
extends RefCounted

const COLOR_ADDED := Color(0.55, 0.84, 0.44)
const COLOR_DELETED := Color(0.86, 0.36, 0.36)
const COLOR_MODIFIED := Color(0.94, 0.85, 0.36)
const COLOR_RENAMED := Color(0.36, 0.68, 0.94)
const COLOR_DEFAULT := Color(0.7, 0.7, 0.7)


static func status_color(status: String) -> Color:
	match status:
		"added", "untracked":
			return COLOR_ADDED
		"deleted":
			return COLOR_DELETED
		"renamed", "copied":
			return COLOR_RENAMED
		"modified", "typechange":
			return COLOR_MODIFIED
		_:
			return COLOR_DEFAULT


## Flattens one file's hunks into rows for a two-column old/new view.
## No line-pairing/alignment: rows follow hunk order as libgit2 emits it
## (removed lines, then added lines, grouped per change block).
static func build_side_by_side_rows(hunks: Array) -> Array:
	var rows: Array = []
	for hunk in hunks:
		var h: Dictionary = hunk
		var header: String = String(h.get("header", "")).rstrip("\n")
		rows.append({"old_text": header, "new_text": header, "kind": "hunk_header"})

		for line in h.get("lines", []):
			var l: Dictionary = line
			var origin: String = l.get("origin", " ")
			var content: String = String(l.get("content", "")).rstrip("\n")
			if origin == "+":
				rows.append({"old_text": "", "new_text": content, "kind": "added"})
			elif origin == "-":
				rows.append({"old_text": content, "new_text": "", "kind": "removed"})
			else:
				rows.append({"old_text": content, "new_text": content, "kind": "context"})
	return rows


static func format_files(files: Array) -> String:
	if files.is_empty():
		return "[i](no changes)[/i]"

	var out: PackedStringArray = []
	for file in files:
		var f: Dictionary = file
		var path: String = f.get("new_path", "")
		if path.is_empty():
			path = f.get("old_path", "")
		out.append("[b]%s[/b] (%s)" % [_escape(path), f.get("status", "")])
		out.append(format_hunks(f.get("hunks", [])))
		out.append("")

	return "\n".join(out)


## BBCode for one file's hunks only (no path/status header line).
static func format_hunks(hunks: Array) -> String:
	if hunks.is_empty():
		return "[i](no changes)[/i]"

	var out: PackedStringArray = []
	for hunk in hunks:
		var h: Dictionary = hunk
		out.append("[color=cyan]%s[/color]" % _escape(String(h.get("header", "")).rstrip("\n")))

		for line in h.get("lines", []):
			var l: Dictionary = line
			var origin: String = l.get("origin", " ")
			var content: String = _escape(String(l.get("content", "")).rstrip("\n"))
			if origin == "+":
				out.append("[color=green]+%s[/color]" % content)
			elif origin == "-":
				out.append("[color=red]-%s[/color]" % content)
			else:
				out.append(" %s" % content)

	return "\n".join(out)


static func _escape(text: String) -> String:
	return text.replace("[", "[lb]")
