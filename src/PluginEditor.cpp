#include "PluginEditor.h"

BladesLook::BladesLook() {
    setColour(juce::Slider::textBoxTextColourId, text());
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, text());
    setColour(juce::TextButton::textColourOffId, dim());
    setColour(juce::TextButton::textColourOnId, bg());
}

void BladesLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider& s) {
    const float d = (float) std::min(w, h) - 6.0f;
    const float cx = x + w * 0.5f, cy = y + h * 0.5f, r = d * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool on = s.isEnabled();
    // track
    juce::Path track; track.addCentredArc(cx, cy, r, r, 0.0f, startAngle, endAngle, true);
    g.setColour(panel().brighter(0.15f)); g.strokePath(track, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // value arc
    juce::Path val; val.addCentredArc(cx, cy, r, r, 0.0f, startAngle, angle, true);
    g.setColour(on ? accent() : dim().withAlpha(0.5f)); g.strokePath(val, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // knob body and pointer
    const float br = r - 7.0f;
    g.setColour(panel().brighter(on ? 0.05f : 0.0f)); g.fillEllipse(cx - br, cy - br, br * 2, br * 2);
    g.setColour(juce::Colours::black.withAlpha(0.35f)); g.drawEllipse(cx - br, cy - br, br * 2, br * 2, 1.0f);
    juce::Path p; p.addRoundedRectangle(-1.5f, -br + 3.0f, 3.0f, br * 0.55f, 1.5f);
    g.setColour(on ? text() : dim()); g.fillPath(p, juce::AffineTransform::rotation(angle).translated(cx, cy));
}

void BladesLook::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool) {
    auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = b.getToggleState();
    g.setColour(on ? accent() : (highlighted ? panel().brighter(0.2f) : panel()));
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(on ? accent() : panel().brighter(0.3f)); g.drawRoundedRectangle(r, 5.0f, 1.0f);
}

void BladesEditor::addKnob(Knob& k, const juce::String& id, const juce::String& name, bool small) {
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, small ? 76 : 90, 16);
    k.slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    k.slider.setDoubleClickReturnValue(true, proc.apvts.getParameter(id)->convertFrom0to1(proc.apvts.getParameter(id)->getDefaultValue()));
    k.label.setText(name, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setFont(juce::FontOptions(small ? 10.5f : 11.5f, juce::Font::bold));
    k.label.setColour(juce::Label::textColourId, BladesLook::dim());
    addAndMakeVisible(k.slider); addAndMakeVisible(k.label);
    k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, id, k.slider);
}

BladesEditor::BladesEditor(UnofficialBladesProcessor& p) : AudioProcessorEditor(&p), proc(p) {
    setLookAndFeel(&look);
    static const char* names[5] = { "FREQUENCY", "RESONANCE", "MORPH", "LEVEL", "DRIVE" };
    static const char* ids[5] = { "freq", "reso", "morph", "level", "drive" };
    for (int i = 0; i < 5; ++i) {
        addKnob(f1[i], juce::String(ids[i]) + "1", names[i]);
        addKnob(f2[i], juce::String(ids[i]) + "2", names[i]);
    }
    addKnob(topo, "topo", "TOPOLOGY");
    addKnob(offset, "offset", "OFFSET (LINKED)");
    addKnob(fm1, "fm1", "FM 1", true);
    addKnob(fm2, "fm2", "FM 2", true);
    addKnob(inGain, "ingain", "INPUT", true);
    addKnob(outGain, "outgain", "OUTPUT", true);

    auto hzText = [](double v) { return v < 1000.0 ? juce::String(v, v < 100.0 ? 1 : 0) + " Hz" : juce::String(v / 1000.0, 2) + " kHz"; };
    auto hzValue = [](const juce::String& s) { double v = s.getDoubleValue(); return s.containsIgnoreCase("k") ? v * 1000.0 : v; };
    auto pct = [](double v) { return juce::String((int) std::lround(v * 100.0)) + " %"; };
    auto pctValue = [](const juce::String& s) { return s.getDoubleValue() / 100.0; };
    auto morphText = [pct](double v) { return v < 0.03 ? juce::String("LP") : std::fabs(v - 0.5) < 0.03 ? juce::String("BP") : v > 0.97 ? juce::String("HP") : pct(v); };
    for (auto* arr : { f1, f2 }) {
        arr[0].slider.textFromValueFunction = hzText;  arr[0].slider.valueFromTextFunction = hzValue;
        for (int i = 1; i < 5; ++i) arr[i].slider.valueFromTextFunction = pctValue;
        arr[1].slider.textFromValueFunction = pct;
        arr[2].slider.textFromValueFunction = morphText;
        arr[3].slider.textFromValueFunction = pct;
        arr[4].slider.textFromValueFunction = pct;
    }
    topo.slider.textFromValueFunction = [pct](double v) { return v < 0.03 ? juce::String("serial 1 > 2") : std::fabs(v - 0.5) < 0.03 ? juce::String("parallel") : v > 0.97 ? juce::String("1 only") : pct(v); };
    topo.slider.valueFromTextFunction = pctValue;
    offset.slider.textFromValueFunction = [](double v) { return (v >= 0 ? "+" : "") + juce::String(v, 2) + " oct"; };
    auto signedPct = [](double v) { return (v > 0 ? "+" : "") + juce::String((int) std::lround(v * 100.0)) + " %"; };
    fm1.slider.textFromValueFunction = fm2.slider.textFromValueFunction = signedPct;
    fm1.slider.valueFromTextFunction = fm2.slider.valueFromTextFunction = pctValue;
    auto dbf = [](double v) { return juce::String(v, 1) + " dB"; };
    inGain.slider.textFromValueFunction = outGain.slider.textFromValueFunction = dbf;
    for (auto* arr : { f1, f2 }) for (int i = 0; i < 5; ++i) arr[i].slider.updateText();
    topo.slider.updateText(); offset.slider.updateText(); fm1.slider.updateText(); fm2.slider.updateText(); inGain.slider.updateText(); outGain.slider.updateText();

    // routing buttons
    static const char* ioNames[3] = { "MONO IN > MIX OUT", "STEREO: L > 1, R > 2", "MONO IN > SPLIT OUT" };
    auto* ioParam = dynamic_cast<juce::RangedAudioParameter*>(proc.apvts.getParameter("io"));
    ioAttach = std::make_unique<juce::ParameterAttachment>(*ioParam, [this](float v) {
        const int sel = (int) std::lround(v);
        for (int i = 0; i < 3; ++i) ioButtons[i].setToggleState(i == sel, juce::dontSendNotification);
    });
    for (int i = 0; i < 3; ++i) {
        ioButtons[i].setButtonText(ioNames[i]);
        ioButtons[i].setClickingTogglesState(false);
        ioButtons[i].onClick = [this, i] { ioAttach->setValueAsCompleteGesture((float) i); };
        addAndMakeVisible(ioButtons[i]);
    }
    ioAttach->sendInitialUpdate();

    // link button: when on, filter 2's frequency knob is dimmed and the offset knob takes over
    auto* linkParam = dynamic_cast<juce::RangedAudioParameter*>(proc.apvts.getParameter("link"));
    linkAttach = std::make_unique<juce::ParameterAttachment>(*linkParam, [this](float v) {
        const bool on = v > 0.5f;
        linkButton.setToggleState(on, juce::dontSendNotification);
        f2[0].slider.setEnabled(!on); f2[0].label.setEnabled(!on); f2[0].label.setAlpha(on ? 0.4f : 1.0f);
        offset.slider.setEnabled(on); offset.label.setEnabled(on); offset.label.setAlpha(on ? 1.0f : 0.4f);
        repaint();
    });
    linkButton.setButtonText("LINK 2 TO 1");
    linkButton.setClickingTogglesState(false);
    linkButton.onClick = [this] { linkAttach->setValueAsCompleteGesture(linkButton.getToggleState() ? 0.0f : 1.0f); };
    addAndMakeVisible(linkButton);
    linkAttach->sendInitialUpdate();

    hint.setText("FM: send a track to this plug-in's sidechain input and turn up FM 1 / FM 2 (negative values invert).\n"
                 "LINK: filter 2 follows filter 1 and its frequency knob becomes OFFSET.\n"
                 "TOPOLOGY (mix output only): 0 = filter 1 into filter 2, middle = parallel, 100 = filter 1 alone.\n"
                 "DRIVE: 0 = clean; turning it up folds the signal back on itself.",
                 juce::dontSendNotification);
    hint.setFont(juce::FontOptions(10.5f)); hint.setColour(juce::Label::textColourId, BladesLook::dim());
    hint.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(hint);
    setSize(780, 490);
}

BladesEditor::~BladesEditor() { setLookAndFeel(nullptr); }

void BladesEditor::paint(juce::Graphics& g) {
    g.fillAll(BladesLook::bg());
    g.setColour(BladesLook::panel()); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 10.0f);
    g.setColour(BladesLook::text()); g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("UNOFFICIAL BLADES", 28, 18, 360, 30, juce::Justification::centredLeft);
    g.setColour(BladesLook::dim()); g.setFont(juce::FontOptions(12.0f));
    g.drawText("dual multimode filter, modelled from a published circuit", 28, 46, 460, 18, juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("INPUT / OUTPUT", 28, 72, 140, 14, juce::Justification::centredLeft);
    g.setColour(BladesLook::accent());
    g.drawText("FILTER 1", 28, 130, 100, 14, juce::Justification::centredLeft);
    g.drawText("FILTER 2", 28, 262, 100, 14, juce::Justification::centredLeft);
    g.drawText("BOTH", 540, 130, 100, 14, juce::Justification::centredLeft);
    g.setColour(BladesLook::panel().brighter(0.2f));
    g.drawHorizontalLine(126, 28.0f, (float) getWidth() - 28.0f);
    g.drawHorizontalLine(258, 28.0f, (float) getWidth() - 28.0f);
    g.drawHorizontalLine(390, 28.0f, (float) getWidth() - 28.0f);
    g.drawVerticalLine(524, 130.0f, 386.0f);
}

void BladesEditor::resized() {
    // routing row
    {
        const int bw = 188, gap = 8, y = 88;
        for (int i = 0; i < 3; ++i) ioButtons[i].setBounds(28 + i * (bw + gap), y, bw, 26);
        linkButton.setBounds(getWidth() - 28 - 140, y, 140, 26);
    }
    const int kw = 96, kh = 108;
    auto place = [](Knob& k, juce::Rectangle<int> cell, int labelH) {
        k.label.setBounds(cell.removeFromTop(labelH));
        k.slider.setBounds(cell);
    };
    for (int i = 0; i < 5; ++i) {
        place(f1[i], juce::Rectangle<int>(28 + i * kw, 146, kw, kh), 16);
        place(f2[i], juce::Rectangle<int>(28 + i * kw, 278, kw, kh), 16);
    }
    // right-hand column
    place(topo,   juce::Rectangle<int>(540, 146, 106, kh), 16);
    place(offset, juce::Rectangle<int>(646, 146, 106, kh), 16);
    place(fm1,    juce::Rectangle<int>(540, 278, 106, kh), 16);
    place(fm2,    juce::Rectangle<int>(646, 278, 106, kh), 16);
    // bottom row: gains and the hints
    place(inGain,  juce::Rectangle<int>(28, 396, 100, 86), 14);
    place(outGain, juce::Rectangle<int>(132, 396, 100, 86), 14);
    hint.setBounds(256, 398, getWidth() - 256 - 28, 84);
}
