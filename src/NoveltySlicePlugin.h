#pragma once
#include "FlucomaPluginBase.h"
#include <clients/rt/NoveltySliceClient.hpp>
#include <clients/common/BufferAdaptor.hpp>
#include <clients/common/MemoryBufferAdaptor.hpp>
#include <clients/common/ParameterTypes.hpp>
#include "VectorBufferAdaptor.h"

// Using declarations for FluCoMa types
using namespace fluid::client;
using namespace noveltyslice;
using NoveltySliceClientType = NRTThreadingNoveltySliceClient;

class NoveltySlicePlugin : public FluComaPluginBase<NoveltySliceClientType, NoveltySlicePlugin> {
public:
    // Constructor is now public since it's called by the base class's start() method
    NoveltySlicePlugin();
    ~NoveltySlicePlugin();

private:
    // Implement parameter UI drawing from base class
    void drawParameterControls() override;
    
    // Implement parameter change detection
    bool haveParametersChanged() override;
    
    // Implement saving current parameter values
    void saveParameterValues() override;
    
    // Implement remaining base class virtual methods
    bool applyAlgorithm() override;
    
    // Setup parameters specifically for NoveltySlice algorithm
    void setupParameters(int numChannels, int64_t numSamples, double sampleRate) override;
    
    // Create markers from results
    bool createMarkersFromResults() override;
    
    // NoveltySlice specific parameters
    double m_threshold;
    int m_kernelSize;
    
    // Store previous parameter values to detect changes
    double m_prevThreshold;
    int m_prevKernelSize;
};