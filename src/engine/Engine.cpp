#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace takt
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double stepBeats = 0.25;

template <typename T> T limit(T value, T low, T high)
{
    return std::isfinite(value) ? std::clamp(value, low, high) : low;
}

float finite(float value) { return std::isfinite(value) ? value : 0.0f; }

std::uint64_t hash(std::uint64_t value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

float random01(std::uint64_t value)
{
    return static_cast<float>(hash(value) >> 40) / 16777216.0f;
}

float interpolate(const std::vector<float>& data, double position, std::size_t first, std::size_t last, bool loop)
{
    if (data.empty()) return 0.0f;
    const auto sourceIndex = static_cast<std::size_t>(std::max(position, 0.0));
    const auto nextIndex = sourceIndex + 1 >= last ? (loop ? first : last - 1) : sourceIndex + 1;
    const auto i = std::min(sourceIndex, data.size() - 1);
    const auto j = std::min(nextIndex, data.size() - 1);
    const float fraction = static_cast<float>(position - std::floor(position));
    return finite(data[i]) + fraction * (finite(data[j]) - finite(data[i]));
}

float filter(float input, float& low, float& band, float g, float a)
{
    const float v1 = a * (band + g * (input - low));
    const float v2 = low + g * v1;
    band = finite(2.0f * v1 - band);
    low = finite(2.0f * v2 - low);
    if (std::abs(band) < 1.0e-20f) band = 0.0f;
    if (std::abs(low) < 1.0e-20f) low = 0.0f;
    return finite(v2);
}

template <typename Enum> Enum enumLimit(Enum value, int last)
{
    return static_cast<Enum>(std::clamp(static_cast<int>(value), 0, last));
}

bool reverseMode(PlaybackMode mode)
{
    return mode == PlaybackMode::Reverse || mode == PlaybackMode::ReverseLoop;
}

bool loopMode(PlaybackMode mode)
{
    return mode == PlaybackMode::ForwardLoop || mode == PlaybackMode::ReverseLoop;
}

bool hasLfo(const TrackParams& p)
{
    for (const auto& lfo : p.lfos)
        if (lfo.depth != 0.0f && lfo.destination != LfoDestination::None) return true;
    return false;
}

bool hasExtendedDsp(const TrackParams& p)
{
    // Old projects retain the original arithmetic and filter states until a new
    // audio control is explicitly used. SRC controls remain inert on Legacy.
    return p.amplitudeEnvelope.mode != EnvelopeMode::Legacy || p.ampVolume != 1.0f
        || p.bitReduction < 16.0f || p.filter.machine != FilterMachine::Prototype
        || p.filter.base != 0.0f || p.filter.width != 127.0f || p.filter.keytrack != 0.0f
        || p.filter.envDepth != 0.0f || p.trackFx.srr != 0.0f || !p.trackFx.drivePre;
}

void sanitizeEnvelope(EnvelopeParams& envelope)
{
    envelope.mode = enumLimit(envelope.mode, 2);
    envelope.attack = limit(envelope.attack, 0.0f, 30.0f);
    envelope.hold = limit(envelope.hold, 0.0f, 60.0f);
    envelope.decay = limit(envelope.decay, 0.0f, 60.0f);
    envelope.sustain = limit(envelope.sustain, 0.0f, 1.0f);
    envelope.release = limit(envelope.release, 0.0f, 60.0f);
}

float lfoWave(LfoWave wave, double phase, std::int64_t cycle, int seed)
{
    const double x = phase - std::floor(phase);
    switch (wave)
    {
        case LfoWave::Triangle: return static_cast<float>(x < 0.25 ? 4.0 * x : (x < 0.75 ? 2.0 - 4.0 * x : 4.0 * x - 4.0));
        case LfoWave::Sine: return static_cast<float>(std::sin(2.0 * pi * x));
        case LfoWave::Square: return x < 0.5 ? 1.0f : -1.0f;
        case LfoWave::Saw: return static_cast<float>(1.0 - 2.0 * x);
        case LfoWave::Exponential: return static_cast<float>((std::exp(-6.0 * x) - std::exp(-6.0)) / (1.0 - std::exp(-6.0)));
        case LfoWave::Ramp: return static_cast<float>(x);
        case LfoWave::Random: return random01(static_cast<std::uint64_t>(cycle) + static_cast<std::uint64_t>(seed) * 7919) * 2.0f - 1.0f;
    }
    return 0.0f;
}

double wrapped(double position, std::size_t first, std::size_t last)
{
    const double size = static_cast<double>(last - first);
    return first + std::fmod(std::fmod(position - first, size) + size, size);
}
}

float masterSoftClip(float input) noexcept
{
    // This approximation is bounded only on [-3, 3]. Clamp before squaring so
    // very large finite sums cannot overflow or escape the master peak limit.
    const float x = std::clamp(finite(input), -3.0f, 3.0f);
    const float square = x * x;
    return std::clamp(x * (27.0f + square) / (27.0f + 9.0f * square), -1.0f, 1.0f);
}

void Engine::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = limit(sampleRate, 8000.0, 384000.0);
    blockSize_ = std::clamp(maxBlockSize, 1, 4096);
    const auto delaySize = static_cast<std::size_t>(std::ceil(sampleRate_ * 12.0)) + 2;
    delayL_.assign(delaySize, 0.0f);
    delayR_.assign(delaySize, 0.0f);
    const double lengths[] = { 0.0297, 0.0371, 0.0411, 0.0437 };
    for (std::size_t i = 0; i < reverb_.size(); ++i)
        reverb_[i].data.assign(static_cast<std::size_t>(sampleRate_ * lengths[i]) + 1, 0.0f);
    for (auto& item : filters_) item.prepare(sampleRate_);
    chorus_.prepare(sampleRate_);
    prepared_ = true;
    reset();
}

void Engine::reset()
{
    voices_.fill(Voice{});
    for (auto& envelope : amplitudeEnvelopes_) envelope.reset();
    for (auto& envelope : filterEnvelopes_) envelope.reset();
    for (auto& item : filters_) item.reset();
    for (auto& item : trackFx_) item.reset();
    chorus_.reset();
    lfoStates_ = {};
    currentSteps_.fill(0);
    internalPpq_ = 0.0;
    retrigTrains_ = {};
    conditionMemory_ = {};
    firstCycles_.fill(-1);
    probabilityEpoch_ = 0;
    sequenceOriginPpq_ = expectedPpq_ = 0.0;
    wasPlaying_ = pendingOrigin_ = false;
    delayCursor_ = 0;
    std::fill(delayL_.begin(), delayL_.end(), 0.0f);
    std::fill(delayR_.begin(), delayR_.end(), 0.0f);
    for (auto& line : reverb_)
    {
        std::fill(line.data.begin(), line.data.end(), 0.0f);
        line.cursor = 0;
        line.damp = 0.0f;
    }
}

void Engine::setSample(int track, std::shared_ptr<const Sample> sample)
{
    if (track < 0 || track >= numTracks) return;
    if (samples_[track] == sample) return;
    voices_[track] = Voice{};
    amplitudeEnvelopes_[track].reset();
    filterEnvelopes_[track].reset();
    filters_[track].reset();
    trackFx_[track].reset();
    samples_[track] = std::move(sample);
}

void Engine::setTrackParams(int track, const TrackParams& value)
{
    if (track < 0 || track >= numTracks) return;
    auto p = value;
    p.gain = limit(p.gain, 0.0f, 2.0f);
    p.pan = limit(p.pan, -1.0f, 1.0f);
    p.pitch = limit(p.pitch, -48.0f, 48.0f);
    p.cutoff = limit(p.cutoff, 20.0f, 20000.0f);
    p.resonance = limit(p.resonance, 0.0f, 0.98f);
    p.attack = limit(p.attack, 0.0001f, 10.0f);
    p.decay = limit(p.decay, 0.005f, 30.0f);
    p.drive = limit(p.drive, 0.0f, 1.0f);
    p.bitDepth = limit(p.bitDepth, 1.0f, 24.0f);
    p.start = limit(p.start, 0.0f, 1.0f);
    p.end = limit(p.end, p.start, 1.0f);
    p.delaySend = limit(p.delaySend, 0.0f, 1.0f);
    p.reverbSend = limit(p.reverbSend, 0.0f, 1.0f);
    p.machine = enumLimit(p.machine, 6);
    p.playback = enumLimit(p.playback, 3);
    p.segmentMode = enumLimit(p.segmentMode, 3);
    p.sourceLength = limit(p.sourceLength, 0.0f, 1.0f);
    p.loopPosition = limit(p.loopPosition, 0.0f, 1.0f);
    p.bars = limit(p.bars, 0.0625f, 64.0f);
    p.sampleLevel = limit(p.sampleLevel, 0.0f, 2.0f);
    p.ampVolume = limit(p.ampVolume, 0.0f, 2.0f);
    p.bitReduction = limit(p.bitReduction, 1.0f, 16.0f);
    p.segmentSize = limit(p.segmentSize, 0.001f, 1.0f);
    p.sliceCount = std::clamp(p.sliceCount, 1, maxSlices);
    p.slice = std::clamp(p.slice, 0, p.sliceCount - 1);
    p.sliceLength = std::clamp(p.sliceLength, 1, maxSlices);
    p.speedIndex = std::clamp(p.speedIndex, 0, 6);
    sanitizeEnvelope(p.amplitudeEnvelope);
    p.filter.machine = enumLimit(p.filter.machine, 6);
    p.filter.type = limit(p.filter.type, 0.0f, 1.0f);
    p.filter.eqGain = limit(p.filter.eqGain, -24.0f, 24.0f);
    p.filter.eqQ = limit(p.filter.eqQ, 0.1f, 20.0f);
    p.filter.combFeedback = limit(p.filter.combFeedback, 0.0f, 0.98f);
    p.filter.combLowpassHz = limit(p.filter.combLowpassHz, 20.0f, 20000.0f);
    p.filter.base = limit(p.filter.base, 0.0f, 127.0f);
    p.filter.width = limit(p.filter.width, 0.0f, 127.0f);
    p.filter.keytrack = limit(p.filter.keytrack, 0.0f, 1.0f);
    p.filter.envDepth = limit(p.filter.envDepth, -128.0f, 128.0f);
    p.filter.envDelay = limit(p.filter.envDelay, 0.0f, 30.0f);
    sanitizeEnvelope(p.filter.envelope);
    p.trackFx.srr = limit(p.trackFx.srr, 0.0f, 127.0f);
    p.trackFx.chorusSend = limit(p.trackFx.chorusSend, 0.0f, 1.0f);
    for (auto& point : p.slicePoints)
    {
        point.start = limit(point.start, 0.0f, 1.0f);
        point.end = limit(point.end, point.start, 1.0f);
        point.loop = limit(point.loop, point.start, std::max(point.start, point.end));
    }
    for (auto& lfo : p.lfos)
    {
        lfo.speed = limit(lfo.speed, -64.0f, 63.0f);
        lfo.multiplier = limit(lfo.multiplier, 1.0f / 128.0f, 2048.0f);
        lfo.fade = limit(lfo.fade, -64.0f, 63.0f);
        lfo.phase = limit(lfo.phase, 0.0f, 127.0f);
        lfo.depth = limit(lfo.depth, -128.0f, 127.0f);
        lfo.wave = enumLimit(lfo.wave, 6);
        lfo.mode = enumLimit(lfo.mode, 4);
        lfo.destination = enumLimit(lfo.destination, 14);
    }
    const bool machineChanged = params_[track].machine != p.machine;
    const bool extendedActivated = !(hasLfo(params_[track]) || hasExtendedDsp(params_[track]))
                                    && (hasLfo(p) || hasExtendedDsp(p));
    const bool envelopeActivated = params_[track].amplitudeEnvelope.mode != p.amplitudeEnvelope.mode;
    const bool filterChanged = params_[track].filter.machine != p.filter.machine;
    params_[track] = p;
    modulated_[track] = p;
    lfoEnabled_[track] = hasLfo(p);
    extendedDsp_[track] = hasExtendedDsp(p);
    if (machineChanged) voices_[track] = Voice{};
    else if (extendedActivated && voices_[track].active && p.machine == Machine::Legacy)
        configureMachineRegion(track, voices_[track], p);
    if (envelopeActivated && voices_[track].active) amplitudeEnvelopes_[track].trigger(p.amplitudeEnvelope);
    if (filterChanged) filters_[track].reset();
    if (voices_[track].active) updateVoiceCoefficients(track);
}

void Engine::setStep(int track, int step, const Step& value)
{
    if (track < 0 || track >= numTracks || step < 0 || step >= maxSteps) return;
    auto s = value;
    s.velocity = limit(s.velocity, 0.0f, 1.0f);
    s.probability = limit(s.probability, 0.0f, 1.0f);
    s.pitch = limit(s.pitch, -48.0f, 48.0f);
    s.cutoff = limit(s.cutoff, 20.0f, 20000.0f);
    s.conditionEvery = std::clamp(s.conditionEvery, 1, 64);
    s.conditionOffset = std::clamp(s.conditionOffset, 0, s.conditionEvery - 1);
    s.retrigs = std::clamp(s.retrigs, 1, 8);
    s.microtiming = limit(s.microtiming, -0.49f, 0.49f);
    s.note = std::clamp(s.note, 0, 127);
    s.slice = std::clamp(s.slice, 0, maxSlices - 1);
    s.rule.condition = enumLimit(s.rule.condition, 6);
    s.rule.fill = enumLimit(s.rule.fill, 2);
    s.rule.cycleB = std::clamp(s.rule.cycleB, 1, 8);
    s.rule.cycleA = std::clamp(s.rule.cycleA, 1, s.rule.cycleB);
    s.retrig.rateIndex = std::clamp(s.retrig.rateIndex, 0, 16);
    s.retrig.velocityFade = limit(s.retrig.velocityFade, -64.0f, 64.0f);
    if (!std::isinf(s.retrig.fadeLengthBeats) || s.retrig.fadeLengthBeats < 0.0)
        s.retrig.fadeLengthBeats = limit(s.retrig.fadeLengthBeats, 0.0, 512.0);
    s.noteLengthBeats = limit(s.noteLengthBeats, 0.0f, 512.0f);
    steps_[track][step] = s;
}

void Engine::setTrackLength(int track, int length)
{
    if (track >= 0 && track < numTracks)
        lengths_[track] = std::clamp(length, 1, maxSteps);
}

void Engine::setFx(const FxParams& value)
{
    fx_.delayMix = limit(value.delayMix, 0.0f, 1.0f);
    fx_.feedback = limit(value.feedback, 0.0f, 0.92f);
    fx_.delayBeats = limit(value.delayBeats, 0.03125f, 4.0f);
    fx_.reverbMix = limit(value.reverbMix, 0.0f, 1.0f);
}

void Engine::setChorus(const ChorusParams& value)
{
    chorusParams_.depth = limit(value.depth, 0.0f, 1.0f);
    chorusParams_.speed = limit(value.speed, 0.05f, 10.0f);
    chorusParams_.highpassHz = limit(value.highpassHz, 20.0f, 20000.0f);
    chorusParams_.width = limit(value.width, -1.0f, 1.0f);
    chorusParams_.volume = limit(value.volume, 0.0f, 1.0f);
    chorusParams_.delaySend = limit(value.delaySend, 0.0f, 1.0f);
    chorusParams_.reverbSend = limit(value.reverbSend, 0.0f, 1.0f);
}

void Engine::setSwing(float swing) { swing_ = limit(swing, 0.0f, 0.75f); }
void Engine::setFill(bool fill) { fill_ = fill; }
void Engine::setLastPatternCycle(bool last) { lastPatternCycle_ = last; }

void Engine::restartSequencer(bool preserveVoices)
{
    if (!preserveVoices)
    {
        voices_.fill(Voice{});
        for (auto& envelope : amplitudeEnvelopes_) envelope.reset();
        for (auto& envelope : filterEnvelopes_) envelope.reset();
    }
    retrigTrains_ = {};
    conditionMemory_ = {};
    firstCycles_.fill(-1);
    ++probabilityEpoch_;
    internalPpq_ = 0.0;
    pendingOrigin_ = true;
    wasPlaying_ = false;
    currentSteps_.fill(0);
}

int Engine::getCurrentStep(int track) const
{
    return track >= 0 && track < numTracks ? currentSteps_[track] : 0;
}

double Engine::stepTime(std::int64_t index) const
{
    return static_cast<double>(index) * stepBeats + ((index & 1) ? swing_ * stepBeats * 0.5 : 0.0);
}

double Engine::trackStepTime(int track, std::int64_t index) const
{
    // The 1x default follows the exact original arithmetic.
    return sequenceOriginPpq_ + stepTime(index) / sequencer::trackSpeed(params_[track].speedIndex);
}

int Engine::schedule(double ppq, double beatsPerSample, int samples)
{
    int count = 0, initialCount = 0;
    const auto sampleOffset = [&](double at)
    {
        const double position = (at - ppq) / beatsPerSample;
        if (position <= -1.0 + 1.0e-7 || position > samples - 1.0 + 1.0e-7) return -1;
        return static_cast<int>(std::ceil(position - 1.0e-7));
    };
    const auto append = [&](double at, int track, const Step& step, float velocity, bool lockTrig,
                            float gateBeats)
    {
        const int offset = sampleOffset(at);
        if (offset < 0 || offset >= samples || count == static_cast<int>(scheduled_.size())) return;
        scheduled_[count++] = {offset, track, velocity, step.pitch, step.cutoff,
                               step.lockPitch, step.lockCutoff, step.note, step.slice,
                               step.lockSlice, step.lfoTrig, lockTrig, gateBeats, step.filterTrig};
    };
    for (int track = 0; track < numTracks; ++track)
    {
        const double duration = sequencer::stepLengthBeats(params_[track].speedIndex);
        const double relative = ppq - sequenceOriginPpq_;
        const auto first = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(relative / duration)) - 2);
        const auto last = static_cast<std::int64_t>(std::ceil((relative + samples * beatsPerSample) / duration)) + 2;
        for (auto index = first; index <= last; ++index)
        {
            const auto& step = steps_[track][static_cast<std::size_t>(index % lengths_[track])];
            if (!step.enabled && !step.lockTrig) continue;
            const double start = trackStepTime(track, index) + step.microtiming * duration;
            if (sampleOffset(start) >= 0 && initialCount < static_cast<int>(initialEvents_.size()))
                initialEvents_[initialCount++] = {start, track, index};

            // Preserve the original conditional hash, retrigger count and sample
            // timestamp path for every old step. Advanced events use the stream
            // below, including occurrences that span later process blocks.
            if (step.advanced || step.lockTrig || params_[track].mute || !samples_[track]
                || step.velocity <= 0.0f || step.probability <= 0.0f) continue;
            const auto cycle = index / lengths_[track];
            if (cycle % step.conditionEvery != step.conditionOffset) continue;
            if (random01(static_cast<std::uint64_t>(index) * 37 + track * 7919) >= step.probability) continue;
            const double stepDuration = trackStepTime(track, index + 1) - trackStepTime(track, index);
            for (int repeat = 0; repeat < step.retrigs; ++repeat)
                append(start + stepDuration * repeat / step.retrigs, track, step, step.velocity, false,
                       step.noteLengthBeats);
        }
    }
    std::sort(initialEvents_.begin(), initialEvents_.begin() + initialCount,
              [](const auto& a, const auto& b) { return a.at < b.at || (a.at == b.at && a.track < b.track); });

    int initial = 0;
    for (;;)
    {
        int repeatTrack = -1;
        double repeatAt = std::numeric_limits<double>::infinity();
        for (int track = 0; track < numTracks; ++track)
        {
            const auto& train = retrigTrains_[track];
            if (train.active && train.next < repeatAt && sampleOffset(train.next) >= 0)
            {
                repeatAt = train.next;
                repeatTrack = track;
            }
        }
        const bool haveInitial = initial < initialCount;
        if (!haveInitial && repeatTrack < 0) break;
        const bool initialFirst = haveInitial
            && (repeatTrack < 0 || initialEvents_[initial].at < repeatAt
                || (initialEvents_[initial].at == repeatAt && initialEvents_[initial].track <= repeatTrack));
        if (!initialFirst)
        {
            auto& train = retrigTrains_[repeatTrack];
            append(train.next, repeatTrack, train.step,
                   sequencer::retrigVelocity(train.step.velocity, train.next - train.start, train.step.retrig), false,
                   static_cast<float>(std::max(0.0, train.step.noteLengthBeats - (train.next - train.start))));
            ++train.repeat;
            train.active = train.repeat < train.count;
            train.next = train.start + sequencer::retrigOffsetBeats(train.repeat, train.step.retrig);
            continue;
        }

        const auto event = initialEvents_[initial++];
        const int track = event.track;
        const auto& step = steps_[track][static_cast<std::size_t>(event.index % lengths_[track])];
        const auto cycle = event.index / lengths_[track];
        if (!step.advanced && !step.lockTrig)
        {
            // Existing conditions also provide useful PRE/NEI memory, without
            // changing their audible Legacy behavior or their historical RNG.
            if (step.conditionEvery > 1 || step.probability < 1.0f)
            {
                const bool result = cycle % step.conditionEvery == step.conditionOffset
                    && random01(static_cast<std::uint64_t>(event.index) * 37 + track * 7919) < step.probability;
                conditionMemory_[track] = {result, true};
            }
            if (step.velocity > 0.0f && cycle % step.conditionEvery == step.conditionOffset
                && random01(static_cast<std::uint64_t>(event.index) * 37 + track * 7919) < step.probability)
                retrigTrains_[track].active = false;
            continue;
        }
        sequencer::Context context;
        context.track = track;
        context.previous = conditionMemory_[track];
        if (track > 0) context.neighbor = conditionMemory_[track - 1];
        context.cycle = static_cast<std::uint64_t>(std::max<std::int64_t>(0, cycle - firstCycles_[track]));
        context.firstPatternCycle = cycle == firstCycles_[track];
        context.lastPatternCycle = lastPatternCycle_;
        context.fill = fill_;
        context.seed ^= hash(probabilityEpoch_);
        context.activation = static_cast<std::uint64_t>(event.index);
        auto rule = step.rule;
        rule.probability = step.probability;
        const auto decision = sequencer::evaluate(rule, context);
        sequencer::remember(conditionMemory_[track], decision);
        if (!decision.plays) continue;
        if (step.lockTrig)
        {
            append(event.at, track, step, 0.0f, true, step.noteLengthBeats);
            continue;
        }
        if (step.velocity <= 0.0f) continue;
        // A track is monophonic: a newly accepted note replaces its current
        // retrigger train. A refused condition or a lock trig leaves it running.
        auto& train = retrigTrains_[track];
        train = RetrigTrain{};
        append(event.at, track, step, sequencer::retrigVelocity(step.velocity, 0.0, step.retrig), false,
               step.noteLengthBeats);
        train.count = sequencer::retrigCount(step.noteLengthBeats, step.retrig);
        if (train.count > 1)
        {
            train.active = true;
            train.step = step;
            train.start = event.at;
            train.repeat = 1;
            train.next = train.start + sequencer::retrigOffsetBeats(1, step.retrig);
        }
    }
    std::sort(scheduled_.begin(), scheduled_.begin() + count,
              [](const auto& a, const auto& b) { return a.offset < b.offset || (a.offset == b.offset && a.track < b.track); });
    return count;
}

void Engine::applyLockTrig(const ScheduledEvent& event)
{
    if (event.track < 0 || event.track >= numTracks || !voices_[event.track].active) return;
    auto& voice = voices_[event.track];
    if (event.lockPitch) { voice.pitchLocked = true; voice.triggerPitch = event.pitch; }
    if (event.lockCutoff) { voice.cutoffLocked = true; voice.cutoff = event.cutoff; }
    if (event.lockSlice)
    {
        voice.sliceLocked = true;
        voice.slice = event.slice;
        if (voice.machine == Machine::Slice || voice.machine == Machine::Grid)
        {
            configureMachineRegion(event.track, voice, modulated_[event.track]);
            voice.position = std::clamp(voice.position, static_cast<double>(voice.first),
                                       static_cast<double>(voice.last - 1));
        }
    }
    if (event.lfoTrig) triggerLfos(event.track);
    if (event.filterTrig)
    {
        voice.filterEnvelopePending = modulated_[event.track].filter.envDelay > 0.0f;
        voice.filterEnvelopeDelayFrames = modulated_[event.track].filter.envDelay * sampleRate_;
        if (!voice.filterEnvelopePending) filterEnvelopes_[event.track].trigger(modulated_[event.track].filter.envelope);
    }
    updateVoiceCoefficients(event.track);
}

void Engine::trigger(int track, float velocity, float pitch, bool lockPitch, float cutoff, bool lockCutoff,
                     int note, int slice, bool lockSlice, bool lfoTrig, float gateBeats, bool filterTrig)
{
    if (track < 0 || track >= numTracks || params_[track].mute || !samples_[track]) return;
    const auto& sample = *samples_[track];
    if (sample.left.empty()) return;
    const auto& p = params_[track];
    auto& v = voices_[track];
    v = Voice{};
    v.first = std::min(static_cast<std::size_t>(p.start * sample.left.size()), sample.left.size() - 1);
    v.last = std::max(v.first + 1, std::min(static_cast<std::size_t>(std::ceil(p.end * sample.left.size())), sample.left.size()));
    v.position = p.reverse ? static_cast<double>(v.last - 1) : static_cast<double>(v.first);
    v.velocity = limit(velocity, 0.0f, 1.0f);
    v.cutoff = lockCutoff ? cutoff : p.cutoff;
    v.triggerPitch = limit(pitch, -48.0f, 48.0f);
    v.pitchLocked = lockPitch;
    v.cutoffLocked = lockCutoff;
    v.machine = p.machine;
    v.note = std::clamp(note, 0, 127);
    v.slice = slice;
    v.sliceLocked = lockSlice;
    v.gateRemainingBeats = std::isfinite(gateBeats) && gateBeats >= 0.0f ? gateBeats : -1.0;
    amplitudeEnvelopes_[track].trigger(p.amplitudeEnvelope);
    if (filterTrig)
    {
        v.filterEnvelopePending = p.filter.envDelay > 0.0f;
        v.filterEnvelopeDelayFrames = p.filter.envDelay * sampleRate_;
        if (!v.filterEnvelopePending) filterEnvelopes_[track].trigger(p.filter.envelope);
    }
    if (lfoTrig) triggerLfos(track);
    if (p.machine != Machine::Legacy || lfoEnabled_[track] || extendedDsp_[track])
    {
        configureMachineRegion(track, v, modulated_[track]);
        v.position = v.reversed ? static_cast<double>(v.last - 1) : static_cast<double>(v.sourceStart);
    }
    updateVoiceCoefficients(track);
    v.active = v.velocity > 0.0f;
}

void Engine::noteOff(int track, int note)
{
    if (track < 0 || track >= numTracks) return;
    auto& voice = voices_[track];
    if (note >= 0 && voice.note != note) return;
    voice.gateOpen = false;
    voice.gateRemainingBeats = -1.0;
    voice.filterEnvelopePending = false;
    amplitudeEnvelopes_[track].noteOff(modulated_[track].amplitudeEnvelope);
    filterEnvelopes_[track].noteOff(modulated_[track].filter.envelope);
}

void Engine::updateVoiceCoefficients(int track)
{
    auto& v = voices_[track];
    if (!samples_[track]) return;
    const bool extended = v.machine != Machine::Legacy || lfoEnabled_[track] || extendedDsp_[track];
    const auto& p = extended ? modulated_[track] : params_[track];
    const bool noteSlice = extended && p.sliceByNote && (v.machine == Machine::Slice || v.machine == Machine::Grid);
    const float tuning = v.pitchLocked ? v.triggerPitch : p.pitch + v.triggerPitch;
    const double semitones = limit(static_cast<double>(noteSlice ? p.pitch : tuning + (extended ? v.note - 60 : 0)), -96.0, 96.0);
    const bool reversed = extended ? v.reversed : p.reverse;
    if (!v.coefficientsValid || v.cachedSemitones != semitones || v.cachedReverse != reversed)
    {
        v.increment = limit(samples_[track]->sampleRate, 8000.0, 384000.0) / sampleRate_ * std::exp2(semitones / 12.0);
        if (reversed) v.increment = -v.increment;
        v.cachedSemitones = semitones;
    }
    if (extended && (!v.coefficientsValid || v.cachedBars != p.bars || v.cachedBpm != currentBpm_ || v.cachedReverse != reversed))
    {
        v.timelineIncrement = static_cast<double>(samples_[track]->left.size()) * currentBpm_ / (60.0 * sampleRate_ * 4.0 * p.bars);
        if (reversed) v.timelineIncrement = -v.timelineIncrement;
        v.cachedBars = p.bars; v.cachedBpm = currentBpm_;
    }
    if (v.machine == Machine::Repitch) v.increment = v.timelineIncrement;
    v.cachedReverse = reversed;
    if (!v.cutoffLocked) v.cutoff = p.cutoff;
    const float hz = limit(v.cutoff, 20.0f, static_cast<float>(sampleRate_ * 0.45));
    if (!v.coefficientsValid || v.cachedCutoff != hz || v.cachedResonance != p.resonance)
    {
        if (!v.coefficientsValid || v.cachedCutoff != hz)
            v.filterG = static_cast<float>(std::tan(pi * hz / sampleRate_));
        v.filterK = 2.0f - 1.9f * p.resonance;
        v.filterA = 1.0f / (1.0f + v.filterG * (v.filterG + v.filterK));
        v.cachedCutoff = hz; v.cachedResonance = p.resonance;
    }
    if (!v.coefficientsValid || v.cachedDrive != p.drive)
    {
        v.driveScale = 1.0f + p.drive * 20.0f;
        v.driveInverse = p.drive > 0.0f ? 1.0f / std::tanh(v.driveScale) : 1.0f;
        v.cachedDrive = p.drive;
    }
    const float bitDepth = std::round(extended ? std::min(p.bitDepth, p.bitReduction) : p.bitDepth);
    if (!v.coefficientsValid || v.cachedBitDepth != bitDepth)
    {
        v.bitLevels = std::exp2(bitDepth - 1.0f);
        v.cachedBitDepth = bitDepth;
    }
    if (!v.coefficientsValid || v.cachedPan != p.pan)
    {
        v.panLeft = std::sqrt(1.0f - p.pan); v.panRight = std::sqrt(1.0f + p.pan);
        v.cachedPan = p.pan;
    }
    v.coefficientsValid = true;
}

void Engine::configureMachineRegion(int track, Voice& v, const TrackParams& p)
{
    const auto frames = samples_[track]->left.size();
    const bool legacy = v.machine == Machine::Legacy;
    float start = p.start, end = legacy ? p.end : std::min(1.0f, p.start + p.sourceLength), loop = legacy ? p.start : p.loopPosition;
    if (v.machine == Machine::Slice || v.machine == Machine::Grid)
    {
        const int count = p.sliceCount;
        int selected = v.sliceLocked ? v.slice : p.slice;
        if (p.sliceByNote && !v.sliceLocked) selected = ((v.note - 24) % count + count) % count;
        selected = std::clamp(selected, 0, count - 1);
        const int finalSlice = std::min(count - 1, selected + p.sliceLength - 1);
        start = static_cast<float>(selected) / count;
        end = static_cast<float>(finalSlice + 1) / count;
        loop = start;
        if (v.machine == Machine::Slice)
        {
            const auto& first = p.slicePoints[static_cast<std::size_t>(selected)];
            const auto& last = p.slicePoints[static_cast<std::size_t>(finalSlice)];
            if (first.end > first.start) { start = first.start; loop = first.loop; }
            if (last.end > last.start) end = last.end;
        }
    }
    v.sourceStart = std::min(static_cast<std::size_t>(start * frames), frames - 1);
    v.last = std::max(v.sourceStart + 1, std::min(static_cast<std::size_t>(std::ceil(end * frames)), frames));
    v.loopFirst = std::min(static_cast<std::size_t>(loop * frames), v.last - 1);
    v.looping = legacy ? p.loop : loopMode(p.playback);
    v.reversed = legacy ? p.reverse : reverseMode(p.playback);
    v.first = v.looping ? std::min(v.sourceStart, v.loopFirst) : v.sourceStart;
}

void Engine::triggerLfos(int track)
{
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto& p = params_[track].lfos[i];
        auto& state = lfoStates_[track][i];
        if (p.mode != LfoMode::Free && p.mode != LfoMode::Hold)
        {
            state.phase = p.wave == LfoWave::Random ? 0.0 : p.phase / 128.0;
            state.elapsed = 0.0;
            state.cycle = 0;
            state.running = true;
        }
        state.age = 0.0;
        if (p.mode == LfoMode::Hold)
            state.held = lfoWave(p.wave, state.phase, state.cycle, track * 3 + static_cast<int>(i) + 1);
    }
    if (lfoEnabled_[track]) updateLfos(track, false);
}

void Engine::updateLfos(int track, bool advance)
{
    const auto& base = params_[track];
    auto& p = modulated_[track];
    // Only scalar destinations are copied per frame; the slice table stays in
    // its preallocated track storage and is never copied in the render loop.
    p.pitch = base.pitch; p.cutoff = base.cutoff; p.gain = base.gain; p.pan = base.pan;
    p.start = base.start; p.sourceLength = base.sourceLength; p.loopPosition = base.loopPosition;
    p.slice = base.slice; p.drive = base.drive; p.bitDepth = base.bitDepth;
    p.bitReduction = base.bitReduction;
    p.delaySend = base.delaySend; p.reverbSend = base.reverbSend;
    p.attack = base.attack; p.decay = base.decay;
    p.amplitudeEnvelope.attack = base.amplitudeEnvelope.attack;
    p.amplitudeEnvelope.decay = base.amplitudeEnvelope.decay;
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto& lfo = base.lfos[i];
        if (lfo.depth == 0.0f || lfo.destination == LfoDestination::None) continue;
        auto& state = lfoStates_[track][i];
        const double tempo = lfo.bpmSync ? currentBpm_ : 120.0;
        const double delta = lfo.speed * lfo.multiplier * tempo / (30720.0 * sampleRate_);
        float value = lfo.mode == LfoMode::Hold ? state.held : lfoWave(lfo.wave, state.phase, state.cycle, track * 3 + static_cast<int>(i) + 1);
        if (lfo.wave == LfoWave::Random && lfo.phase > 0.0f && lfo.mode != LfoMode::Hold)
        {
            const float smoothing = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate_ * (lfo.phase / 127.0) * 0.5 + 1.0)));
            if (advance) state.smoothed += smoothing * (value - state.smoothed);
            value = state.smoothed;
        }
        if (lfo.fade != 0.0f)
        {
            // Independent mapping: full fade range covers eight beats. The
            // manual specifies polarity but does not publish its time curve.
            const double duration = std::abs(lfo.fade) / 64.0 * 8.0 * 60.0 / tempo * sampleRate_;
            const float progress = static_cast<float>(std::min(1.0, state.age / std::max(1.0, duration)));
            value *= lfo.fade > 0.0f ? 1.0f - progress : progress;
        }
        const float amount = value * lfo.depth / 128.0f;
        switch (lfo.destination)
        {
            case LfoDestination::None: break;
            case LfoDestination::Pitch: p.pitch = limit(p.pitch + amount * 48.0f, -96.0f, 96.0f); break;
            case LfoDestination::Cutoff: p.cutoff = limit(p.cutoff * std::exp2(amount * 8.0f), 20.0f, 20000.0f); break;
            case LfoDestination::Gain: p.gain = limit(p.gain + amount * 2.0f, 0.0f, 2.0f); break;
            case LfoDestination::Pan: p.pan = limit(p.pan + amount, -1.0f, 1.0f); break;
            case LfoDestination::Start: p.start = limit(p.start + amount, 0.0f, 1.0f); break;
            case LfoDestination::Length: p.sourceLength = limit(p.sourceLength + amount, 0.0f, 1.0f); break;
            case LfoDestination::LoopPosition: p.loopPosition = limit(p.loopPosition + amount, 0.0f, 1.0f); break;
            case LfoDestination::Slice: p.slice = std::clamp(p.slice + static_cast<int>(std::round(amount * (p.sliceCount - 1))), 0, p.sliceCount - 1); break;
            case LfoDestination::Drive: p.drive = limit(p.drive + amount, 0.0f, 1.0f); break;
            case LfoDestination::BitDepth:
                p.bitDepth = limit(p.bitDepth + amount * 16.0f, 1.0f, 24.0f);
                p.bitReduction = limit(p.bitReduction + amount * 16.0f, 1.0f, 16.0f);
                break;
            case LfoDestination::DelaySend: p.delaySend = limit(p.delaySend + amount, 0.0f, 1.0f); break;
            case LfoDestination::ReverbSend: p.reverbSend = limit(p.reverbSend + amount, 0.0f, 1.0f); break;
            case LfoDestination::Attack:
                p.attack = limit(p.attack * std::exp2(amount * 8.0f), 0.0001f, 10.0f);
                p.amplitudeEnvelope.attack = limit(p.amplitudeEnvelope.attack * std::exp2(amount * 8.0f), 0.0001f, 10.0f);
                break;
            case LfoDestination::Decay:
                p.decay = limit(p.decay * std::exp2(amount * 8.0f), 0.005f, 30.0f);
                p.amplitudeEnvelope.decay = limit(p.amplitudeEnvelope.decay * std::exp2(amount * 8.0f), 0.0001f, 30.0f);
                break;
        }
        const bool continuous = lfo.mode == LfoMode::Free || lfo.mode == LfoMode::Hold || lfo.mode == LfoMode::Trigger;
        if (advance && (continuous || state.running))
        {
            const double next = state.phase + delta;
            const auto crossings = static_cast<std::int64_t>(std::floor(next));
            state.cycle += crossings;
            state.phase = next - std::floor(next);
            state.elapsed += std::abs(delta);
            if ((lfo.mode == LfoMode::One && state.elapsed >= 1.0)
                || (lfo.mode == LfoMode::Half && state.elapsed >= 0.5))
            {
                state.running = false;
                const double start = lfo.wave == LfoWave::Random ? 0.0 : lfo.phase / 128.0;
                const double finish = start + (delta < 0.0 ? -1.0 : 1.0) * (lfo.mode == LfoMode::Half ? 0.5 : 1.0);
                state.phase = finish - std::floor(finish);
            }
        }
        if (advance) state.age += 1.0;
    }
}

void Engine::renderMachine(int track, float& left, float& right)
{
    left = right = 0.0f;
    auto& v = voices_[track];
    const auto& p = modulated_[track];
    if (!v.active || p.mute || !samples_[track]) return;
    const auto& s = *samples_[track];
    // Start/length/slice modulation is latched by the next trig; loop position
    // and the scalar audio destinations affect a running voice immediately.
    if (v.looping && v.machine != Machine::Legacy && v.machine != Machine::Slice && v.machine != Machine::Grid)
        v.loopFirst = std::min(static_cast<std::size_t>(p.loopPosition * s.left.size()), v.last - 1);
    updateVoiceCoefficients(track);
    const double attackFrames = p.attack * sampleRate_, decayFrames = p.decay * sampleRate_;
    float envelope;
    if (p.amplitudeEnvelope.mode != EnvelopeMode::Legacy)
    {
        envelope = amplitudeEnvelopes_[track].next(p.amplitudeEnvelope, sampleRate_);
        if (!amplitudeEnvelopes_[track].isActive()) { v.active = false; return; }
    }
    else
    {
        envelope = static_cast<float>(std::min(1.0, (v.age + 1) / attackFrames) * std::exp(-static_cast<double>(v.age) / decayFrames));
        if (envelope < 1.0e-6f && v.age > attackFrames) { v.active = false; return; }
    }
    const bool stretched = v.machine == Machine::Stretch || v.machine == Machine::Werp;
    const double timeline = stretched ? v.timelineIncrement : v.increment;
    const auto readChannel = [&](const std::vector<float>& data)
    {
        if (v.machine == Machine::Stretch)
        {
            // Two Hann-overlapped independent grains. The moving source center
            // follows tempo; grain read speed follows tuning. No FFT buffers,
            // heap allocations or copied sample data are needed in processing.
            const double grain = sampleRate_ * 0.08;
            const double phase = std::fmod(static_cast<double>(v.age), grain) / grain;
            float result = 0.0f;
            for (int voice = 0; voice < 2; ++voice)
            {
                const double age = std::fmod(phase + voice * 0.5, 1.0) * grain;
                const double position = wrapped(v.position + (age - grain * 0.5) * (v.increment - timeline), v.first, v.last);
                const float window = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * pi * age / grain));
                result += window * interpolate(data, position, v.first, v.last, true);
            }
            return result;
        }
        if (v.machine == Machine::Werp)
        {
            const double segment = std::max(1.0, static_cast<double>(p.segmentSize) * s.left.size());
            const double distance = v.reversed ? v.last - 1.0 - v.position : v.position - v.sourceStart;
            const double number = std::floor(std::max(0.0, distance) / segment);
            const double segmentStart = v.reversed ? v.last - (number + 1.0) * segment : v.sourceStart + number * segment;
            const auto first = static_cast<std::size_t>(std::clamp(segmentStart, static_cast<double>(v.first), static_cast<double>(v.last - 1)));
            const auto last = std::max(first + 1, std::min(v.last, static_cast<std::size_t>(std::max(1.0, segmentStart + segment))));
            const double local = std::fmod(std::max(0.0, distance), segment) / std::max(1.0e-12, std::abs(timeline)) * std::abs(v.increment);
            double position = reverseMode(p.segmentMode) ? last - 1.0 - local : first + local;
            if (loopMode(p.segmentMode)) position = wrapped(position, first, last);
            else if (position < first || position >= last) return 0.0f;
            return interpolate(data, position, first, last, loopMode(p.segmentMode));
        }
        return interpolate(data, v.position, v.looping ? v.loopFirst : v.first, v.last, v.looping);
    };
    if (!v.looping)
    {
        const double remaining = timeline > 0 ? v.last - v.position : v.position - v.first + 1;
        envelope *= static_cast<float>(std::min(1.0, remaining / (std::abs(timeline) * sampleRate_ * 0.001)));
    }
    float l = readChannel(s.left), r = s.right.empty() ? l : readChannel(s.right);
    const float depth = std::min(p.bitDepth, p.bitReduction);
    if (depth < 16.0f)
    {
        const float levels = v.bitLevels;
        l = std::round(l * levels) / levels; r = std::round(r * levels) / levels;
    }
    if (p.trackFx.drivePre && p.drive > 0.0f) StereoTrackFx::overdrivePrepared(l, r, v.driveScale, v.driveInverse);
    if (p.trackFx.srrPre) trackFx_[track].rateReduction(l, r, p.trackFx.srr);
    if (v.filterEnvelopePending)
    {
        if (v.filterEnvelopeDelayFrames <= 0.0)
        {
            filterEnvelopes_[track].trigger(p.filter.envelope);
            v.filterEnvelopePending = false;
        }
        else v.filterEnvelopeDelayFrames -= 1.0;
    }
    const float filterEnvelope = filterEnvelopes_[track].next(p.filter.envelope, sampleRate_);
    if (p.filter.machine == FilterMachine::Prototype && p.filter.base == 0.0f
        && p.filter.width == 127.0f && p.filter.envDepth == 0.0f && p.filter.keytrack == 0.0f)
    {
        l = filter(l, v.lowL, v.bandL, v.filterG, v.filterA);
        r = filter(r, v.lowR, v.bandR, v.filterG, v.filterA);
    }
    else filters_[track].process(l, r, p.filter, v.cutoff, p.resonance, filterEnvelope, v.note);
    if (!p.trackFx.srrPre) trackFx_[track].rateReduction(l, r, p.trackFx.srr);
    if (!p.trackFx.drivePre && p.drive > 0.0f) StereoTrackFx::overdrivePrepared(l, r, v.driveScale, v.driveInverse);
    const float amplitude = envelope * v.velocity * p.gain * p.sampleLevel * p.ampVolume;
    left = finite(l * amplitude * v.panLeft); right = finite(r * amplitude * v.panRight);
    v.position += timeline;
    ++v.age;
    const auto boundary = v.looping && v.reversed ? v.loopFirst : v.first;
    if (v.position < boundary || v.position >= v.last)
    {
        if (v.looping) v.position = wrapped(v.position, v.loopFirst, v.last);
        else v.active = false;
    }
}

void Engine::renderVoice(int track, float& left, float& right)
{
    if (voices_[track].machine != Machine::Legacy || lfoEnabled_[track] || extendedDsp_[track])
    {
        renderMachine(track, left, right);
        return;
    }
    left = right = 0.0f;
    auto& v = voices_[track];
    const auto& p = params_[track];
    if (!v.active || p.mute || !samples_[track]) return;
    const auto& s = *samples_[track];
    const double attackFrames = p.attack * sampleRate_;
    const double decayFrames = p.decay * sampleRate_;
    float envelope = static_cast<float>(std::min(1.0, (v.age + 1) / attackFrames)
                        * std::exp(-static_cast<double>(v.age) / decayFrames));
    if (envelope < 1.0e-6f && v.age > attackFrames)
    {
        v.active = false;
        return;
    }
    if (!p.loop)
    {
        const double remaining = v.increment > 0 ? v.last - v.position : v.position - v.first + 1;
        envelope *= static_cast<float>(std::min(1.0, remaining / (std::abs(v.increment) * sampleRate_ * 0.001)));
    }
    float l = interpolate(s.left, v.position, v.first, v.last, p.loop);
    float r = s.right.empty() ? l : interpolate(s.right, v.position, v.first, v.last, p.loop);
    if (p.drive > 0.0f)
    {
        l = std::tanh(l * v.driveScale) * v.driveInverse;
        r = std::tanh(r * v.driveScale) * v.driveInverse;
    }
    if (p.bitDepth < 16.0f)
    {
        const float levels = v.bitLevels;
        l = std::round(l * levels) / levels;
        r = std::round(r * levels) / levels;
    }
    l = filter(l, v.lowL, v.bandL, v.filterG, v.filterA);
    r = filter(r, v.lowR, v.bandR, v.filterG, v.filterA);
    const float amplitude = envelope * v.velocity * p.gain;
    left = finite(l * amplitude * v.panLeft);
    right = finite(r * amplitude * v.panRight);
    v.position += v.increment;
    ++v.age;
    if (v.position < v.first || v.position >= v.last)
    {
        if (p.loop)
        {
            const double size = static_cast<double>(v.last - v.first);
            v.position = v.first + std::fmod(std::fmod(v.position - v.first, size) + size, size);
        }
        else v.active = false;
    }
}

void Engine::process(float* left, float* right, int numSamples, const Transport& transport,
                     const TriggerEvent* events, int numEvents)
{
    if (!left || !right || numSamples <= 0) return;
    if (!prepared_)
    {
        std::fill(left, left + numSamples, 0.0f);
        std::fill(right, right + numSamples, 0.0f);
        return;
    }
    const double bpm = limit(transport.bpm, 20.0, 400.0);
    currentBpm_ = bpm;
    const double beatsPerSample = bpm / (60.0 * sampleRate_);
    const double ppq = transport.hostPosition ? limit(transport.ppq, -1.0e9, 1.0e9) : internalPpq_;
    const bool discontinuity = transport.hostPosition && wasPlaying_
        && std::abs(ppq - expectedPpq_) > beatsPerSample * 1.5;
    if (transport.playing && (!wasPlaying_ || discontinuity || pendingOrigin_))
    {
        if (pendingOrigin_) sequenceOriginPpq_ = ppq;
        pendingOrigin_ = false;
        retrigTrains_ = {};
        conditionMemory_ = {};
        ++probabilityEpoch_;
        for (int track = 0; track < numTracks; ++track)
        {
            const double duration = sequencer::stepLengthBeats(params_[track].speedIndex);
            const auto index = std::max<std::int64_t>(0,
                static_cast<std::int64_t>(std::floor((ppq - sequenceOriginPpq_) / duration)));
            firstCycles_[track] = index / lengths_[track];
        }
    }
    wasPlaying_ = transport.playing;
    expectedPpq_ = ppq + numSamples * beatsPerSample;
    const double delayFrames = limit(sampleRate_ * 60.0 / bpm * fx_.delayBeats, 1.0,
                                     static_cast<double>(delayL_.size() - 2));
    const auto delayWhole = static_cast<std::size_t>(std::floor(delayFrames));
    const float delayFraction = static_cast<float>(delayFrames - delayWhole);
    numEvents = std::max(0, numEvents);
    const bool sortedEvents = !events || std::is_sorted(events, events + numEvents,
        [](const TriggerEvent& a, const TriggerEvent& b) { return a.sampleOffset < b.sampleOffset; });
    int externalCursor = 0;
    const auto applyExternalEvent = [&](const TriggerEvent& event)
    {
        if (event.noteOff) noteOff(event.track, event.note);
        else trigger(event.track, event.velocity, event.pitch, false, 0.0f, false,
                     event.note, event.slice, event.lockSlice, true, event.gateBeats, event.filterTrig);
    };

    for (int begin = 0; begin < numSamples; begin += blockSize_)
    {
        const int size = std::min(blockSize_, numSamples - begin);
        const int count = transport.playing ? schedule(ppq + begin * beatsPerSample, beatsPerSample, size) : 0;
        int next = 0;
        for (int frame = 0; frame < size; ++frame)
        {
            while (next < count && scheduled_[next].offset == frame)
            {
                const auto& e = scheduled_[next++];
                if (e.lockTrig) applyLockTrig(e);
                else trigger(e.track, e.velocity, e.pitch, e.lockPitch, e.cutoff, e.lockCutoff,
                             e.note, e.slice, e.lockSlice, e.lfoTrig, e.gateBeats, e.filterTrig);
            }
            // The processor supplies ordered MIDI: advance one cursor across
            // all chunks, preserving caller order at equal offsets. Retain the
            // allocation-free unsorted fallback for direct Engine callers.
            if (events)
            {
                const int offset = begin + frame;
                if (sortedEvents)
                {
                    while (externalCursor < numEvents && events[externalCursor].sampleOffset < offset) ++externalCursor;
                    while (externalCursor < numEvents && events[externalCursor].sampleOffset == offset)
                        applyExternalEvent(events[externalCursor++]);
                }
                else
                    for (int event = 0; event < numEvents; ++event)
                        if (events[event].sampleOffset == offset) applyExternalEvent(events[event]);
            }
            for (int track = 0; track < numTracks; ++track)
                if (lfoEnabled_[track]) updateLfos(track);

            float mixL = 0.0f, mixR = 0.0f, sendDL = 0.0f, sendDR = 0.0f, sendRL = 0.0f, sendRR = 0.0f;
            float sendCL = 0.0f, sendCR = 0.0f;
            for (int track = 0; track < numTracks; ++track)
            {
                auto& voice = voices_[track];
                if (voice.gateOpen && voice.gateRemainingBeats >= 0.0)
                {
                    if (voice.gateRemainingBeats <= beatsPerSample * 1.0e-7) noteOff(track);
                    else voice.gateRemainingBeats = std::max(0.0, voice.gateRemainingBeats - beatsPerSample);
                }
                float l, r;
                renderVoice(track, l, r);
                mixL += l; mixR += r;
                const auto& sends = lfoEnabled_[track] ? modulated_[track] : params_[track];
                sendDL += l * sends.delaySend;
                sendDR += r * sends.delaySend;
                sendRL += l * sends.reverbSend;
                sendRR += r * sends.reverbSend;
                sendCL += l * sends.trackFx.chorusSend;
                sendCR += r * sends.trackFx.chorusSend;
            }

            float chorusLeft, chorusRight;
            chorus_.process(sendCL, sendCR, chorusParams_, chorusLeft, chorusRight);
            if (chorusParams_.volume > 0.0f)
            {
                mixL += chorusLeft * chorusParams_.volume;
                mixR += chorusRight * chorusParams_.volume;
            }
            if (chorusParams_.delaySend > 0.0f)
            {
                sendDL += chorusLeft * chorusParams_.delaySend;
                sendDR += chorusRight * chorusParams_.delaySend;
            }
            if (chorusParams_.reverbSend > 0.0f)
            {
                sendRL += chorusLeft * chorusParams_.reverbSend;
                sendRR += chorusRight * chorusParams_.reverbSend;
            }

            const auto read = (delayCursor_ + delayL_.size() - delayWhole) % delayL_.size();
            const auto previous = (read + delayL_.size() - 1) % delayL_.size();
            const float dl = delayL_[read] + delayFraction * (delayL_[previous] - delayL_[read]);
            const float dr = delayR_[read] + delayFraction * (delayR_[previous] - delayR_[read]);
            delayL_[delayCursor_] = finite(sendDL + dr * fx_.feedback);
            delayR_[delayCursor_] = finite(sendDR + dl * fx_.feedback);
            delayCursor_ = (delayCursor_ + 1) % delayL_.size();
            mixL += dl * fx_.delayMix;
            mixR += dr * fx_.delayMix;

            float taps[4];
            for (int i = 0; i < 4; ++i)
            {
                auto& line = reverb_[i];
                line.damp += 0.3f * (line.data[line.cursor] - line.damp);
                if (std::abs(line.damp) < 1.0e-20f) line.damp = 0.0f;
                taps[i] = line.damp;
            }
            const float input[4] = { sendRL, sendRR, (sendRL + sendRR) * 0.5f, (sendRL - sendRR) * 0.5f };
            const float feedback[4] = {
                (taps[0] + taps[1] + taps[2] + taps[3]) * 0.5f,
                (taps[0] - taps[1] + taps[2] - taps[3]) * 0.5f,
                (taps[0] + taps[1] - taps[2] - taps[3]) * 0.5f,
                (taps[0] - taps[1] - taps[2] + taps[3]) * 0.5f
            };
            for (int i = 0; i < 4; ++i)
            {
                auto& line = reverb_[i];
                line.data[line.cursor] = finite(input[i] * 0.35f + feedback[i] * 0.75f);
                line.cursor = (line.cursor + 1) % line.data.size();
            }
            mixL += (taps[0] + taps[2]) * 0.5f * fx_.reverbMix;
            mixR += (taps[1] + taps[3]) * 0.5f * fx_.reverbMix;
            left[begin + frame] = masterSoftClip(mixL);
            right[begin + frame] = masterSoftClip(mixR);
        }
    }
    if (transport.playing)
    {
        const double finalPpq = ppq + numSamples * beatsPerSample;
        internalPpq_ = finalPpq;
        for (int track = 0; track < numTracks; ++track)
        {
            const double duration = sequencer::stepLengthBeats(params_[track].speedIndex);
            auto index = static_cast<std::int64_t>(std::floor(std::max(0.0,
                finalPpq - sequenceOriginPpq_ - beatsPerSample) / duration));
            if (trackStepTime(track, index) > finalPpq - beatsPerSample && index > 0) --index;
            currentSteps_[track] = static_cast<int>(index % lengths_[track]);
        }
    }
}

std::shared_ptr<const Sample> Engine::makeDemoSample(int track, double sampleRate)
{
    auto s = std::make_shared<Sample>();
    s->sampleRate = limit(sampleRate, 8000.0, 384000.0);
    track = ((track % numTracks) + numTracks) % numTracks;
    const char* names[numTracks] = { "Pulse Kick", "Dust Snare", "Closed Alloy", "Open Alloy",
        "Low Tom", "Hand Clap", "Rim Click", "Bright Shaker", "FM Bass", "Glass Bell",
        "Chord Mist", "Plucked Wire", "High Tom", "Noise Sweep", "Metal Gong", "Soft Sine" };
    s->name = names[track];
    const double duration = track == 10 || track == 14 ? 2.0 : (track == 3 || track >= 8 ? 1.0 : 0.5);
    const auto frames = static_cast<std::size_t>(s->sampleRate * duration);
    s->left.resize(frames);
    s->right.resize(frames);
    double phase = 0.0;
    float lastNoise = 0.0f;
    for (std::size_t i = 0; i < frames; ++i)
    {
        const double t = static_cast<double>(i) / s->sampleRate;
        const float noise = random01(i + static_cast<std::uint64_t>(track + 1) * 1000003) * 2.0f - 1.0f;
        const float highNoise = (noise - lastNoise) * 0.5f;
        lastNoise = noise;
        double l = 0.0, r = 0.0;
        switch (track)
        {
            case 0:
                phase += 2 * pi * (48 + 150 * std::exp(-t * 38)) / s->sampleRate;
                l = std::sin(phase) * std::exp(-t * 10) + highNoise * std::exp(-t * 160) * 0.2;
                break;
            case 1: l = (noise * 0.65 + std::sin(2*pi*185*t)*0.3) * std::exp(-t*17); break;
            case 2: l = highNoise * std::exp(-t*65) * 0.8; break;
            case 3: l = (highNoise + std::sin(2*pi*7919*t)*0.15) * std::exp(-t*8)*0.6; break;
            case 4:
            case 12:
                phase += 2*pi*(track == 4 ? 95 : 170)*(1+0.9*std::exp(-t*30))/s->sampleRate;
                l = std::sin(phase)*std::exp(-t*12); break;
            case 5:
                l = noise * (std::exp(-t*25) + (t>0.012 ? std::exp(-(t-0.012)*80) : 0)
                         + (t>0.026 ? std::exp(-(t-0.026)*80) : 0)) * 0.35; break;
            case 6: l = (std::sin(2*pi*1740*t) + std::sin(2*pi*2711*t)*0.3)*std::exp(-t*120); break;
            case 7: l = highNoise*std::exp(-t*28)*(0.4+0.4*std::sin(2*pi*47*t)); break;
            case 8: l = std::sin(2*pi*55*t + 1.4*std::sin(2*pi*110*t)*std::exp(-t*5))*std::exp(-t*4)*0.8; break;
            case 9: l = (std::sin(2*pi*523.25*t) + 0.3*std::sin(2*pi*1431*t))*std::exp(-t*5)*0.65; break;
            case 10:
                l = (std::sin(2*pi*261.63*t)+std::sin(2*pi*329.63*t)+std::sin(2*pi*392*t))*std::exp(-t*2)*0.22;
                r = (std::sin(2*pi*262.1*t)+std::sin(2*pi*329.1*t)+std::sin(2*pi*392.5*t))*std::exp(-t*2)*0.22;
                break;
            case 11: l = (std::sin(2*pi*220*t)+std::sin(2*pi*440*t)*0.25+std::sin(2*pi*660*t)*0.1)*std::exp(-t*7)*0.7; break;
            case 13: l = noise*std::exp(-t*4)*(0.5+0.5*std::sin(2*pi*(600*t+1400*t*t)))*0.65; break;
            case 14: l = (std::sin(2*pi*174*t)+0.4*std::sin(2*pi*429*t)+0.25*std::sin(2*pi*811*t))*std::exp(-t*2)*0.55; break;
            default: l = std::sin(2*pi*220*t)*std::exp(-t*4)*0.8; break;
        }
        if (track != 10) r = l;
        const double fade = std::min(1.0, t / 0.0003) * std::min(1.0, (duration-t) / 0.01);
        s->left[i] = static_cast<float>(l * fade);
        s->right[i] = static_cast<float>(r * fade);
    }
    return s;
}
}
