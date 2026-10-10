#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace takt
{
// Independent DSP, based on the documented controls rather than hardware code.
// Prototype/Legacy envelope leave the published VST path available to the host.
enum class EnvelopeMode { Legacy, Ahd, Adsr };
struct EnvelopeParams
{
    EnvelopeMode mode = EnvelopeMode::Legacy;
    float attack = 0.002f, hold = 0.0f, decay = 1.0f, sustain = 0.8f, release = 0.2f;
    bool holdNote = false, reset = true;
};

class Envelope
{
public:
    void reset();
    void trigger(const EnvelopeParams& params);
    void noteOff(const EnvelopeParams& params);
    float next(const EnvelopeParams& params, double sampleRate);
    bool isActive() const { return active_; }
    float value() const { return level_; }

private:
    enum class Stage { Attack, Hold, Decay, Sustain, Release, Off };
    void begin(Stage stage);
    Stage stage_ = Stage::Off;
    std::uint64_t age_ = 0, totalAge_ = 0;
    float level_ = 0.0f, initial_ = 0.0f;
    bool active_ = false, gate_ = false;
};

enum class FilterMachine { Prototype, Multimode, Lowpass4, Eq, CombMinus, CombPlus, Legacy };
struct FilterParams
{
    FilterMachine machine = FilterMachine::Prototype;
    float type = 0.0f, eqGain = 0.0f, eqQ = 1.0f;
    float combFeedback = 0.5f, combLowpassHz = 18000.0f;
    float base = 0.0f, width = 127.0f, keytrack = 0.0f;
    bool bwPre = true;
    float envDepth = 0.0f, envDelay = 0.0f;
    EnvelopeParams envelope{EnvelopeMode::Adsr, 0.002f, 0.0f, 1.0f, 0.0f, 0.2f, false, true};
};

class StereoFilter
{
public:
    void prepare(double sampleRate);
    void reset();
    void process(float& left, float& right, const FilterParams& params,
                 float cutoff, float resonance, float envelope = 0.0f, int note = 60);

private:
    struct State
    {
        float low = 0.0f, band = 0.0f;
        float z1 = 0.0f, z2 = 0.0f, bwLow = 0.0f, bwHighLow = 0.0f, combLow = 0.0f;
    };
    struct Coefficients
    {
        bool valid = false;
        FilterMachine machine = FilterMachine::Prototype;
        float cutoff = 0.0f, keytrack = 0.0f, envDepth = 0.0f, envelope = 0.0f;
        int note = 0;
        double frequency = 0.0;
        float resonance = 0.0f, type = 0.0f, g = 0.0f, k = 1.0f, a = 1.0f, a4 = 1.0f;
        float base = 0.0f, width = 127.0f, highpassAlpha = 0.0f, lowpassAlpha = 0.0f;
        float eqGain = 0.0f, eqQ = 1.0f, b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a2 = 0.0f;
        float combLowpassHz = 0.0f, combAlpha = 0.0f, combFeedback = 0.0f, feedback = 0.0f;
        std::size_t combWhole = 1;
        float combFraction = 0.0f;
    } coefficients_;
    void updateCoefficients(const FilterParams& params, float cutoff, float resonance, float envelope, int note);
    float machine(float input, int channel, const FilterParams& params);
    float baseWidth(float input, int channel);
    double sampleRate_ = 44100.0;
    std::array<std::array<State, 2>, 2> stages_{};
    std::array<std::vector<float>, 2> comb_;
    std::size_t combCursor_ = 0;
};

struct TrackFxParams
{
    float srr = 0.0f, chorusSend = 0.0f;
    bool srrPre = true, drivePre = true;
};

class StereoTrackFx
{
public:
    void reset();
    void rateReduction(float& left, float& right, float amount);
    static void overdrive(float& left, float& right, float amount);
    static void overdrivePrepared(float& left, float& right, float drive, float inverse);

private:
    int countdown_ = 0;
    int reductionFrames_ = 1;
    float cachedReduction_ = -1.0f;
    float heldLeft_ = 0.0f, heldRight_ = 0.0f;
};

struct ChorusParams
{
    float depth = 0.5f, speed = 0.5f, highpassHz = 20.0f, width = 1.0f, volume = 0.0f;
    float delaySend = 0.0f, reverbSend = 0.0f;
};

class Chorus
{
public:
    void prepare(double sampleRate);
    void reset();
    void process(float left, float right, const ChorusParams& params, float& wetLeft, float& wetRight);

private:
    std::array<std::vector<float>, 2> delay_;
    std::array<float, 2> low_{};
    double sampleRate_ = 44100.0, phase_ = 0.0;
    float cachedHighpass_ = -1.0f, highpassAlpha_ = 0.0f;
    std::size_t cursor_ = 0;
};
}
