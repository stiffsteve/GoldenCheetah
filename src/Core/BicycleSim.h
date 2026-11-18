/*
* Copyright (c) 2019 Eric Christoffersen (impolexg@outlook.com)
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the Free
* Software Foundation; either version 2 of the License, or (at your option)
* any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc., 51
* Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

// Core/view split: moved from src/Train/BicycleSim.h with the model
// intact. The GUI coupling is gone: no Context*, no RealtimeData,
// no Settings/Pages, no ErgFile include (it was dead). Configuration
// is a plain struct (BicycleConfig) with the SimBicyclePage defaults
// as member defaults; a Train-side helper builds it from a Context*.
// Hypoxia is OUT for v1: the altitude-fit functions and the Townsend
// table are deleted, and m_AltitudeCorrectionFactor stays in
// BicycleConstants (default 1.0, inert) so the fit is a pure addition
// later.
//
// The model is a kinetic-energy model: power in, minus the power
// needed to hold the current speed, becomes kinetic energy, which
// becomes speed — so speed carries momentum through watts and
// gradient changes. The force model is the same kreuzotter equation
// as computeInstantSpeed (PhysicsUtility.h): WattsForV is the forward
// direction (power needed at speed v), computeInstantSpeed solves
// the cubic for v at given power.

#if !defined(BICYCLESIM_H)
#define BICYCLESIM_H

#include <chrono>

#include "PhysicsUtility.h"

class BicycleWheel
{
    double m_massKG, m_I, m_equivalentMassKG;

public:

    BicycleWheel(double outerR,          // outer radius in meters (for circumference)
                 double innerR,          // inner rim radius
                 double weightKG,        // total wheel weight
                 double centerWeightKG,
                 double spokeCount,
                 double spokeMass);

    BicycleWheel() {}

    double KEMass()           const { return MassKG() + EquivalentMassKG(); }
    double MassKG()           const { return m_massKG; }
    double EquivalentMassKG() const { return m_equivalentMassKG; }
    double I()                const { return m_I; }
};

struct BicycleConstants
{
    double m_crr;                      // coefficient rolling resistance
    double m_Cm;                       // coefficient of powertrain loss
    double m_Cd;                       // coefficient of drag
    double m_A;                        // frontal area (m2)
    double m_T;                        // temp in kelvin
    double m_AltitudeCorrectionFactor; // inverse of altitude cp penalty

    BicycleConstants() :
        m_crr(0.004),
        m_Cm(1.0),
        m_Cd(1.0 - 0.0045),
        m_A(0.5),
        m_T(293.15),
        m_AltitudeCorrectionFactor(1.)
    {}

    BicycleConstants(double crr, double Cm, double Cd, double A, double T, double acf) :
        m_crr(crr), m_Cm(Cm), m_Cd(Cd), m_A(A), m_T(T), m_AltitudeCorrectionFactor(acf)
    {}
};

// Full bicycle configuration: the 28 SimBicyclePage values (see
// doc/core-view-split/bicyclesim-scope.md) plus the rider weight.
// Defaults are the SimBicyclePage defaults: bike mass values
// approximate a good current bike, wheels are shimano c40 with
// conti tubeless tires. trainer_altitude_m is unused in v1
// (hypoxia out); it is reserved for the altitude fit.
struct BicycleConfig
{
    double rider_weight_kg = 80;
    double bike_mass_without_wheels_g = 4000;
    double front_wheel_g = 739;
    double front_spoke_count = 24;
    double front_spoke_nipple_g = 5.6;
    double front_rim_g = 330;
    double front_rotor_g = 120;
    double front_skewer_g = 40;
    double front_tire_g = 220;
    double front_tube_sealant_g = 26;
    double front_outer_radius_m = 0.35;
    double front_rim_inner_radius_m = 0.3;
    double rear_wheel_g = 739;
    double rear_spoke_count = 24;
    double rear_spoke_nipple_g = 5.6;
    double rear_rim_g = 330;
    double rear_rotor_g = 120;
    double rear_skewer_g = 40;
    double rear_tire_g = 220;
    double rear_tube_sealant_g = 26;
    double rear_outer_radius_m = 0.35;
    double rear_rim_inner_radius_m = 0.3;
    double cassette_g = 190;
    double crr = 0.004;
    double Cm = 1.0;
    double Cd = 1.0 - 0.0045;
    double frontal_area_m2 = 0.5;
    double temp_kelvin = 293.15;
    double trainer_altitude_m = 0;
};

struct SpeedDistance
{
    double v, d;
};

class BicycleSimState
{
    enum Names { eWatts = 0, eAltitude, eSlope, eLastName };

    double v[eLastName];

public:

    double Watts()   const { return (v[eWatts]); }
    double Altitude()const { return (v[eAltitude]); }
    double Slope()   const { return (v[eSlope]); }

    double& Watts()        { return (v[eWatts]); }
    double& Altitude()     { return (v[eAltitude]); }
    double& Slope()        { return (v[eSlope]); }

    void Clear()
    {
        for (int i = 0; i < eLastName; i++)
        {
            v[i] = 0.0;
        }
    }

    BicycleSimState() {}
    BicycleSimState(double watts, double slope, double altitude)
    {
        Clear();
        this->Altitude() = altitude;
        this->Slope()    = slope;
        this->Watts()    = watts;
    }

    // Linear interpolate all fields to new sim state.
    static BicycleSimState Interpolate(const BicycleSimState &v0, const BicycleSimState &v1, double t0, double t1, double t)
    {
        if (t == t0) return v0;
        if (t == t1) return v1;

        double p = (t - t0) / (t1 - t0);

        BicycleSimState r;
        for (int i = 0; i < eLastName; i++)
        {
            r.v[i] = (v1.v[i] - v0.v[i]) * p + v0.v[i];
        }

        return r;
    }
};

class Bicycle
{
    // Invariant bicycle and rider properties.
    BicycleConstants m_constants;
    BicycleWheel m_frontWheel, m_rearWheel;
    double m_bicycleMassWithoutWheelsKG, m_riderMassKG;
    double m_KEMass; // effective mass used for computing kinetic energy

    // Dynamic data persisted for modelling momentum.
    BicycleSimState m_state;
    double          m_Velocity; // velocity in meters per second.

    bool            m_isFirstSample;
    std::chrono::high_resolution_clock::time_point m_prevSampleTime;

    void Init(const BicycleConfig &config);

    double SampleDT(); // Gather sample time stamp and return dt (in seconds)

    // Reduce power spikes by limiting power growth per sample period.
    double FilterWattIncrease(double watts);

    // Shared by both SampleSpeed overloads once dt is decided.
    SpeedDistance sampleSpeed(BicycleSimState &newState, double dt);

public:

    // All defaults + 80 kg rider — the old bicycle(NULL) path.
    Bicycle();
    explicit Bicycle(const BicycleConfig &config);
    SpeedDistance SampleSpeed(BicycleSimState &newState);

    // Explicit-dt overload: the primary API for core/game/tests.
    // Same rules as SampleDT: first tick dt = 0.01 s, dt > 1.0 s
    // clamped to 0.01 s, dt <= 0 means no change.
    SpeedDistance SampleSpeed(BicycleSimState &newState, double dt_seconds);

    // Reconfigure from a new config (replaces Reset(Context*)).
    void configure(const BicycleConfig &config);

    double MassKG() const;                   // Actual mass of bike and rider.
    double EquivalentMassKG() const;         // Additional mass due to rotational inertia
    double KEMass() const;                   // Total effective kinetic mass
    double RollingResistance() const { return m_constants.m_crr; }
    double WindResistance(double altitude) const { return m_constants.m_Cd * m_constants.m_A * AirDensity(altitude, m_constants.m_T); }

    const BicycleWheel &FrontWheel() const { return m_frontWheel; }
    const BicycleWheel &RearWheel() const { return m_rearWheel; }

    // Reset timer. This forces next sample to use 0.01s as its dt.
    void resettimer()
    {
        m_isFirstSample = true;
    }

    // Restart sim. Reset time and all history.
    void clear()
    {
        m_Velocity = 0.0;
        resettimer();
        this->m_state.Clear();
    }

    double KEFromV(double v) const;  // Compute kinetic energy of bike and rider (J) at speed v (in m/s)
    double VFromKE(double ke) const; // Compute velocity (m/s) from kinetic energy of bike and rider (J)

    double WattsForV(const BicycleSimState &simState, double v) const;

    // Compute new velocity in m/s after simState applied for dt seconds.
    double V(const BicycleSimState &simState,   // current sim state
             double v,                          // current velocity (m/s)
             double dt) const;                  // impulse duration

    double DV(const BicycleSimState &simState,  // current sim state
             double v) const;                        // current velocity

};


#endif // BICYCLESIM_H
