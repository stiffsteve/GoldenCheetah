QT += testlib core

TARGET = bicycleSim
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES = testBicycleSim.cpp

# GC_OBJS, not header-only: the model lives in
# src/Core/BicycleSim.cpp (moved from src/Train), and it calls
# AirDensity/MsToKmh in src/Core/PhysicsUtility.cpp (with its
# cubic solver in src/Metrics/BlinnSolver.cpp). All three objects
# come from the main src build, like the physicsUtility suite.
GC_OBJS = BicycleSim \
          PhysicsUtility \
          BlinnSolver

include(../../unittests.pri)
