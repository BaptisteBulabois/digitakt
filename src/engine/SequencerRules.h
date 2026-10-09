#pragma once

#include <array>
#include <cstdint>

namespace takt::sequencer
{
// Digitakt II OS 1.17 manual: sections 10.7, 10.8.3 and 11.2/11.3.
// These helpers are independent of the scheduler and never allocate.
enum class Condition { Always, Probability, Cycle, Previous, Neighbor, First, Last };
enum class Fill { Any, On, Off };

struct TrigRule
{
    Condition condition = Condition::Always;
    bool inverted = false;
    int cycleA = 1, cycleB = 1; // Hardware A:B, one-based; B is restricted to 1..8.
    float probability = 1.0f; // General PROB applies alongside the selected condition.
    Fill fill = Fill::Any;
};

struct ConditionMemory
{
    bool value = false, valid = false;
};

struct Context
{
    std::uint64_t cycle = 0; // Zero-based track/pattern repeat, not the step index.
    bool firstPatternCycle = true, lastPatternCycle = false, fill = false;
    ConditionMemory previous, neighbor;
    std::uint64_t seed = 0x74616b742d696900ULL;
    std::uint64_t activation = 0; // Unique absolute occurrence, including each loop.
    int track = 0;
};

struct Decision
{
    bool plays = false;
    bool updatesMemory = false;
    bool memoryValue = false;
};

// A keyed draw is reused if a scheduler examines the same occurrence in several
// blocks. A NEW occurrence must supply a new activation, even for the same step.
double probabilityForActivation(std::uint64_t seed, int track,
                               std::uint64_t activation) noexcept;
Decision evaluate(const TrigRule&, const Context&) noexcept;
void remember(ConditionMemory&, const Decision&) noexcept;

inline constexpr std::array<int, 17> retrigDenominators{
    1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48, 64, 80
};
inline constexpr std::array<double, 7> trackSpeeds{
    0.125, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0
};

double trackSpeed(int index) noexcept;
double stepLengthBeats(int speedIndex) noexcept;
double trackLengthBeats(int steps, int speedIndex) noexcept;
double retrigIntervalBeats(int rateIndex) noexcept;
double beatsToSamples(double beats, double bpm, double sampleRate) noexcept;

struct RetrigParams
{
    bool enabled = false;
    int rateIndex = 9; // 1/16 => one trigger per nominal sixteenth note.
    double fadeLengthBeats = 0.25; // LEN controls the velocity envelope, not the gate.
    float velocityFade = 0.0f; // VFAD -64..64.
};

// Infinite gates cannot be materialized: query the interval and schedule only the
// relevant block. Finite counts include the initial note and exclude the gate end.
int retrigCount(double gateBeats, const RetrigParams&) noexcept;
double retrigOffsetBeats(int repeat, const RetrigParams&) noexcept;
float retrigVelocity(float initialVelocity, double elapsedBeats,
                     const RetrigParams&) noexcept;

// Existing states use 1..8 evenly spaced triggers within the (swung) step.
// Keep that behavior separate from hardware RATE/LEN/VFAD.
double legacyRetrigOffsetBeats(int repeat, int count, double stepDuration) noexcept;
}
