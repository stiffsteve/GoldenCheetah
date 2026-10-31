QT += testlib core

TARGET = telemetryBus
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES = testTelemetryBus.cpp

# No GC_OBJS: TelemetryBus, TelemetryMerge, and the Phase 0 types
# are header-only, like the workoutPlayer suite.

include(../../unittests.pri)
