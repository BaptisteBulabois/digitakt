#include "PluginEditor.h"
#include "engine/SequencerRules.h"
#include <cmath>
#include <tuple>

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
juce::String patternName(int slot)
{
    slot = juce::jlimit(0, takt::patternSlots - 1, slot);
    return juce::String::charToString(static_cast<juce::juce_wchar>('A' + slot / 16)) + number(slot % 16 + 1);
}
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
juce::StringArray trigConditionNames()
{
    juce::StringArray names{"OFF", "PROB", "PRE", "!PRE", "NEI", "!NEI", "1ST", "!1ST", "LST", "!LST"};
    for (int inverse = 0; inverse < 2; ++inverse)
        for (int b = 1; b <= 8; ++b)
            for (int a = 1; a <= b; ++a) names.add((inverse != 0 ? "!" : "") + juce::String(a) + ":" + juce::String(b));
    return names;
}
int trigConditionIndex(const takt::sequencer::TrigRule& rule)
{
    using C = takt::sequencer::Condition;
    if (rule.condition == C::Always) return 0;
    if (rule.condition == C::Probability) return 1;
    if (rule.condition == C::Cycle)
    { const auto b = juce::jlimit(1, 8, rule.cycleB), a = juce::jlimit(1, b, rule.cycleA); return 10 + b * (b - 1) / 2 + a - 1 + (rule.inverted ? 36 : 0); }
    return 2 + (static_cast<int>(rule.condition) - static_cast<int>(C::Previous)) * 2 + (rule.inverted ? 1 : 0);
}
takt::sequencer::TrigRule trigConditionAt(int index)
{
    using C = takt::sequencer::Condition; takt::sequencer::TrigRule rule;
    if (index == 1) rule.condition = C::Probability;
    else if (index >= 2 && index <= 9) { rule.condition = static_cast<C>(static_cast<int>(C::Previous) + (index - 2) / 2); rule.inverted = (index - 2) % 2 != 0; }
    else if (index >= 10)
    { rule.condition = C::Cycle; auto offset = index - 10; rule.inverted = offset >= 36; offset %= 36;
      int b = 1; while (offset >= b && b < 8) { offset -= b; ++b; } rule.cycleA = offset + 1; rule.cycleB = b; }
    return rule;
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
        const auto lit = lockOnly ? accent : trigRed;
        g.setColour(juce::Colour(0xff080b0e)); g.fillRoundedRectangle(r, 9.0f);
        r = r.reduced(3.0f);
        g.setColour(enabled ? lit.withAlpha(over || down ? 0.48f : 0.20f) : (over ? border : juce::Colour(0xff252b31)));
        g.fillRoundedRectangle(r, 7.0f);
        g.setColour(selected ? ink.withAlpha(0.8f) : border);
        g.drawRoundedRectangle(r, 7.0f, selected ? 1.5f : 1.0f);
        drawCaption(g, number(index % 16 + 1), {0, 13, getWidth(), 27}, 22.0f,
             withinLength ? (playing ? accent : enabled ? lit : ink) : mutedInk.darker(), true, juce::Justification::centred);
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
        downAt = juce::Time::getMillisecondCounterHiRes(); holdEditing = onBeginHold && onBeginHold();
        juce::Button::mouseDown(e);
    }
    void mouseUp(const juce::MouseEvent& e) override
    { if (onEndHold) onEndHold(); juce::Button::mouseUp(e); holdEditing = false; }
    void clicked() override
    { if (onStepClick && (!holdEditing || juce::Time::getMillisecondCounterHiRes() - downAt < 250.0)) onStepClick(true); }
    int index;
    bool enabled = false, selected = false, playing = false, hasLock = false, withinLength = true, grid = true, lockOnly = false;
    std::function<void(bool)> onStepClick;
    std::function<bool()> onBeginHold;
    std::function<void()> onEndHold;
private:
    double downAt = 0.0;
    bool holdEditing = false;
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
    void setMarkers(const std::vector<float>& points, float loopPoint, bool editable)
    {
        if (markers == points && loop == loopPoint && editing == editable) return;
        markers = points; loop = loopPoint; editing = editable; repaint();
    }
    void setViewport(double zoom, double position, double vertical)
    {
        const auto width = static_cast<float>(1.0 / juce::jmax(1.0, zoom));
        const auto startPosition = static_cast<float>(position) * (1.0f - width);
        const auto scale = static_cast<float>(vertical);
        if (visibleWidth == width && visibleStart == startPosition && verticalScale == scale) return;
        visibleWidth = width; visibleStart = startPosition; verticalScale = scale; repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!editing || !onPointEdit) return;
        const auto position = samplePosition(e.position.x);
        draggingPoint = std::abs(position - start) < std::abs(position - end) ? 0 : 1;
        if (std::abs(position - loop) < std::abs(position - (draggingPoint == 0 ? start : end))) draggingPoint = 2;
        onPointEdit(draggingPoint, position);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    { if (editing && onPointEdit) onPointEdit(draggingPoint, samplePosition(e.position.x)); }
    std::function<void(int, float)> onPointEdit;
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
            g.saveState();
            g.reduceClipRegion(getLocalBounds());
            g.fillPath(shape, juce::AffineTransform::scale(r.getWidth() / visibleWidth, r.getHeight() * verticalScale)
                .translated(-visibleStart * r.getWidth() / visibleWidth, r.getHeight() * (1.0f - verticalScale) * 0.5f));
            g.restoreState();
            g.setColour(background.withAlpha(0.7f));
            const auto startX = screenPosition(start), endX = screenPosition(end);
            g.fillRect(0.0f, 0.0f, juce::jlimit(0.0f, r.getWidth(), startX), r.getHeight());
            g.fillRect(juce::jlimit(0.0f, r.getWidth(), endX), 0.0f,
                       juce::jlimit(0.0f, r.getWidth(), r.getWidth() - endX), r.getHeight());
            g.setColour(ink.withAlpha(0.25f));
            for (auto marker : markers) g.drawVerticalLine(juce::roundToInt(screenPosition(marker)), 2.0f, r.getHeight() - 2.0f);
            g.setColour(ink.withAlpha(0.8f));
            g.drawVerticalLine(juce::roundToInt(startX), 2.0f, r.getHeight() - 2.0f);
            g.drawVerticalLine(juce::roundToInt(endX), 2.0f, r.getHeight() - 2.0f);
            if (loop >= 0.0f)
            { g.setColour(accent); g.drawVerticalLine(juce::roundToInt(screenPosition(loop)), 2.0f, r.getHeight() - 2.0f); }
        }
    }
private:
    float samplePosition(float x) const { return juce::jlimit(0.0f, 1.0f, visibleStart + x / static_cast<float>(juce::jmax(1, getWidth())) * visibleWidth); }
    float screenPosition(float x) const { return (x - visibleStart) / visibleWidth * static_cast<float>(getWidth()); }
    std::shared_ptr<const takt::Sample> sample;
    juce::Path shape;
    float start = 0.0f, end = 1.0f;
    float loop = -1.0f, visibleStart = 0.0f, visibleWidth = 1.0f, verticalScale = 1.0f;
    std::vector<float> markers;
    int draggingPoint = 0;
    bool editing = false;
};

TaktAudioProcessorEditor::TaktAudioProcessorEditor(TaktAudioProcessor& p)
    : AudioProcessorEditor(p), processor(p), skin(std::make_unique<HardwareLookAndFeel>()),
      panel(std::make_unique<Panel>(*this)), waveform(std::make_unique<Waveform>()), tooltips(this, 650),
      trackLevel(std::make_unique<Dial>("LEVEL"))
{
    setLookAndFeel(skin.get());
    waveformMarkers.reserve(takt::maxSlices);
    setWantsKeyboardFocus(true);
    addKeyListener(this);
    addAndMakeVisible(*panel);
    panel->setSize(designWidth, designHeight);
    drawerBackdrop.setColour(juce::Label::backgroundColourId, juce::Colour(0xff151a20));
    drawerBackdrop.setColour(juce::Label::outlineColourId, border);
    drawerBackdrop.setOpaque(true); drawerBackdrop.setInterceptsMouseClicks(true, true);
    panel->addAndMakeVisible(drawerBackdrop);
    panel->addAndMakeVisible(*waveform);
    waveform->setComponentID("sample-waveform");
    waveform->onPointEdit = [this](int point, float value) { editSlicePoint(point, value); };
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
    addButton(sendFxButton, true, "view-send-fx", "Shared delay, reverb and chorus pages. Click again or use Up/Down to cycle pages.");
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
    addButton(yesButton, false, "navigation-yes", "Confirm a destination, or open the Slice/Grid menu on SRC. FUNC+YES saves a temporary checkpoint.");
    addButton(trkButton, true, "track-select-modifier", "Latch TRK then a pad to select silently. Turn an encoder while TRK is latched to apply its relative change to all audio tracks. NO cancels; release TRK commits.");
    addButton(sourceImportButton, false, "source-sample-import", "Import a sample into the selected track. This VST uses imported files rather than the hardware's +Drive/project pool.");
    sourceImportButton.onClick = [this] { if (processor.isSampleImportPending(selectedTrack)) cancelSelectedImport(); else chooseSample(); };
    addButton(fillButton, true, "sequencer-fill", "Latch FILL mode. Trigs set to FILL ON/OFF follow this musical condition; independent of the host transport.");
    fillButton.onClick = [this] { processor.setParameter("fill", fillButton.getToggleState() ? 1.0f : 0.0f); };
    addButton(pageButton, false, "sequencer-page-next", "Cycle the eight sequencer pages. LEDs below are clickable for direct page selection.");
    addButton(toolsButton, true, "vst-tools", "Open software utilities: tempo, swing, length, clipboard scope, demo and selected-track controls.");
    addButton(leftButton, false, "navigation-left", "Previous sequencer page.");
    addButton(rightButton, false, "navigation-right", "Next sequencer page.");
    const std::array<const char*, 3> unavailableNames{{"KEYBOARD", "PTN", "SONG"}};
    for (int i = 0; i < 3; ++i)
    {
        auto& b = unavailableButtons[static_cast<std::size_t>(i)];
        b.setButtonText(unavailableNames[static_cast<std::size_t>(i)]);
        addButton(b, false, "unavailable-" + juce::String(unavailableNames[static_cast<std::size_t>(i)]).toLowerCase(), i == 0 ? "Keyboard mode is not implemented in this version." : i == 1 ? "Select a pattern using bank A-H and pads 1-16. During playback, the next pattern is queued at the boundary." : "Select and edit one of 16 songs. Rows retain their pattern, repeat count, length, tempo and track mutes.");
        b.setEnabled(i != 0);
    }
    unavailableButtons[1].onClick = [this] { selectedBank = processor.getCurrentPattern() / 16; showView(view == View::Patterns ? View::Parameters : View::Patterns); };
    unavailableButtons[2].onClick = [this] { showView(view == View::Song ? View::Parameters : View::Song); };
    const std::array<const char*, 4> centreNames{{"PRESET/KIT", "SETTINGS", "SAMPLING", "TEMPO"}};
    for (int i = 0; i < 4; ++i)
    {
        auto& b = centreButtons[static_cast<std::size_t>(i)]; b.setButtonText(centreNames[static_cast<std::size_t>(i)]);
        addButton(b, false, "panel-menu-" + juce::String(i), i == 1 || i == 3
            ? "Open the software utilities drawer. TEMPO and SWING are implemented here; hardware settings are not reproduced."
            : "This internal library or sampling mode is not implemented. Import user samples with the VST strip below.");
        b.setEnabled(i != 2);
        b.onClick = [this] { toolsVisible = true; toolsButton.setToggleState(true, juce::dontSendNotification); updateVisibility(); };
    }
    centreButtons[0].setTooltip("FUNC toggles PERFORM KIT, preserving the current kit across pattern changes. Click PRESET/KIT for SAVE KIT and RELOAD KIT.");
    centreButtons[0].onClick = [this]
    {
        if (funcButton.getToggleState())
        {
            processor.setPerformKit(!processor.getPerformKit()); funcButton.setToggleState(false, juce::dontSendNotification);
            showStatus(processor.getPerformKit() ? "PERFORM KIT on: current sounds follow pattern changes without autosaving tweaks." : "PERFORM KIT off."); return;
        }
        showView(View::Patterns);
    };
    for (int i = 0; i < 8; ++i)
    {
        auto& b = bankButtons[static_cast<std::size_t>(i)]; b.setButtonText(juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)));
        addButton(b, false, "pattern-bank-" + b.getButtonText(), "Select this bank. Pads 1-16 choose patterns silently; playback changes at the next boundary.");
        b.onClick = [this, i] { selectedBank = i; refreshSteps(); refreshArrangementControls(); panel->repaint(); };
    }
    chainText.setComponentID("arrangement-chain"); chainText.setMultiLine(false); chainText.setTextToShowWhenEmpty("A01 A02 B03 (up to 64 patterns)", mutedInk);
    chainText.setTooltip("Transient chain of pattern addresses separated by spaces, commas or semicolons. Example: A01 A02 A02 B03.");
    chainText.setColour(juce::TextEditor::backgroundColourId, inset); chainText.setColour(juce::TextEditor::textColourId, ink);
    chainText.addKeyListener(this); panel->addAndMakeVisible(chainText);
    addButton(chainApplyButton, false, "arrangement-chain-play", "Activate this transient chain; the internal or host transport determines playback.");
    addButton(chainAppendButton, false, "arrangement-chain-add", "Append the current pattern to the chain text.");
    chainApplyButton.onClick = [this] { applyChain(); };
    chainAppendButton.onClick = [this] { auto text = chainText.getText().trim(); chainText.setText(text + (text.isEmpty() ? "" : " ") + patternName(processor.getCurrentPattern()), false); };
    addButton(performKitButton, true, "perform-kit", "Retain the current kit across pattern changes. Tweaks are saved only with SAVE KIT; the Live project still recalls its full state.");
    performKitButton.onClick = [this] { processor.setPerformKit(performKitButton.getToggleState()); };
    addButton(kitSaveButton, false, "kit-save", "Save the current sound parameters, samples and slice points to this pattern's kit.");
    addButton(kitReloadButton, false, "kit-reload", "Restore this pattern's saved kit without changing its sequence or transport.");
    kitSaveButton.onClick = [this] { finishControlAll(false); processor.saveKit(); showStatus("Current pattern kit saved."); };
    kitReloadButton.onClick = [this] { finishControlAll(false); processor.reloadKit(); refreshControls(); timerCallback(); showStatus("Current pattern kit reloaded; sequence and transport remain unchanged."); };
    for (int i = 0; i < takt::songSlots; ++i) songSelect.addItem("SONG " + number(i + 1), i + 1);
    songSelect.setComponentID("song-select"); songSelect.addKeyListener(this); panel->addAndMakeVisible(songSelect);
    songSelect.onChange = [this] { if (!refreshing) selectSong(songSelect.getSelectedId() - 1); };
    songRowSelect.setComponentID("song-row"); songRowSelect.addKeyListener(this); panel->addAndMakeVisible(songRowSelect);
    songRowSelect.onChange = [this] { if (!refreshing) { selectedSongRow = juce::jmax(0, songRowSelect.getSelectedId() - 1); rebuildControls(); } };
    addButton(songAddButton, false, "song-add-row", "Insert a row after the selected row, using the current pattern; at most 99 rows.");
    addButton(songDeleteButton, false, "song-delete-row", "Delete the selected song row.");
    addButton(songPlayButton, false, "song-play", "Play this song from the selected row with the internal clock. With HOST SYNC, select the song and use Live's transport.");
    addButton(songQueueButton, false, "song-queue-row", "When this song is playing, jump to the selected row at the next row boundary.");
    addButton(songMutesButton, false, "song-row-mutes", "Choose which of the 16 tracks this row mutes. The row's mute mask is saved with the song.");
    songAddButton.onClick = [this]
    {
        auto song = processor.getSong(selectedSong); if (song.rowCount >= takt::songRowCapacity) { showStatus("A song has at most 99 rows.", true); return; }
        const auto row = song.rowCount == 0 ? 0 : juce::jmin(song.rowCount, selectedSongRow + 1);
        for (int i = song.rowCount; i > row; --i) song.rows[static_cast<std::size_t>(i)] = song.rows[static_cast<std::size_t>(i - 1)];
        song.rows[static_cast<std::size_t>(row)] = {}; song.rows[static_cast<std::size_t>(row)].pattern.index = processor.getCurrentPattern();
        ++song.rowCount; processor.setSong(selectedSong, song); selectedSongRow = row; displayedSongRows = -1; rebuildControls();
    };
    songDeleteButton.onClick = [this]
    {
        auto song = processor.getSong(selectedSong); if (song.rowCount == 0) return;
        for (int i = selectedSongRow; i + 1 < song.rowCount; ++i) song.rows[static_cast<std::size_t>(i)] = song.rows[static_cast<std::size_t>(i + 1)];
        --song.rowCount; processor.setSong(selectedSong, song); selectedSongRow = juce::jlimit(0, juce::jmax(0, song.rowCount - 1), selectedSongRow); displayedSongRows = -1; rebuildControls();
    };
    songPlayButton.onClick = [this] { finishControlAll(false); if (processor.startSong(selectedSong, selectedSongRow)) { if (!processor.isUsingHostClock()) processor.setParameter("play", 1.0f); showStatus("Song selected. In HOST SYNC, Live's transport and BPM take priority."); } else showStatus("Add at least one song row first.", true); };
    songQueueButton.onClick = [this] { if (processor.getCurrentSong() != selectedSong || !processor.queueSongRow(selectedSongRow)) showStatus("Select this song with PLAY SONG before queueing its row.", true); else showStatus("Row jump queued for the next row boundary."); };
    songMutesButton.onClick = [this] { showSongMuteMenu(); };
    addButton(arrangementBackButton, false, "arrangement-back", "Close pattern or song editing. Playback keeps its current arrangement.");
    arrangementBackButton.onClick = [this] { showView(View::Parameters); };
    arrangementLabel.setColour(juce::Label::textColourId, ink); arrangementLabel.setFont(font(11.0f)); panel->addAndMakeVisible(arrangementLabel);
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
        pad->onBeginHold = [this, i]
        {
            if (view == View::Patterns || view == View::Song || !gridRecording || trkButton.getToggleState() || funcButton.getToggleState()) return false;
            heldStep = selectedPage * 16 + i; selectStep(heldStep, false); return true;
        };
        pad->onEndHold = [this] { heldStep = -1; };
        pad->onStepClick = [this, i](bool toggle)
        {
            if (view == View::Patterns) { selectPatternPad(i); return; }
            if (view == View::Song) { selectSong(i); return; }
            if (trkButton.getToggleState()) { finishControlAll(false); selectTrack(i); trkButton.setToggleState(false, juce::dontSendNotification); }
            else if (gridRecording && funcButton.getToggleState() && toggle)
            { const auto index = selectedPage * 16 + i; auto step = processor.getStep(selectedTrack, index);
              step.advanced = true; step.enabled = false; step.lockTrig = !step.lockTrig; processor.setStep(selectedTrack, index, step);
              funcButton.setToggleState(false, juce::dontSendNotification); selectStep(index, false); }
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
    const juce::StringArray speeds{"1/8x", "1/4x", "1/2x", "3/4x", "1x", "3/2x", "2x"};
    for (int i = 0; i < speeds.size(); ++i) trackSpeed.addItem(speeds[i], i + 1);
    trackSpeed.setComponentID("track-speed"); trackSpeed.addKeyListener(this);
    trackSpeed.setTooltip("Selected track speed. Length and speed determine its independent sequencer cycle.");
    trackSpeed.onChange = [this] { if (!refreshing) processor.setParameter(TaktAudioProcessor::trackParameterID(selectedTrack, "speedIndex"), static_cast<float>(trackSpeed.getSelectedId() - 1)); };
    panel->addAndMakeVisible(trackSpeed);
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
    yesButton.onClick = [this] { confirmAction(); };
    pageButton.onClick = [this] { selectSequencerPage((selectedPage + 1) % 8); };
    leftButton.onClick = [this] { if (view == View::SliceEditor) moveSlice(-1); else if (view == View::Patterns) { selectedBank = (selectedBank + 7) % 8; refreshArrangementControls(); refreshSteps(); } else if (view == View::Song) { songRowSelect.setSelectedId(juce::jmax(1, selectedSongRow), juce::sendNotification); } else selectSequencerPage((selectedPage + 7) % 8); };
    rightButton.onClick = [this] { if (view == View::SliceEditor) moveSlice(1); else if (view == View::Patterns) { selectedBank = (selectedBank + 1) % 8; refreshArrangementControls(); refreshSteps(); } else if (view == View::Song) { songRowSelect.setSelectedId(juce::jmin(songRowSelect.getNumItems(), selectedSongRow + 2), juce::sendNotification); } else selectSequencerPage((selectedPage + 1) % 8); };
    toolsButton.onClick = [this] { if (view == View::Patterns || view == View::Song) view = View::Parameters; toolsVisible = toolsButton.getToggleState(); rebuildControls(); };
    trkButton.onClick = [this]
    {
        if (funcButton.getToggleState()) { toolsVisible = true; toolsButton.setToggleState(true, juce::dontSendNotification); trkButton.setToggleState(false, juce::dontSendNotification); funcButton.setToggleState(false, juce::dontSendNotification); updateVisibility(); }
        else if (trkButton.getToggleState())
        { if (processor.beginControlAll(selectedTrack)) showStatus("CONTROL ALL: encoder edits affect all audio tracks. NO cancels; click TRK to commit."); }
        else finishControlAll(false);
    };
    stepToolsButton.onClick = [this] { showView(view == View::StepTools ? View::Parameters : View::StepTools); };
    sendFxButton.onClick = [this] { if (view == View::SendFx) changeParameterPage(1); else showView(View::SendFx); };
    noButton.onClick = [this]
    {
        if (processor.isControlAllActive()) { finishControlAll(true); trkButton.setToggleState(false, juce::dontSendNotification); }
        else if (!pendingDestination.isEmpty()) { cancelDestinationPreview(); refreshControls(); showStatus("LFO destination selection cancelled."); }
        else if (funcButton.getToggleState()) { temporaryReloadButton.onClick(); funcButton.setToggleState(false, juce::dontSendNotification); }
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
    importButton.onClick = [this] { if (processor.isSampleImportPending(selectedTrack)) cancelSelectedImport(); else chooseSample(); }; triggerButton.onClick = [this] { processor.triggerTrack(selectedTrack); };
    layoutPanel(); selectTrack(0);
    setResizable(true, true); setResizeLimits(720, 624, 1350, 1170);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(designWidth) / designHeight);
    setSize(designWidth, designHeight); timerCallback(); startTimerHz(30);
}

TaktAudioProcessorEditor::~TaktAudioProcessorEditor()
{
    stopTimer(); cancelDestinationPreview(); finishControlAll(false);
    fileChooser.reset(); setLookAndFeel(nullptr);
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
    trackLevel->present("", true, "Selected-track LEVEL. Distinct from SRC LEV and AMP VOL.");
    trackLevel->setBounds(66, 188, 94, 88);
    for (int i = 0; i < 8; ++i)
        encoders[static_cast<std::size_t>(i)]->setBounds(459 + (i % 4) * 104, 77 + (i / 4) * 107, 89, 97);
    sourceImportButton.setBounds(778, 154, 82, 20);
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
    trackSpeed.setBounds(684, 349, 82, 26); fillButton.setBounds(774, 349, 54, 26);
    copyButton.setBounds(522, 390, 87, 27);
    pasteButton.setBounds(617, 390, 87, 27);
    clearButton.setBounds(712, 390, 116, 27);
    temporarySaveButton.setBounds(178, 432, 122, 28);
    temporaryReloadButton.setBounds(308, 432, 130, 28);
    muteButton.setBounds(448, 432, 73, 28);
    reverseButton.setBounds(529, 432, 83, 28);
    loopButton.setBounds(620, 432, 73, 28);
    demoButton.setBounds(703, 432, 125, 28);
    for (int i = 0; i < 8; ++i) bankButtons[static_cast<std::size_t>(i)].setBounds(179 + i * 81, 315, 68, 28);
    arrangementLabel.setBounds(177, 350, 650, 23);
    chainText.setBounds(178, 380, 397, 28); chainAppendButton.setBounds(585, 380, 116, 28); chainApplyButton.setBounds(710, 380, 116, 28);
    performKitButton.setBounds(178, 432, 148, 28); kitSaveButton.setBounds(338, 432, 120, 28); kitReloadButton.setBounds(470, 432, 126, 28);
    songSelect.setBounds(178, 316, 172, 28); songRowSelect.setBounds(362, 316, 172, 28);
    songAddButton.setBounds(546, 316, 125, 28); songDeleteButton.setBounds(683, 316, 144, 28);
    songPlayButton.setBounds(178, 390, 135, 28); songQueueButton.setBounds(325, 390, 145, 28); songMutesButton.setBounds(482, 390, 145, 28);
    arrangementBackButton.setBounds(710, 432, 116, 28);
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
    const auto host = displayedHostClock;
    const auto playing = displayedPlaying;
    const std::array<const char*, 6> familyNames{{"TRIG", "SRC", "FLTR", "AMP", "FX", "MOD"}};
    const auto context = view == View::Patterns ? juce::String("PATTERNS")
                       : view == View::Song ? juce::String("SONG ") + number(selectedSong + 1) + " ROW " + number(selectedSongRow + 1)
                       : view == View::SliceEditor ? juce::String("SLICE ") + juce::String(selectedSlice + 1) + "/" + juce::String(currentSliceCount())
                       : view == View::StepTools ? juce::String("STEP TOOLS*")
                       : view == View::SendFx ? juce::StringArray{"DELAY*", "REVERB*", "CHORUS"}[sendFxPage]
                       : juce::String(familyNames[static_cast<std::size_t>(family)]);
    const auto sourceWave = view == View::SliceEditor || (view == View::Parameters && family == Family::Source && parameterPages[1] == 1);
    g.setColour(juce::Colour(0xff090b0e)); g.fillRoundedRectangle(166, 76, 271, 220, 5);
    g.setColour(juce::Colours::black); g.fillRect(174, 84, 255, 162);
    drawCaption(g, patternName(uiSnapshot.currentPattern) + (uiSnapshot.performKit ? " P" : "") + "  T" + number(selectedTrack + 1), {181, 89, 147, 15}, 10, ink, true);
    drawCaption(g, host ? juce::String("DAW") : juce::String(displayedTempo, 1), {340, 89, 81, 15}, 10, ink, true, juce::Justification::centredRight);
    g.setColour(ink.withAlpha(0.4f)); g.drawHorizontalLine(107, 180, 422);
    const auto pageCaption = view == View::Parameters
        ? "  " + juce::String(parameterPages[static_cast<std::size_t>(family)] + 1) + "/" + juce::String(parameterPageCount()) : juce::String{};
    const std::array<const char*, 7> machineNames{{"LEGACY", "ONESHOT", "WERP", "STRETCH", "REPITCH", "SLICE", "GRID"}};
    const std::array<const char*, 7> filterNames{{"PROTOTYPE", "MULTI", "LP4", "EQ", "COMB-", "COMB+", "LEGACY"}};
    const auto machineCaption = view == View::Parameters && family == Family::Source ? juce::String(" ") + machineNames[static_cast<std::size_t>(juce::jlimit(0, 6, displayedMachine))]
        : view == View::Parameters && family == Family::Filter ? juce::String(" ") + filterNames[static_cast<std::size_t>(juce::jlimit(0, 6, displayedFilterMachine))]
        : view == View::Parameters && family == Family::Amp ? juce::String(" ") + juce::StringArray{"LEGACY", "AHD", "ADSR"}[juce::jlimit(0, 2, displayedAmpMode)] : juce::String{};
    drawCaption(g, context + machineCaption + pageCaption,
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
    drawCaption(g, view == View::SliceEditor ? juce::String(linkedSlicePoints ? "LINKED" : "UNLINKED") + "  < / > SLICE   YES EXIT"
        : view == View::Patterns ? "BANK " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + selectedBank)) + "  PADS SELECT PATTERN"
        : view == View::Song ? "PADS SELECT SONG  /  ENCODERS EDIT ROW"
        : "STEP " + number(selectedStep + 1) + "  LEN " + juce::String(displayedTrackLength),
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
    drawCaption(g, "BANK", {78, 565, 70, 15}, 8, accent, false, juce::Justification::centred);
    drawCaption(g, "EDIT", {78, 641, 70, 15}, 8, accent, false, juce::Justification::centred);
    drawCaption(g, "SEND FX", {710, 347, 69, 14}, 8, accent, false, juce::Justification::centred);
    drawCaption(g, view == View::Patterns ? "SELECT PATTERN 1-16" : view == View::Song ? "SELECT SONG 1-16" : gridRecording ? "GRID RECORDING" : "TRIG TRACKS", {180, 482, 249, 16}, 9, gridRecording ? trigRed : ink, true);
    const auto current = displayedCurrentStep;
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
    const bool patterns = view == View::Patterns && !helpVisible, song = view == View::Song && !helpVisible;
    const bool arrangement = patterns || song;
    drawerBackdrop.setVisible(drawer || arrangement);
    if (drawer || arrangement) drawerBackdrop.toFront(false);
    for (auto* dial : {transportDials[0].get(), transportDials[1].get()})
    { dial->setVisible(drawer); if (drawer) dial->toFront(false); }
    for (auto* control : std::initializer_list<juce::Component*>{&patternLength, &editScope, &copyButton, &pasteButton, &clearButton,
                     &temporarySaveButton, &temporaryReloadButton, &muteButton, &reverseButton, &loopButton, &demoButton, &trackSpeed, &fillButton})
    { control->setVisible(drawer); if (drawer) control->toFront(false); }
    for (auto& button : bankButtons) { button.setVisible(patterns); if (patterns) button.toFront(false); }
    for (auto* control : std::initializer_list<juce::Component*>{&chainText, &chainAppendButton, &chainApplyButton, &performKitButton, &kitSaveButton, &kitReloadButton})
    { control->setVisible(patterns); if (patterns) control->toFront(false); }
    for (auto* control : std::initializer_list<juce::Component*>{&songSelect, &songRowSelect, &songAddButton, &songDeleteButton, &songPlayButton, &songQueueButton, &songMutesButton})
    { control->setVisible(song); if (song) control->toFront(false); }
    arrangementLabel.setVisible(arrangement); if (arrangement) arrangementLabel.toFront(false);
    arrangementBackButton.setVisible(arrangement); if (arrangement) arrangementBackButton.toFront(false);
    waveform->setVisible(!helpVisible && (view == View::SliceEditor || (view == View::Parameters && family == Family::Source && parameterPages[1] == 1)));
    sourceImportButton.setVisible(!helpVisible && view == View::Parameters && family == Family::Source);
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
    if (view == View::SendFx) return 3;
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
    auto* parameter = processor.parameters.getParameter(id);
    if (parameter == nullptr) { bindUnavailable(slot, label, "Parameter is unavailable."); return; }
    bindings[index] = {BindingKind::Parameter, id, {}, {}};
    dial.slider.getProperties().set("parameterID", id);
    const auto range = parameter->getNormalisableRange();
    dial.slider.setNormalisableRange(juce::NormalisableRange<double>(range.start, range.end, range.interval, range.skew, range.symmetricSkew));
    dial.slider.setValue(processor.parameterValue(id), juce::dontSendNotification);
    // setParameter owns each normal gesture; CONTROL ALL owns its transaction
    // gestures. A second drag gesture would nest both and confuse the host.
    dial.slider.onDragStart = {}; dial.slider.onDragEnd = {};
    dial.slider.onValueChange = [this, index, id, name, global]
    {
        if (refreshing) return;
        discardStaleDestinationPreview();
        const auto value = static_cast<float>(encoders[index]->slider.getValue());
        if (!global && (heldStep >= 0 || juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown()))
        {
            if (name == "pitch") changeStep([value](takt::Step& step) { step.pitch = value; step.lockPitch = true; });
            else if (name == "cutoff") changeStep([value](takt::Step& step) { step.cutoff = value; step.lockCutoff = true; });
            else showStatus("Step locks currently support TUNE, FREQ and SLICE. This track control was left unchanged.", true);
            return;
        }
        if (name.endsWith("_destination") && pendingDestination.isEmpty())
        { pendingDestination = id; pendingDestinationPattern = processor.getCurrentPattern(); pendingDestinationTrack = selectedTrack;
          previousDestination = processor.parameterValue(id); showStatus("Previewing LFO destination. YES confirms; NO restores the previous destination."); }
        if (!global && processor.isControlAllActive()) processor.updateControlAll(name, value);
        else processor.setParameter(id, value);
    };
    formatSlider(dial.slider, format);
}

void TaktAudioProcessorEditor::bindChoice(int slot, const juce::String& name, const juce::String& label,
                                          const juce::StringArray& choices, const juce::String& tip)
{
    bindParameter(slot, name, label, ValueFormat::Integer, tip);
    auto& slider = encoders[static_cast<std::size_t>(slot)]->slider;
    slider.textFromValueFunction = [choices](double value) { return choices[juce::jlimit(0, choices.size() - 1, juce::roundToInt(value))]; };
    slider.valueFromTextFunction = [choices](const juce::String& text)
    { for (int i = 0; i < choices.size(); ++i) if (choices[i].equalsIgnoreCase(text.trim())) return static_cast<double>(i); return 0.0; };
    slider.updateText();
}

void TaktAudioProcessorEditor::bindCustom(int slot, const juce::String& label, double low, double high, double interval,
                                          ValueFormat format, const juce::String& tip, std::function<double()> read,
                                          std::function<void(double)> write)
{
    const auto index = static_cast<std::size_t>(slot); auto& dial = *encoders[index];
    dial.present(label, true, tip); bindings[index] = {BindingKind::Custom, {}, {}, std::move(read)};
    dial.slider.setNormalisableRange(juce::NormalisableRange<double>(low, high, interval));
    dial.slider.setValue(bindings[index].read(), juce::dontSendNotification); formatSlider(dial.slider, format);
    dial.slider.onValueChange = [this, index, write = std::move(write)] { if (!refreshing) write(encoders[index]->slider.getValue()); };
}

void TaktAudioProcessorEditor::bindStep(int slot, const juce::String& field, const juce::String& label,
                                       double low, double high, double interval, ValueFormat format, const juce::String& tip)
{
    const auto index = static_cast<std::size_t>(slot);
    auto& dial = *encoders[index]; dial.present(label, true, tip);
    bindings[index] = {BindingKind::Step, {}, field, {}}; dial.slider.getProperties().set("stepField", field);
    dial.slider.setNormalisableRange(juce::NormalisableRange<double>(low, high, interval));
    if (field == "cutoff") dial.slider.setSkewFactorFromMidPoint(1000);
    formatSlider(dial.slider, format);
    dial.slider.onValueChange = [this, index, field]
    {
        if (refreshing) return;
        const auto value = static_cast<float>(encoders[index]->slider.getValue());
        changeStep([this, field, value](takt::Step& s)
        {
            if (field == "velocity") s.velocity = value;
            else if (field == "probability") { s.probability = value; if (view == View::Parameters && family == Family::Trig) s.advanced = true; }
            else if (field == "pitch") { s.pitch = value; s.lockPitch = true; }
            else if (field == "cutoff") { s.cutoff = value; s.lockCutoff = true; }
            else if (field == "every") { s.conditionEvery = juce::roundToInt(value); s.conditionOffset = juce::jmin(s.conditionOffset, s.conditionEvery - 1); }
            else if (field == "offset") s.conditionOffset = juce::jmin(juce::roundToInt(value), s.conditionEvery - 1);
            else if (field == "retrigs") s.retrigs = juce::roundToInt(value);
            else if (field == "microtiming") s.microtiming = value;
            else if (field == "note") s.note = juce::roundToInt(value);
            else if (field == "lfoTrig") s.lfoTrig = value >= 0.5f;
            else if (field == "filterTrig") s.filterTrig = value >= 0.5f;
            else if (field == "slice") { s.slice = juce::roundToInt(value); s.lockSlice = true; }
            else if (field == "noteLength") { s.noteLengthBeats = value; s.advanced = true; }
            else if (field == "retrigOn") { s.retrig.enabled = value >= 0.5f; s.advanced = true; }
            else if (field == "retrigRate") { s.retrig.rateIndex = juce::roundToInt(value); s.advanced = true; }
            else if (field == "retrigFade") { s.retrig.velocityFade = value; s.advanced = true; }
            else if (field == "retrigLength") { s.retrig.fadeLengthBeats = value; s.advanced = true; }
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
        if (processor.isControlAllActive()) { processor.updateControlAll("reverse", value >= 2 ? 1.0f : 0.0f); processor.updateControlAll("loop", value % 2 ? 1.0f : 0.0f); }
        else { processor.setParameter(TaktAudioProcessor::trackParameterID(selectedTrack, "reverse"), value >= 2 ? 1.0f : 0.0f); processor.setParameter(TaktAudioProcessor::trackParameterID(selectedTrack, "loop"), value % 2 ? 1.0f : 0.0f); }
    };
}

void TaktAudioProcessorEditor::rebuildControls()
{
    refreshUiSnapshot();
    refreshing = true;
    displayedMachine = currentMachine();
    displayedAmpMode = juce::roundToInt(processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "ampMode")));
    displayedFilterMachine = juce::roundToInt(processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "filterMachine")));
    controlAttachments.clear();
    for (std::size_t i = 0; i < encoders.size(); ++i)
    {
        auto& slider = encoders[i]->slider;
        slider.onValueChange = {}; slider.onDragStart = {}; slider.onDragEnd = {}; slider.textFromValueFunction = {}; slider.valueFromTextFunction = {};
        slider.setTextValueSuffix({}); slider.setDoubleClickReturnValue(false, 0);
        slider.getProperties().set("parameterID", juce::String{}); slider.getProperties().set("stepField", juce::String{});
        bindings[i] = {}; bindUnavailable(static_cast<int>(i), {}, "No parameter in this position.");
    }
    const auto missing = [this](int slot, const char* name) { bindUnavailable(slot, name, juce::String(name) + " is not implemented in this version."); };
    if (view == View::Patterns)
    {
        bindCustom(0, "PTN LEN", 1, 128, 1, ValueFormat::Integer, "Master pattern length used for pattern changes and arrangement boundaries; individual track lengths remain independent.",
            [this] { return static_cast<double>(uiSnapshot.patternLength); }, [this](double value) { processor.setPatternLength(juce::roundToInt(value)); });
        bindCustom(1, "PERFORM KIT", 0, 1, 1, ValueFormat::Integer, "Preserve the current kit across pattern changes without autosaving performance tweaks.",
            [this] { return processor.getPerformKit() ? 1.0 : 0.0; }, [this](double value) { processor.setPerformKit(value >= 0.5); });
        encoders[1]->slider.textFromValueFunction = [](double value) { return value >= 0.5 ? juce::String("ON") : juce::String("OFF"); }; encoders[1]->slider.updateText();
    }
    else if (view == View::Song)
    {
        const auto& song = uiSnapshot.song;
        if (song.rowCount > 0)
        {
            bindCustom(0, "PATTERN", 0, 127, 1, ValueFormat::Integer, "Pattern address for this song row.",
                [this] { return static_cast<double>(uiSnapshot.song.rows[static_cast<std::size_t>(selectedSongRow)].pattern.index); },
                [this](double value) { editSongRow([value](takt::SongRow& row) { row.pattern.index = juce::roundToInt(value); }); });
            encoders[0]->slider.textFromValueFunction = [](double value) { return patternName(juce::roundToInt(value)); }; encoders[0]->slider.updateText();
            encoders[0]->slider.valueFromTextFunction = [this](const juce::String& value)
            { const auto address = value.trim().toUpperCase(); const auto slot = address.substring(1).getIntValue();
              if (address.length() >= 2 && address.length() <= 3 && address[0] >= 'A' && address[0] <= 'H' && address.substring(1).containsOnly("0123456789") && slot >= 1 && slot <= 16) return static_cast<double>((address[0] - 'A') * 16 + slot - 1);
              return encoders[0]->slider.getValue(); };
            bindCustom(1, "REPS", 1, 64, 1, ValueFormat::Integer, "Repeat count for this row. This implementation allows 1-64 repeats.",
                [this] { return static_cast<double>(uiSnapshot.song.rows[static_cast<std::size_t>(selectedSongRow)].repeats); },
                [this](double value) { editSongRow([value](takt::SongRow& row) { row.repeats = juce::roundToInt(value); }); });
            bindCustom(2, "ROW LEN", 0, 1024, 1, ValueFormat::Integer, "Zero follows the pattern length; a custom length is 2-1024 sequencer steps.",
                [this] { return static_cast<double>(uiSnapshot.song.rows[static_cast<std::size_t>(selectedSongRow)].length); },
                [this](double value) { editSongRow([value](takt::SongRow& row) { row.length = value < 0.5 ? 0 : juce::jmax(2, juce::roundToInt(value)); }); });
            encoders[2]->slider.textFromValueFunction = [](double value) { return value < 0.5 ? juce::String("PATTERN") : juce::String(juce::roundToInt(value)); }; encoders[2]->slider.updateText();
            bindCustom(3, "ROW BPM", 0, 300, 1, ValueFormat::Integer, "Zero follows pattern BPM; otherwise 30-300. Live's tempo has priority when HOST SYNC is active.",
                [this] { return uiSnapshot.song.rows[static_cast<std::size_t>(selectedSongRow)].tempo; },
                [this](double value) { editSongRow([value](takt::SongRow& row) { row.tempo = value < 0.5 ? 0 : juce::jmax(30.0, value); }); });
            encoders[3]->slider.textFromValueFunction = [](double value) { return value < 0.5 ? juce::String("PATTERN") : juce::String(juce::roundToInt(value)); }; encoders[3]->slider.updateText();
            bindCustom(4, "SWING*", 0, 76, 1, ValueFormat::Integer, "PATTERN follows the pattern swing; otherwise the legacy software swing amount, 0-75%.",
                [this] { const auto swing = uiSnapshot.song.rows[static_cast<std::size_t>(selectedSongRow)].swing; return swing < 0 ? 0.0 : static_cast<double>(swing) * 100 + 1; },
                [this](double value) { editSongRow([value](takt::SongRow& row) { row.swing = value < .5 ? -1.0f : static_cast<float>((value - 1) / 100); }); });
            encoders[4]->slider.textFromValueFunction = [](double value) { return value < .5 ? juce::String("PATTERN") : juce::String(juce::roundToInt(value) - 1) + "%"; }; encoders[4]->slider.updateText();
            encoders[4]->slider.valueFromTextFunction = [](const juce::String& value) { return value.containsIgnoreCase("PATTERN") ? 0.0 : value.getDoubleValue() + 1; };
        }
        bindCustom(5, "SONG BPM", 0, 300, 1, ValueFormat::Integer, "Zero uses row/pattern BPM; otherwise this song-wide 30-300 BPM overrides individual rows. Host tempo has priority.",
            [this] { return uiSnapshot.song.tempo; }, [this](double value) { auto changed = processor.getSong(selectedSong); changed.tempo = value < 0.5 ? 0 : juce::jmax(30.0, value); processor.setSong(selectedSong, changed); });
        encoders[5]->slider.textFromValueFunction = [](double value) { return value < 0.5 ? juce::String("ROWS") : juce::String(juce::roundToInt(value)); }; encoders[5]->slider.updateText();
        bindCustom(6, "END", 0, 1, 1, ValueFormat::Integer, "LOOP repeats from the first row; STOP ends the internal arrangement and does not stop Live.",
            [this] { return uiSnapshot.song.endLoop ? 1.0 : 0.0; }, [this](double value) { auto changed = processor.getSong(selectedSong); changed.endLoop = value >= 0.5; processor.setSong(selectedSong, changed); });
        encoders[6]->slider.textFromValueFunction = [](double value) { return value >= 0.5 ? juce::String("LOOP") : juce::String("STOP"); }; encoders[6]->slider.updateText();
    }
    else if (view == View::SliceEditor)
    {
        bindCustom(0, "LINK", 0, 1, 1, ValueFormat::Integer, "Adjacent end/start points are linked by default.",
            [this] { return linkedSlicePoints ? 1.0 : 0.0; }, [this](double value) { linkedSlicePoints = value >= 0.5; });
        encoders[0]->slider.textFromValueFunction = [](double value) { return value >= 0.5 ? juce::String("LINKED") : juce::String("UNLINKED"); }; encoders[0]->slider.updateText();
        bindCustom(3, "LOOP", 0, 1, 0.00001, ValueFormat::Percent, "Slice loop point. FUNC snaps to a nearby zero crossing.",
            [this] { return effectiveSlicePoint(selectedSlice).loop; }, [this](double value) { editSlicePoint(2, static_cast<float>(value)); });
        bindCustom(4, "START", 0, 1, 0.00001, ValueFormat::Percent, "Slice start. Linked points move the previous slice's end. FUNC snaps to a zero crossing.",
            [this] { return effectiveSlicePoint(selectedSlice).start; }, [this](double value) { editSlicePoint(0, static_cast<float>(value)); });
        bindCustom(5, "ZOOM", 1, 64, 0.1, ValueFormat::Number, "Horizontal waveform zoom; FUNC turns this into vertical zoom.",
            [this] { return funcButton.getToggleState() ? sliceVerticalZoom : sliceZoom; },
            [this](double value) { if (funcButton.getToggleState()) sliceVerticalZoom = value; else sliceZoom = value; });
        bindCustom(6, "POSITION", 0, 1, 0.00001, ValueFormat::Percent, "Horizontal view position. FUNC moves the current start/end together.",
            [this] { return funcButton.getToggleState() ? effectiveSlicePoint(selectedSlice).start : slicePosition; },
            [this](double value)
            {
                if (!funcButton.getToggleState()) { slicePosition = value; return; }
                auto point = effectiveSlicePoint(selectedSlice); const auto length = point.end - point.start;
                point.start = juce::jlimit(0.0f, 1.0f - length, static_cast<float>(value));
                const auto delta = point.start - effectiveSlicePoint(selectedSlice).start; point.end = point.start + length;
                point.loop = juce::jlimit(point.start, point.end, point.loop + delta); processor.setSlicePoint(selectedTrack, selectedSlice, point);
            });
        bindCustom(7, "END", 0, 1, 0.00001, ValueFormat::Percent, "Slice end. Linked points move the next slice's start. FUNC snaps to a zero crossing.",
            [this] { return effectiveSlicePoint(selectedSlice).end; }, [this](double value) { editSlicePoint(1, static_cast<float>(value)); });
    }
    else if (view == View::StepTools)
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
        if (sendFxPage == 0)
        {
            bindParameter(0, "delayBeats", "TIME*", ValueFormat::Beats, "Legacy delay time in beats, preserving existing automation.", true);
            bindParameter(2, "feedback", "FDBK", ValueFormat::Percent, "Shared delay feedback.", true);
            bindParameter(7, "delayMix", "VOL*", ValueFormat::Percent, "Shared delay return level.", true);
            missing(1, "PING"); missing(3, "WIDTH"); missing(4, "HPF"); missing(5, "LPF"); missing(6, "REV");
        }
        else if (sendFxPage == 1)
        {
            missing(0, "PRE"); missing(1, "DEC"); missing(2, "FREQ"); missing(3, "GAIN");
            missing(4, "HPF"); missing(5, "LPF"); bindParameter(7, "reverbMix", "VOL*", ValueFormat::Percent, "Shared reverb return level.", true);
        }
        else
        {
            bindParameter(0, "chorusDepth", "DPTH", ValueFormat::Percent, "Depth of the chorus modulation.", true);
            bindParameter(1, "chorusSpeed", "SPD", ValueFormat::Hertz, "Speed of the chorus modulation.", true);
            bindParameter(2, "chorusHighpass", "HPF", ValueFormat::Hertz, "High-pass filtering of the chorus input.", true);
            bindParameter(3, "chorusWidth", "WDTH", ValueFormat::Number, "Stereo width of the chorus; bipolar software range.", true);
            bindParameter(4, "chorusDelaySend", "DEL", ValueFormat::Percent, "Chorus wet signal sent to the delay.", true);
            bindParameter(5, "chorusReverbSend", "REV", ValueFormat::Percent, "Chorus wet signal sent to the reverb.", true);
            bindParameter(7, "chorusVolume", "VOL", ValueFormat::Percent, "Chorus output return volume.", true);
        }
    }
    else switch (family)
    {
        case Family::Trig:
            if (parameterPages[0] == 0)
            {
                bindStep(0, "note", "NOTE", 0, 127, 1, ValueFormat::Integer, "MIDI note of the selected trig. With SLICE=NOTE, this selects the slice."); bindStep(1, "velocity", "VEL", 0, 1, 0.01, ValueFormat::Percent, "Velocity of the selected step, not a track default.");
                bindStep(2, "noteLength", "LEN", 0.03125, 512, 0.03125, ValueFormat::Beats, "Selected trig's note gate in beats. Editing enables the advanced sequencer. Finite lengths only; INF is not implemented.");
                bindStep(3, "probability", "PROB", 0, 1, 0.01, ValueFormat::Percent, "Probability of the selected step, independent of COND and FILL.");
                bindStep(4, "lfoTrig", "LFO.T", 0, 1, 1, ValueFormat::Integer, "Retrigger this track's three LFOs when this note trig plays.");
                bindStep(5, "filterTrig", "FLT.T", 0, 1, 1, ValueFormat::Integer, "Trigger this track's filter envelope when this note trig plays.");
                bindCustom(6, "FILL", 0, 2, 1, ValueFormat::Integer, "ANY plays independently of FILL; ON requires FILL, OFF requires it to be inactive. Editing enables advanced rules.",
                    [this] { return static_cast<double>(uiSnapshot.selectedStepValue.rule.fill); },
                    [this](double value) { changeStep([value](takt::Step& step) { step.advanced = true; step.rule.fill = static_cast<takt::sequencer::Fill>(juce::roundToInt(value)); }); });
                encoders[6]->slider.textFromValueFunction = [](double value) { return juce::StringArray{"ANY", "ON", "OFF"}[juce::jlimit(0, 2, juce::roundToInt(value))]; }; encoders[6]->slider.updateText();
                const auto names = trigConditionNames();
                bindCustom(7, "COND", 0, names.size() - 1, 1, ValueFormat::Integer, "Documented PRE, NEI, 1ST, LST, A:B and inverses. Editing enables advanced rules; old EVERY/OFFSET remains under STEP TOOLS.",
                    [this] { return static_cast<double>(trigConditionIndex(uiSnapshot.selectedStepValue.rule)); },
                    [this](double value) { changeStep([value](takt::Step& step) { const auto fill = step.rule.fill; step.rule = trigConditionAt(juce::roundToInt(value)); step.rule.fill = fill; step.rule.probability = step.probability; step.advanced = true; }); });
                encoders[7]->slider.textFromValueFunction = [names](double value) { return names[juce::jlimit(0, names.size() - 1, juce::roundToInt(value))]; }; encoders[7]->slider.updateText();
            }
            else
            {
                bindStep(0, "retrigOn", "RTRG", 0, 1, 1, ValueFormat::Integer, "Enable the documented rate/gate retrigger mode for this trig; supersedes legacy repeat count.");
                bindStep(1, "retrigFade", "VFAD", -64, 64, 1, ValueFormat::Integer, "Retrig velocity fade curve; negative fades out, positive fades in.");
                bindStep(2, "retrigLength", "LEN", 0.03125, 512, 0.03125, ValueFormat::Beats, "Velocity-fade envelope duration in beats, independent of the note gate. Finite values only.");
                bindStep(3, "retrigRate", "RATE", 0, 16, 1, ValueFormat::Integer, "Retrigger rate in fractions of a whole note. 1/16 gives one trigger per normal step; 1/32 gives two.");
                encoders[3]->slider.textFromValueFunction = [](double value) { return "1/" + juce::String(takt::sequencer::retrigDenominators[static_cast<std::size_t>(juce::jlimit(0, 16, juce::roundToInt(value)))]); }; encoders[3]->slider.updateText();
                bindStep(6, "microtiming", "PTIM*", -0.49, 0.49, 0.01, ValueFormat::Percent, "Legacy microtiming as a fraction of one step. Existing states retain their timing."); missing(7, "PORT");
            }
            break;
        case Family::Source:
        {
            const auto machine = currentMachine();
            if (machine != 4) bindParameter(0, "pitch", "TUNE", ValueFormat::Pitch, "Sample tuning. Existing pitch automation range is retained.");
            if (machine == 0) bindPlayback(1);
            else bindChoice(1, "playMode", "PLAY", {"FWD", "REV", "FWD LOOP", "REV LOOP"}, "Direction and looping between the selected source/slice region.");
            bindUnavailable(3, "SAMP", "Import a sample with the button below this encoder. An internal hardware sample pool is not emulated.");
            if (machine == 0)
            {
                bindParameter(4, "start", "START*", ValueFormat::Percent, "Absolute sample start; original automation is retained.");
                bindParameter(5, "end", "END*", ValueFormat::Percent, "Legacy absolute end. Select ONESHOT with FUNC+SRC to use relative LEN and independent LOOP.");
                missing(6, "LOOP"); missing(7, "LEV");
            }
            else
            {
                bindParameter(7, "sampleLevel", "LEV", ValueFormat::Percent, "Sample level before the amplifier, independent of track LEVEL.");
                if (machine == 2)
                {
                    bindParameter(4, "segmentSize", "SEG", ValueFormat::Percent, "Werp segment size as a fraction of the source. This is an independent DSP adaptation.");
                    bindChoice(5, "segmentMode", "MODE", {"FWD", "REV", "FWD LOOP", "REV LOOP"}, "Playback direction and looping of each Werp segment.");
                }
                else if (machine == 5 || machine == 6)
                {
                    bindCustom(4, "SLICE", 0, 128, 1, ValueFormat::Integer, "Choose a slice, or NOTE to select slices using incoming/step notes. Slice numbering starts at 1.",
                        [this] { return processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "sliceByNote")) >= 0.5f ? 0.0 : processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "slice")) + 1.0; },
                        [this](double value)
                        {
                            if ((heldStep >= 0 || juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown()) && value >= 0.5)
                            { changeStep([value](takt::Step& step) { step.slice = juce::roundToInt(value) - 1; step.lockSlice = true; }); return; }
                            const auto update = [this](const juce::String& suffix, float v) { if (processor.isControlAllActive()) processor.updateControlAll(suffix, v); else processor.setParameter(TaktAudioProcessor::trackParameterID(selectedTrack, suffix), v); };
                            update("sliceByNote", value < 0.5 ? 1.0f : 0.0f); if (value >= 0.5) update("slice", static_cast<float>(value - 1));
                        });
                    encoders[4]->slider.textFromValueFunction = [](double value) { return value < 0.5 ? juce::String("NOTE") : juce::String(juce::roundToInt(value)); }; encoders[4]->slider.updateText();
                    bindParameter(5, "sliceLength", "LEN", ValueFormat::Integer, "Number of consecutive slices to play.");
                    if (machine == 6) bindParameter(6, "sliceCount", "GRID", ValueFormat::Integer, "Number of equal slices across the entire sample.");
                }
                else
                {
                    bindParameter(4, "start", "STRT", ValueFormat::Percent, "Playback start, as a normalized source position.");
                    bindParameter(5, "sourceLength", "LEN", ValueFormat::Percent, "Relative source length: the end is STRT + LEN, constrained to the sample.");
                }
                if (machine == 1) bindParameter(6, "loopPosition", "LOOP", ValueFormat::Percent, "Independent loop return position inside the source region.");
                else if (machine >= 2 && machine <= 4) bindParameter(6, "bars", "BARS", ValueFormat::Number, "Total source duration in bars at the host/internal tempo.");
            }
            break;
        }
        case Family::Filter:
            if (parameterPages[2] == 0)
            {
                bindParameter(0, "filterEnvAttack", "ATK", ValueFormat::Milliseconds, "Filter envelope attack time.");
                bindParameter(1, "filterEnvDecay", "DEC", ValueFormat::Seconds, "Filter envelope decay time.");
                bindParameter(2, "filterEnvSustain", "SUS", ValueFormat::Percent, "Filter envelope sustain level.");
                bindParameter(3, "filterEnvRelease", "REL", ValueFormat::Seconds, "Filter envelope release time.");
                bindParameter(4, "cutoff", "FREQ", ValueFormat::Hertz, "Filter cutoff, EQ center frequency or comb tuning. FUNC+FLTR selects the machine.");
                if (displayedFilterMachine == 3)
                { bindParameter(5, "eqGain", "GAIN", ValueFormat::Number, "Equalizer boost/cut in dB."); bindParameter(6, "eqQ", "Q", ValueFormat::Number, "EQ bandwidth; higher Q narrows the affected range."); }
                else if (displayedFilterMachine == 4 || displayedFilterMachine == 5)
                { bindParameter(5, "combFeedback", "FDBK", ValueFormat::Percent, "Comb feedback amount."); bindParameter(6, "combLowpass", "LPF", ValueFormat::Hertz, "Low-pass cutoff of the comb feedback."); }
                else
                {
                    bindParameter(5, "resonance", "RESO", ValueFormat::Percent, "Filter resonance.");
                    if (displayedFilterMachine == 1) bindParameter(6, "filterType", "TYPE", ValueFormat::Percent, "Morph from low-pass through band-pass to high-pass.");
                    else if (displayedFilterMachine == 6) bindChoice(6, "filterType", "TYPE", {"LP", "HP"}, "Digitakt I-inspired 12 dB/octave low-pass or high-pass filter.");
                }
                bindParameter(7, "filterEnvDepth", "ENV", ValueFormat::Number, "Bipolar filter envelope depth. Prototype mode preserves the first VST's filter path.");
            }
            else
            {
                bindParameter(0, "filterEnvDelay", "DEL", ValueFormat::Seconds, "Delay before the filter envelope attack.");
                bindParameter(3, "filterKeytrack", "KEY.T", ValueFormat::Percent, "Amount of filter frequency tracking from the trig's note.");
                bindParameter(4, "filterBase", "BASE", ValueFormat::Integer, "Base-width high-pass frequency; zero disables the high-pass.");
                bindParameter(5, "filterWidth", "WIDTH", ValueFormat::Integer, "Base-width range; 127 disables the low-pass.");
                bindChoice(6, "filterBwPre", "BW.RT", {"POST", "PRE"}, "Route the base-width filter before or after the selected filter machine.");
                bindChoice(7, "filterEnvReset", "RSET", {"OFF", "ON"}, "Reset the filter envelope for consecutive trigs.");
            }
            break;
        case Family::Amp:
            bindParameter(0, "attack", "ATK", ValueFormat::Milliseconds, "Amplitude envelope attack. The legacy parameter range is preserved.");
            if (displayedAmpMode == 1)
            {
                bindCustom(1, "HOLD", 0, 10.01, .01, ValueFormat::Seconds, "Fixed hold time 0-10 seconds, or NOTE at the maximum to follow the note gate.",
                    [this] { return processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "ampHoldNote")) >= .5f ? 10.01 : static_cast<double>(processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "ampHold"))); },
                    [this](double value)
                    { if (processor.isControlAllActive()) { processor.updateControlAll("ampHoldNote", value > 10 ? 1.0f : 0.0f); if (value <= 10) processor.updateControlAll("ampHold", static_cast<float>(value)); }
                      else { processor.setParameter(TaktAudioProcessor::trackParameterID(selectedTrack, "ampHoldNote"), value > 10 ? 1.0f : 0.0f); if (value <= 10) processor.setParameter(TaktAudioProcessor::trackParameterID(selectedTrack, "ampHold"), static_cast<float>(value)); } });
                encoders[1]->slider.textFromValueFunction = [](double value) { return value > 10 ? juce::String("NOTE") : juce::String(value, 2) + " s"; }; encoders[1]->slider.updateText();
                encoders[1]->slider.valueFromTextFunction = [](const juce::String& text) { return text.containsIgnoreCase("NOTE") ? 10.01 : text.getDoubleValue(); };
                bindParameter(2, "decay", "DEC", ValueFormat::Seconds, "Amplitude envelope decay after the AHD hold phase.");
            }
            else
            {
                bindParameter(1, "decay", "DEC", ValueFormat::Seconds, "Amplitude envelope decay.");
                if (displayedAmpMode == 2)
                { bindParameter(2, "ampSustain", "SUS", ValueFormat::Percent, "Amplitude envelope sustain level while the note gate is open."); bindParameter(3, "ampRelease", "REL", ValueFormat::Seconds, "Amplitude envelope release after note-off."); }
            }
            bindChoice(4, "ampReset", "RSET", {"OFF", "ON"}, "Reset or continue the amplitude envelope in AHD/ADSR. LEGACY retains its original retrigger behavior.");
            bindChoice(5, "ampMode", "MODE", {"LEGACY", "AHD", "ADSR"}, "LEGACY preserves the first VST's envelope; AHD and ADSR use the documented hold/sustain/release workflow.");
            bindParameter(6, "pan", "PAN", ValueFormat::Number, "Stereo position, -1 left to +1 right.");
            bindParameter(7, "ampVolume", "VOL", ValueFormat::Percent, "Amplitude gain, independent from track LEVEL and source LEV."); break;
        case Family::Fx:
            bindParameter(0, "bitReduction", "BR", ValueFormat::Integer, "Bit reduction from 16 down to 1 bit. The first version's 4-24-bit parameter is still recalled and automated independently.");
            bindParameter(1, "drive", "OVER", ValueFormat::Percent, "Track overdrive amount.");
            bindParameter(2, "srr", "SRR", ValueFormat::Integer, "Sample-rate reduction amount.");
            bindChoice(3, "srrPre", "ROUT", {"POST", "PRE"}, "Apply sample-rate reduction before or after the filter section.");
            bindParameter(4, "delaySend", "DEL", ValueFormat::Percent, "Track send to the shared delay.");
            bindParameter(5, "reverbSend", "REV", ValueFormat::Percent, "Track send to the shared reverb.");
            bindParameter(6, "chorusSend", "CHR", ValueFormat::Percent, "Track send to the shared chorus.");
            bindChoice(7, "drivePre", "OD.RT", {"POST", "PRE"}, "Apply overdrive before or after the selected filter machine."); break;
        case Family::Mod:
        {
            const auto prefix = "lfo" + juce::String(parameterPages[5] + 1) + "_";
            bindParameter(0, prefix + "speed", "SPD", ValueFormat::Number, "Bipolar LFO speed. 8, 16 and 32 align to straight beats; negative values reverse the cycle.");
            bindCustom(1, "MULT", 0, 37, 1, ValueFormat::Integer, "Power-of-two multiplier, either synchronized to BPM or to fixed 120 BPM.",
                [this, prefix]
                { const auto value = processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, prefix + "multiplier"));
                  const auto power = juce::jlimit(0, 18, juce::roundToInt(std::log2(juce::jmax(1.0f / 128, value))) + 7);
                  return static_cast<double>(power + (processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, prefix + "bpmSync")) >= 0.5f ? 0 : 19)); },
                [this, prefix](double value)
                { const auto index = juce::roundToInt(value); const auto factor = static_cast<float>(std::pow(2.0, index % 19 - 7));
                  if (processor.isControlAllActive()) { processor.updateControlAll(prefix + "multiplier", factor); processor.updateControlAll(prefix + "bpmSync", index < 19 ? 1.0f : 0.0f); }
                  else { processor.setParameter(TaktAudioProcessor::trackParameterID(selectedTrack, prefix + "multiplier"), factor); processor.setParameter(TaktAudioProcessor::trackParameterID(selectedTrack, prefix + "bpmSync"), index < 19 ? 1.0f : 0.0f); } });
            encoders[1]->slider.textFromValueFunction = [](double value) { const auto index = juce::roundToInt(value); const auto exponent = index % 19 - 7; return (exponent < 0 ? "x1/" + juce::String(1 << -exponent) : "x" + juce::String(1 << exponent)) + (index < 19 ? " BPM" : " 120"); }; encoders[1]->slider.updateText();
            bindParameter(2, prefix + "fade", "FADE", ValueFormat::Number, "Positive values fade out, negative values fade in. Zero disables fading.");
            bindChoice(3, prefix + "destination", "DEST", {"OFF", "TUNE", "FREQ", "LEVEL", "PAN", "STRT", "LEN", "LOOP", "SLICE", "OVER", "BR", "DEL", "REV", "ATK", "DEC"}, "Preview the modulation destination. YES confirms, NO cancels.");
            bindChoice(4, prefix + "wave", "WAVE", {"TRI", "SINE", "SQR", "SAW", "EXP", "RAMP", "RND"}, "LFO waveform. EXP and RAMP are unipolar; all others are bipolar.");
            bindParameter(5, prefix + "phase", processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, prefix + "wave")) >= 5.5f ? "SLEW" : "SPH", ValueFormat::Integer, "Start phase 0-127; for RND this smooths transitions instead.");
            bindChoice(6, prefix + "mode", "MODE", {"FRE", "TRG", "HLD", "ONE", "HLF"}, "Free, retrigger, sample-and-hold, one complete cycle, or half a cycle. LFO.T on TRIG controls note retriggers.");
            bindParameter(7, prefix + "depth", "DEP", ValueFormat::Number, "Bipolar modulation depth. Zero leaves the destination unchanged.");
            break;
        }
    }
    for (int i = 0; i < 6; ++i) familyButtons[static_cast<std::size_t>(i)].setToggleState(view == View::Parameters && static_cast<int>(family) == i, juce::dontSendNotification);
    previousPageButton.setEnabled((view == View::Parameters || view == View::SendFx) && parameterPageCount() > 1);
    nextPageButton.setEnabled((view == View::Parameters || view == View::SendFx) && parameterPageCount() > 1);
    stepToolsButton.setToggleState(view == View::StepTools, juce::dontSendNotification); sendFxButton.setToggleState(view == View::SendFx, juce::dontSendNotification);
    pitchLockButton.setVisible(view == View::StepTools); cutoffLockButton.setVisible(view == View::StepTools);
    refreshing = false; refreshControls(); refreshArrangementControls(); updateVisibility(); panel->repaint();
}

void TaktAudioProcessorEditor::selectTrack(int track)
{
    cancelDestinationPreview();
    finishControlAll(false);
    if (view == View::SliceEditor) view = View::Parameters;
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
    cancelDestinationPreview();
    if (funcButton.getToggleState() && f == Family::Source)
    { funcButton.setToggleState(false, juce::dontSendNotification); showMachineMenu(); return; }
    if (funcButton.getToggleState() && f == Family::Fx)
    {
        funcButton.setToggleState(false, juce::dontSendNotification); showView(View::SendFx); return;
    }
    if (funcButton.getToggleState() && f == Family::Filter)
    { funcButton.setToggleState(false, juce::dontSendNotification); showFilterMachineMenu(); return; }
    if (view == View::Parameters && family == f) parameterPages[static_cast<std::size_t>(family)] = (parameterPages[static_cast<std::size_t>(family)] + 1) % parameterPageCount();
    family = f; view = View::Parameters; helpVisible = false; helpButton.setToggleState(false, juce::dontSendNotification);
    helpLabel.setVisible(false); rebuildControls();
}

void TaktAudioProcessorEditor::changeParameterPage(int delta)
{
    if (view == View::SendFx) { sendFxPage = (sendFxPage + delta + 3) % 3; rebuildControls(); return; }
    if (view != View::Parameters) return;
    cancelDestinationPreview();
    auto& page = parameterPages[static_cast<std::size_t>(family)]; page = (page + delta + parameterPageCount()) % parameterPageCount(); rebuildControls();
}

void TaktAudioProcessorEditor::selectSequencerPage(int page)
{
    selectedPage = juce::jlimit(0, 7, page); selectedStep = selectedPage * 16;
    refreshSteps(); refreshControls(); panel->repaint();
}

void TaktAudioProcessorEditor::showView(View v)
{
    if (v == View::Patterns || v == View::Song)
    {
        cancelDestinationPreview();
        finishControlAll(false); trkButton.setToggleState(false, juce::dontSendNotification);
        toolsVisible = false; toolsButton.setToggleState(false, juce::dontSendNotification);
        helpVisible = false; helpButton.setToggleState(false, juce::dontSendNotification);
    }
    view = v; rebuildControls(); refreshSteps();
}

void TaktAudioProcessorEditor::goBack()
{
    if (processor.isControlAllActive()) { finishControlAll(true); trkButton.setToggleState(false, juce::dontSendNotification); return; }
    if (!pendingDestination.isEmpty()) { cancelDestinationPreview(); refreshControls(); return; }
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
        auto s = processor.getStep(selectedTrack, selectedStep);
        if (s.enabled || s.lockTrig) s = {};
        else { s.enabled = true; s.advanced = true; }
        processor.setStep(selectedTrack, selectedStep, s);
        if (selectedStep >= processor.getTrackLength(selectedTrack)) showStatus("Step is beyond this track's length. Increase LENGTH to hear it.");
    }
    refreshSteps(); refreshControls(); panel->repaint();
}

void TaktAudioProcessorEditor::refreshUiSnapshot()
{
    if (!updatingTimer) uiSnapshot = processor.getUiSnapshot(selectedTrack, selectedPage, selectedStep, selectedSong);
    if (view == View::Song) selectedSongRow = juce::jlimit(0, juce::jmax(0, uiSnapshot.song.rowCount - 1), selectedSongRow);
}

void TaktAudioProcessorEditor::refreshSteps()
{
    refreshUiSnapshot();
    const auto current = uiSnapshot.currentSteps[static_cast<std::size_t>(selectedTrack)], length = uiSnapshot.trackLength;
    displayedCurrentStep = current; displayedTrackLength = length;
    const auto playing = processor.isUsingHostClock() ? processor.isHostPlaying() : processor.parameterValue("play") >= 0.5f;
    for (int i = 0; i < 16; ++i)
    {
        auto& pad = *stepPads[static_cast<std::size_t>(i)];
        const auto visualState = [&pad] { return std::make_tuple(pad.index, pad.enabled, pad.selected, pad.playing,
            pad.hasLock, pad.withinLength, pad.grid, pad.lockOnly); };
        const auto previous = visualState(); pad.grid = gridRecording;
        if (view == View::Patterns || view == View::Song)
        {
            pad.index = i; pad.grid = false; pad.lockOnly = false; pad.withinLength = true; pad.hasLock = false;
            pad.enabled = view == View::Patterns ? uiSnapshot.currentPattern == selectedBank * 16 + i : uiSnapshot.songRowCounts[static_cast<std::size_t>(i)] > 0;
            pad.selected = view == View::Patterns ? uiSnapshot.currentPattern == selectedBank * 16 + i : selectedSong == i;
            pad.playing = view == View::Patterns ? uiSnapshot.queuedPattern == selectedBank * 16 + i : uiSnapshot.currentSong == i && playing;
            pad.setTooltip(view == View::Patterns ? "Select " + patternName(selectedBank * 16 + i) + " silently. While playing, it is queued at the pattern boundary." : "Select SONG " + number(i + 1) + " for editing. Use PLAY SONG to activate it.");
            if (previous != visualState()) pad.repaint();
            continue;
        }
        pad.index = gridRecording ? selectedPage * 16 + i : i;
        const auto& s = uiSnapshot.visibleSteps[static_cast<std::size_t>(i)];
        pad.enabled = gridRecording && (s.enabled || s.lockTrig); pad.lockOnly = s.lockTrig;
        pad.selected = gridRecording ? selectedStep == pad.index : selectedTrack == i;
        pad.playing = playing && gridRecording && current == pad.index; pad.hasLock = s.lockPitch || s.lockCutoff || s.lockSlice;
        pad.withinLength = !gridRecording || pad.index < length;
        pad.setTooltip(gridRecording ? "Click to toggle a note. Hold to edit without removing it; right-click selects. FUNC+pad toggles a yellow lock trig." : "Click to play this track. Right-click or modifier-click selects it silently.");
        if (previous != visualState()) pad.repaint();
    }
    for (int i = 0; i < 8; ++i) pageButtons[static_cast<std::size_t>(i)].setToggleState(i == selectedPage, juce::dontSendNotification);
    patternLength.setValue(length, juce::dontSendNotification);
}

void TaktAudioProcessorEditor::refreshControls()
{
    refreshUiSnapshot();
    const auto wasRefreshing = refreshing; refreshing = true;
    const auto& s = uiSnapshot.selectedStepValue;
    for (std::size_t i = 0; i < bindings.size(); ++i)
    {
        auto& slider = encoders[i]->slider; if (slider.isMouseButtonDown()) continue;
        if (bindings[i].kind == BindingKind::Parameter)
            slider.setValue(processor.parameterValue(bindings[i].parameterID), juce::dontSendNotification);
        else if (bindings[i].kind == BindingKind::Custom && bindings[i].read)
            slider.setValue(bindings[i].read(), juce::dontSendNotification);
        else if (bindings[i].kind == BindingKind::Step)
        {
            const auto& field = bindings[i].stepField; double value = 0;
            if (field == "velocity") value = s.velocity; else if (field == "probability") value = s.probability;
            else if (field == "pitch") value = s.pitch; else if (field == "cutoff") value = s.cutoff;
            else if (field == "every") value = s.conditionEvery; else if (field == "offset") value = s.conditionOffset;
            else if (field == "retrigs") value = s.retrigs; else if (field == "microtiming") value = s.microtiming;
            else if (field == "note") value = s.note; else if (field == "lfoTrig") value = s.lfoTrig ? 1 : 0;
            else if (field == "filterTrig") value = s.filterTrig ? 1 : 0;
            else if (field == "slice") value = s.slice;
            else if (field == "noteLength") value = s.noteLengthBeats;
            else if (field == "retrigOn") value = s.retrig.enabled ? 1 : 0;
            else if (field == "retrigRate") value = s.retrig.rateIndex;
            else if (field == "retrigFade") value = s.retrig.velocityFade;
            else if (field == "retrigLength") value = s.retrig.fadeLengthBeats;
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

int TaktAudioProcessorEditor::currentMachine() const
{ return juce::jlimit(0, 6, juce::roundToInt(processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "machine")))); }

int TaktAudioProcessorEditor::currentSliceCount() const
{ return updatingTimer ? timerSliceCount : juce::jlimit(1, takt::maxSlices, juce::roundToInt(processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "sliceCount")))); }

takt::SlicePoint TaktAudioProcessorEditor::effectiveSlicePoint(int index) const
{
    const auto count = currentSliceCount(); index = juce::jlimit(0, count - 1, index);
    auto point = updatingTimer ? uiSnapshot.slicePoints[static_cast<std::size_t>(index)]
                              : processor.getSlicePoints(selectedTrack)[static_cast<std::size_t>(index)];
    if (point.end <= point.start)
    { point.start = static_cast<float>(index) / static_cast<float>(count); point.end = static_cast<float>(index + 1) / static_cast<float>(count); point.loop = point.start; }
    return point;
}

void TaktAudioProcessorEditor::finishControlAll(bool cancel)
{
    if (!processor.isControlAllActive()) return;
    if (cancel) processor.cancelControlAll(); else processor.commitControlAll();
    pendingDestination.clear();
    showStatus(cancel ? "CONTROL ALL changes cancelled." : "CONTROL ALL changes committed.");
}

void TaktAudioProcessorEditor::cancelDestinationPreview()
{
    discardStaleDestinationPreview();
    if (pendingDestination.isEmpty()) return;
    if (processor.isControlAllActive()) processor.updateControlAll(pendingDestination.fromFirstOccurrenceOf("_", false, false), previousDestination);
    else processor.setParameter(pendingDestination, previousDestination);
    pendingDestination.clear();
}

void TaktAudioProcessorEditor::discardStaleDestinationPreview()
{
    // A preview belongs to the pattern and track where its first edit happened.
    // Once playback selects another pattern, its old value must never be
    // restored into the newly active kit, including when this editor closes.
    if (!pendingDestination.isEmpty()
        && (pendingDestinationPattern != processor.getCurrentPattern() || pendingDestinationTrack != selectedTrack))
        pendingDestination.clear();
}

void TaktAudioProcessorEditor::showMachineMenu()
{
    finishControlAll(false); trkButton.setToggleState(false, juce::dontSendNotification);
    juce::PopupMenu menu; const juce::StringArray names{"LEGACY (original player)", "ONESHOT", "WERP", "STRETCH", "REPITCH", "SLICE", "GRID"};
    for (int i = 0; i < names.size(); ++i) menu.addItem(i + 1, names[i], true, i == currentMachine());
    const juce::Component::SafePointer<TaktAudioProcessorEditor> safe(this); const auto track = selectedTrack;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&familyButtons[1]), [safe, track](int result)
    {
        if (safe == nullptr || result == 0) return;
        auto& p = safe->processor; const auto id = [track](const juce::String& name) { return TaktAudioProcessor::trackParameterID(track, name); };
        if (p.parameterValue(id("machine")) < 0.5f && result > 1)
        {
            p.setParameter(id("sourceLength"), juce::jmax(0.001f, p.parameterValue(id("end")) - p.parameterValue(id("start"))));
            const auto reverse = p.parameterValue(id("reverse")) >= 0.5f, loop = p.parameterValue(id("loop")) >= 0.5f;
            p.setParameter(id("playMode"), static_cast<float>((reverse ? 1 : 0) + (loop ? 2 : 0)));
        }
        p.setParameter(id("machine"), static_cast<float>(result - 1));
        if (safe->selectedTrack == track)
        { safe->family = Family::Source; safe->view = View::Parameters; safe->rebuildControls(); safe->showStatus("SRC machine selected. Its parameters are available on both SRC pages."); }
    });
}

void TaktAudioProcessorEditor::showFilterMachineMenu()
{
    finishControlAll(false); trkButton.setToggleState(false, juce::dontSendNotification);
    juce::PopupMenu menu;
    const juce::StringArray names{"PROTOTYPE (original filter)", "MULTI-MODE", "LOWPASS 4", "EQ", "COMB-", "COMB+", "LEGACY (Digitakt I inspired)"};
    const auto track = selectedTrack, selected = juce::roundToInt(processor.parameterValue(TaktAudioProcessor::trackParameterID(track, "filterMachine")));
    for (int i = 0; i < names.size(); ++i) menu.addItem(i + 1, names[i], true, i == selected);
    const juce::Component::SafePointer<TaktAudioProcessorEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&familyButtons[2]), [safe, track](int result)
    {
        if (safe == nullptr || result == 0) return;
        safe->processor.setParameter(TaktAudioProcessor::trackParameterID(track, "filterMachine"), static_cast<float>(result - 1));
        if (safe->selectedTrack == track) { safe->family = Family::Filter; safe->showView(View::Parameters); safe->showStatus("Filter machine selected. FLTR pages follow its documented controls."); }
    });
}

void TaktAudioProcessorEditor::selectPatternPad(int pad)
{
    finishControlAll(false); trkButton.setToggleState(false, juce::dontSendNotification);
    const auto slot = selectedBank * 16 + juce::jlimit(0, 15, pad);
    if (!processor.queuePattern(slot)) { showStatus("Pattern selection failed.", true); return; }
    refreshArrangementControls(); refreshSteps(); refreshControls();
    showStatus(processor.getQueuedPattern() >= 0 ? patternName(slot) + " queued for the next pattern boundary." : patternName(slot) + " selected. Pads choose patterns silently.");
}

void TaktAudioProcessorEditor::selectSong(int slot)
{
    selectedSong = juce::jlimit(0, takt::songSlots - 1, slot); selectedSongRow = 0; displayedSongRows = -1;
    rebuildControls(); refreshSteps();
    showStatus("SONG " + number(selectedSong + 1) + " selected for editing. ADD ROW creates a song; PLAY SONG activates it.");
}

void TaktAudioProcessorEditor::editSongRow(const std::function<void(takt::SongRow&)>& edit)
{
    auto song = processor.getSong(selectedSong);
    if (selectedSongRow < 0 || selectedSongRow >= song.rowCount) return;
    edit(song.rows[static_cast<std::size_t>(selectedSongRow)]); processor.setSong(selectedSong, song);
    refreshArrangementControls(); panel->repaint();
}

void TaktAudioProcessorEditor::refreshArrangementControls()
{
    refreshUiSnapshot();
    const auto wasRefreshing = refreshing; refreshing = true;
    for (int i = 0; i < 8; ++i) bankButtons[static_cast<std::size_t>(i)].setToggleState(selectedBank == i, juce::dontSendNotification);
    performKitButton.setToggleState(uiSnapshot.performKit, juce::dontSendNotification);
    centreButtons[0].setToggleState(uiSnapshot.performKit, juce::dontSendNotification);
    unavailableButtons[1].setToggleState(view == View::Patterns, juce::dontSendNotification);
    unavailableButtons[2].setToggleState(view == View::Song, juce::dontSendNotification);
    if (view == View::Patterns)
    {
        const auto queued = uiSnapshot.queuedPattern;
        const auto mode = uiSnapshot.arrangementMode;
        arrangementLabel.setText("CURRENT " + patternName(uiSnapshot.currentPattern) + "  |  NEXT " + (queued < 0 ? juce::String("--") : patternName(queued))
            + "  |  " + (mode == takt::PatternChain::Mode::Song ? "SONG" : mode == takt::PatternChain::Mode::Chain ? "CHAIN" : "PATTERN") + "  |  8 banks x 16 patterns", juce::dontSendNotification);
    }
    if (view == View::Song)
    {
        const auto& song = uiSnapshot.song; selectedSongRow = juce::jlimit(0, juce::jmax(0, song.rowCount - 1), selectedSongRow);
        songSelect.setSelectedId(selectedSong + 1, juce::dontSendNotification);
        if (displayedSongRows != song.rowCount)
        {
            songRowSelect.clear(juce::dontSendNotification);
            for (int i = 0; i < song.rowCount; ++i) songRowSelect.addItem("ROW " + number(i + 1), i + 1);
            displayedSongRows = song.rowCount;
        }
        songRowSelect.setSelectedId(song.rowCount == 0 ? 0 : selectedSongRow + 1, juce::dontSendNotification);
        songRowSelect.setTextWhenNothingSelected("EMPTY SONG");
        songDeleteButton.setEnabled(song.rowCount > 0); songPlayButton.setEnabled(song.rowCount > 0); songMutesButton.setEnabled(song.rowCount > 0);
        songQueueButton.setEnabled(song.rowCount > 0 && uiSnapshot.currentSong == selectedSong); songAddButton.setEnabled(song.rowCount < takt::songRowCapacity);
        const auto activeRow = uiSnapshot.currentSongRow;
        const auto playingCaption = uiSnapshot.currentSong == selectedSong && activeRow >= 0 ? "PLAY ROW " + number(activeRow + 1) : "NOT ACTIVE";
        const auto mask = song.rowCount > 0 ? song.rows[static_cast<std::size_t>(selectedSongRow)].muteMask : 0;
        arrangementLabel.setText(juce::String(playingCaption) + "  |  " + juce::String(song.rowCount) + "/99 ROWS  |  MUTES 0x" + juce::String::toHexString(static_cast<int>(mask)).paddedLeft('0', 4)
            + "  |  END " + (song.endLoop ? "LOOP" : "STOP"), juce::dontSendNotification);
    }
    refreshing = wasRefreshing;
}

void TaktAudioProcessorEditor::showSongMuteMenu()
{
    const auto song = processor.getSong(selectedSong); if (song.rowCount == 0) return;
    const auto mask = song.rows[static_cast<std::size_t>(selectedSongRow)].muteMask;
    juce::PopupMenu menu;
    for (int i = 0; i < takt::numTracks; ++i) menu.addItem(i + 1, "MUTE TRACK " + number(i + 1), true, (mask & (1u << i)) != 0);
    menu.addSeparator(); menu.addItem(17, "UNMUTE ALL"); menu.addItem(18, "MUTE ALL");
    const juce::Component::SafePointer<TaktAudioProcessorEditor> safe(this); const auto songSlot = selectedSong, row = selectedSongRow;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&songMutesButton), [safe, songSlot, row](int result)
    {
        if (safe == nullptr || result == 0) return;
        auto changed = safe->processor.getSong(songSlot); if (row >= changed.rowCount) return;
        auto& muteMask = changed.rows[static_cast<std::size_t>(row)].muteMask;
        if (result == 17) muteMask = 0; else if (result == 18) muteMask = 0xffff;
        else muteMask ^= static_cast<std::uint16_t>(1u << (result - 1));
        safe->processor.setSong(songSlot, changed); safe->refreshArrangementControls();
    });
}

void TaktAudioProcessorEditor::applyChain()
{
    const auto tokens = juce::StringArray::fromTokens(chainText.getText().toUpperCase(), " ,;\t\r\n", "");
    std::vector<int> chain;
    for (const auto& token : tokens)
    {
        if (token.isEmpty()) continue;
        if (token.length() < 2 || token.length() > 3 || token[0] < 'A' || token[0] > 'H' || !token.substring(1).containsOnly("0123456789"))
        { showStatus("Use pattern addresses A01-H16 separated by spaces. The chain was left unchanged.", true); return; }
        const auto slot = token.substring(1).getIntValue();
        if (slot < 1 || slot > 16 || chain.size() >= static_cast<std::size_t>(takt::patternChainCapacity))
        { showStatus("A chain accepts 1-64 addresses, each with a pattern number 01-16.", true); return; }
        chain.push_back(static_cast<int>(token[0] - 'A') * 16 + slot - 1);
    }
    if (chain.empty()) { showStatus("Enter a pattern chain first, for example A01 A02 B03.", true); return; }
    finishControlAll(false);
    if (processor.setChain(chain)) { if (!processor.isUsingHostClock()) processor.setParameter("play", 1.0f); refreshArrangementControls(); refreshSteps(); showStatus("Transient chain selected. In HOST SYNC, use Live's transport to hear it."); }
    else showStatus("Chain selection failed.", true);
}

void TaktAudioProcessorEditor::confirmAction()
{
    discardStaleDestinationPreview();
    if (!pendingDestination.isEmpty()) { pendingDestination.clear(); showStatus("LFO destination confirmed."); return; }
    if (view == View::SliceEditor)
    {
        if (funcButton.getToggleState())
        { processor.setParameter(TaktAudioProcessor::trackParameterID(selectedTrack, "sliceByNote"), 0.0f); processor.setParameter(TaktAudioProcessor::trackParameterID(selectedTrack, "slice"), static_cast<float>(selectedSlice)); processor.triggerTrack(selectedTrack); funcButton.setToggleState(false, juce::dontSendNotification); showStatus("Previewing the selected slice."); }
        else showView(View::Parameters);
        return;
    }
    if (view == View::Song && !funcButton.getToggleState()) { songQueueButton.onClick(); return; }
    if (funcButton.getToggleState()) { temporarySaveButton.onClick(); funcButton.setToggleState(false, juce::dontSendNotification); }
    else if (view == View::Parameters && family == Family::Source && (currentMachine() == 5 || currentMachine() == 6)) showSliceMenu();
    else showStatus("YES confirms a menu or LFO destination. On SRC Slice/Grid it opens slice tools.");
}

void TaktAudioProcessorEditor::showSliceMenu()
{
    const auto slices = currentMachine() == 5;
    juce::PopupMenu menu;
    if (slices) { menu.addItem(1, "EDIT SLICE POINTS"); menu.addItem(2, "CREATE SLICE GRID"); }
    menu.addItem(3, "CREATE LINEAR LOCKS"); menu.addItem(4, "CREATE RANDOM LOCKS");
    const juce::Component::SafePointer<TaktAudioProcessorEditor> safe(this); const auto track = selectedTrack;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&yesButton), [safe, track](int result)
    {
        if (safe == nullptr || safe->selectedTrack != track) return;
        if (result == 1) safe->openSliceEditor();
        else if (result == 3 || result == 4) safe->allocateSliceLocks(result == 4);
        else if (result == 2)
        {
            juce::PopupMenu counts; for (int count : {2, 4, 8, 16, 32, 64, 128}) counts.addItem(count, juce::String(count) + " equal slices");
            counts.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&safe->yesButton), [safe, track](int count)
            {
                if (safe == nullptr || safe->selectedTrack != track || count == 0) return;
                juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Replace slice points?",
                    "Create " + juce::String(count) + " equal slices? Existing points will be replaced. Automatic transient detection is not implemented.", "YES", "NO", safe.getComponent(),
                    juce::ModalCallbackFunction::create([safe, track, count](int confirmed)
                    { if (safe != nullptr && confirmed != 0) { safe->processor.createSliceGrid(track, count); if (safe->selectedTrack == track) safe->openSliceEditor(); } }));
            });
        }
    });
}

void TaktAudioProcessorEditor::allocateSliceLocks(bool random)
{
    int slice = 0; const auto count = currentSliceCount(); juce::Random generator;
    for (int i = 0; i < takt::maxSteps; ++i)
    {
        auto step = processor.getStep(selectedTrack, i); if (!step.enabled) continue;
        step.slice = random ? generator.nextInt(count) : slice++ % count; step.lockSlice = true;
        processor.setStep(selectedTrack, i, step);
    }
    refreshSteps(); showStatus(random ? "Random slice locks allocated to existing note trigs." : "Linear slice locks allocated to existing note trigs.");
}

void TaktAudioProcessorEditor::openSliceEditor()
{
    finishControlAll(false); trkButton.setToggleState(false, juce::dontSendNotification);
    selectedSlice = juce::jlimit(0, currentSliceCount() - 1, juce::roundToInt(processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "slice"))));
    toolsVisible = false; toolsButton.setToggleState(false, juce::dontSendNotification);
    sliceZoom = sliceVerticalZoom = 1.0; slicePosition = 0.0; showView(View::SliceEditor);
    showStatus("SLICE EDIT: A links points, D loop, E start, H end. F zoom, G pan. FUNC snaps points to zero crossings; FUNC+arrows split/remove. YES/NO exit.");
}

float TaktAudioProcessorEditor::snapZeroCrossing(float position) const
{
    const auto sample = processor.getSample(selectedTrack); if (!sample || sample->left.size() < 2) return position;
    const auto size = static_cast<int>(sample->left.size()); const auto index = juce::jlimit(1, size - 1, juce::roundToInt(position * static_cast<float>(size - 1)));
    for (int distance = 0; distance < juce::jmin(size, 4096); ++distance)
        for (const auto candidate : {index - distance, index + distance})
            if (candidate > 0 && candidate < size && (sample->left[static_cast<std::size_t>(candidate - 1)] < 0) != (sample->left[static_cast<std::size_t>(candidate)] < 0))
                return static_cast<float>(candidate) / static_cast<float>(size - 1);
    return position;
}

void TaktAudioProcessorEditor::editSlicePoint(int which, float value)
{
    if (view != View::SliceEditor) return;
    auto point = effectiveSlicePoint(selectedSlice); const auto sample = processor.getSample(selectedTrack);
    const auto minimum = sample && !sample->left.empty() ? 1.0f / static_cast<float>(sample->left.size()) : 0.00001f;
    if (funcButton.getToggleState()) value = snapZeroCrossing(value);
    if (which == 0) point.start = juce::jlimit(0.0f, juce::jmax(0.0f, point.end - minimum), value);
    else if (which == 1) point.end = juce::jlimit(juce::jmin(1.0f, point.start + minimum), 1.0f, value);
    else point.loop = juce::jlimit(point.start, point.end, value);
    point.loop = juce::jlimit(point.start, point.end, point.loop);
    processor.setSlicePoint(selectedTrack, selectedSlice, point);
    if (linkedSlicePoints && which == 0 && selectedSlice > 0)
    { auto previous = effectiveSlicePoint(selectedSlice - 1); previous.end = juce::jmax(previous.start + minimum, point.start); previous.loop = juce::jmin(previous.loop, previous.end); processor.setSlicePoint(selectedTrack, selectedSlice - 1, previous); }
    if (linkedSlicePoints && which == 1 && selectedSlice + 1 < currentSliceCount())
    { auto next = effectiveSlicePoint(selectedSlice + 1); next.start = juce::jmin(next.end - minimum, point.end); next.loop = juce::jmax(next.loop, next.start); processor.setSlicePoint(selectedTrack, selectedSlice + 1, next); }
}

void TaktAudioProcessorEditor::moveSlice(int delta)
{
    const auto count = currentSliceCount();
    if (funcButton.getToggleState())
    {
        std::vector<takt::SlicePoint> points; for (int i = 0; i < count; ++i) points.push_back(effectiveSlicePoint(i));
        if (delta > 0 && count < takt::maxSlices)
        { auto point = points[static_cast<std::size_t>(selectedSlice)]; const auto middle = (point.start + point.end) * 0.5f;
          points[static_cast<std::size_t>(selectedSlice)].end = middle; points[static_cast<std::size_t>(selectedSlice)].loop = juce::jmin(point.loop, middle);
          point.start = point.loop = middle; points.insert(points.begin() + selectedSlice + 1, point); }
        else if (delta < 0 && count > 1) points.erase(points.begin() + selectedSlice);
        else { showStatus("Cannot split/remove beyond the slice count limits.", true); funcButton.setToggleState(false, juce::dontSendNotification); return; }
        processor.setParameter(TaktAudioProcessor::trackParameterID(selectedTrack, "sliceCount"), static_cast<float>(points.size()));
        for (std::size_t i = 0; i < points.size(); ++i) processor.setSlicePoint(selectedTrack, static_cast<int>(i), points[i]);
        selectedSlice = juce::jmin(selectedSlice, static_cast<int>(points.size()) - 1); funcButton.setToggleState(false, juce::dontSendNotification);
    }
    else selectedSlice = (selectedSlice + delta + count) % count;
    refreshControls(); panel->repaint();
}

void TaktAudioProcessorEditor::changeStep(const std::function<void(takt::Step&)>& edit)
{
    if (refreshing) return;
    auto step = processor.getStep(selectedTrack, selectedStep); edit(step);
    processor.setStep(selectedTrack, selectedStep, step); refreshSteps(); refreshControls(); panel->repaint();
}

void TaktAudioProcessorEditor::editSelection(int action)
{
    if (view == View::Patterns || view == View::Song)
    { showStatus("Sequence clipboard commands apply to STEP/PAGE/TRACK outside pattern/song editing. Use the visible arrangement controls here.", true); return; }
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
    if (view == View::Patterns || view == View::Song) { showStatus("Sequence undo is available outside pattern/song editing.", true); return; }
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
    if (key == juce::KeyPress::returnKey) { confirmAction(); return true; }
    if ((view == View::Patterns || view == View::Song) && (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey))
    { if (key == juce::KeyPress::leftKey) leftButton.onClick(); else rightButton.onClick(); return true; }
    if (view == View::SliceEditor && (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey))
    { moveSlice(key == juce::KeyPress::leftKey ? -1 : 1); return true; }
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
        if (view == View::Patterns) { selectPatternPad(pad); return true; }
        if (view == View::Song) { selectSong(pad); return true; }
        if (trkButton.getToggleState()) { finishControlAll(false); selectTrack(pad); trkButton.setToggleState(false, juce::dontSendNotification); }
        else if (gridRecording) selectStep(selectedPage * 16 + pad, !mods.isShiftDown());
        else if (mods.isShiftDown()) selectTrack(pad); else processor.triggerTrack(pad);
        return true;
    }
    return false;
}

void TaktAudioProcessorEditor::timerCallback()
{
    discardStaleDestinationPreview();
    processor.servicePendingTransitions();
    uiSnapshot = processor.getUiSnapshot(selectedTrack, selectedPage, selectedStep, selectedSong);
    if (view == View::Song) selectedSongRow = juce::jlimit(0, juce::jmax(0, uiSnapshot.song.rowCount - 1), selectedSongRow);
    timerSliceCount = currentSliceCount();
    const juce::ScopedValueSetter<bool> timerGuard(updatingTimer, true);
    const auto ampMode = juce::roundToInt(processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "ampMode")));
    const auto filterMachine = juce::roundToInt(processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "filterMachine")));
    const auto sourceChanged = currentMachine() != displayedMachine;
    if (sourceChanged || ampMode != displayedAmpMode || filterMachine != displayedFilterMachine
        || (view == View::Song && uiSnapshot.song.rowCount != displayedSongRows))
    { if (sourceChanged && view == View::SliceEditor) view = View::Parameters; rebuildControls(); }
    const auto oldPeak = displayedPeak;
    displayedPeak = juce::jmax(processor.getOutputPeak(), displayedPeak * 0.87f);
    if (displayedPeak < 0.001f) displayedPeak = 0.0f;
    const auto host = processor.isUsingHostClock();
    const auto playing = host ? processor.isHostPlaying() : processor.parameterValue("play") >= 0.5f;
    if (host != displayedHostClock || playing != displayedPlaying)
        panel->repaint(176, 53, 249, 20);
    displayedHostClock = host; displayedPlaying = playing; displayedTempo = processor.parameterValue("tempo");
    if (std::abs(oldPeak - displayedPeak) >= 0.001f || (oldPeak != 0.0f && displayedPeak == 0.0f))
        panel->repaint(757, 645, 73, 8);
    runButton.setToggleState(playing, juce::dontSendNotification);
    fillButton.setToggleState(processor.parameterValue("fill") >= 0.5f, juce::dontSendNotification);
    const auto wasRefreshing = refreshing; refreshing = true;
    trackSpeed.setSelectedId(juce::jlimit(0, 6, juce::roundToInt(processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "speedIndex")))) + 1, juce::dontSendNotification);
    refreshing = wasRefreshing;
    for (int i = 0; i < takt::numTracks; ++i)
    {
        auto& pad = *trackPads[static_cast<std::size_t>(i)];
        const auto selected = i == selectedTrack;
        const auto muted = processor.parameterValue(TaktAudioProcessor::trackParameterID(i, "mute")) >= 0.5f;
        const auto active = playing && uiSnapshot.currentTrigEnabled[static_cast<std::size_t>(i)] && !muted;
        if (pad.selected != selected || pad.muted != muted || pad.playing != active)
        { pad.selected = selected; pad.muted = muted; pad.playing = active; pad.repaint(); }
    }
    const auto& sample = uiSnapshot.sample; const auto& name = uiSnapshot.sampleName;
    if (lastSample != sample || lastSampleName != name)
    {
        lastSample = sample; lastSampleName = name; waveform->setSample(sample);
        sampleLabel.setText(name.isEmpty() ? "No sample loaded" : name, juce::dontSendNotification);
        sampleInfoLabel.setText(sample && !sample->left.empty()
            ? juce::String(sample->sampleRate / 1000.0, 1) + " kHz | " + (sample->right.empty() ? "MONO" : "STEREO") + " | " + juce::String(static_cast<double>(sample->left.size()) / sample->sampleRate, 2) + " seconds"
            : "WAV / AIFF / FLAC | Import or drop your audio", juce::dontSendNotification);
    }
    const auto machine = currentMachine();
    if (view == View::SliceEditor || machine == 5 || machine == 6)
    {
        const auto count = currentSliceCount(); selectedSlice = juce::jlimit(0, count - 1, selectedSlice);
        const auto slice = view == View::SliceEditor ? selectedSlice : juce::jlimit(0, count - 1, juce::roundToInt(processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "slice"))));
        waveformMarkers.clear();
        for (int i = 0; i < count; ++i) waveformMarkers.push_back(machine == 6 ? static_cast<float>(i) / static_cast<float>(count) : effectiveSlicePoint(i).start);
        const auto point = machine == 6 ? takt::SlicePoint{static_cast<float>(slice) / static_cast<float>(count), static_cast<float>(slice + 1) / static_cast<float>(count), static_cast<float>(slice) / static_cast<float>(count)} : effectiveSlicePoint(slice);
        waveform->setRegion(point.start, point.end); waveform->setMarkers(waveformMarkers, point.loop, view == View::SliceEditor);
        waveform->setViewport(view == View::SliceEditor ? sliceZoom : 1.0, slicePosition, view == View::SliceEditor ? sliceVerticalZoom : 1.0);
    }
    else
    {
        const auto start = machine == 2 ? 0.0f : processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "start"));
        const auto end = machine == 0 ? processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "end"))
            : machine == 2 ? 1.0f : juce::jmin(1.0f, start + processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "sourceLength")));
        waveform->setRegion(start, end); waveform->setMarkers({}, machine == 1 ? processor.parameterValue(TaktAudioProcessor::trackParameterID(selectedTrack, "loopPosition")) : -1.0f, false);
        waveform->setViewport(1.0, 0.0, 1.0);
    }
    if (view == View::Parameters && family == Family::Mod)
    {
        const auto id = TaktAudioProcessor::trackParameterID(selectedTrack, "lfo" + juce::String(parameterPages[5] + 1) + "_wave");
        encoders[5]->present(processor.parameterValue(id) >= 5.5f ? "SLEW" : "SPH", true, "Start phase 0-127; for RND this smooths transitions instead.");
    }
    refreshSteps(); refreshControls(); refreshArrangementControls(); undoButton.setEnabled(uiSnapshot.canUndo);
    refreshImportControls();
    const auto droppedTriggers = processor.getDroppedUiTriggers(), droppedMidi = processor.getDroppedMidiEvents();
    if (droppedTriggers != displayedDroppedTriggers || droppedMidi != displayedDroppedMidi)
    {
        displayedDroppedTriggers = droppedTriggers; displayedDroppedMidi = droppedMidi;
        toolsButton.setTooltip("Open software utilities: tempo, swing, length, clipboard scope, demo and selected-track controls. Dropped events since opening the plugin: MIDI "
            + juce::String(static_cast<juce::int64>(droppedMidi)) + ", audition " + juce::String(static_cast<juce::int64>(droppedTriggers)) + ".");
    }
    if (juce::Time::getMillisecondCounterHiRes() > statusExpiry)
    {
        statusLabel.setColour(juce::Label::textColourId, mutedInk);
        statusLabel.setText(processor.isSampleImportPending(selectedTrack) ? "Loading sample in the background | CANCEL IMPORT cancels this track's pending import"
            : view == View::Patterns ? "PATTERNS: choose bank A-H, then pad 1-16 | edit or play a chain | BACK returns to parameters"
            : view == View::Song ? "SONG: pads select songs | ADD ROW | encoders edit the selected row | BACK returns to parameters"
            : gridRecording ? "GRID: click a pad to toggle a step | right-click selects | STEP TOOLS edits locks | ? shows all shortcuts" : "PLAY: pads trigger tracks | right-click selects silently | REC returns to grid editing | ? shows all shortcuts", juce::dontSendNotification);
    }
    const auto playingPage = playing && displayedCurrentStep >= 0 ? displayedCurrentStep / 16 : -1;
    if (displayedPlayingPage != playingPage)
    { displayedPlayingPage = playingPage; panel->repaint(659, 423, 172, 57); }
    auto oled = juce::String(uiSnapshot.currentPattern) + ":" + juce::String(uiSnapshot.performKit ? 1 : 0)
        + ":" + juce::String(selectedTrack) + ":" + juce::String(selectedStep) + ":" + juce::String(displayedTrackLength)
        + ":" + juce::String(static_cast<int>(view)) + ":" + juce::String(static_cast<int>(family))
        + ":" + juce::String(parameterPages[static_cast<std::size_t>(family)]) + ":" + juce::String(sendFxPage)
        + ":" + juce::String(displayedMachine) + ":" + juce::String(displayedFilterMachine) + ":" + juce::String(displayedAmpMode)
        + ":" + juce::String(selectedBank) + ":" + juce::String(selectedSong) + ":" + juce::String(selectedSongRow)
        + ":" + juce::String(selectedSlice) + ":" + juce::String(currentSliceCount()) + ":" + juce::String(linkedSlicePoints ? 1 : 0)
        + ":" + juce::String(host ? 1 : 0) + ":" + juce::String(displayedTempo, 1);
    for (const auto& encoder : encoders)
        oled += ":" + encoder->caption() + ":" + encoder->slider.getTextFromValue(encoder->slider.getValue()) + (encoder->slider.isEnabled() ? ":1" : ":0");
    if (lastOledContents != oled)
    { lastOledContents = std::move(oled); panel->repaint(165, 75, 273, 222); }
    if (++timerTicks % 6 == 0) processor.releaseUnusedSamples();
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
    const auto destinationPattern = processor.getCurrentPattern();
    fileChooser = std::make_unique<juce::FileChooser>("Load a sample for " + patternName(destinationPattern) + " / track " + number(destinationTrack + 1), juce::File{}, "*.wav;*.aif;*.aiff;*.flac", true);
    const juce::Component::SafePointer<TaktAudioProcessorEditor> safe(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, destinationTrack, destinationPattern](const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            const auto file = chooser.getResult();
            if (file.existsAsFile()) safe->importSample(file, destinationTrack, destinationPattern);
            juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->fileChooser.reset(); });
        });
}

void TaktAudioProcessorEditor::importSample(const juce::File& file)
{
    importSample(file, selectedTrack, processor.getCurrentPattern());
}

void TaktAudioProcessorEditor::importSample(const juce::File& file, int destinationTrack, int destinationPattern)
{
    const juce::Component::SafePointer<TaktAudioProcessorEditor> safe(this);
    const auto destination = patternName(destinationPattern) + " / track " + number(destinationTrack + 1);
    const auto ticket = processor.loadSampleAsync(destinationTrack, file,
        [safe, destination, name = file.getFileName()](bool success, const juce::String& error)
        {
            if (safe == nullptr) return;
            safe->showStatus(success ? "Loaded " + name + " on " + destination + "." : error, !success);
            safe->timerCallback();
        }, destinationPattern);
    if (ticket != 0)
    {
        showStatus("Loading " + file.getFileName() + " into " + destination + "... IMPORT cancels this track's pending import.");
        refreshImportControls();
    }
}

void TaktAudioProcessorEditor::cancelSelectedImport()
{
    processor.cancelSampleImport(selectedTrack);
    showStatus("Sample import cancelled on track " + number(selectedTrack + 1) + ".");
    refreshImportControls();
}

void TaktAudioProcessorEditor::refreshImportControls()
{
    const auto pending = processor.isSampleImportPending(selectedTrack);
    importButton.setButtonText(pending ? "CANCEL IMPORT" : "IMPORT SAMPLE");
    sourceImportButton.setButtonText(pending ? "CANCEL" : "IMPORT");
    const auto tip = pending ? "Cancel the pending sample import for this track. Other controls remain available while the file loads."
                             : "Import WAV, AIFF or FLAC into this track. You can also drop a file on the panel.";
    importButton.setTooltip(tip); sourceImportButton.setTooltip(tip);
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
