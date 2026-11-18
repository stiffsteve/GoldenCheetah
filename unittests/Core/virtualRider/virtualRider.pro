QT += testlib core

TARGET = virtualRider
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES = testVirtualRider.cpp

# VirtualRider is header-only, but it calls computeInstantSpeed,
# which lives in src/Core/PhysicsUtility.cpp (with its cubic
# solver in src/Metrics/BlinnSolver.cpp), and it owns a Bicycle
# (src/Core/BicycleSim.cpp). All three objects come from the main
# src build, like the physicsUtility suite.
GC_OBJS = BicycleSim \
          PhysicsUtility \
          BlinnSolver

include(../../unittests.pri)
