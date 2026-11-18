QT += testlib core

TARGET = physicsUtility
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES = testPhysicsUtility.cpp

# GC_OBJS, not header-only: computeInstantSpeed lives in
# src/Core/PhysicsUtility.cpp (moved from src/Train), and it
# calls the cubic solver in src/Metrics/BlinnSolver.cpp.
# Both objects come from the main src build, like the season
# suite's Season object.
GC_OBJS = PhysicsUtility \
          BlinnSolver

include(../../unittests.pri)
