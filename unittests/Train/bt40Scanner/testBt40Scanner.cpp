// Bt40Scanner tests — headless, no radio, no sleeps.
//
// Drives the pure helpers isFtmsTrainer() / deviceInfoFor()
// with constructed QBluetoothDeviceInfo objects: the FTMS
// advertisement filter and the ble:<address> id mapping.
// discover() itself is NOT tested here — it needs a real
// Bluetooth adapter and an event loop; it is hardware-gated.
//
// No sleeps anywhere: everything is pure predicate/mapping.

#include "Train/Bt40Scanner.h"

#include <QBluetoothAddress>
#include <QBluetoothUuid>
#include <QTest>

namespace {

// FTMS service UUID (0x1826, Bluetooth SIG assigned number).
QBluetoothUuid ftmsUuid()
{
    return QBluetoothUuid(quint16(0x1826));
}

QBluetoothDeviceInfo device(const QString &address,
                            const QString &name,
                            QBluetoothDeviceInfo::CoreConfigurations config,
                            const QList<QBluetoothUuid> &uuids)
{
    QBluetoothDeviceInfo info(QBluetoothAddress(address), name, 0);
    info.setCoreConfigurations(config);
    info.setServiceUuids(uuids);
    return info;
}

QBluetoothDeviceInfo ftmsHub()
{
    return device("AA:BB:CC:DD:EE:FF", "Zwift Hub",
                  QBluetoothDeviceInfo::LowEnergyCoreConfiguration,
                  {ftmsUuid()});
}

} // namespace

class TestBt40Scanner : public QObject {
    Q_OBJECT
private slots:
    void ftmsAdvertisementDetected();
    void noFtmsUuidRejected();
    void classicOnlyRejected();
    void noServicesRejected();
    void nullAddressRejected();
    void deviceInfoIdIsBleAddress();
    void deviceInfoEmptyNameStillHasId();
};

void TestBt40Scanner::ftmsAdvertisementDetected()
{
    QVERIFY(Bt40Scanner::isFtmsTrainer(ftmsHub()));
}

void TestBt40Scanner::noFtmsUuidRejected()
{
    // Heart-rate service only (0x180D) — not a trainer.
    QBluetoothDeviceInfo info = device(
        "AA:BB:CC:DD:EE:FF", "HR Strap",
        QBluetoothDeviceInfo::LowEnergyCoreConfiguration,
        {QBluetoothUuid(quint16(0x180D))});
    QVERIFY(!Bt40Scanner::isFtmsTrainer(info));
}

void TestBt40Scanner::classicOnlyRejected()
{
    // FTMS UUID, but a Classic-only device (no LE): the stable
    // id and the GATT path both need Low Energy.
    QBluetoothDeviceInfo info = device(
        "AA:BB:CC:DD:EE:FF", "Zwift Hub",
        QBluetoothDeviceInfo::BaseRateCoreConfiguration, {ftmsUuid()});
    QVERIFY(!Bt40Scanner::isFtmsTrainer(info));
}

void TestBt40Scanner::noServicesRejected()
{
    QBluetoothDeviceInfo info = device(
        "AA:BB:CC:DD:EE:FF", "Zwift Hub",
        QBluetoothDeviceInfo::LowEnergyCoreConfiguration, {});
    QVERIFY(!Bt40Scanner::isFtmsTrainer(info));
}

void TestBt40Scanner::nullAddressRejected()
{
    // No address means no stable ble:<address> id (the macOS
    // case); the scanner must not claim it as a trainer.
    QBluetoothDeviceInfo info(QBluetoothAddress(), "Zwift Hub", 0);
    info.setCoreConfigurations(
        QBluetoothDeviceInfo::LowEnergyCoreConfiguration);
    info.setServiceUuids({ftmsUuid()});
    QVERIFY(!Bt40Scanner::isFtmsTrainer(info));
}

void TestBt40Scanner::deviceInfoIdIsBleAddress()
{
    DeviceInfo di = Bt40Scanner::deviceInfoFor(ftmsHub());
    QCOMPARE(di.id, std::string("ble:AA:BB:CC:DD:EE:FF"));
    QCOMPARE(di.name, std::string("Zwift Hub"));
    QCOMPARE(di.transport, std::string("ble"));
    QVERIFY(di.is_controllable_trainer);
    QVERIFY(di.has_power);
    QVERIFY(di.has_speed_cadence);
    QVERIFY(!di.has_hr); // Zwift Hub reports no HR
}

void TestBt40Scanner::deviceInfoEmptyNameStillHasId()
{
    QBluetoothDeviceInfo info = device(
        "11:22:33:44:55:66", "",
        QBluetoothDeviceInfo::LowEnergyCoreConfiguration, {ftmsUuid()});
    DeviceInfo di = Bt40Scanner::deviceInfoFor(info);
    QCOMPARE(di.id, std::string("ble:11:22:33:44:55:66"));
    QCOMPARE(di.name, std::string(""));
}

QTEST_MAIN(TestBt40Scanner)
#include "testBt40Scanner.moc"
