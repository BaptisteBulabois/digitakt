#include "AmpFilter.h"

#include <algorithm>
#include <cmath>

namespace takt
{
namespace
{
constexpr double pi = 3.14159265358979323846;
template <typename T> T safe(T value, T low, T high)
{
    return std::isfinite(value) ? std::clamp(value, low, high) : low;
}
float clean(float value)
{
    return std::isfinite(value) && std::abs(value) > 1.0e-20f ? value : 0.0f;
}
float ramp(std::uint64_t age, double frames)
{
    return static_cast<float>(std::clamp((age + 1.0) / std::max(1.0, frames), 0.0, 1.0));
}
float curve(float progress)
{
    return static_cast<float>((std::exp(-6.0 * progress) - std::exp(-6.0)) / (1.0 - std::exp(-6.0)));
}
double controlHz(float value)
{
    return 20.0 * std::exp(std::log(1000.0) * safe(value, 0.0f, 127.0f) / 127.0);
}
}

void Envelope::reset()
{
    stage_ = Stage::Off;
    age_ = totalAge_ = 0;
    level_ = initial_ = 0.0f;
    active_ = gate_ = false;
}

void Envelope::begin(Stage stage)
{
    stage_ = stage;
    age_ = 0;
    initial_ = level_;
}

void Envelope::trigger(const EnvelopeParams& params)
{
    if (params.reset || !active_) level_ = 0.0f;
    active_ = gate_ = true;
    totalAge_ = 0;
    begin(Stage::Attack);
}

void Envelope::noteOff(const EnvelopeParams& params)
{
    gate_ = false;
    if (!active_) return;
    if (params.mode == EnvelopeMode::Adsr) begin(Stage::Release);
    else if (params.mode == EnvelopeMode::Ahd && params.holdNote) begin(Stage::Decay);
}

float Envelope::next(const EnvelopeParams& params, double sampleRate)
{
    if (!active_) return 0.0f;
    const double rate = safe(sampleRate, 8000.0, 384000.0);
    const double attack = safe(params.attack, 0.0f, 30.0f) * rate;
    const double decay = safe(params.decay, 0.0f, 60.0f) * rate;
    if (params.mode == EnvelopeMode::Legacy)
    {
        level_ = static_cast<float>(std::min(1.0, (totalAge_ + 1.0) / std::max(1.0, attack))
                    * std::exp(-static_cast<double>(totalAge_) / std::max(1.0, decay)));
        ++totalAge_;
        if (level_ < 1.0e-6f && totalAge_ > attack) { active_ = false; level_ = 0.0f; }
        return level_;
    }
    switch (stage_)
    {
        case Stage::Attack:
        {
            const float progress = ramp(age_++, attack);
            level_ = initial_ + (1.0f - initial_) * progress;
            if (progress >= 1.0f) begin(params.mode == EnvelopeMode::Ahd ? Stage::Hold : Stage::Decay);
            break;
        }
        case Stage::Hold:
            level_ = 1.0f;
            if ((!params.holdNote && ramp(age_++, safe(params.hold, 0.0f, 60.0f) * rate) >= 1.0f)
                || (params.holdNote && !gate_)) begin(Stage::Decay);
            break;
        case Stage::Decay:
        {
            const float target = params.mode == EnvelopeMode::Adsr ? safe(params.sustain, 0.0f, 1.0f) : 0.0f;
            const float progress = ramp(age_++, decay);
            level_ = target + (initial_ - target) * curve(progress);
            if (progress >= 1.0f)
            {
                level_ = target;
                if (params.mode == EnvelopeMode::Adsr && gate_) begin(Stage::Sustain);
                else { stage_ = Stage::Off; active_ = false; }
            }
            break;
        }
        case Stage::Sustain:
            level_ = safe(params.sustain, 0.0f, 1.0f);
            if (!gate_) begin(Stage::Release);
            break;
        case Stage::Release:
        {
            const float progress = ramp(age_++, safe(params.release, 0.0f, 60.0f) * rate);
            level_ = initial_ * curve(progress);
            if (progress >= 1.0f) { stage_ = Stage::Off; active_ = false; level_ = 0.0f; }
            break;
        }
        case Stage::Off: active_ = false; level_ = 0.0f; break;
    }
    ++totalAge_;
    return clean(level_);
}

void StereoFilter::prepare(double sampleRate)
{
    sampleRate_ = safe(sampleRate, 8000.0, 384000.0);
    for (auto& line : comb_) line.assign(static_cast<std::size_t>(std::ceil(sampleRate_ / 20.0)) + 4, 0.0f);
    reset();
}

void StereoFilter::reset()
{
    coefficients_ = {};
    stages_ = {};
    for (auto& line : comb_) std::fill(line.begin(), line.end(), 0.0f);
    combCursor_ = 0;
}

void StereoFilter::updateCoefficients(const FilterParams& params, float cutoff, float resonance, float envelope, int note)
{
    auto& c = coefficients_;
    const bool changedMachine = !c.valid || c.machine != params.machine;
    const float keytrack = safe(params.keytrack, 0.0f, 1.0f), envDepth = safe(params.envDepth, -128.0f, 128.0f);
    envelope = envDepth == 0.0f ? 0.0f : safe(envelope, 0.0f, 1.0f);
    note = keytrack == 0.0f ? 60 : std::clamp(note, 0, 127);
    cutoff = std::isfinite(cutoff) ? cutoff : 0.0f;
    const bool changedFrequency = !c.valid || c.cutoff != cutoff || c.keytrack != keytrack
        || c.note != note || c.envDepth != envDepth || c.envelope != envelope;
    if (changedFrequency)
    {
        const double semitones = (note - 60) * keytrack;
        const double octaves = envDepth / 128.0 * envelope * 8.0;
        const double exponent = semitones / 12.0 + octaves;
        c.frequency = safe(static_cast<double>(cutoff) * (exponent == 0.0 ? 1.0 : std::exp2(exponent)), 20.0, sampleRate_ * 0.45);
        c.cutoff = cutoff; c.keytrack = keytrack; c.note = note; c.envDepth = envDepth; c.envelope = envelope;
    }
    resonance = safe(resonance, 0.0f, 0.98f);
    const bool isComb = params.machine == FilterMachine::CombMinus || params.machine == FilterMachine::CombPlus;
    if (!isComb && params.machine != FilterMachine::Eq && (changedMachine || changedFrequency || c.resonance != resonance))
    {
        if (changedMachine || changedFrequency) c.g = static_cast<float>(std::tan(pi * c.frequency / sampleRate_));
        c.k = 2.0f - 1.9f * resonance;
        c.a = 1.0f / (1.0f + c.g * (c.g + c.k));
        c.a4 = 1.0f / (1.0f + c.g * (c.g + std::sqrt(2.0f)));
    }
    c.resonance = resonance; c.type = safe(params.type, 0.0f, 1.0f);
    const float base = safe(params.base, 0.0f, 127.0f), width = safe(params.width, 0.0f, 127.0f);
    if (!c.valid || c.base != base || c.width != width)
    {
        c.highpassAlpha = base > 0.0f ? static_cast<float>(1.0 - std::exp(-2.0 * pi * std::min(controlHz(base), sampleRate_ * 0.45) / sampleRate_)) : 0.0f;
        c.lowpassAlpha = width < 127.0f ? static_cast<float>(1.0 - std::exp(-2.0 * pi * std::min(controlHz(std::min(127.0f, base + width)), sampleRate_ * 0.45) / sampleRate_)) : 0.0f;
        c.base = base; c.width = width;
    }
    if (params.machine == FilterMachine::Eq)
    {
        const float gainDb = safe(params.eqGain, -24.0f, 24.0f), q = safe(params.eqQ, 0.1f, 20.0f);
        if (changedMachine || changedFrequency || c.eqGain != gainDb || c.eqQ != q)
        {
            const double gain = std::pow(10.0, gainDb / 40.0);
            const double omega = 2.0 * pi * c.frequency / sampleRate_;
            const double alpha = std::sin(omega) / (2.0 * q), a0 = 1.0 + alpha / gain;
            c.b0 = static_cast<float>((1.0 + alpha * gain) / a0);
            c.b1 = static_cast<float>(-2.0 * std::cos(omega) / a0);
            c.b2 = static_cast<float>((1.0 - alpha * gain) / a0);
            c.a2 = static_cast<float>((1.0 - alpha / gain) / a0);
            c.eqGain = gainDb; c.eqQ = q;
        }
    }
    if (isComb)
    {
        if ((changedMachine || changedFrequency) && !comb_[0].empty())
        {
            const double delay = std::clamp(sampleRate_ / c.frequency, 1.0, static_cast<double>(comb_[0].size() - 2));
            c.combWhole = static_cast<std::size_t>(std::floor(delay));
            c.combFraction = static_cast<float>(delay - c.combWhole);
        }
        const float lowpass = safe(params.combLowpassHz, 20.0f, static_cast<float>(sampleRate_ * 0.45));
        if (changedMachine || c.combLowpassHz != lowpass)
        {
            c.combAlpha = static_cast<float>(1.0 - std::exp(-2.0 * pi * lowpass / sampleRate_));
            c.combLowpassHz = lowpass;
        }
        c.combFeedback = safe(params.combFeedback, 0.0f, 0.98f);
        c.feedback = c.combFeedback * (params.machine == FilterMachine::CombMinus ? -1.0f : 1.0f);
    }
    c.machine = params.machine; c.valid = true;
}

float StereoFilter::baseWidth(float input, int channel)
{
    auto& state = stages_[0][static_cast<std::size_t>(channel)];
    const auto& c = coefficients_;
    if (c.base > 0.0f)
    {
        state.bwHighLow = clean(state.bwHighLow + c.highpassAlpha * (input - state.bwHighLow));
        input -= state.bwHighLow;
    }
    if (c.width < 127.0f)
    {
        state.bwLow = clean(state.bwLow + c.lowpassAlpha * (input - state.bwLow));
        input = state.bwLow;
    }
    return clean(input);
}

float StereoFilter::machine(float input, int channel, const FilterParams& params)
{
    auto& state = stages_[0][static_cast<std::size_t>(channel)];
    const auto& c = coefficients_;
    const auto svf = [&](float signal, State& stage, float damping, float a, float morph)
    {
        const float band = a * (stage.band + c.g * (signal - stage.low));
        const float low = stage.low + c.g * band;
        stage.band = clean(2.0f * band - stage.band);
        stage.low = clean(2.0f * low - stage.low);
        const float high = signal - damping * band - low;
        return clean(morph <= 0.5f ? low + morph * 2.0f * (band - low)
                                  : band + (morph - 0.5f) * 2.0f * (high - band));
    };
    switch (params.machine)
    {
        case FilterMachine::Prototype: return svf(input, state, c.k, c.a, 0.0f);
        case FilterMachine::Multimode: return svf(input, state, c.k, c.a, c.type);
        case FilterMachine::Legacy: return svf(input, state, c.k, c.a, params.type < 0.5f ? 0.0f : 1.0f);
        case FilterMachine::Lowpass4:
        {
            const float first = svf(input, state, c.k, c.a, 0.0f);
            return svf(first, stages_[1][static_cast<std::size_t>(channel)], std::sqrt(2.0f), c.a4, 0.0f);
        }
        case FilterMachine::Eq:
        {
            const float output = clean(c.b0 * input + state.z1);
            state.z1 = clean(c.b1 * input - c.b1 * output + state.z2);
            state.z2 = clean(c.b2 * input - c.a2 * output);
            return output;
        }
        case FilterMachine::CombMinus:
        case FilterMachine::CombPlus:
        {
            auto& line = comb_[static_cast<std::size_t>(channel)];
            if (line.empty()) return input;
            const auto index = (combCursor_ + line.size() - c.combWhole) % line.size();
            const auto previous = (index + line.size() - 1) % line.size();
            const float delayed = line[index] + c.combFraction * (line[previous] - line[index]);
            state.combLow = clean(state.combLow + c.combAlpha * (delayed - state.combLow));
            const float output = clean(input * (1.0f - c.combFeedback) + state.combLow * c.feedback);
            line[combCursor_] = output;
            return output;
        }
    }
    return input;
}

void StereoFilter::process(float& left, float& right, const FilterParams& params,
                           float cutoff, float resonance, float envelope, int note)
{
    updateCoefficients(params, cutoff, resonance, envelope, note);
    float values[2] = {clean(left), clean(right)};
    for (int channel = 0; channel < 2; ++channel)
    {
        float input = values[channel];
        if (params.bwPre) input = baseWidth(input, channel);
        input = machine(input, channel, params);
        if (!params.bwPre) input = baseWidth(input, channel);
        values[channel] = clean(input);
    }
    if (!comb_[0].empty()) combCursor_ = (combCursor_ + 1) % comb_[0].size();
    left = values[0]; right = values[1];
}

void StereoTrackFx::reset()
{
    countdown_ = 0;
    cachedReduction_ = -1.0f;
    heldLeft_ = heldRight_ = 0.0f;
}

void StereoTrackFx::rateReduction(float& left, float& right, float amount)
{
    amount = safe(amount, 0.0f, 127.0f);
    if (amount <= 0.0f) { countdown_ = 0; return; }
    if (cachedReduction_ != amount)
    {
        reductionFrames_ = static_cast<int>(std::round(std::exp2(amount / 16.0f)));
        cachedReduction_ = amount;
    }
    if (countdown_-- <= 0)
    {
        heldLeft_ = clean(left); heldRight_ = clean(right);
        countdown_ = reductionFrames_ - 1;
    }
    left = heldLeft_; right = heldRight_;
}

void StereoTrackFx::overdrive(float& left, float& right, float amount)
{
    amount = safe(amount, 0.0f, 1.0f);
    if (amount <= 0.0f) return;
    const float drive = 1.0f + amount * 20.0f;
    overdrivePrepared(left, right, drive, 1.0f / std::tanh(drive));
}

void StereoTrackFx::overdrivePrepared(float& left, float& right, float drive, float inverse)
{
    left = clean(std::tanh(clean(left) * drive) * inverse);
    right = clean(std::tanh(clean(right) * drive) * inverse);
}

void Chorus::prepare(double sampleRate)
{
    sampleRate_ = safe(sampleRate, 8000.0, 384000.0);
    for (auto& line : delay_) line.assign(static_cast<std::size_t>(sampleRate_ * 0.1) + 4, 0.0f);
    reset();
}

void Chorus::reset()
{
    for (auto& line : delay_) std::fill(line.begin(), line.end(), 0.0f);
    low_ = {};
    cursor_ = 0;
    phase_ = 0.0;
    cachedHighpass_ = -1.0f;
}

void Chorus::process(float left, float right, const ChorusParams& params, float& wetLeft, float& wetRight)
{
    wetLeft = wetRight = 0.0f;
    if (delay_[0].empty()) return;
    const float highpass = safe(params.highpassHz, 20.0f, static_cast<float>(sampleRate_ * 0.45));
    if (cachedHighpass_ != highpass)
    {
        highpassAlpha_ = static_cast<float>(1.0 - std::exp(-2.0 * pi * highpass / sampleRate_));
        cachedHighpass_ = highpass;
    }
    const float alpha = highpassAlpha_;
    const float values[2] = {clean(left), clean(right)};
    float output[2] = {};
    const float width = safe(params.width, -1.0f, 1.0f);
    for (int channel = 0; channel < 2; ++channel)
    {
        low_[static_cast<std::size_t>(channel)] = clean(low_[static_cast<std::size_t>(channel)] + alpha * (values[channel] - low_[static_cast<std::size_t>(channel)]));
        auto& line = delay_[static_cast<std::size_t>(channel)];
        line[cursor_] = values[channel] - low_[static_cast<std::size_t>(channel)];
        const double modulation = std::sin(2.0 * pi * (phase_ + channel * std::abs(width) * 0.25));
        const double frames = sampleRate_ * (0.012 + 0.005 * safe(params.depth, 0.0f, 1.0f) * modulation);
        const auto whole = static_cast<std::size_t>(std::floor(frames));
        const float fraction = static_cast<float>(frames - whole);
        const auto index = (cursor_ + line.size() - whole) % line.size();
        const auto previous = (index + line.size() - 1) % line.size();
        output[channel] = clean(line[index] + fraction * (line[previous] - line[index]));
    }
    cursor_ = (cursor_ + 1) % delay_[0].size();
    phase_ += safe(static_cast<double>(params.speed), 0.05, 10.0) / sampleRate_;
    phase_ -= std::floor(phase_);
    if (width < 0.0f) std::swap(output[0], output[1]);
    wetLeft = clean(output[0]); wetRight = clean(output[1]);
}
}
