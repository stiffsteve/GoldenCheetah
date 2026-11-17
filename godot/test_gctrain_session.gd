# Godot second client: automated headless test for GcTrainSession.
#
# Runs the same proof as the C harness (gc-session-c-test.c) and
# the HUD scene (main.gd) — the Phase 5 dashboard workout, 60 s
# at 250 W then 60 s at 150 W, FakeDevice trainer — but through
# the GDExtension node, asserting what the HUD and a game would
# actually read: getters, step/state signals, manual commands,
# pause/resume, and live metrics. No window, no GPU, no test
# framework: it extends SceneTree and drives one 250 ms fixed
# step per frame, the same cadence as the dashboard and the C
# harness, so the numbers are comparable.
#
# Run it with: ./scripts/godot-test.sh
# (or directly: godot --headless --path godot -s test_gctrain_session.gd)
# Exit code 0 and "gctrain-test: PASS" on success, 1 on any
# failed check. Bounds mirror the C harness: watts at 55 s
# above 200 W, NP 200-235, W'bal 19000-20000, TSS 1.0-1.7.
#
# The session is created with ClassDB.instantiate, not by
# naming the class, so this script still parses (and fails
# cleanly with "extension not loaded") when the GDExtension
# is absent — the same reason main.gd leaves its session
# variable untyped.
extends SceneTree

const STATE_RUNNING := 2
const STATE_PAUSED := 3
const STATE_FINISHED := 5
const STEP_MS := 250

var session = null
var failures: Array[String] = []
var checks := 0
var frames := 0
var step_changes: Array = []
var state_changes := []
var telemetry_updates := 0
var manual_sent := false
var manual_cleared := false
var paused_at_time_in_step := -1.0
var pause_checked := false
var resumed := false
var finishing := false
var done := false


func _process(_delta: float) -> bool:
	# MainLoop._process returning true quits the loop, so this
	# always returns false; the test exits via quit() in _report.
	# The finishing branch runs before the done guard: _finish
	# sets both, and the Finished check needs this extra frame.
	if finishing:
		# One frame after stop_session: the node's _process has
		# refreshed, so the cached state is no longer stale.
		_check(session.get_state() == STATE_FINISHED, "Finished after stop")
		_report()
		return false
	if done:
		return false
	if session == null:
		_setup()
		return false
	frames += 1
	var sim: float = frames * STEP_MS / 1000.0
	if frames == 1 and not _has_check("first-tick"):
		_check(session.get_target_watts() == 250.0,
			"target 250 W after first tick", "first-tick")
	if frames == 2 and not _has_check("second-tick"):
		_check(session.get_trainer_connected(),
			"trainer connected after device tick", "second-tick")
		_check(session.get_power_valid(),
			"power valid after device tick", "second-tick")
		_check(session.get_hr_valid(),
			"HR valid after device tick", "second-tick")
	_run_schedule(sim)
	return false


func _setup() -> void:
	session = ClassDB.instantiate("GcTrainSession")
	if session == null:
		_fail("GcTrainSession class not registered (extension not loaded?)")
		_finish()
		return
	root.add_child(session)
	session.set_fixed_step_ms(STEP_MS)
	session.step_changed.connect(_on_step_changed)
	session.state_changed.connect(_on_state_changed)
	session.telemetry_updated.connect(_on_telemetry_updated)
	# _ready runs on add_child: session created, workout loaded
	# and started. State and step come from direct C calls and
	# hold immediately. The target comes from the workout
	# player, so it is set after the first tick (frame 1);
	# the validity flags come from device samples, which only
	# exist after the FakeDevice has ticked once, so they are
	# checked on frame 2.
	_check(session.get_state() == STATE_RUNNING, "state is Running at start")
	_check(session.get_step_index() == 0, "step 0 at start")


func _run_schedule(sim: float) -> void:
	if not manual_sent and sim >= 30.0:
		session.set_manual_erg(100.0)
		manual_sent = true
		print("manual ERG 100 W sent")
	if manual_sent and not manual_cleared and sim >= 35.0 \
			and not _has_check("manual"):
		_check(session.get_target_watts() == 100.0,
			"manual 100 W target while override set", "manual")
	if manual_sent and not manual_cleared and sim >= 40.0:
		session.clear_manual()
		manual_cleared = true
		print("manual cleared, workout resumes")
	if manual_cleared and sim >= 45.0 and not _has_check("cleared"):
		_check(session.get_target_watts() == 250.0,
			"workout 250 W target after manual cleared", "cleared")
	if sim >= 55.0 and not _has_check("converged"):
		_check(session.get_watts() > 200.0,
			"watts converge near 250 W late in step 0", "converged")
	if sim >= 60.0 and not _has_check("step1"):
		_check(session.get_step_index() == 1, "step 1 at 60 s", "step1")
		_check(session.get_target_watts() == 150.0,
			"step 1 target is 150 W", "step1")
		_check(step_changes.has(1), "step_changed signal fired for step 1",
			"step1")
	if sim >= 61.0 and paused_at_time_in_step < 0.0:
		session.pause_session()
		paused_at_time_in_step = session.get_time_in_step_secs()
		print("paused at time_in_step=%.1fs" % paused_at_time_in_step)
	if sim >= 62.0 and paused_at_time_in_step >= 0.0 and not pause_checked:
		pause_checked = true
		_check(session.get_state() == STATE_PAUSED, "state Paused while paused")
		_check(session.get_time_in_step_secs() == paused_at_time_in_step,
			"time in step frozen while paused")
		session.resume_session()
		resumed = true
		print("resumed")
	if resumed and sim >= 63.0 and not _has_check("resumed"):
		_check(session.get_state() == STATE_RUNNING,
			"state Running after resume", "resumed")
	if sim >= 65.0:
		_finish()


func _finish() -> void:
	if done:
		return
	done = true
	if session == null:
		# Setup failed (extension not loaded): nothing to stop.
		_report()
		return
	var np: float = session.get_normalized_power()
	var wbal: float = session.get_wbal_joules()
	var tss: float = session.get_tss_so_far()
	var remaining: float = session.get_time_remaining_secs()
	print("final: NP=%.1f Wbal=%.0f TSS=%.2f remaining=%.1fs "
		% [np, wbal, tss, remaining])
	_check(np > 200.0 and np < 235.0, "NP near dashboard run (218.8)")
	_check(wbal > 19000.0 and wbal <= 20000.0, "W'bal near 20000")
	_check(tss > 1.0 and tss < 1.7, "TSS near dashboard run (1.37)")
	_check(remaining > 50.0 and remaining < 60.0,
		"time remaining in step 1 is ~55 s at 65 s elapsed")
	_check(session.get_watts_3s() > 0.0, "3 s watts live")
	_check(session.get_heartrate() > 0.0, "heart rate live")
	_check(telemetry_updates > 0, "telemetry_updated signal fired")
	session.stop_session()
	# State is only refreshed by the node's next _process, so
	# the Finished check waits one frame (see _process).
	finishing = true


func _report() -> void:
	if failures.is_empty():
		print("gctrain-test: PASS (%d checks)" % checks)
		quit(0)
	else:
		print("gctrain-test: FAIL (%d failed of %d checks)"
			% [failures.size(), checks])
		quit(1)


func _check(passed: bool, label: String, tag: String = "") -> void:
	checks += 1
	if tag != "":
		_seen_tags[tag] = true
	if passed:
		print("  ok: %s" % label)
	else:
		failures.append(label)
		print("  FAIL: %s" % label)


var _seen_tags := {}


func _has_check(tag: String) -> bool:
	return _seen_tags.has(tag)


func _fail(label: String) -> void:
	checks += 1
	failures.append(label)
	print("  FAIL: %s" % label)


func _on_step_changed(index: int) -> void:
	step_changes.append(index)
	print("step=%d" % index)


func _on_state_changed(state: int) -> void:
	state_changes.append(state)
	print("state=%d" % state)


func _on_telemetry_updated() -> void:
	telemetry_updates += 1
