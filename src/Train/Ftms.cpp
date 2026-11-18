#include "Ftms.h"

#include <QIODevice>

void ftms_parse_indoor_bike_data(QDataStream &ds, FtmsIndoorBikeData &bd)
{
    //quint16 flags, inst_speed, avg_speed, inst_cadence, avg_cadence, tot_energy, energy_per_hour, elapsed_time, remaining_time;
    //qint16 resistence_level, inst_power, avg_power;
    //quint8 energy_per_min, heart_rate, met_equivalent;
    quint16 dummy16;
    quint8 dummy8;

    ds >> bd.flags;

    if (!(bd.flags & FtmsIndoorBikeFlags::FTMS_MORE_DATA))
    {
        // If more data is not set, instant speed is present
        ds >> bd.inst_speed; // resolution: 0.01 km/h
    }

    if (bd.flags & FtmsIndoorBikeFlags::FTMS_AVERAGE_SPEED_PRESENT)
    {
        ds >> bd.avg_speed; // resolution: 0.01 km/h
    }

    if (bd.flags & FtmsIndoorBikeFlags::FTMS_INST_CADENCE_PRESENT)
    {
        ds >> bd.inst_cadence; // resolution: 0.5 rpm
    }

    if (bd.flags & FtmsIndoorBikeFlags::FTMS_AVERAGE_CADENCE_PRESENT)
    {
        ds >> bd.avg_cadence; // resolution: 0.5 rpm
    }

    if (bd.flags & FtmsIndoorBikeFlags::FTMS_TOTAL_DISTANCE_PRESENT)
    {
        ds >> dummy16 >> dummy8; // we don't care about this, so just read 24 bits
    }

    if (bd.flags & FtmsIndoorBikeFlags::FTMS_RESISTANCE_LEVEL_PRESENT)
    {
        ds >> bd.resistence_level; // resolution: unitless
    }

    if (bd.flags & FtmsIndoorBikeFlags::FTMS_INST_POWER_PRESENT)
    {
        ds >> bd.inst_power; // resolution: 1 watt
    }

    if (bd.flags & FtmsIndoorBikeFlags::FTMS_AVERAGE_POWER_PRESENT)
    {
        ds >> bd.avg_power; // resolution: 1 watt
    }

    if (bd.flags & FtmsIndoorBikeFlags::FTMS_EXPENDED_ENERGY_PRESENT)
    {
        ds >> bd.tot_energy >> bd.energy_per_hour >> bd.energy_per_min; // resolution: 1 kcal
    }

    if (bd.flags & FtmsIndoorBikeFlags::FTMS_HEART_RATE_PRESENT)
    {
        ds >> bd.heart_rate; // resolution: 1 bpm
    }

    if (bd.flags & FtmsIndoorBikeFlags::FTMS_METABOLIC_EQUIV_PRESENT)
    {
        ds >> bd.met_equivalent; // resolution: 1 MET
    }

    if (bd.flags & FtmsIndoorBikeFlags::FTMS_ELAPSED_TIME_PRESENT)
    {
        ds >> bd.elapsed_time; // resolution: 1 second
    }

    if (bd.flags & FtmsIndoorBikeFlags::FTMS_REMAINING_TIME_PRESENT)
    {
        ds >> bd.remaining_time; // resolution: 1 second
    }
}

void ftms_parse_feature(QDataStream &ds, FtmsDeviceInformation &info)
{
    quint32 features, target_settings;
    ds >> features >> target_settings;
    Q_UNUSED(features); // the feature bits are not used by the legacy code

    // Sticky: the legacy code only ever set these flags true,
    // never cleared them (the feature characteristic is read
    // once at discovery), so set-true rather than assign.
    if (target_settings & FtmsTargetSetting::FTMS_POWER_TARGET_SUPPORTED) {
        info.supports_power_target = true;
    }
    if (target_settings & FtmsTargetSetting::FTMS_RESISTANCE_TARGET_SUPPORTED) {
        info.supports_resistance_target = true;
    }
    if (target_settings & FtmsTargetSetting::FTMS_INDOOR_BIKE_SIMULATION_SUPPORTED) {
        info.supports_simulation_target = true;
    }
    if (target_settings & FtmsTargetSetting::FTMS_SPIN_DOWN_CONTROL_SUPPORTED) {
        info.supports_spin_down_calibration = true;
    }
}

qint16 ftms_power_cap(qint16 power, FtmsDeviceInformation &device_info) {

    // Guard: before the device reports its power range the
    // increment is 0, and dividing by it would crash (this window
    // exists in legacy too — the first ERG command can arrive
    // before the range read completes). Pass the value through
    // unclamped rather than crashing; the only behaviour change
    // is in the previously-crashing case.
    if (device_info.power_increment == 0) {
        return power;
    }

    power = qRound((double)power/device_info.power_increment)*device_info.power_increment;
    if (power > device_info.maximal_power)
    {
        power = device_info.maximal_power;
    } else if (power < device_info.minimal_power) {
        power = device_info.minimal_power;
    }

    return power;
}

double ftms_resistance_cap(qint16 resistance, FtmsDeviceInformation &device_info) {
    // Same zero-increment guard as ftms_power_cap (see above).
    if (device_info.resistance_increment == 0) {
        return resistance;
    }

    resistance = qRound((double)resistance/device_info.resistance_increment)*device_info.resistance_increment;
    if (resistance > device_info.maximal_resistance)
    {
        resistance = device_info.maximal_resistance;
    } else if (resistance < device_info.minimal_resistance) {
        resistance = device_info.minimal_resistance;
    }

    return resistance;
}

QByteArray ftms_encode_request_control()
{
    QByteArray command;
    QDataStream ds(&command, QIODevice::ReadWrite);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << (quint8)FtmsControlPointCommand::FTMS_REQUEST_CONTROL;
    return command;
}

QByteArray ftms_encode_set_target_power(qint16 watts)
{
    QByteArray command;
    QDataStream ds(&command, QIODevice::ReadWrite);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << (quint8)FtmsControlPointCommand::FTMS_SET_TARGET_POWER << watts;
    return command;
}

QByteArray ftms_encode_set_resistance_level(qint16 resistance)
{
    QByteArray command;
    QDataStream ds(&command, QIODevice::ReadWrite);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << (quint8)FtmsControlPointCommand::FTMS_SET_TARGET_RESISTANCE_LEVEL << resistance;
    return command;
}

QByteArray ftms_encode_spin_down_control(quint8 opcode)
{
    QByteArray command;
    QDataStream ds(&command, QIODevice::ReadWrite);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << (quint8)FtmsControlPointCommand::FTMS_SPIN_DOWN_CONTROL << opcode;
    return command;
}

QByteArray ftms_encode_sim_params(qint16 wind_speed_milli,
                                  qint16 grade_centi,
                                  quint8 crr, quint8 cw)
{
    QByteArray command;
    QDataStream ds(&command, QIODevice::ReadWrite);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << (quint8)FtmsControlPointCommand::FTMS_SET_INDOOR_BIKE_SIMULATION_PARAMS
       << wind_speed_milli << grade_centi << crr << cw;
    return command;
}
