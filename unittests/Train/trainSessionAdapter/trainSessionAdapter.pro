QT += testlib core

TARGET = trainSessionAdapter
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES = testTrainSessionAdapter.cpp \
          ../../../src/Train/TrainSessionAdapter.cpp

HEADERS = ../../../src/Train/TrainSessionAdapter.h

# No GC_OBJS: TrainSession, FakeDevice, and the Phase 1/2
# types are header-only. The adapter header is listed in
# HEADERS so qmake generates its moc object in this build;
# without it, the test links against a TrainSessionAdapter
# with no meta-object (undefined staticMetaObject, vtable,
# and signals), which is the link error that first appeared
# here.

include(../../unittests.pri)
