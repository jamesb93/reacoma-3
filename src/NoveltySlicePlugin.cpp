#pragma once
#include "FlucomaPluginBase.h"
#include <clients/rt/NoveltySliceClient.hpp>
#include <clients/common/BufferAdaptor.hpp>
#include <clients/common/MemoryBufferAdaptor.hpp>
#include <clients/common/ParameterTypes.hpp>
#include <clients/common/ParameterSet.hpp>
#include "VectorBufferAdaptor.h"
#include "reaper_plugin_functions.h"
#include <algorithm>
#include <cmath>

// Using declarations for Flucoma types
using namespace fluid::client;
using namespace noveltyslice;
using namespace flucoma;  // For parameter management
using NoveltySliceClientType = NRTThreadingNoveltySliceClient;

class NoveltySlicePlugin : public FlucomaPluginBase<NoveltySliceClientType, NoveltySlicePlugin, SlicerTag> {
public:
    NoveltySlicePlugin()
        : FlucomaPluginBase<NoveltySliceClientType, NoveltySlicePlugin, SlicerTag>("Noveltyslice"),
          m_threshold(m_parameterManager.addParameter<double>(
              "Threshold", 
              0.1, 
              0.0, 
              1.0, 
              "%.2f",
              "Detection threshold (0-1). Higher values = fewer slices"
          )),
          m_kernelSize(m_parameterManager.addParameter<int>(
              "Kernel Size", 
              3, 
              3, 
              100,
              nullptr,
              "Size of the novelty detection kernel. Higher values detect changes over larger time spans"
          )),
          m_filterSize(m_parameterManager.addParameter<int>(
              "Filter Size", 
              3, 
              1, 
              15,
              nullptr,
              "Size of smoothing filter. Higher values smooth out the novelty curve"
          )),
          m_minSliceLength(m_parameterManager.addParameter<int>(
              "Min Slice Length", 
              2, 
              1, 
              50,
              nullptr,
              "Minimum number of frames between slices"
          )),
          m_algorithm(m_parameterManager.addParameter<int>(
              "Algorithm", 
              0, 
              0, 
              2,
              "Spectrum|Pitch|Loudness",  // Format string for algorithm options
              "Detection algorithm: Spectrum analyzes timbre, Pitch tracks frequency content, Loudness tracks amplitude"
          ))
    {
        strcpy(m_status, "Ready to slice");
    }
    
    ~NoveltySlicePlugin() {}

private:    
    // Optional: override to add any custom controls
    void setupParameterControls() override {
        ImGui::Separator(m_ctx);
        ImGui::Text(m_ctx, "Algorithm Settings");
    }
    
    bool applyAlgorithm() override {
        return process();
    }
    
    void setupParameters(int numChannels, int64_t numSamples, double sampleRate) override {
        auto inputBuffer = InputBufferT::type(
            new fluid::VectorBufferAdaptor(m_audioData, numChannels, numSamples, sampleRate)
        );

        int estimatedSlices = static_cast<int>(numSamples / 1024);
        auto outBuffer = std::make_shared<MemoryBufferAdaptor>(1, estimatedSlices, sampleRate);
        auto outputBuffer = BufferT::type(outBuffer);

        m_params.template set<0>(std::move(inputBuffer), nullptr);  // source buffer
        m_params.template set<1>(LongT::type(0), nullptr);         // startFrame
        m_params.template set<2>(LongT::type(-1), nullptr);        // numFrames (-1 = all)
        m_params.template set<3>(LongT::type(0), nullptr);         // startChan
        m_params.template set<4>(LongT::type(-1), nullptr);        // numChans (-1 = all)
        m_params.template set<5>(std::move(outputBuffer), nullptr); // indices buffer
        
        m_params.template set<6>(LongT::type(m_algorithm), nullptr);       // algorithm
        m_params.template set<7>(LongRuntimeMaxParam(m_kernelSize, m_kernelSize), nullptr); // kernelSize
        m_params.template set<8>(FloatT::type(m_threshold), nullptr); // threshold
        m_params.template set<9>(LongRuntimeMaxParam(m_filterSize, m_filterSize), nullptr); // filterSize
        m_params.template set<10>(LongT::type(m_minSliceLength), nullptr);   // minSliceLength
        
        m_params.template set<11>(fluid::client::FFTParams(1024, -1, -1), nullptr); // hopSize
    }
    
    // Member references to parameters
    ParameterWrapper<double>& m_threshold;
    ParameterWrapper<int>& m_kernelSize;
    ParameterWrapper<int>& m_filterSize;
    ParameterWrapper<int>& m_minSliceLength;
    ParameterWrapper<int>& m_algorithm;
};