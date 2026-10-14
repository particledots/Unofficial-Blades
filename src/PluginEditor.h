#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// A plain, original look: dark panel, round knobs with a value arc, buttons for the routing.
class BladesLook : public juce::LookAndFeel_V4 {
public:
    BladesLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return juce::FontOptions(13.0f, juce::Font::bold); }
    static juce::Colour bg()     { return juce::Colour(0xff1c1f24); }
    static juce::Colour panel()  { return juce::Colour(0xff262a31); }
    static juce::Colour accent() { return juce::Colour(0xff4fb3c8); }
    static juce::Colour text()   { return juce::Colour(0xffe6e8eb); }
    static juce::Colour dim()    { return juce::Colour(0xff8b929c); }
};

class BladesEditor : public juce::AudioProcessorEditor {
public:
    explicit BladesEditor(UnofficialBladesProcessor&);
    ~BladesEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    void addKnob(Knob&, const juce::String& id, const juce::String& name, bool small = false);

    UnofficialBladesProcessor& proc;
    BladesLook look;
    Knob f1[5], f2[5];                 // frequency, resonance, morph, level, drive
    Knob topo, offset, fm1, fm2, inGain, outGain;
    juce::TextButton ioButtons[3];
    juce::TextButton linkButton;
    std::unique_ptr<juce::ParameterAttachment> ioAttach, linkAttach;
    juce::Label hint;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BladesEditor)
};
