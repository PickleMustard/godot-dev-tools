@tool
extends ScrollContainer

signal file_selected(path: String)

@onready var file_list: GitFileList = %DiffFileList


func _ready() -> void:
	file_list.file_selected.connect(func(path: String) -> void: file_selected.emit(path))


func load_diff(files: Array) -> void:
	file_list.load_files(files, GitFilePanel.ActionMode.NONE)


func get_hunks_for(path: String) -> Array:
	return file_list.get_hunks_for(path)
