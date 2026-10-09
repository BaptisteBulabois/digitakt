#include "PluginEditor.h"
#include <cmath>

namespace
{
constexpr int designWidth = 900, designHeight = 780;
const juce::Colour background{0xff101317}, panelColour{0xff191e24};
const juce::Colour inset{0xff11161b}, border{0xff303943};
const juce::Colour ink{0xffe8e9e2}, mutedInk{0xff8d9aa5}, accent{0xffefb34d};
const juce::Colour trigRed{0xffed514d};
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
    juce::Font getTextButtonFont(juce::TextButton& button, int) override
    { return font(button.getComponentID().startsWith("panel-menu-") ? 8.0f : 11.0f, true); }
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
        g.setColour(juce::Colour(0xff080b0e));
        g.fillEllipse(cx - radius + 2, cy - radius + 4, radius * 2, radius * 2);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff43494f), cx, cy - radius,
                                             juce::Colour(0xff252a2f), cx, cy + radius, false));
        g.fillEllipse(cx - radius, cy - radius, radius * 2, radius * 2);
        g.setColour(juce::Colour(0xff151a1e));
        for (int rib = 0; rib < 32; ++rib)
        {
            const auto a = static_cast<float>(rib) * juce::MathConstants<float>::twoPi / 32;
            g.drawLine(cx + std::sin(a) * (radius - 4), cy - std::cos(a) * (radius - 4),
                       cx + std::sin(a) * (radius - 1), cy - std::cos(a) * (radius - 1), 1.1f);
        }
        g.setColour(juce::Colour(0xff343a40));
        g.fillEllipse(cx - radius + 6, cy - radius + 6, (radius - 6) * 2, (radius - 6) * 2);
        const auto angle = startAngle + position * (endAngle - startAngle);
        const auto inner = radius * 0.35f, outer = radius * 0.65f;
        g.setColour(slider.isEnabled() ? ink : mutedInk.darker());
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
    void present(const juce::String& name, bool available, const juce::String& tip)
    {
        label.setText(name, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, available ? ink : mutedInk.darker(0.25f));
        label.setTooltip(tip);
        slider.setTooltip(tip);
        slider.setEnabled(available);
    }
    juce::String caption() const { return label.getText(); }
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
        g.setColour(selected ? accent.withAlpha(0.18f) : (over ? border : inset));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(selected ? accent : border);
        g.drawRoundedRectangle(r, 4.0f, selected ? 1.5f : 1.0f);
        drawCaption(g, number(index + 1), {0, 0, getWidth(), getHeight()}, 10.0f,
             selected ? accent : (muted ? mutedInk : ink), true, juce::Justification::centred);
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
        g.setColour(juce::Colour(0xff080b0e)); g.fillRoundedRectangle(r, 9.0f);
        r = r.reduced(3.0f);
        g.setColour(enabled ? trigRed.withAlpha(over || down ? 0.48f : 0.20f) : (over ? border : juce::Colour(0xff252b31)));
        g.fillRoundedRectangle(r, 7.0f);
        g.setColour(selected ? ink.withAlpha(0.8f) : border);
        g.drawRoundedRectangle(r, 7.0f, selected ? 1.5f : 1.0f);
        drawCaption(g, number(index % 16 + 1), {0, 13, getWidth(), 27}, 22.0f,
             withinLength ? (playing ? accent : enabled ? trigRed : ink) : mutedInk.darker(), true, juce::Justification::centred);
        if (hasLock && grid) drawCaption(g, "LOCK", {0, 43, getWidth(), 12}, 8.0f, accent, true, juce::Justification::centred);
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
    bool enabled = false, selected = false, playing = false, hasLock = false, withinLength = true, grid = true;
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
            g.setColour(ink.withAlpha(0.8f));
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
      panel(std::make_unique<Panel>(*this)), waveform(std::make_unique<Waveform>()), tooltips(this, 650),
      trackLevel(std::make_unique<Dial>("LEVEL"))
{
    setLookAndFeel(skin.get());
    setWantsKeyboardFocus(true);
    addKeyListener(this);
    addAndMakeVisible(*panel);
    panel->setSize(designWidth, designHeight);
    drawerBackdrop.setColour(juce::Label::backgroundColourId, juce::Colour(0xff151a20));
    drawerBackdrop.setColour(juce::Label::outlineColourId, border);
    drawerBackdrop.setOpaque(true); drawerBackdrop.setInterceptsMouseClicks(true, true);
    panel->addAndMakeVisible(drawerBackdrop);
    panel->addAndMakeVisible(*waveform);
    const auto addButton = [this](juce::TextButton& b, bool toggle, const juce::String& id, const juce::String& tip)
    {
        b.setClickingTogglesState(toggle); b.setComponentID(id); b.setTooltip(tip);
        b.addKeyListener(this); panel->addAndMakeVisible(b);
    };
    addButton(runButton, true, "transport-play", "Play or pause the internal clock. With a host clock, use Live's transport.");
    addButton(hostButton, true, "transport-host", "Follow the DAW tempo and playback position. Falls back to the internal clock when no host position is available.");
    addButton(demoButton, false, "load-demo", "Replace samples and sequence with the original demo. TEMP SAVE first if you want to restore your work.");
    addButton(importButton, false, "sample-import", "Import WAV, AIFF or FLAC into this track. You can also drop a file on the panel.");
    addButton(triggerButton, false, "sample-audition", "Play the selected track without starting or changing the sequence.");
    addButton(reverseButton, true, "track-reverse", "Reverse sample playback. This is the existing sample player, without a separate loop position.");
    addButton(loopButton, true, "track-loop", "Repeat between the sample start and end while the attack/decay envelope is active.");
    addButton(muteButton, true, "track-mute", "Silence this track. Right-click its selector above to mute without changing the selected track.");
    addButton(pitchLockButton, true, "step-lock-pitch", "Override the sample pitch on this selected step. This is a sample-pitch lock, not the hardware NOTE parameter.");
    addButton(cutoffLockButton, true, "step-lock-cutoff", "Override the filter cutoff on this selected step.");
    addButton(previousPageButton, false, "param-page-prev", "Previous parameter subpage. Keyboard: [ or Up.");
    addButton(nextPageButton, false, "param-page-next", "Next parameter subpage. Clicking the active family also cycles its subpages. Keyboard: ] or Down.");
    addButton(gridButton, true, "edit-grid", "GRID: pads add/remove steps. Turn REC off to play tracks. Right-click selects without toggling or sounding.");
    addButton(stepToolsButton, true, "view-step-tools", "Selected-step tools preserved from the first version: pitch/cutoff locks, conditional cycle, repeat count and microtiming.");
    addButton(sendFxButton, true, "view-send-fx", "Shared delay and reverb controls. These four software controls are not the complete hardware send-effects pages.");
    addButton(noButton, false, "navigation-no", "Return from Step Tools, Send FX or Help to the parameter page. Keyboard: Escape.");
    addButton(helpButton, true, "navigation-help", "Show the mouse and keyboard gestures. Escape closes this help.");
    addButton(copyButton, false, "edit-copy", "Copy the selected STEP (complete step), PAGE (16 steps), or TRACK SEQUENCE (128 steps and length). Ctrl+C.");
    addButton(pasteButton, false, "edit-paste", "Paste matching clipboard content to the selected target. Repeat to undo. Ctrl+V.");
    addButton(clearButton, false, "edit-clear", "STEP clears pitch/cutoff locks only. PAGE/TRACK clear their sequence. Repeat to undo. Delete.");
    addButton(undoButton, false, "edit-undo", "Undo the last paste or clear, while no intermediate musical edit has invalidated it. Ctrl+Z.");
    addButton(temporarySaveButton, false, "pattern-temp-save", "Remember sounds, samples, sequence, tempo, swing and effects for this session. Does not save the Live project or transport/master settings.");
    addButton(temporaryReloadButton, false, "pattern-temp-reload", "Restore the temporary checkpoint, or the last recalled/initial pattern when none was saved. Transport and master level stay unchanged.");
    addButton(funcButton, true, "func-modifier", "Click to latch FUNC for the next command: REC copies, PLAY clears, STOP pastes, YES saves temporarily, NO reloads. Click again to release.");
    funcButton.setColour(juce::TextButton::buttonColourId, accent);
    funcButton.setColour(juce::TextButton::textColourOffId, background);
    addButton(stopButton, false, "transport-stop", "Pause the internal sequencer without resetting its position or cutting tails. With host sync, use the DAW transport. FUNC+STOP pastes the selected scope.");
    addButton(yesButton, false, "navigation-yes", "FUNC+YES saves a temporary pattern checkpoint. No browser confirmation is available yet.");
    addButton(trkButton, true, "track-select-modifier", "Latch TRK then click a pad to select its track silently. Selection takes priority over grid editing. FUNC+TRK opens the selected-track mute control.");
    addButton(pageButton, false, "sequencer-page-next", "Cycle the eight sequencer pages. LEDs below are clickable for direct page selection.");
    addButton(toolsButton, true, "vst-tools", "Open software utilities: tempo, swing, length, clipboard scope, demo and selected-track controls.");
    addButton(leftButton, false, "navigation-left", "Previous sequencer page.");
    addButton(rightButton, false, "navigation-right", "Next sequencer page.");
    const std::array<const char*, 3> unavailableNames{{"KEYBOARD", "PTN", "SONG"}};
    for (int i = 0; i < 3; ++i)
    {
        auto& b = unavailableButtons[static_cast<std::size_t>(i)];
        b.setButtonText(unavailableNames[static_cast<std::size_t>(i)]);
        addButton(b, false, "unavailable-" + juce::String(unavailableNames[static_cast<std::size_t>(i)]).toLowerCase(), "This musical mode is not implemented in this version."); b.setEnabled(false);
    }
    const std::array<const char*, 4> centreNames{{"PRESET/KIT", "SETTINGS", "SAMPLING", "TEMPO"}};
    for (int i = 0; i < 4; ++i)
    {
        auto& b = centreButtons[static_cast<std::size_t>(i)]; b.setButtonText(centreNames[static_cast<std::size_t>(i)]);
        addButton(b, false, "panel-menu-" + juce::String(i), i == 1 || i == 3
            ? "Open the software utilities drawer. TEMPO and SWING are implemented here; hardware settings are not reproduced."
            : "This internal library or sampling mode is not implemented. Import user samples with the VST strip below.");
        b.setEnabled(i == 1 || i == 3);
        b.onClick = [this] { toolsVisible = true; toolsButton.setToggleState(true, juce::dontSendNotification); updateVisibility(); };
    }
    previousPageButton.setButtonText("^"); nextPageButton.setButtonText("v");
    runButton.setButtonText("PLAY"); gridButton.setButtonText("REC"); noButton.setButtonText("NO");
    globalButtonAttachments.emplace_back(std::make_unique<ButtonAttachment>(processor.parameters, "hostSync", hostButton));

    const std::array<const char*, 3> transportNames{{"TEMPO", "SWING*", "MASTER"}};
    const std::array<const char*, 3> transportIDs{{"tempo", "swing", "master"}};
    for (std::size_t i = 0; i < transportDials.size(); ++i)
    {
        transportDials[i] = std::make_unique<Dial>(transportNames[i]);
        transportDials[i]->slider.addKeyListener(this);
        panel->addAndMakeVisible(*transportDials[i]);
        globalAttachments.emplace_back(std::make_unique<SliderAttachment>(processor.parameters, transportIDs[i], transportDials[i]->slider));
        formatSlider(transportDials[i]->slider, i == 0 ? ValueFormat::Number : ValueFormat::Percent);
    }
    transportDials[0]->slider.setTooltip("Internal BPM. When a valid host clock is available, the host tempo takes priority.");
    transportDials[1]->slider.setTooltip("Legacy swing amount, 0-75%. This is not the Digitakt ratio of 50-80%; existing automation remains unchanged.");
    trackLevel->slider.setComponentID("track-level"); trackLevel->slider.addKeyListener(this);
    trackLevel->slider.setTooltip("Track LEVEL preserved from version 0.1. This is distinct from the hardware SRC LEV and AMP VOL controls.");
    panel->addAndMakeVisible(*trackLevel);
    for (int i = 0; i < 8; ++i)
    {
        encoders[static_cast<std::size_t>(i)] = std::make_unique<Dial>(juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)));
        auto& s = encoders[static_cast<std::size_t>(i)]->slider;
        s.setComponentID("encoder-" + juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)));
        s.addKeyListener(this); panel->addAndMakeVisible(*encoders[static_cast<std::size_t>(i)]);
    }
    const std::array<const char*, 6> names{{"TRIG", "SRC", "FLTR", "AMP", "FX", "MOD"}};
    const std::array<const char*, 6> ids{{"trig", "src", "fltr", "amp", "fx", "mod"}};
    for (int i = 0; i < 6; ++i)
    {
        auto& b = familyButtons[static_cast<std::size_t>(i)]; b.setButtonText(names[static_cast<std::size_t>(i)]);
        addButton(b, false, "family-" + juce::String(ids[static_cast<std::size_t>(i)]), "Select this parameter family. Click again to cycle its subpages.");
        b.onClick = [this, i] { selectFamily(static_cast<Family>(i)); };
    }
    for (int i = 0; i < takt::numTracks; ++i)
    {
        auto& pad = trackPads[static_cast<std::size_t>(i)]; pad = std::make_unique<TrackPad>(i);
        pad->setComponentID("track-" + juce::String(i + 1)); pad->addKeyListener(this);
        pad->setTooltip("Select track " + juce::String(i + 1) + " silently. Right-click to mute or unmute.");
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
        auto& pad = stepPads[static_cast<std::size_t>(i)]; pad = std::make_unique<StepPad>(i);
        pad->setComponentID("trig-" + juce::String(i + 1)); pad->addKeyListener(this);
        pad->onStepClick = [this, i](bool toggle)
        {
            if (trkButton.getToggleState()) { selectTrack(i); trkButton.setToggleState(false, juce::dontSendNotification); }
            else if (gridRecording) selectStep(selectedPage * 16 + i, toggle);
            else if (toggle) processor.triggerTrack(i);
            else selectTrack(i);
        };
        panel->addAndMakeVisible(*pad);
    }
    for (int i = 0; i < 8; ++i)
    {
        auto& b = pageButtons[static_cast<std::size_t>(i)]; b.setButtonText(juce::String(i + 1));
        addButton(b, false, "seq-page-" + juce::String(i + 1), "Edit steps " + juce::String(i * 16 + 1) + "-" + juce::String(i * 16 + 16) + ". The lit marker below indicates the page currently playing.");
        b.onClick = [this, i] { selectSequencerPage(i); };
    }
    patternLength.setRange(1, 128, 1); patternLength.setSliderStyle(juce::Slider::LinearHorizontal);
    patternLength.setTextBoxStyle(juce::Slider::TextBoxRight, false, 38, 23);
    patternLength.setComponentID("track-length"); patternLength.addKeyListener(this);
    patternLength.setTooltip("Length of this track, 1-128 steps. Other tracks keep their own length.");
    patternLength.onValueChange = [this]
    {
        if (!refreshing) { processor.setTrackLength(selectedTrack, juce::roundToInt(patternLength.getValue())); refreshSteps(); }
    };
    panel->addAndMakeVisible(patternLength);
    editScope.addItem("STEP", 1); editScope.addItem("PAGE", 2); editScope.addItem("TRACK SEQUENCE", 3);
    editScope.setSelectedId(1, juce::dontSendNotification); editScope.setComponentID("edit-scope");
    editScope.setTooltip("STEP copy/paste includes all step values; CLEAR removes only its locks. TRACK SEQUENCE includes steps and length, never the sample or sound controls.");
    editScope.addKeyListener(this); panel->addAndMakeVisible(editScope);
    editScope.onChange = [this] { clearButton.setButtonText(editScope.getSelectedId() == 1 ? "CLEAR LOCKS" : editScope.getSelectedId() == 2 ? "CLEAR PAGE" : "CLEAR TRACK"); panel->repaint(); };
    for (auto* label : {&sampleLabel, &sampleInfoLabel, &statusLabel, &helpLabel})
    {
        label->setColour(juce::Label::textColourId, mutedInk); label->setFont(font(label == &sampleLabel ? 15.0f : 11.0f, label == &sampleLabel));
        panel->addAndMakeVisible(*label);
    }
    sampleLabel.setColour(juce::Label::textColourId, ink);
    helpLabel.setText("MOUSE\nTRK then a pad: select a track silently. The VST strip also selects tracks.\nREC on: pads edit steps; right-click selects without toggling.\nREC off: pads play tracks; right-click selects silently.\nClick a family again or Up/Down to switch its subpage.\n\nFUNC (latched for one command)\nREC: copy  /  PLAY: clear  /  STOP: paste\nYES: temporary save  /  NO: temporary reload  /  FX: send effects\nClipboard scope is selected in VST TOOLS. Repeat paste/clear to undo.\n\nKEYBOARD (editor focused)\n1-8 / Q W E R T Y U I: pads; Shift selects without toggling.\nLeft/Right: sequence page.  [ / ] or Up/Down: parameter subpage.\nSpace: internal play/pause. Ctrl+C / V / Z: copy / paste / undo.\nDelete: clear scope. Escape: back. Shortcuts pause while typing.\n\n* marks an adapted control. Disabled controls are not implemented.\nIn HOST SYNC use Live's transport. STOP pauses without rewinding.", juce::dontSendNotification);
    helpLabel.setJustificationType(juce::Justification::topLeft);
    helpLabel.setColour(juce::Label::backgroundColourId, panelColour);
    helpLabel.setOpaque(true); helpLabel.setInterceptsMouseClicks(true, true); helpLabel.setVisible(false);
    previousPageButton.onClick = [this] { changeParameterPage(-1); };
    nextPageButton.onClick = [this] { changeParameterPage(1); };
    gridButton.setToggleState(true, juce::dontSendNotification);
    gridButton.onClick = [this]
    {
        if (funcButton.getToggleState()) { gridButton.setToggleState(gridRecording, juce::dontSendNotification); editSelection(0); funcButton.setToggleState(false, juce::dontSendNotification); return; }
        gridRecording = gridButton.getToggleState(); refreshSteps(); panel->repaint();
    };
    runButton.onClick = [this]
    {
        if (funcButton.getToggleState()) { editSelection(2); funcButton.setToggleState(false, juce::dontSendNotification); }
        else internalPlayPause();
        timerCallback();
    };
    stopButton.onClick = [this]
    {
        if (funcButton.getToggleState()) { editSelection(1); funcButton.setToggleState(false, juce::dontSendNotification); }
        else if (processor.isUsingHostClock()) showStatus("Use Live's transport while HOST CLOCK is active.");
        else processor.setParameter("play", 0.0f);
        timerCallback();
    };
    yesButton.onClick = [this]
    {
        if (funcButton.getToggleState()) { temporarySaveButton.onClick(); funcButton.setToggleState(false, juce::dontSendNotification); }
        else showStatus("YES confirms menus. No internal browser is implemented yet.");
    };
    pageButton.onClick = [this] { selectSequencerPage((selectedPage + 1) % 8); };
    leftButton.onClick = [this] { selectSequencerPage((selectedPage + 7) % 8); };
    rightButton.onClick = [this] { selectSequencerPage((selectedPage + 1) % 8); };
    toolsButton.onClick = [this] { toolsVisible = toolsButton.getToggleState(); updateVisibility(); panel->repaint(); };
    trkButton.onClick = [this]
    {
        if (funcButton.getToggleState()) { toolsVisible = true; toolsButton.setToggleState(true, juce::dontSendNotification); trkButton.setToggleState(false, juce::dontSendNotification); funcButton.setToggleState(false, juce::dontSendNotification); updateVisibility(); }
    };
    stepToolsButton.onClick = [this] { showView(view == View::StepTools ? View::Parameters : View::StepTools); };
    sendFxButton.onClick = [this] { showView(view == View::SendFx ? View::Parameters : View::SendFx); };
    noButton.onClick = [this]
    {
        if (funcButton.getToggleState()) { temporaryReloadButton.onClick(); funcButton.setToggleState(false, juce::dontSendNotification); }
        else goBack();
    };
    helpButton.onClick = [this] { helpVisible = helpButton.getToggleState(); updateVisibility(); panel->repaint(); };
    pitchLockButton.onClick = [this]
    {
        if (!refreshing) changeStep([this](takt::Step& s)
        { s.lockPitch = pitchLockButton.getToggleState(); if (!s.lockPitch) s.pitch = 0.0f; });
    };
    cutoffLockButton.onClick = [this] { changeStep([this](takt::Step& s) { s.lockCutoff = cutoffLockButton.getToggleState(); }); };
    copyButton.onClick = [this] { editSelection(0); }; pasteButton.onClick = [this] { editSelection(1); };
    clearButton.onClick = [this] { editSelection(2); }; undoButton.onClick = [this] { undoEdit(); };
    temporarySaveButton.onClick = [this] { processor.temporarySavePattern(); showStatus("Temporary checkpoint saved for this session."); };
    temporaryReloadButton.onClick = [this] { processor.temporaryReloadPattern(); refreshControls(); timerCallback(); showStatus("Pattern restored. Transport and master remain unchanged."); };
    demoButton.onClick = [this] { processor.loadDemoPattern(); timerCallback(); showStatus("Demo loaded. Use PLAY with the internal clock to listen."); };
    importButton.onClick = [this] { chooseSample(); }; triggerButton.onClick = [this] { processor.triggerTrack(selectedTrack); };
    layoutPanel(); selectTrack(0);
    setResizable(true, true); setResizeLimits(720, 624, 1350, 1170);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(designWidth) / designHeight);
    setSize(designWidth, designHeight); timerCallback(); startTimerHz(30);
}

TaktAudioProcessorEditor::~TaktAudioProcessorEditor()
{
    stopTimer(); fileChooser.reset(); setLookAndFeel(nullptr);
}

void TaktAudioProcessorEditor::paint(juce::Graphics& g) { g.fillAll(background); }

void TaktAudioProcessorEditor::resized()
{
    const auto scale = juce::jmin(static_cast<float>(getWidth()) / designWidth, static_cast<float>(getHeight()) / designHeight);
    panel->setTransform(juce::AffineTransform::scale(scale));
    panel->setTopLeftPosition(juce::roundToInt((static_cast<float>(getWidth()) - designWidth * scale) * 0.5f),
                             juce::roundToInt((static_cast<float>(getHeight()) - designHeight * scale) * 0.5f));
}

void TaktAudioProcessorEditor::layoutPanel()
{
    // Compact software panel, preserving the supplied front-panel hierarchy.
    transportDials[2]->present("", true, "Main output volume. Preserved master automation; unlike track LEVEL, affects the entire instrument.");
    transportDials[2]->setBounds(66, 81, 94, 88);
    trackLevel->present("", true, "Selected-track LEVEL. Distinct from hardware SRC LEV / AMP VOL, which are not implemented yet.");
    trackLevel->setBounds(66, 188, 94, 88);
    for (int i = 0; i < 8; ++i)
        encoders[static_cast<std::size_t>(i)]->setBounds(459 + (i % 4) * 104, 77 + (i / 4) * 107, 89, 97);
    for (int i = 0; i < 6; ++i)
        familyButtons[static_cast<std::size_t>(i)].setBounds(460 + i * 63, 308, 54, 35);
    funcButton.setBounds(77, 311, 71, 38);
    unavailableButtons[0].setBounds(78, 380, 70, 38);
    trkButton.setBounds(78, 448, 70, 38);
    unavailableButtons[1].setBounds(78, 524, 70, 38);
    unavailableButtons[2].setBounds(78, 601, 70, 38);
    gridButton.setBounds(179, 411, 60, 36);
    for (int i = 0; i < 4; ++i) centreButtons[static_cast<std::size_t>(i)].setBounds(179 + i * 69, 363, 54, 31);
    runButton.setBounds(249, 411, 60, 36);
    stopButton.setBounds(319, 411, 60, 36);
    yesButton.setBounds(459, 367, 53, 42);
    noButton.setBounds(459, 429, 53, 42);
    previousPageButton.setBounds(578, 362, 42, 33);
    leftButton.setBounds(528, 402, 42, 33);
    nextPageButton.setBounds(578, 402, 42, 33);
    rightButton.setBounds(628, 402, 42, 33);
    pageButton.setBounds(769, 385, 60, 36);
    for (int i = 0; i < 8; ++i)
        pageButtons[static_cast<std::size_t>(i)].setBounds(673 + i * 20, 436, 17, 19);
    for (int i = 0; i < 16; ++i)
        stepPads[static_cast<std::size_t>(i)]->setBounds(182 + (i % 8) * 81, 505 + (i / 8) * 83, 66, 65);
    sampleLabel.setBounds(177, 251, 249, 20);
    sampleInfoLabel.setBounds(177, 273, 249, 16);
    waveform->setBounds(177, 158, 249, 80);
    helpLabel.setBounds(165, 304, 673, 350);
    drawerBackdrop.setBounds(163, 304, 681, 178);
    drawerBackdrop.setVisible(false);
    transportDials[0]->setBounds(178, 316, 83, 84);
    transportDials[1]->setBounds(265, 316, 83, 84);
    patternLength.setBounds(355, 353, 151, 23);
    editScope.setBounds(520, 349, 158, 26);
    copyButton.setBounds(522, 390, 87, 27);
    pasteButton.setBounds(617, 390, 87, 27);
    clearButton.setBounds(712, 390, 116, 27);
    temporarySaveButton.setBounds(178, 432, 122, 28);
    temporaryReloadButton.setBounds(308, 432, 130, 28);
    muteButton.setBounds(448, 432, 73, 28);
    reverseButton.setBounds(529, 432, 83, 28);
    loopButton.setBounds(620, 432, 73, 28);
    demoButton.setBounds(703, 432, 125, 28);
    for (int i = 0; i < 16; ++i)
        trackPads[static_cast<std::size_t>(i)]->setBounds(85 + i * 48, 684, 44, 26);
    importButton.setBounds(53, 720, 119, 28);
    triggerButton.setBounds(181, 720, 82, 28);
    hostButton.setBounds(272, 720, 94, 28);
    stepToolsButton.setBounds(375, 720, 106, 28);
    sendFxButton.setBounds(490, 720, 85, 28);
    undoButton.setBounds(584, 720, 64, 28);
    toolsButton.setBounds(657, 720, 130, 28);
    helpButton.setBounds(796, 720, 37, 28);
    pitchLockButton.setBounds(177, 289, 120, 20);
    cutoffLockButton.setBounds(305, 289, 120, 20);
    statusLabel.setBounds(23, 756, 854, 19);
}

void TaktAudioProcessorEditor::paintPanel(juce::Graphics& g)
{
    g.fillAll(background);
    const auto shell = juce::Rectangle<float>(52.0f, 12.0f, 796.0f, 660.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff303236), 52, 12,
                                         juce::Colour(0xff202226), 848, 672, false));
    g.fillRoundedRectangle(shell, 7.0f);
    g.setColour(border); g.drawRoundedRectangle(shell.reduced(0.5f), 7.0f, 1.0f);
    for (const auto point : {juce::Point<float>(74, 34), {826, 34}, {74, 652}, {826, 652}})
    {
        g.setColour(juce::Colour(0xff0b0d0f)); g.fillEllipse(point.x - 5, point.y - 5, 10, 10);
        g.setColour(border.brighter()); g.drawLine(point.x - 3, point.y, point.x + 3, point.y, 1);
    }
    drawCaption(g, "TAKT II", {88, 25, 285, 24}, 16, ink, true);
    drawCaption(g, "SAMPLE / SEQUENCE", {497, 28, 318, 20}, 10, mutedInk, false, juce::Justification::centredRight);
    const auto host = processor.isUsingHostClock();
    const auto playing = host ? processor.isHostPlaying() : processor.parameterValue("play") >= 0.5f;
    const std::array<const char*, 6> familyNames{{"TRIG", "SRC", "FLTR", "AMP", "FX", "MOD"}};
    const auto context = view == View::StepTools ? juce::String("STEP TOOLS*")
                       : view == View::SendFx ? juce::String("SEND FX*")
                       : juce::String(familyNames[static_cast<std::size_t>(family)]);
    const auto sourceWave = view == View::Parameters && family == Family::Source && parameterPages[1] == 1;
    g.setColour(juce::Colour(0xff090b0e)); g.fillRoundedRectangle(166, 76, 271, 220, 5);
    g.setColour(juce::Colours::black); g.fillRect(174, 84, 255, 162);
    drawCaption(g, "T" + number(selectedTrack + 1) + "  " + (gridRecording ? "GRID" : "TRACKS"), {181, 89, 147, 15}, 10, ink, true);
    drawCaption(g, host ? juce::String("DAW") : juce::String(processor.parameterValue("tempo"), 1), {340, 89, 81, 15}, 10, ink, true, juce::Justification::centredRight);
    g.setColour(ink.withAlpha(0.4f)); g.drawHorizontalLine(107, 180, 422);
    const auto pageCaption = view == View::Parameters
        ? "  " + juce::String(parameterPages[static_cast<std::size_t>(family)] + 1) + "/" + juce::String(parameterPageCount()) : juce::String{};
    drawCaption(g, context + pageCaption,
                {181, 111, 240, 17}, 10, ink, true);
    if (!sourceWave)
    {
        for (int i = 0; i < 8; ++i)
        {
            const int x = 181 + (i % 4) * 60, y = 139 + (i / 4) * 45;
            auto& dial = *encoders[static_cast<std::size_t>(i)];
            drawCaption(g, dial.caption(), {x, y, 58, 14}, 8.5f,
                        dial.slider.isEnabled() ? ink : mutedInk, false, juce::Justification::centred);
            drawCaption(g, dial.slider.getTextFromValue(dial.slider.getValue()), {x, y + 14, 58, 18}, 10.5f,
                        dial.slider.isEnabled() ? ink : mutedInk, true, juce::Justification::centred);
        }
    }
    drawCaption(g, "STEP " + number(selectedStep + 1) + "  LEN " + juce::String(processor.getTrackLength(selectedTrack)),
                {181, 226, 240, 15}, 9, ink);
    for (int i = 0; i < 8; ++i)
        drawCaption(g, juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)),
                    {461 + (i % 4) * 104, 174 + (i / 4) * 107, 86, 16}, 12, ink, true, juce::Justification::centred);
    drawCaption(g, "MAIN VOLUME", {64, 170, 95, 16}, 8, ink, false, juce::Justification::centred);
    drawCaption(g, "LEVEL / DATA", {64, 280, 95, 16}, 8, ink, false, juce::Justification::centred);
    drawCaption(g, "COPY", {179, 452, 60, 17}, 9, accent, false, juce::Justification::centred);
    drawCaption(g, "CLEAR", {249, 452, 60, 17}, 9, accent, false, juce::Justification::centred);
    drawCaption(g, "PASTE", {319, 452, 60, 17}, 9, accent, false, juce::Justification::centred);
    drawCaption(g, "SAVE", {460, 410, 53, 15}, 8, accent, false, juce::Justification::centred);
    drawCaption(g, "RELOAD", {458, 475, 56, 15}, 8, accent, false, juce::Justification::centred);
    drawCaption(g, "SELECT", {78, 488, 70, 15}, 8, accent, false, juce::Justification::centred);
    drawCaption(g, "BANK", {78, 565, 70, 15}, 8, mutedInk, false, juce::Justification::centred);
    drawCaption(g, "EDIT", {78, 641, 70, 15}, 8, mutedInk, false, juce::Justification::centred);
    drawCaption(g, "SEND FX", {710, 347, 69, 14}, 8, accent, false, juce::Justification::centred);
    drawCaption(g, gridRecording ? "GRID RECORDING" : "TRIG TRACKS", {180, 482, 249, 16}, 9, gridRecording ? trigRed : ink, true);
    const auto current = processor.getCurrentStep(selectedTrack);
    const auto playedPage = playing && current >= 0 ? current / 16 : -1;
    drawCaption(g, "EDIT " + juce::String(selectedPage + 1) + " / PLAY " + (playedPage >= 0 ? juce::String(playedPage + 1) : "--"),
                {660, 461, 170, 17}, 8, mutedInk, false, juce::Justification::centredRight);
    if (playedPage >= 0) { g.setColour(trigRed); g.fillEllipse(static_cast<float>(678 + playedPage * 20), 426, 5, 5); }
    drawCaption(g, host ? "DAW CLOCK" : "INTERNAL CLOCK", {176, 55, 157, 16}, 8.5f, mutedInk);
    drawCaption(g, playing ? "PLAY" : "PAUSE", {357, 55, 67, 16}, 8.5f, playing ? accent : mutedInk, true, juce::Justification::centredRight);
    g.setColour(border); g.fillRoundedRectangle(758, 647, 70, 4, 1);
    g.setColour(displayedPeak > 0.97f ? trigRed : accent); g.fillRoundedRectangle(758, 647, 70 * juce::jlimit(0.0f, 1.0f, displayedPeak), 4, 1);
    drawCaption(g, "VST", {53, 685, 29, 23}, 9, mutedInk, true);
}

void TaktAudioProcessorEditor::updateVisibility()
{
    const bool drawer = toolsVisible && !helpVisible;
    drawerBackdrop.setVisible(drawer);
    if (drawer) drawerBackdrop.toFront(false);
    for (auto* dial : {transportDials[0].get(), transportDials[1].get()})
    { dial->setVisible(drawer); if (drawer) dial->toFront(false); }
    for (auto* control : std::initializer_list<juce::Component*>{&patternLength, &editScope, &copyButton, &pasteButton, &clearButton,
                     &temporarySaveButton, &temporaryReloadButton, &muteButton, &reverseButton, &loopButton, &demoButton})
    { control->setVisible(drawer); if (drawer) control->toFront(false); }
    waveform->setVisible(!helpVisible && view == View::Parameters && family == Family::Source && parameterPages[1] == 1);
    pitchLockButton.setVisible(!drawer && !helpVisible && view == View::StepTools);
    cutoffLockButton.setVisible(!drawer && !helpVisible && view == View::StepTools);
    helpLabel.setVisible(helpVisible); if (helpVisible) helpLabel.toFront(false);
}

void TaktAudioProcessorEditor::internalPlayPause()
{
    if (processor.isUsingHostClock()) showStatus("Use Live's transport while HOST CLOCK is active.");
    else processor.setParameter("play", processor.parameterValue("play") >= 0.5f ? 0.0f : 1.0f);
}

int TaktAudioProcessorEditor::parameterPageCount() const
{
    const std::array<int, 6> counts{{2, 2, 2, 1, 1, 3}};
    return counts[static_cast<std::size_t>(family)];
}

void TaktAudioProcessorEditor::formatSlider(juce::Slider& slider, ValueFormat type)
{
    slider.setTextValueSuffix({});
    switch (type)
    {
        case ValueFormat::Percent: percentDisplay(slider); break;
        case ValueFormat::Integer: numberDisplay(slider, 0); break;
        case ValueFormat::Pitch: numberDisplay(slider, 1, " st"); break;
        case ValueFormat::Hertz: numberDisplay(slider, 0, " Hz"); break;
        case ValueFormat::Seconds: numberDisplay(slider, 2, " s"); break;
        case ValueFormat::Beats: numberDisplay(slider, 2, " beats"); break;
        case ValueFormat::Milliseconds:
            slider.textFromValueFunction = [](double value) { return juce::String(value * 1000.0, 1) + " ms"; };
            slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue() / 1000.0; };
            slider.updateText(); break;
        case ValueFormat::Number: numberDisplay(slider, 1); break;
    }
}

void TaktAudioProcessorEditor::bindUnavailable(int slot, const juce::String& label, const juce::String& reason)
{
    auto& dial = *encoders[static_cast<std::size_t>(slot)];
    dial.present(label.isEmpty() ? "--" : label, false, reason);
    dial.slider.setNormalisableRange(juce::NormalisableRange<double>(0.0, 1.0));
    dial.slider.setValue(0, juce::dontSendNotification);
    dial.slider.textFromValueFunction = [](double) { return juce::String("--"); }; dial.slider.updateText();
}

void TaktAudioProcessorEditor::bindParameter(int slot, const juce::String& name, const juce::String& label,
                                            ValueFormat format, const juce::String& tip, bool global)
{
    const auto index = static_cast<std::size_t>(slot);
    auto& dial = *encoders[index]; dial.present(label, true, tip);
    const auto id = global ? name : TaktAudioProcessor::trackParameterID(selectedTrack, name);
    bindings[index] = {BindingKind::Parameter, id, {}};
    dial.slider.getProperties().set("parameterID", id);
    controlAttachments.emplace_back(std::make_unique<SliderAttachment>(processor.parameters, id, dial.slider));
    formatSlider(dial.slider, format);
}

void TaktAudioProcessorEditor::bindStep(int slot, const juce::String& field, const juce::String& label,
                                       double low, double high, double interval, ValueFormat format, const juce::String& tip)
{
    const auto index = static_cast<std::size_t>(slot);
    auto& dial = *encoders[index]; dial.present(label, true, tip);
    bindings[index] = {BindingKind::Step, {}, field}; dial.slider.getProperties().set("stepField", field);
    dial.slider.setNormalisableRange(juce::NormalisableRange<double>(low, high, interval));
    if (field == "cutoff") dial.slider.setSkewFactorFromMidPoint(1000);
    formatSlider(dial.slider, format);
    dial.slider.onValueChange = [this, index, field]
    {
        if (refreshing) return;
        const auto value = static_cast<float>(encoders[index]->slider.getValue());
        changeStep([field, value](takt::Step& s)
        {
            if (field == "velocity") s.velocity = value;
            else if (field == "probability") s.probability = value;
            else if (field == "pitch") { s.pitch = value; s.lockPitch = true; }
            else if (field == "cutoff") { s.cutoff = value; s.lockCutoff = true; }
            else if (field == "every") { s.conditionEvery = juce::roundToInt(value); s.conditionOffset = juce::jmin(s.conditionOffset, s.conditionEvery - 1); }
            else if (field == "offset") s.conditionOffset = juce::jmin(juce::roundToInt(value), s.conditionEvery - 1);
            else if (field == "retrigs") s.retrigs = juce::roundToInt(value);
            else if (field == "microtiming") s.microtiming = value;
        });
    };
}

void TaktAudioProcessorEditor::bindPlayback(int slot)
{
    const auto index = static_cast<std::size_t>(slot); auto& dial = *encoders[index];
    dial.present("PLAY*", true, "Existing player: forward/reverse, one-shot/loop. The loop uses START/END, without the hardware's independent LOOP point.");
    bindings[index].kind = BindingKind::Playback;
    dial.slider.setNormalisableRange(juce::NormalisableRange<double>(0.0, 3.0, 1.0));
    dial.slider.textFromValueFunction = [](double value)
    {
        const std::array<const char*, 4> names{{"FWD", "FWD LOOP", "REV", "REV LOOP"}};
        return juce::String(names[static_cast<std::size_t>(juce::jlimit(0, 3, juce::roundToInt(value)))]);
    };
    dial.slider.valueFromTextFunction = [](const juce::String& value)
    { return static_cast<double>((value.containsIgnoreCase("REV") ? 2 : 0) + (value.containsIgnoreCase("LOOP") ? 1 : 0)); };
    // A rebind can keep value zero: force the text box to use this new formatter.
    dial.slider.updateText();
    dial.slider.onValueChange = [this, index]
    {
        if (refreshing) return;
        const auto value = juce::roundToInt(encoders[index]->slider.getValue());
        processor.setParameter(TaktAudioProcessor::trackParameterID(selectedTrack, "reverse"), value >= 2 ? 1.0f : 0.0f);
        processor.setParameter(TaktAudioProcessor::trackParameterID(selectedTrack, "loop"), value % 2 ? 1.0f : 0.0f);
    };
}

void TaktAudioProcessorEditor::rebuildControls()
{
    refreshing = true;
    controlAttachments.clear();
    for (std::size_t i = 0; i < encoders.size(); ++i)
    {
        auto& slider = encoders[i]->slider;
        slider.onValueChange = {}; slider.textFromValueFunction = {}; slider.valueFromTextFunction = {};
        slider.setTextValueSuffix({}); slider.setDoubleClickReturnValue(false, 0);
        slider.getProperties().set("parameterID", juce::String{}); slider.getProperties().set("stepField", juce::String{});
        bindings[i] = {}; bindUnavailable(static_cast<int>(i), {}, "No parameter in this position.");
    }
    const auto missing = [this](int slot, const char* name) { bindUnavailable(slot, name, juce::String(name) + " is not implemented in this version."); };
    if (view == View::StepTools)
    {
        bindStep(0, "pitch", "LOCK PITCH*", -48, 48, 0.01, ValueFormat::Pitch, "Selected-step sample pitch override. Editing enables PITCH LOCK. Not the hardware NOTE parameter.");
        bindStep(1, "cutoff", "LOCK CUTOFF", 20, 20000, 1, ValueFormat::Hertz, "Selected-step cutoff override. Editing enables FILTER LOCK.");
        bindStep(2, "every", "EVERY*", 1, 64, 1, ValueFormat::Integer, "Play once every N pattern passes. This legacy cycle control is not the complete hardware COND system.");
        bindStep(3, "offset", "OFFSET*", 0, 63, 1, ValueFormat::Integer, "Pass within the EVERY cycle, starting at zero.");
        bindStep(4, "retrigs", "REPEAT COUNT*", 1, 8, 1, ValueFormat::Integer, "Legacy number of repeats spread across one step. This is not the hardware RATE or retrig length.");
        bindStep(5, "microtiming", "MICROTIMING*", -0.49, 0.49, 0.01, ValueFormat::Percent, "Move this step earlier/later, measured as a fraction of one step.");
        bindStep(6, "velocity", "VEL", 0, 1, 0.01, ValueFormat::Percent, "Velocity of the selected step.");
        bindStep(7, "probability", "PROB", 0, 1, 0.01, ValueFormat::Percent, "Probability of the selected step.");
    }
    else if (view == View::SendFx)
    {
        bindParameter(0, "delayMix", "DELAY MIX*", ValueFormat::Percent, "Shared delay return level.", true);
        bindParameter(1, "feedback", "FDBK", ValueFormat::Percent, "Shared delay feedback.", true);
        bindParameter(2, "delayBeats", "TIME*", ValueFormat::Beats, "Legacy delay time in beats, not the hardware's 128th-note values.", true);
        bindParameter(3, "reverbMix", "REVERB MIX*", ValueFormat::Percent, "Shared reverb return level.", true);
        missing(4, "CHORUS"); missing(5, "DELAY FILTER"); missing(6, "REVERB SIZE"); missing(7, "COMPRESSOR");
    }
    else switch (family)
    {
        case Family::Trig:
            if (parameterPages[0] == 0)
            {
                missing(0, "NOTE"); bindStep(1, "velocity", "VEL", 0, 1, 0.01, ValueFormat::Percent, "Velocity of the selected step, not a track default.");
                missing(2, "LEN"); bindStep(3, "probability", "PROB", 0, 1, 0.01, ValueFormat::Percent, "Probability of the selected step, not a track default.");
                missing(4, "LFO.T"); missing(5, "FLT.T"); missing(6, "FILL"); missing(7, "COND");
            }
            else { missing(0, "RTRG"); missing(1, "VFAD"); missing(2, "LEN"); missing(3, "RATE"); missing(6, "PTIM"); missing(7, "PORT"); }
            break;
        case Family::Source:
            bindParameter(0, "pitch", "TUNE", ValueFormat::Pitch, "Sample tuning in semitones. This does not alias the hardware NOTE parameter."); bindPlayback(1);
            missing(3, "SAMP");
            bindParameter(4, "start", "START*", ValueFormat::Percent, "Absolute sample start as a percentage; preserved from the first version.");
            bindParameter(5, "end", "END*", ValueFormat::Percent, "Absolute sample end, not the hardware LEN. Existing end automation is preserved.");
            missing(6, "LOOP"); missing(7, "LEV"); break;
        case Family::Filter:
            if (parameterPages[2] == 0)
            {
                missing(0, "ATK"); missing(1, "DEC"); missing(2, "SUS"); missing(3, "REL");
                bindParameter(4, "cutoff", "FREQ", ValueFormat::Hertz, "Low-pass filter cutoff.");
                bindParameter(5, "resonance", "RESO", ValueFormat::Percent, "Low-pass filter resonance."); missing(6, "TYPE"); missing(7, "ENV");
            }
            else { missing(0, "DEL"); missing(3, "KEY.T"); missing(4, "BASE"); missing(5, "WIDTH"); missing(6, "BW.RT"); missing(7, "RSET"); }
            break;
        case Family::Amp:
            bindParameter(0, "attack", "ATK", ValueFormat::Milliseconds, "Attack of the existing attack/decay envelope. HOLD, sustain and release are not implemented.");
            bindParameter(1, "decay", "DEC", ValueFormat::Seconds, "Decay of the existing attack/decay envelope."); missing(2, "SUS"); missing(3, "REL"); missing(4, "RSET"); missing(5, "MODE");
            bindParameter(6, "pan", "PAN", ValueFormat::Number, "Stereo position, -1 left to +1 right."); missing(7, "VOL"); break;
        case Family::Fx:
            bindParameter(0, "bitDepth", "BR*", ValueFormat::Integer, "Legacy bit resolution 4-24 bits. The hardware BR range is 1-16 bits; existing states and automation are unchanged.");
            bindParameter(1, "drive", "OVER", ValueFormat::Percent, "Track overdrive amount."); missing(2, "SRR"); missing(3, "ROUT");
            bindParameter(4, "delaySend", "DEL", ValueFormat::Percent, "Track send to the shared delay.");
            bindParameter(5, "reverbSend", "REV", ValueFormat::Percent, "Track send to the shared reverb."); missing(6, "CHR"); missing(7, "OD.RT"); break;
        case Family::Mod:
            for (int i = 0; i < 8; ++i)
            { const std::array<const char*, 8> names{{"SPD", "MULT", "FADE", "DEST", "WAVE", "SPH", "MODE", "DEP"}}; missing(i, names[static_cast<std::size_t>(i)]); }
            break;
    }
    for (int i = 0; i < 6; ++i) familyButtons[static_cast<std::size_t>(i)].setToggleState(view == View::Parameters && static_cast<int>(family) == i, juce::dontSendNotification);
    previousPageButton.setEnabled(view == View::Parameters && parameterPageCount() > 1);
    nextPageButton.setEnabled(view == View::Parameters && parameterPageCount() > 1);
    stepToolsButton.setToggleState(view == View::StepTools, juce::dontSendNotification); sendFxButton.setToggleState(view == View::SendFx, juce::dontSendNotification);
    pitchLockButton.setVisible(view == View::StepTools); cutoffLockButton.setVisible(view == View::StepTools);
    refreshing = false; refreshControls(); updateVisibility(); panel->repaint();
}

void TaktAudioProcessorEditor::selectTrack(int track)
{
    refreshing = true; selectedTrack = juce::jlimit(0, takt::numTracks - 1, track);
    levelAttachment.reset(); trackButtonAttachments.clear();
    const auto levelID = TaktAudioProcessor::trackParameterID(selectedTrack, "gain");
    trackLevel->slider.getProperties().set("parameterID", levelID);
    levelAttachment = std::make_unique<SliderAttachment>(processor.parameters, levelID, trackLevel->slider);
    formatSlider(trackLevel->slider, ValueFormat::Percent);
    trackButtonAttachments.emplace_back(std::make_unique<ButtonAttachment>(processor.parameters, TaktAudioProcessor::trackParameterID(selectedTrack, "reverse"), reverseButton));
    trackButtonAttachments.emplace_back(std::make_unique<ButtonAttachment>(processor.parameters, TaktAudioProcessor::trackParameterID(selectedTrack, "mute"), muteButton));
    trackButtonAttachments.emplace_back(std::make_unique<ButtonAttachment>(processor.parameters, TaktAudioProcessor::trackParameterID(selectedTrack, "loop"), loopButton));
    patternLength.setValue(processor.getTrackLength(selectedTrack), juce::dontSendNotification);
    lastSample.reset(); lastSampleName.clear(); waveform->setSample({}); sampleLabel.setText("No sample loaded", juce::dontSendNotification);
    sampleInfoLabel.setText("WAV / AIFF / FLAC | Import or drop your audio", juce::dontSendNotification);
    refreshing = false; rebuildControls(); refreshSteps(); timerCallback();
}

void TaktAudioProcessorEditor::selectFamily(Family f)
{
    if (funcButton.getToggleState() && f == Family::Fx)
    {
        funcButton.setToggleState(false, juce::dontSendNotification); showView(View::SendFx); return;
    }
    if (view == View::Parameters && family == f) parameterPages[static_cast<std::size_t>(family)] = (parameterPages[static_cast<std::size_t>(family)] + 1) % parameterPageCount();
    family = f; view = View::Parameters; helpVisible = false; helpButton.setToggleState(false, juce::dontSendNotification);
    helpLabel.setVisible(false); rebuildControls();
}

void TaktAudioProcessorEditor::changeParameterPage(int delta)
{
    if (view != View::Parameters) return;
    auto& page = parameterPages[static_cast<std::size_t>(family)]; page = (page + delta + parameterPageCount()) % parameterPageCount(); rebuildControls();
}

void TaktAudioProcessorEditor::selectSequencerPage(int page)
{
    selectedPage = juce::jlimit(0, 7, page); selectedStep = selectedPage * 16;
    refreshSteps(); refreshControls(); panel->repaint();
}

void TaktAudioProcessorEditor::showView(View v) { view = v; rebuildControls(); }

void TaktAudioProcessorEditor::goBack()
{
    if (helpVisible)
    { helpVisible = false; helpButton.setToggleState(false, juce::dontSendNotification); }
    else if (toolsVisible) { toolsVisible = false; toolsButton.setToggleState(false, juce::dontSendNotification); }
    else if (view != View::Parameters) showView(View::Parameters);
    funcButton.setToggleState(false, juce::dontSendNotification); trkButton.setToggleState(false, juce::dontSendNotification);
    updateVisibility(); panel->repaint();
}

void TaktAudioProcessorEditor::selectStep(int step, bool toggle)
{
    selectedStep = juce::jlimit(0, takt::maxSteps - 1, step);
    if (toggle)
    {
        auto s = processor.getStep(selectedTrack, selectedStep); s.enabled = !s.enabled; processor.setStep(selectedTrack, selectedStep, s);
        if (selectedStep >= processor.getTrackLength(selectedTrack)) showStatus("Step is beyond this track's length. Increase LENGTH to hear it.");
    }
    refreshSteps(); refreshControls(); panel->repaint();
}

void TaktAudioProcessorEditor::refreshSteps()
{
    const auto current = processor.getCurrentStep(selectedTrack), length = processor.getTrackLength(selectedTrack);
    const auto playing = processor.isUsingHostClock() ? processor.isHostPlaying() : processor.parameterValue("play") >= 0.5f;
    for (int i = 0; i < 16; ++i)
    {
        auto& pad = *stepPads[static_cast<std::size_t>(i)]; pad.grid = gridRecording;
        pad.index = gridRecording ? selectedPage * 16 + i : i;
        const auto s = processor.getStep(selectedTrack, selectedPage * 16 + i);
        pad.enabled = gridRecording && s.enabled;
        pad.selected = gridRecording ? selectedStep == pad.index : selectedTrack == i;
        pad.playing = playing && gridRecording && current == pad.index; pad.hasLock = s.lockPitch || s.lockCutoff;
        pad.withinLength = !gridRecording || pad.index < length;
        pad.setTooltip(gridRecording ? "Click to toggle this step. Right-click or modifier-click to select without toggling." : "Click to play this track. Right-click or modifier-click selects it silently.");
        pad.repaint();
    }
    for (int i = 0; i < 8; ++i) pageButtons[static_cast<std::size_t>(i)].setToggleState(i == selectedPage, juce::dontSendNotification);
    patternLength.setValue(length, juce::dontSendNotification);
}

void TaktAudioProcessorEditor::refreshControls()
{
    const auto wasRefreshing = refreshing; refreshing = true;
    const auto s = processor.getStep(selectedTrack, selectedStep);
    for (std::size_t i = 0; i < bindings.size(); ++i)
    {
        auto& slider = encoders[i]->slider; if (slider.isMouseButtonDown()) continue;
        if (bindings[i].kind == BindingKind::Step)
        {
            const auto& field = bindings[i].stepField; double value = 0;
            if (field == "velocity") value = s.velocity; else if (field == "probability") value = s.probability;
            else if (field == "pitch") value = s.pitch; else if (field == "cutoff") value = s.cutoff;
            else if (field == "every") value = s.conditionEvery; else if (field == "offset") value = s.conditionOffset;
            else if (field == "retrigs") value = s.retrigs; else if (field == "microtiming") value = s.microtiming;
            slider.setValue(value, juce::dontSendNotification);
        }
        else if (bindings[i].kind == BindingKind::Playback)
        {
            const auto reverse = processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "reverse")) >= 0.5f;
            const auto loop = processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "loop")) >= 0.5f;
            slider.setValue((reverse ? 2 : 0) + (loop ? 1 : 0), juce::dontSendNotification);
        }
    }
    pitchLockButton.setToggleState(s.lockPitch, juce::dontSendNotification); cutoffLockButton.setToggleState(s.lockCutoff, juce::dontSendNotification);
    refreshing = wasRefreshing;
}

void TaktAudioProcessorEditor::changeStep(const std::function<void(takt::Step&)>& edit)
{
    if (refreshing) return;
    auto step = processor.getStep(selectedTrack, selectedStep); edit(step);
    processor.setStep(selectedTrack, selectedStep, step); refreshSteps(); refreshControls(); panel->repaint();
}

void TaktAudioProcessorEditor::editSelection(int action)
{
    using Scope = TaktAudioProcessor::EditScope; using Result = TaktAudioProcessor::EditResult;
    const auto scope = editScope.getSelectedId() == 2 ? Scope::Page : editScope.getSelectedId() == 3 ? Scope::Track : Scope::Step;
    const auto result = action == 0 ? processor.copySelection(scope, selectedTrack, selectedStep, selectedPage)
                      : action == 1 ? processor.pasteSelection(scope, selectedTrack, selectedStep, selectedPage)
                                    : processor.clearSelection(scope, selectedTrack, selectedStep, selectedPage);
    if (result == Result::EmptyClipboard) showStatus("Clipboard is empty. COPY a step, page or track sequence first.", true);
    else if (result == Result::ScopeMismatch) showStatus("Clipboard type does not match this scope. Choose the same scope as the copied item.", true);
    else if (result == Result::Undone) showStatus("Previous paste or clear undone.");
    else if (result == Result::Applied) showStatus(action == 0 ? "Copied " + editScope.getText() + "." : action == 1 ? "Pasted. Repeat PASTE or use UNDO to restore." : "Cleared. Repeat CLEAR or use UNDO to restore.");
    else showStatus("This editing target is not available.", true);
    refreshSteps(); refreshControls(); timerCallback();
}

void TaktAudioProcessorEditor::undoEdit()
{
    const auto result = processor.undoLastEdit();
    showStatus(result == TaktAudioProcessor::EditResult::Undone ? "Last paste or clear undone." : "No valid paste or clear to undo.");
    refreshSteps(); refreshControls(); timerCallback();
}

bool TaktAudioProcessorEditor::editingText() const
{
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    return focused != nullptr && (dynamic_cast<juce::TextEditor*>(focused) != nullptr || focused->findParentComponentOfClass<juce::TextEditor>() != nullptr);
}

bool TaktAudioProcessorEditor::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{
    if (editingText() || (originatingComponent != nullptr
        && (dynamic_cast<juce::TextEditor*>(originatingComponent) != nullptr
            || originatingComponent->findParentComponentOfClass<juce::TextEditor>() != nullptr))) return false;
    const auto mods = key.getModifiers(); const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (mods.isCommandDown() || mods.isCtrlDown())
    {
        // On Windows CTRL key presses carry a key code but no text character.
        const auto command = juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode()));
        if (command == 'c') { editSelection(0); return true; }
        if (command == 'v') { editSelection(1); return true; }
        if (command == 'z') { undoEdit(); return true; }
        return false;
    }
    if (key == juce::KeyPress::escapeKey) { goBack(); return true; }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { editSelection(2); return true; }
    if (key == juce::KeyPress::leftKey) { selectSequencerPage((selectedPage + 7) % 8); return true; }
    if (key == juce::KeyPress::rightKey) { selectSequencerPage((selectedPage + 1) % 8); return true; }
    if (key == juce::KeyPress::upKey || ch == '[') { changeParameterPage(-1); return true; }
    if (key == juce::KeyPress::downKey || ch == ']') { changeParameterPage(1); return true; }
    if (key == juce::KeyPress::spaceKey)
    {
        internalPlayPause();
        return true;
    }
    int pad = ch >= '1' && ch <= '8' ? static_cast<int>(ch - '1') : -1;
    const auto row = juce::String("qwertyui").indexOfChar(ch); if (row >= 0) pad = row + 8;
    if (pad >= 0)
    {
        if (trkButton.getToggleState()) { selectTrack(pad); trkButton.setToggleState(false, juce::dontSendNotification); }
        else if (gridRecording) selectStep(selectedPage * 16 + pad, !mods.isShiftDown());
        else if (mods.isShiftDown()) selectTrack(pad); else processor.triggerTrack(pad);
        return true;
    }
    return false;
}

void TaktAudioProcessorEditor::timerCallback()
{
    displayedPeak = juce::jmax(processor.getOutputPeak(), displayedPeak * 0.87f);
    const auto playing = processor.isUsingHostClock() ? processor.isHostPlaying() : processor.parameterValue("play") >= 0.5f;
    runButton.setToggleState(playing, juce::dontSendNotification);
    for (int i = 0; i < takt::numTracks; ++i)
    {
        auto& pad = *trackPads[static_cast<std::size_t>(i)]; pad.selected = i == selectedTrack;
        pad.muted = processor.parameterValue(TaktAudioProcessor::trackParameterID(i, "mute")) >= 0.5f;
        pad.sampleName = processor.getSampleName(i).upToFirstOccurrenceOf(".", false, false);
        const auto current = processor.getCurrentStep(i); pad.playing = playing && current >= 0 && processor.getStep(i, current).enabled && !pad.muted; pad.repaint();
    }
    const auto sample = processor.getSample(selectedTrack); const auto name = processor.getSampleName(selectedTrack);
    if (lastSample != sample || lastSampleName != name)
    {
        lastSample = sample; lastSampleName = name; waveform->setSample(sample);
        sampleLabel.setText(name.isEmpty() ? "No sample loaded" : name, juce::dontSendNotification);
        sampleInfoLabel.setText(sample && !sample->left.empty()
            ? juce::String(sample->sampleRate / 1000.0, 1) + " kHz | " + (sample->right.empty() ? "MONO" : "STEREO") + " | " + juce::String(static_cast<double>(sample->left.size()) / sample->sampleRate, 2) + " seconds"
            : "WAV / AIFF / FLAC | Import or drop your audio", juce::dontSendNotification);
    }
    waveform->setRegion(processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "start")), processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "end")));
    refreshSteps(); refreshControls(); undoButton.setEnabled(processor.canUndoEdit());
    if (juce::Time::getMillisecondCounterHiRes() > statusExpiry)
    {
        statusLabel.setColour(juce::Label::textColourId, mutedInk);
        statusLabel.setText(gridRecording ? "GRID: click a pad to toggle a step | right-click selects | STEP TOOLS edits locks | ? shows all shortcuts" : "PLAY: pads trigger tracks | right-click selects silently | REC returns to grid editing | ? shows all shortcuts", juce::dontSendNotification);
    }
    processor.releaseUnusedSamples(); panel->repaint();
}

void TaktAudioProcessorEditor::showStatus(const juce::String& message, bool error)
{
    statusExpiry = juce::Time::getMillisecondCounterHiRes() + (error ? 14000.0 : 8000.0);
    statusLabel.setColour(juce::Label::textColourId, error ? juce::Colour(0xffef8c74) : accent); statusLabel.setText(message, juce::dontSendNotification);
}

void TaktAudioProcessorEditor::chooseSample()
{
    if (fileChooser) return;
    const auto destinationTrack = selectedTrack;
    fileChooser = std::make_unique<juce::FileChooser>("Load a sample for track " + number(destinationTrack + 1), juce::File{}, "*.wav;*.aif;*.aiff;*.flac", true);
    const juce::Component::SafePointer<TaktAudioProcessorEditor> safe(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, destinationTrack](const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            const auto file = chooser.getResult();
            if (file.existsAsFile())
            {
                juce::String error;
                if (safe->processor.loadSample(destinationTrack, file, error)) safe->showStatus("Loaded " + file.getFileName() + " on track " + number(destinationTrack + 1) + ".");
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
    for (const auto& path : files) if (juce::File(path).hasFileExtension("wav;aif;aiff;flac")) { importSample(juce::File(path)); break; }
}
