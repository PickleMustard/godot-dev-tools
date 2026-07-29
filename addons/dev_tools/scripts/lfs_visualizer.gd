@tool
extends Control

const POLL_INTERVAL_SEC: float = 2.0
const LOCK_POLL_INTERVAL_SEC: float = 60.0
const GitStatusRowScene = preload("res://addons/dev_tools/menus/git_status_row.tscn")
const LfsExtensionRowScene = preload("res://addons/dev_tools/menus/lfs_extension_row.tscn")

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

var _status_thread: Thread
var _status_in_progress: bool = false
var _last_local_statuses: Array = []
var _last_tracked_patterns: PackedStringArray = []
var _remote_check_in_progress: bool = false

var _rebuild_service: LfsRebuildService
var _pending_rebuild_scan: Dictionary = {}
var _rebuild_scan_thread: Thread
var _rebuild_scan_in_progress: bool = false

var _push_service: LfsPushService

var _pre_pull_snapshot: Dictionary = {}
var _active_pull_guard: LfsPullGuard

var _lock_poll_timer: Timer
var _lock_poll_in_progress: bool = false
var _pending_retry_queue: Array = []

@onready var active_edits_list: VBoxContainer = %LfsActiveEditsList
@onready var staged_list: VBoxContainer = %LfsStagedList
@onready var unstaged_list: VBoxContainer = %LfsUnstagedList
@onready var browse_list: VBoxContainer = %LfsBrowseList
@onready var selected_file_label: Label = %SelectedFileLabel
@onready var rescan_button: Button = %RescanButton
@onready var lfs_scan_status_label: Label = %LfsScanStatusLabel
@onready var error_dialog: AcceptDialog = %ErrorDialog
@onready var extension_status_list: VBoxContainer = %ExtensionStatusList
@onready var quarantined_list: VBoxContainer = %QuarantinedList
@onready var rebuild_button: Button = %RebuildLfsButton
@onready var rebuild_confirm_dialog: ConfirmationDialog = %RebuildConfirmDialog
@onready var push_lfs_button: Button = %PushLfsButton
@onready var remote_client: LfsRemoteClient = %LfsRemoteClient


func setup(backend: GitBackend, is_open: bool, root_path: String) -> void:
	git_backend = backend
	repo_open = is_open
	project_root = root_path
	gitattributes_path = root_path.path_join(".gitattributes")
	if is_inside_tree():
		_request_initial_refresh_when_visible()


func _ready() -> void:
	rescan_button.pressed.connect(_on_rescan_pressed)
	rebuild_button.pressed.connect(_on_rebuild_pressed)
	rebuild_confirm_dialog.confirmed.connect(_on_rebuild_confirmed)
	push_lfs_button.pressed.connect(_on_push_lfs_pressed)

	_poll_timer = Timer.new()
	_poll_timer.wait_time = POLL_INTERVAL_SEC
	_poll_timer.autostart = true
	_poll_timer.one_shot = false
	add_child(_poll_timer)
	_poll_timer.timeout.connect(_on_poll_timeout)

	_lock_poll_timer = Timer.new()
	_lock_poll_timer.wait_time = _get_lock_poll_interval()
	_lock_poll_timer.autostart = true
	_lock_poll_timer.one_shot = false
	add_child(_lock_poll_timer)
	_lock_poll_timer.timeout.connect(_on_lock_poll_timeout)

	if git_backend:
		_request_initial_refresh_when_visible()


func set_polling_active(active: bool) -> void:
	if _poll_timer:
		_poll_timer.paused = not active
	if _lock_poll_timer:
		_lock_poll_timer.paused = not active


func _exit_tree() -> void:
	if _changes_thread and _changes_thread.is_started():
		_changes_thread.wait_to_finish()
	if _browse_thread and _browse_thread.is_started():
		_browse_thread.wait_to_finish()
	if _poll_thread and _poll_thread.is_started():
		_poll_thread.wait_to_finish()
	if _status_thread and _status_thread.is_started():
		_status_thread.wait_to_finish()
	if _rebuild_scan_thread and _rebuild_scan_thread.is_started():
		_rebuild_scan_thread.wait_to_finish()


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
	_refresh_status()
	_refresh_active_edits()
	_on_lock_poll_timeout()


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
	unstaged = _filter_expected_lfs_divergence(unstaged)
	unstaged = _filter_locked_by_me(unstaged)
	_populate_status_list(unstaged_list, unstaged, GitStatusRow.ActionMode.STAGE)

	_last_signature = {"status": status, "patterns": patterns}
	_refresh_active_edits()


func _filter_locked_by_me(entries: Array) -> Array:
	var lock_mgr := _lock_manager()
	if lock_mgr == null:
		return entries
	var result: Array = []
	for entry in entries:
		var e: Dictionary = entry
		if lock_mgr.is_locked_by_me(e.get("path", "")):
			continue
		result.append(e)
	return result


func _filter_by_patterns(entries: Array, patterns: PackedStringArray) -> Array:
	var result: Array = []
	for entry in entries:
		var e: Dictionary = entry
		var path: String = e.get("path", "")
		var pattern := GitAttributesUtil.extension_to_pattern(path.get_extension())
		if patterns.has(pattern):
			result.append(e)
	return result


func _filter_expected_lfs_divergence(entries: Array) -> Array:
	var manifest := LfsManifest.load_manifest(project_root)
	if manifest.is_empty():
		return entries
	var patterns := GitAttributesUtil.get_lfs_patterns(gitattributes_path)
	var result: Array = []
	for entry in entries:
		var e: Dictionary = entry
		var path: String = e.get("path", "")
		if LfsStatusScanner.is_expected_lfs_divergence(project_root, path, patterns, manifest):
			continue
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
		var path: String = e.get("path", "")
		row.load_entry(path, e.get("status", ""), mode)
		row.file_selected.connect(_on_row_selected)
		row.stage_requested.connect(_on_stage_requested)
		row.unstage_requested.connect(_on_unstage_requested)
		_apply_lock_badge_to_row(row, path)


## Configures a row's lock badge/affordance from LfsLockManager's current
## state: grayed lock icon (no unlock affordance) if locked by someone
## else, a Lock/Unlock button otherwise.
func _apply_lock_badge_to_row(row: GitStatusRow, path: String) -> void:
	var lock_mgr := _lock_manager()
	if lock_mgr == null:
		return
	row.lock_requested.connect(_on_lock_requested)
	row.unlock_requested.connect(_on_unlock_requested)
	if lock_mgr.is_locked_by_other(path):
		row.mark_locked(lock_mgr.get_lock_owner_name(path))
	elif lock_mgr.is_locked_by_me(path):
		row.configure_lock_button(true, lock_mgr.is_lock_confirmed_mine(path))
	else:
		row.configure_lock_button(false)


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
		_apply_lock_badge_to_row(row, path)


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
	_refresh_status(true)


func _on_row_selected(relative_path: String) -> void:
	# This path calls EditorInterface.edit_resource() directly rather than
	# going through EditorNode::load_scene_or_resource/FileSystemDock, so it
	# bypasses every engine-side lock veto -- must check here too.
	var lock_mgr := _lock_manager()
	if lock_mgr and lock_mgr.is_locked_by_other(relative_path):
		_show_error("'%s' is locked by %s and cannot be opened for editing." % [relative_path, lock_mgr.get_lock_owner_name(relative_path)])
		return

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


func _show_info(message: String) -> void:
	error_dialog.title = "LFS Info"
	error_dialog.dialog_text = message
	error_dialog.popup_centered()


func _get_remote_url() -> String:
	if not git_backend:
		return ""
	for r in git_backend.list_remotes():
		var d: Dictionary = r
		if d.get("name", "") == "origin":
			return d.get("url", "")
	return ""


# --- Per-pattern status ("not yet tracked" / "tracked but not on remote") ---


func _refresh_status(check_remote: bool = false) -> void:
	if project_root.is_empty():
		return
	if _status_in_progress:
		return
	_status_in_progress = true
	if _status_thread and _status_thread.is_started():
		_status_thread.wait_to_finish()
	_status_thread = Thread.new()
	_status_thread.start(_refresh_status_worker.bind(check_remote), Thread.PRIORITY_LOW)


func _refresh_status_worker(check_remote: bool) -> void:
	var patterns := GitAttributesUtil.get_lfs_patterns(gitattributes_path)
	var manifest := LfsManifest.load_manifest(project_root)
	var statuses := LfsStatusScanner.scan(project_root, patterns, manifest)
	call_deferred("_apply_status_data", patterns, statuses, check_remote)


func _apply_status_data(patterns: PackedStringArray, statuses: Array, check_remote: bool) -> void:
	if _status_thread and _status_thread.is_started():
		_status_thread.wait_to_finish()
	_status_in_progress = false

	_last_tracked_patterns = patterns
	_last_local_statuses = statuses
	_populate_extension_status_list(patterns, LfsStatusScanner.aggregate_by_pattern(statuses))
	_populate_quarantined_list(_extract_quarantined_paths(statuses))

	if check_remote:
		_start_remote_check()


func _extract_quarantined_paths(statuses: Array) -> Array:
	var result: Array = []
	for entry in statuses:
		var e: Dictionary = entry
		if int(e.get("status", -1)) == LfsStatusScanner.Status.QUARANTINED:
			result.append(e.get("path", ""))
	return result


func _populate_extension_status_list(patterns: PackedStringArray, aggregate: Dictionary) -> void:
	for child in extension_status_list.get_children():
		child.queue_free()

	var all_patterns: Dictionary = {}
	for p in patterns:
		all_patterns[p] = true
	for p in aggregate.keys():
		all_patterns[p] = true

	if all_patterns.is_empty():
		var empty_label := Label.new()
		empty_label.text = "(no binary file types tracked yet)"
		extension_status_list.add_child(empty_label)
		return

	var sorted_patterns := all_patterns.keys()
	sorted_patterns.sort()

	for pattern in sorted_patterns:
		var row: LfsExtensionRow = LfsExtensionRowScene.instantiate()
		extension_status_list.add_child(row)
		row.load_entry(pattern, patterns.has(pattern))
		row.set_status_summary(aggregate.get(pattern, {}))
		row.tracked_toggled.connect(_on_extension_pattern_toggled)


func _on_extension_pattern_toggled(pattern: String, tracked: bool) -> void:
	GitAttributesUtil.set_pattern_tracked(gitattributes_path, pattern, tracked)
	_refresh_status()


func _populate_quarantined_list(quarantined_paths: Array) -> void:
	for child in quarantined_list.get_children():
		child.queue_free()

	if quarantined_paths.is_empty():
		var empty_label := Label.new()
		empty_label.text = "(no quarantined files)"
		quarantined_list.add_child(empty_label)
		return

	for path in quarantined_paths:
		var row: GitStatusRow = GitStatusRowScene.instantiate()
		quarantined_list.add_child(row)
		row.load_entry(path, "quarantined", GitStatusRow.ActionMode.STAGE)
		row.mark_quarantined()
		row.action_button.text = "Retry"
		row.stage_requested.connect(_on_quarantine_retry_requested)
		row.file_selected.connect(_on_row_selected)


func _on_quarantine_retry_requested(relative_path: String) -> void:
	if _active_pull_guard != null:
		_show_error("A repair is already in progress.")
		return
	var remote_url := _get_remote_url()
	remote_client.setup(LfsCredentialProvider.create_for_remote(remote_url, git_backend))
	_active_pull_guard = LfsPullGuard.new()
	_active_pull_guard.repaired.connect(_on_pull_guard_repaired)
	_active_pull_guard.repair_completed.connect(_on_pull_guard_repair_completed)
	_active_pull_guard.repair(project_root, remote_url, remote_client, PackedStringArray([relative_path]))


func _start_remote_check() -> void:
	var remote_url := _get_remote_url()
	if remote_url.is_empty() or _remote_check_in_progress:
		return
	remote_client.setup(LfsCredentialProvider.create_for_remote(remote_url, git_backend))

	var objects_by_oid: Dictionary = {}
	for entry in _last_local_statuses:
		var e: Dictionary = entry
		if int(e.get("status", -1)) != LfsStatusScanner.Status.TRACKED_OK:
			continue
		var oid: String = e.get("oid", "")
		if oid.is_empty():
			continue
		objects_by_oid[oid] = {"oid": oid, "size": e.get("size", -1)}

	if objects_by_oid.is_empty():
		return

	_remote_check_in_progress = true
	remote_client.batch_result_ready.connect(_on_remote_check_result, CONNECT_ONE_SHOT)
	remote_client.check_objects(remote_url, objects_by_oid.values(), "download")


func _on_remote_check_result(_operation: String, response: Dictionary, error: String) -> void:
	_remote_check_in_progress = false
	if not error.is_empty():
		_show_error("Remote LFS check failed: %s" % error)
		return

	var missing_oids: Dictionary = {}
	for obj in response.get("objects", []):
		var o: Dictionary = obj
		var actions: Dictionary = o.get("actions", {})
		if o.has("error") or not actions.has("download"):
			missing_oids[o.get("oid", "")] = true

	var updated: Array = []
	for entry in _last_local_statuses:
		var e: Dictionary = (entry as Dictionary).duplicate()
		if int(e.get("status", -1)) == LfsStatusScanner.Status.TRACKED_OK and missing_oids.has(e.get("oid", "")):
			e["status"] = LfsStatusScanner.Status.TRACKED_REMOTE_MISSING
		updated.append(e)
	_last_local_statuses = updated

	_populate_extension_status_list(_last_tracked_patterns, LfsStatusScanner.aggregate_by_pattern(updated))


# --- Rebuild LFS Tracking ---


func _ensure_rebuild_service() -> void:
	if _rebuild_service == null:
		_rebuild_service = LfsRebuildService.new()
		_rebuild_service.completed.connect(_on_rebuild_completed)
	var remote_url := _get_remote_url()
	remote_client.setup(LfsCredentialProvider.create_for_remote(remote_url, git_backend))
	_rebuild_service.setup(project_root, git_backend, remote_client, remote_url)


func _on_rebuild_pressed() -> void:
	if _rebuild_scan_in_progress or project_root.is_empty():
		return
	_ensure_rebuild_service()
	_rebuild_scan_in_progress = true
	rebuild_button.disabled = true
	if _rebuild_scan_thread and _rebuild_scan_thread.is_started():
		_rebuild_scan_thread.wait_to_finish()
	_rebuild_scan_thread = Thread.new()
	_rebuild_scan_thread.start(_rebuild_scan_worker)


func _rebuild_scan_worker() -> void:
	var patterns := GitAttributesUtil.get_lfs_patterns(gitattributes_path)
	var result := _rebuild_service.scan_pending(patterns)
	call_deferred("_apply_rebuild_scan", result)


func _apply_rebuild_scan(result: Dictionary) -> void:
	if _rebuild_scan_thread and _rebuild_scan_thread.is_started():
		_rebuild_scan_thread.wait_to_finish()
	_rebuild_scan_in_progress = false
	rebuild_button.disabled = false

	var to_in: Array = result.get("to_migrate_in", [])
	var to_out: Array = result.get("to_migrate_out", [])
	if to_in.is_empty() and to_out.is_empty():
		_show_info("Nothing to rebuild -- all tracked patterns already match LFS state.")
		return

	_pending_rebuild_scan = result
	rebuild_confirm_dialog.dialog_text = "This will migrate %d file(s) into LFS and %d file(s) out of LFS.\nFiles are staged, not committed. This rewrites local git objects. Continue?" % [to_in.size(), to_out.size()]
	rebuild_confirm_dialog.popup_centered()


func _on_rebuild_confirmed() -> void:
	var to_in: Array = _pending_rebuild_scan.get("to_migrate_in", [])
	var to_out: Array = _pending_rebuild_scan.get("to_migrate_out", [])
	_pending_rebuild_scan = {}
	rebuild_button.disabled = true
	lfs_scan_status_label.text = "Rebuilding LFS tracking..."
	lfs_scan_status_label.visible = true
	_rebuild_service.start(to_in, to_out)


func _on_rebuild_completed(migrated_in: int, migrated_out: int, failed_paths: Array) -> void:
	rebuild_button.disabled = false
	lfs_scan_status_label.visible = false
	var message := "Rebuild complete: %d migrated into LFS, %d migrated out." % [migrated_in, migrated_out]
	if not failed_paths.is_empty():
		message += " %d file(s) failed: %s" % [failed_paths.size(), ", ".join(failed_paths)]
	_show_info(message)
	_refresh_all()


# --- Push LFS Objects ---


func _ensure_push_service() -> void:
	if _push_service == null:
		_push_service = LfsPushService.new()
		_push_service.completed.connect(_on_push_completed)
	var remote_url := _get_remote_url()
	remote_client.setup(LfsCredentialProvider.create_for_remote(remote_url, git_backend))
	_push_service.setup(project_root, remote_client, remote_url)


func _on_push_lfs_pressed() -> void:
	var entries: Array = []
	for entry in _last_local_statuses:
		var e: Dictionary = entry
		if int(e.get("status", -1)) == LfsStatusScanner.Status.TRACKED_REMOTE_MISSING:
			entries.append({"path": e.get("path", ""), "oid": e.get("oid", ""), "size": e.get("size", -1)})

	if entries.is_empty():
		_show_info("No files need pushing. Run 'Rescan Files' to check the remote first.")
		return

	_ensure_push_service()
	push_lfs_button.disabled = true
	lfs_scan_status_label.text = "Pushing LFS objects..."
	lfs_scan_status_label.visible = true
	_push_service.start(entries)


func _on_push_completed(pushed: int, failed_paths: Array) -> void:
	push_lfs_button.disabled = false
	lfs_scan_status_label.visible = false
	var message := "Pushed %d LFS object(s)." % pushed
	if not failed_paths.is_empty():
		message += " %d failed: %s" % [failed_paths.size(), ", ".join(failed_paths)]
	_show_info(message)
	_refresh_status(true)


# --- Locking ---


func _lock_manager() -> Object:
	return Engine.get_singleton("LfsLockManager")


func _get_lock_poll_interval() -> float:
	if not git_backend:
		return LOCK_POLL_INTERVAL_SEC
	var configured := git_backend.get_config_string("devtools.lfs.lockpollintervalsec", "")
	if configured.is_empty() or not configured.is_valid_float():
		return LOCK_POLL_INTERVAL_SEC
	return max(5.0, configured.to_float())


func _sync_lock_identity_and_policy() -> void:
	var lock_mgr := _lock_manager()
	if lock_mgr == null or not git_backend:
		return
	lock_mgr.set_local_identity(git_backend.get_config_string("user.name", ""), "")
	var max_locks_str := git_backend.get_config_string("devtools.lfs.maxlocks", "")
	if max_locks_str.is_valid_int():
		lock_mgr.set_max_locks(max_locks_str.to_int())


func _refresh_active_edits() -> void:
	for child in active_edits_list.get_children():
		child.queue_free()

	var lock_mgr := _lock_manager()
	var paths: Array = lock_mgr.get_my_locked_paths() if lock_mgr else []
	paths.sort()

	if paths.is_empty():
		var empty_label := Label.new()
		empty_label.text = "(no active edits)"
		active_edits_list.add_child(empty_label)
		return

	for path in paths:
		var row: GitStatusRow = GitStatusRowScene.instantiate()
		active_edits_list.add_child(row)
		row.load_entry(path, "locked", GitStatusRow.ActionMode.STAGE)
		row.action_button.visible = false
		row.file_selected.connect(_on_row_selected)
		_apply_lock_badge_to_row(row, path)


func _on_lock_poll_timeout() -> void:
	if not repo_open or not is_visible_in_tree() or not git_backend:
		return
	if _lock_poll_in_progress:
		return
	var remote_url := _get_remote_url()
	if remote_url.is_empty():
		return

	_sync_lock_identity_and_policy()

	_lock_poll_in_progress = true
	var lock_mgr := _lock_manager()
	_pending_retry_queue = (lock_mgr.get_pending_actions() if lock_mgr else []).duplicate()
	_drain_next_pending_action(remote_url)


## Retries queued lock/unlock requests one at a time (the shared
## %LfsRemoteClient node's _lock_request child only supports one in-flight
## request), then does the actual list_locks poll once the queue is empty.
func _drain_next_pending_action(remote_url: String) -> void:
	if _pending_retry_queue.is_empty():
		remote_client.lock_operation_ready.connect(_on_list_locks_result.bind(remote_url), CONNECT_ONE_SHOT)
		remote_client.list_locks(remote_url)
		return

	var action: Dictionary = _pending_retry_queue.pop_front()
	remote_client.lock_operation_ready.connect(_on_pending_action_result.bind(action, remote_url), CONNECT_ONE_SHOT)
	if action.get("type", "") == "lock":
		remote_client.create_lock(remote_url, action.get("path", ""))
	else:
		remote_client.delete_lock(remote_url, action.get("lock_id", ""))


func _on_pending_action_result(_operation: String, response: Dictionary, error: String, action: Dictionary, remote_url: String) -> void:
	var lock_mgr := _lock_manager()
	if error.is_empty() and lock_mgr:
		lock_mgr.clear_pending_action(action.get("path", ""))
		if action.get("type", "") == "lock":
			var lock: Dictionary = response.get("lock", {})
			if lock.has("id"):
				lock_mgr.record_self_created_lock(lock.get("id", ""), action.get("path", ""))
		else:
			lock_mgr.record_self_deleted_lock(action.get("lock_id", ""))
	_drain_next_pending_action(remote_url)


func _on_list_locks_result(_operation: String, response: Dictionary, error: String, _remote_url: String) -> void:
	_lock_poll_in_progress = false
	if not error.is_empty():
		return
	var lock_mgr := _lock_manager()
	if lock_mgr:
		lock_mgr.replace_locks(response.get("locks", []))
	_refresh_active_edits()
	_refresh_changes()
	_refresh_browse()


func _on_lock_requested(path: String) -> void:
	var lock_mgr := _lock_manager()
	if lock_mgr == null:
		return
	if not lock_mgr.can_acquire_more_locks():
		_show_error("Lock limit reached (%d/%d). Unlock another file first." % [lock_mgr.get_my_lock_count(), lock_mgr.get_max_locks()])
		return
	var remote_url := _get_remote_url()
	if remote_url.is_empty():
		_show_error("No remote configured; cannot lock.")
		return
	remote_client.lock_operation_ready.connect(_on_create_lock_result.bind(path), CONNECT_ONE_SHOT)
	remote_client.create_lock(remote_url, path)


func _on_create_lock_result(_operation: String, response: Dictionary, error: String, path: String) -> void:
	var lock_mgr := _lock_manager()
	if not error.is_empty():
		if lock_mgr:
			lock_mgr.enqueue_pending_lock(path)
		_show_error("Could not lock '%s': %s (will retry)." % [path, error])
		return
	var lock: Dictionary = response.get("lock", {})
	if lock_mgr and lock.has("id"):
		lock_mgr.record_self_created_lock(lock.get("id", ""), path)
	_refresh_active_edits()
	_refresh_changes()
	_refresh_browse()


func _on_unlock_requested(path: String) -> void:
	var lock_mgr := _lock_manager()
	if lock_mgr == null:
		return
	var lock: Dictionary = lock_mgr.get_lock_for_path(path)
	var lock_id: String = lock.get("id", "")
	if lock_id.is_empty():
		_show_error("No known lock id for '%s'; cannot unlock yet (try rescanning)." % path)
		return
	var remote_url := _get_remote_url()
	if remote_url.is_empty():
		_show_error("No remote configured; cannot unlock.")
		return
	remote_client.lock_operation_ready.connect(_on_delete_lock_result.bind(lock_id, path), CONNECT_ONE_SHOT)
	remote_client.delete_lock(remote_url, lock_id)


func _on_delete_lock_result(_operation: String, response: Dictionary, error: String, lock_id: String, path: String) -> void:
	var lock_mgr := _lock_manager()
	if not error.is_empty():
		if lock_mgr:
			lock_mgr.enqueue_pending_unlock(lock_id, path)
		_show_error("Could not unlock '%s': %s (will retry)." % [path, error])
		return
	if lock_mgr:
		lock_mgr.record_self_deleted_lock(lock_id)
	_refresh_active_edits()
	_refresh_changes()
	_refresh_browse()


# --- Pull-time quarantine + repair ---


func _on_pull_starting() -> void:
	var patterns := GitAttributesUtil.get_lfs_patterns(gitattributes_path)
	_pre_pull_snapshot = LfsPullGuard.snapshot(project_root, patterns)


func _on_pull_finished_relay(ok: bool, _error_message: String, merge_result: int) -> void:
	if not ok or merge_result != 0:
		_pre_pull_snapshot = {}
		return

	var patterns := GitAttributesUtil.get_lfs_patterns(gitattributes_path)
	var newly_quarantined := LfsPullGuard.quarantine_changed_pointers(project_root, patterns, _pre_pull_snapshot)
	_pre_pull_snapshot = {}
	_refresh_all()

	if newly_quarantined.is_empty():
		return

	var remote_url := _get_remote_url()
	remote_client.setup(LfsCredentialProvider.create_for_remote(remote_url, git_backend))
	_active_pull_guard = LfsPullGuard.new()
	_active_pull_guard.repaired.connect(_on_pull_guard_repaired)
	_active_pull_guard.repair_completed.connect(_on_pull_guard_repair_completed)
	_active_pull_guard.repair(project_root, remote_url, remote_client, newly_quarantined)


func _on_pull_guard_repaired(relative_path: String) -> void:
	var res_path := "res://" + relative_path.trim_prefix("/")
	EditorInterface.get_resource_filesystem().update_file(res_path)


func _on_pull_guard_repair_completed(repaired_paths: Array, failed_paths: Array) -> void:
	_active_pull_guard = null

	if not repaired_paths.is_empty():
		var res_paths := PackedStringArray()
		for p in repaired_paths:
			res_paths.append("res://" + String(p).trim_prefix("/"))
		EditorInterface.get_resource_filesystem().reimport_files(res_paths)

	if not failed_paths.is_empty():
		_show_error("%d file(s) could not be repaired after pull and remain quarantined: %s" % [failed_paths.size(), ", ".join(failed_paths)])

	_refresh_all()
