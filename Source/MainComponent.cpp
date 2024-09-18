#include "MainComponent.h"

//==============================================================================
float MainComponent::distortionAmount = 1.0f;
float MainComponent::distortionFunction(float x)
{
	return std::tanh(distortionAmount * x);
}

MainComponent::MainComponent() : forwardFFT(fftOrder),window(fftSize,juce::dsp::WindowingFunction<float>::hann), state(Clean), 
audioSetupComp (deviceManager,
																																		  0,     // minimum input channels
																																				256,   // maximum input channels
																																				0,     // minimum output channels
																																				256,   // maximum output channels
																																				false, // ability to select midi inputs
																																				false, // ability to select midi output device
																																				false, // treat channels as stereo pairs
																																				false) // hide advanced options
{
	
	addAndMakeVisible (audioSetupComp);
	addAndMakeVisible (diagnosticsBox);

	diagnosticsBox.setMultiLine (true);
	diagnosticsBox.setReturnKeyStartsNewLine (true);
	diagnosticsBox.setReadOnly (true);
	diagnosticsBox.setScrollbarsShown (true);
	diagnosticsBox.setCaretVisible (false);
	diagnosticsBox.setPopupMenuEnabled (true);
	diagnosticsBox.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0x32ffffff));
	diagnosticsBox.setColour (juce::TextEditor::outlineColourId,    juce::Colour (0x1c000000));
	diagnosticsBox.setColour (juce::TextEditor::shadowColourId,     juce::Colour (0x16000000));
	deviceManager.addChangeListener (this);
	// Make sure you set the size of the component after
	// you add any child components.
	setSize (1920, 1080);
	//setAudioChannels(2,2);
	std::fill(std::begin(scopeData), std::end(scopeData), 0.0f);
	startTimerHz(30);
	addAndMakeVisible(frequencySlider);
	addAndMakeVisible(outputGainSlider);
	addAndMakeVisible(inputGainSlider);
	frequencySlider.setRange(0,10);
	outputGainSlider.setRange(-20,20);
	inputGainSlider.setRange(0,30);
	frequencySlider.setTextValueSuffix(" Hz");
	frequencySlider.addListener(this);
	outputGainSlider.addListener(this);
	inputGainSlider.addListener(this);
	//label
	
	frequencyLabel.setText ("Freqy", juce::dontSendNotification);
	frequencyLabel.attachToComponent(&frequencySlider, true);

	inputGainLabel.setText ("InputGain", juce::dontSendNotification);
	inputGainLabel.attachToComponent(&inputGainSlider, true);

	outputGainLabel.setText ("OutPutGain", juce::dontSendNotification);
	outputGainLabel.attachToComponent(&outputGainSlider, true);
	
	//button
	addAndMakeVisible(&distortionBtn);
	distortionBtn.setButtonText("Guitar Cabinet");
	distortionBtn.onClick = [this] {changeState(Distortion);};
	//clean 
	addAndMakeVisible(&cleanBtn);
	cleanBtn.setButtonText("Clean");
	cleanBtn.onClick = [this] {changeState(Clean);};
	
	//processor chain
	processorChain.get<inputGainIndex>().setGainDecibels(30.0f);
	processorChain.get<waveShaperIndex>().functionToUse = &MainComponent::distortionFunction;
	processorChain.get<outputGainIndex>().setGainDecibels(0.0f); //-20
		
    // Some platforms require permissions to open input channels so request that here
    if (juce::RuntimePermissions::isRequired (juce::RuntimePermissions::recordAudio)
        && ! juce::RuntimePermissions::isGranted (juce::RuntimePermissions::recordAudio))
    {
        juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio,
                                           [&] (bool granted) { setAudioChannels (granted ? 2 : 0, 2); });
    }
    else
		{
			// Specify the number of input and output channels that we want to open
			setAudioChannels (2, 2);
		}
}

MainComponent::~MainComponent()
{
    // This shuts down the audio device and clears the audio source.
    shutdownAudio();
}

//==============================================================================
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
	// This function will be called when the audio device is started, or when
	// its settings (i.e. sample rate, block size, etc) are changed.
	juce::dsp::ProcessSpec spec;
	spec.sampleRate=sampleRate;
	spec.maximumBlockSize=samplesPerBlockExpected;
	spec.numChannels=2;
	auto& filter = processorChain.template get<filterIndex>();
	filter.state = FilterCoefs::makeFirstOrderLowPass(spec.sampleRate, 1000.0f);
	processorChain.prepare(spec);

	
	 juce::String message;
	message << "Preparing to play audio..\n";
	message << " SamplesPerBlockExpexted = " << samplesPerBlockExpected << "\n";
	message << " sample rate = " << sampleRate;
	juce::Logger::getCurrentLogger()->writeToLog(message);
	// You can use this function to initialise any resources you might need,
	// but be careful - it will be called on the audio thread, not the GUI thread.
	// For more details, see the help for AudioProcessor::prepareToPlay()
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
	// Your audio-processing code goes here!
	juce::dsp::AudioBlock<float>block(*bufferToFill.buffer);
	juce::dsp::ProcessContextReplacing<float> context(block);
	processorChain.process(context);
	/* for the spectrum*/
	if(bufferToFill.buffer->getNumChannels() > 0)
	{
		auto * channelData = bufferToFill.buffer-> getReadPointer (0, bufferToFill.startSample);
		for(auto i=0; i<bufferToFill.numSamples; ++i)
		{
			pushNextSampleIntoFifo(channelData[i]);
		}
	}
}
void MainComponent::pushNextSampleIntoFifo(float sample)
{
	//if the fifof contains enough data set a flag to say that the next frame should be rendered
	if(fifoIndex == fftSize)
	{
		if(!nextFFtBlockReady)
		{
			juce::zeromem(fftData,sizeof(fftData));
			memcpy(fftData,fifo,sizeof(fifo));
			nextFFtBlockReady = true;
		}
		fifoIndex =0;
	}
	fifo[fifoIndex++] = sample;
}
void MainComponent::drawNextFrameOfSpectrum()
{
	window.multiplyWithWindowingTable(fftData, fftSize);
	
	forwardFFT.performFrequencyOnlyForwardTransform(fftData);
	auto mindB = -100.0f;
	auto maxdB = 0.0f;
	for(int i =0; i <scopeSize; ++i)
	{
		auto skewedProportionX = 1.0f - std::exp (std::log(1.0f - (float) i / (float) scopeSize) * 0.2f);
		auto fftDataIndex = juce::jlimit(0,fftSize/2, (int) (skewedProportionX * (float) fftSize * 0.5f));
		auto level = juce::jmap(juce::jlimit(mindB, maxdB, juce::Decibels::gainToDecibels (fftData[fftDataIndex])-
																					juce::Decibels::gainToDecibels((float) fftSize)), mindB,maxdB, 0.0f,1.0f);
		
		scopeData[i] = level;
	}
}
void MainComponent::timerCallback()
{
	if(nextFFtBlockReady)
	{
		drawNextFrameOfSpectrum();
		nextFFtBlockReady = false;
		repaint();
	}
}


void MainComponent::releaseResources()
{
    // This will be called when the audio device stops, or when it is being
    // restarted due to a setting change.
	juce::Logger::getCurrentLogger()->writeToLog("Realeasing");
    // For more details, see the help for AudioProcessor::releaseResources()
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
	auto spectrumHeight = 151;
			auto width = getWidth();
			auto height = getHeight();
			// Draw the spectrum background
			g.setColour(getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
			g.fillRect(0, height - spectrumHeight, width, spectrumHeight);
			// Draw the spectrum lines
			g.setColour(juce::Colours::white);
			for (int i = 1; i < scopeSize; ++i)
			{
					auto x1 = juce::jmap(i - 1, 0, scopeSize - 1, 0, width);
					auto y1 = juce::jmap(scopeData[i - 1], 0.0f, 1.0f, (float) (height - spectrumHeight), (float) (height - spectrumHeight - spectrumHeight));
					auto x2 = juce::jmap(i, 0, scopeSize - 1, 0, width);
					auto y2 = juce::jmap(scopeData[i], 0.0f, 1.0f, (float) (height - spectrumHeight), (float) (height - spectrumHeight - spectrumHeight));
					g.drawLine(x1, y1, x2, y2);
			}
}

void MainComponent::resized()
{
	// This is called when the MainContentComponent is resized.
	// If you add any child components, this is where you should
	// update their positions.
	auto sliderLeft = 50;
	auto sliderWidth = getWidth()/3;
	auto screenWidth = getWidth();
	auto btnWidth = screenWidth/10;
	frequencySlider.setBounds(sliderLeft, 20,sliderWidth, 20);
	outputGainSlider.setBounds(sliderLeft, 50, sliderWidth, 20);
	inputGainSlider.setBounds(sliderLeft, 80, sliderWidth, 20);
	distortionBtn.setBounds(10, 120, btnWidth, 200);
	cleanBtn.setBounds(180, 120, btnWidth, 200);

	auto rect = getLocalBounds();
	rect.reduce(400,20);
	audioSetupComp.setBounds (rect.removeFromLeft(proportionOfWidth (10)));
	rect.reduce (10, 10);
	rect.removeFromTop (30);
	rect.removeFromTop (20);
	
	//diagnosticsBox.setBounds (rect);}
}
void MainComponent::sliderValueChanged(juce::Slider * slider)
{
	if(slider == &frequencySlider)
	{
		//float local = 0.0;
		distortionAmount = static_cast<float> (slider->getValue())*10.0f;
	}
	else if (slider == &outputGainSlider)
	{
		
		processorChain.get<outputGainIndex>().setGainDecibels(slider->getValue()); //-20
	}
	else if (slider == &inputGainSlider)
	{
		processorChain.get<inputGainIndex>().setGainDecibels(slider->getValue());

	}
	
	return;
}

void MainComponent::buttonClicked(juce::Button * button)
{
	changeState(Distortion);
}

	void MainComponent::changeState(audioState newState)
	{
			if (state != newState)
			{
					state = newState;
					
					// Get reference to the convolution processor
					auto& convolution = processorChain.template get<convolutionIndex>();
					
					// Handle the state change
					if (state == Distortion)
					{
						auto dir = juce::File::getCurrentWorkingDirectory();
					//	auto parent = dir.getParentDirectory();
						// Construct the path to the guitar_amp.wav file within the Resources folder
						juce::File impulseResponseFile("/Users/averytaylor/Documents/guitar_pedal_board/Resources/guitar_amp.wav");
									try
									{
											convolution.loadImpulseResponse(
																											impulseResponseFile,
													juce::dsp::Convolution::Stereo::yes,
													juce::dsp::Convolution::Trim::no,
													1024
											);
											juce::Logger::getCurrentLogger()->writeToLog("Impulse response loaded successfully.");
									}
									catch (const std::exception& e)
									{
											juce::Logger::getCurrentLogger()->writeToLog("Error loading impulse response: " + juce::String(e.what()));
									}
							}
				
					else if (state == Clean)
					{
							// Set an empty impulse response or bypass the convolution when in Clean state
							convolution.reset(); // This effectively bypasses the convolution
							juce::Logger::getCurrentLogger()->writeToLog("Convolution bypassed.");
					}
			}
	
	return;
}
