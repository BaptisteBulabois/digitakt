#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "engine/Engine.h"
#include "engine/PatternChain.h"
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
    // They and the clipboard/undo are session-local, outside serialized state.
    void temporarySavePattern();
    void temporaryReloadPattern();
    std::array<takt::SlicePoint, takt::maxSlices> getSlicePoints(int track) const;
    bool setSlicePoint(int track, int slice, takt::SlicePoint);
    bool createSliceGrid(int track, int count);
    // A performance transaction: the active track is always included in mask.
    // Encoder values are offsets from its value at begin, applied to each
    // included track's baseline and clamped to that parameter's range.
    bool beginControlAll(int activeTrack, std::uint16_t trackMask = 0xffff);
    bool updateControlAll(const juce::String& suffix, float activeTrackValue);
    void commitControlAll();
    void cancelControlAll();
    bool isControlAllActive() const;
    void loadDemoPattern();
    void triggerTrack(int track, float velocity = 1.0f);
    void triggerSlice(int track, int slice, float velocity = 1.0f);
    bool loadSample(int track, const juce::File& file, juce::String& error);
    juce::String getSampleName(int track) const;
    std::shared_ptr<const takt::Sample> getSample(int track) const;
    int getCurrentStep(int track) const;
    float getOutputPeak() const { return outputPeak.load(); }
    bool isHostPlaying() const { return hostPlaying.load(); }
    bool isUsingHostClock() const { return usingHostClock.load(); }
    void releaseUnusedSamples();
    static constexpr size_t trackParameterCount = 85;
    struct PatternSnapshot
    {
        std::array<std::array<takt::Step, takt::maxSteps>, takt::numTracks> steps{};
        std::array<int, takt::numTracks> lengths{};
        std::array<std::shared_ptr<const takt::Sample>, takt::numTracks> samples{};
        std::array<std::array<float, trackParameterCount>, takt::numTracks> trackParameters{};
        std::array<std::array<takt::SlicePoint, takt::maxSlices>, takt::numTracks> slicePoints{};
        std::array<float, 17> globalParameters{};
        int patternLength = 16;
        int sourceSlot = 0, sourceSong = -1, sourceRow = -1;
    };
    bool setCurrentPattern(int slot);
    bool queuePattern(int slot);
    int getCurrentPattern() const { return currentPatternSlot.load(); }
    int getQueuedPattern() const { return queuedPatternSlot.load(); }
    takt::PatternChain::Mode getArrangementMode() const { return arrangementMode.load(); }
    int getCurrentSong() const { return currentSongSlot.load(); }
    int getCurrentSongRow() const { return currentSongRow.load(); }
    int getPatternLength() const;
    void setPatternLength(int length);
    std::shared_ptr<const PatternSnapshot> getPatternSnapshot(int slot) const;
    bool setPatternSnapshot(int slot, const PatternSnapshot&);
    bool setChain(const std::vector<int>& slots);
    std::vector<int> getChain() const;
    takt::Song getSong(int slot) const;
    bool setSong(int slot, const takt::Song&);
    bool startSong(int slot, int firstRow = 0);
    bool queueSongRow(int row);
    void setPerformKit(bool enabled);
    bool getPerformKit() const { return performKit.load(); }
    void saveKit();
    void reloadKit();
    // Call from the editor timer (or an offline host), never the render thread.
    // Audio transitions first use immutable snapshots, then mirror APVTS here.
    void servicePendingTransitions();

private:
    struct ArrangementCommand
    {
        enum class Kind { Reset, UpdatePattern, Select, Chain, UpdateSong, Song, JumpRow, PerformKit, ReloadKit };
        Kind kind = Kind::UpdatePattern;
        const PatternSnapshot* snapshot = nullptr;
        std::array<int, takt::patternChainCapacity> chain{};
        takt::Song song{};
        int slot = 0, row = 0, count = 0;
        bool flag = false;
        std::uint64_t serial = 0;
    };
    struct RetiredPattern { std::shared_ptr<const PatternSnapshot> snapshot; std::uint64_t serial = 0; };
    bool enqueueArrangementLocked(ArrangementCommand);
    std::shared_ptr<const PatternSnapshot> ensurePatternLocked(int slot);
    bool saveActivePatternLocked();
    void drainArrangementCommands();
    void applyAudioTransition(const takt::PatternChain::Transition&);
    void applyAudioPattern(const PatternSnapshot&, bool applyKit);
    void publishArrangementPosition();
    takt::TrackParams makeTrackParams(int track, const std::array<float, trackParameterCount>&,
                                    const std::array<takt::SlicePoint, takt::maxSlices>&) const;
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
    std::array<std::array<takt::SlicePoint, takt::maxSlices>, takt::numTracks> slicePoints{};
    std::vector<std::shared_ptr<const takt::Sample>> retiredSamples;
    Clipboard clipboard;
    UndoEdit undoEdit;
    std::shared_ptr<const PatternSnapshot> permanentPattern, temporaryPattern;
    std::array<std::shared_ptr<const PatternSnapshot>, takt::patternSlots> patternBank{};
    std::vector<RetiredPattern> retiredPatterns;
    takt::PatternChain songMetadata, audioArrangement;
    std::array<const PatternSnapshot*, takt::patternSlots> audioBank{};
    juce::AbstractFifo arrangementFifo{256};
    std::unique_ptr<std::array<ArrangementCommand, 256>> arrangementCommands
        = std::make_unique<std::array<ArrangementCommand, 256>>();
    std::uint64_t nextArrangementSerial = 0;
    std::atomic<std::uint64_t> consumedArrangementSerial{0};
    std::atomic<const PatternSnapshot*> activeAudioSnapshot{nullptr};
    std::atomic<const PatternSnapshot*> activeAudioKitSnapshot{nullptr};
    std::atomic<std::uint64_t> audioTransitionGeneration{0}, mirroredTransitionGeneration{0};
    std::atomic<std::uint64_t> audioSnapshotSequence{0}; // seqlock; only the renderer writes
    std::atomic<std::uint64_t> audioKitGeneration{0}, mirroredKitGeneration{0};
    std::atomic<bool> servicingTransitions{false};
    std::array<takt::TrackParams, takt::numTracks> appliedTrackParams{};
    std::array<std::array<std::atomic<std::uint64_t>, trackParameterCount>, takt::numTracks> parameterVersions{};
    std::array<std::atomic<std::uint64_t>, 17> globalParameterVersions{};
    std::array<std::array<std::atomic<std::uint64_t>, trackParameterCount>, takt::numTracks> transitionParameterVersions{};
    std::array<std::atomic<std::uint64_t>, 17> transitionGlobalVersions{};
    std::array<std::array<juce::String, trackParameterCount>, takt::numTracks> parameterIDs{};
    std::atomic<int> currentPatternSlot{0}, queuedPatternSlot{-1}, currentSongSlot{-1}, currentSongRow{-1};
    std::atomic<takt::PatternChain::Mode> arrangementMode{takt::PatternChain::Mode::Pattern};
    std::atomic<bool> performKit{false};
    std::vector<int> chainSelection;
    int patternLength = 16;
    double processingRate = 44100.0, arrangementPpq = 0.0, nextArrangementStepPpq = .25;
    double expectedHostPpq = 0.0;
    bool arrangementWasPlaying = false, previousHostClock = false;
    std::atomic<bool> arrangementOriginEnabled{false};
    struct ControlAllTransaction
    {
        std::array<std::array<float, trackParameterCount>, takt::numTracks> baseline{};
        std::array<bool, trackParameterCount> changed{};
        std::uint16_t mask = 0xffff;
        int activeTrack = 0;
        bool active = false;
    } controlAll;
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
    std::array<std::array<std::atomic<float>*, trackParameterCount>, takt::numTracks> trackValues{};
    std::array<std::atomic<float>*, 17> globalValues{};
    std::array<float, 17> appliedGlobals{}; // audio-thread control snapshot
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TaktAudioProcessor)
};
