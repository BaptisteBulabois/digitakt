#pragma once

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

struct TrackParams
{
    float gain = 0.8f, pan = 0.0f, pitch = 0.0f, cutoff = 18000.0f;
    float resonance = 0.1f, attack = 0.002f, decay = 1.0f;
    float drive = 0.0f, bitDepth = 16.0f, start = 0.0f, end = 1.0f;
    float delaySend = 0.12f, reverbSend = 0.1f;
    bool reverse = false, mute = false, loop = false;
};

struct Step
{
    bool enabled = false;
    float velocity = 0.8f, probability = 1.0f, pitch = 0.0f, cutoff = 18000.0f;
    bool lockPitch = false, lockCutoff = false;
    int conditionEvery = 1, conditionOffset = 0, retrigs = 1;
    float microtiming = 0.0f;
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
    void setSwing(float swing);
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
    };
    struct ScheduledEvent
    {
        int offset = 0, track = 0;
        float velocity = 0.0f, pitch = 0.0f, cutoff = 18000.0f;
        bool lockPitch = false, lockCutoff = false;
    };
    struct ReverbLine
    {
        std::vector<float> data;
        std::size_t cursor = 0;
        float damp = 0.0f;
    };

    void trigger(int track, float velocity, float pitch, bool lockPitch,
                 float cutoff, bool lockCutoff);
    void renderVoice(int track, float& left, float& right);
    void updateVoiceCoefficients(int track);
    int schedule(double ppq, double beatsPerSample, int samples);
    double stepTime(std::int64_t index) const;

    double sampleRate_ = 44100.0, internalPpq_ = 0.0;
    int blockSize_ = 512;
    float swing_ = 0.0f;
    FxParams fx_;
    std::array<std::shared_ptr<const Sample>, numTracks> samples_{};
    std::array<TrackParams, numTracks> params_{};
    std::array<std::array<Step, maxSteps>, numTracks> steps_{};
    std::array<int, numTracks> lengths_{{16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16}};
    std::array<int, numTracks> currentSteps_{};
    std::array<Voice, numTracks> voices_{};
    std::array<ScheduledEvent, 4096> scheduled_{};
    std::vector<float> delayL_, delayR_;
    std::size_t delayCursor_ = 0;
    std::array<ReverbLine, 4> reverb_{};
    bool prepared_ = false;
};
}
