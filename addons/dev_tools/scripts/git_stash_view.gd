@tool
extends MarginContainer

signal stash_apply_requested(index: int, pop: bool)
signal stash_drop_requested(index: int)

@onready var stash_list: ItemList = %StashList
@onready var apply_button: Button = %StashApplyButton
@onready var pop_button: Button = %StashPopButton
@onready var drop_button: Button = %StashDropButton

var _stash_indices: PackedInt32Array = []


func _ready() -> void:
	apply_button.pressed.connect(_on_apply_pressed)
	pop_button.pressed.connect(_on_pop_pressed)
	drop_button.pressed.connect(_on_drop_pressed)


func load_stashes(stashes: Array) -> void:
	stash_list.clear()
	_stash_indices.clear()
	for s in stashes:
		var d: Dictionary = s
		stash_list.add_item("stash@{%d}: %s" % [d.get("index", 0), d.get("message", "")])
		_stash_indices.append(d.get("index", 0))

	var has_stashes := not stashes.is_empty()
	apply_button.disabled = not has_stashes
	pop_button.disabled = not has_stashes
	drop_button.disabled = not has_stashes


func _selected_index() -> int:
	var selected := stash_list.get_selected_items()
	if selected.is_empty():
		return -1
	return _stash_indices[selected[0]]


func _on_apply_pressed() -> void:
	var index := _selected_index()
	if index == -1:
		return
	stash_apply_requested.emit(index, false)


func _on_pop_pressed() -> void:
	var index := _selected_index()
	if index == -1:
		return
	stash_apply_requested.emit(index, true)


func _on_drop_pressed() -> void:
	var index := _selected_index()
	if index == -1:
		return
	stash_drop_requested.emit(index)
