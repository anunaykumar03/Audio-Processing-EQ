/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//READ ABOUT FIFO AND ALGORITHM TO GENERATE SPECTRUM STUFF
#include <array>
template<typename T>
struct Fifo
{
    void prepare(int numChannels, int numSamples)
    {
        static_assert( std::is_same_v<T, juce::AudioBuffer<float>>,
                      "prepare(numChannels, numSamples) should only be used when the Fifo is holding juce::AudioBuffer<float>");
        for( auto& buffer : buffers)
        {
            buffer.setSize(numChannels,
                           numSamples,
                           false,   //clear everything?
                           true,    //including the extra space?
                           true);   //avoid reallocating if you can?
            buffer.clear();
        }
    }
    
    void prepare(size_t numElements)
    {
        static_assert( std::is_same_v<T, std::vector<float>>,
                      "prepare(numElements) should only be used when the Fifo is holding std::vector<float>");
        for( auto& buffer : buffers )
        {
            buffer.clear();
            buffer.resize(numElements, 0);
        }
    }
    
    bool push(const T& t)
    {
        auto write = fifo.write(1);
        if( write.blockSize1 > 0 )
        {
            buffers[write.startIndex1] = t;
            return true;
        }
        
        return false;
    }
    
    bool pull(T& t)
    {
        auto read = fifo.read(1);
        if( read.blockSize1 > 0 )
        {
            t = buffers[read.startIndex1];
            return true;
        }
        
        return false;
    }
    
    int getNumAvailableForReading() const
    {
        return fifo.getNumReady();
    }
private:
    static constexpr int Capacity = 30;
    std::array<T, Capacity> buffers;
    juce::AbstractFifo fifo {Capacity};
};

enum Channel
{
    Right, //effectively 0
    Left //effectively 1
};

template<typename BlockType>
struct SingleChannelSampleFifo
{
    SingleChannelSampleFifo(Channel ch) : channelToUse(ch)
    {
        prepared.set(false);
    }
    
    void update(const BlockType& buffer)
    {
        jassert(prepared.get());
        jassert(buffer.getNumChannels() > channelToUse );
        auto* channelPtr = buffer.getReadPointer(channelToUse);
        
        for( int i = 0; i < buffer.getNumSamples(); ++i )
        {
            pushNextSampleIntoFifo(channelPtr[i]);
        }
    }

    void prepare(int bufferSize)
    {
        prepared.set(false);
        size.set(bufferSize);
        
        bufferToFill.setSize(1,             //channel
                             bufferSize,    //num samples
                             false,         //keepExistingContent
                             true,          //clear extra space
                             true);         //avoid reallocating
        audioBufferFifo.prepare(1, bufferSize);
        fifoIndex = 0;
        prepared.set(true);
    }
    //==============================================================================
    int getNumCompleteBuffersAvailable() const { return audioBufferFifo.getNumAvailableForReading(); }
    bool isPrepared() const { return prepared.get(); }
    int getSize() const { return size.get(); }
    //==============================================================================
    bool getAudioBuffer(BlockType& buf) { return audioBufferFifo.pull(buf); }
private:
    Channel channelToUse;
    int fifoIndex = 0;
    Fifo<BlockType> audioBufferFifo;
    BlockType bufferToFill;
    juce::Atomic<bool> prepared = false;
    juce::Atomic<int> size = 0;
    
    void pushNextSampleIntoFifo(float sample)
    {
        if (fifoIndex == bufferToFill.getNumSamples())
        {
            auto ok = audioBufferFifo.push(bufferToFill);

            juce::ignoreUnused(ok);
            
            fifoIndex = 0;
        }
        
        bufferToFill.setSample(0, fifoIndex, sample);
        ++fifoIndex;
    }
};

enum Slope
{
    Slope_12,
    Slope_24,
    Slope_36,
    Slope_48
};

struct ChainSettings
{
    float peakFreq {0}, peakGainInDecibels {0}, peakQuality {1.f};
    float lowCutFreq {0}, highCutFreq {0};
    int lowCutSlope {Slope::Slope_12}, highCutSlope {Slope::Slope_12};
};

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts);

using Filter = juce::dsp::IIR::Filter<float>; /*RESEARCH PROCESS CHAINS AND PROCESS CONTEXT*/

using CutFilter = juce::dsp::ProcessorChain<Filter, Filter, Filter, Filter>;

using MonoChain = juce::dsp::ProcessorChain<CutFilter, Filter, CutFilter>;

enum ChainPositions
{
    LowCut,
    Peak,
    HighCut
};

using Coefficients = Filter::CoefficientsPtr;
void updateCoefficients(Coefficients& old, const Coefficients& replacements);

Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate);

//refactoring the switch cases for getCoefficients... (now commented)
template<int Index, typename ChainType, typename CoefficientType>
void update(ChainType& chain, const CoefficientType& coefficients)
{
    updateCoefficients(chain.template get<Index>().coefficients, coefficients[Index]);
    chain.template setBypassed<Index>(false);
}

template<typename ChainType, typename CoefficientType>
void updateLowCutFilter(ChainType& leftLowCut,
                     const CoefficientType& cutCoefficients,
                     const ChainSettings& chainSettings)
                     //const Slope& lowCutSlope) IDKY DOES NOT WORK :(
{
// const ChainSettings& chainSettings) [not using the entire chainSettings object only using the slope so not member variable in updateCutFilter] IDKY DOES NOT WORK :(

//        auto cutCoefficients = juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod(chainSettings.lowCutFreq, getSampleRate(), 2*(chainSettings.lowCutSlope+1)); //last formula is derived from the implementation of IIRHighpass..., slope choice = 0,1,2,3 therefore order = 2,4,6,8 = 2*((0,1,2,3)+1)
//
//        //LOWCUT DSP
//        auto& leftLowCut = leftChain.get<ChainPositions::LowCut>();
    
    //bypass all the links in the chain, 4 positions -> 4 bypass
    
    leftLowCut.template setBypassed<0>(true);
    leftLowCut.template setBypassed<1>(true);
    leftLowCut.template setBypassed<2>(true);
    leftLowCut.template setBypassed<3>(true);
    
    switch(chainSettings.lowCutSlope)
    {
        case Slope_48:
        {
            update<3>(leftLowCut, cutCoefficients);
        }
        
        case Slope_36:
        {
            update<2>(leftLowCut, cutCoefficients);
        }
        
        case Slope_24:
        {
            update<1>(leftLowCut, cutCoefficients);
        }
            
        case Slope_12:
        {
            update<0>(leftLowCut, cutCoefficients);
        }
    }
}

/*
switch(chainSettings.lowCutSlope)
{
    case Slope_12:
    {
        *leftLowCut.template get<0>().coefficients = *cutCoefficients[0];
        leftLowCut.template setBypassed<0>(false);
        break;
    }
        
    case Slope_24:
    {
        *leftLowCut.template get<0>().coefficients = *cutCoefficients[0];
        leftLowCut.template setBypassed<0>(false);
        *leftLowCut.template get<1>().coefficients = *cutCoefficients[1];
        leftLowCut.template setBypassed<1>(false);
        break;
    }
    
    case Slope_36:
    {
        *leftLowCut.template get<0>().coefficients = *cutCoefficients[0];
        leftLowCut.template setBypassed<0>(false);
        *leftLowCut.template get<1>().coefficients = *cutCoefficients[1];
        leftLowCut.template setBypassed<1>(false);
        *leftLowCut.template get<2>().coefficients = *cutCoefficients[2];
        leftLowCut.template setBypassed<2>(false);
        break;
    }
    
    case Slope_48:
    {
        *leftLowCut.template get<0>().coefficients = *cutCoefficients[0];
        leftLowCut.template setBypassed<0>(false);
        *leftLowCut.template get<1>().coefficients = *cutCoefficients[1];
        leftLowCut.template setBypassed<1>(false);
        *leftLowCut.template get<2>().coefficients = *cutCoefficients[2];
        leftLowCut.template setBypassed<2>(false);
        *leftLowCut.template get<3>().coefficients = *cutCoefficients[3];
        leftLowCut.template setBypassed<3>(false);
        break;
    }
}*/

template<typename ChainType, typename CoefficientType>
void updateHighCutFilter(ChainType& leftHighCut,
                     const CoefficientType& cutCoefficients,
                     const ChainSettings& chainSettings)
                     //const Slope& lowCutSlope) IDKY DOES NOT WORK :(
{
    
    leftHighCut.template setBypassed<0>(true);
    leftHighCut.template setBypassed<1>(true);
    leftHighCut.template setBypassed<2>(true);
    leftHighCut.template setBypassed<3>(true);
    
    switch(chainSettings.highCutSlope)
    {
        case Slope_48:
        {
            update<3>(leftHighCut, cutCoefficients);
        }
        
        case Slope_36:
        {
            update<2>(leftHighCut, cutCoefficients);
        }
        
        case Slope_24:
        {
            update<1>(leftHighCut, cutCoefficients);
        }
            
        case Slope_12:
        {
            update<0>(leftHighCut, cutCoefficients);
        }
    }
}

inline auto makeLowCutFilter(const ChainSettings& chainSettings, double sampleRate)
{
    return juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod(chainSettings.lowCutFreq, sampleRate, 2*(chainSettings.lowCutSlope+1));
}

inline auto makeHighCutFilter(const ChainSettings& chainSettings, double sampleRate)
{
    return juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod(chainSettings.highCutFreq, sampleRate, 2*(chainSettings.highCutSlope+1));
}

//==============================================================================
/**
*/
class SimpleEQAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    SimpleEQAudioProcessor();
    ~SimpleEQAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override; //called by the host when its about to start playback
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override; //called when hit the play button

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    
    //audio plugins use parameters. need public variable in our processor for linking with GUI
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts{*this, nullptr, "Parameters", createParameterLayout()};
    
    using BlockType = juce::AudioBuffer<float>;
    SingleChannelSampleFifo<BlockType> leftChannelFifo {Channel::Left};
    SingleChannelSampleFifo<BlockType> rightChannelFifo {Channel::Right};
    
private:
    //making namespace aliases because juce::dsp:: uses lots of namespaces and nested namespaces... now in public up
    MonoChain leftChain, rightChain;
    
    void updatePeakFilter(const ChainSettings& chainSettings); //peak filter updating refactoring
    
    void lowCutFiltersImplemented(const ChainSettings& chainSettings);
    void highCutFiltersImplemented(const ChainSettings& chainSettings);
    
    void updateFilters();
    
    juce::dsp::Oscillator<float> osc; //test oscillator to verify FFT accuracy
    

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleEQAudioProcessor)
};
