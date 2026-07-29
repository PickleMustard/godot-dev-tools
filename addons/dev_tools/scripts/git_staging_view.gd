@tool
extends Control

signal file_selected(path: String)
signal stage_requested(path: String)
signal unstage_requested(path: String)

@onready var staged_list: DevToolsGitFileList = %StagedFileList
@onready var unstaged_list: DevToolsGitFileList = %UnstagedFileList


func _ready() -> void:
	staged_list.file_selected.connect(func(path: String) -> void: file_selected.emit(path))
	staged_list.unstage_requested.connect(func(path: String) -> void: unstage_requested.emit(path))
	unstaged_list.file_selected.connect(func(path: String) -> void: file_selected.emit(path))
	unstaged_list.stage_requested.connect(func(path: String) -> void: stage_requested.emit(path))


func load_diffs(staged_files: Array, unstaged_files: Array) -> void:
	staged_list.load_files(staged_files, DevToolsGitFilePanel.ActionMode.UNSTAGE)
	unstaged_list.load_files(unstaged_files, DevToolsGitFilePanel.ActionMode.STAGE)


func get_hunks_for(path: String) -> Array:
	var hunks: Array = staged_list.get_hunks_for(path)
	if hunks.is_empty():
		hunks = unstaged_list.get_hunks_for(path)
	return hunks
