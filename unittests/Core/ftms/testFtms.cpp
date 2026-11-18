// FTMS protocol tests — headless, no radio.
//
// Drives the Ftms.cpp helpers with hand-built notification
// payloads (wire format, little-endian, byte-exact so the
// layout is pinned, not just round-tripped through a writer)
// and asserts exact control-point bytes for the encoders.
//
// No sleeps anywhere: everything is pure parsing/encoding.
//
// Two things this suite pins that the legacy code gets subtly
// wrong or leaves implicit:
//  - ftms_parse_indoor_bike_data DISCARDS the 24-bit total
//    distance (reads it into dummies). The skip must be exactly
//    3 bytes or every field after it shifts; the distance-skip
//    test proves it.
//  - Scaling lives outside the parser: cadence is raw 0.5 rpm
//    units (/2.0 in updateValue), speed raw 0.01 kph units
//    (/100 in updateValue). The parser returns raw values and
//    this suite asserts raw values.

#include "Train/Ftms.h"

#include <QTest>

namespace {

// Build a payload from literal bytes (little-endian on the wire).
QByteArray payload(std::initializer_list<unsigned char> bytes)
{
    QByteArray out;
    for (unsigned char b : bytes) {
        out.append(static_cast<char>(b));
    }
    return out;
}

FtmsDeviceInformation deviceInfo(qint16 minPower, qint16 maxPower,
                                 quint16 powerInc, qint16 minRes,
                                 qint16 maxRes, quint16 resInc)
{
    FtmsDeviceInformation info;
    info.minimal_power = minPower;
    info.maximal_power = maxPower;
    info.power_increment = powerInc;
    info.minimal_resistance = minRes;
    info.maximal_resistance = maxRes;
    info.resistance_increment = resInc;
    return info;
}

// QDataStream is neither copyable nor movable, so each test builds
// its own stream from the payload bytes (always little-endian,
// as updateValue does).

} // namespace

class TestFtms : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------- indoor-bike parsing
    void parseMinimalSpeed()
    {
        // flags = 0: MORE_DATA clear, so instant speed is present
        // and nothing else is.
        FtmsIndoorBikeData bd{};
        QByteArray data = payload({0x00, 0x00, 0x80, 0x0C});
        QDataStream ds(data);
        ds.setByteOrder(QDataStream::LittleEndian);
        ftms_parse_indoor_bike_data(ds, bd);
        QCOMPARE(bd.inst_speed, static_cast<quint16>(3200)); // 32.00 kph
        QCOMPARE(bd.inst_power, static_cast<qint16>(0));
        QCOMPARE(bd.inst_cadence, static_cast<quint16>(0));
    }

    void parseAllFields()
    {
        // Every flag set: the payload must be consumed in exactly
        // the parser's order, or the fields shift.
        FtmsIndoorBikeData bd{};
        QByteArray data = payload({
            0xFF, 0x1F, // flags = 0x1FFF (all present)
            0xB8, 0x0B, // avg_speed 3000 (30.00 kph)
            0xB4, 0x00, // inst_cadence 180 (90 rpm at 0.5/unit)
            0xAA, 0x00, // avg_cadence 170
            0x11, 0x22, 0x33, // total distance, 24 bits, discarded
            0x07, 0x00, // resistance level 7
            0xFA, 0x00, // inst_power 250 W
            0xF0, 0x00, // avg_power 240 W
            0x2C, 0x01, // total energy 300 kcal
            0x58, 0x02, // energy per hour 600 kcal
            0x05, // energy per minute 5 kcal
            0x5A, // heart rate 90 bpm
            0x08, // metabolic equivalent 8 MET
            0xE1, 0x00, // elapsed time 225 s
            0xC8, 0x01, // remaining time 456 s
        });
        QDataStream ds(data);
        ds.setByteOrder(QDataStream::LittleEndian);
        ftms_parse_indoor_bike_data(ds, bd);
        // MORE_DATA set, so no instant speed was read.
        QCOMPARE(bd.inst_speed, static_cast<quint16>(0));
        QCOMPARE(bd.avg_speed, static_cast<quint16>(3000));
        QCOMPARE(bd.inst_cadence, static_cast<quint16>(180));
        QCOMPARE(bd.avg_cadence, static_cast<quint16>(170));
        QCOMPARE(bd.resistence_level, static_cast<qint16>(7));
        QCOMPARE(bd.inst_power, static_cast<qint16>(250));
        QCOMPARE(bd.avg_power, static_cast<qint16>(240));
        QCOMPARE(bd.tot_energy, static_cast<quint16>(300));
        QCOMPARE(bd.energy_per_hour, static_cast<quint16>(600));
        QCOMPARE(bd.energy_per_min, static_cast<quint8>(5));
        QCOMPARE(bd.heart_rate, static_cast<quint8>(90));
        QCOMPARE(bd.met_equivalent, static_cast<quint8>(8));
        QCOMPARE(bd.elapsed_time, static_cast<quint16>(225));
        QCOMPARE(bd.remaining_time, static_cast<quint16>(456));
    }

    void parseDistanceSkipIsExactlyThreeBytes()
    {
        // flags = MORE_DATA | TOTAL_DISTANCE | INST_POWER.
        // The distance field is 0xFFFFFF; if the skip were 2 or
        // 4 bytes, inst_power would parse as garbage.
        FtmsIndoorBikeData bd{};
        QByteArray data = payload({
            0x51, 0x00, // flags = 0x0051
            0xFF, 0xFF, 0xFF, // total distance, discarded
            0xC8, 0x00, // inst_power 200 W
        });
        QDataStream ds(data);
        ds.setByteOrder(QDataStream::LittleEndian);
        ftms_parse_indoor_bike_data(ds, bd);
        QCOMPARE(bd.inst_power, static_cast<qint16>(200));
    }

    // ------------------------------------------------ feature parsing
    void parseFeatureAllTargets()
    {
        FtmsDeviceInformation info;
        QByteArray data = payload({
            0xFF, 0xFF, 0xFF, 0xFF, // features: ignored by the parser
            0x0C, 0xA0, 0x00, 0x00, // target settings: power | resistance | sim | spin-down (0xA00C)
        });
        QDataStream ds(data);
        ds.setByteOrder(QDataStream::LittleEndian);
        ftms_parse_feature(ds, info);
        QVERIFY(info.supports_power_target);
        QVERIFY(info.supports_resistance_target);
        QVERIFY(info.supports_simulation_target);
        QVERIFY(info.supports_spin_down_calibration);
    }

    void parseFeatureSubset()
    {
        FtmsDeviceInformation info;
        QByteArray data = payload({
            0x00, 0x00, 0x00, 0x00, // features
            0x08, 0x20, 0x00, 0x00, // target settings: power | sim only
        });
        QDataStream ds(data);
        ds.setByteOrder(QDataStream::LittleEndian);
        ftms_parse_feature(ds, info);
        QVERIFY(info.supports_power_target);
        QVERIFY(!info.supports_resistance_target);
        QVERIFY(info.supports_simulation_target);
        QVERIFY(!info.supports_spin_down_calibration);
    }

    void parseFeatureNone()
    {
        FtmsDeviceInformation info;
        QByteArray data = payload({
            0x00, 0x00, 0x00, 0x00, // features
            0x00, 0x00, 0x00, 0x00, // target settings: none
        });
        QDataStream ds(data);
        ds.setByteOrder(QDataStream::LittleEndian);
        ftms_parse_feature(ds, info);
        QVERIFY(!info.supports_power_target);
        QVERIFY(!info.supports_resistance_target);
        QVERIFY(!info.supports_simulation_target);
        QVERIFY(!info.supports_spin_down_calibration);
    }

    // ------------------------------------------------------------ caps
    void powerCapRounding()
    {
        FtmsDeviceInformation info =
            deviceInfo(0, 1000, 5, 0, 0, 0);
        QCOMPARE(ftms_power_cap(253, info), static_cast<qint16>(255));
        QCOMPARE(ftms_power_cap(252, info), static_cast<qint16>(250));
        QCOMPARE(ftms_power_cap(251, info), static_cast<qint16>(250));
        QCOMPARE(ftms_power_cap(0, info), static_cast<qint16>(0));
        QCOMPARE(ftms_power_cap(1000, info), static_cast<qint16>(1000));
    }

    void powerCapClampsToRange()
    {
        FtmsDeviceInformation info =
            deviceInfo(0, 1000, 5, 0, 0, 0);
        QCOMPARE(ftms_power_cap(2000, info), static_cast<qint16>(1000));
        QCOMPARE(ftms_power_cap(-10, info), static_cast<qint16>(0));
    }

    void powerCapZeroIncrementPassesThrough()
    {
        // Before the device reports its range the increment is 0.
        // Dividing by it would crash; the cap passes the value
        // through instead (see Ftms.cpp).
        FtmsDeviceInformation info =
            deviceInfo(0, 0, 0, 0, 0, 0);
        QCOMPARE(ftms_power_cap(253, info), static_cast<qint16>(253));
        QCOMPARE(ftms_power_cap(2000, info), static_cast<qint16>(2000));
    }

    void resistanceCapRounding()
    {
        FtmsDeviceInformation info =
            deviceInfo(0, 0, 0, 0, 100, 10);
        QCOMPARE(ftms_resistance_cap(47, info), 50.0);
        QCOMPARE(ftms_resistance_cap(44, info), 40.0);
        QCOMPARE(ftms_resistance_cap(0, info), 0.0);
        QCOMPARE(ftms_resistance_cap(100, info), 100.0);
    }

    void resistanceCapClampsToRange()
    {
        FtmsDeviceInformation info =
            deviceInfo(0, 0, 0, 0, 100, 10);
        QCOMPARE(ftms_resistance_cap(200, info), 100.0);
        QCOMPARE(ftms_resistance_cap(-5, info), 0.0);
    }

    void resistanceCapZeroIncrementPassesThrough()
    {
        FtmsDeviceInformation info =
            deviceInfo(0, 0, 0, 0, 0, 0);
        QCOMPARE(ftms_resistance_cap(47, info), 47.0);
        QCOMPARE(ftms_resistance_cap(200, info), 200.0);
    }

    // ------------------------------------------------- command bytes
    void encodeRequestControl()
    {
        QCOMPARE(ftms_encode_request_control(), payload({0x00}));
    }

    void encodeTargetPower()
    {
        QCOMPARE(ftms_encode_set_target_power(250),
                 payload({0x05, 0xFA, 0x00}));
        QCOMPARE(ftms_encode_set_target_power(0),
                 payload({0x05, 0x00, 0x00}));
        QCOMPARE(ftms_encode_set_target_power(1000),
                 payload({0x05, 0xE8, 0x03}));
    }

    void encodeSimParams()
    {
        // Flat out: wind 0, grade 8.00% (800), crr 0, cw 0.
        QCOMPARE(ftms_encode_sim_params(0, 800, 0, 0),
                 payload({0x11, 0x00, 0x00, 0x20, 0x03, 0x00, 0x00}));
        // Wind 2.5 m/s (2500), grade -5.00% (-500), crr 33, cw 60.
        QCOMPARE(ftms_encode_sim_params(2500, -500, 33, 60),
                 payload({0x11, 0xC4, 0x09, 0x0C, 0xFE, 0x21, 0x3C}));
    }

    void encodeResistanceLevel()
    {
        QCOMPARE(ftms_encode_set_resistance_level(42),
                 payload({0x04, 0x2A, 0x00}));
        QCOMPARE(ftms_encode_set_resistance_level(0),
                 payload({0x04, 0x00, 0x00}));
    }

    void encodeSpinDown()
    {
        QCOMPARE(ftms_encode_spin_down_control(
                     FtmsSpinDown::FTMS_SPIN_DOWN_START),
                 payload({0x13, 0x01}));
        QCOMPARE(ftms_encode_spin_down_control(
                     FtmsSpinDown::FTMS_SPIN_DOWN_IGNORE),
                 payload({0x13, 0x02}));
    }
};

QTEST_MAIN(TestFtms)
#include "testFtms.moc"
