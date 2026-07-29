@tool
class_name DevToolsLfsExtensionRow
extends PanelContainer

signal tracked_toggled(pattern: String, tracked: bool)

@onready var status_icon: TextureRect = %StatusIcon
@onready var extension_label: Label = %ExtensionLabel
@onready var tracked_check_box: CheckBox = %TrackedCheckBox

var pattern: String = ""


func _ready() -> void:
	tracked_check_box.toggled.connect(_on_toggled)


func load_entry(entry_pattern: String, tracked: bool) -> void:
	pattern = entry_pattern
	extension_label.text = entry_pattern
	tracked_check_box.set_pressed_no_signal(tracked)


## summary: one bucket from LfsStatusScanner.aggregate_by_pattern(), or {} to clear.
func set_status_summary(summary: Dictionary) -> void:
	var icon_name := ""
	var tooltip := ""

	if summary.get("quarantined", 0) > 0:
		icon_name = "StatusError"
		tooltip = "%d file(s) quarantined (bad/incomplete LFS download)." % summary["quarantined"]
	elif summary.get("remote_missing", 0) > 0:
		icon_name = "StatusError"
		tooltip = "%d file(s) tracked but missing from the remote LFS store -- needs push." % summary["remote_missing"]
	elif summary.get("pending_to_lfs", 0) > 0:
		icon_name = "StatusWarning"
		tooltip = "%d file(s) selected for LFS tracking but not yet migrated -- use Rebuild LFS Tracking." % summary["pending_to_lfs"]
	elif summary.get("pending_from_lfs", 0) > 0:
		icon_name = "StatusWarning"
		tooltip = "%d file(s) untracked from LFS but still stored as pointers -- use Rebuild LFS Tracking." % summary["pending_from_lfs"]

	if icon_name.is_empty():
		status_icon.visible = false
		return

	status_icon.texture = get_theme_icon(icon_name, "EditorIcons")
	status_icon.tooltip_text = tooltip
	status_icon.visible = true


func _on_toggled(toggled_on: bool) -> void:
	tracked_toggled.emit(pattern, toggled_on)
