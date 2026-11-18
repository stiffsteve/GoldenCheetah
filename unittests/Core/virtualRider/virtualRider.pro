QT += testlib core

TARGET = virtualRider
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES = testVirtualRider.cpp

# VirtualRider is header-only, but it calls computeInstantSpeed,
# which lives in src/Core/PhysicsUtility.cpp (with its cubic
# solver in src/Metrics/BlinnSolver.cpp). Both objects come from
# the main src build, like the physicsUtility suite.
GC_OBJS = PhysicsUtility \
          BlinnSolver

include(../../unittests.pri)
