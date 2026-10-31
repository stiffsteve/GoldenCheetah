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

// Phase 1: ERG/MRC workout parser (core/view split, TDD section 14).
// Pure file in, Workout out. No hardware, no GUI, no Qt.
//
// Scope, deliberately narrow (see doc/core-view-split/phase1-plan.md):
//   - ERG:  [COURSE HEADER] with "MINUTES WATTS", [COURSE DATA]
//           lines "minutes watts" (absolute watts).
//   - MRC:  [COURSE HEADER] with "MINUTES PERCENT" (or "MINUTES
//           FTP"), [COURSE DATA] lines "minutes percent" (percent
//           of FTP, may exceed 100, e.g. 150), or "minutes
//           percent%" with an explicit % suffix.
//   - NOT parsed: CRS (DISTANCE GRADE WIND — distance-based, not
//     time-based, so it does not fit WorkoutStep), ZWO (XML),
//     TTS, PGMF, ERG2, code, and any other format. Those return
//     ok=false with an error, rather than silently producing a
//     workout with wrong durations.
//
// Semantics, matching the legacy ErgFile parser where it matters
// and differing where the TDD requires it:
//   - Points: a data point's value applies from its time until
//     the next point's time, exactly as ErgFile::Sections() does.
//     A first point after time 0 creates an initial step from 0
//     to that point, with that point's value. There is no step
//     after the last point, so total duration is the last point's
//     time, not beyond it.
//   - Merging: consecutive steps with identical targets are merged
//     into one step. Total duration is unchanged; the workout is
//     just cleaner (legacy files often repeat a value across
//     several change points with no intervening change).
//   - Preservation, not legacy conversion: unlike ErgFile, this
//     parser does NOT bake the athlete's CP into points. MRC
//     percent stays as target_pct_ftp, ERG watts stay as
//     target_watts, and header FTP (if present) is ignored —
//     WorkoutPlayer::load() resolves percent against the FTP
//     passed to it. A legacy-parsed MRC workout and a core-parsed
//     one will therefore disagree on absolute watts by design;
//     see the Phase 1 plan doc and WorkoutAdapter.h.
//   - Header: DESCRIPTION becomes Workout.name if present, else
//     the basename of source_path, else empty. FILE NAME is
//     ignored (source_path is the caller's job). UNITS, VERSION,
//     and other header settings are ignored.
//   - Ignored, not errors: blank lines, ';' comments (a
//     GoldenCheetah extension in legacy files), [COURSE TEXT]
//     cues, and LAP lines. Laps and text cues have no Workout
//     equivalent in Phase 1; dropping them silently is a known
//     limitation, recorded here, not a parsing failure.
//   - Line endings: \r, \n, and \r\n are all handled (split on
//     \r and \n), matching the legacy parser's Mac-CR workaround.
//   - Times are minutes (double) in the file, seconds (double)
//     in WorkoutStep, via minutes * 60. No rounding to whole
//     seconds, so fractional minutes (e.g. 54.5) work exactly.
//
// Result: parseWorkout returns a ParseResult, not a throw and not
// a bare Workout, because a malformed file is a normal input in a
// workout library, not a crash. On failure, workout is empty and
// error explains why (including the offending line number where
// useful).
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// Header-only, like the other Phase 0/1 core types.

#ifndef _GC_WorkoutParser_h
#define _GC_WorkoutParser_h 1

#include "Workout.h"

#include <cctype>
#include <sstream>
#include <string>
#include <vector>

struct WorkoutParseResult {
    bool ok = false;
    Workout workout;
    std::string error;
};

namespace workout_parser_detail {

inline std::string trim(const std::string &s) {
    size_t first = 0;
    while (first < s.size() &&
           std::isspace(static_cast<unsigned char>(s[first]))) {
        ++first;
    }
    size_t last = s.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(s[last - 1]))) {
        --last;
    }
    return s.substr(first, last - first);
}

inline std::string toUpper(const std::string &s) {
    std::string out = s;
    for (char &c : out) {
        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

inline bool startsWith(const std::string &s, const std::string &prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

// A parsed data point, before conversion to steps.
struct Point {
    double minutes = 0;
    double value = 0; // watts for ERG, percent for MRC
    bool is_percent = false;
};

} // namespace workout_parser_detail

inline WorkoutParseResult parseWorkout(const std::string &content,
                                       const std::string &source_path) {
    using namespace workout_parser_detail;

    WorkoutParseResult result;
    Workout &workout = result.workout;
    workout.source_path = source_path;

    enum class Section { None, Header, Data, Text, End };
    Section section = Section::None;

    bool saw_data_section = false;
    bool format_is_mrc = false;
    bool format_seen = false;
    std::string description;
    std::vector<Point> points;

    // Split on \r and \n, so \r\n, \n, and old Mac \r all work.
    std::vector<std::string> lines;
    {
        std::string current;
        for (char c : content) {
            if (c == '\r' || c == '\n') {
                lines.push_back(current);
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        if (!current.empty()) {
            lines.push_back(current);
        }
    }

    for (size_t line_no = 0; line_no < lines.size(); ++line_no) {
        std::string raw = lines[line_no];
        std::string line = trim(raw);
        if (line.empty() || line[0] == ';') {
            continue;
        }
        std::string upper = toUpper(line);

        if (upper.find("[COURSE HEADER]") != std::string::npos) {
            section = Section::Header;
            continue;
        }
        if (upper.find("[END COURSE HEADER]") != std::string::npos) {
            section = Section::None;
            continue;
        }
        if (upper.find("[COURSE DATA]") != std::string::npos) {
            section = Section::Data;
            saw_data_section = true;
            continue;
        }
        if (upper.find("[END COURSE DATA]") != std::string::npos) {
            section = Section::End;
            continue;
        }
        if (upper.find("[COURSE TEXT]") != std::string::npos) {
            section = Section::Text;
            continue;
        }
        if (upper.find("[END COURSE TEXT]") != std::string::npos) {
            section = Section::End;
            continue;
        }

        if (section == Section::Header) {
            if (startsWith(upper, "MINUTES")) {
                format_seen = true;
                if (upper.find("PERCENT") != std::string::npos ||
                    upper.find("FTP") != std::string::npos) {
                    format_is_mrc = true;
                } else if (upper.find("WATTS") != std::string::npos) {
                    format_is_mrc = false;
                } else {
                    result.error = "unsupported MINUTES format in header";
                    return result;
                }
                continue;
            }
            if (upper.find("DISTANCE") != std::string::npos &&
                upper.find("GRADE") != std::string::npos) {
                result.error =
                    "CRS format (DISTANCE GRADE) is not supported in "
                    "Phase 1: it is distance-based, not time-based";
                return result;
            }
            // DESCRIPTION = ... (case-insensitive key, value as written)
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = toUpper(trim(line.substr(0, eq)));
                std::string value = trim(line.substr(eq + 1));
                if (key == "DESCRIPTION") {
                    description = value;
                }
                // FTP, VERSION, UNITS, FILE NAME, etc.: ignored.
            }
            continue;
        }

        if (section == Section::Text) {
            continue; // text cues: no Workout equivalent in Phase 1
        }

        if (section != Section::Data) {
            continue; // lines outside any section are ignored
        }

        // Data section: LAP lines are ignored (no lap concept yet).
        {
            std::istringstream in(line);
            double minutes = 0;
            if (!(in >> minutes)) {
                result.error = "malformed data line " +
                               std::to_string(line_no + 1) + ": " + line;
                return result;
            }
            std::string rest;
            std::getline(in, rest);
            std::string token = toUpper(trim(rest));
            if (startsWith(token, "LAP")) {
                continue;
            }
            // Value: a number, optionally with a % suffix.
            std::istringstream value_in(trim(rest));
            double value = 0;
            if (!(value_in >> value)) {
                result.error = "malformed data line " +
                               std::to_string(line_no + 1) + ": " + line;
                return result;
            }
            std::string suffix;
            value_in >> suffix;
            bool is_percent = format_is_mrc || suffix == "%";
            points.push_back(Point{minutes, value, is_percent});
        }
    }

    if (!saw_data_section) {
        result.error = "no [COURSE DATA] section found";
        return result;
    }
    if (points.empty()) {
        result.error = "no data points in [COURSE DATA]";
        return result;
    }
    if (format_seen && format_is_mrc) {
        // MRC: all points are percent, even without a % suffix.
        for (Point &p : points) {
            p.is_percent = true;
        }
    }

    // Points to steps, following ErgFile::Sections(): a point's
    // value applies until the next point's time. Points must be
    // non-decreasing in time; a point at the same time as the
    // previous one replaces it (no zero-duration step), and a
    // point earlier than the previous one is an error.
    workout.name = description;
    if (workout.name.empty() && !source_path.empty()) {
        size_t slash = source_path.find_last_of("/\\");
        workout.name = slash == std::string::npos
                           ? source_path
                           : source_path.substr(slash + 1);
    }

    auto append_step = [&](double duration_secs, const Point &p) {
        if (duration_secs <= 0) {
            return;
        }
        WorkoutStep step;
        step.duration_secs = duration_secs;
        if (p.is_percent) {
            step.target_pct_ftp = p.value;
        } else {
            step.target_watts = p.value;
        }
        // Merge with the previous step if the target is identical:
        // same watts, or same percent, with no gradient involved.
        if (!workout.steps.empty()) {
            WorkoutStep &prev = workout.steps.back();
            if (prev.target_watts == step.target_watts &&
                prev.target_pct_ftp == step.target_pct_ftp &&
                prev.target_gradient_pct == step.target_gradient_pct) {
                prev.duration_secs += step.duration_secs;
                return;
            }
        }
        workout.steps.push_back(step);
    };

    double prev_minutes = 0;
    bool first = true;
    for (size_t i = 0; i < points.size(); ++i) {
        const Point &p = points[i];
        if (!first && p.minutes < prev_minutes) {
            result.error = "data points out of order at time " +
                           std::to_string(p.minutes) + " minutes";
            result.workout = Workout();
            result.workout.source_path = source_path;
            return result;
        }
        if (first) {
            // Initial step from 0 to the first point, if it is
            // after time 0 (ErgFile::Sections() behaviour).
            if (p.minutes > 0) {
                append_step(p.minutes * 60.0, p);
            }
            first = false;
        } else if (p.minutes > prev_minutes) {
            // The previous point's value applies over this interval.
            append_step((p.minutes - prev_minutes) * 60.0, points[i - 1]);
        }
        // Same-time point: no step; it replaces the previous value
        // for the next interval (handled by using points[i] as the
        // "previous" point on the next iteration).
        prev_minutes = p.minutes;
    }

    result.ok = true;
    return result;
}

#endif // _GC_WorkoutParser_h
