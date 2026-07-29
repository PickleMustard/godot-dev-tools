@tool
extends Control

const LfsExtensionRowScene = preload("res://addons/dev_tools/menus/lfs_extension_row.tscn")

var project_root: String = ""
var gitignore_path: String = ""
var gitattributes_path: String = ""
var _gitattributes_dirty: bool = false
var _lfs_scan_thread: Thread
var _lfs_scan_in_progress: bool = false

@onready var gitignore_edit: CodeEdit = %GitignoreEdit
@onready var save_gitignore_button: Button = %SaveGitignoreButton
@onready var gitattributes_edit: CodeEdit = %GitattributesEdit
@onready var save_gitattributes_button: Button = %SaveGitattributesButton
@onready var reload_gitattributes_button: Button = %ReloadGitattributesButton
@onready var rescan_button: Button = %RescanButton
@onready var lfs_checkbox_list: VBoxContainer = %LfsCheckboxList
@onready var lfs_scan_status_label: Label = %LfsScanStatusLabel
@onready var error_dialog: AcceptDialog = %ErrorDialog


func setup(root_path: String) -> void:
	project_root = root_path
	gitignore_path = root_path.path_join(".gitignore")
	gitattributes_path = root_path.path_join(".gitattributes")
	if is_inside_tree():
		_reload_all()


func _ready() -> void:
	save_gitignore_button.pressed.connect(_on_save_gitignore_pressed)
	save_gitattributes_button.pressed.connect(_on_save_gitattributes_pressed)
	reload_gitattributes_button.pressed.connect(_on_reload_gitattributes_pressed)
	rescan_button.pressed.connect(_on_rescan_pressed)
	gitattributes_edit.text_changed.connect(_on_gitattributes_text_changed)

	if not project_root.is_empty():
		_reload_all()


func _exit_tree() -> void:
	if _lfs_scan_thread and _lfs_scan_thread.is_started():
		_lfs_scan_thread.wait_to_finish()


func _reload_all() -> void:
	_load_gitignore()
	_load_gitattributes_edit()
	_refresh_lfs_checkboxes()


func _load_gitignore() -> void:
	gitignore_edit.text = _read_file_text(gitignore_path)


func _on_save_gitignore_pressed() -> void:
	_write_file_text(gitignore_path, gitignore_edit.text)


func _load_gitattributes_edit() -> void:
	gitattributes_edit.text = _read_file_text(gitattributes_path)
	_gitattributes_dirty = false


func _on_save_gitattributes_pressed() -> void:
	_write_file_text(gitattributes_path, gitattributes_edit.text)
	_gitattributes_dirty = false
	_refresh_lfs_checkboxes()


func _on_reload_gitattributes_pressed() -> void:
	_load_gitattributes_edit()
	_refresh_lfs_checkboxes()


func _on_gitattributes_text_changed() -> void:
	_gitattributes_dirty = true


func _refresh_lfs_checkboxes() -> void:
	if _lfs_scan_in_progress:
		return
	_lfs_scan_in_progress = true
	if _lfs_scan_thread and _lfs_scan_thread.is_started():
		_lfs_scan_thread.wait_to_finish()
	rescan_button.disabled = true
	lfs_scan_status_label.text = "Scanning..."
	lfs_scan_status_label.visible = true
	_lfs_scan_thread = Thread.new()
	_lfs_scan_thread.start(_refresh_lfs_checkboxes_worker, Thread.PRIORITY_LOW)


func _refresh_lfs_checkboxes_worker() -> void:
	var extensions := LfsScanner.scan_binary_extensions(project_root)
	var tracked_patterns := DevToolsGitAttributesUtil.get_lfs_patterns(gitattributes_path)
	call_deferred("_apply_lfs_checkboxes_data", extensions, tracked_patterns)


func _apply_lfs_checkboxes_data(extensions: PackedStringArray, tracked_patterns: PackedStringArray) -> void:
	if _lfs_scan_thread and _lfs_scan_thread.is_started():
		_lfs_scan_thread.wait_to_finish()
	_lfs_scan_in_progress = false
	rescan_button.disabled = false
	lfs_scan_status_label.visible = false

	for child in lfs_checkbox_list.get_children():
		child.queue_free()

	if extensions.is_empty():
		var empty_label := Label.new()
		empty_label.text = "(no binary file types found)"
		lfs_checkbox_list.add_child(empty_label)
		return

	for ext in extensions:
		var pattern := DevToolsGitAttributesUtil.extension_to_pattern(ext)
		var row: DevToolsLfsExtensionRow = LfsExtensionRowScene.instantiate()
		lfs_checkbox_list.add_child(row)
		row.load_entry(pattern, tracked_patterns.has(pattern))
		row.tracked_toggled.connect(_on_lfs_pattern_toggled)


func _on_lfs_pattern_toggled(pattern: String, tracked: bool) -> void:
	DevToolsGitAttributesUtil.set_pattern_tracked(gitattributes_path, pattern, tracked)
	if not _gitattributes_dirty:
		_load_gitattributes_edit()


func _on_rescan_pressed() -> void:
	_refresh_lfs_checkboxes()


func _read_file_text(path: String) -> String:
	if not FileAccess.file_exists(path):
		return ""
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return ""
	var text := file.get_as_text()
	file.close()
	return text


func _write_file_text(path: String, text: String) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		_show_error("Could not write '%s'." % path)
		return
	file.store_string(text)
	file.close()


func _show_error(message: String) -> void:
	error_dialog.title = "Dotfiles Error"
	error_dialog.dialog_text = message
	error_dialog.popup_centered()
