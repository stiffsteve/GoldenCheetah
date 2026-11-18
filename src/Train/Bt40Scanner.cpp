/*
 * Copyright (c) 2026 GoldenCheetah contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the Free Software Foundation, Inc., 51 Franklin Street,
 * Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "Bt40Scanner.h"

#include "Ftms.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QEventLoop>
#include <QTimer>

QList<QBluetoothDeviceInfo> Bt40Scanner::discover(int timeout_ms)
{
    QBluetoothDeviceDiscoveryAgent agent;
    QList<QBluetoothDeviceInfo> found;

    QEventLoop loop;
    QObject::connect(
        &agent,
        &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
        [&](const QBluetoothDeviceInfo &info) { found.append(info); });
    QObject::connect(&agent,
                     &QBluetoothDeviceDiscoveryAgent::finished, &loop,
                     &QEventLoop::quit);
    QObject::connect(
        &agent,
        &QBluetoothDeviceDiscoveryAgent::errorOccurred,
        [&](QBluetoothDeviceDiscoveryAgent::Error) { loop.quit(); });
    QTimer::singleShot(timeout_ms, &loop, &QEventLoop::quit);

    agent.start();
    loop.exec();
    agent.stop();

    QList<QBluetoothDeviceInfo> trainers;
    for (const QBluetoothDeviceInfo &info : found) {
        if (isFtmsTrainer(info)) {
            trainers.append(info);
        }
    }
    return trainers;
}

bool Bt40Scanner::isFtmsTrainer(const QBluetoothDeviceInfo &info)
{
    if (!(info.coreConfigurations() &
          QBluetoothDeviceInfo::LowEnergyCoreConfiguration)) {
        return false;
    }
    if (info.address().isNull()) {
        return false; // no stable id on this platform (see header)
    }
    return info.serviceUuids().contains(s_FtmsService_UUID);
}

DeviceInfo Bt40Scanner::deviceInfoFor(const QBluetoothDeviceInfo &info)
{
    DeviceInfo di;
    di.id = "ble:" + info.address().toString().toStdString();
    di.name = info.name().toStdString();
    di.transport = "ble";
    di.is_controllable_trainer = true;
    di.has_power = true;
    di.has_hr = false;
    di.has_speed_cadence = true;
    return di;
}
