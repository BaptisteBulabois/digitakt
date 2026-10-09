#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr double testRate = 48000.0;
constexpr int testBlock = 512;

void check(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void passed(const char* name) { std::cout << "PASS: " << name << std::endl; }

// APVTS uses timers. A message manager supports their lifetime without opening
// an editor, connecting to an audio device or requiring a Linux display.
class HeadlessMessages
{
public:
    HeadlessMessages() { juce::MessageManager::getInstance(); }
    ~HeadlessMessages()
    {
        juce::DeletedAtShutdown::deleteAll();
        juce::MessageManager::deleteInstance();
    }
};

class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : directory(juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("takt-integration", "", false))
    {
        check(directory.createDirectory().wasOk(), "Cannot create temporary sample directory");
    }
    ~TemporaryDirectory() { directory.deleteRecursively(); }
    juce::File directory;
};

std::unique_ptr<juce::AudioFormatWriter> wavWriter(const juce::File& file,
                                                 double rate, int channels)
{
    check(file.getParentDirectory().createDirectory().wasOk(), "Cannot create WAV directory");
    check(!file.exists() || file.deleteFile(), "Cannot replace generated WAV file");
    auto stream = file.createOutputStream();
    check(stream != nullptr, "Cannot open generated WAV file");
    juce::WavAudioFormat format;
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        format.createWriterFor(stream.get(), rate, static_cast<unsigned int>(channels),
                               24, {}, 0));
    check(writer != nullptr, "Cannot create WAV writer");
    stream.release(); // AudioFormatWriter now owns the stream.
    return writer;
}

juce::File makeWave(const juce::File& directory, const juce::String& name,
                    int channels, int frames = 2205, double rate = 22050.0)
{
    const auto file = directory.getChildFile(name + ".wav");
    juce::AudioBuffer<float> data(channels, frames);
    for (int channel = 0; channel < channels; ++channel)
        for (int frame = 0; frame < frames; ++frame)
            data.setSample(channel, frame,
                channel == 0 ? 0.3f + 0.1f * std::sin(0.071f * static_cast<float>(frame))
                             : -0.2f + 0.08f * std::cos(0.093f * static_cast<float>(frame)));
    auto writer = wavWriter(file, rate, channels);
    check(writer->writeFromAudioSampleBuffer(data, 0, frames), "Cannot write WAV fixture");
    return file;
}

void configureDry(TaktAudioProcessor& processor)
{
    processor.setParameter("master", 1.0f);
    processor.setParameter("play", 0.0f);
    processor.setParameter("hostSync", 0.0f);
    processor.setParameter("delayMix", 0.0f);
    processor.setParameter("reverbMix", 0.0f);
    for (int track = 0; track < takt::numTracks; ++track)
    {
        auto set = [&](const char* name, float value)
        { processor.setParameter(TaktAudioProcessor::trackParameterID(track, name), value); };
        set("delaySend", 0.0f); set("reverbSend", 0.0f);
        set("attack", 0.0001f); set("decay", 10.0f);
        set("cutoff", 20000.0f); set("resonance", 0.0f);
    }
}

juce::AudioBuffer<float> process(juce::AudioProcessor& processor, int frames,
                               juce::MidiBuffer midi = {})
{
    juce::AudioBuffer<float> output(2, frames);
    output.clear();
    processor.processBlock(output, midi);
    for (int channel = 0; channel < output.getNumChannels(); ++channel)
        for (int frame = 0; frame < frames; ++frame)
            check(std::isfinite(output.getSample(channel, frame)), "Audio contains NaN or infinity");
    return output;
}

float magnitude(const juce::AudioBuffer<float>& audio, int begin = 0, int frames = -1)
{
    if (frames < 0) frames = audio.getNumSamples() - begin;
    if (frames <= 0) return 0.0f;
    return std::max(audio.getMagnitude(0, begin, frames), audio.getMagnitude(1, begin, frames));
}

juce::MidiBuffer note(int number, int offset = 0, juce::uint8 velocity = 110)
{
    juce::MidiBuffer events;
    events.addEvent(juce::MidiMessage::noteOn(1, number, velocity), offset);
    return events;
}

void checkEqualAudio(const juce::AudioBuffer<float>& actual,
                     const juce::AudioBuffer<float>& expected, float tolerance,
                     const std::string& context)
{
    check(actual.getNumSamples() == expected.getNumSamples(), context + ": duration differs");
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < actual.getNumSamples(); ++frame)
            check(std::abs(actual.getSample(channel, frame) - expected.getSample(channel, frame))
                      <= tolerance, context + ": rendered samples differ");
}

void testMidiAndBuses()
{
    TaktAudioProcessor processor;
    check(processor.getTotalNumInputChannels() == 0 && processor.getTotalNumOutputChannels() == 2,
          "Instrument must expose a stereo output without an audio input");
    auto layout = processor.getBusesLayout();
    check(processor.isBusesLayoutSupported(layout), "Default stereo bus layout was rejected");
    layout.outputBuses.set(0, juce::AudioChannelSet::mono());
    check(!processor.isBusesLayoutSupported(layout), "Unsupported mono output was accepted");
    configureDry(processor);
    for (const int midiNote : { 36, 51 })
    {
        processor.prepareToPlay(testRate, testBlock);
        const int offset = midiNote == 36 ? 37 : 91;
        const auto audio = process(processor, testBlock, note(midiNote, offset));
        check(magnitude(audio, 0, offset) == 0.0f, "MIDI note started before its sample offset");
        check(magnitude(audio, offset) > 0.001f, "First or last audio track did not respond to MIDI");
    }
    processor.prepareToPlay(testRate, testBlock);
    juce::MidiBuffer ignored;
    ignored.addEvent(juce::MidiMessage::noteOn(1, 35, static_cast<juce::uint8>(127)), 0);
    ignored.addEvent(juce::MidiMessage::noteOn(1, 52, static_cast<juce::uint8>(127)), 31);
    ignored.addEvent(juce::MidiMessage::noteOff(1, 36), 47);
    ignored.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 63);
    ignored.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 95);
    check(magnitude(process(processor, testBlock, ignored)) == 0.0f,
          "An out-of-range note or controller triggered an audio track");

    processor.prepareToPlay(testRate, testBlock);
    auto first = process(processor, testBlock, note(36));
    processor.prepareToPlay(testRate, testBlock);
    auto last = process(processor, testBlock, note(51));
    float difference = 0.0f;
    for (int frame = 0; frame < testBlock; ++frame)
        difference += std::abs(first.getSample(0, frame) - last.getSample(0, frame));
    check(difference > 0.1f, "Distinct MIDI notes addressed the same sample");
    processor.prepareToPlay(testRate, testBlock);
    processor.setParameter(TaktAudioProcessor::trackParameterID(0, "mute"), 1.0f);
    auto simultaneous = note(36);
    simultaneous.addEvent(juce::MidiMessage::noteOn(1, 51, static_cast<juce::uint8>(110)), 0);
    checkEqualAudio(process(processor, testBlock, simultaneous), last, 1.0e-6f,
                    "Muting one track affected the independent last track");
    passed("stereo buses, MIDI notes 36–51, sample offsets and independent tracks");
}

void testSampleImportAndResampling(const juce::File& mono, const juce::File& stereo,
                                  const juce::File& directory)
{
    TaktAudioProcessor processor;
    configureDry(processor);
    for (int track = 0; track < takt::numTracks; ++track)
    {
        juce::String error;
        const auto& file = (track & 1) == 0 ? mono : stereo;
        check(processor.loadSample(track, file, error), "Sample import failed: " + error.toStdString());
        const auto sample = processor.getSample(track);
        check(sample && sample->sampleRate == 22050.0 && sample->left.size() == 2205,
              "Sample import changed source rate or frame count");
        check(sample->right.size() == sample->left.size(), "Sample import omitted a channel");
        check(processor.getSampleName(track) == file.getFileNameWithoutExtension(),
              "Sample name did not follow the imported file");
        check((sample->left == sample->right) == ((track & 1) == 0),
              "Mono duplication or distinct stereo channel import failed");
        processor.prepareToPlay(testRate, testBlock);
        check(magnitude(process(processor, testBlock, note(36 + track))) > 0.01f,
              "An imported sample could not be played on one of the 16 tracks");
    }
    const auto previous = processor.getSample(0);
    juce::String error;
    check(!processor.loadSample(0, directory.getChildFile("missing.wav"), error) && error.isNotEmpty(),
          "Missing sample file was accepted without an error");
    const auto invalid = directory.getChildFile("invalid.wav");
    check(invalid.replaceWithText("This is not audio."), "Cannot write invalid sample fixture");
    check(!processor.loadSample(0, invalid, error), "Invalid audio file was accepted");
    const auto tooShort = makeWave(directory, "one-frame", 1, 1);
    check(!processor.loadSample(0, tooShort, error), "A one-frame sample was accepted");
    check(!processor.loadSample(-1, mono, error) && !processor.loadSample(16, mono, error),
          "Invalid track index was accepted");
    check(processor.getSample(0) == previous, "A rejected import replaced an existing sample");

    for (const double outputRate : { 44100.0, 48000.0, 96000.0 })
    {
        processor.prepareToPlay(outputRate, testBlock);
        const int sourceDuration = static_cast<int>(std::ceil(outputRate * 0.1));
        const auto audio = process(processor, sourceDuration + 256, note(36));
        check(magnitude(audio, sourceDuration - 150, 50) > 0.001f,
              "Resampling ended an imported sample too early");
        check(magnitude(audio, sourceDuration + 2) == 0.0f,
              "Resampling did not preserve the imported sample's duration");
    }
    processor.setParameter(TaktAudioProcessor::trackParameterID(0, "pitch"), 12.0f);
    processor.prepareToPlay(testRate, testBlock);
    const auto octave = process(processor, 5000, note(36));
    check(magnitude(octave, 2000, 100) > 0.001f && magnitude(octave, 2402) == 0.0f,
          "An octave pitch shift did not halve the sample duration");
    passed("WAV import on all 16 tracks, mono/stereo, rejected files and sample-rate conversion");
}

void testParameters(const juce::File& mono)
{
    TaktAudioProcessor processor;
    configureDry(processor);
    juce::String error;
    check(processor.loadSample(0, mono, error), error.toStdString());
    auto set = [&](const char* id, float value)
    { processor.setParameter(TaktAudioProcessor::trackParameterID(0, id), value); };
    auto render = [&]
    {
        processor.prepareToPlay(testRate, testBlock);
        return process(processor, testBlock, note(36));
    };
    set("pan", -1.0f);
    auto left = render();
    check(left.getMagnitude(0, 0, testBlock) > 0.01f && left.getMagnitude(1, 0, testBlock) == 0,
          "Hard-left pan did not isolate the left output");
    set("pan", 1.0f);
    auto right = render();
    check(right.getMagnitude(1, 0, testBlock) > 0.01f && right.getMagnitude(0, 0, testBlock) == 0,
          "Hard-right pan did not isolate the right output");
    set("pan", 0.0f);
    set("mute", 1.0f);
    check(magnitude(render()) == 0, "Mute parameter did not silence the track");
    set("mute", 0.0f);
    set("gain", 0.0f);
    check(magnitude(render()) == 0, "Zero track gain did not silence the track");
    set("gain", 0.8f);
    check(magnitude(render()) > 0.01f, "Track did not resume after mute/gain changes");
    processor.setParameter("master", 0.0f);
    check(magnitude(render()) == 0, "Zero master gain did not silence the instrument");
    check(std::abs(processor.parameterValue(TaktAudioProcessor::trackParameterID(0, "gain")) - 0.8f)
              < 1.0e-5f, "Parameter access did not preserve a changed gain");
    passed("automatable mute, track/master gain and stereo pan");
}

void compareSteps(const takt::Step& a, const takt::Step& b)
{
    check(a.enabled == b.enabled && a.velocity == b.velocity && a.probability == b.probability
              && a.pitch == b.pitch && a.cutoff == b.cutoff && a.lockPitch == b.lockPitch
              && a.lockCutoff == b.lockCutoff && a.conditionEvery == b.conditionEvery
              && a.conditionOffset == b.conditionOffset && a.retrigs == b.retrigs
              && a.microtiming == b.microtiming, "A sequencer step or parameter lock changed in state recall");
}

using PatternSteps = std::array<std::array<takt::Step, takt::maxSteps>, takt::numTracks>;

PatternSteps patternOf(const TaktAudioProcessor& processor)
{
    PatternSteps result{};
    for (int track = 0; track < takt::numTracks; ++track)
        for (int step = 0; step < takt::maxSteps; ++step)
            result[static_cast<size_t>(track)][static_cast<size_t>(step)] = processor.getStep(track, step);
    return result;
}

void checkPattern(const TaktAudioProcessor& processor, const PatternSteps& expected)
{
    for (int track = 0; track < takt::numTracks; ++track)
        for (int step = 0; step < takt::maxSteps; ++step)
            compareSteps(processor.getStep(track, step), expected[static_cast<size_t>(track)][static_cast<size_t>(step)]);
}

takt::Step musicalStep(int seed)
{
    takt::Step step;
    step.enabled = true;
    step.velocity = 0.25f + 0.125f * static_cast<float>(seed % 5);
    step.probability = 1.0f;
    step.pitch = static_cast<float>(seed % 25 - 12);
    step.cutoff = static_cast<float>(1000 + 250 * seed);
    step.lockPitch = true;
    step.lockCutoff = true;
    step.conditionEvery = 1 + seed % 3;
    step.conditionOffset = 0;
    step.retrigs = 1 + seed % 4;
    step.microtiming = 0.03125f * static_cast<float>(seed % 5 - 2);
    return step;
}

juce::MemoryBlock stateOf(TaktAudioProcessor& processor)
{
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    return state;
}

juce::ValueTree readStateTree(const juce::MemoryBlock& state)
{
    juce::MemoryInputStream stream(state, false);
    check(stream.readString() == "TAKTII_STATE_1", "Unexpected state signature");
    auto tree = juce::ValueTree::readFromStream(stream);
    check(tree.isValid(), "Cannot decode test state tree");
    return tree;
}

juce::MemoryBlock writeStateTree(const juce::ValueTree& tree)
{
    juce::MemoryBlock state;
    juce::MemoryOutputStream stream(state, false);
    stream.writeString("TAKTII_STATE_1");
    tree.writeToStream(stream);
    return state;
}

void checkSafeStep(const takt::Step& step)
{
    check(std::isfinite(step.velocity) && step.velocity >= 0 && step.velocity <= 1
              && std::isfinite(step.probability) && step.probability >= 0 && step.probability <= 1
              && std::isfinite(step.pitch) && step.pitch >= -48 && step.pitch <= 48
              && std::isfinite(step.cutoff) && step.cutoff >= 20 && step.cutoff <= 20000
              && std::isfinite(step.microtiming) && std::abs(step.microtiming) <= 0.49f
              && step.conditionEvery >= 1 && step.conditionEvery <= 64
              && step.conditionOffset >= 0 && step.conditionOffset < step.conditionEvery
              && step.retrigs >= 1 && step.retrigs <= 8,
          "An invalid sequencer value reached the backing state or editor");
}

void testState(const juce::File& mono, const juce::File& stereo)
{
    TaktAudioProcessor source, restored;
    configureDry(source);
    source.setParameter("tempo", 143.0f);
    source.setParameter("swing", 0.25f);
    for (int track = 0; track < takt::numTracks; ++track)
    {
        juce::String error;
        check(source.loadSample(track, (track & 1) == 0 ? mono : stereo, error), error.toStdString());
        source.setTrackLength(track, 128 - 3 * track);
        source.setParameter(TaktAudioProcessor::trackParameterID(track, "gain"), 0.2f + 0.025f * track);
        source.setParameter(TaktAudioProcessor::trackParameterID(track, "pan"), (track - 8) / 8.0f);
        for (int index = 0; index < takt::maxSteps; ++index)
        {
            takt::Step step;
            step.enabled = index % 3 == 0;
            step.velocity = 0.25f + 0.125f * (index % 5);
            step.probability = (index & 1) == 0 ? 1.0f : 0.5f;
            step.pitch = static_cast<float>(track - 8 + index % 12);
            step.cutoff = static_cast<float>(300 + 45 * index);
            step.lockPitch = index % 4 == 0;
            step.lockCutoff = index % 5 == 0;
            step.conditionEvery = 1 + index % 4;
            step.conditionOffset = index % step.conditionEvery;
            step.retrigs = 1 + index % 8;
            step.microtiming = 0.03125f * static_cast<float>(index % 7 - 3);
            source.setStep(track, index, step);
        }
    }
    const auto state = stateOf(source);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(restored.parameterValue("tempo") == source.parameterValue("tempo")
              && restored.parameterValue("swing") == source.parameterValue("swing"),
          "Global parameters did not survive state recall");
    for (int track = 0; track < takt::numTracks; ++track)
    {
        check(restored.getTrackLength(track) == source.getTrackLength(track), "Track length changed on recall");
        const auto originalSample = source.getSample(track), recalledSample = restored.getSample(track);
        check(originalSample && recalledSample && originalSample->name == recalledSample->name
                  && originalSample->sampleRate == recalledSample->sampleRate
                  && originalSample->left == recalledSample->left && originalSample->right == recalledSample->right,
              "Embedded source sample was not restored exactly");
        for (const char* name : { "gain", "pan", "pitch", "cutoff", "resonance", "attack", "decay",
                                 "drive", "bitDepth", "start", "end", "delaySend", "reverbSend",
                                 "reverse", "mute", "loop" })
        {
            const auto id = TaktAudioProcessor::trackParameterID(track, name);
            check(std::abs(restored.parameterValue(id) - source.parameterValue(id)) < 1.0e-4f,
                  "A track parameter changed on recall");
        }
        for (int index = 0; index < takt::maxSteps; ++index)
            compareSteps(source.getStep(track, index), restored.getStep(track, index));
    }
    source.prepareToPlay(testRate, testBlock);
    restored.prepareToPlay(testRate, testBlock);
    auto midi = note(36, 31);
    midi.addEvent(juce::MidiMessage::noteOn(1, 51, static_cast<juce::uint8>(90)), 177);
    checkEqualAudio(process(restored, testBlock, midi), process(source, testBlock, midi),
                    1.0e-6f, "Recalled embedded samples and controls");

    const auto previous = stateOf(restored);
    const char wrongHeader[] = "invalid-state";
    restored.setStateInformation(wrongHeader, sizeof(wrongHeader));
    check(stateOf(restored) == previous, "An invalid state header changed the instrument");
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize() / 2));
    check(stateOf(restored) == previous, "Truncated state partially changed the instrument");
    juce::MemoryInputStream input(state, false);
    check(input.readString() == "TAKTII_STATE_1", "Unexpected state signature");
    auto damagedTree = juce::ValueTree::readFromStream(input);
    juce::MemoryBlock badAudio("broken", 6);
    damagedTree.getChildWithName("SEQUENCER").getChild(0)
        .setProperty("sampleData", juce::var(badAudio), nullptr);
    juce::MemoryBlock damaged;
    juce::MemoryOutputStream stream(damaged, false);
    stream.writeString("TAKTII_STATE_1");
    damagedTree.writeToStream(stream);
    restored.setStateInformation(damaged.getData(), static_cast<int>(damaged.getSize()));
    check(stateOf(restored) == previous, "A corrupt embedded sample partially changed the instrument");
    passed("state recall: all 16 samples, 128 steps, locks, lengths and parameters; corrupt-state rejection");

    auto invalidParameterTree = readStateTree(previous);
    auto master = invalidParameterTree.getChildWithProperty("id", "master");
    check(master.isValid(), "Master PARAM is missing from serialised state");
    master.setProperty("value", std::numeric_limits<double>::quiet_NaN(), nullptr);
    const auto invalidParameterState = writeStateTree(invalidParameterTree);
    restored.setStateInformation(invalidParameterState.getData(),
                                 static_cast<int>(invalidParameterState.getSize()));
    check(stateOf(restored) == previous && std::isfinite(restored.parameterValue("master")),
          "A non-finite saved parameter changed the instrument");
    restored.prepareToPlay(testRate, testBlock);
    check(magnitude(process(restored, testBlock, note(36))) > 0.001f,
          "Rejected non-finite state disrupted subsequent MIDI rendering");

    auto extremeTree = readStateTree(previous);
    auto track = extremeTree.getChildWithName("SEQUENCER").getChild(0);
    track.setProperty("length", std::numeric_limits<int>::max(), nullptr);
    auto extremeStep = track.getChild(0);
    extremeStep.setProperty("conditionEvery", std::numeric_limits<int>::min(), nullptr);
    extremeStep.setProperty("conditionOffset", std::numeric_limits<int>::max(), nullptr);
    extremeStep.setProperty("retrigs", std::numeric_limits<int>::max(), nullptr);
    extremeStep.setProperty("velocity", std::numeric_limits<double>::infinity(), nullptr);
    extremeStep.setProperty("probability", -100.0, nullptr);
    extremeStep.setProperty("pitch", std::numeric_limits<double>::quiet_NaN(), nullptr);
    extremeStep.setProperty("cutoff", std::numeric_limits<double>::quiet_NaN(), nullptr);
    extremeStep.setProperty("microtiming", std::numeric_limits<double>::infinity(), nullptr);
    extremeTree.getChildWithProperty("id", "master").setProperty("value", 1000000.0, nullptr);
    const auto extremeState = writeStateTree(extremeTree);
    restored.setStateInformation(extremeState.getData(), static_cast<int>(extremeState.getSize()));
    checkSafeStep(restored.getStep(0, 0));
    check(restored.getStep(0, 0).conditionEvery == 1
              && restored.getStep(0, 0).conditionOffset == 0
              && restored.getTrackLength(0) == takt::maxSteps,
          "Extreme saved conditions or track length were not clamped safely");
    check(restored.parameterValue("master") >= 0 && restored.parameterValue("master") <= 1,
          "Out-of-range saved master parameter escaped its defined range");
    restored.prepareToPlay(testRate, testBlock);
    check(magnitude(process(restored, testBlock, note(36))) > 0.001f,
          "Sanitised state could not render finite audio");

    takt::Step direct;
    direct.enabled = true;
    direct.conditionEvery = std::numeric_limits<int>::min();
    direct.conditionOffset = std::numeric_limits<int>::max();
    direct.retrigs = std::numeric_limits<int>::max();
    direct.velocity = std::numeric_limits<float>::infinity();
    direct.probability = std::numeric_limits<float>::quiet_NaN();
    direct.pitch = std::numeric_limits<float>::infinity();
    direct.cutoff = std::numeric_limits<float>::quiet_NaN();
    direct.microtiming = std::numeric_limits<float>::infinity();
    restored.setStep(3, 5, direct);
    checkSafeStep(restored.getStep(3, 5));
    const float validMaster = restored.parameterValue("master");
    restored.setParameter("master", std::numeric_limits<float>::quiet_NaN());
    check(restored.parameterValue("master") == validMaster,
          "Direct non-finite parameter assignment was accepted");
    passed("malformed state: NaN PARAM rejection and safe extreme step/condition sanitisation");
}

class TestPlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override { return position; }
    void set(double ppq, double bpm, bool playing)
    {
        position.setPpqPosition(ppq);
        position.setBpm(bpm);
        position.setIsPlaying(playing);
    }
    PositionInfo position;
};

juce::AudioBuffer<float> renderWithHost(TaktAudioProcessor& processor, TestPlayHead& host,
                                      const std::vector<int>& sizes, int total, double bpm)
{
    juce::AudioBuffer<float> audio(2, total);
    int cursor = 0;
    size_t block = 0;
    while (cursor < total)
    {
        const int count = std::min(sizes[block++ % sizes.size()], total - cursor);
        host.set(cursor * bpm / (60.0 * testRate), bpm, true);
        const auto next = process(processor, count);
        for (int channel = 0; channel < 2; ++channel)
            audio.copyFrom(channel, cursor, next, channel, 0, count);
        cursor += count;
    }
    return audio;
}

class HostStateListener final : public juce::AudioProcessorListener
{
public:
    explicit HostStateListener(TaktAudioProcessor& owner) : processor(owner) { processor.addListener(this); }
    ~HostStateListener() override { processor.removeListener(this); }
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& change) override
    {
        if (!change.nonParameterStateChanged) return;
        ++notifications;
        // Hosts may synchronously inspect/save state in this callback. This
        // catches an edit notifying its host while retaining controlMutex.
        observedStep = processor.getStep(0, 0);
        observedState = stateOf(processor);
    }
    int notifications = 0;
    takt::Step observedStep;
    juce::MemoryBlock observedState;
private:
    TaktAudioProcessor& processor;
};

void testClipboard(const juce::File& mono, const juce::File& stereo)
{
    using Scope = TaktAudioProcessor::EditScope;
    using Result = TaktAudioProcessor::EditResult;
    TaktAudioProcessor processor;
    configureDry(processor);
    for (int track = 0; track < takt::numTracks; ++track)
    {
        processor.setTrackLength(track, track == 0 ? 128 : 16 + track);
        for (int index = 0; index < takt::maxSteps; ++index)
        {
            auto step = musicalStep((track * 7 + index) % 48);
            step.enabled = index % 3 == 0;
            processor.setStep(track, index, step);
        }
    }
    check(processor.pasteSelection(Scope::Step, 1, 23) == Result::EmptyClipboard,
          "Pasting with an empty clipboard did not report its missing content");
    const auto initial = patternOf(processor);
    check(processor.copySelection(Scope::Step, 0, 5) == Result::Applied, "Step copy failed");
    checkPattern(processor, initial);
    check(processor.pasteSelection(Scope::Step, 1, 23) == Result::Applied, "Step paste failed");
    auto copied = initial;
    copied[1][23] = initial[0][5];
    checkPattern(processor, copied);
    check(processor.canUndoEdit(), "Step paste did not provide an undo");
    check(processor.pasteSelection(Scope::Step, 1, 23) == Result::Undone,
          "Repeating step paste did not undo the operation");
    checkPattern(processor, initial);
    check(processor.pasteSelection(Scope::Step, 1, 23) == Result::Applied,
          "Repeating step paste after undo did not apply it again");
    checkPattern(processor, copied);
    check(processor.undoLastEdit() == Result::Undone, "Explicit paste undo failed");
    checkPattern(processor, initial);

    check(processor.copySelection(Scope::Page, 0, 0, 2) == Result::Applied, "Page copy failed");
    check(processor.pasteSelection(Scope::Step, 1, 23) == Result::ScopeMismatch,
          "Copying a page did not replace the previous step clipboard type");
    checkPattern(processor, initial);
    check(processor.pasteSelection(Scope::Page, 1, 0, 6) == Result::Applied, "Page paste failed");
    auto pageCopy = initial;
    for (int index = 0; index < 16; ++index) pageCopy[1][96 + index] = initial[0][32 + index];
    checkPattern(processor, pageCopy);
    check(processor.getTrackLength(1) == 17, "Page paste changed the target track length");
    check(processor.pasteSelection(Scope::Page, 1, 0, 6) == Result::Undone, "Repeated page paste did not undo");
    checkPattern(processor, initial);

    check(processor.clearSelection(Scope::Page, 1, 0, 6) == Result::Applied, "Page clear failed");
    auto pageClear = initial;
    for (int index = 0; index < 16; ++index) pageClear[1][96 + index] = {};
    checkPattern(processor, pageClear);
    check(processor.getTrackLength(1) == 17, "Page clear changed track length");
    check(processor.clearSelection(Scope::Page, 1, 0, 6) == Result::Undone, "Repeated page clear did not undo");
    checkPattern(processor, initial);
    check(processor.clearSelection(Scope::Page, 1, 0, 6) == Result::Applied,
          "Repeated page clear after undo did not apply again");
    checkPattern(processor, pageClear);
    check(processor.undoLastEdit() == Result::Undone, "Explicit page clear undo failed");
    checkPattern(processor, initial);

    juce::String error;
    check(processor.loadSample(0, mono, error) && processor.loadSample(2, stereo, error), error.toStdString());
    processor.setParameter(TaktAudioProcessor::trackParameterID(2, "pitch"), -7.0f);
    const auto destinationSample = processor.getSample(2);
    check(processor.copySelection(Scope::Track, 0) == Result::Applied, "Track copy failed");
    check(processor.pasteSelection(Scope::Page, 1, 0, 6) == Result::ScopeMismatch,
          "Copying a track did not replace the previous page clipboard type");
    check(processor.pasteSelection(Scope::Track, 2) == Result::Applied, "Track paste failed");
    auto trackCopy = initial;
    trackCopy[2] = initial[0];
    checkPattern(processor, trackCopy);
    check(processor.getTrackLength(2) == 128, "Track paste omitted the 128-step length");
    check(processor.getSample(2) == destinationSample
              && processor.parameterValue(TaktAudioProcessor::trackParameterID(2, "pitch")) == -7.0f,
          "Copying a track sequence replaced its target sample or preset parameters");
    check(processor.pasteSelection(Scope::Track, 2) == Result::Undone, "Repeated track paste did not undo");
    checkPattern(processor, initial);
    check(processor.getTrackLength(2) == 18, "Track paste undo did not restore its previous length");
    check(processor.clearSelection(Scope::Track, 0) == Result::Applied, "Track clear failed");
    auto trackClear = initial;
    trackClear[0].fill(takt::Step{});
    checkPattern(processor, trackClear);
    check(processor.getTrackLength(0) == 128, "Track clear changed its sequence length");
    check(processor.clearSelection(Scope::Track, 0) == Result::Undone, "Repeated track clear did not undo");
    checkPattern(processor, initial);

    check(processor.copySelection(Scope::Step, 0, 5) == Result::Applied
              && processor.pasteSelection(Scope::Step, 1, 23) == Result::Applied,
          "Cannot establish a paste for invalidated-undo regression");
    processor.setParameter(TaktAudioProcessor::trackParameterID(1, "gain"), 0.33f);
    check(!processor.canUndoEdit() && processor.undoLastEdit() == Result::NothingToUndo,
          "Editing a preset retained a stale undo that could overwrite subsequent work");
    auto checkInvalidated = [&](const std::function<void()>& edit)
    {
        check(processor.copySelection(Scope::Step, 0, 5) == Result::Applied
                  && processor.pasteSelection(Scope::Step, 1, 23) == Result::Applied,
              "Cannot establish an undo before a subsequent edit");
        edit();
        check(!processor.canUndoEdit() && processor.undoLastEdit() == Result::NothingToUndo,
              "A later step, length or sample edit left a stale paste undo");
    };
    checkInvalidated([&] { processor.setStep(4, 127, musicalStep(19)); });
    checkInvalidated([&] { processor.setTrackLength(4, 37); });
    checkInvalidated([&] { check(processor.loadSample(4, mono, error), error.toStdString()); });
    const auto beforeInvalid = patternOf(processor);
    check(processor.copySelection(Scope::Step, -1, 5) == Result::InvalidSelection
              && processor.pasteSelection(Scope::Page, 0, 0, 8) == Result::InvalidSelection,
          "Invalid clipboard selection was accepted");
    checkPattern(processor, beforeInvalid);

    // Check the audible meaning of a copied pitch lock and lock-only CLEAR,
    // rather than checking the data transfer alone.
    TaktAudioProcessor musical;
    TestPlayHead playHead;
    configureDry(musical);
    for (int track = 0; track < takt::numTracks; ++track) musical.clearTrack(track);
    check(musical.loadSample(0, mono, error), error.toStdString());
    musical.setTrackLength(0, 128);
    takt::Step octave;
    octave.enabled = true;
    octave.velocity = 0.75f;
    octave.pitch = 12;
    octave.cutoff = 20000;
    octave.lockPitch = octave.lockCutoff = true;
    musical.setStep(0, 3, octave);
    check(musical.copySelection(Scope::Step, 0, 3) == Result::Applied
              && musical.pasteSelection(Scope::Step, 0, 99) == Result::Applied,
          "Cannot copy an audible locked trig to the last sequence pages");
    musical.setParameter("hostSync", 1.0f);
    musical.setPlayHead(&playHead);
    playHead.set(99 * 0.25, 120, true);
    auto render = [&]
    {
        musical.prepareToPlay(testRate, testBlock);
        return process(musical, 6000);
    };
    const auto pitched = render();
    check(magnitude(pitched, 1500, 100) > 0.001f && magnitude(pitched, 2402) == 0,
          "Copied pitch lock did not transpose the played sample by an octave");
    check(musical.parameterValue(TaktAudioProcessor::trackParameterID(0, "pitch")) == 0,
          "Pasting a pitch lock modified the base preset");
    check(musical.clearSelection(Scope::Step, 0, 99) == Result::Applied, "Step lock clear failed");
    auto cleared = octave;
    cleared.lockPitch = cleared.lockCutoff = false;
    cleared.pitch = 0; // CLEAR removes the legacy additive pitch payload too.
    compareSteps(musical.getStep(0, 99), cleared);
    check(magnitude(render(), 3500, 100) > 0.001f,
          "Clearing pitch locks removed the note or failed to return to the base pitch");
    check(musical.clearSelection(Scope::Step, 0, 99) == Result::Undone,
          "Repeated step lock clear did not restore the locks");
    checkEqualAudio(render(), pitched, 1.0e-6f, "Undo of lock-only clear");
    musical.setPlayHead(nullptr);
    passed("typed clipboard: musical locks, isolated pages, 128-step tracks, replacement and repeated paste/clear undo");

    HostStateListener listener(musical);
    auto requiresNotification = [&](const std::function<void()>& edit)
    {
        const int previous = listener.notifications;
        edit();
        check(listener.notifications > previous && listener.observedState.getSize() > 0,
              "A musical edit did not notify its host that project state must be saved");
    };
    check(musical.copySelection(Scope::Step, 0, 3) == Result::Applied, "Cannot copy for host-state callback test");
    requiresNotification([&] { check(musical.pasteSelection(Scope::Step, 0, 0) == Result::Applied, "Host paste failed"); });
    compareSteps(listener.observedStep, octave);
    requiresNotification([&] { check(musical.undoLastEdit() == Result::Undone, "Host undo failed"); });
    requiresNotification([&] { check(musical.clearSelection(Scope::Step, 0, 3) == Result::Applied, "Host clear failed"); });
    requiresNotification([&] { check(musical.clearSelection(Scope::Step, 0, 3) == Result::Undone, "Host clear undo failed"); });
    musical.temporarySavePattern();
    musical.setStep(0, 3, {});
    requiresNotification([&] { musical.temporaryReloadPattern(); });
    const auto hostRecall = stateOf(musical);
    const int notificationsBeforeRecall = listener.notifications;
    musical.setStateInformation(hostRecall.getData(), static_cast<int>(hostRecall.getSize()));
    check(listener.notifications == notificationsBeforeRecall,
          "Recalling a saved DAW state marked its host project as newly modified");
    passed("host project-dirty callbacks can synchronously read edited steps and serialise state");
}

void checkMusicalState(TaktAudioProcessor& processor, const juce::ValueTree& expected)
{
    const auto actual = readStateTree(stateOf(processor));
    for (auto parameter : expected)
        if (parameter.hasType("PARAM"))
        {
            const auto id = parameter["id"].toString();
            if (id == "play" || id == "hostSync" || id == "master") continue;
            const auto recalled = actual.getChildWithProperty("id", id);
            check(recalled.isValid() && std::abs(static_cast<float>(recalled["value"])
                                                    - static_cast<float>(parameter["value"])) < 1.0e-4f,
                  "Temporary recall changed the saved musical parameter " + id.toStdString());
        }
    check(actual.getChildWithName("SEQUENCER").isEquivalentTo(expected.getChildWithName("SEQUENCER")),
          "Temporary recall changed saved samples, track lengths, steps or locks");
}

void testTemporaryPattern(const juce::File& mono, const juce::File& stereo)
{
    TaktAudioProcessor processor;
    configureDry(processor);
    juce::String error;
    for (int track = 0; track < takt::numTracks; ++track)
    {
        check(processor.loadSample(track, (track & 1) == 0 ? stereo : mono, error), error.toStdString());
        processor.setTrackLength(track, 128 - track);
        processor.setStep(track, 127, musicalStep(track + 3));
    }
    processor.setParameter("tempo", 143.0f);
    processor.setParameter("swing", 0.375f);
    processor.setParameter("feedback", 0.71f);
    processor.setParameter(TaktAudioProcessor::trackParameterID(3, "pitch"), 7.0f);
    processor.setTrackLength(3, 128);
    processor.setStep(3, 127, musicalStep(13));
    processor.temporarySavePattern();
    const auto saved = readStateTree(stateOf(processor));
    processor.prepareToPlay(testRate, testBlock);
    const auto originalAudio = process(processor, testBlock, note(39));
    // Mutate every musical parameter and sample slot, including boolean
    // controls on the final track. This detects incomplete snapshot arrays.
    for (auto* parameter : processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
        check(ranged != nullptr, "A musical parameter is not ranged");
        const auto id = ranged->paramID;
        if (id == "play" || id == "hostSync" || id == "master") continue;
        const float replacement = ranged->getValue() < 0.5f ? 0.83f : 0.17f;
        processor.setParameter(id, ranged->convertFrom0to1(replacement));
    }
    for (int track = 0; track < takt::numTracks; ++track)
    {
        check(processor.loadSample(track, (track & 1) == 0 ? mono : stereo, error), error.toStdString());
        processor.setTrackLength(track, 7);
        processor.clearTrack(track);
    }
    processor.setParameter("tempo", 207.0f);
    processor.setParameter("swing", 0.0f);
    processor.setParameter("feedback", 0.1f);
    processor.setParameter(TaktAudioProcessor::trackParameterID(3, "pitch"), -12.0f);
    processor.setTrackLength(3, 7);
    processor.clearTrack(3);
    check(processor.loadSample(3, stereo, error), error.toStdString());
    processor.setParameter("play", 1.0f);
    processor.setParameter("hostSync", 1.0f);
    processor.setParameter("master", 0.42f);
    processor.temporaryReloadPattern();
    checkMusicalState(processor, saved);
    check(processor.parameterValue("play") == 1 && processor.parameterValue("hostSync") == 1
              && std::abs(processor.parameterValue("master") - 0.42f) < 1.0e-6f,
          "Temporary reload unexpectedly changed live transport or master controls");
    processor.setParameter("play", 0.0f);
    processor.setParameter("hostSync", 0.0f);
    processor.setParameter("master", 1.0f);
    processor.prepareToPlay(testRate, testBlock);
    checkEqualAudio(process(processor, testBlock, note(39)), originalAudio, 1.0e-6f,
                    "Temporary sample/preset recall");
    processor.setStep(3, 127, {});
    processor.temporaryReloadPattern();
    checkMusicalState(processor, saved);

    TaktAudioProcessor fallback;
    const auto initial = readStateTree(stateOf(fallback));
    fallback.clearTrack(0);
    fallback.setParameter("tempo", 222.0f);
    check(fallback.loadSample(0, mono, error), error.toStdString());
    fallback.temporaryReloadPattern();
    checkMusicalState(fallback, initial);
    const auto persisted = stateOf(processor);
    fallback.setStateInformation(persisted.getData(), static_cast<int>(persisted.getSize()));
    const auto baseline = readStateTree(stateOf(fallback));
    fallback.setParameter("tempo", 191.0f);
    fallback.setStep(3, 127, {});
    check(fallback.loadSample(3, stereo, error), error.toStdString());
    const auto editedDawState = stateOf(fallback);
    check(editedDawState.getSize() > 0, "DAW save during temporary editing failed");
    const char rejectedState[] = "invalid-state";
    fallback.setStateInformation(rejectedState, sizeof(rejectedState));
    fallback.temporaryReloadPattern();
    checkMusicalState(fallback, baseline);
    fallback.temporarySavePattern();
    TaktAudioProcessor replacement;
    configureDry(replacement);
    replacement.setParameter("tempo", 193.0f);
    replacement.setTrackLength(15, 128);
    replacement.setStep(15, 127, musicalStep(21));
    const auto newDawState = stateOf(replacement);
    fallback.setStateInformation(newDawState.getData(), static_cast<int>(newDawState.getSize()));
    const auto replacementBaseline = readStateTree(stateOf(fallback));
    fallback.setParameter("tempo", 209.0f);
    fallback.clearTrack(15);
    fallback.temporaryReloadPattern();
    checkMusicalState(fallback, replacementBaseline);
    passed("temporary pattern save/reload: samples, parameters, 128 steps and fallback to initial/last loaded DAW state");
}

void testHostTransport(const juce::File& mono)
{
    TaktAudioProcessor processor;
    TestPlayHead host;
    configureDry(processor);
    processor.setParameter("hostSync", 1.0f);
    processor.setPlayHead(&host);
    juce::String error;
    check(processor.loadSample(0, mono, error), error.toStdString());
    for (int track = 0; track < takt::numTracks; ++track) processor.clearTrack(track);
    takt::Step step;
    step.enabled = true;
    processor.setStep(0, 0, step);
    processor.setStep(0, 1, step);
    processor.setStep(0, 8, step);
    processor.prepareToPlay(testRate, testBlock);
    host.set(0, 120, true);
    check(magnitude(process(processor, 96)) > 0.01f && processor.isHostPlaying()
              && processor.getCurrentStep(0) == 0,
          "Host play did not trigger the pattern independently of the manual Run parameter");

    processor.prepareToPlay(testRate, testBlock);
    host.set(1.75, 120, true);
    check(magnitude(process(processor, 257)) == 0 && processor.getCurrentStep(0) == 7,
          "Host seek did not follow PPQ to an inactive step");
    host.set(2.0, 120, true);
    check(magnitude(process(processor, 17)) > 0.001f && processor.getCurrentStep(0) == 8,
          "Host seek to an enabled step failed to retrigger");
    processor.prepareToPlay(testRate, testBlock);
    host.set(0, 120, true);
    check(magnitude(process(processor, 96)) > 0.01f, "Backward host seek failed to retrigger");

    processor.prepareToPlay(testRate, testBlock);
    const double beatsPerSample = 240.0 / (60.0 * testRate);
    host.set(0.25 - 64 * beatsPerSample, 240, true);
    check(magnitude(process(processor, 32)) == 0, "Host tempo scheduled the next step too early");
    host.set(0.25 - 32 * beatsPerSample, 240, true);
    const auto boundary = process(processor, 96);
    check(magnitude(boundary, 0, 32) == 0 && magnitude(boundary, 32) > 0.01f,
          "Host BPM/PPQ did not trigger at the expected boundary with changed block sizes");

    host.set(0.25, 240, false);
    const int stoppedStep = processor.getCurrentStep(0);
    for (int block = 0; block < 24; ++block) process(processor, testBlock);
    check(!processor.isHostPlaying() && processor.getCurrentStep(0) == stoppedStep
              && magnitude(process(processor, testBlock)) == 0,
          "Host stop continued advancing or triggering the pattern");
    processor.prepareToPlay(testRate, testBlock);
    host.position.setBpm(juce::nullopt);
    host.position.setIsPlaying(true);
    check(magnitude(process(processor, testBlock)) == 0 && !processor.isHostPlaying(),
          "Incomplete host position did not fall back to the manual transport");
    processor.setParameter("hostSync", 0.0f);
    processor.setParameter("play", 1.0f);
    check(magnitude(process(processor, testBlock)) > 0.01f && !processor.isHostPlaying(),
          "Manual transport could not run independently of the host");

    TaktAudioProcessor reference, variable;
    TestPlayHead referenceHost, variableHost;
    for (auto* candidate : { &reference, &variable })
    {
        configureDry(*candidate);
        candidate->setParameter("hostSync", 1.0f);
        candidate->setParameter("swing", 0.375f);
        candidate->prepareToPlay(testRate, testBlock);
    }
    reference.setPlayHead(&referenceHost);
    variable.setPlayHead(&variableHost);
    const auto constant = renderWithHost(reference, referenceHost, { 512 }, 48000, 137.0);
    const auto changing = renderWithHost(variable, variableHost, { 17, 257, 96, 511 }, 48000, 137.0);
    check(magnitude(constant) > 0.01f, "Host-synchronised comparison rendered no audio");
    checkEqualAudio(changing, constant, 1.0e-5f, "Host sequencing across variable block sizes");
    processor.setPlayHead(nullptr);
    reference.setPlayHead(nullptr);
    variable.setPlayHead(nullptr);
    passed("host transport: BPM/PPQ, play/stop, forward/backward seek and variable block sizes");
}

juce::Component* findComponent(juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id) return &parent;
    for (int index = 0; index < parent.getNumChildComponents(); ++index)
        if (auto* result = findComponent(*parent.getChildComponent(index), id)) return result;
    return nullptr;
}

template <typename Type>
Type& component(juce::Component& parent, const juce::String& id)
{
    auto* result = dynamic_cast<Type*>(findComponent(parent, id));
    check(result != nullptr, "Editor component is missing or has the wrong type: " + id.toStdString());
    return *result;
}

void click(juce::Component& parent, const juce::String& id,
           int mouseModifier = juce::ModifierKeys::leftButtonModifier)
{
    auto& target = component<juce::Button>(parent, id);
    check(target.isEnabled() && target.isVisible(), "Cannot click disabled/hidden editor control " + id.toStdString());
    for (auto* ancestor = target.getParentComponent(); ancestor && ancestor != &parent;
         ancestor = ancestor->getParentComponent())
        check(ancestor->isVisible(), "Cannot click a control inside a hidden editor view " + id.toStdString());
    const auto position = target.getLocalBounds().getCentre().toFloat();
    const auto now = juce::Time::getCurrentTime();
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    juce::MouseEvent down(source, position, juce::ModifierKeys(mouseModifier),
                          1.0f, 0, 0, 0, 0, &target, &target, now, position, now, 1, false);
    juce::MouseEvent up(source, position, juce::ModifierKeys{},
                        1.0f, 0, 0, 0, 0, &target, &target, now, position, now, 1, false);
    // Component's public virtual entry points route through Button's actual
    // mouse handlers, including toggles and callbacks, without a native window.
    static_cast<juce::Component&>(target).mouseDown(down);
    static_cast<juce::Component&>(target).mouseUp(up);
}

using ParameterIdentities = std::vector<std::pair<juce::String, const juce::AudioProcessorParameter*>>;

ParameterIdentities parameterIdentities(TaktAudioProcessor& processor)
{
    ParameterIdentities result;
    for (auto* parameter : processor.getParameters())
    {
        auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter);
        check(identified != nullptr, "A published automation parameter lost its stable ID");
        result.emplace_back(identified->paramID, parameter);
    }
    return result;
}

void testEditorNavigation(const juce::File& stereo)
{
    TaktAudioProcessor processor;
    juce::String error;
    check(processor.loadSample(8, stereo, error), error.toStdString());
    for (int track = 0; track < takt::numTracks; ++track)
    {
        processor.setTrackLength(track, 128);
        processor.setParameter(TaktAudioProcessor::trackParameterID(track, "pan"), (track - 8) / 8.0f);
        processor.setParameter(TaktAudioProcessor::trackParameterID(track, "pitch"), static_cast<float>(track - 8));
        processor.setStep(track, 127, musicalStep(track + 3));
    }
    const auto before = stateOf(processor);
    const auto identities = parameterIdentities(processor);
    check(identities.size() == 265, "UI navigation changed the published automation parameter count");
    std::set<juce::String> ids;
    for (const auto& parameter : identities) ids.insert(parameter.first);
    for (const char* id : { "play", "hostSync", "tempo", "swing", "master", "delayMix", "feedback", "delayBeats", "reverbMix" })
        check(ids.erase(id) == 1, "A global automation ID changed");
    for (int track = 0; track < takt::numTracks; ++track)
        for (const char* name : { "gain", "pan", "pitch", "cutoff", "resonance", "attack", "decay", "drive",
                                 "bitDepth", "start", "end", "delaySend", "reverbSend", "reverse", "mute", "loop" })
            check(ids.erase(TaktAudioProcessor::trackParameterID(track, name)) == 1,
                  "A track automation ID changed");
    check(ids.empty(), "Contextual encoders introduced unexpected automation IDs");

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    check(editor != nullptr, "Cannot create editor for navigation validation");
    check(stateOf(processor) == before, "Opening the editor changed musical state");
    click(*editor, "track-9");
    check(component<juce::Slider>(*editor, "track-level").getProperties()["parameterID"].toString()
              == TaktAudioProcessor::trackParameterID(8, "gain"), "Track 9 did not become selected");
    click(*editor, "family-src");
    auto expectedPitchID = TaktAudioProcessor::trackParameterID(8, "pitch");
    check(component<juce::Slider>(*editor, "encoder-A").getProperties()["parameterID"].toString() == expectedPitchID,
          "SRC encoder A did not bind the selected track's published pitch parameter");
    click(*editor, "param-page-next");
    check(component<juce::Slider>(*editor, "encoder-A").getProperties()["parameterID"].toString() == expectedPitchID,
          "SRC waveform subpage changed its automation target");
    for (int page = 1; page <= 8; ++page)
    {
        const auto id = "seq-page-" + juce::String(page);
        click(*editor, id);
        check(component<juce::Button>(*editor, id).getToggleState(), "Sequence page did not become selected");
    }
    click(*editor, "trig-16", juce::ModifierKeys::rightButtonModifier);
    check(stateOf(processor) == before, "Selecting the last trig pad modified its note or locks");
    click(*editor, "family-amp");
    const auto panID = TaktAudioProcessor::trackParameterID(8, "pan");
    check(component<juce::Slider>(*editor, "encoder-G").getProperties()["parameterID"].toString() == panID,
          "AMP encoder G did not bind the selected track's pan parameter");
    check(component<juce::Slider>(*editor, "track-level").getProperties()["parameterID"].toString()
              == TaktAudioProcessor::trackParameterID(8, "gain"),
          "LEVEL control did not remain attached to selected track gain");
    click(*editor, "family-src");
    click(*editor, "param-page-prev");
    click(*editor, "family-fltr");
    click(*editor, "param-page-next");
    click(*editor, "family-fx");
    click(*editor, "family-mod");
    click(*editor, "family-trig");
    click(*editor, "view-step-tools");
    click(*editor, "family-fx");
    click(*editor, "view-send-fx");
    click(*editor, "family-amp");
    check(component<juce::Slider>(*editor, "track-level").getProperties()["parameterID"].toString()
              == TaktAudioProcessor::trackParameterID(8, "gain"),
          "Parameter or sequence page navigation changed the active track");
    check(stateOf(processor) == before, "Navigating families, tracks or pages modified musical values or steps");
    check(parameterIdentities(processor) == identities,
          "Navigation replaced published parameters instead of rebinding their existing attachments");

    component<juce::Slider>(*editor, "encoder-G").setValue(0.25, juce::sendNotificationSync);
    check(std::abs(processor.parameterValue(panID) - 0.25f) < 1.0e-5f
              && processor.parameterValue(TaktAudioProcessor::trackParameterID(0, "pan")) == -1.0f,
          "Contextual AMP pan wrote to the wrong track after navigation");
    click(*editor, "family-src");
    auto& pitch = component<juce::Slider>(*editor, "encoder-A");
    check(pitch.getProperties()["parameterID"].toString() == expectedPitchID,
          "Returning to SRC failed to restore its pitch attachment");
    pitch.setValue(-5, juce::sendNotificationSync);
    check(std::abs(processor.parameterValue(expectedPitchID) + 5.0f) < 1.0e-5f
              && processor.parameterValue(TaktAudioProcessor::trackParameterID(0, "pitch")) == -8.0f,
          "Contextual SRC pitch wrote to the wrong track after navigation");
    passed("editor navigation preserves samples/steps/values and stable automation; contextual attachments edit track 9");
}

void testPanelGestures()
{
    TaktAudioProcessor processor;
    configureDry(processor);
    for (int track = 0; track < takt::numTracks; ++track) processor.clearTrack(track);
    processor.setTrackLength(8, 128);
    const auto source = musicalStep(9);
    processor.setStep(8, 127, source);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    const auto beforeNavigation = stateOf(processor);
    click(*editor, "track-select-modifier");
    click(*editor, "trig-9");
    check(component<juce::Slider>(*editor, "track-level").getProperties()["parameterID"].toString()
              == TaktAudioProcessor::trackParameterID(8, "gain"), "TRK + pad did not select track 9");
    check(!component<juce::Button>(*editor, "track-select-modifier").getToggleState(),
          "TRK modifier remained latched after selecting its track");
    click(*editor, "seq-page-8");
    click(*editor, "trig-16", juce::ModifierKeys::rightButtonModifier);
    check(stateOf(processor) == beforeNavigation, "TRK or pad selection modified musical state");
    const auto chord = [&](const char* target)
    {
        click(*editor, "func-modifier");
        check(component<juce::Button>(*editor, "func-modifier").getToggleState(), "FUNC did not latch");
        click(*editor, target);
        check(!component<juce::Button>(*editor, "func-modifier").getToggleState(),
              "FUNC remained latched after its command");
    };
    chord("edit-grid"); // FUNC + REC copies; it must not switch recording mode.
    check(component<juce::Button>(*editor, "edit-grid").getToggleState(), "FUNC + REC changed GRID mode");
    click(*editor, "trig-15", juce::ModifierKeys::rightButtonModifier);
    chord("transport-stop");
    compareSteps(processor.getStep(8, 126), source);
    chord("transport-stop");
    compareSteps(processor.getStep(8, 126), takt::Step{});
    chord("transport-stop");
    compareSteps(processor.getStep(8, 126), source);
    chord("transport-play");
    auto cleared = source;
    cleared.pitch = 0;
    cleared.lockPitch = cleared.lockCutoff = false;
    compareSteps(processor.getStep(8, 126), cleared);
    chord("transport-play");
    compareSteps(processor.getStep(8, 126), source);
    check(processor.parameterValue("play") == 0, "Clipboard transport chords started playback");

    click(*editor, "vst-tools");
    auto& scope = component<juce::ComboBox>(*editor, "edit-scope");
    check(scope.isVisible(), "VST tools did not expose editing scope");
    scope.setSelectedId(2, juce::sendNotificationSync);
    const auto beforePagePaste = patternOf(processor);
    chord("edit-grid");
    click(*editor, "seq-page-7");
    chord("transport-stop");
    auto afterPagePaste = beforePagePaste;
    for (int index = 0; index < 16; ++index) afterPagePaste[8][96 + index] = beforePagePaste[8][112 + index];
    checkPattern(processor, afterPagePaste);
    chord("transport-stop");
    checkPattern(processor, beforePagePaste);

    chord("navigation-yes");
    const auto checkpoint = readStateTree(stateOf(processor));
    processor.setParameter(TaktAudioProcessor::trackParameterID(8, "pitch"), -11);
    processor.clearTrack(8);
    processor.setParameter("master", 0.42f);
    chord("navigation-no");
    checkMusicalState(processor, checkpoint);
    check(std::abs(processor.parameterValue("master") - 0.42f) < 1.0e-6f,
          "FUNC + NO changed the live master level");
    check(component<juce::Slider>(*editor, "track-level").getProperties()["parameterID"].toString()
              == TaktAudioProcessor::trackParameterID(8, "gain"), "Temporary recall changed editor track navigation");
    passed("panel gestures: TRK selection, FUNC copy/paste/clear undo, page clipboard and temporary save/reload");
}

void testEditorKeyboard()
{
    TaktAudioProcessor processor;
    configureDry(processor);
    processor.clearTrack(0);
    const auto copied = musicalStep(9);
    processor.setStep(0, 5, copied);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* panel = dynamic_cast<TaktAudioProcessorEditor*>(editor.get());
    check(panel != nullptr, "Cannot access the editor keyboard event interface");
    const auto shortcut = [&](char key, juce::Component* origin)
    {
        // Native Windows CTRL shortcuts can carry no printable character.
        return panel->keyPressed(juce::KeyPress(static_cast<int>(key),
                                               juce::ModifierKeys::ctrlModifier, 0), origin);
    };
    click(*editor, "trig-6", juce::ModifierKeys::rightButtonModifier);
    check(shortcut('C', panel), "CTRL+C without a text character was not handled");
    processor.setStep(0, 5, musicalStep(20));
    click(*editor, "trig-12", juce::ModifierKeys::rightButtonModifier);
    juce::TextEditor textInput;
    editor->addAndMakeVisible(textInput);
    juce::Label textChild;
    textInput.addAndMakeVisible(textChild);
    const auto beforeTyping = stateOf(processor);
    check(!shortcut('C', &textInput) && !shortcut('V', &textChild),
          "A shortcut originating in a text editor was intercepted by musical editing");
    check(stateOf(processor) == beforeTyping, "Text input shortcuts changed the musical sequence");
    check(shortcut('V', panel), "CTRL+V without a text character was not handled");
    compareSteps(processor.getStep(0, 11), copied);
    const auto afterPaste = stateOf(processor);
    check(!shortcut('Z', &textInput) && stateOf(processor) == afterPaste,
          "Text input undo undid the musical paste");
    check(shortcut('Z', panel), "CTRL+Z without a text character was not handled");
    compareSteps(processor.getStep(0, 11), takt::Step{});
    passed("Windows CTRL+C/V/Z key codes work without text characters and bypass text input events");
}

void testGui(const juce::File& png)
{
    TaktAudioProcessor processor;
    processor.prepareToPlay(testRate, testBlock);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    check(editor != nullptr && editor->getWidth() > 600 && editor->getHeight() > 300,
          "Editor could not be created at a usable size");
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true);
    check(image.isValid() && image.getWidth() == editor->getWidth(), "Editor snapshot failed");
    std::set<juce::uint32> colours;
    for (int y = 0; y < image.getHeight(); y += 7)
        for (int x = 0; x < image.getWidth(); x += 7)
            colours.insert(image.getPixelAt(x, y).getARGB());
    check(colours.size() > 16, "Editor rendered an empty or uniform image");
    check(png.getParentDirectory().createDirectory().wasOk(), "Cannot create screenshot directory");
    check(!png.exists() || png.deleteFile(), "Cannot replace screenshot");
    auto stream = png.createOutputStream();
    check(stream != nullptr, "Cannot open screenshot file");
    juce::PNGImageFormat encoder;
    check(encoder.writeImageToStream(image, *stream), "Cannot encode editor PNG");
    editor.reset();
    passed("GUI editor creation and rendered PNG screenshot");
    std::cout << "PNG: " << png.getFullPathName() << std::endl;
}

void setHostedParameter(juce::AudioPluginInstance& plugin, const juce::String& name,
                        float normalisedValue)
{
    for (auto* parameter : plugin.getParameters())
        if (parameter->getName(128) == name)
        {
            parameter->setValueNotifyingHost(normalisedValue);
            return;
        }
    throw std::runtime_error("Hosted VST3 parameter is missing: " + name.toStdString());
}

void prepareHosted(juce::AudioPluginInstance& plugin)
{
    plugin.enableAllBuses();
    auto layout = plugin.getBusesLayout();
    check(layout.outputBuses.size() == 1, "Hosted VST3 exposes an unexpected output bus count");
    layout.outputBuses.set(0, juce::AudioChannelSet::stereo());
    check(plugin.setBusesLayout(layout), "Hosted VST3 rejected a stereo output bus");
    check(plugin.getTotalNumInputChannels() == 0 && plugin.getTotalNumOutputChannels() == 2,
          "Hosted VST3 has an incorrect channel layout");
    plugin.setRateAndBufferSizeDetails(testRate, testBlock);
    plugin.prepareToPlay(testRate, testBlock);
}

void testVst3(const juce::File& bundle)
{
    check(bundle.exists(), "VST3 bundle does not exist: " + bundle.getFullPathName().toStdString());
    juce::AudioPluginFormatManager formats;
    auto* vst = new juce::VST3PluginFormat;
    formats.addFormat(vst);
    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst->findAllTypesForFile(descriptions, bundle.getFullPathName());
    check(descriptions.size() > 0, "Actual VST3 module could not be scanned");
    juce::String error;
    auto plugin = formats.createPluginInstance(*descriptions[0], testRate, testBlock, error);
    check(plugin != nullptr, "Actual VST3 module could not be instantiated: " + error.toStdString());
    check(plugin->acceptsMidi(), "Actual VST3 module does not accept MIDI");
    prepareHosted(*plugin);
    setHostedParameter(*plugin, "Follow host", 0.0f);
    setHostedParameter(*plugin, "Run", 0.0f);
    setHostedParameter(*plugin, "Master", 0.6f);
    setHostedParameter(*plugin, "Delay return", 0.0f);
    setHostedParameter(*plugin, "Reverb return", 0.0f);
    auto midi = note(36, 64);
    midi.addEvent(juce::MidiMessage::noteOn(1, 51, static_cast<juce::uint8>(110)), 200);
    const auto expected = process(*plugin, testBlock, midi);
    check(magnitude(expected, 0, 64) == 0 && magnitude(expected, 64) > 0.001f,
          "Actual VST3 did not render MIDI at its sample offset");
    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    check(state.getSize() > 1000, "Actual VST3 returned an empty state");
    setHostedParameter(*plugin, "Master", 0.0f);
    check(magnitude(process(*plugin, testBlock, note(36))) == 0,
          "Actual VST3 did not respond to host parameter automation");
    plugin->releaseResources();
    plugin.reset();

    auto recalled = formats.createPluginInstance(*descriptions[0], testRate, testBlock, error);
    check(recalled != nullptr, "Cannot instantiate second VST3 for state recall: " + error.toStdString());
    recalled->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    prepareHosted(*recalled);
    checkEqualAudio(process(*recalled, testBlock, midi), expected, 1.0e-5f,
                    "Actual VST3 state recall into a new instance");
    recalled->releaseResources();
    recalled.reset();
    passed("actual VST3 module scan, instantiation, stereo MIDI rendering, automation and state recall");
}

juce::MemoryBlock componentState(const juce::MemoryBlock& hostedState)
{
    const auto xml = juce::AudioProcessor::getXmlFromBinary(hostedState.getData(), static_cast<int>(hostedState.getSize()));
    check(xml != nullptr, "Cannot decode the genuine legacy VST3 host state");
    const auto* component = xml->getChildByName("IComponent");
    check(component != nullptr, "Legacy VST3 state contains no IComponent");
    juce::MemoryBlock result;
    check(result.fromBase64Encoding(component->getAllSubText()), "Cannot decode legacy component state");
    return result;
}

takt::Step stepFromTree(const juce::ValueTree& tree)
{
    takt::Step step;
    step.enabled = tree["enabled"];
    step.velocity = tree["velocity"];
    step.probability = tree["probability"];
    step.pitch = tree["pitch"];
    step.cutoff = tree["cutoff"];
    step.lockPitch = tree["lockPitch"];
    step.lockCutoff = tree["lockCutoff"];
    step.conditionEvery = tree["conditionEvery"];
    step.conditionOffset = tree["conditionOffset"];
    step.retrigs = tree["retrigs"];
    step.microtiming = tree["microtiming"];
    return step;
}

void checkLegacySamplesAndPattern(TaktAudioProcessor& current, const juce::ValueTree& legacy)
{
    const auto sequencer = legacy.getChildWithName("SEQUENCER");
    check(sequencer.getNumChildren() == takt::numTracks, "Legacy fixture does not contain 16 tracks");
    for (int track = 0; track < takt::numTracks; ++track)
    {
        const auto savedTrack = sequencer.getChild(track);
        check(current.getTrackLength(track) == static_cast<int>(savedTrack["length"]),
              "Genuine legacy track length changed after recall");
        for (int index = 0; index < takt::maxSteps; ++index)
            compareSteps(current.getStep(track, index), stepFromTree(savedTrack.getChild(index)));
        const auto sample = current.getSample(track);
        const auto binary = savedTrack["sampleData"];
        const auto* blob = binary.getBinaryData();
        check(sample && blob, "A genuine legacy embedded sample was lost");
        juce::MemoryInputStream source(*blob, false);
        juce::GZIPDecompressorInputStream decoded(source);
        const int count = decoded.readInt();
        const double rate = decoded.readDouble();
        check(sample->sampleRate == rate && sample->name == savedTrack["sampleName"].toString().toStdString()
                  && sample->left.size() == static_cast<size_t>(count)
                  && sample->right.size() == static_cast<size_t>(count),
              "Genuine legacy sample metadata changed after recall");
        for (const auto* channel : { &sample->left, &sample->right })
            for (const auto value : *channel)
                check(decoded.readFloat() == value, "A genuine legacy source sample frame changed after recall");
    }
    for (auto parameter : legacy)
        if (parameter.hasType("PARAM"))
            check(std::abs(current.parameterValue(parameter["id"].toString())
                               - static_cast<float>(parameter["value"])) < 1.0e-4f,
                  "A genuine legacy automation parameter changed after recall");
}

void testLegacyVst3(const juce::File& bundle)
{
    check(bundle.exists(), "Legacy VST3 bundle does not exist: " + bundle.getFullPathName().toStdString());
    juce::AudioPluginFormatManager formats;
    auto* vst = new juce::VST3PluginFormat;
    formats.addFormat(vst);
    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst->findAllTypesForFile(descriptions, bundle.getFullPathName());
    check(descriptions.size() > 0, "Cannot scan the preserved legacy VST3 module");
    juce::String error;
    auto legacy = formats.createPluginInstance(*descriptions[0], testRate, testBlock, error);
    check(legacy != nullptr, "Cannot instantiate the preserved legacy module: " + error.toStdString());
    prepareHosted(*legacy);
    setHostedParameter(*legacy, "Follow host", 0.0f);
    setHostedParameter(*legacy, "Run", 0.0f);
    setHostedParameter(*legacy, "Master", 0.61f);
    setHostedParameter(*legacy, "Tempo", (143.0f - 30.0f) / 270.0f);
    setHostedParameter(*legacy, "Delay return", 0.0f);
    setHostedParameter(*legacy, "Reverb return", 0.0f);
    setHostedParameter(*legacy, "Track 9 gain", 0.6f);
    setHostedParameter(*legacy, "Track 9 pitch", 43.0f / 72.0f);
    juce::MemoryBlock hosted;
    legacy->getStateInformation(hosted);
    auto tree = readStateTree(componentState(hosted));
    auto track = tree.getChildWithName("SEQUENCER").getChild(8);
    track.setProperty("length", 128, nullptr);
    auto last = track.getChild(127);
    last.setProperty("enabled", true, nullptr);
    last.setProperty("velocity", 0.625, nullptr);
    last.setProperty("probability", 0.5, nullptr);
    last.setProperty("pitch", 12.0, nullptr);
    last.setProperty("cutoff", 4200.0, nullptr);
    last.setProperty("lockPitch", true, nullptr);
    last.setProperty("lockCutoff", true, nullptr);
    last.setProperty("conditionEvery", 3, nullptr);
    last.setProperty("conditionOffset", 1, nullptr);
    last.setProperty("retrigs", 4, nullptr);
    last.setProperty("microtiming", 0.125, nullptr);
    const auto component = writeStateTree(tree);
    auto xml = juce::AudioProcessor::getXmlFromBinary(hosted.getData(), static_cast<int>(hosted.getSize()));
    auto* stateElement = xml->getChildByName("IComponent");
    stateElement->deleteAllChildElements();
    stateElement->addTextElement(component.toBase64Encoding());
    juce::MemoryBlock seed;
    juce::AudioProcessor::copyXmlToBinary(*xml, seed);
    legacy->setStateInformation(seed.getData(), static_cast<int>(seed.getSize()));
    // The fixture below is emitted by the preserved binary, including its
    // original sample encoder and state implementation, not by the new code.
    legacy->getStateInformation(hosted);
    const auto produced = componentState(hosted);
    const auto producedTree = readStateTree(produced);
    const auto producedTrack = producedTree.getChildWithName("SEQUENCER").getChild(8);
    check(static_cast<int>(producedTrack["length"]) == 128,
          "The preserved module did not accept the seeded 128-step legacy pattern");
    compareSteps(stepFromTree(producedTrack.getChild(127)), stepFromTree(last));
    const auto producedPitch = producedTree.getChildWithProperty("id", TaktAudioProcessor::trackParameterID(8, "pitch"));
    check(producedPitch.isValid() && std::abs(static_cast<float>(producedPitch["value"]) - 7.0f) < 1.0e-4f,
          "The preserved module did not emit the customised legacy track pitch");
    const auto fixture = bundle.getParentDirectory().getChildFile("legacy-produced.state");
    check(fixture.replaceWithData(produced.getData(), produced.getSize()), "Cannot save genuine legacy state fixture");

    TaktAudioProcessor current;
    current.setStateInformation(produced.getData(), static_cast<int>(produced.getSize()));
    checkLegacySamplesAndPattern(current, producedTree);
    current.prepareToPlay(testRate, testBlock);
    auto midi = note(36, 37);
    midi.addEvent(juce::MidiMessage::noteOn(1, 44, static_cast<juce::uint8>(103)), 97);
    midi.addEvent(juce::MidiMessage::noteOn(1, 51, static_cast<juce::uint8>(91)), 173);
    const auto expected = process(*legacy, testBlock, midi);
    check(magnitude(expected) > 0.001f, "Preserved legacy module rendered no audio for comparison");
    checkEqualAudio(process(current, testBlock, midi), expected, 1.0e-5f,
                    "Genuine legacy state MIDI rendering");
    TaktAudioProcessor recalled;
    const auto migrated = stateOf(current);
    recalled.setStateInformation(migrated.getData(), static_cast<int>(migrated.getSize()));
    checkLegacySamplesAndPattern(recalled, producedTree);
    legacy->releaseResources();
    legacy.reset();
    passed("genuine legacy module-produced state preserves 16 source samples, 128 steps, locks, IDs and rendered audio");
    std::cout << "LEGACY STATE: " << fixture.getFullPathName() << std::endl;
}

void renderDemo(const juce::File& wav)
{
    TaktAudioProcessor processor;
    processor.setParameter("hostSync", 0.0f);
    processor.setParameter("tempo", 120.0f);
    processor.setParameter("play", 1.0f);
    processor.prepareToPlay(testRate, testBlock);
    auto writer = wavWriter(wav, testRate, 2);
    constexpr int totalFrames = 8 * 48000; // Four bars at 120 BPM.
    float peak = 0;
    for (int cursor = 0; cursor < totalFrames; cursor += testBlock)
    {
        const int count = std::min(testBlock, totalFrames - cursor);
        const auto audio = process(processor, count);
        peak = std::max(peak, magnitude(audio));
        check(writer->writeFromAudioSampleBuffer(audio, 0, count), "Cannot write offline demo audio");
    }
    writer.reset();
    check(peak > 0.01f, "Offline demo contained no audio");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(wav));
    check(reader && reader->lengthInSamples == totalFrames && reader->numChannels == 2
              && reader->sampleRate == testRate, "Offline WAV has incorrect duration or channel format");
    passed("offline four-bar demo at 120 BPM, stereo 48 kHz WAV");
    std::cout << "WAV: " << wav.getFullPathName() << std::endl;
}
}

int main(int argc, char** argv)
{
    try
    {
        juce::File screenshot, vst3, legacy, render;
        for (int i = 1; i < argc; ++i)
        {
            const std::string option(argv[i]);
            if (option == "--help")
            {
                std::cout << "TaktTests [--gui editor.png] [--host plugin.vst3] [--legacy old-plugin.vst3] [--render demo.wav]\n"
                             "Default processor tests are headless; GUI/module checks require a display on Linux (Xvfb is sufficient).\n";
                return 0;
            }
            check(i + 1 < argc, "Missing file path after " + option);
            auto path = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[++i]));
            if (option == "--gui") screenshot = path;
            else if (option == "--host") vst3 = path;
            else if (option == "--legacy") legacy = path;
            else if (option == "--render") render = path;
            else throw std::runtime_error("Unknown option: " + option);
        }
        std::unique_ptr<juce::ScopedJuceInitialiser_GUI> gui;
        std::unique_ptr<HeadlessMessages> messages;
        if (screenshot != juce::File{} || vst3 != juce::File{} || legacy != juce::File{})
            gui = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
        else messages = std::make_unique<HeadlessMessages>();
        TemporaryDirectory temporary;
        const auto mono = makeWave(temporary.directory, "mono-22050", 1);
        const auto stereo = makeWave(temporary.directory, "stereo-22050", 2);
        testMidiAndBuses();
        testSampleImportAndResampling(mono, stereo, temporary.directory);
        testParameters(mono);
        testState(mono, stereo);
        testClipboard(mono, stereo);
        testTemporaryPattern(mono, stereo);
        testHostTransport(mono);
        if (screenshot != juce::File{})
        {
            testEditorNavigation(stereo);
            testPanelGestures();
            testEditorKeyboard();
            testGui(screenshot);
        }
        else std::cout << "SKIP: GUI screenshot (enable with --gui editor.png)" << std::endl;
        if (vst3 != juce::File{}) testVst3(vst3);
        else std::cout << "SKIP: actual VST3 module (enable with --host plugin.vst3)" << std::endl;
        if (legacy != juce::File{}) testLegacyVst3(legacy);
        else std::cout << "SKIP: preserved legacy module (enable with --legacy old-plugin.vst3)" << std::endl;
        if (render != juce::File{}) renderDemo(render);
        std::cout << "All requested integration checks passed." << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << std::endl;
        return 1;
    }
}
