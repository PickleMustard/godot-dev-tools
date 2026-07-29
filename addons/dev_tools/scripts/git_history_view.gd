@tool
extends Control

const ROW_HEIGHT: float = 24.0
const LANE_WIDTH: float = 16.0
const DOT_RADIUS: float = 4.0
const TEXT_MARGIN: float = 12.0
const LANE_COLORS: Array[Color] = [
	Color(0.36, 0.68, 0.94),
	Color(0.94, 0.55, 0.36),
	Color(0.55, 0.84, 0.44),
	Color(0.84, 0.44, 0.78),
	Color(0.94, 0.85, 0.36),
	Color(0.44, 0.84, 0.84),
]

var commits: Array = []
var max_lane: int = 0


func load_history(history: Array) -> void:
	commits = history
	max_lane = 0
	for c in commits:
		var d: Dictionary = c
		max_lane = max(max_lane, int(d.get("lane", 0)))
		for pl in d.get("parent_lanes", []):
			max_lane = max(max_lane, int(pl))

	custom_minimum_size = Vector2(0, commits.size() * ROW_HEIGHT)
	queue_redraw()


func _lane_color(lane: int) -> Color:
	return LANE_COLORS[lane % LANE_COLORS.size()]


func _lane_x(lane: int) -> float:
	return LANE_WIDTH * 0.5 + lane * LANE_WIDTH


func _draw() -> void:
	if commits.is_empty():
		return

	var font: Font = ThemeDB.fallback_font
	var font_size: int = ThemeDB.fallback_font_size
	if Engine.is_editor_hint():
		var editor_theme := EditorInterface.get_editor_theme()
		if editor_theme and editor_theme.has_font("source", "EditorFonts"):
			font = editor_theme.get_font("source", "EditorFonts")

	var text_x := _lane_x(max_lane) + LANE_WIDTH + TEXT_MARGIN

	for i in range(commits.size()):
		var c: Dictionary = commits[i]
		var lane: int = c.get("lane", 0)
		var y := i * ROW_HEIGHT + ROW_HEIGHT * 0.5
		var x := _lane_x(lane)
		var color := _lane_color(lane)

		for pl in c.get("parent_lanes", []):
			var pl_i: int = pl
			if pl_i < 0:
				continue
			var px := _lane_x(pl_i)
			var py := y + ROW_HEIGHT
			draw_line(Vector2(x, y), Vector2(px, py), color, 2.0, true)

		draw_circle(Vector2(x, y), DOT_RADIUS, color)

		var refs: Array = c.get("refs", [])
		var ref_names: Array = []
		for r in refs:
			var rd: Dictionary = r
			ref_names.append(rd.get("name", ""))
		var ref_text := ""
		if not ref_names.is_empty():
			ref_text = " (%s)" % ", ".join(ref_names)
		var label_text := "%s %s%s" % [c.get("short_hash", ""), c.get("summary", ""), ref_text]
		draw_string(font, Vector2(text_x, y + font_size * 0.35), label_text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size)
