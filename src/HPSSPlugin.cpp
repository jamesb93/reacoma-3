#pragma once
#include "FlucomaPluginBase.h"
#include <clients/rt/HPSSClient.hpp>
#include <clients/common/BufferAdaptor.hpp>
#include <clients/common/MemoryBufferAdaptor.hpp>
#include <clients/common/ParameterTypes.hpp>
#include <clients/common/ParameterSet.hpp>
#include "VectorBufferAdaptor.h"
#include "reaper_plugin_functions.h"
#include <algorithm>
#include <cmath>

using namespace fluid::client;
using namespace hpss;
using namespace flucoma;

class HPSSPlugin : public FlucomaPluginBase<NRTThreadedHPSSClient, HPSSPlugin, AudioTag> {
public:
    HPSSPlugin()
        : FlucomaPluginBase<NRTThreadedHPSSClient, HPSSPlugin, AudioTag>("HPSS")
        //   m_threshold(m_parameterManager.addParameter<double>(
        //       "Threshold", 
        //       0.1, 
        //       0.0, 
        //       1.0, 
        //       "%.2f",
        //       "Detection threshold (0-1). Higher values = fewer slices"
        //   )),
        //   m_kernelSize(m_parameterManager.addParameter<int>(
        //       "Kernel Size", 
        //       3, 
        //       3, 
        //       100,
        //       nullptr,
        //       "Size of the novelty detection kernel. Higher values detect changes over larger time spans"
        //   )),
        //   m_filterSize(m_parameterManager.addParameter<int>(
        //       "Filter Size", 
        //       3, 
        //       1, 
        //       15,
        //       nullptr,
        //       "Size of smoothing filter. Higher values smooth out the novelty curve"
        //   )),
        //   m_minSliceLength(m_parameterManager.addParameter<int>(
        //       "Min Slice Length", 
        //       2, 
        //       1, 
        //       50,
        //       nullptr,
        //       "Minimum number of frames between slices"
        //   )),
        //   m_algorithm(m_parameterManager.addParameter<int>(
        //       "Algorithm", 
        //       0, 
        //       0, 
        //       2,
        //       "Spectrum|Pitch|Loudness",  // Format string for algorithm options
        //       "Detection algorithm: Spectrum analyzes timbre, Pitch tracks frequency content, Loudness tracks amplitude"
        //   ))
    {
        strcpy(m_status, "Ready to decompose");
    }
    
    ~HPSSPlugin() {}

private:    
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
    
        auto h = std::make_shared<MemoryBufferAdaptor>(1, numSamples, sampleRate);
        auto harmonicBuffer = BufferT::type(h);
    
        auto p = std::make_shared<MemoryBufferAdaptor>(1, numSamples, sampleRate);
        auto percussiveBuffer = BufferT::type(p);
    
        m_params.template set<0>(std::move(inputBuffer), nullptr);  // source buffer
        m_params.template set<1>(LongT::type(0), nullptr);         // startFrame
        m_params.template set<2>(LongT::type(-1), nullptr);        // numFrames (-1 = all)
        m_params.template set<3>(LongT::type(0), nullptr);         // startChan
        m_params.template set<4>(LongT::type(-1), nullptr);        // numChans (-1 = all)
        m_params.template set<5>(std::move(h), nullptr);           // harmonic buffer
        m_params.template set<6>(std::move(p), nullptr);           // percussive buffer
    }
};