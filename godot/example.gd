# Godot second client: example scene for real app usage.
#
# Unlike main.gd, which takes the plugin's proof defaults,
# this scene drives the session the way a real app would:
# the GcTrainSession node is configured in example.tscn
# (auto_load_workout off, ftp_watts 300, weight_kg 80 — values
# set in the scene file are applied before _ready, which is
# what makes them work for create-time params), and this
# script loads a workout from a file on disk and starts the
# session explicitly. It runs the same 65 s proof as main.gd
# (manual ERG 100 W at 30 s, cleared at 40 s) and checks its
# own result, so it stays verifiable headless:
#   godot --headless --path godot example.tscn
# prints "example-client: PASS" and exits 0 on success.
#
# Expected numbers: the workout is the same 60 s at 250 W /
# 60 s at 150 W as the demo, so NP is 218.6 and W'bal 20000
# as in the main run; TSS differs because FTP is 300 here,
# not 250 (~0.96 instead of 1.37) — which is also the proof
# that the scene's ftp_watts reached the session.
extends Node

const DURATION_SECS := 65.0
const MANUAL_AT_SECS := 30.0
const MANUAL_CLEAR_AT_SECS := 40.0
const WORKOUT_PATH := "res://workouts/demo.erg"

# Untyped on purpose: the GcTrainSession native class only
# exists once the GDExtension loads (see main.gd).
@onready var session = $GcTrainSession

var labels := {}
var last_printed_second := -1
var manual_sent := false
var manual_cleared := false
var finishing := false
var done := false


func _ready() -> void:
	# The node is Idle here: auto_load_workout is off, so
	# _ready created the session with the scene's ftp/weight
	# and loaded nothing. Prove the properties arrived, then
	# load from disk and start explicitly.
	print("configured ftp=%.0f W weight=%.0f kg"
		% [session.get_ftp_watts(), session.get_weight_kg()])
	var file := FileAccess.open(WORKOUT_PATH, FileAccess.READ)
	if file == null:
		print("example-client: FAIL (cannot open %s)" % WORKOUT_PATH)
		get_tree().quit(1)
		return
	var text: String = file.get_as_text()
	var status: int = session.load_workout_text(text, "demo.erg")
	if status != 0:
		print("example-client: FAIL (load_workout_text status %d)" % status)
		get_tree().quit(1)
		return
	status = session.start_session()
	if status != 0:
		print("example-client: FAIL (start_session status %d)" % status)
		get_tree().quit(1)
		return
	session.set_fixed_step_ms(250)
	session.step_changed.connect(_on_step_changed)
	session.state_changed.connect(_on_state_changed)
	_build_hud()


func _build_hud() -> void:
	var box := VBoxContainer.new()
	box.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(box)
	for key in ["watts", "target", "step", "state", "metrics"]:
		var label := Label.new()
		label.name = key
		box.add_child(label)
		labels[key] = label
	var manual_button := Button.new()
	manual_button.text = "Manual ERG 100 W"
	manual_button.pressed.connect(_on_manual_button)
	box.add_child(manual_button)
	var clear_button := Button.new()
	clear_button.text = "Clear manual"
	clear_button.pressed.connect(_on_clear_button)
	box.add_child(clear_button)
	var slope_button := Button.new()
	slope_button.text = "Slope 5%"
	slope_button.pressed.connect(_on_slope_button)
	box.add_child(slope_button)
	var pause_button := Button.new()
	pause_button.text = "Pause / Resume"
	pause_button.pressed.connect(_on_pause_button)
	box.add_child(pause_button)


func _process(_delta: float) -> void:
	if finishing:
		# One frame after stop_session: the node's _process has
		# refreshed, so the cached state is no longer stale
		# (same timing fact as the layer test).
		if session.get_state() == 5:  # Finished
			print("example-client: PASS")
			get_tree().quit(0)
		else:
			print("example-client: FAIL (not Finished after stop)")
			get_tree().quit(1)
		return
	if done:
		return
	var elapsed: float = session.get_elapsed_secs()
	if not manual_sent and elapsed >= MANUAL_AT_SECS:
		session.set_manual_erg(100.0)
		manual_sent = true
		print("manual ERG 100 W sent")
	if not manual_cleared and elapsed >= MANUAL_CLEAR_AT_SECS:
		session.clear_manual()
		manual_cleared = true
		print("manual cleared, workout resumes")
	_update_hud()
	var second := int(elapsed)
	if second != last_printed_second and second % 1 == 0:
		last_printed_second = second
		print("t=%ds watts=%.1f 3s=%.1f target=%.0f step=%d "
			% [second, session.get_watts(), session.get_watts_3s(),
				session.get_target_watts(), session.get_step_index()])
	if elapsed >= DURATION_SECS:
		done = true
		_finish()


func _update_hud() -> void:
	labels["watts"].text = ("watts=%.1f 3s=%.1f hr=%.0f cad=%.0f "
		% [session.get_watts(), session.get_watts_3s(),
			session.get_heartrate(), session.get_cadence()])
	labels["target"].text = ("target=%.0f W speed=%.1f kph "
		% [session.get_target_watts(), session.get_speed_kph()])
	labels["step"].text = ("step=%d in-step=%.1fs remaining=%.1fs"
		% [session.get_step_index(), session.get_time_in_step_secs(),
			session.get_time_remaining_secs()])
	labels["state"].text = ("state=%d power_valid=%s hr_valid=%s "
		% [session.get_state(), str(session.get_power_valid()),
			str(session.get_hr_valid())])
	labels["metrics"].text = ("NP=%.1f W'bal=%.0f TSS=%.2f"
		% [session.get_normalized_power(), session.get_wbal_joules(),
			session.get_tss_so_far()])


func _finish() -> void:
	session.stop_session()
	var np: float = session.get_normalized_power()
	var wbal: float = session.get_wbal_joules()
	var tss: float = session.get_tss_so_far()
	print("final: NP=%.1f Wbal=%.0f TSS=%.2f step=%d"
		% [np, wbal, tss, session.get_step_index()])
	var ok := true
	if not (np > 200.0 and np < 235.0):
		ok = false
		print("FAIL: NP %.1f outside 200-235" % np)
	if not (wbal > 19000.0 and wbal <= 20000.0):
		ok = false
		print("FAIL: W'bal %.0f outside 19000-20000" % wbal)
	if not (tss > 0.8 and tss < 1.1):
		ok = false
		print("FAIL: TSS %.2f outside 0.8-1.1 (FTP 300)" % tss)
	if session.get_step_index() != 1:
		ok = false
		print("FAIL: not in step 1 at 65 s")
	if not ok:
		print("example-client: FAIL")
		get_tree().quit(1)
		return
	finishing = true


func _on_step_changed(index: int) -> void:
	print("step=%d" % index)


func _on_state_changed(state: int) -> void:
	print("state=%d" % state)


func _on_manual_button() -> void:
	session.set_manual_erg(100.0)
	print("manual ERG 100 W sent (button)")


func _on_clear_button() -> void:
	session.clear_manual()
	print("manual cleared (button)")


func _on_slope_button() -> void:
	session.set_slope(5.0)
	print("slope 5% sent (button)")


func _on_pause_button() -> void:
	if session.get_state() == 2:  # Running
		session.pause_session()
	elif session.get_state() == 3:  # Paused
		session.resume_session()
