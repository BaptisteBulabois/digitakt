#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr const char* trackNames[] = { "gain", "pan", "pitch", "cutoff", "resonance",
    "attack", "decay", "drive", "bitDepth", "start", "end", "delaySend", "reverbSend",
    "reverse", "mute", "loop" };
constexpr const char* globalNames[] = { "play", "hostSync", "tempo", "swing", "master",
    "delayMix", "feedback", "delayBeats", "reverbMix" };
constexpr int maxSampleFrames = 10'000'000;

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
    return layout;
}

TaktAudioProcessor::TaktAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Main", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "TAKT_II", createParameterLayout())
{
    formats.registerBasicFormats();
    lengths.fill(16);
    for (int t = 0; t < takt::numTracks; ++t)
    {
        samples[static_cast<size_t>(t)] = takt::Engine::makeDemoSample(t);
        sampleDurations[static_cast<size_t>(t)].store(samples[static_cast<size_t>(t)]->left.size()
                                                   / samples[static_cast<size_t>(t)]->sampleRate);
        currentSteps[static_cast<size_t>(t)].store(-1);
        for (size_t p = 0; p < std::size(trackNames); ++p)
            trackValues[static_cast<size_t>(t)][p] = parameters.getRawParameterValue(trackParameterID(t, trackNames[p]));
    }
    for (size_t p = 0; p < std::size(globalNames); ++p)
    {
        globalValues[p] = parameters.getRawParameterValue(globalNames[p]);
        appliedGlobals[p] = globalValues[p]->load();
    }
    loadDemoPattern();
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        permanentPattern = capturePatternLocked();
    }
    for (int t = 0; t < takt::numTracks; ++t)
        for (const char* name : trackNames)
            parameters.addParameterListener(trackParameterID(t, name), this);
    for (const char* name : globalNames) parameters.addParameterListener(name, this);
}

TaktAudioProcessor::~TaktAudioProcessor()
{
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
        dryTail = std::max(dryTail, p[15]->load() > .5f ? envelope : std::min(playback, envelope));
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
    if (samplesDirty.exchange(false))
        for (int t = 0; t < takt::numTracks; ++t)
            engine.setSample(t, samples[static_cast<size_t>(t)]);
    if (patternDirty.exchange(false))
    {
        for (int t = 0; t < takt::numTracks; ++t)
        {
            engine.setTrackLength(t, lengths[static_cast<size_t>(t)]);
            for (int s = 0; s < takt::maxSteps; ++s)
                engine.setStep(t, s, steps[static_cast<size_t>(t)][static_cast<size_t>(s)]);
        }
    }
    for (size_t p = 0; p < appliedGlobals.size(); ++p) appliedGlobals[p] = globalValues[p]->load();
    for (int t = 0; t < takt::numTracks; ++t)
    {
        const auto& values = trackValues[static_cast<size_t>(t)];
        takt::TrackParams p;
        p.gain = values[0]->load(); p.pan = values[1]->load(); p.pitch = values[2]->load();
        p.cutoff = values[3]->load(); p.resonance = values[4]->load(); p.attack = values[5]->load();
        p.decay = values[6]->load(); p.drive = values[7]->load(); p.bitDepth = values[8]->load();
        p.start = values[9]->load(); p.end = values[10]->load(); p.delaySend = values[11]->load();
        p.reverbSend = values[12]->load(); p.reverse = values[13]->load() > 0.5f;
        p.mute = values[14]->load() > 0.5f; p.loop = values[15]->load() > 0.5f;
        engine.setTrackParams(t, p);
    }
    takt::FxParams fx;
    fx.delayMix = appliedGlobals[5]; fx.feedback = appliedGlobals[6];
    fx.delayBeats = appliedGlobals[7]; fx.reverbMix = appliedGlobals[8];
    engine.setFx(fx);
    engine.setSwing(appliedGlobals[3]);
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
        if (!message.isNoteOn()) continue;
        const int track = message.getNoteNumber() - 36;
        if (validTrack(track) && eventCount < static_cast<int>(events.size()))
            events[static_cast<size_t>(eventCount++)] = { juce::jlimit(0, count - 1, metadata.samplePosition), track,
                                                        message.getFloatVelocity(), 0 };
    }
    std::sort(events.begin(), events.begin() + eventCount,
              [](const auto& a, const auto& b) { return a.sampleOffset < b.sampleOffset; });
    engine.process(buffer.getWritePointer(0), buffer.getWritePointer(1), count, transport,
                   events.data(), eventCount);
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
    return steps[static_cast<size_t>(track)][static_cast<size_t>(step)];
}

void TaktAudioProcessor::setStep(int track, int step, const takt::Step& value)
{
    if (!validTrack(track) || step < 0 || step >= takt::maxSteps) return;
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
    return lengths[static_cast<size_t>(track)];
}

void TaktAudioProcessor::setTrackLength(int track, int length)
{
    if (!validTrack(track)) return;
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
    snapshot->steps = steps;
    snapshot->lengths = lengths;
    snapshot->samples = samples;
    for (size_t t = 0; t < snapshot->trackParameters.size(); ++t)
        for (size_t p = 0; p < snapshot->trackParameters[t].size(); ++p)
            snapshot->trackParameters[t][p] = trackValues[t][p]->load();
    for (size_t p = 0; p < snapshot->globalParameters.size(); ++p)
        snapshot->globalParameters[p] = globalValues[p]->load();
    return snapshot;
}

void TaktAudioProcessor::temporarySavePattern()
{
    std::lock_guard<std::mutex> lock(controlMutex);
    if (!restoringPattern) temporaryPattern = capturePatternLocked();
}

void TaktAudioProcessor::restorePattern(const PatternSnapshot& snapshot)
{
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (restoringPattern) return;
        restoringPattern = true;
        for (size_t t = 0; t < samples.size(); ++t)
            if (samples[t] != snapshot.samples[t]) retainSampleLocked(samples[t]);
        steps = snapshot.steps;
        lengths = snapshot.lengths;
        samples = snapshot.samples;
        for (size_t t = 0; t < samples.size(); ++t)
            sampleDurations[t].store(samples[t]->left.size() / samples[t]->sampleRate);
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

void TaktAudioProcessor::temporaryReloadPattern()
{
    std::shared_ptr<const PatternSnapshot> snapshot;
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        snapshot = temporaryPattern ? temporaryPattern : permanentPattern;
    }
    if (snapshot) restorePattern(*snapshot);
}

void TaktAudioProcessor::loadDemoPattern()
{
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
    if (count > 0) queuedTriggers[static_cast<size_t>(first)] = { 0, track, juce::jlimit(0.0f, 1.0f, velocity), 0 };
    triggerFifo.finishedWrite(count);
}

bool TaktAudioProcessor::loadSample(int track, const juce::File& file, juce::String& error)
{
    if (!validTrack(track)) { error = "Invalid track"; return false; }
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
    retiredSamples.erase(std::remove_if(retiredSamples.begin(), retiredSamples.end(),
                        [](const auto& sample) { return sample.use_count() == 1; }), retiredSamples.end());
}

void TaktAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    releaseUnusedSamples();
    auto state = parameters.copyState();
    if (auto previous = state.getChildWithName("SEQUENCER"); previous.isValid()) state.removeChild(previous, nullptr);
    juce::ValueTree sequencer("SEQUENCER");
    std::array<std::shared_ptr<const takt::Sample>, takt::numTracks> snapshot;
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        snapshot = samples;
        for (int t = 0; t < takt::numTracks; ++t)
        {
            juce::ValueTree track("TRACK");
            track.setProperty("index", t, nullptr);
            track.setProperty("length", lengths[static_cast<size_t>(t)], nullptr);
            for (int s = 0; s < takt::maxSteps; ++s)
            {
                const auto& value = steps[static_cast<size_t>(t)][static_cast<size_t>(s)];
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
                track.addChild(step, -1, nullptr);
            }
            sequencer.addChild(track, -1, nullptr);
        }
    }
    for (int t = 0; t < takt::numTracks; ++t)
        if (const auto& sample = snapshot[static_cast<size_t>(t)])
        {
            auto track = sequencer.getChild(t);
            track.setProperty("sampleName", juce::String(sample->name), nullptr);
            const auto data = encodeSample(*sample);
            track.setProperty("sampleData", juce::var(data), nullptr);
        }
    state.addChild(sequencer, -1, nullptr);
    destination.reset();
    juce::MemoryOutputStream stream(destination, false);
    stream.writeString("TAKTII_STATE_1");
    state.writeToStream(stream);
}

void TaktAudioProcessor::setStateInformation(const void* data, int size)
{
    if (size <= 0) return;
    releaseUnusedSamples();
    juce::MemoryInputStream input(data, static_cast<size_t>(size), false);
    if (input.readString() != "TAKTII_STATE_1") return;
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
    auto sequencer = state.getChildWithName("SEQUENCER");
    if (!sequencer.isValid() || sequencer.getNumChildren() != takt::numTracks) return;
    decltype(steps) nextSteps{};
    decltype(lengths) nextLengths{};
    decltype(samples) nextSamples{};
    for (int t = 0; t < takt::numTracks; ++t)
    {
        auto track = sequencer.getChild(t);
        if (static_cast<int>(track["index"]) != t || track.getNumChildren() != takt::maxSteps) return;
        nextLengths[static_cast<size_t>(t)] = juce::jlimit(1, takt::maxSteps, static_cast<int>(track["length"]));
        auto sampleData = track["sampleData"];
        if (auto* blob = sampleData.getBinaryData()) nextSamples[static_cast<size_t>(t)] = decodeSample(*blob, track["sampleName"].toString());
        if (!nextSamples[static_cast<size_t>(t)]) return;
        for (int s = 0; s < takt::maxSteps; ++s)
        {
            auto child = track.getChild(s);
            auto& value = nextSteps[static_cast<size_t>(t)][static_cast<size_t>(s)];
            value.enabled = child["enabled"]; value.velocity = child["velocity"];
            value.probability = child["probability"]; value.pitch = child["pitch"];
            value.cutoff = child["cutoff"]; value.lockPitch = child["lockPitch"]; value.lockCutoff = child["lockCutoff"];
            value.conditionEvery = child["conditionEvery"]; value.conditionOffset = child["conditionOffset"];
            value.retrigs = child["retrigs"]; value.microtiming = child["microtiming"];
            value = sanitizedStep(value);
        }
    }
    state.removeChild(sequencer, nullptr);
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (restoringPattern) return;
        for (auto& sample : samples) retainSampleLocked(sample);
        restoringPattern = true;
        steps = nextSteps; lengths = nextLengths; samples = std::move(nextSamples);
        for (int t = 0; t < takt::numTracks; ++t)
            sampleDurations[static_cast<size_t>(t)].store(samples[static_cast<size_t>(t)]->left.size()
                                                       / samples[static_cast<size_t>(t)]->sampleRate);
        clipboard.available = false; undoEdit.available = false;
        temporaryPattern.reset();
        editRevision.fetch_add(1, std::memory_order_relaxed);
        patternDirty.store(true); samplesDirty.store(true);
    }
    parameters.replaceState(state);
    {
        std::lock_guard<std::mutex> lock(controlMutex);
        permanentPattern = capturePatternLocked();
        restoringPattern = false;
    }
    // This is DAW/preset recall, not a user edit. Do not mark a freshly loaded
    // project dirty; the editor refreshes the restored state from its timer.
}

juce::AudioProcessorEditor* TaktAudioProcessor::createEditor() { return new TaktAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TaktAudioProcessor(); }
