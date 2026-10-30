QT += testlib core

TARGET = phase0Mapping
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES = testPhase0Mapping.cpp

# No GC_OBJS: the Phase 0 types are header-only, and the old types
# (RealtimeData, ErgFile, Zones) are deliberately NOT linked here —
# see the header comment in testPhase0Mapping.cpp for why linking
# them would drag Context and the GUI into a core test.

include(../../unittests.pri)
