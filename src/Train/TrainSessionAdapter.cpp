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

#include "TrainSessionAdapter.h"

TrainSessionAdapter::TrainSessionAdapter(TrainSession &session,
                                         QObject *parent)
    : QObject(parent), session_(session) {
    session_.subscribe(this);
    connect(&timer_, &QTimer::timeout, this,
            &TrainSessionAdapter::tick);
}

TrainSessionAdapter::~TrainSessionAdapter() {
    session_.unsubscribe(this);
}

void TrainSessionAdapter::onTelemetry(const TelemetrySample &sample) {
    emit telemetryUpdated(sample);
}

void TrainSessionAdapter::onSessionStateChanged(SessionState state) {
    emit sessionStateChanged(state);
}

void TrainSessionAdapter::onWorkoutStepChanged(int index) {
    emit workoutStepChanged(index);
}

void TrainSessionAdapter::tick() { session_.tick(); }

void TrainSessionAdapter::startTicking(int interval_ms) {
    timer_.start(interval_ms);
}

void TrainSessionAdapter::stopTicking() { timer_.stop(); }

bool TrainSessionAdapter::isTicking() const { return timer_.isActive(); }

void TrainSessionAdapter::startSession() { session_.start(); }
void TrainSessionAdapter::pauseSession() { session_.pause(); }
void TrainSessionAdapter::resumeSession() { session_.resume(); }
void TrainSessionAdapter::stopAndSaveSession() { session_.stopAndSave(); }
void TrainSessionAdapter::lapSession() { session_.lap(); }
void TrainSessionAdapter::loadWorkout(const Workout &workout) {
    session_.loadWorkout(workout);
}
void TrainSessionAdapter::setManualCommand(const TrainerCommand &command) {
    session_.setManualCommand(command);
}
void TrainSessionAdapter::setSimGradient(double gradient_pct) {
    session_.setSimGradient(gradient_pct);
}
