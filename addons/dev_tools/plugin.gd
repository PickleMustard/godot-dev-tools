@tool
extends EditorPlugin

const MainPanelScene := preload("res://addons/dev_tools/menus/dev_tools_panel.tscn")
const GitVisualizerScene := preload("res://addons/dev_tools/menus/git_visualizer.tscn")

var main_panel: Control
var git_backend: GitBackend
var git_visualizer: Control


func _enter_tree() -> void:
	main_panel = MainPanelScene.instantiate()
	get_editor_interface().get_editor_main_screen().add_child(main_panel)

	git_backend = GitBackend.new()
	var project_root := ProjectSettings.globalize_path("res://")
	var repo_open := git_backend.open_repository(project_root)

	git_visualizer = GitVisualizerScene.instantiate()
	git_visualizer.setup(git_backend, repo_open, project_root)
	main_panel.add_tool_tab("Git", git_visualizer)

	_make_visible(false)


func _exit_tree() -> void:
	if git_visualizer:
		git_visualizer.queue_free()
		git_visualizer = null
	git_backend = null
	if main_panel:
		main_panel.queue_free()
		main_panel = null


func _has_main_screen() -> bool:
	return true


func _make_visible(visible: bool) -> void:
	if main_panel:
		main_panel.visible = visible


func _get_plugin_name() -> String:
	return "Dev Tools"


func _get_plugin_icon() -> Texture2D:
	return get_editor_interface().get_base_control().get_theme_icon("Tools", "EditorIcons")
