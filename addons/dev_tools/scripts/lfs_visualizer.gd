@tool
extends Control

const POLL_INTERVAL_SEC: float = 2.0
const GitStatusRowScene = preload("res://addons/dev_tools/menus/git_status_row.tscn")

var git_backend: GitBackend
var project_root: String = ""
var repo_open: bool = false
var gitattributes_path: String = ""
var _poll_timer: Timer
var _last_signature: Dictionary = {}
var _initial_refresh_pending: bool = false
var _changes_thread: Thread
var _changes_in_progress: bool = false
var _browse_thread: Thread
var _browse_in_progress: bool = false
var _poll_thread: Thread
var _poll_check_in_progress: bool = false
var _cached_status: Dictionary = {}
var _has_cached_status: bool = false

@onready var staged_list: VBoxContainer = %LfsStagedList
@onready var unstaged_list: VBoxContainer = %LfsUnstagedList
@onready var browse_list: VBoxContainer = %LfsBrowseList
@onready var selected_file_label: Label = %SelectedFileLabel
@onready var rescan_button: Button = %RescanButton
@onready var lfs_scan_status_label: Label = %LfsScanStatusLabel
@onready var error_dialog: AcceptDialog = %ErrorDialog


func setup(backend: GitBackend, is_open: bool, root_path: String) -> void:
	git_backend = backend
	repo_open = is_open
	project_root = root_path
	gitattributes_path = root_path.path_join(".gitattributes")
	if is_inside_tree():
		_request_initial_refresh_when_visible()


func _ready() -> void:
	rescan_button.pressed.connect(_on_rescan_pressed)

	_poll_timer = Timer.new()
	_poll_timer.wait_time = POLL_INTERVAL_SEC
	_poll_timer.autostart = true
	_poll_timer.one_shot = false
	add_child(_poll_timer)
	_poll_timer.timeout.connect(_on_poll_timeout)

	if git_backend:
		_request_initial_refresh_when_visible()


func set_polling_active(active: bool) -> void:
	if _poll_timer:
		_poll_timer.paused = not active


func _exit_tree() -> void:
	if _changes_thread and _changes_thread.is_started():
		_changes_thread.wait_to_finish()
	if _browse_thread and _browse_thread.is_started():
		_browse_thread.wait_to_finish()
	if _poll_thread and _poll_thread.is_started():
		_poll_thread.wait_to_finish()


func _request_initial_refresh_when_visible() -> void:
	if _initial_refresh_pending:
		return
	if is_visible_in_tree():
		_schedule_initial_refresh()
	elif not visibility_changed.is_connected(_on_became_visible_for_initial_refresh):
		visibility_changed.connect(_on_became_visible_for_initial_refresh)


func _on_became_visible_for_initial_refresh() -> void:
	if not is_visible_in_tree():
		return
	if visibility_changed.is_connected(_on_became_visible_for_initial_refresh):
		visibility_changed.disconnect(_on_became_visible_for_initial_refresh)
	_schedule_initial_refresh()


func _schedule_initial_refresh() -> void:
	if _initial_refresh_pending:
		return
	_initial_refresh_pending = true
	get_tree().create_timer(0.3).timeout.connect(_refresh_all)


func _on_git_status_ready(status: Dictionary) -> void:
	_cached_status = status
	_has_cached_status = true


func _refresh_all() -> void:
	if _has_cached_status:
		var patterns := GitAttributesUtil.get_lfs_patterns(gitattributes_path)
		_apply_changes_data(_cached_status, patterns)
	else:
		_refresh_changes()
	_refresh_browse()


func _refresh_changes() -> void:
	if not repo_open or not git_backend:
		_populate_status_list(staged_list, [], GitStatusRow.ActionMode.UNSTAGE)
		_populate_status_list(unstaged_list, [], GitStatusRow.ActionMode.STAGE)
		return
	if _changes_in_progress:
		return
	_changes_in_progress = true
	if _changes_thread and _changes_thread.is_started():
		_changes_thread.wait_to_finish()
	_changes_thread = Thread.new()
	_changes_thread.start(_refresh_changes_worker)


func _refresh_changes_worker() -> void:
	var patterns := GitAttributesUtil.get_lfs_patterns(gitattributes_path)
	var status: Dictionary = git_backend.get_status()
	call_deferred("_apply_changes_data", status, patterns)


func _apply_changes_data(status: Dictionary, patterns: PackedStringArray) -> void:
	if _changes_thread and _changes_thread.is_started():
		_changes_thread.wait_to_finish()
	_changes_in_progress = false

	var staged := _filter_by_patterns(status.get("staged", []), patterns)
	_populate_status_list(staged_list, staged, GitStatusRow.ActionMode.UNSTAGE)

	var unstaged_combined: Array = []
	unstaged_combined.append_array(status.get("unstaged", []))
	unstaged_combined.append_array(status.get("untracked", []))
	var unstaged := _filter_by_patterns(unstaged_combined, patterns)
	_populate_status_list(unstaged_list, unstaged, GitStatusRow.ActionMode.STAGE)

	_last_signature = {"status": status, "patterns": patterns}


func _filter_by_patterns(entries: Array, patterns: PackedStringArray) -> Array:
	var result: Array = []
	for entry in entries:
		var e: Dictionary = entry
		var path: String = e.get("path", "")
		var pattern := GitAttributesUtil.extension_to_pattern(path.get_extension())
		if patterns.has(pattern):
			result.append(e)
	return result


func _populate_status_list(list: VBoxContainer, entries: Array, mode: int) -> void:
	for child in list.get_children():
		child.queue_free()

	if entries.is_empty():
		var empty_label := Label.new()
		empty_label.text = "(none)"
		list.add_child(empty_label)
		return

	for entry in entries:
		var e: Dictionary = entry
		var row: GitStatusRow = GitStatusRowScene.instantiate()
		list.add_child(row)
		row.load_entry(e.get("path", ""), e.get("status", ""), mode)
		row.file_selected.connect(_on_row_selected)
		row.stage_requested.connect(_on_stage_requested)
		row.unstage_requested.connect(_on_unstage_requested)


func _refresh_browse() -> void:
	if project_root.is_empty():
		for child in browse_list.get_children():
			child.queue_free()
		return
	if _browse_in_progress:
		return
	_browse_in_progress = true
	if _browse_thread and _browse_thread.is_started():
		_browse_thread.wait_to_finish()
	rescan_button.disabled = true
	lfs_scan_status_label.text = "Scanning..."
	lfs_scan_status_label.visible = true
	_browse_thread = Thread.new()
	_browse_thread.start(_refresh_browse_worker, Thread.PRIORITY_LOW)


func _refresh_browse_worker() -> void:
	var patterns := GitAttributesUtil.get_lfs_patterns(gitattributes_path)
	var files := LfsScanner.scan_files_matching_patterns(project_root, patterns)
	call_deferred("_apply_browse_data", files)


func _apply_browse_data(files: PackedStringArray) -> void:
	if _browse_thread and _browse_thread.is_started():
		_browse_thread.wait_to_finish()
	_browse_in_progress = false
	rescan_button.disabled = false
	lfs_scan_status_label.visible = false

	for child in browse_list.get_children():
		child.queue_free()

	if files.is_empty():
		var empty_label := Label.new()
		empty_label.text = "(no LFS-tracked files found)"
		browse_list.add_child(empty_label)
		return

	for path in files:
		var row: GitStatusRow = GitStatusRowScene.instantiate()
		browse_list.add_child(row)
		row.load_entry(path, "", GitStatusRow.ActionMode.STAGE)
		row.action_button.visible = false
		row.file_selected.connect(_on_row_selected)


func _on_poll_timeout() -> void:
	if not repo_open or not is_visible_in_tree() or not git_backend:
		return
	if _changes_in_progress or _poll_check_in_progress:
		return
	_poll_check_in_progress = true
	if _poll_thread and _poll_thread.is_started():
		_poll_thread.wait_to_finish()
	_poll_thread = Thread.new()
	_poll_thread.start(_poll_signature_worker)


func _poll_signature_worker() -> void:
	var patterns := GitAttributesUtil.get_lfs_patterns(gitattributes_path)
	var status: Dictionary = git_backend.get_status()
	var sig := {"status": status, "patterns": patterns}
	call_deferred("_apply_poll_signature", sig)


func _apply_poll_signature(sig: Dictionary) -> void:
	if _poll_thread and _poll_thread.is_started():
		_poll_thread.wait_to_finish()
	_poll_check_in_progress = false
	if sig != _last_signature:
		_refresh_changes()


func _on_rescan_pressed() -> void:
	_refresh_browse()


func _on_row_selected(relative_path: String) -> void:
	var res_path := "res://" + relative_path.trim_prefix("/")
	if not ResourceLoader.exists(res_path):
		_show_error("Could not find resource '%s'." % res_path)
		return
	var resource := load(res_path)
	if resource == null:
		_show_error("Could not load '%s'." % res_path)
		return
	selected_file_label.text = relative_path
	EditorInterface.edit_resource(resource)


func _on_stage_requested(path: String) -> void:
	if not git_backend:
		return
	if git_backend.stage_file(path):
		_refresh_changes()
	else:
		_show_error("Could not stage '%s'." % path)


func _on_unstage_requested(path: String) -> void:
	if not git_backend:
		return
	if git_backend.unstage_file(path):
		_refresh_changes()
	else:
		_show_error("Could not unstage '%s'." % path)


func _show_error(message: String) -> void:
	error_dialog.title = "LFS Error"
	error_dialog.dialog_text = message
	error_dialog.popup_centered()
