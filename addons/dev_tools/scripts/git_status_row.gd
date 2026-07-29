@tool
class_name GitStatusRow
extends PanelContainer

enum ActionMode { STAGE, UNSTAGE }

signal file_selected(path: String)
signal stage_requested(path: String)
signal unstage_requested(path: String)
signal lock_requested(path: String)
signal unlock_requested(path: String)

@onready var row_status_icon: TextureRect = %RowStatusIcon
@onready var path_label: Label = %PathLabel
@onready var action_button: Button = %ActionButton
@onready var lock_button: Button = %LockButton

var file_path: String = ""
var action_mode: int = ActionMode.STAGE
var _is_locked_by_me: bool = false


func _ready() -> void:
	focus_entered.connect(func() -> void: file_selected.emit(file_path))
	action_button.pressed.connect(_on_action_pressed)
	lock_button.pressed.connect(_on_lock_button_pressed)


func load_entry(path: String, status: String, mode: int) -> void:
	file_path = path
	action_mode = mode
	path_label.text = "[%s] %s" % [status, path] if not status.is_empty() else path
	action_button.text = "+" if mode == ActionMode.STAGE else "-"
	row_status_icon.visible = false
	lock_button.visible = false
	_is_locked_by_me = false


## Marks this row as a quarantined (bad/incomplete LFS download) file.
func mark_quarantined() -> void:
	row_status_icon.texture = get_theme_icon("StatusError", "EditorIcons")
	row_status_icon.tooltip_text = "Quarantined: bad or incomplete LFS download. Content withheld from Godot's importer."
	row_status_icon.visible = true


## Marks this row as locked by another developer -- grays the stage/unstage
## action out and shows a lock badge. No unlock affordance is offered here;
## only the owner can unlock (see configure_lock_button()).
func mark_locked(owner_name: String) -> void:
	row_status_icon.texture = get_theme_icon("Lock", "EditorIcons")
	row_status_icon.tooltip_text = "Locked by %s" % owner_name
	row_status_icon.visible = true
	action_button.disabled = true
	lock_button.visible = false


## Shows the Lock/Unlock affordance for a lockable file not locked by
## someone else. is_confirmed_mine only matters when is_locked_by_me is
## true -- it distinguishes a lock this session created (confirmed) from
## one inferred from a name match against a previous session (probable),
## which is a soft heuristic, not a security boundary.
func configure_lock_button(is_locked_by_me: bool, is_confirmed_mine: bool = true) -> void:
	_is_locked_by_me = is_locked_by_me
	lock_button.visible = true
	if is_locked_by_me:
		lock_button.text = "Unlock"
		lock_button.tooltip_text = "Locked by you (previous session) -- click to unlock." if not is_confirmed_mine else ""
	else:
		lock_button.text = "Lock"
		lock_button.tooltip_text = ""


func _on_action_pressed() -> void:
	if action_mode == ActionMode.STAGE:
		stage_requested.emit(file_path)
	else:
		unstage_requested.emit(file_path)


func _on_lock_button_pressed() -> void:
	if _is_locked_by_me:
		unlock_requested.emit(file_path)
	else:
		lock_requested.emit(file_path)
