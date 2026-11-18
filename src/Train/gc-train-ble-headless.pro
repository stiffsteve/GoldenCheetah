# Real-trainer hardware validation harness (core/view split).
# Console program: QCoreApplication + event loop, no window,
# no dialogs. See gc-train-ble-headless.cpp and
# doc/core-view-split/real-trainer-hardware-validation-plan.md.
#
# Deliberately NOT wired into build.pro: a standalone program,
# like the other headless programs. Build it after the main
# GUI build — it links the prebuilt objects from the sub-src
# build (same GC_OBJS mechanism as the unit tests), so
# scripts/baseline-build.sh (or qmake + sub-qwt + sub-src)
# must have run first:
#   export PATH="$HOME/Qt/6.8.3/gcc_64/bin:$PATH"
#   cd src/Train && qmake gc-train-ble-headless.pro && make
#
# Qt modules: core + bluetooth + widgets + core5compat + svg.
# Widgets, core5compat, and svg come in through the
# production BT40 link closure (RealtimeController ->
# VMProWidget -> VMProConfigurator -> Colors, which uses
# QSvgRenderer and QRegExp); the harness itself never shows
# a widget.

QT += core bluetooth widgets core5compat svg

TARGET = gc-train-ble-headless
CONFIG += console c++17
CONFIG -= app_bundle

TEMPLATE = app

SOURCES = gc-train-ble-headless.cpp

# Prebuilt objects from the main sub-src build (flat in src/).
# The list is the production BT40 link closure, found by
# iterating the linker: BT40Controller derives from
# RealtimeController, whose .cpp reaches DeviceTypes,
# GSettings, ANTMessage, VMProWidget, and the Kurt protocol
# helpers; RealtimeData reaches GSettings too.
GC_SRC_DIR = ..
GC_OBJS = Bt40DeviceAdapter Bt40Backend BT40Controller BT40Device \
    Ftms Bt40Scanner CalibrationData DeviceConfiguration \
    RealtimeData PhysicsUtility BlinnSolver \
    RealtimeController moc_RealtimeController \
    Settings DeviceTypes ANTMessage \
    VMProWidget moc_VMProWidget \
    VMProConfigurator moc_VMProConfigurator \
    Colors Utils moc_Colors \
    KurtInRide KurtSmartControl PolynomialRegression \
    moc_BT40Controller moc_BT40Device
for(obj, GC_OBJS) {
    LIBS += $${GC_SRC_DIR}/$${obj}.o
}

INCLUDEPATH += ..
