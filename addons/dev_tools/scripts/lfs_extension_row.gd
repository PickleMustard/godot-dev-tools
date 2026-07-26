@tool
class_name LfsExtensionRow
extends PanelContainer

signal tracked_toggled(pattern: String, tracked: bool)

@onready var extension_label: Label = %ExtensionLabel
@onready var tracked_check_box: CheckBox = %TrackedCheckBox

var pattern: String = ""


func _ready() -> void:
	tracked_check_box.toggled.connect(_on_toggled)


func load_entry(entry_pattern: String, tracked: bool) -> void:
	pattern = entry_pattern
	extension_label.text = entry_pattern
	tracked_check_box.set_pressed_no_signal(tracked)


func _on_toggled(toggled_on: bool) -> void:
	tracked_toggled.emit(pattern, toggled_on)
