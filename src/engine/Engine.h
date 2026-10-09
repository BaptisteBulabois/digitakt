#pragma once

#include "SequencerRules.h"
#include "AmpFilter.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace takt
{
constexpr int numTracks = 16;
constexpr int maxSteps = 128;

struct Sample
{
    std::vector<float> left, right;
    double sampleRate = 44100.0;
    std::string name;
};

// Legacy preserves the original playback path and is the default for old states.
enum class Machine { Legacy, Oneshot, Werp, Stretch, Repitch, Slice, Grid };
enum class PlaybackMode { Forward, Reverse, ForwardLoop, ReverseLoop };
enum class LfoWave { Triangle, Sine, Square, Saw, Exponential, Ramp, Random };
enum class LfoMode { Free, Trigger, Hold, One, Half };
enum class LfoDestination { None, Pitch, Cutoff, Gain, Pan, Start, Length, LoopPosition,
                            Slice, Drive, BitDepth, DelaySend, ReverbSend, Attack, Decay };
constexpr int maxSlices = 128;

struct SlicePoint
{
    float start = 0.0f, end = 0.0f, loop = 0.0f; // normalized source positions; end=0 uses an equal grid
};

struct LfoParams
{
    float speed = 16.0f, multiplier = 1.0f, fade = 0.0f, phase = 0.0f, depth = 0.0f;
    bool bpmSync = true;
    LfoWave wave = LfoWave::Triangle;
    LfoMode mode = LfoMode::Free;
    LfoDestination destination = LfoDestination::None;
};

struct TrackParams
{
    float gain = 0.8f, pan = 0.0f, pitch = 0.0f, cutoff = 18000.0f;
    float resonance = 0.1f, attack = 0.002f, decay = 1.0f;
    float drive = 0.0f, bitDepth = 16.0f, start = 0.0f, end = 1.0f;
    float delaySend = 0.12f, reverbSend = 0.1f;
    bool reverse = false, mute = false, loop = false;
    Machine machine = Machine::Legacy;
    PlaybackMode playback = PlaybackMode::Forward;
    float sourceLength = 1.0f, loopPosition = 0.0f, bars = 1.0f;
    float sampleLevel = 1.0f, segmentSize = 0.125f;
    float ampVolume = 1.0f, bitReduction = 16.0f;
    PlaybackMode segmentMode = PlaybackMode::Forward;
    int slice = 0, sliceLength = 1, sliceCount = 16;
    bool sliceByNote = false;
    int speedIndex = 4;
    EnvelopeParams amplitudeEnvelope{};
    FilterParams filter{};
    TrackFxParams trackFx{};
    std::array<SlicePoint, maxSlices> slicePoints{};
    std::array<LfoParams, 3> lfos{};
};

struct Step
{
    bool enabled = false;
    float velocity = 0.8f, probability = 1.0f, pitch = 0.0f, cutoff = 18000.0f;
    bool lockPitch = false, lockCutoff = false;
    int conditionEvery = 1, conditionOffset = 0, retrigs = 1;
    float microtiming = 0.0f;
    int note = 60, slice = 0;
    bool lockSlice = false, lfoTrig = true;
    bool advanced = false, lockTrig = false;
    sequencer::TrigRule rule{};
    sequencer::RetrigParams retrig{};
    float noteLengthBeats = 0.25f;
    bool filterTrig = true;
};

struct Transport
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = false, hostPosition = false;
};

struct FxParams
{
    float delayMix = 0.25f, feedback = 0.35f, delayBeats = 0.5f, reverbMix = 0.2f;
};

struct TriggerEvent
{
    int sampleOffset = 0, track = 0;
    float velocity = 1.0f, pitch = 0.0f;
    int note = 60;
    int slice = 0;
    bool lockSlice = false;
    bool noteOff = false;
    bool filterTrig = true;
    float gateBeats = -1.0f; // MIDI: open gate until note-off; GUI audition: finite gate.
};

// All setters and process are called from the audio thread. prepare is called
// while processing is suspended. process performs no allocations or locking.
class Engine
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void reset();
    void setSample(int track, std::shared_ptr<const Sample> sample);
    void setTrackParams(int track, const TrackParams& params);
    void setStep(int track, int step, const Step& value);
    void setTrackLength(int track, int length);
    void setFx(const FxParams& params);
    void setChorus(const ChorusParams& params);
    void setSwing(float swing);
    void setFill(bool fill);
    void setLastPatternCycle(bool last);
    void restartSequencer(bool preserveVoices = true);
    void process(float* left, float* right, int numSamples, const Transport& transport,
                 const TriggerEvent* events = nullptr, int numEvents = 0);
    int getCurrentStep(int track) const;
    static std::shared_ptr<const Sample> makeDemoSample(int track, double sampleRate = 44100.0);

private:
    struct Voice
    {
        double position = 0.0, increment = 1.0;
        std::size_t first = 0, last = 0;
        std::uint64_t age = 0;
        float velocity = 0.0f, cutoff = 18000.0f, triggerPitch = 0.0f;
        float lowL = 0.0f, bandL = 0.0f, lowR = 0.0f, bandR = 0.0f;
        float filterG = 0.0f, filterK = 1.0f;
        bool active = false, pitchLocked = false, cutoffLocked = false;
        Machine machine = Machine::Legacy;
        bool looping = false, reversed = false;
        std::size_t loopFirst = 0, sourceStart = 0;
        double timelineIncrement = 1.0;
        int note = 60, slice = 0;
        bool sliceLocked = false;
        bool gateOpen = true, filterEnvelopePending = false;
        double gateRemainingBeats = -1.0, filterEnvelopeDelayFrames = 0.0;
    };
    struct ScheduledEvent
    {
        int offset = 0, track = 0;
        float velocity = 0.0f, pitch = 0.0f, cutoff = 18000.0f;
        bool lockPitch = false, lockCutoff = false;
        int note = 60, slice = 0;
        bool lockSlice = false, lfoTrig = true;
        bool lockTrig = false;
        float gateBeats = 0.25f;
        bool filterTrig = true;
    };
    struct InitialEvent
    {
        double at = 0.0;
        int track = 0;
        std::int64_t index = 0;
    };
    struct RetrigTrain
    {
        bool active = false;
        Step step{};
        double start = 0.0, next = 0.0;
        int repeat = 0, count = 0;
    };
    struct LfoState
    {
        double phase = 0.0, elapsed = 0.0, age = 0.0;
        float held = 0.0f, smoothed = 0.0f;
        std::int64_t cycle = 0;
        bool running = false;
    };
    struct ReverbLine
    {
        std::vector<float> data;
        std::size_t cursor = 0;
        float damp = 0.0f;
    };

    void trigger(int track, float velocity, float pitch, bool lockPitch,
                 float cutoff, bool lockCutoff, int note = 60, int slice = 0,
                 bool lockSlice = false, bool lfoTrig = true, float gateBeats = -1.0f,
                 bool filterTrig = true);
    void noteOff(int track, int note = -1);
    void renderVoice(int track, float& left, float& right);
    void updateVoiceCoefficients(int track);
    void updateLfos(int track, bool advance = true);
    void triggerLfos(int track);
    void renderMachine(int track, float& left, float& right);
    void configureMachineRegion(int track, Voice& voice, const TrackParams& params);
    void applyLockTrig(const ScheduledEvent& event);
    int schedule(double ppq, double beatsPerSample, int samples);
    double stepTime(std::int64_t index) const;
    double trackStepTime(int track, std::int64_t index) const;

    double sampleRate_ = 44100.0, internalPpq_ = 0.0;
    int blockSize_ = 512;
    float swing_ = 0.0f;
    FxParams fx_;
    std::array<std::shared_ptr<const Sample>, numTracks> samples_{};
    std::array<TrackParams, numTracks> params_{};
    std::array<TrackParams, numTracks> modulated_{};
    std::array<std::array<LfoState, 3>, numTracks> lfoStates_{};
    std::array<std::array<Step, maxSteps>, numTracks> steps_{};
    std::array<int, numTracks> lengths_{{16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16}};
    std::array<int, numTracks> currentSteps_{};
    std::array<Voice, numTracks> voices_{};
    std::array<Envelope, numTracks> amplitudeEnvelopes_{}, filterEnvelopes_{};
    std::array<StereoFilter, numTracks> filters_{};
    std::array<StereoTrackFx, numTracks> trackFx_{};
    Chorus chorus_;
    ChorusParams chorusParams_{};
    std::array<ScheduledEvent, 4096> scheduled_{};
    std::array<InitialEvent, 4096> initialEvents_{};
    std::array<RetrigTrain, numTracks> retrigTrains_{};
    std::array<sequencer::ConditionMemory, numTracks> conditionMemory_{};
    std::array<std::int64_t, numTracks> firstCycles_{};
    std::uint64_t probabilityEpoch_ = 0;
    bool fill_ = false, lastPatternCycle_ = false, wasPlaying_ = false;
    bool pendingOrigin_ = false;
    double sequenceOriginPpq_ = 0.0, expectedPpq_ = 0.0;
    std::vector<float> delayL_, delayR_;
    std::size_t delayCursor_ = 0;
    std::array<ReverbLine, 4> reverb_{};
    bool prepared_ = false;
    double currentBpm_ = 120.0;
};
}
