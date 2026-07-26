@tool
extends Control

const MAX_HISTORY: int = 300
const HISTORY_PREVIEW_COUNT: int = 5
const POLL_INTERVAL_SEC: float = 2.0
const BRANCH_MENU_CREATE_ID: int = 1 << 20

var git_backend: GitBackend
var project_root: String = ""
var repo_open: bool = false
var _poll_timer: Timer
var _last_signature: Dictionary = {}
var _branch_menu_names: PackedStringArray = []
var _initial_refresh_pending: bool = false

@onready var main_tabs: TabContainer = %MainTabs
@onready var branch_menu_button: MenuButton = %BranchMenuButton
@onready var staged_section: Control = %StagedSection
@onready var staged_list: ItemList = %StagedList
@onready var unstaged_section: Control = %UnstagedSection
@onready var unstaged_list: ItemList = %UnstagedList
@onready var history_section: Control = %HistorySection
@onready var history_preview_list: ItemList = %HistoryPreviewList
@onready var commit_message_edit: TextEdit = %CommitMessageEdit
@onready var commit_button: Button = %CommitButton
@onready var branches_view = %Branches
@onready var error_dialog: AcceptDialog = %ErrorDialog

@onready var center_area: Control = %CenterArea
@onready var view_host: Control = %ViewHost
@onready var diff_view = %DiffView
@onready var staging_view = %StagingView
@onready var history_graph = %HistoryGraph
@onready var no_repo_view: Control = %NoRepoView
@onready var init_repo_button: Button = %InitRepoButton


func setup(backend: GitBackend, is_open: bool, root_path: String) -> void:
	git_backend = backend
	repo_open = is_open
	project_root = root_path
	if is_inside_tree():
		_schedule_initial_refresh()


func _ready() -> void:
	staged_section.focus_entered.connect(_show_view.bind("StagingView"))
	staged_list.focus_entered.connect(_show_view.bind("StagingView"))
	unstaged_section.focus_entered.connect(_show_view.bind("StagingView"))
	unstaged_list.focus_entered.connect(_show_view.bind("StagingView"))
	history_section.focus_entered.connect(_show_view.bind("HistoryView"))
	history_preview_list.focus_entered.connect(_show_view.bind("HistoryView"))
	init_repo_button.pressed.connect(_on_init_repo_pressed)

	branch_menu_button.get_popup().id_pressed.connect(_on_branch_menu_id_pressed)
	commit_button.pressed.connect(_on_commit_pressed)
	branches_view.checkout_requested.connect(_checkout_branch)
	branches_view.create_branch_requested.connect(_on_create_branch_requested)
	branches_view.merge_requested.connect(_on_merge_requested)

	_poll_timer = Timer.new()
	_poll_timer.wait_time = POLL_INTERVAL_SEC
	_poll_timer.autostart = true
	_poll_timer.one_shot = false
	add_child(_poll_timer)
	_poll_timer.timeout.connect(_on_poll_timeout)

	if git_backend:
		_schedule_initial_refresh()


func set_polling_active(active: bool) -> void:
	if _poll_timer:
		_poll_timer.paused = not active


func _schedule_initial_refresh() -> void:
	if _initial_refresh_pending:
		return
	_initial_refresh_pending = true
	get_tree().create_timer(0.3).timeout.connect(_refresh)


func _refresh() -> void:
	if not repo_open:
		_show_view("NoRepoView")
		return

	var branches: Array = git_backend.list_branches()
	var current_branch: String = git_backend.get_current_branch()
	branch_menu_button.text = "Branch: %s" % current_branch
	_populate_branch_menu(branches)
	branches_view.load_branches(branches)

	var status: Dictionary = git_backend.get_status()
	_populate_file_list(staged_list, status.get("staged", []))

	var unstaged_combined: Array = []
	unstaged_combined.append_array(status.get("unstaged", []))
	unstaged_combined.append_array(status.get("untracked", []))
	_populate_file_list(unstaged_list, unstaged_combined)

	var history: Array = git_backend.get_commit_history(MAX_HISTORY)
	_populate_history_preview(history)

	diff_view.load_diff(git_backend.get_diff_head())
	staging_view.load_diffs(git_backend.get_staged_diff(), git_backend.get_unstaged_diff())
	history_graph.load_history(history)

	_show_view("DiffView")

	var top_hash := ""
	if history.size() > 0:
		top_hash = (history[0] as Dictionary).get("hash", "")
	_last_signature = {
		"branch": current_branch,
		"status": status,
		"top_hash": top_hash,
		"branches": branches,
	}


func _on_poll_timeout() -> void:
	if not repo_open or not is_visible_in_tree():
		return
	var sig := _compute_poll_signature()
	if sig != _last_signature:
		_refresh()


func _compute_poll_signature() -> Dictionary:
	if not repo_open:
		return {}
	return {
		"branch": git_backend.get_current_branch(),
		"status": git_backend.get_status(),
		"top_hash": git_backend.get_head_oid_hex(),
		"branches": git_backend.list_branches(),
	}


func _populate_file_list(list: ItemList, entries: Array) -> void:
	list.clear()
	for entry in entries:
		var e: Dictionary = entry
		list.add_item("[%s] %s" % [e.get("status", ""), e.get("path", "")])


func _populate_history_preview(history: Array) -> void:
	history_preview_list.clear()
	var count: int = min(HISTORY_PREVIEW_COUNT, history.size())
	for i in range(count):
		var c: Dictionary = history[i]
		history_preview_list.add_item("%s %s" % [c.get("short_hash", ""), c.get("summary", "")])


func _show_view(view_name: String) -> void:
	for child in view_host.get_children():
		child.visible = (child.name == view_name)


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


func _on_commit_pressed() -> void:
	if not git_backend:
		return
	var msg := commit_message_edit.text.strip_edges()
	if msg.is_empty():
		_show_error("Commit message cannot be empty.")
		return
	if git_backend.commit_staged(msg):
		commit_message_edit.text = ""
		_refresh()
	else:
		_show_error("Commit failed. Make sure there are staged changes.")


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


func _show_error(message: String) -> void:
	error_dialog.title = "Git Error"
	error_dialog.dialog_text = message
	error_dialog.popup_centered()


func _show_info(message: String) -> void:
	error_dialog.title = "Git Info"
	error_dialog.dialog_text = message
	error_dialog.popup_centered()
