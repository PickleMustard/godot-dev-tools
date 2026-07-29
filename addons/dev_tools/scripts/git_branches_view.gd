@tool
extends MarginContainer

signal checkout_requested(name: String)
signal checkout_remote_requested(remote_ref_name: String)
signal create_branch_requested(name: String, checkout_after: bool)
signal merge_requested(source: String, target: String)

@onready var branches_list: ItemList = %BranchesList
@onready var checkout_selected_button: Button = %CheckoutSelectedButton
@onready var remote_branches_list: ItemList = %RemoteBranchesList
@onready var checkout_selected_remote_button: Button = %CheckoutSelectedRemoteButton
@onready var new_branch_name_edit: LineEdit = %NewBranchNameEdit
@onready var create_branch_checkout_check: CheckBox = %CreateBranchCheckoutCheck
@onready var create_branch_button: Button = %CreateBranchButton
@onready var source_branch_option: OptionButton = %SourceBranchOption
@onready var target_branch_option: OptionButton = %TargetBranchOption
@onready var create_pull_request_button: Button = %CreatePullRequestButton

var _branch_names: PackedStringArray = []
var _remote_branch_names: PackedStringArray = []


func _ready() -> void:
	checkout_selected_button.pressed.connect(_on_checkout_selected_pressed)
	checkout_selected_remote_button.pressed.connect(_on_checkout_selected_remote_pressed)
	create_branch_button.pressed.connect(_on_create_branch_pressed)
	create_pull_request_button.pressed.connect(_on_create_pull_request_pressed)


func load_branches(branches: Array) -> void:
	_branch_names.clear()
	_remote_branch_names.clear()
	branches_list.clear()
	remote_branches_list.clear()
	source_branch_option.clear()
	target_branch_option.clear()

	var current_idx: int = -1
	for b in branches:
		var d: Dictionary = b
		var branch_name: String = d.get("name", "")
		var is_current: bool = d.get("is_current", false)
		var is_remote: bool = d.get("is_remote", false)

		if is_remote:
			_remote_branch_names.append(branch_name)
			remote_branches_list.add_item(branch_name)
			continue

		_branch_names.append(branch_name)
		branches_list.add_item(branch_name + (" (current)" if is_current else ""))
		source_branch_option.add_item(branch_name)
		target_branch_option.add_item(branch_name)

		if is_current:
			current_idx = _branch_names.size() - 1

	if current_idx != -1:
		target_branch_option.select(current_idx)


func focus_new_branch_field() -> void:
	new_branch_name_edit.grab_focus()


func _on_checkout_selected_pressed() -> void:
	var selected := branches_list.get_selected_items()
	if selected.is_empty():
		return
	checkout_requested.emit(_branch_names[selected[0]])


func _on_checkout_selected_remote_pressed() -> void:
	var selected := remote_branches_list.get_selected_items()
	if selected.is_empty():
		return
	checkout_remote_requested.emit(_remote_branch_names[selected[0]])


func _on_create_branch_pressed() -> void:
	var branch_name := new_branch_name_edit.text.strip_edges()
	if branch_name.is_empty():
		return
	create_branch_requested.emit(branch_name, create_branch_checkout_check.button_pressed)
	new_branch_name_edit.text = ""


func _on_create_pull_request_pressed() -> void:
	if source_branch_option.item_count == 0 or target_branch_option.item_count == 0:
		return
	merge_requested.emit(
		source_branch_option.get_item_text(source_branch_option.selected),
		target_branch_option.get_item_text(target_branch_option.selected)
	)
