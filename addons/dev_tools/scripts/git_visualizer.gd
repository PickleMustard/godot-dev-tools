@tool
extends Control

const MAX_HISTORY: int = 300
const HISTORY_PREVIEW_COUNT: int = 5

var git_backend: GitBackend
var project_root: String = ""
var repo_open: bool = false

@onready var branch_label: Label = %BranchLabel
@onready var staged_section: Control = %StagedSection
@onready var staged_list: ItemList = %StagedList
@onready var unstaged_section: Control = %UnstagedSection
@onready var unstaged_list: ItemList = %UnstagedList
@onready var history_section: Control = %HistorySection
@onready var history_preview_list: ItemList = %HistoryPreviewList

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
		_refresh()


func _ready() -> void:
	_apply_panel_styles()
	staged_section.focus_entered.connect(_show_view.bind("StagingView"))
	staged_list.focus_entered.connect(_show_view.bind("StagingView"))
	unstaged_section.focus_entered.connect(_show_view.bind("StagingView"))
	unstaged_list.focus_entered.connect(_show_view.bind("StagingView"))
	history_section.focus_entered.connect(_show_view.bind("HistoryView"))
	history_preview_list.focus_entered.connect(_show_view.bind("HistoryView"))
	init_repo_button.pressed.connect(_on_init_repo_pressed)

	if git_backend:
		_refresh()


func _apply_panel_styles() -> void:
	if not Engine.is_editor_hint():
		return
	var editor_theme := EditorInterface.get_editor_theme()
	if not editor_theme or not editor_theme.has_stylebox("panel", "PanelContainer"):
		return
	var base_style: StyleBox = editor_theme.get_stylebox("panel", "PanelContainer").duplicate()

	var sidebar_style: StyleBox = base_style.duplicate()
	sidebar_style.content_margin_left = 8
	sidebar_style.content_margin_top = 6
	sidebar_style.content_margin_right = 8
	sidebar_style.content_margin_bottom = 6
	if sidebar_style is StyleBoxFlat:
		(sidebar_style as StyleBoxFlat).bg_color = (sidebar_style as StyleBoxFlat).bg_color.darkened(0.06)
	staged_section.add_theme_stylebox_override("panel", sidebar_style)
	unstaged_section.add_theme_stylebox_override("panel", sidebar_style)
	history_section.add_theme_stylebox_override("panel", sidebar_style)

	var center_style: StyleBox = base_style.duplicate()
	center_style.content_margin_left = 12
	center_style.content_margin_top = 10
	center_style.content_margin_right = 12
	center_style.content_margin_bottom = 10
	if center_style is StyleBoxFlat:
		(center_style as StyleBoxFlat).bg_color = (center_style as StyleBoxFlat).bg_color.lightened(0.03)
	center_area.add_theme_stylebox_override("panel", center_style)


func _refresh() -> void:
	if not repo_open:
		_show_view("NoRepoView")
		return

	branch_label.text = "Branch: %s" % git_backend.get_current_branch()

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
