#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <algorithm>
#include <cmath>
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
        juce::File screenshot, vst3, render;
        for (int i = 1; i < argc; ++i)
        {
            const std::string option(argv[i]);
            if (option == "--help")
            {
                std::cout << "TaktTests [--gui editor.png] [--host plugin.vst3] [--render demo.wav]\n"
                             "Default processor tests are headless; --gui and --host require a display (Xvfb is sufficient).\n";
                return 0;
            }
            check(i + 1 < argc, "Missing file path after " + option);
            auto path = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[++i]));
            if (option == "--gui") screenshot = path;
            else if (option == "--host") vst3 = path;
            else if (option == "--render") render = path;
            else throw std::runtime_error("Unknown option: " + option);
        }
        std::unique_ptr<juce::ScopedJuceInitialiser_GUI> gui;
        std::unique_ptr<HeadlessMessages> messages;
        if (screenshot != juce::File{} || vst3 != juce::File{})
            gui = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
        else messages = std::make_unique<HeadlessMessages>();
        TemporaryDirectory temporary;
        const auto mono = makeWave(temporary.directory, "mono-22050", 1);
        const auto stereo = makeWave(temporary.directory, "stereo-22050", 2);
        testMidiAndBuses();
        testSampleImportAndResampling(mono, stereo, temporary.directory);
        testParameters(mono);
        testState(mono, stereo);
        testHostTransport(mono);
        if (screenshot != juce::File{}) testGui(screenshot);
        else std::cout << "SKIP: GUI screenshot (enable with --gui editor.png)" << std::endl;
        if (vst3 != juce::File{}) testVst3(vst3);
        else std::cout << "SKIP: actual VST3 module (enable with --host plugin.vst3)" << std::endl;
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
