#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "BladesModel.h"

class UnofficialBladesProcessor : public juce::AudioProcessor {
public:
    UnofficialBladesProcessor();
    ~UnofficialBladesProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Unofficial Blades"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }   // a resonant filter can ring (or self-oscillate) when the input stops

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    blades::Unit unit;
    // smoothed copies of the knobs (frequencies are smoothed on a log scale)
    struct Smooth { double v = 0.0; void snap(double x) { v = x; } void run(double target, double a) { v += a * (target - v); } };
    Smooth sLogHz1, sLogHz2, sReso1, sReso2, sMorph1, sMorph2, sLevel1, sLevel2, sDrive1, sDrive2, sTopo, sOffset;
    double fs = 44100.0;
    bool first = true;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UnofficialBladesProcessor)
};
