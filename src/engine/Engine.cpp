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

float filter(float input, float& low, float& band, float g, float k)
{
    const float a = 1.0f / (1.0f + g * (g + k));
    const float v1 = a * (band + g * (input - low));
    const float v2 = low + g * v1;
    band = finite(2.0f * v1 - band);
    low = finite(2.0f * v2 - low);
    if (std::abs(band) < 1.0e-20f) band = 0.0f;
    if (std::abs(low) < 1.0e-20f) low = 0.0f;
    return finite(v2);
}
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
    prepared_ = true;
    reset();
}

void Engine::reset()
{
    voices_.fill(Voice{});
    currentSteps_.fill(0);
    internalPpq_ = 0.0;
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
    params_[track] = p;
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

void Engine::setSwing(float swing) { swing_ = limit(swing, 0.0f, 0.75f); }

int Engine::getCurrentStep(int track) const
{
    return track >= 0 && track < numTracks ? currentSteps_[track] : 0;
}

double Engine::stepTime(std::int64_t index) const
{
    return static_cast<double>(index) * stepBeats + ((index & 1) ? swing_ * stepBeats * 0.5 : 0.0);
}

int Engine::schedule(double ppq, double beatsPerSample, int samples)
{
    int count = 0;
    const auto first = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(ppq / stepBeats)) - 2);
    const auto last = static_cast<std::int64_t>(std::ceil((ppq + samples * beatsPerSample) / stepBeats)) + 2;
    for (int track = 0; track < numTracks; ++track)
    {
        if (params_[track].mute || !samples_[track]) continue;
        for (auto index = first; index <= last; ++index)
        {
            const auto& step = steps_[track][static_cast<std::size_t>(index % lengths_[track])];
            if (!step.enabled || step.velocity <= 0.0f || step.probability <= 0.0f) continue;
            const auto cycle = index / lengths_[track];
            if (cycle % step.conditionEvery != step.conditionOffset) continue;
            if (random01(static_cast<std::uint64_t>(index) * 37 + track * 7919) >= step.probability) continue;
            const double start = stepTime(index) + step.microtiming * stepBeats;
            const double duration = stepTime(index + 1) - stepTime(index);
            for (int repeat = 0; repeat < step.retrigs; ++repeat)
            {
                const double at = start + duration * repeat / step.retrigs;
                const double position = (at - ppq) / beatsPerSample;
                // Trigger on the first sample at or after a beat timestamp. The
                // tolerance only compensates floating-point block boundaries.
                // A timestamp between the previous sample and this block's
                // first sample belongs at offset zero, otherwise fractional
                // beat boundaries would disappear between process calls.
                if (position <= -1.0 + 1.0e-7 || position > samples - 1.0 + 1.0e-7) continue;
                const int offset = static_cast<int>(std::ceil(position - 1.0e-7));
                if (offset < 0 || offset >= samples || count == static_cast<int>(scheduled_.size())) continue;
                scheduled_[count++] = { offset, track, step.velocity, step.pitch, step.cutoff,
                                        step.lockPitch, step.lockCutoff };
            }
        }
    }
    std::sort(scheduled_.begin(), scheduled_.begin() + count,
              [](const auto& a, const auto& b) { return a.offset < b.offset || (a.offset == b.offset && a.track < b.track); });
    return count;
}

void Engine::trigger(int track, float velocity, float pitch, bool lockPitch, float cutoff, bool lockCutoff)
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
    updateVoiceCoefficients(track);
    v.active = v.velocity > 0.0f;
}

void Engine::updateVoiceCoefficients(int track)
{
    const auto& p = params_[track];
    auto& v = voices_[track];
    const double semitones = limit(static_cast<double>(v.pitchLocked ? v.triggerPitch : p.pitch + v.triggerPitch), -96.0, 96.0);
    v.increment = limit(samples_[track]->sampleRate, 8000.0, 384000.0) / sampleRate_ * std::exp2(semitones / 12.0);
    if (p.reverse) v.increment = -v.increment;
    if (!v.cutoffLocked) v.cutoff = p.cutoff;
    const float hz = limit(v.cutoff, 20.0f, static_cast<float>(sampleRate_ * 0.45));
    v.filterG = static_cast<float>(std::tan(pi * hz / sampleRate_));
    v.filterK = 2.0f - 1.9f * p.resonance;
}

void Engine::renderVoice(int track, float& left, float& right)
{
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
    const float drive = 1.0f + p.drive * 20.0f;
    if (p.drive > 0.0f)
    {
        l = std::tanh(l * drive) / std::tanh(drive);
        r = std::tanh(r * drive) / std::tanh(drive);
    }
    if (p.bitDepth < 16.0f)
    {
        const float levels = std::exp2(std::round(p.bitDepth) - 1.0f);
        l = std::round(l * levels) / levels;
        r = std::round(r * levels) / levels;
    }
    l = filter(l, v.lowL, v.bandL, v.filterG, v.filterK);
    r = filter(r, v.lowR, v.bandR, v.filterG, v.filterK);
    const float amplitude = envelope * v.velocity * p.gain;
    left = finite(l * amplitude * std::sqrt(1.0f - p.pan));
    right = finite(r * amplitude * std::sqrt(1.0f + p.pan));
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
    const double beatsPerSample = bpm / (60.0 * sampleRate_);
    const double ppq = transport.hostPosition ? limit(transport.ppq, -1.0e9, 1.0e9) : internalPpq_;
    const double delayFrames = limit(sampleRate_ * 60.0 / bpm * fx_.delayBeats, 1.0,
                                     static_cast<double>(delayL_.size() - 2));
    const auto delayWhole = static_cast<std::size_t>(std::floor(delayFrames));
    const float delayFraction = static_cast<float>(delayFrames - delayWhole);

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
                trigger(e.track, e.velocity, e.pitch, e.lockPitch, e.cutoff, e.lockCutoff);
            }
            // External events may be unsorted; their offsets are relative to the
            // full caller block, even if the engine renders smaller chunks.
            if (events)
                for (int event = 0; event < numEvents; ++event)
                    if (events[event].sampleOffset == begin + frame)
                        trigger(events[event].track, events[event].velocity, events[event].pitch, false, 0.0f, false);

            float mixL = 0.0f, mixR = 0.0f, sendDL = 0.0f, sendDR = 0.0f, sendRL = 0.0f, sendRR = 0.0f;
            for (int track = 0; track < numTracks; ++track)
            {
                float l, r;
                renderVoice(track, l, r);
                mixL += l; mixR += r;
                sendDL += l * params_[track].delaySend;
                sendDR += r * params_[track].delaySend;
                sendRL += l * params_[track].reverbSend;
                sendRR += r * params_[track].reverbSend;
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
            left[begin + frame] = std::tanh(finite(mixL));
            right[begin + frame] = std::tanh(finite(mixR));
        }
    }
    if (transport.playing)
    {
        const double finalPpq = ppq + numSamples * beatsPerSample;
        internalPpq_ = finalPpq;
        auto index = static_cast<std::int64_t>(std::floor(std::max(0.0, finalPpq - beatsPerSample) / stepBeats));
        if (stepTime(index) > finalPpq - beatsPerSample && index > 0) --index;
        for (int track = 0; track < numTracks; ++track)
            currentSteps_[track] = static_cast<int>(index % lengths_[track]);
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
