#include "PluginEditor.h"
#include <cmath>

namespace
{
constexpr int designWidth = 1120, designHeight = 930;
const juce::Colour background{0xff101317}, panelColour{0xff191e24};
const juce::Colour inset{0xff11161b}, border{0xff303943};
const juce::Colour ink{0xffe8e9e2}, mutedInk{0xff8d9aa5}, accent{0xffefb34d};
juce::Font font(float size, bool bold = false)
{
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
}
void drawCaption(juce::Graphics& g, const juce::String& value, juce::Rectangle<int> r,
          float size = 12.0f, juce::Colour colour = mutedInk, bool bold = false,
          juce::Justification alignment = juce::Justification::centredLeft)
{
    g.setColour(colour);
    g.setFont(font(size, bold));
    g.drawFittedText(value, r, alignment, 1);
}
juce::String number(int n) { return juce::String(n).paddedLeft('0', 2); }
void percentDisplay(juce::Slider& slider)
{
    slider.textFromValueFunction = [](double value) { return juce::String(juce::roundToInt(value * 100.0)) + "%"; };
    slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue() / 100.0; };
    slider.updateText();
}
void numberDisplay(juce::Slider& slider, int decimals, const juce::String& suffix = {})
{
    slider.textFromValueFunction = [decimals, suffix](double value) { return juce::String(value, decimals) + suffix; };
    slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue(); };
    slider.updateText();
}
}

class TaktAudioProcessorEditor::HardwareLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    HardwareLookAndFeel()
    {
        setColour(juce::Slider::textBoxTextColourId, ink);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::thumbColourId, accent);
        setColour(juce::Slider::trackColourId, accent);
        setColour(juce::Slider::backgroundColourId, border);
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff262e37));
        setColour(juce::TextButton::buttonOnColourId, accent);
        setColour(juce::TextButton::textColourOffId, ink);
        setColour(juce::TextButton::textColourOnId, background);
        setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff252d35));
        setColour(juce::TooltipWindow::textColourId, ink);
        setColour(juce::TooltipWindow::outlineColourId, border);
    }
    juce::Font getLabelFont(juce::Label&) override { return font(12.0f); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return font(11.0f, true); }
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                              bool over, bool down) override
    {
        auto r = button.getLocalBounds().toFloat().reduced(0.5f);
        auto fill = button.getToggleState() ? accent : colour;
        if (over) fill = fill.brighter(0.12f);
        if (down) fill = fill.darker(0.12f);
        g.setColour(fill);
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(button.getToggleState() ? accent.brighter(0.2f) : border.brighter(0.12f));
        g.drawRoundedRectangle(r, 4.0f, 1.0f);
    }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float position, float startAngle, float endAngle, juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                            static_cast<float>(width), static_cast<float>(height)).reduced(4.0f);
        const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto cx = bounds.getCentreX(), cy = bounds.getCentreY();
        juce::Path track, value;
        track.addCentredArc(cx, cy, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, endAngle, true);
        value.addCentredArc(cx, cy, radius - 2.0f, radius - 2.0f, 0.0f, startAngle,
                           startAngle + position * (endAngle - startAngle), true);
        g.setColour(border);
        g.strokePath(track, juce::PathStrokeType(2.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(slider.isEnabled() ? accent : mutedInk.darker());
        g.strokePath(value, juce::PathStrokeType(2.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xff282f37));
        g.fillEllipse(cx - radius + 7.0f, cy - radius + 7.0f, (radius - 7.0f) * 2.0f, (radius - 7.0f) * 2.0f);
        const auto angle = startAngle + position * (endAngle - startAngle);
        const auto inner = radius * 0.35f, outer = radius * 0.65f;
        g.setColour(ink);
        g.drawLine(cx + std::sin(angle) * inner, cy - std::cos(angle) * inner,
                   cx + std::sin(angle) * outer, cy - std::cos(angle) * outer, 2.0f);
    }
};

class TaktAudioProcessorEditor::Panel final : public juce::Component
{
public:
    explicit Panel(TaktAudioProcessorEditor& e) : editor(e) {}
    void paint(juce::Graphics& g) override { editor.paintPanel(g); }
private:
    TaktAudioProcessorEditor& editor;
};

class TaktAudioProcessorEditor::Dial final : public juce::Component
{
public:
    explicit Dial(const juce::String& name)
    {
        label.setText(name, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(font(10.0f, true));
        label.setColour(juce::Label::textColourId, mutedInk);
        addAndMakeVisible(label);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                   juce::MathConstants<float>::pi * 2.75f, true);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 19);
        slider.setNumDecimalPlacesToDisplay(2);
        addAndMakeVisible(slider);
    }
    void resized() override
    {
        label.setBounds(0, 0, getWidth(), 16);
        slider.setBounds(0, 16, getWidth(), getHeight() - 16);
    }
    juce::Slider slider;
private:
    juce::Label label;
};

class TaktAudioProcessorEditor::TrackPad final : public juce::Button
{
public:
    explicit TrackPad(int track) : juce::Button("Select track " + juce::String(track + 1)), index(track) {}
    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(selected ? accent.withAlpha(0.13f) : (over ? border : inset));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(selected ? accent : border);
        g.drawRoundedRectangle(r, 4.0f, selected ? 1.5f : 1.0f);
        drawCaption(g, number(index + 1), {8, 4, getWidth() - 16, 25}, 19.0f,
             selected ? accent : (muted ? mutedInk : ink), true);
        drawCaption(g, sampleName.isEmpty() ? "EMPTY" : sampleName, {8, 31, getWidth() - 16, 16}, 9.0f,
             muted ? mutedInk.darker() : mutedInk);
        if (muted) drawCaption(g, "M", {getWidth() - 19, 5, 12, 16}, 9.0f, accent, true);
        if (playing || down)
        {
            g.setColour(accent);
            g.fillRoundedRectangle(r.getX() + 3.0f, r.getBottom() - 4.0f, r.getWidth() - 6.0f, 2.0f, 1.0f);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onMute) onMute(); return; }
        juce::Button::mouseDown(e);
    }
    int index;
    bool selected = false, muted = false, playing = false;
    juce::String sampleName;
    std::function<void()> onMute;
};

class TaktAudioProcessorEditor::StepPad final : public juce::Button
{
public:
    using juce::Button::clicked;
    explicit StepPad(int step) : juce::Button("Step " + juce::String(step + 1)), index(step) {}
    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(enabled ? accent.withAlpha(over || down ? 0.38f : 0.23f) : (over ? border : inset));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(selected ? ink.withAlpha(0.8f) : (enabled ? accent.withAlpha(0.65f) : border));
        g.drawRoundedRectangle(r, 4.0f, selected ? 1.5f : 1.0f);
        drawCaption(g, number(index + 1), {0, 8, getWidth(), 24}, 18.0f,
             withinLength ? (enabled ? accent : ink) : mutedInk.darker(), true, juce::Justification::centred);
        if (hasLock) drawCaption(g, "LOCK", {0, 33, getWidth(), 13}, 8.0f, accent, true, juce::Justification::centred);
        if (playing)
        {
            g.setColour(accent);
            g.fillRoundedRectangle(5.0f, static_cast<float>(getHeight() - 5), static_cast<float>(getWidth() - 10), 2.5f, 1.0f);
        }
        if (!withinLength)
        {
            g.setColour(background.withAlpha(0.5f));
            g.fillRoundedRectangle(r, 4.0f);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || e.mods.isAnyModifierKeyDown())
        {
            if (onStepClick) onStepClick(false);
            return;
        }
        juce::Button::mouseDown(e);
    }
    void clicked() override { if (onStepClick) onStepClick(true); }
    int index;
    bool enabled = false, selected = false, playing = false, hasLock = false, withinLength = true;
    std::function<void(bool)> onStepClick;
};

class TaktAudioProcessorEditor::Waveform final : public juce::Component
{
public:
    void setSample(std::shared_ptr<const takt::Sample> s)
    {
        if (sample == s) return;
        sample = std::move(s);
        shape.clear();
        if (sample && !sample->left.empty())
        {
            constexpr std::size_t bins = 1024;
            std::array<float, bins> peaks{};
            const auto length = sample->left.size();
            for (std::size_t bin = 0; bin < bins; ++bin)
            {
                const auto first = bin * length / bins, last = (bin + 1) * length / bins;
                const auto stride = juce::jmax<std::size_t>(1, (last - first) / 24);
                for (auto i = first; i < last; i += stride)
                {
                    peaks[bin] = juce::jmax(peaks[bin], std::abs(sample->left[i]));
                    if (i < sample->right.size()) peaks[bin] = juce::jmax(peaks[bin], std::abs(sample->right[i]));
                }
                peaks[bin] = juce::jlimit(0.012f, 1.0f, peaks[bin]);
            }
            shape.startNewSubPath(0.0f, 0.5f);
            for (std::size_t i = 0; i < bins; ++i)
                shape.lineTo(static_cast<float>(i) / static_cast<float>(bins - 1), 0.5f - peaks[i] * 0.46f);
            for (std::size_t i = bins; i-- > 0;)
                shape.lineTo(static_cast<float>(i) / static_cast<float>(bins - 1), 0.5f + peaks[i] * 0.46f);
            shape.closeSubPath();
        }
        repaint();
    }
    void setRegion(float a, float b)
    {
        if (std::abs(a - start) > 1.0e-6f || std::abs(b - end) > 1.0e-6f)
        { start = a; end = b; repaint(); }
    }
    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour(inset);
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(border.withAlpha(0.6f));
        for (int i = 1; i < 16; ++i)
            g.drawVerticalLine(i * getWidth() / 16, 4.0f, static_cast<float>(getHeight() - 4));
        g.drawHorizontalLine(getHeight() / 2, 0.0f, static_cast<float>(getWidth()));
        if (shape.isEmpty())
            drawCaption(g, "DROP AUDIO HERE  /  IMPORT A SAMPLE  /  LOAD THE DEMO", getLocalBounds(), 11.0f,
                 mutedInk, false, juce::Justification::centred);
        else
        {
            g.setColour(accent.withAlpha(0.8f));
            g.fillPath(shape, juce::AffineTransform::scale(r.getWidth(), r.getHeight()));
            g.setColour(background.withAlpha(0.7f));
            g.fillRect(0.0f, 0.0f, start * r.getWidth(), r.getHeight());
            g.fillRect(end * r.getWidth(), 0.0f, (1.0f - end) * r.getWidth(), r.getHeight());
            g.setColour(ink.withAlpha(0.8f));
            g.drawVerticalLine(juce::roundToInt(start * r.getWidth()), 2.0f, r.getHeight() - 2.0f);
            g.drawVerticalLine(juce::jmin(getWidth() - 1, juce::roundToInt(end * r.getWidth())), 2.0f, r.getHeight() - 2.0f);
        }
    }
private:
    std::shared_ptr<const takt::Sample> sample;
    juce::Path shape;
    float start = 0.0f, end = 1.0f;
};

TaktAudioProcessorEditor::TaktAudioProcessorEditor(TaktAudioProcessor& p)
    : AudioProcessorEditor(p), processor(p), skin(std::make_unique<HardwareLookAndFeel>()),
      panel(std::make_unique<Panel>(*this)), waveform(std::make_unique<Waveform>()), tooltips(this, 650)
{
    setLookAndFeel(skin.get());
    addAndMakeVisible(*panel);
    panel->setSize(designWidth, designHeight);
    panel->addAndMakeVisible(*waveform);
    const auto addButton = [this](juce::TextButton& button, bool toggle)
    {
        button.setClickingTogglesState(toggle);
        panel->addAndMakeVisible(button);
    };
    addButton(runButton, true); addButton(hostButton, true);
    addButton(demoButton, false); addButton(clearButton, false);
    addButton(importButton, false); addButton(triggerButton, false);
    addButton(reverseButton, true); addButton(muteButton, true); addButton(loopButton, true);
    addButton(pitchLockButton, true); addButton(cutoffLockButton, true);
    runButton.setTooltip("Start or stop the internal sequencer. Turn off Host Sync to use this clock.");
    hostButton.setTooltip("Follow the DAW tempo, playback position and transport. RUN controls the internal clock only.");
    demoButton.setTooltip("Replace the pattern and samples with an original sixteen-track demo.");
    clearButton.setTooltip("Erase all trigs on the selected track. Its sample and sound settings stay loaded.");
    importButton.setTooltip("Load WAV, AIFF or FLAC audio into the selected track. You can also drop a file on the panel.");
    triggerButton.setTooltip("Play the selected sample immediately, including when the sequencer is stopped.");
    reverseButton.setTooltip("Play the selected sample backwards.");
    loopButton.setTooltip("Repeat the selected sample region while its amplitude envelope is active.");
    muteButton.setTooltip("Silence the selected track. Right-click any track pad to toggle its mute.");
    pitchLockButton.setTooltip("Use this step's pitch in place of the track pitch.");
    cutoffLockButton.setTooltip("Use this step's filter cutoff in place of the track filter setting.");
    globalButtonAttachments.emplace_back(std::make_unique<ButtonAttachment>(processor.parameters, "play", runButton));
    globalButtonAttachments.emplace_back(std::make_unique<ButtonAttachment>(processor.parameters, "hostSync", hostButton));

    const std::array<const char*, 3> transportNames{{"TEMPO", "SWING", "MASTER"}};
    const std::array<const char*, 3> transportIds{{"tempo", "swing", "master"}};
    for (std::size_t i = 0; i < transportDials.size(); ++i)
    {
        transportDials[i] = std::make_unique<Dial>(transportNames[i]);
        panel->addAndMakeVisible(*transportDials[i]);
        globalAttachments.emplace_back(std::make_unique<SliderAttachment>(processor.parameters, transportIds[i], transportDials[i]->slider));
    }
    numberDisplay(transportDials[0]->slider, 1);
    transportDials[0]->slider.setTooltip("Internal tempo in beats per minute. The host supplies tempo when Host Sync is on.");
    percentDisplay(transportDials[1]->slider); percentDisplay(transportDials[2]->slider);

    const std::array<const char*, 13> labels{{"LEVEL", "PAN", "TUNE", "CUTOFF", "RESONANCE", "ATTACK", "DECAY", "DRIVE", "BITS", "START", "END", "DELAY SEND", "REVERB SEND"}};
    const std::array<const char*, 13> tips{{"Track volume.", "Stereo position, from left to right.", "Sample pitch in semitones.", "Low-pass filter frequency in hertz.", "Filter resonance.", "Amplitude attack in seconds.", "Amplitude decay in seconds.", "Saturation amount.", "Bit reduction resolution.", "Start of the sample region.", "End of the sample region.", "Amount sent to the stereo delay.", "Amount sent to the reverb."}};
    for (std::size_t i = 0; i < trackDials.size(); ++i)
    {
        trackDials[i] = std::make_unique<Dial>(labels[i]);
        trackDials[i]->slider.setTooltip(tips[i]);
        panel->addAndMakeVisible(*trackDials[i]);
    }
    for (auto i : {0, 4, 7, 9, 10, 11, 12}) percentDisplay(trackDials[static_cast<std::size_t>(i)]->slider);
    trackDials[2]->slider.setNumDecimalPlacesToDisplay(1);
    trackDials[3]->slider.setNumDecimalPlacesToDisplay(0);
    trackDials[8]->slider.setNumDecimalPlacesToDisplay(0);

    for (int i = 0; i < takt::numTracks; ++i)
    {
        auto& pad = trackPads[static_cast<std::size_t>(i)];
        pad = std::make_unique<TrackPad>(i);
        pad->setTooltip("Select track " + juce::String(i + 1) + ". Right-click to mute or unmute.");
        pad->onClick = [this, i] { selectTrack(i); };
        pad->onMute = [this, i]
        {
            const auto id = TaktAudioProcessor::trackParameterID(i, "mute");
            processor.setParameter(id, processor.parameterValue(id) >= 0.5f ? 0.0f : 1.0f);
        };
        panel->addAndMakeVisible(*pad);
    }
    for (int i = 0; i < 16; ++i)
    {
        auto& pad = stepPads[static_cast<std::size_t>(i)];
        pad = std::make_unique<StepPad>(i);
        pad->setTooltip("Click to add or remove a trig. Right-click or modifier-click to edit it without toggling.");
        pad->onStepClick = [this, i](bool toggle) { selectStep(selectedPage * 16 + i, toggle); };
        panel->addAndMakeVisible(*pad);
    }
    for (int i = 0; i < 8; ++i)
    {
        auto& b = pageButtons[static_cast<std::size_t>(i)];
        b.setButtonText(juce::String(i + 1));
        b.setTooltip("Steps " + juce::String(i * 16 + 1) + "–" + juce::String(i * 16 + 16));
        b.onClick = [this, i] { selectedPage = i; selectedStep = i * 16; refreshSteps(); refreshStepControls(); panel->repaint(); };
        panel->addAndMakeVisible(b);
    }
    patternLength.setRange(1.0, 128.0, 1.0);
    patternLength.setSliderStyle(juce::Slider::LinearHorizontal);
    patternLength.setTextBoxStyle(juce::Slider::TextBoxRight, false, 39, 23);
    patternLength.setTooltip("Length of the selected track, from 1 to 128 steps. Each track can have its own length.");
    patternLength.onValueChange = [this]
    {
        if (!refreshing) { processor.setTrackLength(selectedTrack, juce::roundToInt(patternLength.getValue())); refreshSteps(); }
    };
    panel->addAndMakeVisible(patternLength);

    const std::array<const char*, 8> stepNames{{"VELOCITY", "PROBABILITY", "LOCK TUNE", "LOCK CUTOFF", "EVERY", "OFFSET", "RETRIGS", "MICROTIMING"}};
    const std::array<const char*, 8> stepTips{{"Volume for this trig.", "Chance that this trig plays on each pass.", "Pitch override in semitones. Editing turns on Pitch Lock.", "Filter frequency override. Editing turns on Filter Lock.", "Play this trig once every N pattern passes.", "Choose the pass within the Every cycle, starting from zero.", "Number of repeated triggers within this step.", "Move this trig earlier or later, as a percentage of one step."}};
    for (std::size_t i = 0; i < stepDials.size(); ++i)
    {
        stepDials[i] = std::make_unique<Dial>(stepNames[i]);
        auto& s = stepDials[i]->slider;
        s.setTooltip(stepTips[i]);
        if (i < 2) { s.setRange(0.0, 1.0, 0.01); percentDisplay(s); }
        if (i == 2) { s.setRange(-48.0, 48.0, 0.01); numberDisplay(s, 1, " st"); }
        if (i == 3) { s.setRange(20.0, 20000.0, 1.0); s.setSkewFactorFromMidPoint(1000.0); numberDisplay(s, 0, " Hz"); }
        if (i == 4) { s.setRange(1.0, 64.0, 1.0); s.setNumDecimalPlacesToDisplay(0); }
        if (i == 5) { s.setRange(0.0, 63.0, 1.0); s.setNumDecimalPlacesToDisplay(0); }
        if (i == 6) { s.setRange(1.0, 8.0, 1.0); s.setNumDecimalPlacesToDisplay(0); }
        if (i == 7) { s.setRange(-0.49, 0.49, 0.01); percentDisplay(s); }
        s.onValueChange = [this, i]
        {
            if (refreshing) return;
            const auto value = static_cast<float>(stepDials[i]->slider.getValue());
            changeStep([i, value](takt::Step& step)
            {
                switch (i)
                {
                    case 0: step.velocity = value; break;
                    case 1: step.probability = value; break;
                    case 2: step.pitch = value; step.lockPitch = true; break;
                    case 3: step.cutoff = value; step.lockCutoff = true; break;
                    case 4: step.conditionEvery = juce::roundToInt(value); step.conditionOffset = juce::jmin(step.conditionOffset, step.conditionEvery - 1); break;
                    case 5: step.conditionOffset = juce::jmin(juce::roundToInt(value), step.conditionEvery - 1); break;
                    case 6: step.retrigs = juce::roundToInt(value); break;
                    case 7: step.microtiming = value; break;
                    default: break;
                }
            });
        };
        panel->addAndMakeVisible(*stepDials[i]);
    }
    pitchLockButton.onClick = [this] { changeStep([this](takt::Step& s) { s.lockPitch = pitchLockButton.getToggleState(); }); };
    cutoffLockButton.onClick = [this] { changeStep([this](takt::Step& s) { s.lockCutoff = cutoffLockButton.getToggleState(); }); };
    const std::array<const char*, 4> fxNames{{"DELAY MIX", "FEEDBACK", "DELAY TIME", "REVERB MIX"}};
    const std::array<const char*, 4> fxIds{{"delayMix", "feedback", "delayBeats", "reverbMix"}};
    for (std::size_t i = 0; i < fxDials.size(); ++i)
    {
        fxDials[i] = std::make_unique<Dial>(fxNames[i]);
        panel->addAndMakeVisible(*fxDials[i]);
        globalAttachments.emplace_back(std::make_unique<SliderAttachment>(processor.parameters, fxIds[i], fxDials[i]->slider));
        if (i != 2) percentDisplay(fxDials[i]->slider);
    }
    numberDisplay(fxDials[2]->slider, 2, " beats");
    fxDials[0]->slider.setTooltip("Wet level of the shared stereo delay.");
    fxDials[1]->slider.setTooltip("Delay feedback: how long the repeats continue.");
    fxDials[2]->slider.setTooltip("Delay interval in beats, following the active tempo.");
    fxDials[3]->slider.setTooltip("Wet level of the shared reverb.");

    for (auto* label : {&sampleLabel, &sampleInfoLabel, &statusLabel})
    {
        label->setColour(juce::Label::textColourId, mutedInk);
        label->setFont(font(label == &sampleLabel ? 13.0f : 11.0f, label == &sampleLabel));
        panel->addAndMakeVisible(*label);
    }
    sampleLabel.setColour(juce::Label::textColourId, ink);
    demoButton.onClick = [this]
    {
        processor.loadDemoPattern(); selectTrack(selectedTrack);
        showStatus("Original demo loaded. Turn off HOST SYNC and press RUN to listen.");
    };
    clearButton.onClick = [this]
    {
        processor.clearTrack(selectedTrack); refreshSteps(); refreshStepControls();
        showStatus("Track " + number(selectedTrack + 1) + " trigs cleared.");
    };
    importButton.onClick = [this] { chooseSample(); };
    triggerButton.onClick = [this] { processor.triggerTrack(selectedTrack); };
    layoutPanel();
    selectTrack(0);
    setResizable(true, true);
    setResizeLimits(900, 748, 1680, 1395);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(designWidth) / designHeight);
    setSize(designWidth, designHeight);
    timerCallback();
    startTimerHz(30);
}

TaktAudioProcessorEditor::~TaktAudioProcessorEditor()
{
    stopTimer();
    fileChooser.reset();
    setLookAndFeel(nullptr);
}

void TaktAudioProcessorEditor::paint(juce::Graphics& g) { g.fillAll(background); }

void TaktAudioProcessorEditor::resized()
{
    const auto scale = juce::jmin(static_cast<float>(getWidth()) / designWidth,
                                static_cast<float>(getHeight()) / designHeight);
    panel->setTransform(juce::AffineTransform::scale(scale));
    panel->setTopLeftPosition(juce::roundToInt((static_cast<float>(getWidth()) - designWidth * scale) * 0.5f),
                             juce::roundToInt((static_cast<float>(getHeight()) - designHeight * scale) * 0.5f));
}

void TaktAudioProcessorEditor::layoutPanel()
{
    runButton.setBounds(38, 104, 100, 38);
    hostButton.setBounds(148, 104, 126, 38);
    for (int i = 0; i < 3; ++i) transportDials[static_cast<std::size_t>(i)]->setBounds(290 + i * 110, 89, 100, 70);
    demoButton.setBounds(784, 104, 144, 38); clearButton.setBounds(938, 104, 142, 38);
    for (int i = 0; i < 16; ++i) trackPads[static_cast<std::size_t>(i)]->setBounds(38 + i * 65, 207, 61, 53);
    sampleLabel.setBounds(190, 286, 595, 24);
    sampleInfoLabel.setBounds(38, 367, 700, 17);
    triggerButton.setBounds(802, 291, 104, 27); importButton.setBounds(916, 291, 164, 27);
    waveform->setBounds(38, 321, 1042, 44);
    for (int i = 0; i < 13; ++i) trackDials[static_cast<std::size_t>(i)]->setBounds(35 + i * 80, 427, 77, 88);
    reverseButton.setBounds(38, 516, 110, 20); loopButton.setBounds(158, 516, 90, 20); muteButton.setBounds(258, 516, 90, 20);
    patternLength.setBounds(240, 561, 166, 26);
    for (int i = 0; i < 8; ++i) pageButtons[static_cast<std::size_t>(i)].setBounds(659 + i * 53, 565, 47, 25);
    for (int i = 0; i < 16; ++i) stepPads[static_cast<std::size_t>(i)]->setBounds(38 + i * 65, 606, 61, 57);
    for (int i = 0; i < 8; ++i) stepDials[static_cast<std::size_t>(i)]->setBounds(35 + i * 103, 722, 98, 76);
    pitchLockButton.setBounds(886, 732, 180, 26); cutoffLockButton.setBounds(886, 770, 180, 26);
    for (int i = 0; i < 4; ++i) fxDials[static_cast<std::size_t>(i)]->setBounds(567 + i * 128, 819, 115, 79);
    statusLabel.setBounds(24, 907, 1072, 18);
}

void TaktAudioProcessorEditor::paintPanel(juce::Graphics& g)
{
    g.fillAll(background);
    for (auto r : {juce::Rectangle<int>(24, 88, 1072, 72), {24, 170, 1072, 102},
                   {24, 282, 1072, 105}, {24, 397, 1072, 145}, {24, 552, 1072, 126},
                   {24, 688, 1072, 115}, {24, 813, 1072, 90}})
    {
        g.setColour(panelColour);
        g.fillRoundedRectangle(r.toFloat(), 7.0f);
        g.setColour(border.withAlpha(0.45f));
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 7.0f, 1.0f);
    }
    drawCaption(g, "TAKT", {24, 19, 132, 42}, 37.0f, ink, true);
    g.setColour(accent); g.fillRoundedRectangle(160.0f, 28.0f, 35.0f, 31.0f, 4.0f);
    drawCaption(g, "II", {160, 28, 35, 31}, 22.0f, background, true, juce::Justification::centred);
    drawCaption(g, "16-TRACK SAMPLE INSTRUMENT", {215, 27, 450, 20}, 12.0f, ink, true);
    drawCaption(g, "Stereo sampling  /  128-step sequencing  /  Per-step sound locks", {215, 49, 580, 18}, 11.0f);
    const auto hostSync = processor.isUsingHostClock();
    const auto playing = hostSync ? processor.isHostPlaying() : processor.parameterValue("play") >= 0.5f;
    drawCaption(g, hostSync ? "HOST CLOCK" : "INTERNAL CLOCK", {868, 22, 215, 18}, 10.0f,
         mutedInk, true, juce::Justification::centredRight);
    drawCaption(g, playing ? "PLAYING" : "STOPPED", {910, 42, 105, 20}, 11.0f,
         playing ? accent : mutedInk, true, juce::Justification::centredRight);
    g.setColour(border); g.fillRoundedRectangle(1026.0f, 46.0f, 56.0f, 7.0f, 2.0f);
    g.setColour(displayedPeak > 0.97f ? juce::Colour(0xffef6b58) : accent);
    g.fillRoundedRectangle(1026.0f, 46.0f, 56.0f * juce::jlimit(0.0f, 1.0f, displayedPeak), 7.0f, 2.0f);
    drawCaption(g, "CLOCK SOURCE", {634, 105, 136, 16}, 9.0f, mutedInk, true);
    drawCaption(g, hostSync ? "DAW TRANSPORT" : "INTERNAL", {634, 125, 136, 17}, 11.0f, hostSync ? ink : accent, true);
    drawCaption(g, "TRACKS", {38, 178, 100, 21}, 11.0f, ink, true);
    drawCaption(g, "SELECT A TRACK TO SHAPE ITS SOUND", {152, 178, 450, 21}, 10.0f);
    drawCaption(g, "RIGHT-CLICK TO MUTE", {850, 178, 230, 21}, 10.0f, mutedInk, false, juce::Justification::centredRight);
    drawCaption(g, "SAMPLE  /  " + number(selectedTrack + 1), {38, 286, 145, 24}, 11.0f, accent, true);
    drawCaption(g, "SOUND  /  TRACK " + number(selectedTrack + 1), {38, 403, 300, 22}, 11.0f, ink, true);
    drawCaption(g, "KNOB VALUES FOLLOW THE SELECTED TRACK", {660, 403, 420, 22}, 10.0f,
         mutedInk, false, juce::Justification::centredRight);
    drawCaption(g, "SEQUENCER", {38, 560, 130, 26}, 11.0f, ink, true);
    drawCaption(g, "LENGTH", {186, 560, 52, 26}, 10.0f, mutedInk, true);
    drawCaption(g, "STEPS " + number(selectedPage * 16 + 1) + "–" + juce::String(selectedPage * 16 + 16),
         {439, 560, 167, 26}, 11.0f, accent, true);
    drawCaption(g, "PAGE", {610, 560, 43, 26}, 10.0f, mutedInk, true);
    const auto step = processor.getStep(selectedTrack, selectedStep);
    drawCaption(g, "STEP " + number(selectedStep + 1) + "  /  " + (step.enabled ? "TRIG ON" : "TRIG OFF"),
         {38, 694, 283, 23}, 11.0f, step.enabled ? accent : ink, true);
    drawCaption(g, "CLICK TO TOGGLE  |  RIGHT-CLICK TO SELECT  |  EDITING TUNE OR CUTOFF ADDS A LOCK",
         {310, 695, 770, 21}, 9.5f, mutedInk, false, juce::Justification::centredRight);
    drawCaption(g, "SEND EFFECTS", {38, 826, 420, 24}, 11.0f, ink, true);
    drawCaption(g, "SHARED STEREO DELAY + REVERB", {38, 852, 460, 20}, 10.0f);
    drawCaption(g, "Use each track's send knobs to place it in the mix.", {38, 875, 470, 17}, 11.0f);
}

void TaktAudioProcessorEditor::selectTrack(int track)
{
    selectedTrack = juce::jlimit(0, takt::numTracks - 1, track);
    trackAttachments.clear(); trackButtonAttachments.clear();
    const std::array<const char*, 13> ids{{"gain", "pan", "pitch", "cutoff", "resonance", "attack", "decay", "drive", "bitDepth", "start", "end", "delaySend", "reverbSend"}};
    for (std::size_t i = 0; i < ids.size(); ++i)
        trackAttachments.emplace_back(std::make_unique<SliderAttachment>(processor.parameters,
            TaktAudioProcessor::trackParameterID(selectedTrack, ids[i]), trackDials[i]->slider));
    for (auto i : {0, 4, 7, 9, 10, 11, 12}) percentDisplay(trackDials[static_cast<std::size_t>(i)]->slider);
    numberDisplay(trackDials[1]->slider, 2);
    numberDisplay(trackDials[2]->slider, 1, " st");
    numberDisplay(trackDials[3]->slider, 0, " Hz");
    trackDials[5]->slider.textFromValueFunction = [](double value) { return juce::String(value * 1000.0, 1) + " ms"; };
    trackDials[5]->slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue() / 1000.0; };
    trackDials[5]->slider.updateText();
    numberDisplay(trackDials[6]->slider, 2, " s");
    numberDisplay(trackDials[8]->slider, 0);
    trackButtonAttachments.emplace_back(std::make_unique<ButtonAttachment>(processor.parameters, TaktAudioProcessor::trackParameterID(selectedTrack, "reverse"), reverseButton));
    trackButtonAttachments.emplace_back(std::make_unique<ButtonAttachment>(processor.parameters, TaktAudioProcessor::trackParameterID(selectedTrack, "mute"), muteButton));
    trackButtonAttachments.emplace_back(std::make_unique<ButtonAttachment>(processor.parameters, TaktAudioProcessor::trackParameterID(selectedTrack, "loop"), loopButton));
    patternLength.setValue(processor.getTrackLength(selectedTrack), juce::dontSendNotification);
    lastSample.reset(); lastSampleName.clear();
    waveform->setSample({});
    sampleLabel.setText("No sample loaded", juce::dontSendNotification);
    sampleInfoLabel.setText("WAV / AIFF / FLAC   |   Drop a file anywhere on the panel", juce::dontSendNotification);
    refreshSteps(); refreshStepControls();
    timerCallback(); panel->repaint();
}

void TaktAudioProcessorEditor::selectStep(int step, bool toggle)
{
    selectedStep = juce::jlimit(0, takt::maxSteps - 1, step);
    if (toggle)
    {
        auto s = processor.getStep(selectedTrack, selectedStep);
        s.enabled = !s.enabled;
        processor.setStep(selectedTrack, selectedStep, s);
        if (selectedStep >= processor.getTrackLength(selectedTrack))
            showStatus("Step " + number(selectedStep + 1) + " is beyond this track's length. Increase LENGTH to hear it.");
    }
    refreshSteps(); refreshStepControls(); panel->repaint();
}

void TaktAudioProcessorEditor::refreshSteps()
{
    const auto current = processor.getCurrentStep(selectedTrack), length = processor.getTrackLength(selectedTrack);
    for (int i = 0; i < 16; ++i)
    {
        auto& pad = *stepPads[static_cast<std::size_t>(i)];
        pad.index = selectedPage * 16 + i;
        const auto s = processor.getStep(selectedTrack, pad.index);
        pad.enabled = s.enabled; pad.selected = selectedStep == pad.index;
        pad.playing = current == pad.index; pad.hasLock = s.lockPitch || s.lockCutoff;
        pad.withinLength = pad.index < length;
        pad.repaint();
    }
    for (int i = 0; i < 8; ++i) pageButtons[static_cast<std::size_t>(i)].setToggleState(i == selectedPage, juce::dontSendNotification);
    patternLength.setValue(length, juce::dontSendNotification);
}

void TaktAudioProcessorEditor::refreshStepControls()
{
    refreshing = true;
    const auto s = processor.getStep(selectedTrack, selectedStep);
    const std::array<double, 8> values{{s.velocity, s.probability, s.pitch, s.cutoff,
                                      static_cast<double>(s.conditionEvery), static_cast<double>(s.conditionOffset),
                                      static_cast<double>(s.retrigs), s.microtiming}};
    for (std::size_t i = 0; i < values.size(); ++i)
        if (!stepDials[i]->slider.isMouseButtonDown()) stepDials[i]->slider.setValue(values[i], juce::dontSendNotification);
    pitchLockButton.setToggleState(s.lockPitch, juce::dontSendNotification);
    cutoffLockButton.setToggleState(s.lockCutoff, juce::dontSendNotification);
    refreshing = false;
}

void TaktAudioProcessorEditor::changeStep(const std::function<void(takt::Step&)>& edit)
{
    auto step = processor.getStep(selectedTrack, selectedStep);
    edit(step);
    processor.setStep(selectedTrack, selectedStep, step);
    refreshSteps(); refreshStepControls(); panel->repaint();
}

void TaktAudioProcessorEditor::timerCallback()
{
    displayedPeak = juce::jmax(processor.getOutputPeak(), displayedPeak * 0.87f);
    const auto hostSync = processor.isUsingHostClock();
    const auto playing = hostSync ? processor.isHostPlaying() : processor.parameterValue("play") >= 0.5f;
    for (int i = 0; i < takt::numTracks; ++i)
    {
        auto& pad = *trackPads[static_cast<std::size_t>(i)];
        pad.selected = i == selectedTrack;
        pad.muted = processor.parameterValue(TaktAudioProcessor::trackParameterID(i, "mute")) >= 0.5f;
        pad.sampleName = processor.getSampleName(i).upToFirstOccurrenceOf(".", false, false);
        const auto current = processor.getCurrentStep(i);
        pad.playing = playing && current >= 0 && processor.getStep(i, current).enabled && !pad.muted;
        pad.repaint();
    }
    const auto sample = processor.getSample(selectedTrack);
    const auto name = processor.getSampleName(selectedTrack);
    if (lastSample != sample || lastSampleName != name)
    {
        lastSample = sample; lastSampleName = name;
        waveform->setSample(sample);
        sampleLabel.setText(name.isEmpty() ? "No sample loaded" : name, juce::dontSendNotification);
        sampleInfoLabel.setText(sample && !sample->left.empty()
            ? juce::String(sample->sampleRate / 1000.0, 1) + " kHz   |   "
              + (sample->right.empty() ? "MONO" : "STEREO") + "   |   "
                + juce::String(static_cast<double>(sample->left.size()) / sample->sampleRate, 2) + " seconds"
            : "WAV / AIFF / FLAC   |   Drop a file anywhere on the panel", juce::dontSendNotification);
    }
    waveform->setRegion(processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "start")),
                        processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "end")));
    refreshSteps(); refreshStepControls();
    if (juce::Time::getMillisecondCounterHiRes() > statusExpiry)
    {
        statusLabel.setColour(juce::Label::textColourId, mutedInk);
        statusLabel.setText("Click a step to add a trig  |  Right-click to edit  |  Import your samples or load the original demo", juce::dontSendNotification);
    }
    processor.releaseUnusedSamples();
    panel->repaint();
}

void TaktAudioProcessorEditor::showStatus(const juce::String& message, bool error)
{
    statusExpiry = juce::Time::getMillisecondCounterHiRes() + (error ? 14000.0 : 8000.0);
    statusLabel.setColour(juce::Label::textColourId, error ? juce::Colour(0xffef8c74) : accent);
    statusLabel.setText(message, juce::dontSendNotification);
}

void TaktAudioProcessorEditor::chooseSample()
{
    if (fileChooser) return;
    const auto destinationTrack = selectedTrack;
    fileChooser = std::make_unique<juce::FileChooser>("Load a sample for track " + number(destinationTrack + 1),
                                                    juce::File{}, "*.wav;*.aif;*.aiff;*.flac", true);
    const juce::Component::SafePointer<TaktAudioProcessorEditor> safe(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, destinationTrack](const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            const auto file = chooser.getResult();
            if (file.existsAsFile())
            {
                juce::String error;
                if (safe->processor.loadSample(destinationTrack, file, error))
                    safe->showStatus("Loaded " + file.getFileName() + " on track " + number(destinationTrack + 1) + ".");
                else safe->showStatus(error, true);
                safe->timerCallback();
            }
            juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->fileChooser.reset(); });
        });
}

void TaktAudioProcessorEditor::importSample(const juce::File& file)
{
    juce::String error;
    if (processor.loadSample(selectedTrack, file, error)) showStatus("Loaded " + file.getFileName() + " on track " + number(selectedTrack + 1) + ".");
    else showStatus(error, true);
    timerCallback();
}

bool TaktAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files) if (juce::File(path).hasFileExtension("wav;aif;aiff;flac")) return true;
    return false;
}

void TaktAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& path : files)
        if (juce::File(path).hasFileExtension("wav;aif;aiff;flac")) { importSample(juce::File(path)); break; }
}
