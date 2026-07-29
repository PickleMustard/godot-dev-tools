@tool
class_name DevToolsGitFileList
extends VBoxContainer

signal file_selected(path: String)
signal stage_requested(path: String)
signal unstage_requested(path: String)

const GitFilePanelScene = preload("res://addons/dev_tools/menus/git_file_panel.tscn")

var _files_by_path: Dictionary = {}


func load_files(files: Array, action_mode: int) -> void:
	for child in get_children():
		child.queue_free()
	_files_by_path.clear()

	if files.is_empty():
		var empty_label := Label.new()
		empty_label.text = "(no changes)"
		add_child(empty_label)
		return

	for file in files:
		var f: Dictionary = file
		var path: String = f.get("new_path", "")
		if path.is_empty():
			path = f.get("old_path", "")
		_files_by_path[path] = f

		var panel: DevToolsGitFilePanel = GitFilePanelScene.instantiate()
		add_child(panel)
		panel.load_file(f, action_mode)
		panel.file_selected.connect(func(p: String) -> void: file_selected.emit(p))
		panel.stage_requested.connect(func(p: String) -> void: stage_requested.emit(p))
		panel.unstage_requested.connect(func(p: String) -> void: unstage_requested.emit(p))


func get_hunks_for(path: String) -> Array:
	var f: Dictionary = _files_by_path.get(path, {})
	return f.get("hunks", [])
