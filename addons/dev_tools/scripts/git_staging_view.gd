@tool
extends Control

@onready var staged_label: RichTextLabel = %StagedDiffLabel
@onready var unstaged_label: RichTextLabel = %UnstagedDiffLabel


func _ready() -> void:
	staged_label.bbcode_enabled = true
	unstaged_label.bbcode_enabled = true
	_apply_monospace_font(staged_label)
	_apply_monospace_font(unstaged_label)


func load_diffs(staged_files: Array, unstaged_files: Array) -> void:
	staged_label.text = GitDiffFormat.format_files(staged_files)
	unstaged_label.text = GitDiffFormat.format_files(unstaged_files)


func _apply_monospace_font(rich_label: RichTextLabel) -> void:
	if not Engine.is_editor_hint():
		return
	var editor_theme := EditorInterface.get_editor_theme()
	if editor_theme and editor_theme.has_font("source", "EditorFonts"):
		var font := editor_theme.get_font("source", "EditorFonts")
		rich_label.add_theme_font_override("normal_font", font)
		rich_label.add_theme_font_override("bold_font", font)
		rich_label.add_theme_font_override("mono_font", font)
