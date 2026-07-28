@tool
class_name GitStatusRow
extends PanelContainer

enum ActionMode { STAGE, UNSTAGE }

signal file_selected(path: String)
signal stage_requested(path: String)
signal unstage_requested(path: String)

@onready var row_status_icon: TextureRect = %RowStatusIcon
@onready var path_label: Label = %PathLabel
@onready var action_button: Button = %ActionButton

var file_path: String = ""
var action_mode: int = ActionMode.STAGE


func _ready() -> void:
	focus_entered.connect(func() -> void: file_selected.emit(file_path))
	action_button.pressed.connect(_on_action_pressed)


func load_entry(path: String, status: String, mode: int) -> void:
	file_path = path
	action_mode = mode
	path_label.text = "[%s] %s" % [status, path] if not status.is_empty() else path
	action_button.text = "+" if mode == ActionMode.STAGE else "-"
	row_status_icon.visible = false


## Marks this row as a quarantined (bad/incomplete LFS download) file.
func mark_quarantined() -> void:
	row_status_icon.texture = get_theme_icon("StatusError", "EditorIcons")
	row_status_icon.tooltip_text = "Quarantined: bad or incomplete LFS download. Content withheld from Godot's importer."
	row_status_icon.visible = true


func _on_action_pressed() -> void:
	if action_mode == ActionMode.STAGE:
		stage_requested.emit(file_path)
	else:
		unstage_requested.emit(file_path)
