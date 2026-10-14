#include "PluginProcessor.h"
#include "PluginEditor.h"

UnofficialBladesProcessor::UnofficialBladesProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                                      .withInput("Sidechain (FM)", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "STATE", createLayout()) {}

juce::AudioProcessorValueTreeState::ParameterLayout UnofficialBladesProcessor::createLayout() {
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    // Frequency: the pole frequency of both integrators of a filter, 5 Hz .. about 24.1 kHz (the top of the VCA range in the drawing), log scale
    NormalisableRange<float> fr(5.0f, (float) blades::kStageMaxHz, 0.0f, 0.25f);
    auto knob = [&](const String& id, const String& name, float def) {
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ id, 1 }, name, NormalisableRange<float>(0.0f, 1.0f), def));
    };
    for (int ch = 1; ch <= 2; ++ch) {
        const String n(ch);
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "freq" + n, 1 }, "Frequency " + n, fr, 1000.0f, AudioParameterFloatAttributes().withLabel("Hz")));
        knob("reso" + n, "Resonance " + n, 0.2f);
        knob("morph" + n, "Morph " + n, 0.0f);
        knob("level" + n, "Level " + n, 0.62f);    // 0.62 = unity gain in the drawing's input level servo
        knob("drive" + n, "Drive " + n, 0.0f);
    }
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "topo", 1 }, "Topology", NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{ "link", 1 }, "Link 2 to 1", false));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "offset", 1 }, "Offset 2 (linked)", NormalisableRange<float>(-5.0f, 5.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel("oct")));
    // FM: the sidechain signal moves the Frequency exponentially. 1.0 = one octave per volt (full scale = 5 V); negative values invert.
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "fm1", 1 }, "FM 1 amount", NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "fm2", 1 }, "FM 2 amount", NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "io", 1 }, "Input / output",
        StringArray{ "Mono in > mix out", "Stereo: L > 1, R > 2, split out", "Mono in > split out" }, 0));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "ingain", 1 }, "Input gain", NormalisableRange<float>(-24.0f, 24.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel("dB")));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "outgain", 1 }, "Output gain", NormalisableRange<float>(-24.0f, 24.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel("dB")));
    return { p.begin(), p.end() };
}

void UnofficialBladesProcessor::prepareToPlay(double sampleRate, int) {
    fs = sampleRate;
    unit.reset();
    first = true;
    setLatencySamples(blades::Oversampler2x::latencySamples());
}

bool UnofficialBladesProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (in != out) return false;
    if (!(out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())) return false;
    const auto sc = l.getChannelSet(true, 1);   // the FM sidechain: off, mono or stereo
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void UnofficialBladesProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    auto mainBuf = getBusBuffer(buf, false, 0);
    const int chans = std::min(mainBuf.getNumChannels(), 2);
    // FM sidechain (if the host has connected one): read it before anything else
    const float* sc[2] = { nullptr, nullptr }; int scChans = 0;
    if (auto* scBus = getBus(true, 1)) if (scBus->isEnabled()) {
        auto scBuf = getBusBuffer(buf, true, 1);
        scChans = std::min(scBuf.getNumChannels(), 2);
        for (int c = 0; c < scChans; ++c) sc[c] = scBuf.getReadPointer(c);
    }
    auto get = [&](const char* id) { return (double) *apvts.getRawParameterValue(id); };
    const double tHz1 = get("freq1"), tHz2 = get("freq2");
    const double tReso1 = get("reso1"), tReso2 = get("reso2"), tMorph1 = get("morph1"), tMorph2 = get("morph2");
    const double tLevel1 = get("level1"), tLevel2 = get("level2"), tDrive1 = get("drive1"), tDrive2 = get("drive2");
    const double tTopo = get("topo"), tOffset = get("offset");
    const bool link = get("link") > 0.5;
    const double fm1Amt = get("fm1"), fm2Amt = get("fm2");
    const int io = juce::jlimit(0, 2, (int) std::lround(get("io")));
    const double inG = std::pow(10.0, get("ingain") / 20.0);
    const double outG = std::pow(10.0, get("outgain") / 20.0);
    const double l1 = std::log(std::max(1.0, tHz1)), l2 = std::log(std::max(1.0, tHz2));
    if (first) {
        sLogHz1.snap(l1); sLogHz2.snap(l2); sReso1.snap(tReso1); sReso2.snap(tReso2); sMorph1.snap(tMorph1); sMorph2.snap(tMorph2);
        sLevel1.snap(tLevel1); sLevel2.snap(tLevel2); sDrive1.snap(tDrive1); sDrive2.snap(tDrive2); sTopo.snap(tTopo); sOffset.snap(tOffset);
        first = false;
    }
    const double a = 1.0 - std::exp(-1.0 / (0.005 * fs));       // about 5 ms smoothing so automation and knob moves do not zipper
    const double fmScale1 = fm1Amt * blades::kVoltsPerFS, fmScale2 = fm2Amt * blades::kVoltsPerFS;   // octaves per unit of sidechain level
    float* d[2] = { mainBuf.getWritePointer(0), chans > 1 ? mainBuf.getWritePointer(1) : nullptr };
    blades::Params p;
    p.link = link;
    for (int i = 0; i < n; ++i) {
        sLogHz1.run(l1, a); sLogHz2.run(l2, a); sReso1.run(tReso1, a); sReso2.run(tReso2, a); sMorph1.run(tMorph1, a); sMorph2.run(tMorph2, a);
        sLevel1.run(tLevel1, a); sLevel2.run(tLevel2, a); sDrive1.run(tDrive1, a); sDrive2.run(tDrive2, a); sTopo.run(tTopo, a); sOffset.run(tOffset, a);
        p.hz1 = std::exp(sLogHz1.v); p.hz2 = std::exp(sLogHz2.v);
        p.reso1 = sReso1.v; p.reso2 = sReso2.v; p.morph1 = sMorph1.v; p.morph2 = sMorph2.v;
        p.level1 = sLevel1.v; p.level2 = sLevel2.v; p.drive1 = sDrive1.v; p.drive2 = sDrive2.v;
        p.topo = sTopo.v; p.offsetOct = sOffset.v;
        double fm = 0.0;
        if (scChans == 1) fm = sc[0][i]; else if (scChans == 2) fm = 0.5 * (sc[0][i] + sc[1][i]);
        const double L = d[0][i], R = chans > 1 ? d[1][i] : L;
        double in1, in2; bool plugged;
        if (chans < 2 || io != 1) { in1 = (chans > 1) ? 0.5 * (L + R) : L; in2 = 0.0; plugged = false; }   // one input; input 2 is normalled
        else { in1 = L; in2 = R; plugged = true; }
        float mix, o1, o2;
        unit.process(in1 * inG, in2 * inG, plugged, p, fs, fm * fmScale1, fm * fmScale2, outG, mix, o1, o2);
        if (chans < 2 || io == 0) { d[0][i] = mix; if (chans > 1) d[1][i] = mix; }
        else { d[0][i] = o1; d[1][i] = o2; }
    }
}

juce::AudioProcessorEditor* UnofficialBladesProcessor::createEditor() { return new BladesEditor(*this); }

void UnofficialBladesProcessor::getStateInformation(juce::MemoryBlock& dest) {
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, dest);
}
void UnofficialBladesProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) if (xml->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new UnofficialBladesProcessor(); }
