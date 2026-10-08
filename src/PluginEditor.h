#pragma once

#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

class TaktAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                       private juce::Timer,
                                       public juce::FileDragAndDropTarget
{
public:
    explicit TaktAudioProcessorEditor(TaktAudioProcessor&);
    ~TaktAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

private:
    class HardwareLookAndFeel;
    class Panel;
    class Dial;
    class TrackPad;
    class StepPad;
    class Waveform;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void paintPanel(juce::Graphics&);
    void layoutPanel();
    void selectTrack(int);
    void selectStep(int, bool toggle);
    void refreshSteps();
    void refreshStepControls();
    void changeStep(const std::function<void(takt::Step&)>&);
    void chooseSample();
    void importSample(const juce::File&);
    void showStatus(const juce::String&, bool error = false);

    TaktAudioProcessor& processor;
    std::unique_ptr<HardwareLookAndFeel> skin;
    std::unique_ptr<Panel> panel;
    std::unique_ptr<Waveform> waveform;
    juce::TooltipWindow tooltips;
    std::array<std::unique_ptr<TrackPad>, takt::numTracks> trackPads;
    std::array<std::unique_ptr<StepPad>, 16> stepPads;
    std::array<juce::TextButton, 8> pageButtons;
    std::array<std::unique_ptr<Dial>, 13> trackDials;
    std::array<std::unique_ptr<Dial>, 8> stepDials;
    std::array<std::unique_ptr<Dial>, 4> fxDials;
    std::array<std::unique_ptr<Dial>, 3> transportDials;
    juce::TextButton runButton{"RUN"}, hostButton{"HOST SYNC"};
    juce::TextButton demoButton{"DEMO PATTERN"}, clearButton{"CLEAR TRACK"};
    juce::TextButton importButton{"IMPORT SAMPLE"}, triggerButton{"AUDITION"};
    juce::TextButton reverseButton{"REVERSE"}, muteButton{"MUTE"}, loopButton{"LOOP"};
    juce::TextButton pitchLockButton{"PITCH LOCK"}, cutoffLockButton{"FILTER LOCK"};
    juce::Slider patternLength;
    juce::Label sampleLabel, sampleInfoLabel, statusLabel;
    std::vector<std::unique_ptr<SliderAttachment>> trackAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> trackButtonAttachments;
    std::vector<std::unique_ptr<SliderAttachment>> globalAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> globalButtonAttachments;
    std::unique_ptr<juce::FileChooser> fileChooser;
    int selectedTrack = 0, selectedStep = 0, selectedPage = 0;
    bool refreshing = false;
    float displayedPeak = 0.0f;
    double statusExpiry = 0.0;
    juce::String lastSampleName;
    std::shared_ptr<const takt::Sample> lastSample;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TaktAudioProcessorEditor)
};
