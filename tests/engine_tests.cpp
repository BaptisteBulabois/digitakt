#include "engine/Engine.h"

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
thread_local bool countAllocations = false;
thread_local std::size_t allocations = 0;
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void releaseAllocation(void* pointer) noexcept { std::free(pointer); }
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
    Engine engine;
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
    Engine engine;
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
    Engine engine;
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
    Engine engine;
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
    Engine a, b, c;
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
    Engine engine;
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
    Engine engine;
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
    Engine engine; initialise(engine,ramp);
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
    Engine engine; initialise(engine,original);
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
    Engine engine; initialise(engine,constant(24000));
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
    Engine engine; initialise(engine);
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
    Engine engine; initialise(engine);
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
        {"finite output and allocation-free processing",testFiniteInputsAndRealtimeAllocation}
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
