#include "engine/AmpFilter.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
thread_local bool trackAllocations = false;
thread_local std::size_t allocationCount = 0;
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void releaseAllocation(void* pointer) noexcept { std::free(pointer); }
}
void* operator new(std::size_t size)
{
    if (trackAllocations) ++allocationCount;
    if (auto* pointer = std::malloc(std::max<std::size_t>(size, 1))) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { releaseAllocation(pointer); }
void operator delete[](void* pointer) noexcept { releaseAllocation(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { releaseAllocation(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { releaseAllocation(pointer); }

namespace
{
constexpr double rate = 48000.0, pi = 3.14159265358979323846;
using namespace takt;
void require(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}
double power(const std::vector<float>& values, int first = 0)
{
    double sum = 0;
    for (std::size_t i = static_cast<std::size_t>(first); i < values.size(); ++i) sum += values[i] * values[i];
    return sum;
}
float advance(Envelope& envelope, const EnvelopeParams& params, int samples)
{
    float result = 0;
    for (int i = 0; i < samples; ++i) result = envelope.next(params, rate);
    return result;
}

void testEnvelopes()
{
    Envelope envelope;
    EnvelopeParams p; p.mode = EnvelopeMode::Ahd; p.attack = .01f; p.hold = .01f; p.decay = .02f;
    envelope.trigger(p);
    require(advance(envelope, p, 240) > .45f && envelope.value() < .55f, "AHD attack rises over the specified duration");
    advance(envelope, p, 260);
    envelope.noteOff(p);
    require(advance(envelope, p, 240) == 1.0f, "fixed AHD hold ignores NOTE OFF");
    advance(envelope, p, 1600);
    require(!envelope.isActive() && envelope.value() == 0, "AHD decay completes and stops");
    p.holdNote = true;
    envelope.trigger(p); advance(envelope, p, 5000);
    require(envelope.value() == 1.0f, "AHD HOLD NOTE sustains until NOTE OFF");
    envelope.noteOff(p); advance(envelope, p, 1000);
    require(!envelope.isActive(), "AHD HOLD NOTE enters decay on release");
    p.mode = EnvelopeMode::Adsr; p.sustain = .35f; p.release = .02f;
    envelope.trigger(p); advance(envelope, p, 3000);
    require(std::abs(envelope.value() - .35f) < .001f && envelope.isActive(), "ADSR holds the sustain level");
    envelope.noteOff(p); advance(envelope, p, 1000);
    require(!envelope.isActive() && envelope.value() == 0, "ADSR release completes after NOTE OFF");
    envelope.trigger(p); advance(envelope, p, 240); p.reset = false;
    envelope.trigger(p);
    require(envelope.next(p, rate) > .5f, "RSET OFF starts a retriggered attack at its current level");
    p.reset = true; envelope.trigger(p);
    require(envelope.next(p, rate) < .01f, "RSET ON restarts attack from zero");
    envelope.reset();
    require(!envelope.isActive() && envelope.next(p, rate) == 0, "reset leaves a silent inactive envelope");
}

std::vector<float> filtered(FilterParams params, double inputHz, float cutoff = 1000,
                            float envelope = 0, int note = 60)
{
    StereoFilter filter; filter.prepare(rate);
    std::vector<float> output(12000);
    for (std::size_t i = 0; i < output.size(); ++i)
    {
        float left = static_cast<float>(.05 * std::sin(2 * pi * inputHz * i / rate));
        float right = left;
        filter.process(left, right, params, cutoff, .1f, envelope, note);
        output[i] = left;
        require(std::isfinite(left) && std::isfinite(right), "filters remain finite");
    }
    return output;
}

void testFilterModesAndRouting()
{
    FilterParams p; p.machine = FilterMachine::Multimode;
    const auto lp = filtered(p, 5000);
    p.machine = FilterMachine::Lowpass4;
    const auto lp4 = filtered(p, 5000);
    require(power(lp4, 1000) < power(lp, 1000) * .05, "LP4 has a steeper stop band than the two-pole multimode filter");
    p.machine = FilterMachine::Multimode; p.type = 1;
    const auto high = filtered(p, 5000), low = filtered(p, 100);
    require(power(high, 1000) > power(low, 1000) * 100, "multimode highpass rejects low frequencies");
    p.type = .5f;
    const auto band = filtered(p, 1000), distant = filtered(p, 100);
    require(power(band, 1000) > power(distant, 1000) * 20, "multimode center setting selects a bandpass response");
    p.machine = FilterMachine::Eq; p.eqGain = 12; p.eqQ = 2;
    const auto boost = filtered(p, 1000);
    p.eqGain = -12;
    const auto cut = filtered(p, 1000);
    require(power(boost, 1000) > power(cut, 1000) * 100, "parametric EQ boosts and cuts the selected frequency");
    p.eqGain = 0; p.base = 80; p.width = 127;
    const auto baseLow = filtered(p, 100), baseHigh = filtered(p, 5000);
    require(power(baseHigh, 1000) > power(baseLow, 1000) * 100, "BASE independently creates a highpass with WIDTH at maximum");
    p.base = 0; p.width = 50; p.bwPre = false;
    const auto widthLow = filtered(p, 100), widthHigh = filtered(p, 5000);
    require(power(widthLow, 1000) > power(widthHigh, 1000) * 100, "WIDTH creates a lowpass after the machine when BW.RT is POST");
    p = {}; p.machine = FilterMachine::Multimode; p.keytrack = 1;
    const auto root = filtered(p, 5000, 500, 0, 60), octave = filtered(p, 5000, 500, 0, 72);
    require(power(octave, 1000) > power(root, 1000) * 10, "keytracking raises cutoff by an octave for twelve semitones");
    p.keytrack = 0; p.envDepth = 64;
    const auto dark = filtered(p, 5000, 500), open = filtered(p, 5000, 500, 1);
    require(power(open, 1000) > power(dark, 1000) * 100, "filter envelope depth changes cutoff with the documented polarity");
}

void testCombAndRateReduction()
{
    const auto impulse = [](FilterMachine machine)
    {
        StereoFilter filter; filter.prepare(rate);
        FilterParams p; p.machine = machine; p.combFeedback = .8f; p.combLowpassHz = 20000;
        std::vector<float> output(500);
        for (std::size_t i = 0; i < output.size(); ++i)
        {
            float left = i == 0 ? .5f : 0, right = left;
            filter.process(left, right, p, 1000, 0);
            output[i] = left;
        }
        return output;
    };
    const auto plus = impulse(FilterMachine::CombPlus), minus = impulse(FilterMachine::CombMinus);
    require(plus[48] > .01f && minus[48] < -.01f, "Comb+ and Comb- use opposite feedback polarity at the pitch interval");
    require(plus[96] > 0 && minus[96] > 0 && power(plus, 40) > .001,
            "comb buffers feed their delayed output back to create a resonant tail");
    StereoTrackFx fx;
    float left = .1f, right = -.1f; fx.rateReduction(left, right, 32);
    for (int i = 0; i < 3; ++i)
    {
        left = .8f; right = -.8f; fx.rateReduction(left, right, 32);
        require(left == .1f && right == -.1f, "SRR holds both channels for its reduction interval");
    }
    left = .5f; right = -.5f; fx.rateReduction(left, right, 32);
    require(left == .5f && right == -.5f, "SRR refreshes its sample at the next reduction interval");
    left = .3f; right = -.2f; fx.rateReduction(left, right, 0);
    require(left == .3f && right == -.2f, "SRR zero is an exact bypass");
    left = .02f; right = -.02f; StereoTrackFx::overdrive(left, right, 1);
    require(left > .3f && right < -.3f, "overdrive produces saturation on both channels");
}

void testChorusAndRealtime()
{
    Chorus chorus; chorus.prepare(rate);
    StereoFilter filter; filter.prepare(rate);
    FilterParams filterParams; filterParams.machine = FilterMachine::CombPlus;
    ChorusParams p; p.depth = 1; p.speed = 2; p.width = 1;
    std::vector<float> left(24000), right(24000);
    allocationCount = 0; trackAllocations = true;
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        float sampleL = static_cast<float>(.05 * std::sin(2 * pi * 440 * i / rate)), sampleR = sampleL;
        chorus.process(sampleL, sampleR, p, left[i], right[i]);
        filter.process(sampleL, sampleR, filterParams, 500, .9f);
    }
    trackAllocations = false;
    require(allocationCount == 0, "comb filtering and stereo chorus allocate no memory during processing");
    require(power(left) > 1 && power(right) > 1 && left != right, "chorus creates audible stereo movement with independent delays");
    require(std::all_of(left.begin(), left.begin() + 200, [](float value) { return value == 0; }), "chorus wet signal obeys its delay latency");
    chorus.reset(); float wetL, wetR;
    chorus.process(0, 0, p, wetL, wetR);
    require(wetL == 0 && wetR == 0, "chorus reset clears its tail");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    filterParams.type = filterParams.base = filterParams.width = filterParams.eqGain = nan;
    p.depth = p.speed = p.width = p.highpassHz = nan;
    for (int i = 0; i < 1000; ++i)
    {
        float badL = nan, badR = std::numeric_limits<float>::infinity();
        filter.process(badL, badR, filterParams, nan, nan, nan);
        chorus.process(badL, badR, p, wetL, wetR);
        require(std::isfinite(badL) && std::isfinite(badR) && std::isfinite(wetL) && std::isfinite(wetR),
                "invalid helper parameters and samples never produce nonfinite audio");
    }
}
}

int main()
{
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"AHD, ADSR, HOLD NOTE, release and envelope reset", testEnvelopes},
        {"multimode, LP4, EQ, base-width, keytrack and filter envelope", testFilterModesAndRouting},
        {"comb polarity and feedback, sample rate reduction and overdrive", testCombAndRateReduction},
        {"stereo chorus, realtime allocation and finite inputs", testChorusAndRealtime}
    };
    int failed = 0;
    for (const auto& test : tests)
    {
        try { test.run(); std::cout << "PASS " << test.name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.name << ": " << error.what() << '\n'; }
    }
    return failed == 0 ? 0 : 1;
}
