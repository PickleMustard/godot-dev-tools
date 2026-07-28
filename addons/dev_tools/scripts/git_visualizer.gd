@tool
extends Control

signal status_ready(status: Dictionary)
signal refresh_data_ready(data: Dictionary)
signal poll_signature_ready(sig: Dictionary)
signal pull_starting()
signal pull_finished_relay(ok: bool, error_message: String, merge_result: int)

const MAX_HISTORY: int = 300
const HISTORY_PREVIEW_COUNT: int = 5
const POLL_INTERVAL_SEC: float = 2.0
const REMOTE_POLL_INTERVAL_SEC: float = 1200.0
const BRANCH_MENU_CREATE_ID: int = 1 << 20
const GitStatusRowScene = preload("res://addons/dev_tools/menus/git_status_row.tscn")

var git_backend: GitBackend
var project_root: String = ""
var repo_open: bool = false
var gitattributes_path: String = ""
var _poll_timer: Timer
var _remote_poll_timer: Timer
var _last_signature: Dictionary = {}
var _branch_menu_names: PackedStringArray = []
var _initial_refresh_pending: bool = false
var _previous_view_name: String = "DiffView"
var _remote_op_busy: bool = false
var _pending_commit_message: String = ""
var _refresh_thread: Thread
var _refresh_in_progress: bool = false
var _poll_thread: Thread
var _poll_check_in_progress: bool = false

@onready var main_tabs: TabContainer = %MainTabs
@onready var fetch_button: Button = %FetchButton
@onready var pull_button: Button = %PullButton
@onready var pull_indicator: TextureRect = %PullIndicator
@onready var pull_gate_dialog: ConfirmationDialog = %PullGateDialog
@onready var branch_menu_button: MenuButton = %BranchMenuButton
@onready var staged_section: Control = %StagedSection
@onready var staged_list: VBoxContainer = %StagedList
@onready var unstaged_section: Control = %UnstagedSection
@onready var unstaged_list: VBoxContainer = %UnstagedList
@onready var stage_all_button: Button = %StageAllButton
@onready var history_section: Control = %HistorySection
@onready var history_preview_list: ItemList = %HistoryPreviewList
@onready var commit_message_edit: TextEdit = %CommitMessageEdit
@onready var commit_button: Button = %CommitButton
@onready var branches_view = %Branches
@onready var stash_view = %Stash
@onready var settings_view = %Settings
@onready var error_dialog: AcceptDialog = %ErrorDialog
@onready var repo_state_banner: Control = %RepoStateBanner
@onready var repo_state_label: Label = %RepoStateLabel

@onready var center_area: Control = %CenterArea
@onready var view_host: Control = %ViewHost
@onready var diff_view = %DiffView
@onready var staging_view = %StagingView
@onready var history_graph = %HistoryGraph
@onready var no_repo_view: Control = %NoRepoView
@onready var init_repo_button: Button = %InitRepoButton
@onready var file_detail_view = %FileDetailView


func setup(backend: GitBackend, is_open: bool, root_path: String) -> void:
	git_backend = backend
	repo_open = is_open
	project_root = root_path
	gitattributes_path = root_path.path_join(".gitattributes")
	if is_inside_tree():
		_request_initial_refresh_when_visible()


func _ready() -> void:
	staged_section.focus_entered.connect(_show_view.bind("StagingView"))
	unstaged_section.focus_entered.connect(_show_view.bind("StagingView"))
	history_section.focus_entered.connect(_show_view.bind("HistoryView"))
	history_preview_list.focus_entered.connect(_show_view.bind("HistoryView"))
	init_repo_button.pressed.connect(_on_init_repo_pressed)
	stage_all_button.pressed.connect(_on_stage_all_pressed)

	branch_menu_button.get_popup().id_pressed.connect(_on_branch_menu_id_pressed)
	commit_button.pressed.connect(_on_commit_pressed)
	branches_view.checkout_requested.connect(_checkout_branch)
	branches_view.checkout_remote_requested.connect(_checkout_remote_branch)
	branches_view.create_branch_requested.connect(_on_create_branch_requested)
	branches_view.merge_requested.connect(_on_merge_requested)

	stash_view.stash_apply_requested.connect(_on_stash_apply_requested)
	stash_view.stash_drop_requested.connect(_on_stash_drop_requested)

	settings_view.remote_url_save_requested.connect(_on_remote_url_save_requested)
	settings_view.ssh_key_save_requested.connect(_on_ssh_key_save_requested)
	settings_view.identity_save_requested.connect(_on_identity_save_requested)
	settings_view.remote_add_requested.connect(_on_settings_remote_add_requested)
	settings_view.remote_remove_requested.connect(_on_settings_remote_remove_requested)
	settings_view.config_set_requested.connect(_on_settings_config_set_requested)

	diff_view.file_selected.connect(_on_diff_file_selected)
	staging_view.file_selected.connect(_on_staging_file_selected)
	staging_view.stage_requested.connect(_on_stage_requested)
	staging_view.unstage_requested.connect(_on_unstage_requested)
	file_detail_view.back_requested.connect(_on_file_detail_back)
	refresh_data_ready.connect(_apply_refresh_data)
	poll_signature_ready.connect(_apply_poll_signature)

	fetch_button.pressed.connect(_on_fetch_pressed)
	pull_button.pressed.connect(_on_pull_pressed)
	pull_gate_dialog.confirmed.connect(_on_pull_gate_pull_confirmed)
	pull_gate_dialog.custom_action.connect(_on_pull_gate_custom_action)
	pull_gate_dialog.add_button("Commit Anyway", true, "commit_anyway")
	pull_indicator.texture = get_theme_icon("MoveDown", "EditorIcons")

	_poll_timer = Timer.new()
	_poll_timer.wait_time = POLL_INTERVAL_SEC
	_poll_timer.autostart = true
	_poll_timer.one_shot = false
	add_child(_poll_timer)
	_poll_timer.timeout.connect(_on_poll_timeout)

	_remote_poll_timer = Timer.new()
	_remote_poll_timer.wait_time = REMOTE_POLL_INTERVAL_SEC
	_remote_poll_timer.autostart = true
	_remote_poll_timer.one_shot = false
	add_child(_remote_poll_timer)
	_remote_poll_timer.timeout.connect(_on_remote_poll_timeout)

	if git_backend:
		git_backend.fetch_finished.connect(_on_fetch_finished)
		git_backend.pull_finished.connect(_on_pull_finished)
		_request_initial_refresh_when_visible()


func set_polling_active(active: bool) -> void:
	if _poll_timer:
		_poll_timer.paused = not active
	if _remote_poll_timer:
		_remote_poll_timer.paused = not active


func _exit_tree() -> void:
	if _refresh_thread and _refresh_thread.is_started():
		_refresh_thread.wait_to_finish()
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
	get_tree().create_timer(0.3).timeout.connect(_refresh)


func _refresh() -> void:
	if not repo_open:
		_show_view("NoRepoView")
		return
	if _refresh_in_progress:
		return
	_refresh_in_progress = true
	_refresh_thread = Thread.new()
	_refresh_thread.start(_refresh_worker)


func _refresh_worker() -> void:
	var data: Dictionary = {}
	data["branches"] = git_backend.list_branches()
	data["current_branch"] = git_backend.get_current_branch()
	data["status"] = git_backend.get_status()
	data["history"] = git_backend.get_commit_history(MAX_HISTORY)
	data["diff_head"] = git_backend.get_diff_head()
	data["staged_diff"] = git_backend.get_staged_diff()
	data["unstaged_diff"] = git_backend.get_unstaged_diff()
	var repo_state: String = git_backend.get_repository_state()
	data["repo_state"] = repo_state
	data["rebase_progress"] = git_backend.get_rebase_progress() if repo_state == "rebase" else {}
	data["stashes"] = git_backend.list_stashes()
	data["settings"] = _gather_settings_data()
	call_deferred("emit_signal", "refresh_data_ready", data)


func _gather_settings_data() -> Dictionary:
	var remotes: Array = git_backend.list_remotes()
	var remote_url := ""
	for r in remotes:
		var d: Dictionary = r
		if d.get("name", "") == "origin":
			remote_url = d.get("url", "")
			break
	return {
		"remote_url": remote_url,
		"remotes": remotes,
		"ssh_key_path": git_backend.get_config_string("devtools.sshkeypath"),
		"user_name": git_backend.get_config_string("user.name"),
		"user_email": git_backend.get_config_string("user.email"),
		"core_autocrlf": git_backend.get_config_string("core.autocrlf"),
		"core_filemode": git_backend.get_config_string("core.filemode"),
		"pull_rebase": git_backend.get_config_string("pull.rebase"),
		"push_default": git_backend.get_config_string("push.default"),
		"init_default_branch": git_backend.get_config_string("init.defaultBranch"),
	}


func _apply_refresh_data(data: Dictionary) -> void:
	if _refresh_thread and _refresh_thread.is_started():
		_refresh_thread.wait_to_finish()
	_refresh_thread = null
	_refresh_in_progress = false

	var branches: Array = data["branches"]
	var current_branch: String = data["current_branch"]
	branch_menu_button.text = "Branch: %s" % current_branch
	_populate_branch_menu(branches)
	branches_view.load_branches(branches)

	var status: Dictionary = data["status"]
	status_ready.emit(status)
	_populate_file_list(staged_list, status.get("staged", []), GitStatusRow.ActionMode.UNSTAGE)

	var unstaged_combined: Array = []
	unstaged_combined.append_array(status.get("unstaged", []))
	unstaged_combined.append_array(status.get("untracked", []))
	unstaged_combined = _filter_expected_lfs_divergence(unstaged_combined)
	_populate_file_list(unstaged_list, unstaged_combined, GitStatusRow.ActionMode.STAGE)

	var history: Array = data["history"]
	_populate_history_preview(history)

	diff_view.load_diff(data["diff_head"])
	staging_view.load_diffs(data["staged_diff"], data["unstaged_diff"])
	history_graph.load_history(history)

	_update_repo_state_banner(data["repo_state"], data["rebase_progress"])
	stash_view.load_stashes(data["stashes"])
	settings_view.load_settings(data["settings"])

	var current_view := _get_current_view_name()
	if current_view.is_empty() or current_view == "NoRepoView":
		current_view = "DiffView"
	_show_view(current_view)

	var top_hash := ""
	if history.size() > 0:
		top_hash = (history[0] as Dictionary).get("hash", "")
	var stashes: Array = data["stashes"]
	_last_signature = {
		"branch": current_branch,
		"status": status,
		"top_hash": top_hash,
		"branches": branches,
		"repo_state": data["repo_state"],
		"stash_count": stashes.size(),
		"stash_top_oid": (stashes[0] as Dictionary).get("oid", "") if stashes.size() > 0 else "",
	}

	_refresh_pull_indicator()


func _on_poll_timeout() -> void:
	if not repo_open or not is_visible_in_tree():
		return
	if _refresh_in_progress or _poll_check_in_progress:
		return
	_poll_check_in_progress = true
	_poll_thread = Thread.new()
	_poll_thread.start(_poll_signature_worker)


func _poll_signature_worker() -> void:
	var sig := _compute_poll_signature()
	call_deferred("emit_signal", "poll_signature_ready", sig)


func _apply_poll_signature(sig: Dictionary) -> void:
	if _poll_thread and _poll_thread.is_started():
		_poll_thread.wait_to_finish()
	_poll_thread = null
	_poll_check_in_progress = false
	if sig != _last_signature:
		_refresh()


func _on_remote_poll_timeout() -> void:
	if not repo_open or not git_backend or _remote_op_busy:
		return
	_start_fetch()


func _compute_poll_signature() -> Dictionary:
	if not repo_open:
		return {}
	var stashes: Array = git_backend.list_stashes()
	return {
		"branch": git_backend.get_current_branch(),
		"status": git_backend.get_status(),
		"top_hash": git_backend.get_head_oid_hex(),
		"branches": git_backend.list_branches(),
		"repo_state": git_backend.get_repository_state(),
		"stash_count": stashes.size(),
		"stash_top_oid": (stashes[0] as Dictionary).get("oid", "") if stashes.size() > 0 else "",
	}


func _filter_expected_lfs_divergence(entries: Array) -> Array:
	if gitattributes_path.is_empty():
		return entries
	var patterns := GitAttributesUtil.get_lfs_patterns(gitattributes_path)
	if patterns.is_empty():
		return entries
	var manifest := LfsManifest.load_manifest(project_root)
	if manifest.is_empty():
		return entries

	var result: Array = []
	for entry in entries:
		var e: Dictionary = entry
		var path: String = e.get("path", "")
		if LfsStatusScanner.is_expected_lfs_divergence(project_root, path, patterns, manifest):
			continue
		result.append(e)
	return result


func _populate_file_list(list: VBoxContainer, entries: Array, mode: int) -> void:
	for child in list.get_children():
		child.queue_free()
	for entry in entries:
		var e: Dictionary = entry
		var row: GitStatusRow = GitStatusRowScene.instantiate()
		list.add_child(row)
		row.load_entry(e.get("path", ""), e.get("status", ""), mode)
		row.file_selected.connect(_on_sidebar_file_selected)
		row.stage_requested.connect(_on_stage_requested)
		row.unstage_requested.connect(_on_unstage_requested)


func _on_sidebar_file_selected(path: String) -> void:
	_open_file_detail(path, staging_view.get_hunks_for(path), "StagingView")


func _on_diff_file_selected(path: String) -> void:
	_open_file_detail(path, diff_view.get_hunks_for(path), "DiffView")


func _on_staging_file_selected(path: String) -> void:
	_open_file_detail(path, staging_view.get_hunks_for(path), "StagingView")


func _open_file_detail(path: String, hunks: Array, return_view: String) -> void:
	_previous_view_name = return_view
	file_detail_view.load_file(path, hunks)
	_show_view("FileDetailView")


func _on_file_detail_back() -> void:
	_show_view(_previous_view_name)


func _on_stage_requested(path: String) -> void:
	if not git_backend:
		return
	if git_backend.stage_file(path):
		_leave_file_detail_if_showing()
		_refresh()
	else:
		_show_error("Could not stage '%s'." % path)


func _on_unstage_requested(path: String) -> void:
	if not git_backend:
		return
	if git_backend.unstage_file(path):
		_leave_file_detail_if_showing()
		_refresh()
	else:
		_show_error("Could not unstage '%s'." % path)


func _on_stash_apply_requested(index: int, pop: bool) -> void:
	if not git_backend:
		return
	if git_backend.apply_stash(index, pop):
		_refresh()
	else:
		_show_error("Could not %s stash@{%d}. There may be a conflict requiring manual resolution (use the git CLI)." % ["pop" if pop else "apply", index])


func _on_stash_drop_requested(index: int) -> void:
	if not git_backend:
		return
	if git_backend.drop_stash(index):
		_refresh()
	else:
		_show_error("Could not drop stash@{%d}." % index)


func _on_stage_all_pressed() -> void:
	if not git_backend:
		return
	var status: Dictionary = git_backend.get_status()
	var entries: Array = []
	entries.append_array(status.get("unstaged", []))
	entries.append_array(status.get("untracked", []))
	var any_failed := false
	for entry in entries:
		var e: Dictionary = entry
		if not git_backend.stage_file(e.get("path", "")):
			any_failed = true
	_leave_file_detail_if_showing()
	_refresh()
	if any_failed:
		_show_error("Some files could not be staged.")


func _leave_file_detail_if_showing() -> void:
	if _get_current_view_name() == "FileDetailView":
		_show_view(_previous_view_name)


func _update_repo_state_banner(state: String, rebase_progress: Dictionary) -> void:
	if state == "none" or state.is_empty():
		repo_state_banner.visible = false
		return

	repo_state_banner.visible = true
	match state:
		"rebase":
			if rebase_progress.get("in_progress", false):
				var current: int = int(rebase_progress.get("current", 0)) + 1
				var total: int = int(rebase_progress.get("total", 0))
				repo_state_label.text = "Rebase in progress (step %d of %d)" % [current, total]
			else:
				repo_state_label.text = "Rebase in progress"
		"merge":
			repo_state_label.text = "Merge in progress"
		"cherry-pick":
			repo_state_label.text = "Cherry-pick in progress"
		"revert":
			repo_state_label.text = "Revert in progress"
		"bisect":
			repo_state_label.text = "Bisect in progress"
		"apply-mailbox":
			repo_state_label.text = "Applying mailbox patches"
		_:
			repo_state_label.text = "Repository operation in progress"


func _populate_history_preview(history: Array) -> void:
	history_preview_list.clear()
	var count: int = min(HISTORY_PREVIEW_COUNT, history.size())
	for i in range(count):
		var c: Dictionary = history[i]
		history_preview_list.add_item("%s %s" % [c.get("short_hash", ""), c.get("summary", "")])


func _show_view(view_name: String) -> void:
	for child in view_host.get_children():
		child.visible = (child.name == view_name)


func _get_current_view_name() -> String:
	for child in view_host.get_children():
		if child.visible:
			return child.name
	return ""


func _on_init_repo_pressed() -> void:
	if not git_backend or project_root.is_empty():
		return
	if git_backend.init_repository(project_root):
		repo_open = git_backend.open_repository(project_root)
		_refresh()


func _populate_branch_menu(branches: Array) -> void:
	var popup := branch_menu_button.get_popup()
	popup.clear()
	_branch_menu_names.clear()

	var id := 0
	for b in branches:
		var d: Dictionary = b
		if d.get("is_current", false):
			continue
		popup.add_item(d.get("name", ""), id)
		_branch_menu_names.append(d.get("name", ""))
		id += 1

	popup.add_separator()
	popup.add_item("Create Branch...", BRANCH_MENU_CREATE_ID)


func _on_branch_menu_id_pressed(id: int) -> void:
	if id == BRANCH_MENU_CREATE_ID:
		main_tabs.current_tab = main_tabs.get_tab_idx_from_control(branches_view)
		branches_view.focus_new_branch_field()
		return
	if id < 0 or id >= _branch_menu_names.size():
		return
	_checkout_branch(_branch_menu_names[id])


func _checkout_branch(branch_name: String) -> void:
	if not git_backend:
		return
	if git_backend.checkout_branch(branch_name):
		_refresh()
	else:
		_show_error("Could not check out branch '%s'. Commit or discard local changes first." % branch_name)


func _checkout_remote_branch(remote_ref_name: String) -> void:
	if not git_backend:
		return
	if git_backend.checkout_remote_branch(remote_ref_name):
		_refresh()
	else:
		_show_error("Could not check out remote branch '%s'. It may already have a differently-named local tracking branch, or local changes conflict." % remote_ref_name)


func _on_commit_pressed() -> void:
	if not git_backend:
		return
	var msg := commit_message_edit.text.strip_edges()
	if msg.is_empty():
		_show_error("Commit message cannot be empty.")
		return

	var ab: Dictionary = git_backend.get_ahead_behind("origin")
	if ab.get("has_upstream", false) and ab.get("behind", 0) > 0:
		_pending_commit_message = msg
		pull_gate_dialog.popup_centered()
		return

	_do_commit(msg)


func _do_commit(msg: String) -> void:
	if git_backend.commit_staged(msg):
		commit_message_edit.text = ""
		_refresh()
	else:
		_show_error("Commit failed. Make sure there are staged changes.")


func _on_pull_gate_pull_confirmed() -> void:
	_pending_commit_message = ""
	_start_pull()


func _on_pull_gate_custom_action(action_name: String) -> void:
	if action_name != "commit_anyway":
		return
	pull_gate_dialog.hide()
	var msg := _pending_commit_message
	_pending_commit_message = ""
	_do_commit(msg)


func _on_fetch_pressed() -> void:
	_start_fetch()


func _on_pull_pressed() -> void:
	_start_pull()


func _start_fetch() -> void:
	if not git_backend or _remote_op_busy:
		return
	if git_backend.start_fetch("origin"):
		_set_remote_busy(true)
	else:
		_show_error("A fetch or pull is already in progress.")


func _start_pull() -> void:
	if not git_backend or _remote_op_busy:
		return
	pull_starting.emit()
	if git_backend.start_pull("origin"):
		_set_remote_busy(true)
	else:
		_show_error("A fetch or pull is already in progress.")


func _set_remote_busy(busy: bool) -> void:
	_remote_op_busy = busy
	fetch_button.disabled = busy
	pull_button.disabled = busy


func _on_fetch_finished(ok: bool, error_message: String) -> void:
	_set_remote_busy(false)
	if ok:
		_refresh_pull_indicator()
	else:
		_show_error("Fetch failed: %s" % error_message)


func _on_pull_finished(ok: bool, error_message: String, merge_result: int) -> void:
	_set_remote_busy(false)
	if ok and merge_result == 0:
		_refresh()
	elif ok and merge_result == 1:
		_show_info("Already up to date with the remote.")
	else:
		_show_error("Pull failed: %s" % error_message)
	_refresh_pull_indicator()
	pull_finished_relay.emit(ok, error_message, merge_result)


func _refresh_pull_indicator() -> void:
	if not git_backend:
		return
	var ab: Dictionary = git_backend.get_ahead_behind("origin")
	pull_indicator.visible = ab.get("has_upstream", false) and ab.get("behind", 0) > 0


func _on_create_branch_requested(branch_name: String, checkout_after: bool) -> void:
	if not git_backend:
		return
	if git_backend.create_branch(branch_name, checkout_after):
		_refresh()
	else:
		_show_error("Could not create branch '%s'. It may already exist, or the name is invalid." % branch_name)


func _on_merge_requested(source: String, target: String) -> void:
	if not git_backend:
		return
	if source == target:
		_show_error("Source and target branches must be different.")
		return
	var result: int = git_backend.merge_branch(source, target)
	if result == 0:
		_refresh()
	elif result == 1:
		_show_info("'%s' already contains all commits from '%s'." % [target, source])
	else:
		_show_error("Merge of '%s' into '%s' failed. There may be a conflict requiring manual resolution (use the git CLI)." % [source, target])


func _on_remote_url_save_requested(url: String) -> void:
	if not git_backend:
		return
	if git_backend.set_remote_url("origin", url):
		_refresh()
	else:
		_show_error("Could not set remote 'origin' to '%s'." % url)


func _on_ssh_key_save_requested(path: String, passphrase: String) -> void:
	if not git_backend:
		return
	git_backend.set_ssh_passphrase(passphrase)
	if git_backend.set_config_string("devtools.sshkeypath", path):
		_refresh()
	else:
		_show_error("Could not save SSH key path.")


func _on_identity_save_requested(user_name: String, user_email: String) -> void:
	if not git_backend:
		return
	if git_backend.set_config_string("user.name", user_name) and git_backend.set_config_string("user.email", user_email):
		_refresh()
	else:
		_show_error("Could not save identity settings.")


func _on_settings_remote_add_requested(remote_name: String, url: String) -> void:
	if not git_backend:
		return
	if git_backend.set_remote_url(remote_name, url):
		_refresh()
	else:
		_show_error("Could not add remote '%s'." % remote_name)


func _on_settings_remote_remove_requested(remote_name: String) -> void:
	if not git_backend:
		return
	if git_backend.remove_remote(remote_name):
		_refresh()
	else:
		_show_error("Could not remove remote '%s'." % remote_name)


func _on_settings_config_set_requested(key: String, value: String) -> void:
	if not git_backend:
		return
	if git_backend.set_config_string(key, value):
		_refresh()
	else:
		_show_error("Could not set config '%s'." % key)


func _show_error(message: String) -> void:
	error_dialog.title = "Git Error"
	error_dialog.dialog_text = message
	error_dialog.popup_centered()


func _show_info(message: String) -> void:
	error_dialog.title = "Git Info"
	error_dialog.dialog_text = message
	error_dialog.popup_centered()
