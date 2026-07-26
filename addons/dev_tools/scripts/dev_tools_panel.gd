@tool
extends Control

@onready var tool_tabs: TabContainer = %ToolTabs


func add_tool_tab(title: String, control: Control) -> void:
	control.name = title
	tool_tabs.add_child(control)
