QT += testlib core bluetooth

TARGET = bt40Scanner
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES = testBt40Scanner.cpp

# Bt40Scanner.cpp lives in src/Train and is compiled by the
# main src build; the test links its object. It uses Ftms.h for
# the FTMS service UUID, so Ftms.o is linked too. discover() is
# not exercised here (it needs a real Bluetooth adapter).
GC_OBJS = Bt40Scanner \
          Ftms

include(../../unittests.pri)
