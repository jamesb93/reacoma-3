#pragma once
#include "FlucomaSlicerPluginBase.h"
#include <clients/rt/NoveltySliceClient.hpp>
#include <clients/common/BufferAdaptor.hpp>
#include <clients/common/MemoryBufferAdaptor.hpp>
#include <clients/common/ParameterTypes.hpp>
#include "VectorBufferAdaptor.h"

// Using declarations for Flucoma types
using namespace fluid::client;
using namespace noveltyslice;
using NoveltySliceClientType = NRTThreadingNoveltySliceClient;

class NoveltySlicePlugin : public FlucomaSlicerPluginBase<NoveltySliceClientType, NoveltySlicePlugin> {
public:
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
    
    // Override getSliceBuffer to return the correct buffer
    fluid::client::BufferAdaptor* getSliceBuffer() override;
    
    // Override the undo label
    const char* getUndoLabel() const override {
        return "Flucoma: Add NoveltySlice Markers";
    }
    
    // NoveltySlice specific parameters
    double m_threshold;
    int m_kernelSize;
    
    // Store previous parameter values to detect changes
    double m_prevThreshold;
    int m_prevKernelSize;
};