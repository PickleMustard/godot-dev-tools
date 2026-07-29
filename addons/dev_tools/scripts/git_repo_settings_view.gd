@tool
extends MarginContainer

signal remote_url_save_requested(url: String)
signal ssh_key_save_requested(path: String, passphrase: String)
signal identity_save_requested(user_name: String, user_email: String)
signal remote_add_requested(remote_name: String, url: String)
signal remote_remove_requested(remote_name: String)
signal config_set_requested(key: String, value: String)

@onready var remote_url_edit: LineEdit = %RemoteUrlEdit
@onready var remote_url_save_button: Button = %RemoteUrlSaveButton

@onready var ssh_key_path_edit: LineEdit = %SshKeyPathEdit
@onready var ssh_key_browse_button: Button = %SshKeyBrowseButton
@onready var ssh_key_file_dialog: FileDialog = %SshKeyFileDialog
@onready var ssh_passphrase_edit: LineEdit = %SshPassphraseEdit
@onready var ssh_key_save_button: Button = %SshKeySaveButton

@onready var user_name_edit: LineEdit = %UserNameEdit
@onready var user_email_edit: LineEdit = %UserEmailEdit
@onready var identity_save_button: Button = %IdentitySaveButton

@onready var remotes_list: ItemList = %RemotesList
@onready var new_remote_name_edit: LineEdit = %NewRemoteNameEdit
@onready var new_remote_url_edit: LineEdit = %NewRemoteUrlEdit
@onready var remote_add_button: Button = %RemoteAddButton
@onready var remote_remove_button: Button = %RemoteRemoveButton

@onready var core_autocrlf_option: OptionButton = %CoreAutocrlfOption
@onready var core_autocrlf_save_button: Button = %CoreAutocrlfSaveButton
@onready var core_filemode_check: CheckBox = %CoreFilemodeCheck
@onready var core_filemode_save_button: Button = %CoreFilemodeSaveButton
@onready var pull_rebase_check: CheckBox = %PullRebaseCheck
@onready var pull_rebase_save_button: Button = %PullRebaseSaveButton
@onready var push_default_option: OptionButton = %PushDefaultOption
@onready var push_default_save_button: Button = %PushDefaultSaveButton
@onready var init_default_branch_edit: LineEdit = %InitDefaultBranchEdit
@onready var init_default_branch_save_button: Button = %InitDefaultBranchSaveButton

@onready var max_locks_spinbox: SpinBox = %MaxLocksSpinBox
@onready var max_locks_save_button: Button = %MaxLocksSaveButton
@onready var lock_poll_interval_spinbox: SpinBox = %LockPollIntervalSpinBox
@onready var lock_poll_interval_save_button: Button = %LockPollIntervalSaveButton

@onready var custom_config_key_edit: LineEdit = %CustomConfigKeyEdit
@onready var custom_config_value_edit: LineEdit = %CustomConfigValueEdit
@onready var custom_config_set_button: Button = %CustomConfigSetButton

var _remote_names: PackedStringArray = []

const AUTOCRLF_VALUES := ["false", "true", "input"]
const PUSH_DEFAULT_VALUES := ["simple", "matching", "upstream", "current", "nothing"]


func _ready() -> void:
	remote_url_save_button.pressed.connect(_on_remote_url_save_pressed)

	ssh_key_browse_button.pressed.connect(_on_ssh_key_browse_pressed)
	ssh_key_file_dialog.file_selected.connect(_on_ssh_key_file_selected)
	ssh_key_save_button.pressed.connect(_on_ssh_key_save_pressed)

	identity_save_button.pressed.connect(_on_identity_save_pressed)

	remote_add_button.pressed.connect(_on_remote_add_pressed)
	remote_remove_button.pressed.connect(_on_remote_remove_pressed)

	core_autocrlf_save_button.pressed.connect(_on_core_autocrlf_save_pressed)
	core_filemode_save_button.pressed.connect(_on_core_filemode_save_pressed)
	pull_rebase_save_button.pressed.connect(_on_pull_rebase_save_pressed)
	push_default_save_button.pressed.connect(_on_push_default_save_pressed)
	init_default_branch_save_button.pressed.connect(_on_init_default_branch_save_pressed)

	max_locks_save_button.pressed.connect(_on_max_locks_save_pressed)
	lock_poll_interval_save_button.pressed.connect(_on_lock_poll_interval_save_pressed)

	custom_config_set_button.pressed.connect(_on_custom_config_set_pressed)


func load_settings(data: Dictionary) -> void:
	remote_url_edit.text = data.get("remote_url", "")
	ssh_key_path_edit.text = data.get("ssh_key_path", "")

	user_name_edit.text = data.get("user_name", "")
	user_email_edit.text = data.get("user_email", "")

	_remote_names.clear()
	remotes_list.clear()
	for remote in data.get("remotes", []):
		var d: Dictionary = remote
		var remote_name: String = d.get("name", "")
		var url: String = d.get("url", "")
		_remote_names.append(remote_name)
		remotes_list.add_item("%s  (%s)" % [remote_name, url])

	_select_option_value(core_autocrlf_option, AUTOCRLF_VALUES, data.get("core_autocrlf", ""))
	core_filemode_check.button_pressed = data.get("core_filemode", "") == "true"
	pull_rebase_check.button_pressed = data.get("pull_rebase", "") == "true"
	_select_option_value(push_default_option, PUSH_DEFAULT_VALUES, data.get("push_default", ""))
	init_default_branch_edit.text = data.get("init_default_branch", "")

	var max_locks: String = data.get("lfs_max_locks", "")
	if max_locks.is_valid_int():
		max_locks_spinbox.value = max_locks.to_int()
	var lock_poll_interval: String = data.get("lfs_lock_poll_interval_sec", "")
	if lock_poll_interval.is_valid_float():
		lock_poll_interval_spinbox.value = lock_poll_interval.to_float()


func _select_option_value(option: OptionButton, values: Array, current: String) -> void:
	var idx := values.find(current)
	option.select(maxi(idx, 0))


func _on_remote_url_save_pressed() -> void:
	var url := remote_url_edit.text.strip_edges()
	if url.is_empty():
		return
	remote_url_save_requested.emit(url)


func _on_ssh_key_browse_pressed() -> void:
	ssh_key_file_dialog.popup_centered_ratio(0.6)


func _on_ssh_key_file_selected(path: String) -> void:
	ssh_key_path_edit.text = path


func _on_ssh_key_save_pressed() -> void:
	var path := ssh_key_path_edit.text.strip_edges()
	if path.is_empty():
		return
	ssh_key_save_requested.emit(path, ssh_passphrase_edit.text)


func _on_identity_save_pressed() -> void:
	var user_name := user_name_edit.text.strip_edges()
	var user_email := user_email_edit.text.strip_edges()
	if user_name.is_empty() or user_email.is_empty():
		return
	identity_save_requested.emit(user_name, user_email)


func _on_remote_add_pressed() -> void:
	var remote_name := new_remote_name_edit.text.strip_edges()
	var url := new_remote_url_edit.text.strip_edges()
	if remote_name.is_empty() or url.is_empty():
		return
	remote_add_requested.emit(remote_name, url)
	new_remote_name_edit.text = ""
	new_remote_url_edit.text = ""


func _on_remote_remove_pressed() -> void:
	var selected := remotes_list.get_selected_items()
	if selected.is_empty():
		return
	remote_remove_requested.emit(_remote_names[selected[0]])


func _on_core_autocrlf_save_pressed() -> void:
	config_set_requested.emit("core.autocrlf", AUTOCRLF_VALUES[core_autocrlf_option.selected])


func _on_core_filemode_save_pressed() -> void:
	config_set_requested.emit("core.filemode", "true" if core_filemode_check.button_pressed else "false")


func _on_pull_rebase_save_pressed() -> void:
	config_set_requested.emit("pull.rebase", "true" if pull_rebase_check.button_pressed else "false")


func _on_push_default_save_pressed() -> void:
	config_set_requested.emit("push.default", PUSH_DEFAULT_VALUES[push_default_option.selected])


func _on_init_default_branch_save_pressed() -> void:
	var branch_name := init_default_branch_edit.text.strip_edges()
	if branch_name.is_empty():
		return
	config_set_requested.emit("init.defaultBranch", branch_name)


func _on_max_locks_save_pressed() -> void:
	config_set_requested.emit("devtools.lfs.maxlocks", str(int(max_locks_spinbox.value)))


func _on_lock_poll_interval_save_pressed() -> void:
	config_set_requested.emit("devtools.lfs.lockpollintervalsec", str(int(lock_poll_interval_spinbox.value)))


func _on_custom_config_set_pressed() -> void:
	var key := custom_config_key_edit.text.strip_edges()
	var value := custom_config_value_edit.text.strip_edges()
	if key.is_empty():
		return
	config_set_requested.emit(key, value)
	custom_config_key_edit.text = ""
	custom_config_value_edit.text = ""
