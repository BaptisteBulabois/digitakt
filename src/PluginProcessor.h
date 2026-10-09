#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "engine/Engine.h"
#include <array>
#include <atomic>
#include <mutex>

class TaktAudioProcessor final : public juce::AudioProcessor,
                                 private juce::AudioProcessorValueTreeState::Listener
{
public:
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::setParameter;
    TaktAudioProcessor();
    ~TaktAudioProcessor() override;
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Takt II"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Init"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    static juce::String trackParameterID(int track, const juce::String& name);
    void setParameter(const juce::String& id, float value);
    float parameterValue(const juce::String& id) const;
    takt::Step getStep(int track, int step) const;
    void setStep(int track, int step, const takt::Step&);
    int getTrackLength(int track) const;
    void setTrackLength(int track, int length);
    void clearTrack(int track);
    enum class EditScope { Step, Page, Track };
    enum class EditResult { Applied, Undone, EmptyClipboard, ScopeMismatch,
                            InvalidSelection, NothingToUndo };
    // Step uses stepIndex (0..127); Page uses pageIndex (0..7). Track ignores
    // both indices. Track clipboard contains sequence and length, not preset.
    EditResult copySelection(EditScope, int track, int stepIndex = 0, int pageIndex = 0);
    EditResult pasteSelection(EditScope, int track, int stepIndex = 0, int pageIndex = 0);
    // Step clears pitch/cutoff locks only. Page/Track clear their note trigs.
    EditResult clearSelection(EditScope, int track, int stepIndex = 0, int pageIndex = 0);
    bool canUndoEdit() const;
    EditResult undoLastEdit();
    // Pattern snapshots exclude play, hostSync, master and editor navigation.
    // They and the clipboard/undo are session-local, outside TAKTII_STATE_1.
    void temporarySavePattern();
    void temporaryReloadPattern();
    void loadDemoPattern();
    void triggerTrack(int track, float velocity = 1.0f);
    bool loadSample(int track, const juce::File& file, juce::String& error);
    juce::String getSampleName(int track) const;
    std::shared_ptr<const takt::Sample> getSample(int track) const;
    int getCurrentStep(int track) const;
    float getOutputPeak() const { return outputPeak.load(); }
    bool isHostPlaying() const { return hostPlaying.load(); }
    bool isUsingHostClock() const { return usingHostClock.load(); }
    void releaseUnusedSamples();

private:
    struct PatternSnapshot
    {
        std::array<std::array<takt::Step, takt::maxSteps>, takt::numTracks> steps{};
        std::array<int, takt::numTracks> lengths{};
        std::array<std::shared_ptr<const takt::Sample>, takt::numTracks> samples{};
        std::array<std::array<float, 16>, takt::numTracks> trackParameters{};
        std::array<float, 9> globalParameters{};
    };
    struct Clipboard
    {
        std::array<takt::Step, takt::maxSteps> content{};
        EditScope scope = EditScope::Step;
        int length = 16;
        bool available = false;
    };
    enum class EditAction { Paste, Clear };
    struct UndoEdit
    {
        std::array<takt::Step, takt::maxSteps> content{};
        EditScope scope = EditScope::Step;
        EditAction action = EditAction::Paste;
        int track = 0, first = 0, count = 0, length = 16;
        std::uint64_t revision = 0;
        bool available = false;
    };
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void syncControls();
    void parameterChanged(const juce::String&, float) override;
    static bool selectionRange(EditScope, int track, int stepIndex, int pageIndex,
                               int& first, int& count);
    bool canUndoEditLocked() const;
    bool undoEditLocked();
    void retainSampleLocked(const std::shared_ptr<const takt::Sample>&);
    std::shared_ptr<const PatternSnapshot> capturePatternLocked() const;
    void restorePattern(const PatternSnapshot&);
    void notifyPatternChanged();
    static bool validTrack(int t) { return t >= 0 && t < takt::numTracks; }
    takt::Engine engine;
    juce::AudioFormatManager formats;
    mutable std::mutex controlMutex;
    std::array<std::array<takt::Step, takt::maxSteps>, takt::numTracks> steps{};
    std::array<int, takt::numTracks> lengths{};
    std::array<std::shared_ptr<const takt::Sample>, takt::numTracks> samples;
    std::vector<std::shared_ptr<const takt::Sample>> retiredSamples;
    Clipboard clipboard;
    UndoEdit undoEdit;
    std::shared_ptr<const PatternSnapshot> permanentPattern, temporaryPattern;
    std::atomic<std::uint64_t> editRevision{0};
    bool restoringPattern = false; // protected by controlMutex
    std::atomic<bool> patternDirty{true}, samplesDirty{true};
    std::array<std::atomic<int>, takt::numTracks> currentSteps;
    std::array<std::atomic<double>, takt::numTracks> sampleDurations;
    std::atomic<float> outputPeak{0};
    std::atomic<bool> hostPlaying{false};
    std::atomic<bool> usingHostClock{false};
    juce::AbstractFifo triggerFifo{128};
    std::array<takt::TriggerEvent, 128> queuedTriggers{};
    std::array<std::array<std::atomic<float>*, 16>, takt::numTracks> trackValues{};
    std::array<std::atomic<float>*, 9> globalValues{};
    std::array<float, 9> appliedGlobals{}; // audio-thread control snapshot
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TaktAudioProcessor)
};
