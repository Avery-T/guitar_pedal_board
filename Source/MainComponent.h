#pragma once

#include <JuceHeader.h>

//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
enum audioState{
	Distortion,
	Clean
};
class MainComponent  : public juce::AudioAppComponent, public juce::Slider::Listener, public juce::Button::Listener,public juce::Timer,public juce::ChangeListener
{
public:
    //==============================================================================
    MainComponent();
    ~MainComponent() override;
    //==============================================================================
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;
    //==============================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;
    //====me===
	  void sliderValueChanged(juce::Slider * slider) override;
	  void buttonClicked(juce::Button * button) override;
	 //what i made
	  void changeState( audioState newState);
		
	//for the visualizer
	void pushNextSampleIntoFifo(float sample);
	void drawNextFrameOfSpectrum(); 
	void timerCallback() override;
	enum
	{
		fftOrder=11,
		fftSize=1<<fftOrder,
		scopeSize=512
	};
	
private:
	void changeListenerCallback (juce::ChangeBroadcaster*) override{}
	juce::dsp::FFT forwardFFT;
	juce::dsp::WindowingFunction<float> window;
	float fifo [fftSize];
	float fftData[2*fftSize];
	int fifoIndex=0;
	bool nextFFtBlockReady=false;
	float scopeData[scopeSize];
	
	juce::Random random;
	juce::Slider levelSlider;
	juce::Slider frequencySlider;
	juce::Slider outputGainSlider;
	juce::Slider inputGainSlider; 
	juce::Label outputGainLabel;
	juce::Label inputGainLabel;
  juce::Label frequencyLabel;
	juce::TextButton distortionBtn;
	juce::TextButton cleanBtn; 
	juce::Label distortionLabel;
	audioState state;
	
	enum{
		filterIndex,
		inputGainIndex,
		waveShaperIndex,
		outputGainIndex,
		convolutionIndex
	};
	
	juce::AudioDeviceSelectorComponent audioSetupComp;
	juce::Label cpuUsageLabel;
	juce::Label cpuUsageText;
	juce::TextEditor diagnosticsBox;

	static float distortionAmount;
	static float distortionFunction(float x);
	using Filter =      juce::dsp::IIR::Filter<float>;
	using FilterCoefs = juce::dsp::IIR::Coefficients<float>;
	
	juce::dsp::ProcessorChain<juce::dsp::ProcessorDuplicator<Filter, FilterCoefs>, juce::dsp::Gain<float>, juce::dsp::WaveShaper<float>, juce::dsp::Gain<float>,juce::dsp::Convolution> processorChain;
	
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};



