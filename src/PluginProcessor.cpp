#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr const char* trackNames[] = { "gain", "pan", "pitch", "cutoff", "resonance",
    "attack", "decay", "drive", "bitDepth", "start", "end", "delaySend", "reverbSend",
    "reverse", "mute", "loop", "machine", "playMode", "sourceLength", "loopPosition",
    "bars", "sampleLevel", "segmentSize", "segmentMode", "slice", "sliceLength",
    "sliceCount", "sliceByNote",
    "lfo1_speed", "lfo1_multiplier", "lfo1_fade", "lfo1_phase", "lfo1_depth",
    "lfo1_bpmSync", "lfo1_wave", "lfo1_mode", "lfo1_destination",
    "lfo2_speed", "lfo2_multiplier", "lfo2_fade", "lfo2_phase", "lfo2_depth",
    "lfo2_bpmSync", "lfo2_wave", "lfo2_mode", "lfo2_destination",
    "lfo3_speed", "lfo3_multiplier", "lfo3_fade", "lfo3_phase", "lfo3_depth",
    "lfo3_bpmSync", "lfo3_wave", "lfo3_mode", "lfo3_destination", "speedIndex",
    "ampMode", "ampHold", "ampSustain", "ampRelease", "ampHoldNote", "ampReset",
    "filterMachine", "filterType", "eqGain", "eqQ", "combFeedback", "combLowpass",
    "filterBase", "filterWidth", "filterKeytrack", "filterBwPre", "filterEnvDepth",
    "filterEnvDelay", "filterEnvAttack", "filterEnvDecay", "filterEnvSustain", "filterEnvRelease", "filterEnvReset",
    "srr", "srrPre", "drivePre", "chorusSend", "ampVolume", "bitReduction" };
constexpr const char* globalNames[] = { "play", "hostSync", "tempo", "swing", "master",
    "delayMix", "feedback", "delayBeats", "reverbMix", "fill", "chorusDepth", "chorusSpeed",
    "chorusHighpass", "chorusWidth", "chorusVolume", "chorusDelaySend", "chorusReverbSend" };
constexpr int maxSampleFrames = 10'000'000;
bool kitGlobal(size_t index) { return index >= 5 && index != 9; }

float safeRange(float value, float low, float high)
{
    return std::isfinite(value) ? juce::jlimit(low, high, value) : low;
}

takt::Step sanitizedStep(takt::Step value)
{
    value.velocity = safeRange(value.velocity, 0, 1);
    value.probability = safeRange(value.probability, 0, 1);
    value.pitch = safeRange(value.pitch, -48, 48);
    value.cutoff = safeRange(value.cutoff, 20, 20000);
    value.conditionEvery = juce::jlimit(1, 64, value.conditionEvery);
    value.conditionOffset = juce::jlimit(0, value.conditionEvery - 1, value.conditionOffset);
    value.retrigs = juce::jlimit(1, 8, value.retrigs);
    value.microtiming = safeRange(value.microtiming, -0.49f, 0.49f);
    value.note = juce::jlimit(0, 127, value.note);
    value.slice = juce::jlimit(0, takt::maxSlices - 1, value.slice);
    value.noteLengthBeats = safeRange(value.noteLengthBeats, .0001f, 512);
    value.rule.condition = static_cast<takt::sequencer::Condition>(
        juce::jlimit(0, 6, static_cast<int>(value.rule.condition)));
    value.rule.fill = static_cast<takt::sequencer::Fill>(juce::jlimit(0, 2, static_cast<int>(value.rule.fill)));
    value.rule.cycleB = juce::jlimit(1, 8, value.rule.cycleB);
    value.rule.cycleA = juce::jlimit(1, value.rule.cycleB, value.rule.cycleA);
    value.rule.probability = value.probability;
    value.retrig.rateIndex = juce::jlimit(0, 16, value.retrig.rateIndex);
    value.retrig.velocityFade = safeRange(value.retrig.velocityFade, -64, 64);
    value.retrig.fadeLengthBeats = std::isfinite(value.retrig.fadeLengthBeats)
        ? juce::jlimit(.0001, 512.0, value.retrig.fadeLengthBeats) : .25;
    return value;
}

juce::MemoryBlock encodeSample(const takt::Sample& sample)
{
    juce::MemoryOutputStream raw;
    raw.writeInt(static_cast<int>(sample.left.size()));
    raw.writeDouble(sample.sampleRate);
    for (float value : sample.left) raw.writeFloat(value);
    for (float value : sample.right) raw.writeFloat(value);
    juce::MemoryOutputStream compressed;
    {
        juce::GZIPCompressorOutputStream gzip(compressed, 6);
        gzip.write(raw.getData(), raw.getDataSize());
    }
    return compressed.getMemoryBlock();
}

std::shared_ptr<const takt::Sample> decodeSample(const juce::MemoryBlock& block,
                                                const juce::String& name)
{
    juce::MemoryInputStream source(block, false);
    juce::GZIPDecompressorInputStream gzip(source);
    const int count = gzip.readInt();
    const double rate = gzip.readDouble();
    if (count < 2 || count > maxSampleFrames || !std::isfinite(rate)
        || rate < 8000 || rate > 384000) return {};
    auto sample = std::make_shared<takt::Sample>();
    sample->name = name.toStdString();
    sample->sampleRate = rate;
    sample->left.resize(static_cast<size_t>(count));
    sample->right.resize(static_cast<size_t>(count));
    for (auto* channel : { &sample->left, &sample->right })
        for (float& value : *channel)
        {
            juce::uint32 bytes = 0;
            if (gzip.read(&bytes, sizeof(bytes)) != sizeof(bytes)) return {};
            bytes = juce::ByteOrder::swapIfBigEndian(bytes);
            std::memcpy(&value, &bytes, sizeof(value));
            if (!std::isfinite(value)) return {};
        }
    return sample;
}
}

juce::String TaktAudioProcessor::trackParameterID(int track, const juce::String& name)
{
    return "t" + juce::String(track + 1) + "_" + name;
}

juce::AudioProcessorValueTreeState::ParameterLayout TaktAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto addFloat = [&](const juce::String& id, const juce::String& label,
                        float low, float high, float initial, float skew = 1.0f)
    {
        juce::NormalisableRange<float> range(low, high);
        range.skew = skew;
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, 1},
                                                              label, range, initial));
    };
    auto addBool = [&](const juce::String& id, const juce::String& label, bool initial)
    {
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, 1}, label, initial));
    };
    addBool("play", "Run", false);
    addBool("hostSync", "Follow host", true);
    addFloat("tempo", "Tempo", 30, 300, 120);
    addFloat("swing", "Swing", 0, 0.75f, 0);
    addFloat("master", "Master", 0, 1, 0.75f);
    addFloat("delayMix", "Delay return", 0, 1, 0.3f);
    addFloat("feedback", "Delay feedback", 0, 0.9f, 0.35f);
    addFloat("delayBeats", "Delay beats", 0.125f, 2, 0.5f);
    addFloat("reverbMix", "Reverb return", 0, 1, 0.25f);
    for (int track = 0; track < takt::numTracks; ++track)
    {
        auto add = [&](const char* name, float lo, float hi, float value, float skew = 1.0f)
        { addFloat(trackParameterID(track, name), "Track " + juce::String(track + 1) + " " + name,
                   lo, hi, value, skew); };
        add("gain", 0, 1.5f, 0.8f);
        add("pan", -1, 1, 0);
        add("pitch", -36, 36, 0);
        add("cutoff", 20, 20000, 18000, 0.25f);
        add("resonance", 0, 0.95f, 0.1f);
        add("attack", 0.0001f, 1, 0.002f, 0.3f);
        add("decay", 0.01f, 10, 1, 0.35f);
        add("drive", 0, 1, 0);
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{trackParameterID(track, "bitDepth"), 1},
                    "Track " + juce::String(track + 1) + " bits", 4, 24, 16));
        add("start", 0, 0.99f, 0);
        add("end", 0.01f, 1, 1);
        add("delaySend", 0, 1, 0.12f);
        add("reverbSend", 0, 1, 0.1f);
        for (const char* name : { "reverse", "mute", "loop" })
            addBool(trackParameterID(track, name), "Track " + juce::String(track + 1) + " " + name, false);
    }
    // Append extensions after every legacy parameter. DAW index ordering and
    // the original 265 IDs/ranges remain unchanged.
    for (int track = 0; track < takt::numTracks; ++track)
    {
        auto add = [&](const juce::String& name, float lo, float hi, float value)
        { addFloat(trackParameterID(track, name), "Track " + juce::String(track + 1) + " " + name,
                   lo, hi, value); };
        auto addInt = [&](const juce::String& name, int lo, int hi, int value)
        { layout.add(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID{trackParameterID(track, name), 1},
            "Track " + juce::String(track + 1) + " " + name, lo, hi, value)); };
        auto addTrackBool = [&](const juce::String& name, bool value)
        { addBool(trackParameterID(track, name), "Track " + juce::String(track + 1) + " " + name, value); };
        addInt("machine", 0, 6, 0);
        addInt("playMode", 0, 3, 0);
        add("sourceLength", .001f, 1, 1);
        add("loopPosition", 0, 1, 0);
        add("bars", .0625f, 64, 1);
        add("sampleLevel", 0, 2, 1);
        add("segmentSize", .001f, 1, .125f);
        addInt("segmentMode", 0, 3, 0);
        addInt("slice", 0, takt::maxSlices - 1, 0);
        addInt("sliceLength", 1, takt::maxSlices, 1);
        addInt("sliceCount", 1, takt::maxSlices, 16);
        addTrackBool("sliceByNote", false);
        for (int lfo = 1; lfo <= 3; ++lfo)
        {
            const auto prefix = "lfo" + juce::String(lfo) + "_";
            add(prefix + "speed", -64, 63, 16);
            add(prefix + "multiplier", 1.0f / 128, 2048, 1);
            add(prefix + "fade", -64, 63, 0);
            add(prefix + "phase", 0, 127, 0);
            add(prefix + "depth", -128, 127, 0);
            addTrackBool(prefix + "bpmSync", true);
            addInt(prefix + "wave", 0, 6, 0);
            addInt(prefix + "mode", 0, 4, 0);
            addInt(prefix + "destination", 0, 14, 0);
        }
    }
    for (int track = 0; track < takt::numTracks; ++track)
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{trackParameterID(track, "speedIndex"), 1},
            "Track " + juce::String(track + 1) + " sequencer speed", 0, 6, 4));
    addBool("fill", "Fill", false);
    for (int track = 0; track < takt::numTracks; ++track)
    {
        auto add = [&](const juce::String& name, float lo, float hi, float value, float skew = 1.0f)
        { addFloat(trackParameterID(track, name), "Track " + juce::String(track + 1) + " " + name, lo, hi, value, skew); };
        auto addInt = [&](const juce::String& name, int lo, int hi, int value)
        { layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{trackParameterID(track, name), 1},
              "Track " + juce::String(track + 1) + " " + name, lo, hi, value)); };
        auto addTrackBool = [&](const juce::String& name, bool value)
        { addBool(trackParameterID(track, name), "Track " + juce::String(track + 1) + " " + name, value); };
        addInt("ampMode", 0, 2, 0); add("ampHold", 0, 10, 0); add("ampSustain", 0, 1, .8f);
        add("ampRelease", .0001f, 10, .2f, .35f); addTrackBool("ampHoldNote", false); addTrackBool("ampReset", true);
        addInt("filterMachine", 0, 6, 0); add("filterType", 0, 1, 0);
        add("eqGain", -24, 24, 0); add("eqQ", .1f, 20, 1);
        add("combFeedback", 0, .98f, .5f); add("combLowpass", 20, 20000, 18000, .25f);
        add("filterBase", 0, 127, 0); add("filterWidth", 0, 127, 127);
        add("filterKeytrack", 0, 1, 0); addTrackBool("filterBwPre", true);
        add("filterEnvDepth", -128, 128, 0); add("filterEnvDelay", 0, 10, 0);
        add("filterEnvAttack", .0001f, 10, .002f, .3f); add("filterEnvDecay", .0001f, 10, 1, .35f);
        add("filterEnvSustain", 0, 1, 0); add("filterEnvRelease", .0001f, 10, .2f, .35f);
        addTrackBool("filterEnvReset", true); add("srr", 0, 127, 0);
        addTrackBool("srrPre", true); addTrackBool("drivePre", true); add("chorusSend", 0, 1, 0);
    }
    addFloat("chorusDepth", "Chorus depth", 0, 1, .5f);
    addFloat("chorusSpeed", "Chorus speed", .05f, 10, .5f);
    addFloat("chorusHighpass", "Chorus highpass", 20, 20000, 20, .25f);
    addFloat("chorusWidth", "Chorus stereo width", -1, 1, 1);
    addFloat("chorusVolume", "Chorus volume", 0, 1, 0);
    addFloat("chorusDelaySend", "Chorus delay send", 0, 1, 0);
    addFloat("chorusReverbSend", "Chorus reverb send", 0, 1, 0);
    for (int track = 0; track < takt::numTracks; ++track)
    {
        addFloat(trackParameterID(track, "ampVolume"), "Track " + juce::String(track + 1) + " amp volume", 0, 2, 1);
        addFloat(trackParameterID(track, "bitReduction"), "Track " + juce::String(track + 1) + " bit reduction", 1, 16, 16);
    }
    return layout;
}

TaktAudioProcessor::TaktAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Main", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "TAKT_II", createParameterLayout())
{
    static_assert(std::size(trackNames) == trackParameterCount, "Track snapshot and parameter names must agree");
    formats.registerBasicFormats();
    lengths.fill(16);
    for (int t = 0; t < takt::numTracks; ++t)
    {
        samples[static_cast<size_t>(t)] = takt::Engine::makeDemoSample(t);
        sampleDurations[static_cast<size_t>(t)].store(samples[static_cast<size_t>(t)]->left.size()
                                                   / samples[static_cast<size_t>(t)]->sampleRate);
        currentSteps[static_cast<size_t>(t)].store(-1);
        for (size_t p = 0; p < std::size(trackNames); ++p)
        {
            trackValues[static_cast<size_t>(t)][p] = parameters.getRawParameterValue(trackParameterID(t, trackNames[p]));
            parameterIDs[static_cast<size_t>(t)][p] = trackParameterID(t, trackNames[p]);
            parameterVersions[static_cast<size_t>(t)][p].store(0);
            transitionParameterVersions[static_cast<size_t>(t)][p].store(0);
        }
    }
    for (size_t p = 0; p < std::size(globalNames); ++p)
    {
        globalValues[p] = parameters.getRawParameterValue(globalNames[p]);
        appliedGlobals[p] = globalValues[p]->load();
        globalParameterVersions[p].store(0);
        transitionGlobalVersions[p].store(0);
    }
    loadDemoPattern();
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        permanentPattern = capturePatternLocked();
        patternBank[0] = permanentPattern;
        audioBank[0] = permanentPattern.get();
        activeAudioSnapshot.store(permanentPattern.get());
        activeAudioKitSnapshot.store(permanentPattern.get());
    }
    for (int t = 0; t < takt::numTracks; ++t)
        for (const char* name : trackNames)
            parameters.addParameterListener(trackParameterID(t, name), this);
    for (const char* name : globalNames) parameters.addParameterListener(name, this);
}

TaktAudioProcessor::~TaktAudioProcessor()
{
    commitControlAll();
    for (int t = 0; t < takt::numTracks; ++t)
        for (const char* name : trackNames)
            parameters.removeParameterListener(trackParameterID(t, name), this);
    for (const char* name : globalNames) parameters.removeParameterListener(name, this);
}

void TaktAudioProcessor::parameterChanged(const juce::String& id, float)
{
    // Called by host automation as well as the editor. An atomic revision
    // invalidates a stale edit undo without taking a lock on the audio thread.
    if (id != "play" && id != "hostSync" && id != "master")
        editRevision.fetch_add(1, std::memory_order_relaxed);
    for (size_t p = 0; p < std::size(globalNames); ++p)
        if (id == globalNames[p])
        {
            globalParameterVersions[p].fetch_add(1, std::memory_order_relaxed);
            return;
        }
    for (size_t t = 0; t < parameterIDs.size(); ++t)
        for (size_t p = 0; p < trackParameterCount; ++p)
            if (id == parameterIDs[t][p])
            {
                parameterVersions[t][p].fetch_add(1, std::memory_order_relaxed);
                return;
            }
}

void TaktAudioProcessor::notifyPatternChanged()
{
    // Never call this while holding controlMutex: host listeners may query
    // sequencer state synchronously from this notification.
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

bool TaktAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
           && layout.getMainInputChannelSet().isDisabled();
}

double TaktAudioProcessor::getTailLengthSeconds() const
{
    double dryTail = 0;
    for (int t = 0; t < takt::numTracks; ++t)
    {
        const auto& p = trackValues[static_cast<size_t>(t)];
        const double envelope = safeRange(p[6]->load(), .01f, 10) * 14.0;
        const double playback = sampleDurations[static_cast<size_t>(t)].load()
                              / std::exp2(safeRange(p[2]->load(), -36, 36) / 12.0);
        dryTail = std::max(dryTail, p[15]->load() > .5f || p[16]->load() > .5f
                                      ? envelope : std::min(playback, envelope));
    }
    const double feedback = safeRange(globalValues[6]->load(), 0, .9f);
    const double repeats = feedback > .001 ? std::ceil(std::log(1.0e-6) / std::log(feedback)) : 1.0;
    const double delay = globalValues[5]->load() > 0
        ? repeats * safeRange(globalValues[7]->load(), .125f, 2) * 60.0
          / safeRange(globalValues[2]->load(), 30, 300) : 0;
    return dryTail + delay + (globalValues[8]->load() > 0 ? 5.0 : 0.0);
}

void TaktAudioProcessor::prepareToPlay(double rate, int size)
{
    processingRate = rate;
    arrangementWasPlaying = previousHostClock = false;
    arrangementPpq = expectedHostPpq = 0;
    nextArrangementStepPpq = .25;
    audioArrangement.stop(true);
    engine.prepare(rate, size);
    patternDirty.store(true);
    samplesDirty.store(true);
    syncControls();
}

void TaktAudioProcessor::releaseResources() { engine.reset(); }

void TaktAudioProcessor::syncControls()
{
    // Keep the previous complete audio snapshot if a UI edit/restore owns the
    // mutex. No blocking or partially restored pattern reaches the renderer.
    std::unique_lock<std::mutex> lock(controlMutex, std::try_to_lock);
    if (!lock.owns_lock() || restoringPattern) return;
    const bool pendingSequence = audioTransitionGeneration.load() != mirroredTransitionGeneration.load();
    const bool pendingKit = audioKitGeneration.load() != mirroredKitGeneration.load();
    if (!pendingKit && samplesDirty.exchange(false))
        for (int t = 0; t < takt::numTracks; ++t)
            engine.setSample(t, samples[static_cast<size_t>(t)]);
    if (!pendingSequence && patternDirty.exchange(false))
    {
        for (int t = 0; t < takt::numTracks; ++t)
        {
            engine.setTrackLength(t, lengths[static_cast<size_t>(t)]);
            for (int s = 0; s < takt::maxSteps; ++s)
                engine.setStep(t, s, steps[static_cast<size_t>(t)][static_cast<size_t>(s)]);
        }
    }
    const auto* sequenceOverlay = activeAudioSnapshot.load(std::memory_order_acquire);
    const auto* kitOverlay = activeAudioKitSnapshot.load(std::memory_order_acquire);
    for (size_t p = 0; p < appliedGlobals.size(); ++p)
    {
        const auto* overlay = kitGlobal(p) ? kitOverlay : sequenceOverlay;
        const bool pending = kitGlobal(p) ? pendingKit : pendingSequence;
        appliedGlobals[p] = pending && overlay && p != 0 && p != 1 && p != 4
            && globalParameterVersions[p].load() == transitionGlobalVersions[p].load()
            ? overlay->globalParameters[p] : globalValues[p]->load();
    }
    for (int t = 0; t < takt::numTracks; ++t)
    {
        std::array<float, trackParameterCount> values{};
        for (size_t p = 0; p < values.size(); ++p)
            values[p] = pendingKit && kitOverlay
                && parameterVersions[static_cast<size_t>(t)][p].load()
                    == transitionParameterVersions[static_cast<size_t>(t)][p].load()
                ? kitOverlay->trackParameters[static_cast<size_t>(t)][p]
                : trackValues[static_cast<size_t>(t)][p]->load();
        auto p = makeTrackParams(t, values, pendingKit && kitOverlay
            ? kitOverlay->slicePoints[static_cast<size_t>(t)] : slicePoints[static_cast<size_t>(t)]);
        appliedTrackParams[static_cast<size_t>(t)] = p;
        if (audioArrangement.mode() == takt::PatternChain::Mode::Song)
            p.mute = (audioArrangement.current().muteMask & (1u << t)) != 0;
        engine.setTrackParams(t, p);
    }
    takt::FxParams fx;
    fx.delayMix = appliedGlobals[5]; fx.feedback = appliedGlobals[6];
    fx.delayBeats = appliedGlobals[7]; fx.reverbMix = appliedGlobals[8];
    engine.setFx(fx);
    engine.setChorus({appliedGlobals[10], appliedGlobals[11], appliedGlobals[12], appliedGlobals[13],
                      appliedGlobals[14], appliedGlobals[15], appliedGlobals[16]});
    engine.setSwing(appliedGlobals[3]);
    engine.setFill(appliedGlobals[9] > .5f);
}

takt::TrackParams TaktAudioProcessor::makeTrackParams(int, const std::array<float, trackParameterCount>& v,
    const std::array<takt::SlicePoint, takt::maxSlices>& points) const
{
    takt::TrackParams p;
    p.gain = v[0]; p.pan = v[1]; p.pitch = v[2]; p.cutoff = v[3]; p.resonance = v[4];
    p.attack = v[5]; p.decay = v[6]; p.drive = v[7]; p.bitDepth = v[8];
    p.start = v[9]; p.end = v[10]; p.delaySend = v[11]; p.reverbSend = v[12];
    p.reverse = v[13] > .5f; p.mute = v[14] > .5f; p.loop = v[15] > .5f;
    p.machine = static_cast<takt::Machine>(static_cast<int>(v[16]));
    p.playback = static_cast<takt::PlaybackMode>(static_cast<int>(v[17]));
    p.sourceLength = v[18]; p.loopPosition = v[19]; p.bars = v[20]; p.sampleLevel = v[21];
    p.segmentSize = v[22]; p.segmentMode = static_cast<takt::PlaybackMode>(static_cast<int>(v[23]));
    p.slice = static_cast<int>(v[24]); p.sliceLength = static_cast<int>(v[25]);
    p.sliceCount = static_cast<int>(v[26]); p.sliceByNote = v[27] > .5f;
    p.speedIndex = static_cast<int>(v[55]); p.slicePoints = points;
    for (size_t lfo = 0; lfo < p.lfos.size(); ++lfo)
    {
        const size_t o = 28 + lfo * 9;
        auto& m = p.lfos[lfo];
        m.speed = v[o]; m.multiplier = v[o + 1]; m.fade = v[o + 2]; m.phase = v[o + 3];
        m.depth = v[o + 4]; m.bpmSync = v[o + 5] > .5f;
        m.wave = static_cast<takt::LfoWave>(static_cast<int>(v[o + 6]));
        m.mode = static_cast<takt::LfoMode>(static_cast<int>(v[o + 7]));
        m.destination = static_cast<takt::LfoDestination>(static_cast<int>(v[o + 8]));
    }
    p.amplitudeEnvelope = {static_cast<takt::EnvelopeMode>(static_cast<int>(v[56])), v[5], v[57],
                           v[6], v[58], v[59], v[60] > .5f, v[61] > .5f};
    p.filter.machine = static_cast<takt::FilterMachine>(static_cast<int>(v[62]));
    p.filter.type = v[63]; p.filter.eqGain = v[64]; p.filter.eqQ = v[65];
    p.filter.combFeedback = v[66]; p.filter.combLowpassHz = v[67];
    p.filter.base = v[68]; p.filter.width = v[69]; p.filter.keytrack = v[70]; p.filter.bwPre = v[71] > .5f;
    p.filter.envDepth = v[72]; p.filter.envDelay = v[73];
    p.filter.envelope = {takt::EnvelopeMode::Adsr, v[74], 0, v[75], v[76], v[77], false, v[78] > .5f};
    p.trackFx = {v[79], v[82], v[80] > .5f, v[81] > .5f};
    p.ampVolume = v[83]; p.bitReduction = v[84];
    return p;
}

void TaktAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int count = buffer.getNumSamples();
    if (count <= 0 || buffer.getNumChannels() < 2) return;
    syncControls();
    takt::Transport transport;
    transport.bpm = appliedGlobals[2];
    transport.playing = globalValues[0]->load() > 0.5f;
    bool following = false;
    if (globalValues[1]->load() > 0.5f)
        if (auto* hostPlayHead = getPlayHead())
            if (auto position = hostPlayHead->getPosition())
                if (position->getPpqPosition() && position->getBpm())
                {
                    transport.ppq = *position->getPpqPosition();
                    transport.bpm = *position->getBpm();
                    transport.playing = position->getIsPlaying();
                    transport.hostPosition = true;
                    following = true;
                }
    usingHostClock.store(following);
    hostPlaying.store(following && transport.playing);
    const bool requestedPlaying = transport.playing;
    if (!requestedPlaying && arrangementWasPlaying) audioArrangement.stop(false);
    // Drain selections before PLAY: starting a saved chain/song activates its
    // first row immediately, instead of cueing it after an extra pattern.
    drainArrangementCommands();
    const bool seek = following && previousHostClock && arrangementWasPlaying
        && std::abs(transport.ppq - expectedHostPpq) > transport.bpm / (60 * processingRate) * 1.5;
    if (requestedPlaying && (!arrangementWasPlaying || seek || following != previousHostClock))
    {
        if (seek)
        {
            applyAudioTransition(audioArrangement.stop(true));
            if (arrangementOriginEnabled) engine.restartSequencer(true);
        }
        audioArrangement.play();
        if (following && (seek || !previousHostClock))
        {
            arrangementPpq = following ? transport.ppq : arrangementPpq;
            nextArrangementStepPpq = arrangementPpq + .25;
        }
    }
    arrangementWasPlaying = requestedPlaying;
    previousHostClock = following;
    std::array<takt::TriggerEvent, 512> events;
    int eventCount = 0;
    int first = 0, firstCount = 0, second = 0, secondCount = 0;
    triggerFifo.prepareToRead(triggerFifo.getNumReady(), first, firstCount, second, secondCount);
    for (int i = 0; i < firstCount; ++i) events[static_cast<size_t>(eventCount++)] = queuedTriggers[static_cast<size_t>(first + i)];
    for (int i = 0; i < secondCount; ++i) events[static_cast<size_t>(eventCount++)] = queuedTriggers[static_cast<size_t>(second + i)];
    triggerFifo.finishedRead(firstCount + secondCount);
    for (const auto metadata : midi)
    {
        auto message = metadata.getMessage();
        if (!message.isNoteOn() && !message.isNoteOff()) continue;
        const int track = message.getNoteNumber() - 36;
        if (validTrack(track) && eventCount < static_cast<int>(events.size()))
            events[static_cast<size_t>(eventCount++)] = { juce::jlimit(0, count - 1, metadata.samplePosition), track,
                                                        message.getFloatVelocity(), 0,
                appliedTrackParams[static_cast<size_t>(track)].sliceByNote ? message.getNoteNumber() : 60,
                0, false, message.isNoteOff() };
    }
    std::sort(events.begin(), events.begin() + eventCount,
              [](const auto& a, const auto& b) { return a.sampleOffset < b.sampleOffset; });
    int offset = 0, nextEvent = 0;
    while (offset < count)
    {
        auto chunkTransport = transport;
        chunkTransport.playing = requestedPlaying && audioArrangement.playing();
        if (!following)
        {
            // Normal pattern tempo automation remains live. Song/chain use
            // their own row/pattern tempo after each exact boundary.
            chunkTransport.bpm = audioArrangement.mode() == takt::PatternChain::Mode::Pattern
                ? appliedGlobals[2] : audioArrangement.current().tempo;
        }
        const double beatsPerSample = chunkTransport.bpm / (60 * processingRate);
        if (following) chunkTransport.ppq = arrangementPpq;
        int size = count - offset;
        if (chunkTransport.playing)
            size = std::min(size, std::max(1, static_cast<int>(std::ceil(
                (nextArrangementStepPpq - arrangementPpq) / beatsPerSample - 1.0e-8))));
        std::array<takt::TriggerEvent, 512> chunkEvents{};
        int chunkEventCount = 0;
        while (nextEvent < eventCount && events[static_cast<size_t>(nextEvent)].sampleOffset < offset + size)
        {
            auto event = events[static_cast<size_t>(nextEvent++)];
            event.sampleOffset -= offset;
            chunkEvents[static_cast<size_t>(chunkEventCount++)] = event;
        }
        if (audioArrangement.mode() == takt::PatternChain::Mode::Song)
        {
            const auto& song = audioArrangement.song(audioArrangement.currentSong());
            const auto& row = song.rows[static_cast<size_t>(audioArrangement.currentRow())];
            engine.setLastPatternCycle(audioArrangement.currentRepeat() + 1 >= row.repeats);
        }
        else engine.setLastPatternCycle(audioArrangement.hasQueuedSelection());
        engine.process(buffer.getWritePointer(0) + offset, buffer.getWritePointer(1) + offset,
                       size, chunkTransport, chunkEvents.data(), chunkEventCount);
        offset += size;
        if (chunkTransport.playing)
        {
            arrangementPpq += size * beatsPerSample;
            if (arrangementPpq + beatsPerSample * .1 >= nextArrangementStepPpq)
            {
                applyAudioTransition(audioArrangement.advanceStep(following));
                nextArrangementStepPpq += .25;
            }
        }
    }
    expectedHostPpq = transport.ppq + (requestedPlaying ? count * transport.bpm / (60 * processingRate) : 0);
    const float master = safeRange(globalValues[4]->load(), 0, 1);
    buffer.applyGain(master);
    outputPeak.store(juce::jmax(buffer.getMagnitude(0, 0, count), buffer.getMagnitude(1, 0, count)));
    for (int t = 0; t < takt::numTracks; ++t)
        currentSteps[static_cast<size_t>(t)].store(engine.getCurrentStep(t));
    midi.clear();
}

void TaktAudioProcessor::setParameter(const juce::String& id, float value)
{
    if (!std::isfinite(value)) return;
    if (!servicingTransitions.load()) servicePendingTransitions();
    if (auto* parameter = parameters.getParameter(id))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        parameter->endChangeGesture();
    }
}

float TaktAudioProcessor::parameterValue(const juce::String& id) const
{
    if (auto* value = parameters.getRawParameterValue(id)) return value->load();
    return 0;
}

takt::Step TaktAudioProcessor::getStep(int track, int step) const
{
    if (!validTrack(track) || step < 0 || step >= takt::maxSteps) return {};
    std::lock_guard<std::mutex> lock(controlMutex);
    if (audioTransitionGeneration.load() != mirroredTransitionGeneration.load())
        if (const auto* snapshot = activeAudioSnapshot.load())
            return snapshot->steps[static_cast<size_t>(track)][static_cast<size_t>(step)];
    return steps[static_cast<size_t>(track)][static_cast<size_t>(step)];
}

void TaktAudioProcessor::setStep(int track, int step, const takt::Step& value)
{
    if (!validTrack(track) || step < 0 || step >= takt::maxSteps) return;
    servicePendingTransitions();
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        steps[static_cast<size_t>(track)][static_cast<size_t>(step)] = sanitizedStep(value);
        patternDirty.store(true);
        editRevision.fetch_add(1, std::memory_order_relaxed);
    }
    notifyPatternChanged();
}

int TaktAudioProcessor::getTrackLength(int track) const
{
    if (!validTrack(track)) return 16;
    std::lock_guard<std::mutex> lock(controlMutex);
    if (audioTransitionGeneration.load() != mirroredTransitionGeneration.load())
        if (const auto* snapshot = activeAudioSnapshot.load()) return snapshot->lengths[static_cast<size_t>(track)];
    return lengths[static_cast<size_t>(track)];
}

void TaktAudioProcessor::setTrackLength(int track, int length)
{
    if (!validTrack(track)) return;
    servicePendingTransitions();
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        lengths[static_cast<size_t>(track)] = juce::jlimit(1, takt::maxSteps, length);
        patternDirty.store(true);
        editRevision.fetch_add(1, std::memory_order_relaxed);
    }
    notifyPatternChanged();
}

void TaktAudioProcessor::clearTrack(int track)
{
    if (!validTrack(track)) return;
    servicePendingTransitions();
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        steps[static_cast<size_t>(track)].fill(takt::Step{});
        patternDirty.store(true);
        editRevision.fetch_add(1, std::memory_order_relaxed);
    }
    notifyPatternChanged();
}

bool TaktAudioProcessor::selectionRange(EditScope scope, int track, int stepIndex,
                                      int pageIndex, int& first, int& count)
{
    if (!validTrack(track)) return false;
    switch (scope)
    {
        case EditScope::Step:
            if (stepIndex < 0 || stepIndex >= takt::maxSteps) return false;
            first = stepIndex; count = 1;
            return true;
        case EditScope::Page:
            if (pageIndex < 0 || pageIndex >= takt::maxSteps / 16) return false;
            first = pageIndex * 16; count = 16;
            return true;
        case EditScope::Track:
            first = 0; count = takt::maxSteps;
            return true;
    }
    return false;
}

TaktAudioProcessor::EditResult TaktAudioProcessor::copySelection(EditScope scope, int track,
                                                               int stepIndex, int pageIndex)
{
    servicePendingTransitions();
    int first = 0, count = 0;
    if (!selectionRange(scope, track, stepIndex, pageIndex, first, count)) return EditResult::InvalidSelection;
    std::lock_guard<std::mutex> lock(controlMutex);
    if (restoringPattern) return EditResult::InvalidSelection;
    std::copy_n(steps[static_cast<size_t>(track)].begin() + first, count, clipboard.content.begin());
    clipboard.scope = scope;
    clipboard.length = lengths[static_cast<size_t>(track)];
    clipboard.available = true;
    // Copy replaces the shared typed clipboard, but changes no audio state.
    undoEdit.available = false;
    return EditResult::Applied;
}

bool TaktAudioProcessor::canUndoEditLocked() const
{
    return !restoringPattern && undoEdit.available
           && undoEdit.revision == editRevision.load(std::memory_order_relaxed);
}

bool TaktAudioProcessor::canUndoEdit() const
{
    std::lock_guard<std::mutex> lock(controlMutex);
    return canUndoEditLocked();
}

bool TaktAudioProcessor::undoEditLocked()
{
    if (!canUndoEditLocked()) return false;
    std::copy_n(undoEdit.content.begin(), undoEdit.count,
                steps[static_cast<size_t>(undoEdit.track)].begin() + undoEdit.first);
    if (undoEdit.scope == EditScope::Track)
        lengths[static_cast<size_t>(undoEdit.track)] = undoEdit.length;
    undoEdit.available = false;
    editRevision.fetch_add(1, std::memory_order_relaxed);
    patternDirty.store(true);
    return true;
}

TaktAudioProcessor::EditResult TaktAudioProcessor::undoLastEdit()
{
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (!undoEditLocked()) return EditResult::NothingToUndo;
    }
    notifyPatternChanged();
    return EditResult::Undone;
}

TaktAudioProcessor::EditResult TaktAudioProcessor::pasteSelection(EditScope scope, int track,
                                                                int stepIndex, int pageIndex)
{
    servicePendingTransitions();
    int first = 0, count = 0;
    if (!selectionRange(scope, track, stepIndex, pageIndex, first, count)) return EditResult::InvalidSelection;
    EditResult result = EditResult::Applied;
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (restoringPattern) return EditResult::InvalidSelection;
        if (!clipboard.available) return EditResult::EmptyClipboard;
        if (clipboard.scope != scope) return EditResult::ScopeMismatch;
        if (canUndoEditLocked() && undoEdit.action == EditAction::Paste
            && undoEdit.scope == scope && undoEdit.track == track && undoEdit.first == first)
        {
            undoEditLocked();
            result = EditResult::Undone;
        }
        else
        {
            std::copy_n(steps[static_cast<size_t>(track)].begin() + first, count, undoEdit.content.begin());
            undoEdit.scope = scope; undoEdit.action = EditAction::Paste;
            undoEdit.track = track; undoEdit.first = first; undoEdit.count = count;
            undoEdit.length = lengths[static_cast<size_t>(track)];
            std::copy_n(clipboard.content.begin(), count, steps[static_cast<size_t>(track)].begin() + first);
            if (scope == EditScope::Track) lengths[static_cast<size_t>(track)] = clipboard.length;
            undoEdit.revision = editRevision.fetch_add(1, std::memory_order_relaxed) + 1;
            undoEdit.available = true;
            patternDirty.store(true);
        }
    }
    notifyPatternChanged();
    return result;
}

TaktAudioProcessor::EditResult TaktAudioProcessor::clearSelection(EditScope scope, int track,
                                                                int stepIndex, int pageIndex)
{
    servicePendingTransitions();
    int first = 0, count = 0;
    if (!selectionRange(scope, track, stepIndex, pageIndex, first, count)) return EditResult::InvalidSelection;
    EditResult result = EditResult::Applied;
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (restoringPattern) return EditResult::InvalidSelection;
        if (canUndoEditLocked() && undoEdit.action == EditAction::Clear
            && undoEdit.scope == scope && undoEdit.track == track && undoEdit.first == first)
        {
            undoEditLocked();
            result = EditResult::Undone;
        }
        else
        {
            auto begin = steps[static_cast<size_t>(track)].begin() + first;
            std::copy_n(begin, count, undoEdit.content.begin());
            undoEdit.scope = scope; undoEdit.action = EditAction::Clear;
            undoEdit.track = track; undoEdit.first = first; undoEdit.count = count;
            undoEdit.length = lengths[static_cast<size_t>(track)];
            if (scope == EditScope::Step)
            {
                // Legacy DSP also uses a nonlocked step pitch additively.
                // Remove this lock payload to make the new Clear Locks action
                // audibly return to the base tuning without changing that DSP.
                begin->pitch = 0.0f;
                begin->lockPitch = false;
                begin->lockCutoff = false;
                begin->lockSlice = false;
                begin->lockSlice = false;
            }
            else std::fill_n(begin, count, takt::Step{});
            undoEdit.revision = editRevision.fetch_add(1, std::memory_order_relaxed) + 1;
            undoEdit.available = true;
            patternDirty.store(true);
        }
    }
    notifyPatternChanged();
    return result;
}

void TaktAudioProcessor::retainSampleLocked(const std::shared_ptr<const takt::Sample>& sample)
{
    // Retain replaced samples until the renderer has dropped its reference.
    // Avoid duplicates: duplicate retired owners would keep each other alive.
    if (sample && std::find(retiredSamples.begin(), retiredSamples.end(), sample) == retiredSamples.end())
        retiredSamples.push_back(sample);
}

std::shared_ptr<const TaktAudioProcessor::PatternSnapshot> TaktAudioProcessor::capturePatternLocked() const
{
    auto snapshot = std::make_shared<PatternSnapshot>();
    for (;;)
    {
    const auto sequenceVersion = audioSnapshotSequence.load(std::memory_order_acquire);
    if ((sequenceVersion & 1u) != 0) continue;
    snapshot->steps = steps;
    snapshot->lengths = lengths;
    snapshot->samples = samples;
    snapshot->slicePoints = slicePoints;
    snapshot->patternLength = patternLength;
    snapshot->sourceSlot = currentPatternSlot.load();
    snapshot->sourceSong = currentSongSlot.load();
    snapshot->sourceRow = currentSongRow.load();
    const auto* sequence = activeAudioSnapshot.load(std::memory_order_acquire);
    const auto* kit = activeAudioKitSnapshot.load(std::memory_order_acquire);
    const bool pendingSequence = audioTransitionGeneration.load() != mirroredTransitionGeneration.load();
    const bool pendingKit = audioKitGeneration.load() != mirroredKitGeneration.load();
    if (pendingSequence && sequence)
    {
        snapshot->steps = sequence->steps;
        snapshot->lengths = sequence->lengths;
        snapshot->patternLength = sequence->patternLength;
    }
    if (pendingKit && kit)
    {
        snapshot->samples = kit->samples;
        snapshot->slicePoints = kit->slicePoints;
    }
    for (size_t t = 0; t < snapshot->trackParameters.size(); ++t)
        for (size_t p = 0; p < snapshot->trackParameters[t].size(); ++p)
            snapshot->trackParameters[t][p] = pendingKit && kit
                && parameterVersions[t][p].load() == transitionParameterVersions[t][p].load()
                ? kit->trackParameters[t][p] : trackValues[t][p]->load();
    for (size_t p = 0; p < snapshot->globalParameters.size(); ++p)
    {
        const auto* overlay = kitGlobal(p) ? kit : sequence;
        const bool pending = kitGlobal(p) ? pendingKit : pendingSequence;
        snapshot->globalParameters[p] = pending && overlay && p != 0 && p != 1 && p != 4
            && globalParameterVersions[p].load() == transitionGlobalVersions[p].load()
            ? overlay->globalParameters[p] : globalValues[p]->load();
    }
    if (audioSnapshotSequence.load(std::memory_order_acquire) == sequenceVersion) return snapshot;
    }
}

void TaktAudioProcessor::temporarySavePattern()
{
    servicePendingTransitions();
    std::lock_guard<std::mutex> lock(controlMutex);
    if (!restoringPattern) temporaryPattern = capturePatternLocked();
}

void TaktAudioProcessor::restorePattern(const PatternSnapshot& snapshot)
{
    commitControlAll();
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (restoringPattern) return;
        restoringPattern = true;
        for (size_t t = 0; t < samples.size(); ++t)
            if (samples[t] != snapshot.samples[t]) retainSampleLocked(samples[t]);
        steps = snapshot.steps;
        lengths = snapshot.lengths;
        samples = snapshot.samples;
        slicePoints = snapshot.slicePoints;
        patternLength = snapshot.patternLength;
        for (size_t t = 0; t < samples.size(); ++t)
            sampleDurations[t].store(samples[t] ? samples[t]->left.size() / samples[t]->sampleRate : 0);
        undoEdit.available = false;
        editRevision.fetch_add(1, std::memory_order_relaxed);
        patternDirty.store(true); samplesDirty.store(true);
    }
    // Parameter and host callbacks may synchronously query sequencer state.
    // Keep them outside controlMutex; syncControls retains its audio snapshot
    // until every parameter has been restored and restoringPattern is cleared.
    for (int t = 0; t < takt::numTracks; ++t)
        for (size_t p = 0; p < std::size(trackNames); ++p)
            setParameter(trackParameterID(t, trackNames[p]), snapshot.trackParameters[static_cast<size_t>(t)][p]);
    for (size_t p = 0; p < std::size(globalNames); ++p)
        if (p != 0 && p != 1 && p != 4)
            setParameter(globalNames[p], snapshot.globalParameters[p]);
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        restoringPattern = false;
    }
    notifyPatternChanged();
}

bool TaktAudioProcessor::enqueueArrangementLocked(ArrangementCommand command)
{
    int first = 0, count = 0, second = 0, secondCount = 0;
    arrangementFifo.prepareToWrite(1, first, count, second, secondCount);
    if (count == 0) return false;
    command.serial = ++nextArrangementSerial;
    (*arrangementCommands)[static_cast<size_t>(first)] = command;
    arrangementFifo.finishedWrite(1);
    return true;
}

std::shared_ptr<const TaktAudioProcessor::PatternSnapshot> TaktAudioProcessor::ensurePatternLocked(int slot)
{
    auto& stored = patternBank[static_cast<size_t>(slot)];
    if (stored) return stored;
    auto fresh = std::make_shared<PatternSnapshot>(*permanentPattern);
    for (auto& track : fresh->steps) track.fill(takt::Step{});
    fresh->lengths.fill(16);
    fresh->patternLength = 16;
    ArrangementCommand command;
    command.kind = ArrangementCommand::Kind::UpdatePattern;
    command.slot = slot; command.snapshot = fresh.get();
    if (!enqueueArrangementLocked(command)) return {};
    stored = std::move(fresh);
    return stored;
}

bool TaktAudioProcessor::saveActivePatternLocked()
{
    auto snapshot = capturePatternLocked();
    const int slot = snapshot->sourceSlot;
    auto& stored = patternBank[static_cast<size_t>(slot)];
    if (performKit.load() && stored)
    {
        auto sequenceOnly = std::make_shared<PatternSnapshot>(*snapshot);
        sequenceOnly->samples = stored->samples;
        sequenceOnly->trackParameters = stored->trackParameters;
        sequenceOnly->slicePoints = stored->slicePoints;
        for (size_t p = 0; p < sequenceOnly->globalParameters.size(); ++p)
            if (kitGlobal(p)) sequenceOnly->globalParameters[p] = stored->globalParameters[p];
        snapshot = std::move(sequenceOnly);
    }
    ArrangementCommand command;
    command.kind = ArrangementCommand::Kind::UpdatePattern;
    command.slot = slot; command.snapshot = snapshot.get();
    if (!enqueueArrangementLocked(command)) return false;
    if (stored) retiredPatterns.push_back({stored, nextArrangementSerial});
    stored = std::move(snapshot);
    return true;
}

bool TaktAudioProcessor::setCurrentPattern(int slot) { return queuePattern(slot); }

bool TaktAudioProcessor::queuePattern(int slot)
{
    if (slot < 0 || slot >= takt::patternSlots) return false;
    servicePendingTransitions();
    commitControlAll();
    std::shared_ptr<const PatternSnapshot> destination;
    const bool playing = isUsingHostClock() ? isHostPlaying() : parameterValue("play") > .5f;
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (restoringPattern || arrangementFifo.getFreeSpace() < 3) return false;
        if (!saveActivePatternLocked()) return false;
        destination = ensurePatternLocked(slot);
        if (!destination) return false;
        ArrangementCommand command;
        command.kind = ArrangementCommand::Kind::Select; command.slot = slot; command.flag = true;
        if (!enqueueArrangementLocked(command)) return false;
        arrangementOriginEnabled.store(true);
        chainSelection.clear();
        queuedPatternSlot.store(playing ? slot : -1);
        if (!playing)
        {
            currentPatternSlot.store(slot);
            arrangementMode.store(takt::PatternChain::Mode::Pattern);
            currentSongSlot.store(-1); currentSongRow.store(-1);
            if (permanentPattern && permanentPattern != destination)
                retiredPatterns.push_back({permanentPattern, nextArrangementSerial});
            permanentPattern = destination;
            temporaryPattern.reset();
        }
    }
    if (!playing)
    {
        if (!performKit.load()) restorePattern(*destination);
        else
        {
            auto withKit = std::make_shared<PatternSnapshot>(*destination);
            {
                std::lock_guard<std::mutex> lock(controlMutex);
                const auto kit = capturePatternLocked();
                withKit->samples = kit->samples;
                withKit->trackParameters = kit->trackParameters;
                withKit->slicePoints = kit->slicePoints;
                for (size_t p = 0; p < withKit->globalParameters.size(); ++p)
                    if (kitGlobal(p)) withKit->globalParameters[p] = kit->globalParameters[p];
            }
            restorePattern(*withKit);
        }
    }
    notifyPatternChanged();
    return true;
}

int TaktAudioProcessor::getPatternLength() const
{
    std::lock_guard<std::mutex> lock(controlMutex);
    return patternLength;
}

void TaktAudioProcessor::setPatternLength(int length)
{
    servicePendingTransitions();
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        patternLength = juce::jlimit(1, takt::maxSteps, length);
        saveActivePatternLocked();
        editRevision.fetch_add(1, std::memory_order_relaxed);
    }
    notifyPatternChanged();
}

std::shared_ptr<const TaktAudioProcessor::PatternSnapshot> TaktAudioProcessor::getPatternSnapshot(int slot) const
{
    if (slot < 0 || slot >= takt::patternSlots) return {};
    std::lock_guard<std::mutex> lock(controlMutex);
    return slot == currentPatternSlot.load() ? capturePatternLocked() : patternBank[static_cast<size_t>(slot)];
}

bool TaktAudioProcessor::setPatternSnapshot(int slot, const PatternSnapshot& source)
{
    if (slot < 0 || slot >= takt::patternSlots) return false;
    for (const auto& sample : source.samples) if (!sample) return false;
    for (const auto& track : source.trackParameters)
        for (float value : track) if (!std::isfinite(value)) return false;
    for (float value : source.globalParameters) if (!std::isfinite(value)) return false;
    auto snapshot = std::make_shared<PatternSnapshot>(source);
    snapshot->patternLength = juce::jlimit(1, takt::maxSteps, source.patternLength);
    for (auto& length : snapshot->lengths) length = juce::jlimit(1, takt::maxSteps, length);
    for (auto& track : snapshot->steps) for (auto& step : track) step = sanitizedStep(step);
    for (int t = 0; t < takt::numTracks; ++t)
        for (size_t p = 0; p < trackParameterCount; ++p)
        {
            const auto* parameter = parameters.getParameter(parameterIDs[static_cast<size_t>(t)][p]);
            snapshot->trackParameters[static_cast<size_t>(t)][p] = parameter->convertFrom0to1(
                parameter->convertTo0to1(snapshot->trackParameters[static_cast<size_t>(t)][p]));
        }
    bool active = false;
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        ArrangementCommand command;
        command.kind = ArrangementCommand::Kind::UpdatePattern;
        command.slot = slot; command.snapshot = snapshot.get();
        if (!enqueueArrangementLocked(command)) return false;
        auto& stored = patternBank[static_cast<size_t>(slot)];
        if (stored) retiredPatterns.push_back({stored, nextArrangementSerial});
        stored = snapshot;
        active = slot == currentPatternSlot.load();
    }
    if (active) restorePattern(*snapshot);
    notifyPatternChanged();
    return true;
}

bool TaktAudioProcessor::setChain(const std::vector<int>& values)
{
    if (values.empty() || values.size() > takt::patternChainCapacity) return false;
    for (int slot : values) if (slot < 0 || slot >= takt::patternSlots) return false;
    servicePendingTransitions();
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (arrangementFifo.getFreeSpace() < static_cast<int>(values.size()) + 2) return false;
        if (!saveActivePatternLocked()) return false;
        for (int slot : values) if (!ensurePatternLocked(slot)) return false;
        ArrangementCommand command;
        command.kind = ArrangementCommand::Kind::Chain; command.count = static_cast<int>(values.size());
        std::copy(values.begin(), values.end(), command.chain.begin());
        if (!enqueueArrangementLocked(command)) return false;
        chainSelection = values;
        arrangementOriginEnabled.store(true);
        queuedPatternSlot.store(values.front());
    }
    notifyPatternChanged();
    return true;
}

std::vector<int> TaktAudioProcessor::getChain() const
{
    std::lock_guard<std::mutex> lock(controlMutex);
    return chainSelection;
}

takt::Song TaktAudioProcessor::getSong(int slot) const
{
    std::lock_guard<std::mutex> lock(controlMutex);
    return songMetadata.song(slot);
}

bool TaktAudioProcessor::setSong(int slot, const takt::Song& song)
{
    if (slot < 0 || slot >= takt::songSlots || song.rowCount < 0 || song.rowCount > takt::songRowCapacity)
        return false;
    for (int row = 0; row < song.rowCount; ++row)
        if (song.rows[static_cast<size_t>(row)].pattern.index < 0
            || song.rows[static_cast<size_t>(row)].pattern.index >= takt::patternSlots) return false;
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (arrangementFifo.getFreeSpace() < song.rowCount + 1) return false;
        for (int row = 0; row < song.rowCount; ++row)
            if (!ensurePatternLocked(song.rows[static_cast<size_t>(row)].pattern.index)) return false;
        takt::PatternChain sanitize;
        sanitize.setSong(slot, song);
        ArrangementCommand command;
        command.kind = ArrangementCommand::Kind::UpdateSong; command.slot = slot;
        command.song = sanitize.song(slot);
        if (!enqueueArrangementLocked(command)) return false;
        songMetadata.setSong(slot, command.song);
    }
    notifyPatternChanged();
    return true;
}

bool TaktAudioProcessor::startSong(int slot, int firstRow)
{
    if (slot < 0 || slot >= takt::songSlots) return false;
    servicePendingTransitions();
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        const auto& song = songMetadata.song(slot);
        if (song.rowCount == 0 || firstRow < 0 || firstRow >= song.rowCount
            || arrangementFifo.getFreeSpace() < 2) return false;
        if (!saveActivePatternLocked()) return false;
        ArrangementCommand command;
        command.kind = ArrangementCommand::Kind::Song; command.slot = slot; command.row = firstRow;
        if (!enqueueArrangementLocked(command)) return false;
        chainSelection.clear();
        arrangementOriginEnabled.store(true);
        queuedPatternSlot.store(song.rows[static_cast<size_t>(firstRow)].pattern.index);
    }
    notifyPatternChanged();
    return true;
}

bool TaktAudioProcessor::queueSongRow(int row)
{
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        const int slot = currentSongSlot.load();
        if (slot < 0 || row < 0 || row >= songMetadata.song(slot).rowCount) return false;
        ArrangementCommand command;
        command.kind = ArrangementCommand::Kind::JumpRow; command.row = row;
        if (!enqueueArrangementLocked(command)) return false;
    }
    notifyPatternChanged();
    return true;
}

void TaktAudioProcessor::setPerformKit(bool enabled)
{
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        ArrangementCommand command;
        command.kind = ArrangementCommand::Kind::PerformKit; command.flag = enabled;
        if (!enqueueArrangementLocked(command)) return;
        performKit.store(enabled);
    }
    notifyPatternChanged();
}

void TaktAudioProcessor::saveKit()
{
    servicePendingTransitions();
    std::lock_guard<std::mutex> lock(controlMutex);
    auto snapshot = capturePatternLocked();
    const int slot = currentPatternSlot.load();
    ArrangementCommand command;
    command.kind = ArrangementCommand::Kind::UpdatePattern;
    command.slot = slot; command.snapshot = snapshot.get();
    if (!enqueueArrangementLocked(command)) return;
    auto& stored = patternBank[static_cast<size_t>(slot)];
    if (stored) retiredPatterns.push_back({stored, nextArrangementSerial});
    stored = std::move(snapshot);
}

void TaktAudioProcessor::reloadKit()
{
    servicePendingTransitions();
    std::shared_ptr<const PatternSnapshot> snapshot;
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        const auto& kit = patternBank[static_cast<size_t>(currentPatternSlot.load())];
        if (!kit) return;
        auto current = std::make_shared<PatternSnapshot>(*capturePatternLocked());
        current->samples = kit->samples;
        current->trackParameters = kit->trackParameters;
        current->slicePoints = kit->slicePoints;
        for (size_t p = 0; p < current->globalParameters.size(); ++p)
            if (kitGlobal(p)) current->globalParameters[p] = kit->globalParameters[p];
        snapshot = std::move(current);
    }
    restorePattern(*snapshot);
    std::lock_guard<std::mutex> lock(controlMutex);
    saveActivePatternLocked();
    ArrangementCommand command;
    command.kind = ArrangementCommand::Kind::ReloadKit;
    command.snapshot = patternBank[static_cast<size_t>(currentPatternSlot.load())].get();
    enqueueArrangementLocked(command);
}

void TaktAudioProcessor::servicePendingTransitions()
{
    if (servicingTransitions.exchange(true)) return;
    const auto generation = audioTransitionGeneration.load(std::memory_order_acquire);
    const auto kitGeneration = audioKitGeneration.load(std::memory_order_acquire);
    if (generation == mirroredTransitionGeneration.load()
        && kitGeneration == mirroredKitGeneration.load())
    {
        servicingTransitions.store(false);
        return;
    }
    std::shared_ptr<const PatternSnapshot> mirror;
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (restoringPattern)
        {
            servicingTransitions.store(false);
            return;
        }
        mirror = capturePatternLocked();
        if (audioTransitionGeneration.load() != generation || audioKitGeneration.load() != kitGeneration)
        {
            servicingTransitions.store(false);
            return;
        }
        for (size_t t = 0; t < samples.size(); ++t)
            if (samples[t] != mirror->samples[t]) retainSampleLocked(samples[t]);
        steps = mirror->steps; lengths = mirror->lengths;
        samples = mirror->samples; slicePoints = mirror->slicePoints;
        patternLength = mirror->patternLength;
        for (size_t t = 0; t < samples.size(); ++t)
            sampleDurations[t].store(samples[t] ? samples[t]->left.size() / samples[t]->sampleRate : 0);
        undoEdit.available = false;
        temporaryPattern.reset();
        const auto& fallback = patternBank[static_cast<size_t>(currentPatternSlot.load())];
        if (permanentPattern && permanentPattern != fallback)
            retiredPatterns.push_back({permanentPattern, nextArrangementSerial});
        permanentPattern = fallback;
        restoringPattern = true;
    }
    // A concurrent automation event wins over the transition baseline. Host
    // callbacks happen here, outside the renderer and outside controlMutex.
    if (kitGeneration != mirroredKitGeneration.load())
        for (size_t t = 0; t < trackValues.size(); ++t)
            for (size_t p = 0; p < trackParameterCount; ++p)
                if (audioTransitionGeneration.load() == generation
                    && parameterVersions[t][p].load() == transitionParameterVersions[t][p].load())
                    setParameter(parameterIDs[t][p], mirror->trackParameters[t][p]);
    for (size_t p = 0; p < globalValues.size(); ++p)
        if (p != 0 && p != 1 && p != 4
            && (!kitGlobal(p) || kitGeneration != mirroredKitGeneration.load())
            && audioTransitionGeneration.load() == generation
            && globalParameterVersions[p].load() == transitionGlobalVersions[p].load())
            setParameter(globalNames[p], mirror->globalParameters[p]);
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        restoringPattern = false;
        if (audioTransitionGeneration.load() == generation)
        {
            mirroredTransitionGeneration.store(generation);
            mirroredKitGeneration.store(kitGeneration);
        }
    }
    servicingTransitions.store(false);
    notifyPatternChanged();
}

void TaktAudioProcessor::applyAudioPattern(const PatternSnapshot& snapshot, bool applyKit)
{
    audioSnapshotSequence.fetch_add(1, std::memory_order_acq_rel);
    for (int t = 0; t < takt::numTracks; ++t)
    {
        const auto index = static_cast<size_t>(t);
        engine.setTrackLength(t, snapshot.lengths[index]);
        for (int s = 0; s < takt::maxSteps; ++s) engine.setStep(t, s, snapshot.steps[index][static_cast<size_t>(s)]);
        if (applyKit)
        {
            engine.setSample(t, snapshot.samples[index]);
            appliedTrackParams[index] = makeTrackParams(t, snapshot.trackParameters[index], snapshot.slicePoints[index]);
            for (size_t p = 0; p < trackParameterCount; ++p)
                transitionParameterVersions[index][p].store(parameterVersions[index][p].load());
        }
        auto p = appliedTrackParams[index];
        if (audioArrangement.mode() == takt::PatternChain::Mode::Song)
            p.mute = (audioArrangement.current().muteMask & (1u << t)) != 0;
        engine.setTrackParams(t, p);
    }
    for (size_t p = 0; p < globalValues.size(); ++p)
    {
        if (applyKit || !kitGlobal(p)) transitionGlobalVersions[p].store(globalParameterVersions[p].load());
        if (p != 0 && p != 1 && p != 4 && (applyKit || !kitGlobal(p))) appliedGlobals[p] = snapshot.globalParameters[p];
    }
    engine.setFx({appliedGlobals[5], appliedGlobals[6], appliedGlobals[7], appliedGlobals[8]});
    engine.setChorus({appliedGlobals[10], appliedGlobals[11], appliedGlobals[12], appliedGlobals[13],
                      appliedGlobals[14], appliedGlobals[15], appliedGlobals[16]});
    engine.setSwing(audioArrangement.current().swing);
    engine.setFill(appliedGlobals[9] > .5f);
    if (arrangementOriginEnabled) engine.restartSequencer(true);
    const auto generation = audioTransitionGeneration.load() + 1;
    if (applyKit)
    {
        activeAudioKitSnapshot.store(&snapshot, std::memory_order_release);
        audioKitGeneration.store(generation, std::memory_order_release);
    }
    activeAudioSnapshot.store(&snapshot, std::memory_order_release);
    publishArrangementPosition();
    audioTransitionGeneration.store(generation, std::memory_order_release);
    audioSnapshotSequence.fetch_add(1, std::memory_order_release);
}

void TaktAudioProcessor::publishArrangementPosition()
{
    currentPatternSlot.store(audioArrangement.currentPattern().index);
    queuedPatternSlot.store(audioArrangement.hasQueuedSelection() ? audioArrangement.queuedPattern().index : -1);
    arrangementMode.store(audioArrangement.mode());
    currentSongSlot.store(audioArrangement.currentSong());
    currentSongRow.store(audioArrangement.currentRow());
}

void TaktAudioProcessor::applyAudioTransition(const takt::PatternChain::Transition& transition)
{
    if (transition.changed && (transition.patternChanged || transition.rowChanged || transition.applyKit))
        if (const auto* snapshot = audioBank[static_cast<size_t>(transition.pattern.index)])
            applyAudioPattern(*snapshot, transition.applyKit);
    if (transition.changed)
    {
        engine.setSwing(transition.swing);
        for (int t = 0; t < takt::numTracks; ++t)
            {
                auto p = appliedTrackParams[static_cast<size_t>(t)];
                if (audioArrangement.mode() == takt::PatternChain::Mode::Song)
                    p.mute = (transition.muteMask & (1u << t)) != 0;
                engine.setTrackParams(t, p);
            }
    }
    publishArrangementPosition();
}

void TaktAudioProcessor::drainArrangementCommands()
{
    int first = 0, count = 0, second = 0, secondCount = 0;
    arrangementFifo.prepareToRead(arrangementFifo.getNumReady(), first, count, second, secondCount);
    auto consume = [&](int offset, int size)
    {
        for (int i = 0; i < size; ++i)
        {
            const auto& c = (*arrangementCommands)[static_cast<size_t>(offset + i)];
            switch (c.kind)
            {
                case ArrangementCommand::Kind::Reset:
                    audioArrangement = takt::PatternChain{};
                    audioBank.fill(nullptr);
                    arrangementWasPlaying = false;
                    arrangementOriginEnabled = c.flag;
                    arrangementPpq = 0; nextArrangementStepPpq = .25;
                    break;
                case ArrangementCommand::Kind::UpdatePattern:
                {
                    audioBank[static_cast<size_t>(c.slot)] = c.snapshot;
                    const auto& p = *c.snapshot;
                    std::uint16_t mute = 0;
                    for (int t = 0; t < takt::numTracks; ++t)
                        if (p.trackParameters[static_cast<size_t>(t)][14] > .5f) mute |= static_cast<std::uint16_t>(1u << t);
                    audioArrangement.setPatternSettings({c.slot}, {p.patternLength, p.globalParameters[2], p.globalParameters[3], mute});
                    break;
                }
                case ArrangementCommand::Kind::Select:
                    arrangementOriginEnabled = arrangementOriginEnabled || c.flag;
                    applyAudioTransition(audioArrangement.selectPattern({c.slot}));
                    break;
                case ArrangementCommand::Kind::Chain:
                {
                    arrangementOriginEnabled = true;
                    std::array<takt::PatternAddress, takt::patternChainCapacity> values{};
                    for (int j = 0; j < c.count; ++j) values[static_cast<size_t>(j)] = {c.chain[static_cast<size_t>(j)]};
                    applyAudioTransition(audioArrangement.setChain(values.data(), c.count));
                    break;
                }
                case ArrangementCommand::Kind::UpdateSong:
                    audioArrangement.setSong(c.slot, c.song);
                    break;
                case ArrangementCommand::Kind::Song:
                    arrangementOriginEnabled = true;
                    applyAudioTransition(audioArrangement.selectSong(c.slot, c.row));
                    break;
                case ArrangementCommand::Kind::JumpRow:
                    audioArrangement.queueSongRow(c.row);
                    applyAudioTransition(audioArrangement.current());
                    break;
                case ArrangementCommand::Kind::PerformKit:
                    audioArrangement.setPerformKit(c.flag);
                    break;
                case ArrangementCommand::Kind::ReloadKit:
                    if (c.snapshot) applyAudioPattern(*c.snapshot, true);
                    break;
            }
            consumedArrangementSerial.store(c.serial, std::memory_order_release);
        }
    };
    consume(first, count); consume(second, secondCount);
    arrangementFifo.finishedRead(count + secondCount);
    publishArrangementPosition();
}

void TaktAudioProcessor::temporaryReloadPattern()
{
    servicePendingTransitions();
    std::shared_ptr<const PatternSnapshot> snapshot;
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        snapshot = temporaryPattern ? temporaryPattern : permanentPattern;
    }
    if (snapshot) restorePattern(*snapshot);
}

std::array<takt::SlicePoint, takt::maxSlices> TaktAudioProcessor::getSlicePoints(int track) const
{
    if (!validTrack(track)) return {};
    std::lock_guard<std::mutex> lock(controlMutex);
    if (audioKitGeneration.load() != mirroredKitGeneration.load())
        if (const auto* snapshot = activeAudioKitSnapshot.load()) return snapshot->slicePoints[static_cast<size_t>(track)];
    return slicePoints[static_cast<size_t>(track)];
}

bool TaktAudioProcessor::setSlicePoint(int track, int slice, takt::SlicePoint point)
{
    if (!validTrack(track) || slice < 0 || slice >= takt::maxSlices
        || !std::isfinite(point.start) || !std::isfinite(point.end) || !std::isfinite(point.loop)) return false;
    point.start = juce::jlimit(0.0f, .999f, point.start);
    point.end = juce::jlimit(point.start + .001f, 1.0f, point.end);
    point.loop = juce::jlimit(point.start, point.end, point.loop);
    servicePendingTransitions();
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (restoringPattern) return false;
        slicePoints[static_cast<size_t>(track)][static_cast<size_t>(slice)] = point;
        editRevision.fetch_add(1, std::memory_order_relaxed);
    }
    notifyPatternChanged();
    return true;
}

bool TaktAudioProcessor::createSliceGrid(int track, int count)
{
    if (!validTrack(track) || count < 1 || count > takt::maxSlices) return false;
    servicePendingTransitions();
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (restoringPattern) return false;
        restoringPattern = true;
        auto& points = slicePoints[static_cast<size_t>(track)];
        points.fill(takt::SlicePoint{});
        for (int slice = 0; slice < count; ++slice)
        {
            auto& point = points[static_cast<size_t>(slice)];
            point.start = static_cast<float>(slice) / static_cast<float>(count);
            point.end = static_cast<float>(slice + 1) / static_cast<float>(count);
            point.loop = point.start;
        }
        editRevision.fetch_add(1, std::memory_order_relaxed);
    }
    setParameter(trackParameterID(track, "sliceCount"), static_cast<float>(count));
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        restoringPattern = false;
    }
    notifyPatternChanged();
    return true;
}

bool TaktAudioProcessor::beginControlAll(int activeTrack, std::uint16_t trackMask)
{
    if (!validTrack(activeTrack)) return false;
    servicePendingTransitions();
    std::lock_guard<std::mutex> lock(controlMutex);
    if (restoringPattern || controlAll.active) return false;
    controlAll.activeTrack = activeTrack;
    controlAll.mask = static_cast<std::uint16_t>(trackMask | (1u << activeTrack));
    controlAll.changed.fill(false);
    for (size_t track = 0; track < controlAll.baseline.size(); ++track)
        for (size_t parameter = 0; parameter < trackParameterCount; ++parameter)
            controlAll.baseline[track][parameter] = trackValues[track][parameter]->load();
    controlAll.active = true;
    return true;
}

bool TaktAudioProcessor::updateControlAll(const juce::String& suffix, float activeTrackValue)
{
    if (!std::isfinite(activeTrackValue)) return false;
    size_t index = std::size(trackNames);
    for (size_t candidate = 0; candidate < std::size(trackNames); ++candidate)
        if (suffix == trackNames[candidate]) { index = candidate; break; }
    if (index == std::size(trackNames)) return false;
    ControlAllTransaction transaction;
    bool startGesture = false;
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (!controlAll.active || restoringPattern) return false;
        transaction = controlAll;
        startGesture = !controlAll.changed[index];
        controlAll.changed[index] = true;
        restoringPattern = true;
    }
    const float delta = activeTrackValue - transaction.baseline[static_cast<size_t>(transaction.activeTrack)][index];
    for (int track = 0; track < takt::numTracks; ++track)
        if ((transaction.mask & (1u << track)) != 0)
            if (auto* parameter = parameters.getParameter(trackParameterID(track, suffix)))
            {
                if (startGesture) parameter->beginChangeGesture();
                parameter->setValueNotifyingHost(parameter->convertTo0to1(
                    transaction.baseline[static_cast<size_t>(track)][index] + delta));
            }
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        restoringPattern = false;
    }
    return true;
}

void TaktAudioProcessor::commitControlAll()
{
    ControlAllTransaction transaction;
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (!controlAll.active || restoringPattern) return;
        transaction = controlAll;
        controlAll.active = false;
    }
    for (int track = 0; track < takt::numTracks; ++track)
        if ((transaction.mask & (1u << track)) != 0)
            for (size_t parameter = 0; parameter < trackParameterCount; ++parameter)
                if (transaction.changed[parameter])
                    parameters.getParameter(trackParameterID(track, trackNames[parameter]))->endChangeGesture();
}

void TaktAudioProcessor::cancelControlAll()
{
    ControlAllTransaction transaction;
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (!controlAll.active || restoringPattern) return;
        transaction = controlAll;
        restoringPattern = true;
        controlAll.active = false;
    }
    for (int track = 0; track < takt::numTracks; ++track)
        if ((transaction.mask & (1u << track)) != 0)
            for (size_t parameter = 0; parameter < trackParameterCount; ++parameter)
                if (transaction.changed[parameter])
                {
                    auto* value = parameters.getParameter(trackParameterID(track, trackNames[parameter]));
                    value->setValueNotifyingHost(value->convertTo0to1(transaction.baseline[static_cast<size_t>(track)][parameter]));
                    value->endChangeGesture();
                }
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        restoringPattern = false;
    }
}

bool TaktAudioProcessor::isControlAllActive() const
{
    std::lock_guard<std::mutex> lock(controlMutex);
    return controlAll.active;
}

void TaktAudioProcessor::loadDemoPattern()
{
    servicePendingTransitions();
    std::unique_lock<std::mutex> lock(controlMutex);
    for (auto& track : steps) track.fill(takt::Step{});
    lengths.fill(16);
    for (int s : { 0, 4, 8, 12 }) steps[0][static_cast<size_t>(s)].enabled = true;
    for (int s : { 4, 12 }) steps[1][static_cast<size_t>(s)].enabled = true;
    for (int s = 0; s < 16; s += 2) { steps[2][static_cast<size_t>(s)].enabled = true; steps[2][static_cast<size_t>(s)].velocity = s % 4 == 0 ? .6f : .35f; }
    for (int s : { 6, 14 }) { steps[3][static_cast<size_t>(s)].enabled = true; steps[3][static_cast<size_t>(s)].velocity = .35f; }
    for (int s : { 0, 7, 10 }) { steps[8][static_cast<size_t>(s)].enabled = true; steps[8][static_cast<size_t>(s)].velocity = .55f; }
    steps[8][7].lockPitch = true; steps[8][7].pitch = 7;
    steps[8][10].lockPitch = true; steps[8][10].pitch = 12;
    patternDirty.store(true);
    editRevision.fetch_add(1, std::memory_order_relaxed);
    lock.unlock();
    notifyPatternChanged();
}

void TaktAudioProcessor::triggerTrack(int track, float velocity)
{
    if (!validTrack(track)) return;
    int first = 0, count = 0, second = 0, count2 = 0;
    triggerFifo.prepareToWrite(1, first, count, second, count2);
    if (count > 0)
    {
        auto& event = queuedTriggers[static_cast<size_t>(first)];
        event = { 0, track, juce::jlimit(0.0f, 1.0f, velocity), 0 };
        event.gateBeats = .25;
    }
    triggerFifo.finishedWrite(count);
}

void TaktAudioProcessor::triggerSlice(int track, int slice, float velocity)
{
    if (!validTrack(track) || slice < 0 || slice >= takt::maxSlices || !std::isfinite(velocity)) return;
    int first = 0, count = 0, second = 0, count2 = 0;
    triggerFifo.prepareToWrite(1, first, count, second, count2);
    if (count > 0)
    {
        auto& event = queuedTriggers[static_cast<size_t>(first)];
        event = {0, track, juce::jlimit(0.0f, 1.0f, velocity), 0, 60, slice, true};
        event.gateBeats = .25;
    }
    triggerFifo.finishedWrite(count);
}

bool TaktAudioProcessor::loadSample(int track, const juce::File& file, juce::String& error)
{
    if (!validTrack(track)) { error = "Invalid track"; return false; }
    servicePendingTransitions();
    releaseUnusedSamples();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader) { error = "Choose a readable WAV, AIFF or FLAC file."; return false; }
    if (reader->lengthInSamples < 2 || reader->lengthInSamples > maxSampleFrames
        || reader->lengthInSamples > reader->sampleRate * 60.0 || reader->numChannels == 0
        || reader->sampleRate < 8000 || reader->sampleRate > 384000)
    { error = "Sample must be between 2 frames and 60 seconds (8–384 kHz)."; return false; }
    const int count = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> audio(2, count);
    if (!reader->read(&audio, 0, count, 0, true, true)) { error = "Could not decode the sample."; return false; }
    if (reader->numChannels == 1) audio.copyFrom(1, 0, audio, 0, 0, count);
    auto sample = std::make_shared<takt::Sample>();
    sample->name = file.getFileNameWithoutExtension().toStdString();
    sample->sampleRate = reader->sampleRate;
    sample->left.assign(audio.getReadPointer(0), audio.getReadPointer(0) + count);
    sample->right.assign(audio.getReadPointer(1), audio.getReadPointer(1) + count);
    for (auto* channel : { &sample->left, &sample->right })
        for (float& v : *channel) if (!std::isfinite(v)) v = 0;
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        retainSampleLocked(samples[static_cast<size_t>(track)]);
        samples[static_cast<size_t>(track)] = std::move(sample);
        sampleDurations[static_cast<size_t>(track)].store(count / reader->sampleRate);
        samplesDirty.store(true);
        editRevision.fetch_add(1, std::memory_order_relaxed);
    }
    error.clear();
    notifyPatternChanged();
    return true;
}

std::shared_ptr<const takt::Sample> TaktAudioProcessor::getSample(int track) const
{
    if (!validTrack(track)) return {};
    std::lock_guard<std::mutex> lock(controlMutex);
    if (audioKitGeneration.load() != mirroredKitGeneration.load())
        if (const auto* snapshot = activeAudioKitSnapshot.load()) return snapshot->samples[static_cast<size_t>(track)];
    return samples[static_cast<size_t>(track)];
}

juce::String TaktAudioProcessor::getSampleName(int track) const
{
    auto sample = getSample(track);
    return sample ? juce::String(sample->name) : "No sample";
}

int TaktAudioProcessor::getCurrentStep(int track) const
{
    return validTrack(track) ? currentSteps[static_cast<size_t>(track)].load() : -1;
}

void TaktAudioProcessor::releaseUnusedSamples()
{
    std::lock_guard<std::mutex> lock(controlMutex);
    const auto serial = consumedArrangementSerial.load(std::memory_order_acquire);
    const auto* active = activeAudioSnapshot.load(std::memory_order_acquire);
    const auto* kit = activeAudioKitSnapshot.load(std::memory_order_acquire);
    retiredPatterns.erase(std::remove_if(retiredPatterns.begin(), retiredPatterns.end(),
        [&](const auto& retired)
        {
            if (retired.serial > serial || retired.snapshot.get() == active || retired.snapshot.get() == kit)
                return false;
            for (const auto& sample : retired.snapshot->samples) retainSampleLocked(sample);
            return true;
        }), retiredPatterns.end());
    retiredSamples.erase(std::remove_if(retiredSamples.begin(), retiredSamples.end(),
                        [](const auto& sample) { return sample.use_count() == 1; }), retiredSamples.end());
}

void TaktAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    releaseUnusedSamples();
    std::shared_ptr<const PatternSnapshot> active;
    decltype(patternBank) bank;
    std::array<takt::Song, takt::songSlots> songs{};
    int slot = 0, songSlot = -1, songRow = -1;
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        active = capturePatternLocked();
        bank = patternBank;
        slot = active->sourceSlot;
        songSlot = active->sourceSong;
        songRow = active->sourceRow;
        // PERFORM KIT never autosaves its tweaks into a stored pattern kit.
        if (!performKit.load()) bank[static_cast<size_t>(slot)] = active;
        else
        {
            auto stored = std::make_shared<PatternSnapshot>(*active);
            if (bank[static_cast<size_t>(slot)])
            {
                const auto& kit = *bank[static_cast<size_t>(slot)];
                stored->samples = kit.samples; stored->trackParameters = kit.trackParameters;
                stored->slicePoints = kit.slicePoints;
                for (size_t p = 0; p < stored->globalParameters.size(); ++p)
                    if (kitGlobal(p)) stored->globalParameters[p] = kit.globalParameters[p];
            }
            bank[static_cast<size_t>(slot)] = std::move(stored);
        }
        for (int s = 0; s < takt::songSlots; ++s) songs[static_cast<size_t>(s)] = songMetadata.song(s);
    }
    auto state = parameters.copyState();
    for (const char* type : {"SEQUENCER", "BANK", "SAMPLE_POOL", "SONGS"})
        if (auto previous = state.getChildWithName(type); previous.isValid()) state.removeChild(previous, nullptr);
    // APVTS can still contain the previous pattern between the audio boundary
    // and the editor timer. The saved root must describe the sounding pattern.
    for (int t = 0; t < takt::numTracks; ++t)
        for (size_t p = 0; p < trackParameterCount; ++p)
            if (auto child = state.getChildWithProperty("id", parameterIDs[static_cast<size_t>(t)][p]); child.isValid())
                child.setProperty("value", active->trackParameters[static_cast<size_t>(t)][p], nullptr);
    for (size_t p = 0; p < std::size(globalNames); ++p)
        if (p != 0 && p != 1 && p != 4)
            if (auto child = state.getChildWithProperty("id", globalNames[p]); child.isValid())
                child.setProperty("value", active->globalParameters[p], nullptr);

    std::vector<std::shared_ptr<const takt::Sample>> pool;
    auto sampleIndex = [&](const std::shared_ptr<const takt::Sample>& sample)
    {
        auto found = std::find(pool.begin(), pool.end(), sample);
        if (found != pool.end()) return static_cast<int>(found - pool.begin());
        pool.push_back(sample);
        return static_cast<int>(pool.size() - 1);
    };
    auto writeSequencer = [&](const PatternSnapshot& snapshot, bool embedded)
    {
        juce::ValueTree sequencer("SEQUENCER");
        sequencer.setProperty("masterLength", snapshot.patternLength, nullptr);
        for (int t = 0; t < takt::numTracks; ++t)
        {
            const auto index = static_cast<size_t>(t);
            juce::ValueTree track("TRACK");
            track.setProperty("index", t, nullptr);
            track.setProperty("length", snapshot.lengths[index], nullptr);
            juce::MemoryOutputStream points;
            for (const auto& point : snapshot.slicePoints[index])
            {
                points.writeFloat(point.start); points.writeFloat(point.end); points.writeFloat(point.loop);
            }
            track.setProperty("slicePoints", juce::var(points.getMemoryBlock()), nullptr);
            if (const auto& sample = snapshot.samples[index])
            {
                track.setProperty("sampleName", juce::String(sample->name), nullptr);
                track.setProperty("sampleRef", sampleIndex(sample), nullptr);
                if (embedded) track.setProperty("sampleData", juce::var(encodeSample(*sample)), nullptr);
            }
            for (const auto& value : snapshot.steps[index])
            {
                juce::ValueTree step("STEP");
                step.setProperty("enabled", value.enabled, nullptr);
                step.setProperty("velocity", value.velocity, nullptr);
                step.setProperty("probability", value.probability, nullptr);
                step.setProperty("pitch", value.pitch, nullptr);
                step.setProperty("cutoff", value.cutoff, nullptr);
                step.setProperty("lockPitch", value.lockPitch, nullptr);
                step.setProperty("lockCutoff", value.lockCutoff, nullptr);
                step.setProperty("conditionEvery", value.conditionEvery, nullptr);
                step.setProperty("conditionOffset", value.conditionOffset, nullptr);
                step.setProperty("retrigs", value.retrigs, nullptr);
                step.setProperty("microtiming", value.microtiming, nullptr);
                step.setProperty("note", value.note, nullptr);
                step.setProperty("slice", value.slice, nullptr);
                step.setProperty("lockSlice", value.lockSlice, nullptr);
                step.setProperty("lfoTrig", value.lfoTrig, nullptr);
                step.setProperty("filterTrig", value.filterTrig, nullptr);
                step.setProperty("advanced", value.advanced, nullptr);
                step.setProperty("lockTrig", value.lockTrig, nullptr);
                step.setProperty("conditionKind", static_cast<int>(value.rule.condition), nullptr);
                step.setProperty("conditionInverted", value.rule.inverted, nullptr);
                step.setProperty("cycleA", value.rule.cycleA, nullptr);
                step.setProperty("cycleB", value.rule.cycleB, nullptr);
                step.setProperty("fillRule", static_cast<int>(value.rule.fill), nullptr);
                step.setProperty("noteLengthBeats", value.noteLengthBeats, nullptr);
                step.setProperty("retrigEnabled", value.retrig.enabled, nullptr);
                step.setProperty("retrigRate", value.retrig.rateIndex, nullptr);
                step.setProperty("retrigFadeLength", value.retrig.fadeLengthBeats, nullptr);
                step.setProperty("retrigVelocityFade", value.retrig.velocityFade, nullptr);
                track.addChild(step, -1, nullptr);
            }
            sequencer.addChild(track, -1, nullptr);
        }
        return sequencer;
    };
    state.addChild(writeSequencer(*active, true), -1, nullptr);
    juce::ValueTree bankTree("BANK");
    bankTree.setProperty("current", slot, nullptr);
    bankTree.setProperty("localOrigin", arrangementOriginEnabled.load(), nullptr);
    bankTree.setProperty("performKit", performKit.load(), nullptr);
    // Chains are intentionally transient; songs and pattern contents persist.
    bankTree.setProperty("song", songSlot, nullptr);
    bankTree.setProperty("row", songRow, nullptr);
    for (int p = 0; p < takt::patternSlots; ++p)
        if (const auto& snapshot = bank[static_cast<size_t>(p)])
        {
            juce::ValueTree pattern("PATTERN");
            pattern.setProperty("slot", p, nullptr);
            juce::MemoryOutputStream values;
            for (const auto& track : snapshot->trackParameters) for (float value : track) values.writeFloat(value);
            for (float value : snapshot->globalParameters) values.writeFloat(value);
            pattern.setProperty("values", juce::var(values.getMemoryBlock()), nullptr);
            pattern.addChild(writeSequencer(*snapshot, false), -1, nullptr);
            bankTree.addChild(pattern, -1, nullptr);
        }
    state.addChild(bankTree, -1, nullptr);
    juce::ValueTree songTree("SONGS");
    for (int s = 0; s < takt::songSlots; ++s)
    {
        const auto& song = songs[static_cast<size_t>(s)];
        juce::ValueTree item("SONG");
        item.setProperty("slot", s, nullptr); item.setProperty("rowCount", song.rowCount, nullptr);
        item.setProperty("endLoop", song.endLoop, nullptr); item.setProperty("tempo", song.tempo, nullptr);
        for (int r = 0; r < song.rowCount; ++r)
        {
            const auto& row = song.rows[static_cast<size_t>(r)];
            juce::ValueTree line("ROW");
            line.setProperty("pattern", row.pattern.index, nullptr); line.setProperty("repeats", row.repeats, nullptr);
            line.setProperty("length", row.length, nullptr); line.setProperty("tempo", row.tempo, nullptr);
            line.setProperty("swing", row.swing, nullptr); line.setProperty("muteMask", row.muteMask, nullptr);
            item.addChild(line, -1, nullptr);
        }
        songTree.addChild(item, -1, nullptr);
    }
    state.addChild(songTree, -1, nullptr);
    juce::ValueTree poolTree("SAMPLE_POOL");
    for (size_t p = 0; p < pool.size(); ++p)
    {
        juce::ValueTree sample("SAMPLE");
        sample.setProperty("id", static_cast<int>(p), nullptr);
        sample.setProperty("name", juce::String(pool[p]->name), nullptr);
        sample.setProperty("data", juce::var(encodeSample(*pool[p])), nullptr);
        poolTree.addChild(sample, -1, nullptr);
    }
    state.addChild(poolTree, -1, nullptr);
    destination.reset();
    juce::MemoryOutputStream stream(destination, false);
    stream.writeString("TAKTII_STATE_2");
    state.writeToStream(stream);
}
void TaktAudioProcessor::setStateInformation(const void* data, int size)
{
    if (size <= 0) return;
    releaseUnusedSamples();
    juce::MemoryInputStream input(data, static_cast<size_t>(size), false);
    const auto signature = input.readString();
    const bool legacy = signature == "TAKTII_STATE_1";
    if (!legacy && signature != "TAKTII_STATE_2") return;
    auto state = juce::ValueTree::readFromStream(input);
    if (!state.isValid() || state.getType() != parameters.state.getType()) return;
    for (auto child : state)
        if (child.hasType("PARAM"))
        {
            const float value = static_cast<float>(child["value"]);
            if (!std::isfinite(value)) return;
            const auto id = child["id"].toString();
            if (auto* parameter = parameters.getParameter(id))
                child.setProperty("value", parameter->convertFrom0to1(parameter->convertTo0to1(value)), nullptr);
        }
    // Missing extensions must reset to defaults, even when loading an old
    // project into an instance which already has a different machine/LFO.
    // STATE_1 always selects the untouched legacy render path.
    for (int track = 0; track < takt::numTracks; ++track)
        for (size_t parameter = 16; parameter < std::size(trackNames); ++parameter)
        {
            const auto id = trackParameterID(track, trackNames[parameter]);
            auto child = state.getChildWithProperty("id", id);
            if (legacy && child.isValid()) { state.removeChild(child, nullptr); child = {}; }
            if (!child.isValid())
            {
                auto* value = parameters.getParameter(id);
                juce::ValueTree defaultParameter("PARAM");
                defaultParameter.setProperty("id", id, nullptr);
                defaultParameter.setProperty("value", value->convertFrom0to1(value->getDefaultValue()), nullptr);
                state.addChild(defaultParameter, -1, nullptr);
            }
        }
    for (size_t parameter = 9; parameter < std::size(globalNames); ++parameter)
    {
        const auto id = juce::String(globalNames[parameter]);
        auto child = state.getChildWithProperty("id", id);
        if (legacy && child.isValid()) { state.removeChild(child, nullptr); child = {}; }
        if (!child.isValid())
        {
            auto* value = parameters.getParameter(id);
            juce::ValueTree defaultParameter("PARAM");
            defaultParameter.setProperty("id", id, nullptr);
            defaultParameter.setProperty("value", value->convertFrom0to1(value->getDefaultValue()), nullptr);
            state.addChild(defaultParameter, -1, nullptr);
        }
    }
    std::vector<std::shared_ptr<const takt::Sample>> pool;
    if (!legacy)
        if (auto poolTree = state.getChildWithName("SAMPLE_POOL"); poolTree.isValid())
        {
            if (poolTree.getNumChildren() > takt::patternSlots * takt::numTracks) return;
            for (int p = 0; p < poolTree.getNumChildren(); ++p)
            {
                const auto item = poolTree.getChild(p);
                if (!item.hasType("SAMPLE") || static_cast<int>(item["id"]) != p) return;
                const auto encoded = item["data"];
                const auto* blob = encoded.getBinaryData();
                if (!blob) return;
                auto sample = decodeSample(*blob, item["name"].toString());
                if (!sample) return;
                pool.push_back(std::move(sample));
            }
        }
    auto readSequencer = [&](juce::ValueTree sequencer, PatternSnapshot& snapshot, bool embedded)
    {
        if (!sequencer.isValid() || !sequencer.hasType("SEQUENCER")
            || sequencer.getNumChildren() != takt::numTracks) return false;
        snapshot.patternLength = juce::jlimit(1, takt::maxSteps,
            static_cast<int>(sequencer.getProperty("masterLength", 16)));
        for (int t = 0; t < takt::numTracks; ++t)
        {
            const auto index = static_cast<size_t>(t);
            auto track = sequencer.getChild(t);
            if (!track.hasType("TRACK") || static_cast<int>(track["index"]) != t
                || track.getNumChildren() != takt::maxSteps) return false;
            snapshot.lengths[index] = juce::jlimit(1, takt::maxSteps, static_cast<int>(track["length"]));
            std::shared_ptr<const takt::Sample> sample;
            if (embedded)
            {
                const auto encoded = track["sampleData"];
                const auto* blob = encoded.getBinaryData();
                if (!blob) return false;
                sample = decodeSample(*blob, track["sampleName"].toString());
                if (!sample) return false;
            }
            if (!legacy && track.hasProperty("sampleRef") && !pool.empty())
            {
                const int reference = track["sampleRef"];
                if (reference < 0 || reference >= static_cast<int>(pool.size())) return false;
                const auto& pooled = pool[static_cast<size_t>(reference)];
                if (embedded && (sample->name != pooled->name || sample->sampleRate != pooled->sampleRate
                    || sample->left != pooled->left || sample->right != pooled->right)) return false;
                sample = pooled;
            }
            if (!sample) return false;
            snapshot.samples[index] = std::move(sample);
            if (!legacy && track.hasProperty("slicePoints"))
            {
                const auto pointData = track["slicePoints"];
                const auto* blob = pointData.getBinaryData();
                if (!blob || blob->getSize() != takt::maxSlices * 3 * sizeof(float)) return false;
                juce::MemoryInputStream points(*blob, false);
                for (auto& point : snapshot.slicePoints[index])
                {
                    point.start = points.readFloat(); point.end = points.readFloat(); point.loop = points.readFloat();
                    if (!std::isfinite(point.start) || !std::isfinite(point.end) || !std::isfinite(point.loop)
                        || point.start < 0 || point.start > 1 || point.end < 0 || point.end > 1
                        || point.loop < 0 || point.loop > 1
                        || (point.end != 0 && point.end <= point.start)) return false;
                }
            }
            for (int s = 0; s < takt::maxSteps; ++s)
            {
                const auto child = track.getChild(s);
                if (!child.hasType("STEP")) return false;
                auto& value = snapshot.steps[index][static_cast<size_t>(s)];
                value.enabled = child["enabled"]; value.velocity = child["velocity"];
                value.probability = child["probability"]; value.pitch = child["pitch"];
                value.cutoff = child["cutoff"]; value.lockPitch = child["lockPitch"]; value.lockCutoff = child["lockCutoff"];
                value.conditionEvery = child["conditionEvery"]; value.conditionOffset = child["conditionOffset"];
                value.retrigs = child["retrigs"]; value.microtiming = child["microtiming"];
                if (!legacy)
                {
                    value.note = child.getProperty("note", 60); value.slice = child.getProperty("slice", 0);
                    value.lockSlice = child.getProperty("lockSlice", false);
                    value.lfoTrig = child.getProperty("lfoTrig", true);
                    value.filterTrig = child.getProperty("filterTrig", true);
                    value.advanced = child.getProperty("advanced", false);
                    value.lockTrig = child.getProperty("lockTrig", false);
                    value.rule.condition = static_cast<takt::sequencer::Condition>(
                        static_cast<int>(child.getProperty("conditionKind", 0)));
                    value.rule.inverted = child.getProperty("conditionInverted", false);
                    value.rule.cycleA = child.getProperty("cycleA", 1); value.rule.cycleB = child.getProperty("cycleB", 1);
                    value.rule.fill = static_cast<takt::sequencer::Fill>(static_cast<int>(child.getProperty("fillRule", 0)));
                    value.noteLengthBeats = child.getProperty("noteLengthBeats", .25f);
                    value.retrig.enabled = child.getProperty("retrigEnabled", false);
                    value.retrig.rateIndex = child.getProperty("retrigRate", 9);
                    value.retrig.fadeLengthBeats = child.getProperty("retrigFadeLength", .25);
                    value.retrig.velocityFade = child.getProperty("retrigVelocityFade", 0.0f);
                }
                value = sanitizedStep(value);
            }
        }
        return true;
    };
    auto active = std::make_shared<PatternSnapshot>();
    if (!readSequencer(state.getChildWithName("SEQUENCER"), *active, true)) return;
    for (int t = 0; t < takt::numTracks; ++t)
        for (size_t p = 0; p < trackParameterCount; ++p)
        {
            auto* parameter = parameters.getParameter(parameterIDs[static_cast<size_t>(t)][p]);
            const auto child = state.getChildWithProperty("id", parameterIDs[static_cast<size_t>(t)][p]);
            active->trackParameters[static_cast<size_t>(t)][p] = child.isValid()
                ? static_cast<float>(child["value"]) : parameter->convertFrom0to1(parameter->getDefaultValue());
        }
    for (size_t p = 0; p < std::size(globalNames); ++p)
    {
        auto* parameter = parameters.getParameter(globalNames[p]);
        const auto child = state.getChildWithProperty("id", globalNames[p]);
        active->globalParameters[p] = child.isValid()
            ? static_cast<float>(child["value"]) : parameter->convertFrom0to1(parameter->getDefaultValue());
    }
    decltype(patternBank) nextBank{};
    takt::PatternChain nextSongs;
    int activeSlot = 0, songSlot = -1, songRow = -1;
    bool nextPerform = false, localOrigin = false;
    if (!legacy)
        if (auto bank = state.getChildWithName("BANK"); bank.isValid())
        {
            activeSlot = bank.getProperty("current", 0);
            songSlot = bank.getProperty("song", -1); songRow = bank.getProperty("row", -1);
            nextPerform = bank.getProperty("performKit", false);
            localOrigin = bank.getProperty("localOrigin", activeSlot != 0 || songSlot >= 0);
            if (activeSlot < 0 || activeSlot >= takt::patternSlots || bank.getNumChildren() > takt::patternSlots
                || songSlot < -1 || songSlot >= takt::songSlots) return;
            for (auto item : bank)
            {
                if (!item.hasType("PATTERN")) return;
                const int slot = item["slot"];
                if (slot < 0 || slot >= takt::patternSlots || nextBank[static_cast<size_t>(slot)]) return;
                auto snapshot = std::make_shared<PatternSnapshot>();
                const auto dataValues = item["values"];
                const auto* blob = dataValues.getBinaryData();
                if (!blob || blob->getSize() != (takt::numTracks * trackParameterCount + std::size(globalNames)) * sizeof(float)
                    || !readSequencer(item.getChildWithName("SEQUENCER"), *snapshot, false)) return;
                juce::MemoryInputStream values(*blob, false);
                for (int t = 0; t < takt::numTracks; ++t)
                    for (size_t p = 0; p < trackParameterCount; ++p)
                    {
                        const float value = values.readFloat();
                        if (!std::isfinite(value)) return;
                        const auto* parameter = parameters.getParameter(parameterIDs[static_cast<size_t>(t)][p]);
                        snapshot->trackParameters[static_cast<size_t>(t)][p]
                            = parameter->convertFrom0to1(parameter->convertTo0to1(value));
                    }
                for (size_t p = 0; p < std::size(globalNames); ++p)
                {
                    const float value = values.readFloat();
                    if (!std::isfinite(value)) return;
                    const auto* parameter = parameters.getParameter(globalNames[p]);
                    snapshot->globalParameters[p] = parameter->convertFrom0to1(parameter->convertTo0to1(value));
                }
                nextBank[static_cast<size_t>(slot)] = std::move(snapshot);
            }
            if (!nextBank[static_cast<size_t>(activeSlot)]) return;
        }
    if (!nextPerform || !nextBank[static_cast<size_t>(activeSlot)])
        nextBank[static_cast<size_t>(activeSlot)] = active;
    if (!legacy)
        if (auto songs = state.getChildWithName("SONGS"); songs.isValid())
        {
            if (songs.getNumChildren() > takt::songSlots) return;
            std::array<bool, takt::songSlots> seen{};
            for (auto item : songs)
            {
                const int slot = item["slot"];
                if (!item.hasType("SONG") || slot < 0 || slot >= takt::songSlots || seen[static_cast<size_t>(slot)])
                    return;
                seen[static_cast<size_t>(slot)] = true;
                takt::Song song;
                song.rowCount = item["rowCount"]; song.endLoop = item.getProperty("endLoop", true);
                song.tempo = item.getProperty("tempo", 0.0);
                if (song.rowCount < 0 || song.rowCount > takt::songRowCapacity
                    || item.getNumChildren() != song.rowCount || !std::isfinite(song.tempo)) return;
                for (int r = 0; r < song.rowCount; ++r)
                {
                    const auto line = item.getChild(r);
                    auto& row = song.rows[static_cast<size_t>(r)];
                    if (!line.hasType("ROW")) return;
                    row.pattern.index = line["pattern"];
                    row.repeats = line.getProperty("repeats", 1); row.length = line.getProperty("length", 0);
                    row.tempo = line.getProperty("tempo", 0.0); row.swing = line.getProperty("swing", -1.0f);
                    row.muteMask = static_cast<std::uint16_t>(static_cast<int>(line.getProperty("muteMask", 0)));
                    if (row.pattern.index < 0 || row.pattern.index >= takt::patternSlots
                        || !nextBank[static_cast<size_t>(row.pattern.index)]
                        || !std::isfinite(row.tempo) || !std::isfinite(row.swing)) return;
                }
                nextSongs.setSong(slot, song);
            }
        }
    if (songSlot >= 0 && (songRow < 0 || songRow >= nextSongs.song(songSlot).rowCount
        || nextSongs.song(songSlot).rows[static_cast<size_t>(songRow)].pattern.index != activeSlot)) return;
    for (const char* type : {"SEQUENCER", "BANK", "SAMPLE_POOL", "SONGS"})
        if (auto child = state.getChildWithName(type); child.isValid()) state.removeChild(child, nullptr);
    commitControlAll();
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        // Reserve the complete transaction before changing any live state.
        if (restoringPattern || arrangementFifo.getFreeSpace() < takt::patternSlots + takt::songSlots + 5) return;
        for (const auto& old : patternBank)
            if (old) retiredPatterns.push_back({old, nextArrangementSerial + 1});
        if (permanentPattern) retiredPatterns.push_back({permanentPattern, nextArrangementSerial + 1});
        for (const auto& sample : samples) retainSampleLocked(sample);
        restoringPattern = true;
        steps = active->steps; lengths = active->lengths; samples = active->samples;
        slicePoints = active->slicePoints; patternLength = active->patternLength;
        patternBank = nextBank; songMetadata = nextSongs; permanentPattern = active;
        currentPatternSlot.store(activeSlot);
        currentSongSlot.store(songSlot); currentSongRow.store(songRow);
        arrangementMode.store(songSlot < 0 ? takt::PatternChain::Mode::Pattern : takt::PatternChain::Mode::Song);
        performKit.store(nextPerform); queuedPatternSlot.store(-1); chainSelection.clear();
        arrangementOriginEnabled.store(localOrigin);
        for (size_t t = 0; t < samples.size(); ++t)
            sampleDurations[t].store(samples[t]->left.size() / samples[t]->sampleRate);
        clipboard.available = undoEdit.available = false; temporaryPattern.reset();
        editRevision.fetch_add(1, std::memory_order_relaxed);
        patternDirty.store(true); samplesDirty.store(true);
        ArrangementCommand command;
        command.kind = ArrangementCommand::Kind::Reset; command.flag = localOrigin;
        enqueueArrangementLocked(command);
        for (int p = 0; p < takt::patternSlots; ++p)
            if (const auto& snapshot = patternBank[static_cast<size_t>(p)])
            {
                command = {};
                command.kind = ArrangementCommand::Kind::UpdatePattern;
                command.slot = p; command.snapshot = snapshot.get();
                enqueueArrangementLocked(command);
            }
        for (int s = 0; s < takt::songSlots; ++s)
        {
            command = {};
            command.kind = ArrangementCommand::Kind::UpdateSong; command.slot = s; command.song = nextSongs.song(s);
            enqueueArrangementLocked(command);
        }
        command = {};
        command.kind = ArrangementCommand::Kind::Select; command.slot = activeSlot;
        enqueueArrangementLocked(command);
        if (songSlot >= 0)
        {
            command = {};
            command.kind = ArrangementCommand::Kind::Song; command.slot = songSlot; command.row = songRow;
            enqueueArrangementLocked(command);
        }
        command = {};
        command.kind = ArrangementCommand::Kind::ReloadKit; command.snapshot = active.get();
        enqueueArrangementLocked(command);
        command = {};
        command.kind = ArrangementCommand::Kind::PerformKit; command.flag = nextPerform;
        enqueueArrangementLocked(command);
    }
    parameters.replaceState(state);
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        mirroredTransitionGeneration.store(audioTransitionGeneration.load());
        mirroredKitGeneration.store(audioKitGeneration.load());
        restoringPattern = false;
    }
    // DAW recall does not mark the project dirty. The audio commands retain
    // all samples/snapshots until their consumption and active use are done.
}

juce::AudioProcessorEditor* TaktAudioProcessor::createEditor() { return new TaktAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TaktAudioProcessor(); }
