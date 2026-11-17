/* Godot second client, step 1: C ABI harness test.
 *
 * A plain C program — no C++, no Qt, no Godot — that exercises
 * the C ABI wrapper (GcSessionC.h) only. It is the proof that
 * the wrapper is usable from C before any Godot code exists.
 *
 * Build (from the repo root; the C++ TU instantiates the
 * header's inline functions, the C TU only calls them):
 *   g++ -std=c++17 -I src -c src/Core/gc-session-c-impl.cpp \
 *       -o /tmp/gc-session-c-impl.o
 *   gcc -std=c11 -I src -c src/Core/gc-session-c-test.c \
 *       -o /tmp/gc-session-c-test.o
 *   g++ /tmp/gc-session-c-impl.o /tmp/gc-session-c-test.o \
 *       -o /tmp/gc-session-c-test
 *   /tmp/gc-session-c-test
 *
 * The workout is the Phase 5 dashboard workout (60 s at 250 W,
 * 60 s at 150 W), run 65 s at 250 ms ticks with a manual ERG
 * 100 W command at 30 s cleared at 40 s, so its numbers are
 * comparable with the console dashboard run (NP 218.8,
 * W'bal 20000, TSS 1.37).
 */

#include "Core/GcSessionC.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL: %s (line %d)\n", msg, __LINE__);                  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static const char *kErgText =
    "[COURSE HEADER]\n"
    "MINUTES  WATTS\n"
    "[END COURSE HEADER]\n"
    "[COURSE DATA]\n"
    "0.0\t250\n"
    "1.0\t250\n"
    "1.0\t150\n"
    "2.0\t150\n";

int main(void) {
    /* Null-handle behaviour: error codes, never a crash. */
    CHECK(gc_session_poll_telemetry(NULL, NULL) == GC_ERR_NULL,
          "poll null handle");
    CHECK(gc_session_tick(NULL, 250) == GC_ERR_NULL, "tick null handle");
    CHECK(gc_session_state(NULL) == GC_STATE_ERROR, "state null handle");
    CHECK(gc_session_current_step(NULL) == -1, "step null handle");
    CHECK(gc_session_target_watts(NULL) == 0, "target null handle");
    gc_session_destroy(NULL); /* must not crash */

    gc_session_t *session = gc_session_create(250, 75);
    CHECK(session != NULL, "create returns a session");
    if (session == NULL) {
        return 1;
    }
    CHECK(gc_session_state(session) == GC_STATE_IDLE, "starts Idle");
    CHECK(gc_session_poll_telemetry(session, NULL) == GC_ERR_NULL,
          "poll null out");
    CHECK(gc_session_tick(session, -1) == GC_ERR_STATE,
          "negative dt is a state error");
    CHECK(gc_session_load_workout_text(session, "", "empty.erg") ==
              GC_ERR_PARSE,
          "empty workout text is a parse error");
    CHECK(gc_session_load_workout_text(session, "[COURSE DATA]\nhello\n",
                                       "bad.erg") == GC_ERR_PARSE,
          "bad workout text is a parse error");
    CHECK(gc_session_load_workout_text(session, kErgText, "test.erg") ==
              GC_OK,
          "ERG text loads");
    CHECK(gc_session_state(session) == GC_STATE_READY,
          "Ready after load");
    CHECK(gc_session_start(session) == GC_OK, "start ok");
    CHECK(gc_session_state(session) == GC_STATE_RUNNING,
          "Running after start");
    CHECK(gc_session_load_workout_text(session, kErgText, "test.erg") ==
              GC_ERR_STATE,
          "load after start is a state error");

    int saw_step0 = 0;
    int saw_step1 = 0;
    int manual_sent = 0;
    int manual_cleared = 0;
    double watts_at_55s = 0;
    double target_at_35s = 0;
    double target_at_45s = 0;
    gc_telemetry_t sample;
    memset(&sample, 0, sizeof(sample));

    for (int64_t elapsed_ms = 0; elapsed_ms < 65000; elapsed_ms += 250) {
        CHECK(gc_session_tick(session, 250) == GC_OK, "tick ok");
        CHECK(gc_session_poll_telemetry(session, &sample) == GC_OK,
              "poll ok");
        int step = gc_session_current_step(session);
        if (step == 0) {
            saw_step0 = 1;
        }
        if (step == 1) {
            saw_step1 = 1;
        }
        if (elapsed_ms == 55000) {
            watts_at_55s = sample.watts;
        }
        if (elapsed_ms == 35000) {
            target_at_35s = sample.target_watts;
        }
        if (elapsed_ms == 45000) {
            target_at_45s = sample.target_watts;
        }
        if (!manual_sent && elapsed_ms >= 30000) {
            CHECK(gc_session_set_manual_erg(session, 100) == GC_OK,
                  "manual ERG set");
            manual_sent = 1;
        }
        if (!manual_cleared && elapsed_ms >= 40000) {
            CHECK(gc_session_clear_manual(session) == GC_OK,
                  "manual cleared");
            manual_cleared = 1;
        }
    }
    /* The last tick is at 65 s, step 1, target 150 W. */
    CHECK(saw_step0, "saw step 0");
    CHECK(saw_step1, "saw step 1");
    CHECK(manual_sent && manual_cleared, "manual sent and cleared");
    CHECK(watts_at_55s > 200, "watts converge near 250 W late in step 0");
    CHECK(target_at_35s == 100, "manual 100 W target while override set");
    CHECK(target_at_45s == 250, "workout 250 W target after manual cleared");
    CHECK(sample.target_watts == 150, "step 1 target is 150 W");
    CHECK(gc_session_time_remaining_secs(session) > 50 &&
              gc_session_time_remaining_secs(session) < 60,
          "time remaining in step 1 is ~55 s at 65 s elapsed");

    gc_live_metrics_t metrics;
    memset(&metrics, 0, sizeof(metrics));
    CHECK(gc_session_live_metrics(session, &metrics) == GC_OK,
          "metrics ok");
    printf("final: NP=%.1f Wbal=%.0f TSS=%.2f watts55=%.1f\n",
           metrics.normalized_power, metrics.wbal_joules,
           metrics.tss_so_far, watts_at_55s);
    CHECK(metrics.normalized_power > 200 &&
              metrics.normalized_power < 235,
          "NP near dashboard run (218.8)");
    CHECK(metrics.wbal_joules > 19000 && metrics.wbal_joules <= 20000,
          "W'bal near 20000");
    CHECK(metrics.tss_so_far > 1.0 && metrics.tss_so_far < 1.7,
          "TSS near dashboard run (1.37)");

    CHECK(gc_session_stop_and_save(session) == GC_OK, "stop ok");
    CHECK(gc_session_state(session) == GC_STATE_FINISHED,
          "Finished after stop");
    gc_session_destroy(session);

    if (failures == 0) {
        printf("gc-session-c-test: PASS\n");
        return 0;
    }
    printf("gc-session-c-test: FAIL: %d failure(s)\n", failures);
    return 1;
}
