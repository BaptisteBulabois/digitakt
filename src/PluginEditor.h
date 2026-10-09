#pragma once

#include "PluginProcessor.h"
#include "engine/PatternChain.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

class TaktAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                       private juce::Timer,
                                       private juce::KeyListener,
                                       public juce::FileDragAndDropTarget
{
public:
    explicit TaktAudioProcessorEditor(TaktAudioProcessor&);
    ~TaktAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    using juce::AudioProcessorEditor::keyPressed;
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;

private:
    class HardwareLookAndFeel;
    class Panel;
    class Dial;
    class TrackPad;
    class StepPad;
    class Waveform;
    enum class Family { Trig, Source, Filter, Amp, Fx, Mod };
    enum class View { Parameters, StepTools, SendFx, SliceEditor, Patterns, Song };
    enum class ValueFormat { Number, Integer, Percent, Pitch, Hertz, Milliseconds, Seconds, Beats };
    enum class BindingKind { Unavailable, Parameter, Step, Playback, Custom };
    struct Binding
    {
        BindingKind kind = BindingKind::Unavailable;
        juce::String parameterID, stepField;
        std::function<double()> read;
    };
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void paintPanel(juce::Graphics&);
    void layoutPanel();
    void selectTrack(int);
    void selectStep(int, bool toggle);
    void selectFamily(Family);
    void changeParameterPage(int delta);
    void selectSequencerPage(int page);
    void showView(View);
    void goBack();
    void rebuildControls();
    void bindParameter(int slot, const juce::String& name, const juce::String& label,
                       ValueFormat, const juce::String& tooltip, bool global = false);
    void bindStep(int slot, const juce::String& field, const juce::String& label,
                  double low, double high, double interval, ValueFormat, const juce::String& tooltip);
    void bindUnavailable(int slot, const juce::String& label, const juce::String& reason);
    void bindPlayback(int slot);
    void bindChoice(int slot, const juce::String& name, const juce::String& label,
                    const juce::StringArray& choices, const juce::String& tooltip);
    void bindCustom(int slot, const juce::String& label, double low, double high, double interval,
                    ValueFormat, const juce::String& tooltip, std::function<double()> read,
                    std::function<void(double)> write);
    void showMachineMenu();
    void showFilterMachineMenu();
    void selectPatternPad(int);
    void selectSong(int);
    void refreshArrangementControls();
    void editSongRow(const std::function<void(takt::SongRow&)>&);
    void showSongMuteMenu();
    void applyChain();
    void showSliceMenu();
    void openSliceEditor();
    void moveSlice(int delta);
    void editSlicePoint(int point, float value);
    void allocateSliceLocks(bool random);
    void confirmAction();
    void finishControlAll(bool cancel);
    void cancelDestinationPreview();
    int currentMachine() const;
    int currentSliceCount() const;
    takt::SlicePoint effectiveSlicePoint(int index) const;
    float snapZeroCrossing(float position) const;
    void formatSlider(juce::Slider&, ValueFormat);
    void refreshSteps();
    void refreshControls();
    void changeStep(const std::function<void(takt::Step&)>&);
    void editSelection(int action);
    void undoEdit();
    int parameterPageCount() const;
    bool editingText() const;
    void chooseSample();
    void importSample(const juce::File&);
    void showStatus(const juce::String&, bool error = false);
    void updateVisibility();
    void internalPlayPause();

    TaktAudioProcessor& processor;
    std::unique_ptr<HardwareLookAndFeel> skin;
    std::unique_ptr<Panel> panel;
    std::unique_ptr<Waveform> waveform;
    juce::TooltipWindow tooltips;
    std::array<std::unique_ptr<TrackPad>, takt::numTracks> trackPads;
    std::array<std::unique_ptr<StepPad>, 16> stepPads;
    std::array<juce::TextButton, 8> pageButtons;
    std::array<std::unique_ptr<Dial>, 8> encoders;
    std::array<Binding, 8> bindings;
    std::array<juce::TextButton, 6> familyButtons;
    std::array<std::unique_ptr<Dial>, 3> transportDials;
    std::unique_ptr<Dial> trackLevel;
    juce::TextButton runButton{"PLAY / PAUSE"}, hostButton{"HOST SYNC"};
    juce::TextButton demoButton{"LOAD DEMO"}, clearButton{"CLEAR LOCKS"};
    juce::TextButton importButton{"IMPORT SAMPLE"}, triggerButton{"AUDITION"};
    juce::TextButton reverseButton{"REVERSE"}, muteButton{"MUTE"}, loopButton{"LOOP"};
    juce::TextButton pitchLockButton{"PITCH LOCK"}, cutoffLockButton{"FILTER LOCK"};
    juce::TextButton previousPageButton{"<"}, nextPageButton{">"};
    juce::TextButton gridButton{"REC / GRID"}, stepToolsButton{"STEP TOOLS"}, sendFxButton{"SEND FX"};
    juce::TextButton copyButton{"COPY"}, pasteButton{"PASTE"}, undoButton{"UNDO"};
    juce::TextButton temporarySaveButton{"TEMP SAVE"}, temporaryReloadButton{"TEMP RELOAD"};
    juce::TextButton noButton{"NO / BACK"}, helpButton{"?"};
    juce::TextButton funcButton{"FUNC"}, stopButton{"STOP"}, yesButton{"YES"};
    juce::TextButton trkButton{"TRK"}, pageButton{"PAGE"}, toolsButton{"VST TOOLS"};
    juce::TextButton sourceImportButton{"IMPORT"};
    std::array<juce::TextButton, 3> unavailableButtons;
    std::array<juce::TextButton, 4> centreButtons;
    juce::TextButton leftButton{"<"}, rightButton{">"};
    juce::Label drawerBackdrop;
    juce::ComboBox editScope;
    juce::ComboBox trackSpeed;
    juce::TextButton fillButton{"FILL"};
    juce::Slider patternLength;
    std::array<juce::TextButton, 8> bankButtons;
    juce::TextEditor chainText;
    juce::TextButton chainApplyButton{"PLAY CHAIN"}, chainAppendButton{"ADD CURRENT"};
    juce::TextButton performKitButton{"PERFORM KIT"}, kitSaveButton{"SAVE KIT"}, kitReloadButton{"RELOAD KIT"};
    juce::ComboBox songSelect, songRowSelect;
    juce::TextButton songAddButton{"ADD ROW"}, songDeleteButton{"DELETE ROW"};
    juce::TextButton songPlayButton{"PLAY SONG"}, songQueueButton{"JUMP TO ROW"}, songMutesButton{"ROW MUTES"};
    juce::TextButton arrangementBackButton{"BACK"};
    juce::Label arrangementLabel;
    juce::Label sampleLabel, sampleInfoLabel, statusLabel, helpLabel;
    std::vector<std::unique_ptr<SliderAttachment>> controlAttachments;
    std::unique_ptr<SliderAttachment> levelAttachment;
    std::vector<std::unique_ptr<ButtonAttachment>> trackButtonAttachments;
    std::vector<std::unique_ptr<SliderAttachment>> globalAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> globalButtonAttachments;
    std::unique_ptr<juce::FileChooser> fileChooser;
    int selectedTrack = 0, selectedStep = 0, selectedPage = 0;
    int selectedBank = 0, selectedSong = 0, selectedSongRow = 0, sendFxPage = 0;
    int displayedAmpMode = -1, displayedFilterMachine = -1, displayedSongRows = -1;
    int selectedSlice = 0, displayedMachine = -1;
    int heldStep = -1;
    bool linkedSlicePoints = true;
    double sliceZoom = 1.0, slicePosition = 0.0, sliceVerticalZoom = 1.0;
    juce::String pendingDestination;
    float previousDestination = 0.0f;
    Family family = Family::Source;
    View view = View::Parameters;
    std::array<int, 6> parameterPages{};
    bool gridRecording = true, helpVisible = false;
    bool toolsVisible = false;
    bool refreshing = false;
    float displayedPeak = 0.0f;
    double statusExpiry = 0.0;
    juce::String lastSampleName;
    std::shared_ptr<const takt::Sample> lastSample;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TaktAudioProcessorEditor)
};
