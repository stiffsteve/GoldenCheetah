QT += testlib core bluetooth

TARGET = ftms
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES = testFtms.cpp

# Ftms.cpp lives in src/Train and is compiled by the main src
# build; the test links its object. QT bluetooth is needed for
# QBluetoothUuid (Ftms.h).
GC_OBJS = Ftms

include(../../unittests.pri)
