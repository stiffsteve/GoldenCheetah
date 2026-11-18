# Godot second client: free-ride proof for the game.
#
# Unlike main.gd and example.gd, which ride a workout on the
# FakeDevice, this scene rides the VirtualRider with no
# workout loaded: the app drives the rider's watts in with
# set_virtual_watts and the course gradient in with
# set_sim_gradient, and the device reports speed from the
# physics model. It runs 40 s — 20 s flat at 200 W, then
# 20 s at 8% — and checks its own result, so it stays
# verifiable headless:
#   godot --headless --path godot res://freeride.tscn
# prints "freeride-client: PASS" and exits 0 on success.
#
# Expected numbers (physics pins): 200 W on the flat at
# 83 kg is 29.55 kph; at 8% it is 10.16 kph. Distance after
# 40 s is about 220 m. A flat that still produced speed, or
# a climb that didn't slow the rider, fails the run.
#
# The bounds are @export so the profile scene
# (freeride-profile.tscn) reuses this script with a heavier
# rider: 100 kg rider + 10 kg bike is 29.08 kph flat and
# 7.83 kph at 8% — the climb is where the weight shows, so
# its bounds are the discriminating check. The scene also
# sets expect_weight_kg / expect_height_cm /
# expect_bike_weight_kg, and _ready fails fast if the node
# properties do not round-trip those values. Height has no
# speed effect in the instant model (Cd is fixed), so the
# height check is a round-trip only.
extends Node

const DURATION_SECS := 40.0
const CLIMB_AT_SECS := 20.0
const VIRTUAL_WATTS := 200.0
const FLAT_GRADIENT := 0.0
const CLIMB_GRADIENT := 8.0

# Bounds and profile expectations, set by the scene that
# uses this script. Defaults are the 75 kg rider (83 kg
# total) in freeride.tscn.
@export var expect_flat_min := 20.0
@export var expect_flat_max := 35.0
@export var expect_climb_min := 5.0
@export var expect_climb_max := 15.0
@export var expect_dist_min := 150.0
@export var expect_dist_max := 300.0
@export var expect_weight_kg := 75.0
@export var expect_height_cm := 175.0
@export var expect_bike_weight_kg := 8.0

# Untyped on purpose: the GcTrainSession native class only
# exists once the GDExtension loads (see main.gd).
@onready var session = $GcTrainSession

var labels := {}
var last_printed_second := -1
var climbed := false
var flat_speed := 0.0
var climb_speed := 0.0
var finishing := false
var done := false


func _ready() -> void:
	# No workout: free ride. The node was configured in
	# freeride.tscn (auto_load_workout off, device_kind 1).
	session.set_fixed_step_ms(250)
	var status: int = session.set_virtual_watts(VIRTUAL_WATTS)
	if status != 0:
		print("freeride-client: FAIL (set_virtual_watts status %d)" % status)
		get_tree().quit(1)
		return
	session.set_sim_gradient(FLAT_GRADIENT)
	status = session.start_session()
	if status != 0:
		print("freeride-client: FAIL (start_session status %d)" % status)
		get_tree().quit(1)
		return
	# The scene's profile must have reached the node.
	if session.get_weight_kg() != expect_weight_kg:
		print("freeride-client: FAIL (weight_kg %.1f, expected %.1f)"
			% [session.get_weight_kg(), expect_weight_kg])
		get_tree().quit(1)
		return
	if session.get_height_cm() != expect_height_cm:
		print("freeride-client: FAIL (height_cm %.1f, expected %.1f)"
			% [session.get_height_cm(), expect_height_cm])
		get_tree().quit(1)
		return
	if session.get_bike_weight_kg() != expect_bike_weight_kg:
		print("freeride-client: FAIL (bike_weight_kg %.1f, expected %.1f)"
			% [session.get_bike_weight_kg(), expect_bike_weight_kg])
		get_tree().quit(1)
		return
	_build_hud()


func _build_hud() -> void:
	var box := VBoxContainer.new()
	box.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(box)
	for key in ["watts", "speed", "distance", "state"]:
		var label := Label.new()
		label.name = key
		box.add_child(label)
		labels[key] = label


func _process(_delta: float) -> void:
	if finishing:
		# One frame after stop_session: the node's _process has
		# refreshed, so the cached state is no longer stale
		# (same timing fact as the layer test).
		if session.get_state() == 5:  # Finished
			print("freeride-client: PASS")
			get_tree().quit(0)
		else:
			print("freeride-client: FAIL (not Finished after stop)")
			get_tree().quit(1)
		return
	if done:
		return
	var elapsed: float = session.get_elapsed_secs()
	if not climbed and elapsed >= CLIMB_AT_SECS:
		flat_speed = session.get_speed_kph()
		session.set_sim_gradient(CLIMB_GRADIENT)
		climbed = true
		print("gradient 8% at t=%ds (flat speed was %.1f kph)"
			% [int(elapsed), flat_speed])
	_update_hud()
	var second := int(elapsed)
	if second != last_printed_second and second % 1 == 0:
		last_printed_second = second
		print("t=%ds watts=%.1f speed=%.1f gradient=%.0f%% dist=%.0f m"
			% [second, session.get_watts(), session.get_speed_kph(),
				session.get_gradient_pct(), session.get_distance_m()])
	if elapsed >= DURATION_SECS:
		done = true
		_finish()


func _update_hud() -> void:
	labels["watts"].text = ("watts=%.1f 3s=%.1f"
		% [session.get_watts(), session.get_watts_3s()])
	labels["speed"].text = ("speed=%.1f kph gradient=%.0f%%"
		% [session.get_speed_kph(), session.get_gradient_pct()])
	labels["distance"].text = ("distance=%.0f m altitude=%.1f m"
		% [session.get_distance_m(), session.get_altitude_m()])
	labels["state"].text = ("state=%d power_valid=%s"
		% [session.get_state(), str(session.get_power_valid())])


func _finish() -> void:
	climb_speed = session.get_speed_kph()
	session.stop_session()
	var distance: float = session.get_distance_m()
	var watts: float = session.get_watts()
	print("final: flat=%.1f kph climb=%.1f kph dist=%.0f m watts=%.1f"
		% [flat_speed, climb_speed, distance, watts])
	var ok := true
	if not (flat_speed > expect_flat_min and flat_speed < expect_flat_max):
		ok = false
		print("FAIL: flat speed %.1f outside %.0f-%.0f kph"
			% [flat_speed, expect_flat_min, expect_flat_max])
	if not (climb_speed > expect_climb_min and climb_speed < expect_climb_max):
		ok = false
		print("FAIL: climb speed %.1f outside %.1f-%.1f kph"
			% [climb_speed, expect_climb_min, expect_climb_max])
	if not climb_speed < flat_speed:
		ok = false
		print("FAIL: climb did not slow the rider")
	if not (distance > expect_dist_min and distance < expect_dist_max):
		ok = false
		print("FAIL: distance %.0f outside %.0f-%.0f m"
			% [distance, expect_dist_min, expect_dist_max])
	if not (watts > 190.0 and watts < 210.0):
		ok = false
		print("FAIL: watts %.1f outside 190-210" % watts)
	if not ok:
		print("freeride-client: FAIL")
		get_tree().quit(1)
		return
	finishing = true
