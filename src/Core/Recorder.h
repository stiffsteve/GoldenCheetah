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

// Phase 4: Recorder (core/view split, TDD section 7.8).
// "Recorder is a sink. File writer, in-memory test recorder,
// and null recorder all implement the same interface."
//
//   IRecorder: start(wallclock_ms), onSample(sample),
//              lap(monotonic_ms), finish().
//
// The FIT writer is an IRecorder implementation in the IO
// layer, and it must not depend on views. It is NOT here:
// see the Phase 4 plan doc, decision 1, for why FIT
// conversion is a separate, gated IO step with legacy-output
// validation, and why Phase 4 instead ships a CSV recorder
// whose columns match the fields a RideFile point carries.
//
// Ownership: the session holds an IRecorder* but does not
// own it. The caller owns the recorder and must keep it alive
// for the whole session, and call finish() (via
// TrainSession::stopAndSave, or directly) before destroying it.
//
// CsvRecorder details:
//  - Columns: monotonic_ms, wallclock_ms, watts, watts_3s,
//    heartrate, cadence, speed_kph, distance_m, altitude_m,
//    gradient_pct, target_watts, power_valid, hr_valid,
//    lap_index. lap_index increments on lap(), starting at 0,
//    so a lap boundary is visible in the file without a
//    separate lap file.
//  - The file is opened on start() and closed on finish().
//    Samples before start() or after finish() are dropped.
//  - The TDD interface returns void, so open failure cannot
//    be thrown through it; CsvRecorder exposes ok(), which is
//    false if the file could not be opened. A session using
//    a failed CsvRecorder still runs (its in-memory state is
//    unaffected); the caller must check ok() after start().
//  - CSV is a plain-text, std-only file format. It is not a
//    FIT file and must not be presented as one.
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// Header-only, like the other core types.

#ifndef _GC_Recorder_h
#define _GC_Recorder_h 1

#include "TelemetrySample.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

class IRecorder {
public:
    virtual ~IRecorder() = default;
    virtual void start(int64_t wallclock_ms) = 0;
    virtual void onSample(const TelemetrySample &sample) = 0;
    virtual void lap(int64_t monotonic_ms) = 0;
    virtual void finish() = 0;
};

class NullRecorder : public IRecorder {
public:
    void start(int64_t) override {}
    void onSample(const TelemetrySample &) override {}
    void lap(int64_t) override {}
    void finish() override {}
};

class InMemoryRecorder : public IRecorder {
public:
    void start(int64_t wallclock_ms) override {
        started_ = true;
        finished_ = false;
        start_wallclock_ms_ = wallclock_ms;
        samples_.clear();
        laps_.clear();
    }
    void onSample(const TelemetrySample &sample) override {
        if (started_ && !finished_) {
            samples_.push_back(sample);
        }
    }
    void lap(int64_t monotonic_ms) override {
        if (started_ && !finished_) {
            laps_.push_back(monotonic_ms);
        }
    }
    void finish() override { finished_ = true; }

    bool started() const { return started_; }
    bool finished() const { return finished_; }
    int64_t startWallclockMs() const { return start_wallclock_ms_; }
    const std::vector<TelemetrySample> &samples() const { return samples_; }
    const std::vector<int64_t> &laps() const { return laps_; }

private:
    bool started_ = false;
    bool finished_ = false;
    int64_t start_wallclock_ms_ = 0;
    std::vector<TelemetrySample> samples_;
    std::vector<int64_t> laps_;
};

class CsvRecorder : public IRecorder {
public:
    explicit CsvRecorder(const std::string &path) : path_(path) {}

    void start(int64_t) override {
        file_.open(path_);
        ok_ = file_.is_open();
        lap_index_ = 0;
        if (ok_) {
            file_ << "monotonic_ms,wallclock_ms,watts,watts_3s,"
                     "heartrate,cadence,speed_kph,distance_m,"
                     "altitude_m,gradient_pct,target_watts,"
                     "power_valid,hr_valid,lap_index\n";
        }
    }
    void onSample(const TelemetrySample &sample) override {
        if (!ok_ || !file_.is_open()) {
            return;
        }
        file_ << sample.monotonic_ms << ',' << sample.wallclock_ms << ','
              << sample.watts << ',' << sample.watts_3s << ','
              << sample.heartrate << ',' << sample.cadence << ','
              << sample.speed_kph << ',' << sample.distance_m << ','
              << sample.altitude_m << ',' << sample.gradient_pct << ','
              << sample.target_watts << ','
              << (sample.power_valid ? 1 : 0) << ','
              << (sample.hr_valid ? 1 : 0) << ',' << lap_index_ << '\n';
    }
    void lap(int64_t) override { ++lap_index_; }
    void finish() override {
        if (file_.is_open()) {
            file_.close();
        }
    }

    bool ok() const { return ok_; }
    const std::string &path() const { return path_; }

private:
    std::string path_;
    std::ofstream file_;
    bool ok_ = false;
    int lap_index_ = 0;
};

#endif // _GC_Recorder_h
