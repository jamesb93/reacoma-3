#pragma once

#include "FlucomaPluginBase.h"
#include <clients/rt/NoveltySliceClient.hpp>
#include <clients/common/BufferAdaptor.hpp>
#include <clients/common/MemoryBufferAdaptor.hpp>
#include <clients/common/ParameterTypes.hpp>

// Using declarations for FluCoMa types
using namespace fluid::client;
using namespace noveltyslice;
using NoveltySliceClientType = NRTThreadingNoveltySliceClient;

class NoveltySlicePlugin : public FluComaPluginBase<NoveltySliceClientType> {
public:
    static void start();
    ~NoveltySlicePlugin();

private:
    static void loop();
    static std::unique_ptr<NoveltySlicePlugin> s_inst;

    NoveltySlicePlugin();
    
    // Implement base class virtual methods
    void frame() override;
    bool applyAlgorithm() override;
    bool processAudio() override;
    bool createMarkersFromResults() override;
    
    // NoveltySlice specific parameters
    double m_threshold;
    int m_kernelSize;
    
    // Store previous parameter values to detect changes
    double m_prevThreshold;
    int m_prevKernelSize;
    
    // Specialized helper for this algorithm
    void setupNoveltySliceParameters(int numChannels, int64_t numSamples, double sampleRate);
};