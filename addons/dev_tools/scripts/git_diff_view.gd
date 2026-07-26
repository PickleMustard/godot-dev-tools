@tool
extends ScrollContainer

@onready var label: RichTextLabel = %DiffLabel


func _ready() -> void:
	label.bbcode_enabled = true
	_apply_monospace_font(label)


func load_diff(files: Array) -> void:
	label.text = GitDiffFormat.format_files(files)


func _apply_monospace_font(rich_label: RichTextLabel) -> void:
	if not Engine.is_editor_hint():
		return
	var editor_theme := EditorInterface.get_editor_theme()
	if editor_theme and editor_theme.has_font("source", "EditorFonts"):
		var font := editor_theme.get_font("source", "EditorFonts")
		rich_label.add_theme_font_override("normal_font", font)
		rich_label.add_theme_font_override("bold_font", font)
		rich_label.add_theme_font_override("mono_font", font)
