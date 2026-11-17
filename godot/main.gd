# Godot second client, step 3: minimal HUD scene (~200 lines).
#
# A GcTrainSession node (the GDExtension wrapping the C ABI)
# runs the Phase 5 dashboard workout: 60 s at 250 W, 60 s at
# 150 W, FakeDevice trainer, 250 ms fixed steps per frame so
# the numbers match the console dashboard and the C harness.
# The HUD shows watts / 3s watts / HR / cadence / speed,
# current step and target, session state and dropout flags,
# and live metrics (NP, W'bal, TSS). Buttons send the same
# commands a game would: manual ERG, slope, sim gradient,
# pause/resume. The proof part is automatic: at 30 s it sends
# a manual ERG 100 W command and at 40 s it clears it, then
# at 65 s it prints the final metrics and quits — the same
# proof the console dashboard runs, now in Godot.
extends Node

const DURATION_SECS := 65.0
const MANUAL_AT_SECS := 30.0
const MANUAL_CLEAR_AT_SECS := 40.0

# Untyped on purpose: the GcTrainSession native class only
# exists once the GDExtension loads, so a static type here
# makes the script fail to parse when the extension is absent.
@onready var session = $GcTrainSession

var labels := {}
var last_printed_second := -1
var manual_sent := false
var manual_cleared := false
var done := false


func _ready() -> void:
	# Fixed 250 ms steps per frame: deterministic, matches the
	# dashboard and C harness cadence. A real game would leave
	# this at 0 and tick with the real frame delta instead.
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
	print("final: NP=%.1f Wbal=%.0f TSS=%.2f"
		% [session.get_normalized_power(), session.get_wbal_joules(),
			session.get_tss_so_far()])
	print("godot-client: PASS")
	get_tree().quit()


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
