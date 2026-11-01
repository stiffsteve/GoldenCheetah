QT += testlib core

TARGET = fakeDevice
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES = testFakeDevice.cpp

# No GC_OBJS: IDevice, FakeDevice, and DeviceManager are
# header-only, like the telemetryBus suite.

include(../../unittests.pri)
