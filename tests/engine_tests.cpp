#include "engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
thread_local bool countAllocations = false;
thread_local std::size_t allocations = 0;
thread_local std::size_t deallocations = 0;
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void releaseAllocation(void* pointer) noexcept
{
    if (countAllocations && pointer) ++deallocations;
    std::free(pointer);
}
}

void* operator new(std::size_t size)
{
    if (countAllocations) ++allocations;
    if (void* result = std::malloc(size == 0 ? 1 : size)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { releaseAllocation(pointer); }
void operator delete[](void* pointer) noexcept { releaseAllocation(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { releaseAllocation(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { releaseAllocation(pointer); }

namespace
{
constexpr double sampleRate = 48000.0;
constexpr double pi = 3.14159265358979323846;
using namespace takt;

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Audio
{
    explicit Audio(int frames) : left(frames), right(frames) {}
    std::vector<float> left, right;
};

double energy(const std::vector<float>& values, int first = 0, int last = -1)
{
    if (last < 0) last = static_cast<int>(values.size());
    double result = 0;
    for (int i = first; i < last; ++i) result += values[i] * values[i];
    return result;
}

void bounded(const Audio& audio)
{
    for (const auto* channel : { &audio.left, &audio.right })
        for (float value : *channel)
            require(std::isfinite(value) && std::abs(value) <= 1.0f, "output must be finite and peak bounded");
}

std::shared_ptr<const Sample> constant(int frames = 64, float amplitude = 0.2f)
{
    auto sample = std::make_shared<Sample>();
    sample->sampleRate = sampleRate;
    sample->left.assign(frames, amplitude);
    sample->right = sample->left;
    return sample;
}

std::shared_ptr<const Sample> sine(double frequency = 220.0, double sourceRate = sampleRate, bool stereo = false)
{
    auto sample = std::make_shared<Sample>();
    sample->sampleRate = sourceRate;
    sample->left.resize(static_cast<std::size_t>(sourceRate));
    sample->right.resize(sample->left.size());
    for (std::size_t i = 0; i < sample->left.size(); ++i)
    {
        sample->left[i] = static_cast<float>(0.08 * std::sin(2 * pi * frequency * i / sourceRate));
        sample->right[i] = static_cast<float>(0.08 * std::sin(2 * pi * (stereo ? frequency * 2 : frequency) * i / sourceRate));
    }
    return sample;
}

TrackParams dry()
{
    TrackParams p;
    p.gain = 1.0f;
    p.attack = 0.0001f;
    p.decay = 30.0f;
    p.cutoff = 20000.0f;
    p.resonance = 0.0f;
    p.delaySend = p.reverbSend = 0.0f;
    return p;
}

void initialise(Engine& engine, std::shared_ptr<const Sample> sample = constant(), int block = 512)
{
    engine.prepare(sampleRate, block);
    engine.setSample(0, std::move(sample));
    engine.setTrackParams(0, dry());
    FxParams fx;
    fx.delayMix = fx.reverbMix = 0;
    engine.setFx(fx);
}

Audio triggerAudio(Engine& engine, int frames = 8192, int offset = 0, float pitch = 0.0f, float velocity = 1.0f)
{
    Audio audio(frames);
    TriggerEvent event{offset, 0, velocity, pitch};
    engine.process(audio.left.data(), audio.right.data(), frames, Transport{}, &event, 1);
    return audio;
}

std::vector<int> onsets(const std::vector<float>& audio)
{
    std::vector<int> result;
    bool active = false;
    for (int i = 0; i < static_cast<int>(audio.size()); ++i)
    {
        const bool next = std::abs(audio[i]) > 1.0e-5f;
        if (next && !active) result.push_back(i);
        active = next;
    }
    return result;
}

Audio sequenceAudio(Engine& engine, int frames, int block, bool host, double bpm = 120.0)
{
    Audio audio(frames);
    for (int begin = 0; begin < frames; begin += block)
    {
        Transport transport;
        transport.bpm = bpm;
        transport.playing = true;
        transport.hostPosition = host;
        transport.ppq = begin * bpm / (60.0 * sampleRate);
        const int size = std::min(block, frames - begin);
        engine.process(audio.left.data() + begin, audio.right.data() + begin, size, transport);
    }
    return audio;
}

double frequency(const std::vector<float>& values, int first = 1000, int last = 20000)
{
    int crossings = 0;
    for (int i = first + 1; i < last; ++i)
        if (values[i - 1] <= 0 && values[i] > 0) ++crossings;
    return crossings * sampleRate / (last - first);
}

void testSilenceAndDemoSamples()
{
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage;
    Audio audio(64);
    engine.process(audio.left.data(), audio.right.data(), 64, Transport{});
    require(energy(audio.left) == 0, "unprepared engine produces silence");
    engine.prepare(sampleRate, 64);
    engine.process(audio.left.data(), audio.right.data(), 64, Transport{});
    require(energy(audio.left) == 0 && energy(audio.right) == 0, "empty prepared engine produces silence");
    for (int track = 0; track < numTracks; ++track)
    {
        const auto sample = Engine::makeDemoSample(track, sampleRate);
        require(sample && !sample->name.empty() && sample->left.size() == sample->right.size(), "demo has named stereo channels");
        require(energy(sample->left) > 1, "each synthesized demo sample is audible");
        engine.setSample(track, sample);
        engine.setTrackParams(track, dry());
        TriggerEvent event{0, track, 0.8f, 0};
        Audio rendered(2048);
        engine.process(rendered.left.data(), rendered.right.data(), 2048, Transport{}, &event, 1);
        require(energy(rendered.left) > 0.0001, "every track can play its demo sample");
        bounded(rendered);
        engine.reset();
    }
}

void testExternalTriggerAndStereo()
{
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage;
    initialise(engine, sine(220, 24000, true));
    auto audio = triggerAudio(engine, 22000, 17);
    require(energy(audio.left, 0, 17) == 0, "external trigger obeys sample offset");
    require(energy(audio.left, 17, 22000) > 1, "external trigger produces audio");
    require(std::abs(frequency(audio.left) - 220) < 4, "source sample rate is resampled correctly");
    require(std::abs(frequency(audio.right) - 440) < 4, "stereo channels remain independent");
    bounded(audio);
    auto p = dry(); p.pan = -1;
    engine.reset(); engine.setTrackParams(0, p);
    audio = triggerAudio(engine, 2048);
    require(energy(audio.right) == 0 && energy(audio.left) > 0.1, "hard left pan removes right dry output");
    p.mute = true; engine.reset(); engine.setTrackParams(0, p);
    audio = triggerAudio(engine, 2048);
    require(energy(audio.left) == 0, "muted track ignores triggers");
}

void testSequencerTimingAndLength()
{
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage;
    initialise(engine);
    Step s; s.enabled = true; s.velocity = 1;
    for (int i = 0; i < 16; ++i) engine.setStep(0, i, s);
    auto audio = sequenceAudio(engine, 48000, 257, true);
    const std::vector<int> expected{0,6000,12000,18000,24000,30000,36000,42000};
    require(onsets(audio.left) == expected, "16th-note events are sample accurate across host blocks");
    require(engine.getCurrentStep(0) == 7, "playhead reports the last rendered step");
    engine.reset(); engine.setSwing(0.6f);
    audio = sequenceAudio(engine, 18000, 271, false);
    require(onsets(audio.left) == std::vector<int>({0,7801,12000}), "swing delays odd steps without moving even steps");
    engine.reset(); engine.setSwing(0); engine.setTrackLength(0, 128); engine.setStep(0,127,s);
    Audio finalStep(64);
    Transport t; t.playing = true; t.hostPosition = true; t.ppq = 127 * 0.25;
    engine.process(finalStep.left.data(), finalStep.right.data(),64,t);
    require(energy(finalStep.left) > 0.1 && engine.getCurrentStep(0) == 127, "all 128 sequencer steps are addressable");
    engine.setTrackLength(0, 3); engine.reset();
    audio = sequenceAudio(engine, 24000, 512, false);
    require(engine.getCurrentStep(0) == 0, "per-track length wraps the playhead");
}

void testConditionsProbabilityAndRetrigs()
{
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage;
    initialise(engine);
    engine.setTrackLength(0,1);
    Step s; s.enabled = true; s.velocity = 1; s.conditionEvery = 2; s.conditionOffset = 1;
    engine.setStep(0,0,s);
    auto audio = sequenceAudio(engine,24000,137,true);
    require(onsets(audio.left) == std::vector<int>({6000,18000}), "cycle conditions select the requested pattern cycles");
    s.probability = 0; engine.setStep(0,0,s); engine.reset();
    audio = sequenceAudio(engine,24000,137,false);
    require(energy(audio.left) == 0, "zero probability never plays");
    s.probability = 1; s.conditionEvery = 1; s.conditionOffset = 0; s.retrigs = 4;
    engine.setStep(0,0,s); engine.reset();
    audio = sequenceAudio(engine,6000,193,true);
    require(onsets(audio.left) == std::vector<int>({0,1500,3000,4500}), "retriggers divide each step evenly");
    s.retrigs = 1; s.microtiming = 0.2f; engine.setStep(0,0,s); engine.reset();
    audio = sequenceAudio(engine,6000,256,false);
    require(onsets(audio.left) == std::vector<int>({1201}), "positive microtiming shifts the hit");
    s.microtiming = -0.2f; engine.setStep(0,0,s); engine.reset();
    audio = sequenceAudio(engine,12000,256,true);
    require(onsets(audio.left) == std::vector<int>({4800,10800}), "negative microtiming plays subsequent hits early");
}

void testBlockInvariance()
{
    auto aStorage = std::make_unique<Engine>(); auto& a = *aStorage;
    auto bStorage = std::make_unique<Engine>(); auto& b = *bStorage;
    auto cStorage = std::make_unique<Engine>(); auto& c = *cStorage;
    for (Engine* engine : {&a,&b,&c})
    {
        initialise(*engine,constant(101),128);
        engine->setTrackLength(0,7);
        engine->setSwing(0.43f);
        for (int step=0; step<7; ++step)
        {
            Step s; s.enabled = true; s.probability = 0.73f; s.velocity = 0.4f + step * 0.08f;
            s.retrigs = step%3 + 1; s.microtiming = (step%2 ? -0.17f : 0.21f);
            engine->setStep(0,step,s);
        }
    }
    const auto whole = sequenceAudio(a,120000,120000,true,137.2);
    const auto split = sequenceAudio(b,120000,137,true,137.2);
    const auto internal = sequenceAudio(c,120000,733,false,137.2);
    require(energy(whole.left)>1, "probabilistic sequence remains audible");
    for (std::size_t i=0; i<whole.left.size(); ++i)
    {
        require(std::abs(whole.left[i]-split.left[i])<1.0e-6f, "host output is independent of block sizes");
        require(std::abs(whole.left[i]-internal.left[i])<1.0e-6f, "internal and host clocks produce identical sequences");
    }
}

void testPitchAndParameterLocks()
{
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage;
    initialise(engine,sine());
    auto p = dry(); p.pitch = 12;
    engine.setTrackParams(0,p);
    auto audio = triggerAudio(engine,22000);
    require(std::abs(frequency(audio.left)-440)<4, "track pitch transposes by an octave");
    engine.reset();
    audio = triggerAudio(engine,22000,0,-12);
    require(std::abs(frequency(audio.left)-220)<4, "external pitch combines with track tuning");
    Step step; step.enabled=true; step.velocity=1; step.lockPitch=true; step.pitch=0;
    engine.setStep(0,0,step); engine.reset();
    audio=sequenceAudio(engine,22000,512,true);
    require(std::abs(frequency(audio.left)-220)<4, "pitch lock replaces track tuning for that trig");
    initialise(engine,sine(5000));
    step.lockPitch=false; step.lockCutoff=true; step.cutoff=100;
    engine.setStep(0,0,step);
    const auto filtered=sequenceAudio(engine,4096,512,true);
    engine.reset(); step.lockCutoff=false; engine.setStep(0,0,step);
    const auto bright=sequenceAudio(engine,4096,512,true);
    require(energy(filtered.left)<energy(bright.left)*0.001, "cutoff lock changes only the selected trig's filter");
}

void testActiveVoiceAutomation()
{
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage;
    initialise(engine,sine());
    auto initial=triggerAudio(engine,512);
    auto p=dry(); p.pitch=12;
    engine.setTrackParams(0,p);
    Audio changed(22000);
    engine.process(changed.left.data(),changed.right.data(),22000,Transport{});
    require(std::abs(frequency(changed.left)-440)<4, "pitch automation updates an already-playing unlocked voice");

    engine.reset();
    Step step; step.enabled=true; step.velocity=1; step.lockPitch=true; step.pitch=0;
    engine.setStep(0,0,step);
    initial=sequenceAudio(engine,512,512,true);
    p.pitch=24; engine.setTrackParams(0,p);
    engine.process(changed.left.data(),changed.right.data(),22000,Transport{});
    require(std::abs(frequency(changed.left)-220)<4, "pitch lock survives changes to the base pitch during playback");

    initialise(engine,sine(5000));
    initial=triggerAudio(engine,512);
    p=dry(); p.cutoff=100; engine.setTrackParams(0,p);
    Audio dark(4096);
    engine.process(dark.left.data(),dark.right.data(),4096,Transport{});
    require(energy(dark.left,512,4096)<energy(initial.left)*0.001, "cutoff automation updates an already-playing unlocked voice");

    engine.reset();
    step.lockPitch=false; step.lockCutoff=true; step.cutoff=100;
    engine.setStep(0,0,step);
    initial=sequenceAudio(engine,512,512,true);
    p.cutoff=20000; engine.setTrackParams(0,p);
    engine.process(dark.left.data(),dark.right.data(),4096,Transport{});
    require(energy(dark.left,512,4096)<0.001, "cutoff lock survives changes to the base cutoff during playback");
}

void testTrimmingReverseAndLoop()
{
    auto ramp=std::make_shared<Sample>(); ramp->sampleRate=sampleRate;
    for (int i=0;i<2000;++i) ramp->left.push_back(0.02f+0.2f*i/2000);
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage; initialise(engine,ramp);
    auto forward=triggerAudio(engine,2400);
    auto p=dry(); p.reverse=true; engine.setTrackParams(0,p); engine.reset();
    auto reverse=triggerAudio(engine,2400);
    require(energy(reverse.left,32,200)>energy(forward.left,32,200)*10, "reverse playback starts at the end of the sample");
    p.reverse=false; p.start=0.25f; p.end=0.5f; engine.setTrackParams(0,p); engine.reset();
    auto trimmed=triggerAudio(engine,2400);
    require(energy(trimmed.left,0,500)>0.1 && energy(trimmed.left,500,2400)==0, "sample trim bounds the playback region");
    p.loop=true; engine.setTrackParams(0,p); engine.reset();
    auto loop=triggerAudio(engine,2400);
    require(energy(loop.left,2000,2400)>0.1, "loop playback wraps inside the trimmed region");
    auto trimmedSilence=std::make_shared<Sample>(); trimmedSilence->sampleRate=24000;
    trimmedSilence->left.assign(200,0.0f);
    std::fill(trimmedSilence->left.begin()+100,trimmedSilence->left.end(),1.0f);
    initialise(engine,trimmedSilence);
    p=dry(); p.end=0.5f; engine.setTrackParams(0,p);
    auto boundary=triggerAudio(engine,1024);
    require(energy(boundary.left)==0, "interpolation cannot read outside the trimmed sample region");
}

void testSampleReplacement()
{
    const auto original=constant(48000);
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage; initialise(engine,original);
    auto first=triggerAudio(engine,512);
    engine.setSample(0,original);
    Audio continuation(512);
    engine.process(continuation.left.data(),continuation.right.data(),512,Transport{});
    require(energy(continuation.left)>1, "reapplying the same sample preserves the playing voice");
    engine.setSample(0,constant(48000,0.1f));
    engine.process(continuation.left.data(),continuation.right.data(),512,Transport{});
    require(energy(continuation.left)==0, "replacing a sample stops only the replaced track's voice");
}

void testEnvelopeFilterDriveAndBitReduction()
{
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage; initialise(engine,constant(24000));
    auto p=dry(); p.attack=0.05f; engine.setTrackParams(0,p);
    auto slow=triggerAudio(engine,8192);
    require(energy(slow.left,0,480)<energy(slow.left,2400,2880)*0.05, "attack fades into the sample");
    p.attack=0.0001f; p.decay=0.02f; engine.reset(); engine.setTrackParams(0,p);
    auto decayed=triggerAudio(engine,8192);
    require(energy(decayed.left,4800,5280)<energy(decayed.left,100,580)*0.001, "decay attenuates sustained material");
    initialise(engine,sine(5000));
    auto bright=triggerAudio(engine,8192);
    p=dry(); p.cutoff=100; engine.reset(); engine.setTrackParams(0,p);
    auto dark=triggerAudio(engine,8192);
    require(energy(dark.left)<energy(bright.left)*0.001, "lowpass suppresses high frequencies");
    initialise(engine,constant(4096,0.02f));
    auto clean=triggerAudio(engine,4096);
    p=dry(); p.drive=1; engine.reset(); engine.setTrackParams(0,p);
    auto driven=triggerAudio(engine,4096);
    require(energy(driven.left)>energy(clean.left)*20, "drive audibly saturates quiet samples");
    p.drive=0; p.bitDepth=2; engine.reset(); engine.setTrackParams(0,p);
    auto crushed=triggerAudio(engine,4096);
    require(energy(crushed.left)==0, "low bit depth quantizes sub-threshold signal");
    engine.reset(); p=dry(); engine.setTrackParams(0,p);
    auto lowVelocity=triggerAudio(engine,4096,0,0,0.25f);
    require(energy(lowVelocity.left)>0 && energy(lowVelocity.left)<energy(clean.left)*0.08, "trigger velocity controls amplitude");
}

void testEffectTailsAndReset()
{
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage; initialise(engine);
    auto p=dry(); p.delaySend=1; engine.setTrackParams(0,p);
    FxParams fx; fx.delayMix=0.8f; fx.reverbMix=0; fx.delayBeats=0.25f; fx.feedback=0.4f; engine.setFx(fx);
    auto delay=triggerAudio(engine,19000);
    require(energy(delay.left,128,5999)==0, "tempo delay waits until the beat interval");
    require(energy(delay.left,6000,6100)>0.01, "delay repeats at the configured tempo division");
    require(energy(delay.right,12000,12100)>0.001, "delay feedback continues into the other channel");
    engine.reset(); Audio reset(8192);
    engine.process(reset.left.data(),reset.right.data(),8192,Transport{});
    require(energy(reset.left)==0 && energy(reset.right)==0, "reset clears voices and effect tails");
    p.delaySend=0; p.reverbSend=1; engine.setTrackParams(0,p);
    fx.delayMix=0; fx.reverbMix=1; engine.setFx(fx);
    auto reverb=triggerAudio(engine,12000);
    require(energy(reverb.left,1000,8000)>0.001, "algorithmic reverb creates an audible tail");
    bounded(delay); bounded(reverb);
}

void testFiniteInputsAndRealtimeAllocation()
{
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage; initialise(engine);
    auto p=dry();
    const float nan=std::numeric_limits<float>::quiet_NaN();
    p.gain=p.pan=p.pitch=p.cutoff=p.attack=p.decay=p.resonance=p.start=p.end=p.drive=p.bitDepth=nan;
    engine.setTrackParams(0,p);
    auto bad=std::make_shared<Sample>(); bad->sampleRate=std::numeric_limits<double>::infinity();
    bad->left={nan,std::numeric_limits<float>::infinity(),1,-1};
    engine.setSample(0,bad);
    auto audio=triggerAudio(engine,512); bounded(audio);
    engine.prepare(sampleRate,128);
    for (int track=0;track<numTracks;++track)
    {
        engine.setSample(track,constant(48000,0.99f));
        p=dry(); p.gain=2; p.drive=1; p.resonance=0.98f; p.loop=true; p.delaySend=p.reverbSend=1;
        engine.setTrackParams(track,p);
        Step step; step.enabled=true; step.velocity=1; step.retrigs=8;
        for (int i=0;i<16;++i) engine.setStep(track,i,step);
    }
    Audio output(32768); Transport transport; transport.playing=true; transport.bpm=400;
    allocations=0; countAllocations=true;
    engine.process(output.left.data(),output.right.data(),32768,transport);
    countAllocations=false;
    require(allocations==0, "audio processing performs no heap allocations");
    require(energy(output.left)>1, "all-track stress test produces audio");
    bounded(output);
    engine.setTrackLength(-1,999); engine.setTrackParams(16,p); engine.setSample(-1,nullptr);
    engine.setStep(0,128,Step{}); engine.process(nullptr,nullptr,64,Transport{});
    require(engine.getCurrentStep(-1)==0, "invalid track input is handled safely");
}

Audio machineAudio(TrackParams params, std::shared_ptr<const Sample> sample, int frames,
                   double bpm = 120.0, int note = 60, float pitch = 0.0f, int block = 257)
{
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage;
    initialise(engine, std::move(sample), 128);
    engine.setTrackParams(0, params);
    Audio result(frames);
    TriggerEvent trigger{0, 0, 1.0f, pitch, note};
    Transport transport; transport.bpm = bpm;
    for (int start = 0; start < frames; start += block)
    {
        const int size = std::min(block, frames - start);
        engine.process(result.left.data() + start, result.right.data() + start, size,
                       transport, start == 0 ? &trigger : nullptr, start == 0 ? 1 : 0);
    }
    return result;
}

void testMachineRegionsAndLegacy()
{
    auto source = std::make_shared<Sample>(); source->sampleRate = sampleRate;
    source->left.assign(4000, 0.02f);
    std::fill(source->left.begin(), source->left.begin() + 1000, 0.3f);
    auto p = dry(); p.machine = Machine::Oneshot; p.start = 0.25f; p.sourceLength = 0.25f;
    const auto trim = machineAudio(p, source, 4096);
    require(energy(trim.left, 20, 800) > 0.1 && energy(trim.left, 1000, 4096) == 0,
            "Oneshot LEN is relative to START and stops at START+LEN");
    p.start = 0.0f; p.sourceLength = 0.5f; p.loopPosition = 0.25f; p.playback = PlaybackMode::ForwardLoop;
    const auto introLoop = machineAudio(p, source, 8000);
    require(energy(introLoop.left, 100, 900) > energy(introLoop.left, 6100, 6900) * 100,
            "forward loop plays its introduction once then returns to LOOP, not START");
    p.playback = PlaybackMode::ReverseLoop;
    const auto reverseLoop = machineAudio(p, source, 8000);
    require(energy(reverseLoop.left, 6100, 6900) > 0.1
            && energy(reverseLoop.left, 6100, 6900) < energy(introLoop.left, 100, 900) * 0.02,
            "reverse loop stays between LOOP and START+LEN");
    auto resampled = std::make_shared<Sample>(); resampled->sampleRate = 24000;
    resampled->left.assign(16, 0.0f); std::fill(resampled->left.begin(), resampled->left.begin() + 8, .8f);
    p.start = 0; p.sourceLength = 1; p.loopPosition = .5f; p.playback = PlaybackMode::ForwardLoop;
    const auto seam = machineAudio(p, resampled, 256);
    require(energy(seam.left, 40, 256) < 1.0e-10,
            "resampling at the loop seam interpolates toward LOOP and never reintroduces START");
    auto legacy = dry(); legacy.start = 0.125f; legacy.end = 0.6f; legacy.loop = true;
    const auto original = machineAudio(legacy, source, 8192);
    legacy.sourceLength = 0.01f; legacy.bars = 64; legacy.playback = PlaybackMode::Reverse;
    legacy.slice = 9; legacy.sampleLevel = 0;
    legacy.lfos[0].destination = LfoDestination::Pitch; // depth zero remains exactly inert
    const auto preserved = machineAudio(legacy, source, 8192);
    require(original.left == preserved.left && original.right == preserved.right,
            "Legacy ignores new machine controls and disabled LFOs sample for sample");
    p = dry(); p.machine = Machine::Oneshot;
    const auto tunedNote = machineAudio(p, sine(), 22000, 120, 72);
    require(std::abs(frequency(tunedNote.left) - 440) < 4,
            "a new-machine trig NOTE transposes from MIDI C4=60 without changing track TUNE");
}

void testGridAndManualSlice()
{
    auto source = std::make_shared<Sample>(); source->sampleRate = sampleRate;
    source->left.assign(4000, 0.0f);
    std::fill(source->left.begin() + 2000, source->left.begin() + 3000, 0.2f);
    auto p = dry(); p.machine = Machine::Grid; p.sliceCount = 4; p.slice = 0;
    const auto silent = machineAudio(p, source, 2000);
    p.slice = 2;
    const auto grid = machineAudio(p, source, 2000);
    require(energy(silent.left) == 0 && energy(grid.left, 0, 1000) > 1 && energy(grid.left, 1000, 2000) == 0,
            "Grid selects only the requested equal-sized source slice");
    p.sliceByNote = true;
    const auto byNote = machineAudio(p, source, 2000, 120, 26, 12);
    require(byNote.left == grid.left, "NOTE selects slices from adapted MIDI C1=24 and does not transpose them");
    const auto wrappedNote = machineAudio(p, source, 2000, 120, 30, -12);
    require(wrappedNote.left == grid.left, "NOTE selection wraps after the last slice");
    p.machine = Machine::Slice; p.sliceByNote = false; p.slice = 1; p.sliceCount = 3;
    p.slicePoints[0] = {0.0f, 0.1f, 0.0f};
    p.slicePoints[1] = {0.5f, 0.55f, 0.525f};
    p.slicePoints[2] = {0.55f, 0.75f, 0.55f};
    auto manual = machineAudio(p, source, 1600);
    require(energy(manual.left, 0, 150) > 0.2 && energy(manual.left, 201, 1600) == 0,
            "Slice plays the manually stored nonuniform start and end markers");
    p.sliceLength = 2;
    manual = machineAudio(p, source, 1600);
    require(energy(manual.left, 500, 900) > 1 && energy(manual.left, 1001, 1600) == 0,
            "Slice LEN plays consecutive slices through the final slice end");
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage; initialise(engine, source); p.slice = 0; p.sliceLength = 1; engine.setTrackParams(0, p);
    Step step; step.enabled = true; step.velocity = 1; step.lockSlice = true; step.slice = 1;
    engine.setStep(0, 0, step);
    const auto locked = sequenceAudio(engine, 1600, 257, true);
    require(energy(locked.left, 0, 150) > 0.2, "a slice lock chooses the sequencer trig's slice instead of the track slice");
}

void testTempoMachines()
{
    auto p = dry(); p.machine = Machine::Repitch; p.bars = 0.5f;
    const auto normal = machineAudio(p, sine(), 50000, 120);
    const auto faster = machineAudio(p, sine(), 50000, 240);
    require(std::abs(frequency(normal.left) - 220) < 4 && std::abs(frequency(faster.left) - 440) < 4,
            "Repitch follows tempo by changing source playback speed and pitch");
    require(energy(normal.left, 30000, 45000) > 1 && energy(faster.left, 24000, 50000) == 0,
            "Repitch halves playback duration when tempo doubles");
    for (const auto machine : {Machine::Stretch, Machine::Werp})
    {
        p.machine = machine; p.segmentSize = 0.1f; p.segmentMode = PlaybackMode::ForwardLoop;
        const auto stretched = machineAudio(p, sine(), 50000, 240);
        require(std::abs(frequency(stretched.left) - 220) < 12,
                "independent Stretch/Werp preserve approximate source pitch when tempo doubles (machine "
                + std::to_string(static_cast<int>(machine)) + ", measured " + std::to_string(frequency(stretched.left)) + " Hz)");
        require(energy(stretched.left, 1000, 20000) > 1 && energy(stretched.left, 24000, 50000) == 0,
                "Stretch/Werp use BARS and host tempo for timeline duration");
        p.pitch = 12;
        const auto transposed = machineAudio(p, sine(), 50000, 240);
        require(std::abs(frequency(transposed.left) - 440) < 12
                && energy(transposed.left, 24000, 50000) == 0,
                "Stretch/Werp tune their grains independently of the tempo timeline");
        p.pitch = 0;
        const auto split = machineAudio(p, sine(), 30000, 137.2, 60, 0, 113);
        const auto whole = machineAudio(p, sine(), 30000, 137.2, 60, 0, 30000);
        require(split.left == whole.left, "granular timeline and pitch are independent of host block partitioning");
        bounded(transposed);
    }
}

void testLfoModesAndLegacyOptIn()
{
    auto p = dry(); p.machine = Machine::Oneshot; p.playback = PlaybackMode::ForwardLoop;
    auto& lfo = p.lfos[0]; lfo.destination = LfoDestination::Pan; lfo.wave = LfoWave::Square;
    lfo.speed = 16; lfo.multiplier = 32; lfo.depth = 127;
    const auto square = machineAudio(p, constant(48000), 48000);
    require(energy(square.right, 1000, 10000) > energy(square.left, 1000, 10000) * 100
            && energy(square.left, 14000, 22000) > energy(square.right, 14000, 22000) * 100,
            "bipolar LFO and documented SPD/MULT timing move pan across each half cycle");
    lfo.mode = LfoMode::Hold;
    const auto held = machineAudio(p, constant(48000), 48000);
    require(energy(held.right, 14000, 22000) > energy(held.left, 14000, 22000) * 100,
            "HOLD latches its value at the trig instead of following its free phase");
    lfo.mode = LfoMode::Half;
    const auto half = machineAudio(p, constant(48000), 48000);
    require(energy(half.left, 28000, 40000) > energy(half.right, 28000, 40000) * 100,
            "HALF stops and holds the value at the middle of the waveform");
    lfo.mode = LfoMode::Trigger; lfo.phase = 64;
    const auto phase = machineAudio(p, constant(48000), 10000);
    require(energy(phase.left, 1000, 8000) > energy(phase.right, 1000, 8000) * 100,
            "TRIGGER restarts from configured start phase");
    p.machine = Machine::Legacy; p.loop = true; lfo.phase = 0;
    const auto optedIn = machineAudio(p, constant(48000), 24000);
    require(energy(optedIn.left, 14000, 22000) > energy(optedIn.right, 14000, 22000) * 100,
            "explicit modulation also works on Legacy tracks without changing their saved default sound");
    p.machine = Machine::Oneshot;
    p.lfos[1] = lfo; p.lfos[1].destination = LfoDestination::Gain; p.lfos[1].depth = -32;
    p.lfos[2] = lfo; p.lfos[2].destination = LfoDestination::Cutoff; p.lfos[2].wave = LfoWave::Random;
    p.lfos[2].phase = 100; p.lfos[2].depth = 16;
    const auto whole = machineAudio(p, sine(), 48000, 127, 60, 0, 48000);
    const auto split = machineAudio(p, sine(), 48000, 127, 60, 0, 193);
    require(whole.left == split.left && energy(whole.left) > 1, "three independent LFOs and random slew remain block invariant");
    bounded(whole);
}

void testNewMachineRealtimeAndFinite()
{
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage; engine.prepare(sampleRate, 128);
    const auto source = sine();
    for (int track = 0; track < numTracks; ++track)
    {
        auto p = dry(); p.machine = track % 2 == 0 ? Machine::Stretch : Machine::Werp;
        p.playback = PlaybackMode::ForwardLoop; p.segmentMode = PlaybackMode::ReverseLoop;
        for (int i = 0; i < 3; ++i)
        {
            p.lfos[i].depth = 12; p.lfos[i].multiplier = 64;
            p.lfos[i].destination = i == 0 ? LfoDestination::Pitch : i == 1 ? LfoDestination::Cutoff : LfoDestination::Pan;
        }
        engine.setSample(track, source); engine.setTrackParams(track, p);
        Step step; step.enabled = true; engine.setStep(track, 0, step);
    }
    Audio output(8192); Transport transport; transport.playing = true; transport.bpm = 180;
    allocations = 0; countAllocations = true;
    engine.process(output.left.data(), output.right.data(), 8192, transport);
    countAllocations = false;
    require(allocations == 0 && energy(output.left) > 1, "sixteen granular tracks and 48 LFOs allocate no memory during rendering");
    bounded(output);
    auto p = dry(); p.machine = Machine::Stretch;
    p.sourceLength = p.loopPosition = p.bars = p.segmentSize = std::numeric_limits<float>::quiet_NaN();
    p.lfos[0].destination = LfoDestination::Pitch; p.lfos[0].depth = std::numeric_limits<float>::infinity();
    engine.reset(); engine.setTrackParams(0, p);
    auto invalid = triggerAudio(engine, 1024); bounded(invalid);
}

void testAdvancedConditionsAndNeighborOrder()
{
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage; initialise(engine);
    engine.setTrackLength(0, 4);
    auto left = dry(); left.pan = -1;
    auto right = dry(); right.pan = 1;
    engine.setTrackParams(0, left);
    engine.setTrackParams(1, right);
    engine.setSample(1, constant());
    engine.setTrackLength(1, 4);
    Step step; step.enabled = step.advanced = true;
    step.rule.condition = sequencer::Condition::Cycle;
    step.rule.cycleA = 2; step.rule.cycleB = 2;
    engine.setStep(0, 0, step);
    step.rule.condition = sequencer::Condition::Previous;
    engine.setStep(0, 1, step);
    step.rule.inverted = true;
    engine.setStep(0, 2, step);
    step.rule.condition = sequencer::Condition::Neighbor;
    step.rule.inverted = false;
    engine.setStep(1, 0, step);
    const auto split = sequenceAudio(engine, 48000, 97, true);
    require(onsets(split.left) == std::vector<int>({12000, 24000, 30000}),
            "A:B, PRE and inverted PRE share conditional memory in musical order");
    require(onsets(split.right) == std::vector<int>({24000}),
            "NEI reads the preceding track's condition at the same time, not a future block event");
    auto wholeStorage = std::make_unique<Engine>(); auto& whole = *wholeStorage; initialise(whole); whole.setTrackParams(0, left); whole.setTrackParams(1, right);
    whole.setSample(1, constant()); whole.setTrackLength(0, 4); whole.setTrackLength(1, 4);
    step.rule = {}; step.rule.condition = sequencer::Condition::Cycle;
    step.rule.cycleA = 2; step.rule.cycleB = 2; whole.setStep(0, 0, step);
    step.rule.condition = sequencer::Condition::Previous; whole.setStep(0, 1, step);
    step.rule.inverted = true; whole.setStep(0, 2, step);
    step.rule.condition = sequencer::Condition::Neighbor; step.rule.inverted = false;
    whole.setStep(1, 0, step);
    const auto oneBlock = sequenceAudio(whole, 48000, 48000, true);
    require(split.left == oneBlock.left && split.right == oneBlock.right,
            "conditional memory is evaluated once per occurrence independent of caller block partitions");

    auto firstStorage = std::make_unique<Engine>(); auto& first = *firstStorage; initialise(first); first.setTrackLength(0, 1);
    step.rule = {}; step.rule.condition = sequencer::Condition::First; first.setStep(0, 0, step);
    require(onsets(sequenceAudio(first, 24000, 127, false).left) == std::vector<int>({0}),
            "1ST only triggers the first cycle of a one-step track");
    first.reset(); first.setLastPatternCycle(true); step.rule.condition = sequencer::Condition::Last;
    first.setStep(0, 0, step);
    require(onsets(sequenceAudio(first, 12000, 127, false).left) == std::vector<int>({0, 6000}),
            "LST uses the explicit final-pattern-cycle signal");
    first.reset(); first.setLastPatternCycle(false); first.setFill(false);
    step.rule = {}; step.rule.fill = sequencer::Fill::On; first.setStep(0, 0, step);
    require(energy(sequenceAudio(first, 6000, 127, false).left) == 0.0, "FILL ON remains silent outside a fill");
    first.setFill(true);
    require(onsets(sequenceAudio(first, 6000, 127, false).left) == std::vector<int>({0}),
            "FILL ON takes effect while the clock is already running");
}

void testAdvancedRetrigsSpeedsAndProbability()
{
    const auto render = [](int block, int speed, bool retrig, float probability)
    {
        auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage; initialise(engine);
        auto p = dry(); p.speedIndex = speed; engine.setTrackParams(0, p);
        Step step; step.enabled = step.advanced = true; step.probability = probability;
        step.retrig.enabled = retrig; step.retrig.rateIndex = 12;
        step.noteLengthBeats = 1.0f;
        engine.setTrackLength(0, 16); engine.setStep(0, 0, step);
        return sequenceAudio(engine, 96000, block, true);
    };
    const auto retrigs = render(137, 4, true, 1.0f);
    require(onsets(retrigs.left) == std::vector<int>({0,3000,6000,9000,12000,15000,18000,21000}),
            "RATE retrigs stream across blocks for the note gate and stop before the gate end");
    const auto sameRetrigs = render(96000, 4, true, 1.0f);
    require(retrigs.left == sameRetrigs.left, "cross-block retrig trains are sample-exact across block sizes");
    require(onsets(render(127, 6, false, 1.0f).left) == std::vector<int>({0,48000}),
            "2x speed loops a sixteen-step track in two beats");
    require(onsets(render(127, 0, false, 1.0f).left) == std::vector<int>({0}),
            "1/8x speed stretches a sixteen-step track to thirty-two beats");

    const auto randomRender = [](int block)
    {
        auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage; initialise(engine); engine.setTrackLength(0, 1);
        Step step; step.enabled = step.advanced = true; step.probability = 0.5f;
        step.rule.condition = sequencer::Condition::Probability;
        engine.setStep(0, 0, step);
        return sequenceAudio(engine, 480000, block, true);
    };
    const auto randomSplit = randomRender(89);
    const auto randomWhole = randomRender(480000);
    const auto notes = onsets(randomSplit.left);
    require(randomSplit.left == randomWhole.left && notes.size() > 20 && notes.size() < 60,
            "PROB draws are fresh for each loop and unaffected by host block partitions");
    auto restartedStorage = std::make_unique<Engine>(); auto& restarted = *restartedStorage; initialise(restarted); restarted.setTrackLength(0, 1);
    Step step; step.enabled = step.advanced = true; step.probability = 0.5f;
    restarted.setStep(0, 0, step);
    const auto passOne = sequenceAudio(restarted, 480000, 113, true);
    restarted.restartSequencer();
    const auto passTwo = sequenceAudio(restarted, 480000, 113, true);
    require(onsets(passOne.left) != onsets(passTwo.left), "explicit restart refreshes the probability epoch");

    auto fadingStorage = std::make_unique<Engine>(); auto& fading = *fadingStorage; initialise(fading); fading.setTrackLength(0, 16);
    step.probability = 1.0f; step.retrig.enabled = true; step.retrig.rateIndex = 12;
    step.retrig.velocityFade = -64; step.retrig.fadeLengthBeats = 0.5;
    step.noteLengthBeats = 0.5; fading.setStep(0, 0, step);
    const auto fade = sequenceAudio(fading, 12000, 127, true);
    require(energy(fade.left, 0, 1000) > energy(fade.left, 9000, 10000) * 8,
            "VFAD progressively reduces retrig amplitudes across its LEN envelope");
}

void testLockTrigsAndSequencerRealtime()
{
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage; initialise(engine, sine());
    auto p = dry(); p.loop = true; engine.setTrackParams(0, p);
    Step note; note.enabled = note.advanced = true; engine.setStep(0, 0, note);
    Step lock; lock.advanced = lock.lockTrig = true; lock.lockPitch = true;
    lock.pitch = 12; lock.lfoTrig = false; engine.setStep(0, 1, lock);
    const auto audio = sequenceAudio(engine, 24000, 127, true);
    require(std::abs(frequency(audio.left, 1000, 5000) - 220) < 15
            && std::abs(frequency(audio.left, 7000, 21000) - 440) < 12,
            "lock trigs modify a sounding voice without resetting its amplitude envelope");
    engine.reset(); engine.setStep(0, 0, Step{});
    require(energy(sequenceAudio(engine, 12000, 127, true).left) == 0.0,
            "lock trigs without an active voice never start a new sample");

    engine.reset();
    for (int track = 0; track < numTracks; ++track)
    {
        engine.setSample(track, constant()); engine.setTrackParams(track, dry());
        note.retrig.enabled = true; note.retrig.rateIndex = 16;
        note.noteLengthBeats = 512; note.probability = 0.75f;
        note.rule.condition = sequencer::Condition::Probability;
        engine.setStep(track, 0, note);
    }
    Audio output(8192); Transport transport; transport.playing = true;
    allocations = 0; countAllocations = true;
    engine.process(output.left.data(), output.right.data(), 8192, transport);
    countAllocations = false;
    require(allocations == 0 && energy(output.left) > 1,
            "advanced scheduler, conditions and sixteen long retrig trains allocate no memory");
    bounded(output);
}

Audio renderDsp(TrackParams params, std::shared_ptr<const Sample> source, int frames,
                const TriggerEvent* events, int eventCount, ChorusParams chorus = {})
{
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage; initialise(engine, std::move(source), 127);
    engine.setTrackParams(0, params); engine.setChorus(chorus);
    Audio output(frames);
    engine.process(output.left.data(), output.right.data(), frames, Transport{}, events, eventCount);
    return output;
}

double toneEnergy(const std::vector<float>& values, double hz, int first = 1000, int last = 22000)
{
    double sineSum = 0.0, cosineSum = 0.0;
    for (int frame = first; frame < last; ++frame)
    {
        const double phase = 2.0 * pi * hz * frame / sampleRate;
        sineSum += values[frame] * std::sin(phase);
        cosineSum += values[frame] * std::cos(phase);
    }
    return (sineSum * sineSum + cosineSum * cosineSum) / ((last - first) * (last - first));
}

void testNewAmplitudeGatesAndVolume()
{
    auto p = dry(); p.machine = Machine::Oneshot; p.playback = PlaybackMode::ForwardLoop;
    p.amplitudeEnvelope = {EnvelopeMode::Ahd, .005f, .1f, .05f, .8f, .01f, false, true};
    const TriggerEvent note{0, 0, 1, 0, 60};
    const auto timed = renderDsp(p, sine(), 24000, &note, 1);
    require(energy(timed.left, 1000, 4000) > 1 && energy(timed.left, 9000, 24000) == 0,
            "AHD performs its attack, timed hold and finite decay on a looping source");

    std::array<TriggerEvent, 2> events{note, TriggerEvent{12000, 0, 0, 0, 60, 0, false, true}};
    p.amplitudeEnvelope.holdNote = true;
    const auto held = renderDsp(p, sine(), 24000, events.data(), 2);
    require(energy(held.left, 9000, 11000) > 1 && energy(held.left, 16000, 24000) == 0,
            "AHD HOLD NOTE sustains until MIDI release, then uses DEC rather than RELEASE");
    events[1].note = 61;
    const auto unmatched = renderDsp(p, sine(), 24000, events.data(), 2);
    require(energy(unmatched.left, 18000, 22000) > 1, "an unmatched note-off cannot release another note on the track");
    events[1].note = 60;

    p.amplitudeEnvelope.mode = EnvelopeMode::Adsr;
    p.amplitudeEnvelope.decay = .01f; p.amplitudeEnvelope.sustain = .6f;
    const auto released = renderDsp(p, sine(), 24000, events.data(), 2);
    require(energy(released.left, 9000, 11000) > .5 && energy(released.left, 13000, 24000) == 0,
            "ADSR sustains and finishes exactly after the external note-off release");
    auto audition = note; audition.gateBeats = .125f;
    const auto finiteAudition = renderDsp(p, sine(), 12000, &audition, 1);
    require(energy(finiteAudition.left, 1000, 2500) > .5 && energy(finiteAudition.left, 4000, 12000) == 0,
            "a finite mouse-audition gate prevents ADSR or HOLD NOTE from sticking without a held key");
    auto sequenceStorage = std::make_unique<Engine>(); auto& sequence = *sequenceStorage; initialise(sequence, sine()); sequence.setTrackParams(0, p);
    Step step; step.enabled = step.advanced = true; step.velocity = 1; step.noteLengthBeats = .125f;
    sequence.setStep(0, 0, step);
    const auto gate = sequenceAudio(sequence, 24000, 113, true);
    require(energy(gate.left, 1000, 2500) > .5 && energy(gate.left, 4000, 24000) == 0,
            "TRIG LEN closes the sequencer gate across host blocks and releases ADSR");

    p.amplitudeEnvelope.mode = EnvelopeMode::Ahd; p.amplitudeEnvelope.attack = .1f;
    p.amplitudeEnvelope.holdNote = true;
    events[1] = note; events[1].sampleOffset = 6000;
    const auto restarted = renderDsp(p, sine(), 10000, events.data(), 2);
    p.amplitudeEnvelope.reset = false;
    const auto continued = renderDsp(p, sine(), 10000, events.data(), 2);
    require(energy(continued.left, 6010, 6100) > energy(restarted.left, 6010, 6100) * 100,
            "AMP RESET off retains the envelope level when a note retriggers");
    p = dry(); p.machine = Machine::Oneshot;
    const auto unity = renderDsp(p, sine(), 20000, &note, 1);
    p.ampVolume = .5f;
    const auto half = renderDsp(p, sine(), 20000, &note, 1);
    p.ampVolume = 0;
    const auto zero = renderDsp(p, sine(), 20000, &note, 1);
    require(energy(half.left) > energy(unity.left) * .24 && energy(half.left) < energy(unity.left) * .26
            && energy(zero.left) == 0, "AMP VOL scales audio independently of SRC LEV and TRACK LEVEL");
}

void testIntegratedFilterMachinesEnvelopeAndBaseWidth()
{
    auto source = std::make_shared<Sample>(); source->sampleRate = sampleRate;
    source->left.resize(48000);
    for (int frame = 0; frame < 48000; ++frame)
        source->left[frame] = static_cast<float>(.03 * std::sin(2 * pi * 300 * frame / sampleRate)
                                             + .03 * std::sin(2 * pi * 7000 * frame / sampleRate));
    const TriggerEvent note{0, 0, 1, 0, 60};
    auto p = dry(); p.machine = Machine::Oneshot; p.cutoff = 1000;
    const auto low = renderDsp(p, source, 24000, &note, 1);
    p.filter.machine = FilterMachine::Multimode; p.filter.type = 1;
    const auto high = renderDsp(p, source, 24000, &note, 1);
    require(toneEnergy(low.left, 300) > toneEnergy(low.left, 7000) * 100
            && toneEnergy(high.left, 7000) > toneEnergy(high.left, 300) * 100,
            "Multimode TYPE audibly changes the actual engine's lowpass into highpass");
    p.filter.machine = FilterMachine::Lowpass4;
    const auto lp4 = renderDsp(p, source, 24000, &note, 1);
    require(toneEnergy(lp4.left, 7000) < toneEnergy(low.left, 7000) * .05
            && toneEnergy(lp4.left, 300) > toneEnergy(low.left, 300) * .5,
            "LP4 steepens rejection while preserving the low-frequency band");
    p.cutoff = 300; p.filter.machine = FilterMachine::Eq; p.filter.eqGain = 12; p.filter.eqQ = 2;
    const auto boost = renderDsp(p, source, 24000, &note, 1);
    p.filter.eqGain = -12;
    const auto cut = renderDsp(p, source, 24000, &note, 1);
    require(toneEnergy(boost.left, 300) > toneEnergy(cut.left, 300) * 150,
            "EQ GAIN controls a real peaking EQ rather than only a display value");
    p.filter.machine = FilterMachine::CombPlus; p.cutoff = 400; p.filter.combFeedback = .85f;
    const auto combPlus = renderDsp(p, source, 24000, &note, 1);
    p.filter.machine = FilterMachine::CombMinus;
    const auto combMinus = renderDsp(p, source, 24000, &note, 1);
    require(energy(combPlus.left) > .01 && energy(combMinus.left) > .01 && combPlus.left != combMinus.left,
            "COMB+ and COMB- are audible and provide opposite-feedback responses");
    bounded(combPlus); bounded(combMinus);
    p.filter.machine = FilterMachine::Legacy; p.filter.type = 0; p.cutoff = 1000;
    const auto legacyLow = renderDsp(p, source, 24000, &note, 1);
    p.filter.type = 1;
    const auto legacyHigh = renderDsp(p, source, 24000, &note, 1);
    require(toneEnergy(legacyLow.left, 300) > toneEnergy(legacyHigh.left, 300) * 40,
            "the hardware Legacy filter switch selects lowpass or highpass independently of old VST playback");

    p = dry(); p.machine = Machine::Oneshot; p.filter.base = 100;
    const auto base = renderDsp(p, source, 24000, &note, 1);
    p.filter.base = 0; p.filter.width = 60;
    const auto width = renderDsp(p, source, 24000, &note, 1);
    require(toneEnergy(base.left, 7000) > toneEnergy(base.left, 300) * 100
            && toneEnergy(width.left, 300) > toneEnergy(width.left, 7000) * 100,
            "BASE and WIDTH affect the serial highpass and lowpass band of the audio path");

    p = dry(); p.machine = Machine::Oneshot; p.cutoff = 200;
    p.filter.machine = FilterMachine::Multimode; p.filter.envDepth = 127;
    p.filter.envelope.attack = .0001f; p.filter.envelope.decay = .05f;
    const auto envelope = renderDsp(p, sine(4000), 10000, &note, 1);
    TriggerEvent noFilter = note; noFilter.filterTrig = false;
    const auto untriggered = renderDsp(p, sine(4000), 10000, &noFilter, 1);
    require(energy(envelope.left, 50, 300) > energy(untriggered.left, 50, 300) * 100
            && energy(envelope.left, 50, 300) > energy(envelope.left, 6000, 6250) * 100,
            "FLTR ENV sweeps audible frequency and FLT.T off leaves the envelope untriggered");
    p.filter.envDelay = .01f;
    const auto delayed = renderDsp(p, sine(4000), 10000, &note, 1);
    require(energy(delayed.left, 600, 800) > energy(delayed.left, 200, 400) * 100,
            "FLTR DEL delays the envelope attack while source audio continues immediately");
    p.filter.envDelay = 0; p.filter.envelope.decay = .001f;
    p.filter.envelope.sustain = 1; p.filter.envelope.release = .01f;
    std::array<TriggerEvent, 2> releasedNotes{note, TriggerEvent{1000, 0, 0, 0, 60, 0, false, true}};
    const auto released = renderDsp(p, sine(4000), 6000, releasedNotes.data(), 2);
    require(energy(released.left, 500, 800) > energy(released.left, 3000, 3300) * 100,
            "MIDI note-off releases the filter envelope even when the amplitude uses Legacy decay");
}

void testIntegratedTrackFxAndChorusRouting()
{
    const TriggerEvent note{0, 0, 1, 0, 60};
    auto p = dry(); p.machine = Machine::Oneshot;
    const auto original = renderDsp(p, sine(4000), 20000, &note, 1);
    p.trackFx.srr = 96;
    const auto srr = renderDsp(p, sine(4000), 20000, &note, 1);
    require(srr.left != original.left && energy(srr.left) > 1, "SRR modifies the engine's actual rendered signal");
    p.cutoff = 500; p.trackFx.srrPre = true;
    const auto before = renderDsp(p, sine(4000), 20000, &note, 1);
    p.trackFx.srrPre = false;
    const auto after = renderDsp(p, sine(4000), 20000, &note, 1);
    require(before.left != after.left && energy(before.left) > energy(after.left) * 5,
            "SRR ROUT changes audible filter routing rather than only automation state");
    p = dry(); p.machine = Machine::Oneshot; p.bitReduction = 4;
    const auto bits = renderDsp(p, sine(4000), 20000, &note, 1);
    require(bits.left != original.left && energy(bits.left) > 1, "hardware BR has its own audible 1..16-bit control");
    p = dry(); p.machine = Machine::Oneshot; p.cutoff = 450; p.drive = .8f;
    const auto preDrive = renderDsp(p, sine(300), 24000, &note, 1);
    p.trackFx.drivePre = false;
    const auto postDrive = renderDsp(p, sine(300), 24000, &note, 1);
    require(toneEnergy(postDrive.left, 900) > toneEnergy(preDrive.left, 900) * 3,
            "OD POST creates harmonics after filtering while OD PRE lets the filter attenuate them");

    p = dry(); p.machine = Machine::Oneshot; p.trackFx.chorusSend = 1;
    ChorusParams chorus; chorus.volume = 1;
    const auto wet = renderDsp(p, constant(64), 6000, &note, 1, chorus);
    require(energy(wet.left, 500, 2000) > .01, "track CHO SEND feeds an audible stereo chorus tail");
    auto stereoImpulse = std::make_shared<Sample>(); stereoImpulse->sampleRate = sampleRate;
    stereoImpulse->left.assign(64, .2f); stereoImpulse->right.assign(64, 0);
    const auto normal = renderDsp(p, stereoImpulse, 6000, &note, 1, chorus);
    chorus.width = -1;
    const auto swapped = renderDsp(p, stereoImpulse, 6000, &note, 1, chorus);
    require(energy(normal.left, 500, 2000) > .01 && energy(normal.right, 500, 2000) == 0
            && energy(swapped.right, 500, 2000) > .01 && energy(swapped.left, 500, 2000) == 0,
            "negative chorus WIDTH swaps the stereo wet channels without affecting the dry source");

    const auto routeTail = [&](bool reverb)
    {
        auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage; initialise(engine, constant(64)); engine.setTrackParams(0, p);
        FxParams effects; effects.delayMix = reverb ? 0 : 1; effects.feedback = 0;
        effects.delayBeats = .03125f; effects.reverbMix = reverb ? 1 : 0; engine.setFx(effects);
        ChorusParams route; route.volume = 0; route.delaySend = reverb ? 0 : 1;
        route.reverbSend = reverb ? 1 : 0; engine.setChorus(route);
        Audio out(10000); engine.process(out.left.data(), out.right.data(), 10000, Transport{}, &note, 1);
        return out;
    };
    require(energy(routeTail(false).left, 1100, 4000) > .01
            && energy(routeTail(true).left, 2500, 10000) > .001,
            "chorus independently routes its wet return into tempo delay and reverb");
    auto baselineStorage = std::make_unique<Engine>(); auto& baseline = *baselineStorage;
    auto restartedStorage = std::make_unique<Engine>(); auto& restarted = *restartedStorage;
    for (auto* engine : {&baseline, &restarted})
    {
        initialise(*engine, constant(64)); engine->setTrackParams(0, p); engine->setChorus(chorus);
        Audio first(256); engine->process(first.left.data(), first.right.data(), 256, Transport{}, &note, 1);
    }
    restarted.restartSequencer();
    Audio uninterrupted(2000), tail(2000);
    baseline.process(uninterrupted.left.data(), uninterrupted.right.data(), 2000, Transport{});
    restarted.process(tail.left.data(), tail.right.data(), 2000, Transport{});
    require(tail.left == uninterrupted.left && tail.right == uninterrupted.right && energy(tail.right) > .01,
            "a pattern restart preserves source voices and pending send-effect tails");
    restarted.reset(); restarted.process(tail.left.data(), tail.right.data(), 2000, Transport{});
    require(energy(tail.left) == 0 && energy(tail.right) == 0, "reset clears chorus as well as delay and reverb");
}

void testCompleteDspRealtimeAndInvalidControls()
{
    auto engineStorage = std::make_unique<Engine>(); auto& engine = *engineStorage; engine.prepare(sampleRate, 127); const auto source = sine();
    for (int track = 0; track < numTracks; ++track)
    {
        auto p = dry(); p.machine = track % 2 == 0 ? Machine::Stretch : Machine::Werp;
        p.playback = PlaybackMode::ForwardLoop; p.segmentMode = PlaybackMode::ReverseLoop;
        p.amplitudeEnvelope = {EnvelopeMode::Adsr, .002f, 0, .01f, .8f, .1f, false, true};
        p.filter.machine = static_cast<FilterMachine>(1 + track % 6);
        p.filter.envDepth = 32; p.filter.base = 4; p.filter.width = 110; p.filter.eqGain = 6;
        p.trackFx.srr = 20; p.trackFx.srrPre = track % 2 == 0;
        p.drive = .2f; p.trackFx.drivePre = track % 2 != 0;
        p.trackFx.chorusSend = .2f; p.bitReduction = 12; p.ampVolume = .6f;
        for (int lfo = 0; lfo < 3; ++lfo)
        {
            p.lfos[lfo].depth = 12; p.lfos[lfo].multiplier = 64;
            p.lfos[lfo].destination = lfo == 0 ? LfoDestination::Attack : lfo == 1 ? LfoDestination::Cutoff : LfoDestination::BitDepth;
        }
        engine.setSample(track, source); engine.setTrackParams(track, p);
        Step step; step.enabled = step.advanced = true; step.retrig.enabled = true;
        step.noteLengthBeats = .5f; step.retrig.rateIndex = 12; engine.setStep(track, 0, step);
    }
    ChorusParams chorus; chorus.volume = .3f; chorus.delaySend = chorus.reverbSend = .2f; engine.setChorus(chorus);
    Audio output(8192); Transport transport; transport.playing = true; transport.bpm = 180;
    allocations = deallocations = 0; countAllocations = true;
    engine.process(output.left.data(), output.right.data(), 8192, transport);
    countAllocations = false;
    require(allocations == 0 && deallocations == 0 && energy(output.left) > 1,
            "sixteen granular/envelope/filter/SRR tracks, 48 LFOs, retrigs and chorus allocate or free no memory");
    bounded(output);
    auto p = dry(); p.machine = Machine::Oneshot;
    p.amplitudeEnvelope.mode = static_cast<EnvelopeMode>(999);
    p.amplitudeEnvelope.attack = p.amplitudeEnvelope.sustain = std::numeric_limits<float>::quiet_NaN();
    p.filter.machine = static_cast<FilterMachine>(999); p.filter.envDepth = std::numeric_limits<float>::infinity();
    p.filter.envDelay = p.filter.envelope.release = p.trackFx.srr = p.bitReduction = std::numeric_limits<float>::quiet_NaN();
    p.ampVolume = std::numeric_limits<float>::infinity();
    engine.reset(); engine.setTrackParams(0, p); bounded(triggerAudio(engine, 1024));
}

void testMasterSaturationNumerics()
{
    float previous = -1.0f;
    double maxError = 0.0;
    for (int index = -100000; index <= 100000; ++index)
    {
        const float input = index * 0.001f;
        const float output = masterSoftClip(input);
        require(std::isfinite(output) && std::abs(output) <= 1.0f, "master saturation bounds every finite input");
        require(output >= previous - 1.0e-7f, "master saturation is monotonic");
        require(std::abs(output + masterSoftClip(-input)) < 1.0e-7f, "master saturation is odd and introduces no DC bias");
        maxError = std::max(maxError, static_cast<double>(std::abs(output - std::tanh(input))));
        previous = output;
    }
    require(maxError < .024, "bounded rational master approximation stays within 0.024 of tanh");
    require(masterSoftClip(0.0f) == 0.0f && masterSoftClip(std::numeric_limits<float>::max()) == 1.0f
            && masterSoftClip(std::numeric_limits<float>::lowest()) == -1.0f,
            "silence remains exact and huge finite bus sums cannot overflow the saturator");
    require(masterSoftClip(std::numeric_limits<float>::quiet_NaN()) == 0.0f
            && masterSoftClip(std::numeric_limits<float>::infinity()) == 0.0f,
            "nonfinite bus sums are safely silenced");
}

void testOrderedAndUnorderedMidiCursor()
{
    auto p = dry(); p.machine = Machine::Oneshot; p.playback = PlaybackMode::ForwardLoop;
    p.amplitudeEnvelope.mode = EnvelopeMode::Adsr;
    p.amplitudeEnvelope.attack = .0001f; p.amplitudeEnvelope.decay = .005f;
    p.amplitudeEnvelope.sustain = 1.0f; p.amplitudeEnvelope.release = .001f;
    // The unsorted caller order is intentional. At sample 17 OFF then ON must
    // leave the new note open; at sample 113 ON then OFF must release it.
    std::vector<TriggerEvent> events{
        {700, 0, .6f, 0, 60}, {17, 0, 0, 0, 60, 0, false, true},
        {0, 0, .8f, 0, 60}, {17, 0, .8f, 0, 72},
        {113, 0, .8f, 0, 60}, {2000, 0, 1, 0, 60},
        {113, 0, 0, 0, 60, 0, false, true}, {-5, 0, 1, 0, 60},
        {701, 0, 0, 0, 60, 0, false, true}, {9000, 0, 1, 0, 60}
    };
    auto sorted = events;
    std::stable_sort(sorted.begin(), sorted.end(), [](const TriggerEvent& a, const TriggerEvent& b)
    { return a.sampleOffset < b.sampleOffset; });
    auto first = std::make_unique<Engine>(), second = std::make_unique<Engine>(), third = std::make_unique<Engine>();
    for (auto* engine : {first.get(), second.get(), third.get()})
    { initialise(*engine, sine(), engine == third.get() ? 512 : 37); engine->setTrackParams(0, p); }
    Audio ordered(2000), unordered(2000), largerChunks(2000);
    allocations = deallocations = 0; countAllocations = true;
    first->process(ordered.left.data(), ordered.right.data(), 2000, {}, sorted.data(), static_cast<int>(sorted.size()));
    second->process(unordered.left.data(), unordered.right.data(), 2000, {}, events.data(), static_cast<int>(events.size()));
    third->process(largerChunks.left.data(), largerChunks.right.data(), 2000, {}, sorted.data(), static_cast<int>(sorted.size()));
    countAllocations = false;
    require(allocations == 0 && deallocations == 0, "sorted MIDI cursor and unsorted fallback allocate/free no memory");
    require(ordered.left == unordered.left && ordered.right == unordered.right
            && ordered.left == largerChunks.left && ordered.right == largerChunks.right,
            "sorted and unsorted external MIDI preserve equal-offset ordering and full-block offsets across chunks");
    require(energy(ordered.left, 40, 100) > .01 && energy(ordered.left, 200, 650) == 0.0,
            "OFF/ON leaves the new ADSR gate open while ON/OFF at one offset releases it");
    std::swap(sorted[4], sorted[5]); // reverse ON/OFF at offset 113
    first->reset(); Audio reversed(2000);
    first->process(reversed.left.data(), reversed.right.data(), 2000, {}, sorted.data(), static_cast<int>(sorted.size()));
    require(energy(reversed.left, 200, 650) > .01,
            "equal-offset MIDI is evaluated in caller order, rather than sorting notes by type");
}
}

int main()
{
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"silence and original demo samples",testSilenceAndDemoSamples},
        {"external triggers, resampling and stereo",testExternalTriggerAndStereo},
        {"sample-accurate sequencer and track length",testSequencerTimingAndLength},
        {"probability, conditions, retrigs and microtiming",testConditionsProbabilityAndRetrigs},
        {"host/internal clock block invariance",testBlockInvariance},
        {"pitch and per-step parameter locks",testPitchAndParameterLocks},
        {"active voice automation and lock preservation",testActiveVoiceAutomation},
        {"sample trim, reverse and loop",testTrimmingReverseAndLoop},
        {"sample synchronization and replacement",testSampleReplacement},
        {"envelope, filter, drive and bit reduction",testEnvelopeFilterDriveAndBitReduction},
        {"tempo delay, reverb and reset",testEffectTailsAndReset},
        {"finite output and allocation-free processing",testFiniteInputsAndRealtimeAllocation},
        {"Oneshot regions, loop position and exact Legacy path",testMachineRegionsAndLegacy},
        {"Grid, manual slices, NOTE and slice locks",testGridAndManualSlice},
        {"Repitch, Stretch and Werp tempo/pitch independence",testTempoMachines},
        {"three LFOs, modes, phase, slew and Legacy opt-in",testLfoModesAndLegacyOptIn},
        {"allocation-free granular/LFO stress and invalid inputs",testNewMachineRealtimeAndFinite},
        {"advanced conditions, PRE/NEI order, FILL and first/last cycles",testAdvancedConditionsAndNeighborOrder},
        {"advanced RATE/VFAD retrigs, track speed and seeded probability",testAdvancedRetrigsSpeedsAndProbability},
        {"lock trigs and allocation-free advanced sequencing",testLockTrigsAndSequencerRealtime},
        {"AHD/ADSR, hold, gate releases, reset and independent AMP VOL",testNewAmplitudeGatesAndVolume},
        {"integrated filter machines, FLT.T, envelopes and BASE/WIDTH",testIntegratedFilterMachinesEnvelopeAndBaseWidth},
        {"integrated BR/SRR/OD routing, chorus and send-effect tails",testIntegratedTrackFxAndChorusRouting},
        {"complete 16-track DSP allocation/free stress and invalid controls",testCompleteDspRealtimeAndInvalidControls},
        {"bounded master saturation, numerical accuracy and finite extremes",testMasterSaturationNumerics},
        {"sorted MIDI cursor, unsorted fallback and equal-offset note ordering",testOrderedAndUnorderedMidiCursor}
    };
    int failed=0;
    for (const auto& test:tests)
    {
        try { test.run(); std::cout<<"PASS "<<test.name<<'\n'; }
        catch (const std::exception& error) { ++failed; std::cerr<<"FAIL "<<test.name<<": "<<error.what()<<'\n'; }
    }
    std::cout<<(sizeof(tests)/sizeof(tests[0])-failed)<<"/"<<sizeof(tests)/sizeof(tests[0])<<" DSP checks passed\n";
    return failed==0 ? 0 : 1;
}
