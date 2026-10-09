#include "SequencerRules.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace takt::sequencer
{
namespace
{
std::uint64_t mix(std::uint64_t value) noexcept
{
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

float safeVelocity(float value) noexcept
{
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}
}

double probabilityForActivation(std::uint64_t seed, int track,
                               std::uint64_t activation) noexcept
{
    const auto trackKey = static_cast<std::uint64_t>(std::max(track, 0));
    const auto bits = mix(seed ^ mix(trackKey + 0x9e3779b97f4a7c15ULL)
                         ^ mix(activation + 0xd1b54a32d192ed03ULL));
    return static_cast<double>(bits >> 11) * (1.0 / 9007199254740992.0);
}

Decision evaluate(const TrigRule& rule, const Context& context) noexcept
{
    bool matches = true;
    switch (rule.condition)
    {
        case Condition::Always:
        case Condition::Probability: break;
        case Condition::Cycle:
        {
            const int b = std::clamp(rule.cycleB, 1, 8);
            const int a = std::clamp(rule.cycleA, 1, b);
            matches = context.cycle % static_cast<std::uint64_t>(b)
                == static_cast<std::uint64_t>(a - 1);
            break;
        }
        case Condition::Previous:
            matches = context.previous.valid && context.previous.value;
            break;
        case Condition::Neighbor:
            // Track 1 has no preceding track; never wrap around to track 16.
            matches = context.track > 0 && context.neighbor.valid && context.neighbor.value;
            break;
        case Condition::First: matches = context.firstPatternCycle; break;
        case Condition::Last: matches = context.lastPatternCycle; break;
    }
    const double probability = std::isfinite(rule.probability)
        ? std::clamp(static_cast<double>(rule.probability), 0.0, 1.0) : 0.0;
    const bool probabilityMatches = probabilityForActivation(context.seed, context.track,
                                                             context.activation) < probability;
    if (rule.condition == Condition::Probability)
        matches = rule.inverted ? !probabilityMatches : probabilityMatches;
    else
    {
        if (rule.inverted) matches = !matches;
        matches = matches && probabilityMatches;
    }
    const bool fillMatches = rule.fill == Fill::Any
        || (rule.fill == Fill::On && context.fill)
        || (rule.fill == Fill::Off && !context.fill);

    Decision result;
    result.plays = matches && fillMatches;
    // Unconditional notes do not replace the last evaluated conditional lock.
    // PRE and inverted PRE read memory without changing it (manual examples).
    result.updatesMemory = rule.condition != Condition::Previous
        && (rule.condition != Condition::Always || probability < 1.0
            || rule.fill != Fill::Any);
    result.memoryValue = result.plays;
    return result;
}

void remember(ConditionMemory& memory, const Decision& decision) noexcept
{
    if (decision.updatesMemory)
    {
        memory.value = decision.memoryValue;
        memory.valid = true;
    }
}

double trackSpeed(int index) noexcept
{
    return trackSpeeds[static_cast<std::size_t>(std::clamp(index, 0, 6))];
}

double stepLengthBeats(int speedIndex) noexcept { return 0.25 / trackSpeed(speedIndex); }

double trackLengthBeats(int steps, int speedIndex) noexcept
{
    return std::clamp(steps, 1, 128) * stepLengthBeats(speedIndex);
}

double retrigIntervalBeats(int rateIndex) noexcept
{
    const auto denominator = retrigDenominators[static_cast<std::size_t>(std::clamp(rateIndex, 0, 16))];
    return 4.0 / denominator;
}

double beatsToSamples(double beats, double bpm, double sampleRate) noexcept
{
    if (!std::isfinite(beats) || !std::isfinite(bpm) || !std::isfinite(sampleRate)
        || beats < 0.0 || bpm <= 0.0 || sampleRate <= 0.0) return 0.0;
    return beats * (60.0 / bpm) * sampleRate;
}

int retrigCount(double gateBeats, const RetrigParams& params) noexcept
{
    if (!params.enabled) return 1;
    if (!std::isfinite(gateBeats) || gateBeats <= 0.0) return 0;
    // Tolerance keeps an exact interval multiple from adding a trigger at the
    // exclusive gate boundary through binary floating-point roundoff.
    const double count = std::ceil(gateBeats / retrigIntervalBeats(params.rateIndex) - 1.0e-10);
    return static_cast<int>(std::clamp(count, 1.0,
                                     static_cast<double>(std::numeric_limits<int>::max())));
}

double retrigOffsetBeats(int repeat, const RetrigParams& params) noexcept
{
    return std::max(repeat, 0) * retrigIntervalBeats(params.rateIndex);
}

float retrigVelocity(float initialVelocity, double elapsedBeats,
                     const RetrigParams& params) noexcept
{
    const float initial = safeVelocity(initialVelocity);
    if (!params.enabled) return initial;
    const double fade = std::isfinite(params.velocityFade)
        ? std::clamp(static_cast<double>(params.velocityFade), -64.0, 64.0) / 64.0 : 0.0;
    double progress = 0.0;
    if (params.fadeLengthBeats <= 0.0) progress = 1.0;
    else if (std::isfinite(params.fadeLengthBeats) && std::isfinite(elapsedBeats))
        progress = std::clamp(elapsedBeats / params.fadeLengthBeats, 0.0, 1.0);
    // The manual specifies endpoints, not a curve equation. Linear interpolation
    // is the independent DSP adaptation; VEL remains the maximum velocity.
    const double factor = fade >= 0.0 ? 1.0 - fade * (1.0 - progress)
                                     : 1.0 + fade * progress;
    return safeVelocity(static_cast<float>(initial * factor));
}

double legacyRetrigOffsetBeats(int repeat, int count, double stepDuration) noexcept
{
    if (!std::isfinite(stepDuration) || stepDuration < 0.0) return 0.0;
    const int validCount = std::clamp(count, 1, 8);
    return stepDuration * std::clamp(repeat, 0, validCount - 1) / validCount;
}
}
