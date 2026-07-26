@tool
class_name GitDiffFormat
extends RefCounted


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

		for hunk in f.get("hunks", []):
			var h: Dictionary = hunk
			out.append("[color=cyan]%s[/color]" % _escape(String(h.get("header", ""))))

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
		out.append("")

	return "\n".join(out)


static func _escape(text: String) -> String:
	return text.replace("[", "[lb]")
