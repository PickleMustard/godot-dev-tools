@tool
extends EditorPlugin

const MainPanelScene := preload("res://addons/dev_tools/menus/dev_tools_panel.tscn")
const GitVisualizerScene := preload("res://addons/dev_tools/menus/git_visualizer.tscn")
const DotfilesViewScene := preload("res://addons/dev_tools/menus/dotfiles_view.tscn")
const LfsVisualizerScene := preload("res://addons/dev_tools/menus/lfs_visualizer.tscn")

var main_panel: Control
var git_backend: GitBackend
var git_visualizer: Control
var dotfiles_view: Control
var lfs_visualizer: Control
var project_root: String = ""
var repo_open: bool = false


func _enter_tree() -> void:
	main_panel = MainPanelScene.instantiate()
	get_editor_interface().get_editor_main_screen().add_child(main_panel)
	_make_visible(false)

	project_root = ProjectSettings.globalize_path("res://")

	call_deferred("_init_git_tab")
	call_deferred("_init_dotfiles_tab")
	call_deferred("_init_lfs_tab")


func _init_git_tab() -> void:
	git_backend = GitBackend.new()
	repo_open = git_backend.open_repository(project_root)

	git_visualizer = GitVisualizerScene.instantiate()
	git_visualizer.setup(git_backend, repo_open, project_root)
	main_panel.add_tool_tab("Git", git_visualizer)


func _init_dotfiles_tab() -> void:
	dotfiles_view = DotfilesViewScene.instantiate()
	dotfiles_view.setup(project_root)
	main_panel.add_tool_tab("Dotfiles", dotfiles_view)


func _init_lfs_tab() -> void:
	lfs_visualizer = LfsVisualizerScene.instantiate()
	lfs_visualizer.setup(git_backend, repo_open, project_root)
	main_panel.add_tool_tab("LFS", lfs_visualizer)
	if git_visualizer:
		git_visualizer.status_ready.connect(lfs_visualizer._on_git_status_ready)


func _exit_tree() -> void:
	if git_visualizer:
		git_visualizer.queue_free()
		git_visualizer = null
	if dotfiles_view:
		dotfiles_view.queue_free()
		dotfiles_view = null
	if lfs_visualizer:
		lfs_visualizer.queue_free()
		lfs_visualizer = null
	git_backend = null
	if main_panel:
		main_panel.queue_free()
		main_panel = null


func _has_main_screen() -> bool:
	return true


func _make_visible(visible: bool) -> void:
	if main_panel:
		main_panel.visible = visible
	if git_visualizer:
		git_visualizer.set_polling_active(visible)
	if lfs_visualizer:
		lfs_visualizer.set_polling_active(visible)


func _get_plugin_name() -> String:
	return "Dev Tools"


func _get_plugin_icon() -> Texture2D:
	return get_editor_interface().get_base_control().get_theme_icon("Tools", "EditorIcons")
