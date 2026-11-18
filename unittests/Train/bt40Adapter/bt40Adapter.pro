QT += testlib core

TARGET = bt40Adapter
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES = testBt40Adapter.cpp

# Bt40DeviceAdapter.cpp lives in src/Train and is compiled by
# the main src build; the test links its object. The adapter
# header is Qt-free and its .cpp includes no GUI header, so the
# production BT40 backend is NOT linked here — the stub in the
# test file implements IBt40Backend. Deliberately no widgets and
# no bluetooth in QT: if the adapter ever pulled either in, this
# binary would fail to link.
GC_OBJS = Bt40DeviceAdapter

include(../../unittests.pri)
