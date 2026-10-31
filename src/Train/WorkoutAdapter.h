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

// Phase 1 step 3: Train adapter, legacy ErgFile -> core Workout.
// Converts an already-parsed legacy ErgFile to the Phase 1 Workout
// type, via ErgFile::Sections() and the Phase 0
// workoutStepFromSection() mapping. This is the "GUI still uses
// same workout logic via adapter" path from the TDD: the GUI keeps
// parsing with ErgFile, and this adapter is what a future GUI
// switch to WorkoutPlayer would call. No GUI code calls it yet
// in Phase 1 (see doc/core-view-split/phase1-plan.md for why the
// switch itself is deferred).
//
// This header lives in Train, not Core, on purpose — the same
// boundary pattern as TelemetryAdapter.h. ErgFile.h pulls in
// Context.h, Zones.h, Settings.h, <QtGui>, and <QTextEdit>, so a
// mapping that takes ErgFile cannot live in src/Core. This adapter
// is therefore NOT checked by util/check-core-includes.sh.
//
// Differences from the core parser (WorkoutParser.h), documented
// so they are not mistaken for bugs:
//   - Legacy conversion: ErgFile already converted MRC percent to
//     absolute watts using the athlete's CP at parse time, and
//     scaled ERG watts by CP/FTP when a header FTP was set. So
//     steps produced here are absolute-watt steps, never
//     target_pct_ftp steps, even for an originally-MRC file.
//     The core parser preserves percent; this adapter cannot
//     recover it, because ErgFile does not retain it.
//   - CRS/slope files: ErgFile::Sections() for a CRS file is
//     distance-based, not time-based, so its "durations" are not
//     seconds and must not become WorkoutStep durations. Such
//     files therefore return a Workout with name/description but
//     no steps, rather than a workout with wrong durations.
//     Callers must check steps.empty() and fall back to the
//     legacy query adapter for slope routes.
//   - Sections() quirk: if the first point is after time 0, the
//     legacy Sections() produces an initial section AND a
//     following section whose duration is measured from 0, not
//     from the first point, so the two overlap. This adapter
//     passes Sections() through unchanged (it is the legacy
//     behaviour), so a workout converted this way can have a
//     total duration longer than the file's duration in that
//     rare edge case (first point after 0 with a same-time
//     duplicate). The core parser does not have this quirk.
//     Most real files start at 0.0, where the quirk does not occur.
//
// Testing limitation, stated plainly: this adapter cannot be
// unit-tested without linking ErgFile, which drags in Context
// and the GUI (see Phase 0 step 4). Its field mapping is
// therefore reviewed, not test-covered. It is also not compiled
// into the GUI binary yet, because no production file includes
// it in Phase 1 — qmake HEADERS lists do not compile headers.
// What verifies it in Phase 1 is a standalone syntax check
// (g++ -fsyntax-only, with the full Qt/GSL/Qwt include paths).
// The mapping function itself (workoutStepFromSection) IS covered
// by the Phase 0 mapping tests; what is not covered is the
// ErgFile -> section plumbing here, nor a live ErgFile run.
// Do not claim otherwise.

#ifndef _GC_WorkoutAdapter_h
#define _GC_WorkoutAdapter_h 1

#include "ErgFile.h"
#include "Core/Mapping.h"
#include "Core/Workout.h"

#include <string>

inline Workout workoutFromErgFile(ErgFile &ergFile) {
    Workout workout;
    std::string name = ergFile.description().toStdString();
    if (name.empty()) {
        name = ergFile.name().toStdString();
    }
    if (name.empty()) {
        name = ergFile.filename().toStdString();
    }
    workout.name = name;
    workout.source_path = ergFile.filename().toStdString();

    // Only time-based watt workouts produce steps. CRS/slope
    // routes are distance-based; see the header comment.
    if (!ergFile.hasWatts()) {
        return workout;
    }

    bool is_slope = ergFile.format() == ErgFileFormat::crs ||
                    ergFile.type() == ErgFileType::slp;
    if (is_slope) {
        return workout;
    }

    QList<ErgFileSection> sections = ergFile.Sections();
    for (const ErgFileSection &section : sections) {
        WorkoutSectionSource source;
        source.duration_ms = section.duration;
        source.start_value = section.start;
        source.is_slope = false;
        workout.steps.push_back(workoutStepFromSection(source));
    }
    return workout;
}

#endif // _GC_WorkoutAdapter_h
