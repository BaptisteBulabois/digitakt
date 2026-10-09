#include "engine/SequencerRules.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

namespace
{
thread_local bool countAllocations = false;
thread_local std::size_t allocations = 0;
}

void* operator new(std::size_t size)
{
    if (countAllocations) ++allocations;
    if (void* pointer = std::malloc(size == 0 ? 1 : size)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace
{
using namespace takt::sequencer;
void require(bool passes, const std::string& message)
{
    if (!passes) throw std::runtime_error(message);
}
bool close(double actual, double expected) { return std::abs(actual - expected) < 1.0e-9; }

void testProbability()
{
    Context context;
    TrigRule rule;
    rule.condition = Condition::Probability;
    rule.probability = 0.5f;
    int accepted = 0, changes = 0;
    bool previous = false;
    for (std::uint64_t occurrence = 0; occurrence < 10000; ++occurrence)
    {
        context.activation = occurrence;
        const bool decision = evaluate(rule, context).plays;
        require(decision == evaluate(rule, context).plays,
                "overlapping block scans reuse a draw for one occurrence");
        const double draw = probabilityForActivation(context.seed, context.track, occurrence);
        require(draw >= 0.0 && draw < 1.0, "probability draw lies in [0,1)");
        accepted += decision ? 1 : 0;
        changes += occurrence > 0 && decision != previous ? 1 : 0;
        previous = decision;
        rule.inverted = true;
        require(evaluate(rule, context).plays != decision,
                "inverted probability complements the exact same random draw");
        rule.inverted = false;
    }
    require(accepted > 4500 && accepted < 5500 && changes > 3500,
            "probability is refreshed over repeated loops with a balanced seeded draw");

    context.activation = 20;
    const double original = probabilityForActivation(context.seed, 0, 20);
    require(original != probabilityForActivation(context.seed + 1, 0, 20)
            && original != probabilityForActivation(context.seed, 1, 20),
            "independent seeds and tracks have independent draws");
    rule.probability = 0.0f;
    require(!evaluate(rule, context).plays, "zero probability never triggers");
    rule.probability = 1.0f;
    require(evaluate(rule, context).plays, "full probability always triggers");
    rule.probability = std::numeric_limits<float>::quiet_NaN();
    require(!evaluate(rule, context).plays, "nonfinite probability cannot emit a trig");
}

void testCyclesFirstLastAndFill()
{
    TrigRule rule;
    rule.condition = Condition::Cycle;
    rule.cycleA = 2;
    rule.cycleB = 4;
    Context context;
    for (std::uint64_t cycle = 0; cycle < 20; ++cycle)
    {
        context.cycle = cycle;
        const bool expected = cycle % 4 == 1;
        require(evaluate(rule, context).plays == expected, "A:B is one-based and repeats every B cycles");
        rule.inverted = true;
        require(evaluate(rule, context).plays != expected, "inverted A:B complements every repeat");
        rule.inverted = false;
    }
    rule.condition = Condition::First;
    context.firstPatternCycle = true;
    require(evaluate(rule, context).plays, "1ST plays during the first pattern cycle");
    context.firstPatternCycle = false;
    require(!evaluate(rule, context).plays, "1ST stops after the first cycle");
    rule.inverted = true;
    require(evaluate(rule, context).plays, "inverted 1ST plays subsequent cycles");
    rule.inverted = false;
    rule.condition = Condition::Last;
    context.lastPatternCycle = false;
    require(!evaluate(rule, context).plays, "LST is false without a pending final pattern cycle");
    context.lastPatternCycle = true;
    require(evaluate(rule, context).plays, "LST covers the final pattern cycle, not just its last step");
    rule.inverted = true;
    require(!evaluate(rule, context).plays, "inverted LST excludes the final pattern cycle");

    rule = TrigRule{};
    rule.fill = Fill::On;
    context.fill = false;
    require(!evaluate(rule, context).plays, "FILL ON excludes ordinary playback");
    context.fill = true;
    require(evaluate(rule, context).plays, "FILL ON plays during a fill");
    rule.fill = Fill::Off;
    require(!evaluate(rule, context).plays, "FILL OFF excludes fill playback");
    context.fill = false;
    require(evaluate(rule, context).plays, "FILL OFF plays outside fill");
    rule.condition = Condition::Cycle;
    rule.cycleA = 2;
    rule.cycleB = 4;
    context.cycle = 1;
    context.fill = true;
    rule.fill = Fill::On;
    require(evaluate(rule, context).plays, "FILL combines with A:B instead of replacing it");
    context.cycle = 2;
    require(!evaluate(rule, context).plays, "FILL cannot bypass a false condition");
}

void testPreviousAndNeighbor()
{
    Context context;
    TrigRule rule;
    rule.condition = Condition::Previous;
    require(!evaluate(rule, context).plays, "PRE has no true predecessor at playback start");
    rule.inverted = true;
    require(evaluate(rule, context).plays, "inverted PRE is true without a true predecessor");

    // Manual examples use 50% then NOT PRE, NOT PRE, PRE. The PRE family
    // never replaces the original conditional result, even after a false note.
    for (bool source : {false, true})
    {
        context.previous = {source, true};
        rule.inverted = true;
        for (int i = 0; i < 2; ++i)
        {
            const auto result = evaluate(rule, context);
            require(result.plays != source && !result.updatesMemory,
                    "consecutive inverted PRE trigs retain the original conditional outcome");
            remember(context.previous, result);
        }
        rule.inverted = false;
        const auto result = evaluate(rule, context);
        require(result.plays == source, "PRE reads the same original outcome after inverted PRE");
    }

    rule = TrigRule{};
    const auto unconditional = evaluate(rule, context);
    require(!unconditional.updatesMemory, "an unconditional note does not overwrite PRE memory");
    rule.condition = Condition::Cycle;
    rule.cycleA = 2;
    rule.cycleB = 2;
    context.cycle = 0;
    remember(context.previous, evaluate(rule, context));
    require(context.previous.valid && !context.previous.value,
            "false conditional locks update memory even when a note does not play");
    context.cycle = 1;
    remember(context.previous, evaluate(rule, context));
    require(context.previous.value, "a later true conditional lock replaces memory");

    rule.condition = Condition::Neighbor;
    context.neighbor = {true, true};
    context.track = 0;
    require(!evaluate(rule, context).plays, "track one has no neighbor; it does not wrap to track sixteen");
    context.track = 1;
    require(evaluate(rule, context).plays, "NEI reads the preceding track's latest evaluated condition");
    context.neighbor.valid = false;
    require(!evaluate(rule, context).plays, "NEI is false if the preceding track has no conditional result");
    rule.inverted = true;
    require(evaluate(rule, context).plays, "inverted NEI plays without a true neighboring conditional result");
}

void testRetrigsAndTrackTiming()
{
    require(retrigDenominators.size() == 17 && trackSpeeds.size() == 7,
            "rate and speed tables contain the documented choices");
    RetrigParams params;
    require(retrigCount(0.25, params) == 1, "disabled retrigs emit only the initial note");
    params.enabled = true;
    params.rateIndex = 12; // 1/32
    require(close(retrigIntervalBeats(12), 0.125) && retrigCount(0.25, params) == 2,
            "1/32 yields two triggers per sixteenth-note gate");
    require(close(retrigOffsetBeats(1, params), 0.125), "RATE uses musical beat spacing");
    params.rateIndex = 8; // 1/12
    require(retrigCount(4.0, params) == 12, "1/12 triplets produce twelve triggers per bar");
    params.rateIndex = 16; // 1/80
    require(retrigCount(0.25, params) == 5, "1/80 includes initial note and excludes gate end");
    require(retrigCount(0.0, params) == 0
            && retrigCount(std::numeric_limits<double>::infinity(), params) == 0,
            "unbounded retrigs must be streamed rather than materialized");
    params.fadeLengthBeats = 1.0;
    params.velocityFade = -64.0f;
    require(close(retrigVelocity(0.8f, 0.0, params), 0.8f)
            && close(retrigVelocity(0.8f, 1.0, params), 0.0), "VFAD -64 fully fades out over LEN");
    params.velocityFade = -32.0f;
    require(close(retrigVelocity(0.8f, 1.0, params), 0.4f), "VFAD -32 finishes at half VEL");
    params.velocityFade = 32.0f;
    require(close(retrigVelocity(0.8f, 0.0, params), 0.4f)
            && close(retrigVelocity(0.8f, 1.0, params), 0.8f), "VFAD +32 fades from half VEL to VEL");
    params.velocityFade = 64.0f;
    require(close(retrigVelocity(0.8f, 0.0, params), 0.0)
            && close(retrigVelocity(0.8f, 2.0, params), 0.8f), "VFAD +64 fades in and clamps after LEN");
    params.velocityFade = 0.0f;
    require(close(retrigVelocity(0.8f, 2.0, params), 0.8f), "VFAD zero leaves VEL unchanged");
    const int before = retrigCount(1.0, params);
    params.fadeLengthBeats = 0.125;
    require(retrigCount(1.0, params) == before, "LEN controls fade duration independently of note gate");

    for (int rate = 0; rate < 17; ++rate)
    {
        const double interval = retrigIntervalBeats(rate);
        require(close(beatsToSamples(interval, 120.0, 48000.0), interval * 24000.0),
                "retrig intervals convert accurately at 120 BPM");
        require(close(beatsToSamples(interval, 60.0, 48000.0), interval * 48000.0),
                "half tempo doubles the sample interval");
    }
    require(close(stepLengthBeats(4), 0.25) && close(trackLengthBeats(16, 4), 4.0),
            "sixteen nominal steps form a four-beat pattern");
    require(close(trackLengthBeats(16, 6), 2.0) && close(trackLengthBeats(16, 0), 32.0),
            "per-track 2x and 1/8x speeds scale pattern length");
    require(close(trackLengthBeats(12, 3), 4.0), "3/4x supports triplet track timing");
    require(close(legacyRetrigOffsetBeats(1, 2, 0.3), 0.15),
            "legacy count retrigs retain their evenly divided swung step duration");
    require(beatsToSamples(1.0, 0.0, 48000.0) == 0.0
            && close(trackLengthBeats(200, 4), 32.0), "timing helpers bound invalid input");
}

void testNoAllocations()
{
    Context context;
    TrigRule rule;
    rule.condition = Condition::Probability;
    rule.probability = 0.5f;
    RetrigParams params;
    params.enabled = true;
    double sink = 0.0;
    allocations = 0;
    countAllocations = true;
    for (int i = 0; i < 2048; ++i)
    {
        context.activation = static_cast<std::uint64_t>(i);
        const auto decision = evaluate(rule, context);
        remember(context.previous, decision);
        sink += retrigVelocity(0.8f, 0.125, params)
            + retrigOffsetBeats(i % 8, params) + retrigCount(4.0, params)
            + trackLengthBeats(i % 128, i % 7);
    }
    countAllocations = false;
    require(allocations == 0 && sink > 0.0, "audio-side rule, rate and fade calls allocate no memory");
}
}

int main()
{
    try
    {
        testProbability();
        testCyclesFirstLastAndFill();
        testPreviousAndNeighbor();
        testRetrigsAndTrackTiming();
        testNoAllocations();
        std::cout << "PASS sequencer conditions, probability, retrigs, track timing and allocation checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
