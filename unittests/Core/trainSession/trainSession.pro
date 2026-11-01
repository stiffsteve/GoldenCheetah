QT += testlib core

TARGET = trainSession
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES = testTrainSession.cpp

# No GC_OBJS: TrainSession, LiveMetrics, Recorder, FakeDevice,
# and the Phase 1/2 types are header-only, like the fakeDevice
# suite.

include(../../unittests.pri)
