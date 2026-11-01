/*
 * Copyright (c) 2026 GoldenCheetah contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the Free Software Foundation, Inc., 51 Franklin Street,
 * Fifth Floor, Boston, MA  02110-1301  USA
 */

// Phase 5: TrainSessionAdapter (core/view split, TDD Phase 5).
// "TrainView becomes layout plus Qwt plus buttons plus
// observer adapter." This is the observer adapter half.
//
// A QObject that implements ISessionObserver and re-emits
// Qt signals, so Qt views can subscribe to a TrainSession
// with signals and slots, idiomatically, without the session
// knowing about Qt. Core stays free of Widgets (Phase 4),
// and the GUI stays idiomatic Qt (TDD 7.7: "If Qt signals
// are wanted, add a thin Qt adapter in the GUI layer that
// implements ITelemetryObserver and re-emits Qt signals").
//
// This is GUI-layer code, not core: it uses QObject, QTimer,
// and signals, and it is deliberately NOT added to
// util/check-core-includes.sh, which checks core headers
// for Qt. The adapter does not touch widgets, plots, or
// RealtimeData; a view that uses it still owns its own
// layout and rendering.
//
// Ownership: the adapter does NOT own the session. The
// caller creates the session, passes it by reference, and
// keeps it alive for the whole adapter lifetime. The adapter
// subscribes itself on construction and unsubscribes on
// destruction, so destroying the adapter never breaks the
// session, and destroying the session while the adapter
// lives is a caller error (documented, not silently handled).
//
// Timing: startTicking(interval_ms) starts a QTimer that
// calls tick() on that interval, for production. Tests do
// NOT use the timer: they call tick() directly, so adapter
// tests stay deterministic with no event loop and no sleep,
// exactly as core tests drive TrainSession::tick().
// stopTicking() stops the timer; tick() still works manually
// after that.
//
// Signals carry TelemetrySample and SessionState by value.
// Both are declared with Q_DECLARE_METATYPE below, so they
// can be used with QSignalSpy and with queued connections.
// A view that wants the sample on the UI thread should
// connect with Qt::QueuedConnection; the adapter itself
// emits on whatever thread tick() is called from, and does
// not force a thread policy on its callers.

#ifndef _GC_TrainSessionAdapter_h
#define _GC_TrainSessionAdapter_h 1

#include "Core/SessionState.h"
#include "Core/TelemetrySample.h"
#include "Core/TrainSession.h"
#include "Core/TrainerCommand.h"
#include "Core/Workout.h"

#include <QMetaType>
#include <QObject>
#include <QTimer>

Q_DECLARE_METATYPE(TelemetrySample)
Q_DECLARE_METATYPE(SessionState)

class TrainSessionAdapter : public QObject, public ISessionObserver {
    Q_OBJECT
public:
    explicit TrainSessionAdapter(TrainSession &session,
                                 QObject *parent = nullptr);
    ~TrainSessionAdapter() override;

    TrainSession &session() { return session_; }

    // ISessionObserver, re-emitted as Qt signals.
    void onTelemetry(const TelemetrySample &sample) override;
    void onSessionStateChanged(SessionState state) override;
    void onWorkoutStepChanged(int index) override;

public slots:
    void tick();
    void startTicking(int interval_ms = 250);
    void stopTicking();
    bool isTicking() const;

    // Pass-through intents, so a view can talk to the
    // adapter without holding the session directly.
    void startSession();
    void pauseSession();
    void resumeSession();
    void stopAndSaveSession();
    void lapSession();
    void loadWorkout(const Workout &workout);
    void setManualCommand(const TrainerCommand &command);
    void setSimGradient(double gradient_pct);

signals:
    void telemetryUpdated(const TelemetrySample &sample);
    void sessionStateChanged(SessionState state);
    void workoutStepChanged(int index);

private:
    TrainSession &session_;
    QTimer timer_;
};

#endif // _GC_TrainSessionAdapter_h
