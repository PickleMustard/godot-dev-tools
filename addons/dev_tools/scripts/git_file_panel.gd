@tool
class_name GitFilePanel
extends VBoxContainer

enum ActionMode { NONE, STAGE, UNSTAGE }

signal file_selected(path: String)
signal stage_requested(path: String)
signal unstage_requested(path: String)

@onready var header_panel: PanelContainer = %HeaderPanel
@onready var status_chip: ColorRect = %StatusChip
@onready var path_label: Label = %PathLabel
@onready var status_label: Label = %StatusLabel
@onready var action_button: Button = %ActionButton
@onready var body_label: RichTextLabel = %BodyLabel

var file_path: String = ""
var file_hunks: Array = []
var action_mode: int = ActionMode.NONE


func _ready() -> void:
	header_panel.focus_entered.connect(_on_header_focused)
	action_button.pressed.connect(_on_action_pressed)
	body_label.bbcode_enabled = true
	_apply_monospace_font(body_label)


func load_file(file: Dictionary, mode: int) -> void:
	file_path = String(file.get("new_path", ""))
	if file_path.is_empty():
		file_path = String(file.get("old_path", ""))
	file_hunks = file.get("hunks", [])
	var status: String = file.get("status", "")

	path_label.text = file_path
	status_label.text = "(%s)" % status
	status_chip.color = GitDiffFormat.status_color(status)
	body_label.text = GitDiffFormat.format_hunks(file_hunks)

	action_mode = mode
	action_button.visible = mode != ActionMode.NONE
	if mode == ActionMode.STAGE:
		action_button.text = "+"
	elif mode == ActionMode.UNSTAGE:
		action_button.text = "-"


func _on_header_focused() -> void:
	file_selected.emit(file_path)


func _on_action_pressed() -> void:
	if action_mode == ActionMode.STAGE:
		stage_requested.emit(file_path)
	elif action_mode == ActionMode.UNSTAGE:
		unstage_requested.emit(file_path)


func _apply_monospace_font(rich_label: RichTextLabel) -> void:
	if not Engine.is_editor_hint():
		return
	var editor_theme := EditorInterface.get_editor_theme()
	if editor_theme and editor_theme.has_font("source", "EditorFonts"):
		var font := editor_theme.get_font("source", "EditorFonts")
		rich_label.add_theme_font_override("normal_font", font)
		rich_label.add_theme_font_override("bold_font", font)
		rich_label.add_theme_font_override("mono_font", font)
