@tool
extends Control

signal back_requested

const GUTTER_WIDTH := 6.0

@onready var back_button: Button = %BackButton
@onready var title_label: Label = %TitleLabel
@onready var rows_container: VBoxContainer = %RowsContainer


func _ready() -> void:
	back_button.pressed.connect(func() -> void: back_requested.emit())


func load_file(path: String, hunks: Array) -> void:
	title_label.text = path
	for child in rows_container.get_children():
		child.queue_free()

	for row in DevToolsGitDiffFormat.build_side_by_side_rows(hunks):
		rows_container.add_child(_build_row(row))


func _build_row(row: Dictionary) -> Control:
	var kind: String = row.get("kind", "context")
	var old_text: String = row.get("old_text", "")
	var new_text: String = row.get("new_text", "")

	var hbox := HBoxContainer.new()

	var old_label := _make_cell()
	var gutter := ColorRect.new()
	gutter.custom_minimum_size = Vector2(GUTTER_WIDTH, 0)
	var new_label := _make_cell()

	match kind:
		"removed":
			old_label.text = "[color=red]-%s[/color]" % _escape(old_text)
			gutter.color = DevToolsGitDiffFormat.COLOR_DELETED
		"added":
			new_label.text = "[color=green]+%s[/color]" % _escape(new_text)
			gutter.color = DevToolsGitDiffFormat.COLOR_ADDED
		"hunk_header":
			old_label.text = "[color=cyan]%s[/color]" % _escape(old_text)
			new_label.visible = false
			gutter.color = Color(0, 0, 0, 0)
		_:
			old_label.text = " %s" % _escape(old_text)
			new_label.text = " %s" % _escape(new_text)
			gutter.color = Color(0, 0, 0, 0)

	hbox.add_child(old_label)
	hbox.add_child(gutter)
	hbox.add_child(new_label)
	return hbox


func _make_cell() -> RichTextLabel:
	var label := RichTextLabel.new()
	label.bbcode_enabled = true
	label.fit_content = true
	label.scroll_active = false
	label.autowrap_mode = 0
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_apply_monospace_font(label)
	return label


func _escape(text: String) -> String:
	return text.replace("[", "[lb]")


func _apply_monospace_font(rich_label: RichTextLabel) -> void:
	if not Engine.is_editor_hint():
		return
	var editor_theme := EditorInterface.get_editor_theme()
	if editor_theme and editor_theme.has_font("source", "EditorFonts"):
		var font := editor_theme.get_font("source", "EditorFonts")
		rich_label.add_theme_font_override("normal_font", font)
		rich_label.add_theme_font_override("bold_font", font)
		rich_label.add_theme_font_override("mono_font", font)
