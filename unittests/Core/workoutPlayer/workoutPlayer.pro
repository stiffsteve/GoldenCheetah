QT += testlib core

TARGET = workoutPlayer
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES = testWorkoutPlayer.cpp

# No GC_OBJS: WorkoutPlayer, WorkoutParser, and the Phase 0 types
# are header-only, like the phase0Mapping suite.

include(../../unittests.pri)
