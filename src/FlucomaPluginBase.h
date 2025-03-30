#pragma once

#include "reaper_imgui_functions.h"
#include <clients/common/FluidContext.hpp>
#include <clients/common/ParameterSet.hpp>
#include <memory>
#include <vector>
#include <chrono>

// Base class for FluCoMa plugins using parameter sets and processing audio
template<typename ClientType>
class FluComaPluginBase {
public:
    virtual ~FluComaPluginBase();

protected:
    // Constructor
    FluComaPluginBase(const char* pluginName, double debounceTimeMs = 30.0);
    
    // Main UI frame processing
    virtual void frame() = 0;
    
    // Common audio data reading functionality
    bool readAudioSamples();
    
    // Apply the algorithm - must be implemented by derived classes
    virtual bool applyAlgorithm() = 0;
    
    // Process the audio with the current parameters
    virtual bool processAudio() = 0;
    
    // Create markers from the processing results
    virtual bool createMarkersFromResults() = 0;
    
    // Debounce handling
    bool shouldProcessDebounced();
    void resetDebounce();
    void triggerDebounce();
    
    // UI Context
    ImGui_Context* m_ctx;
    
    // Status message
    char m_status[255];
    
    // Plugin name
    const char* m_pluginName;
    
    // FluCoMa context
    fluid::client::FluidContext m_context;
    
    // Parameter set and client
    typename ClientType::ParamSetType m_params;
    ClientType m_client;
    
    // Audio data storage
    std::vector<float> m_audioData;
    
    // Debounce variables
    std::chrono::steady_clock::time_point m_lastParamChange;
    bool m_paramsChanged;
    double m_debounceTimeMs;
};

// Implementation of template methods
template<typename ClientType>
FluComaPluginBase<ClientType>::FluComaPluginBase(const char* pluginName, double debounceTimeMs)
    : m_ctx{},
      m_pluginName{pluginName},
      m_context{},
      m_params{ClientType::getParameterDescriptors(), fluid::FluidDefaultAllocator()},
      m_client{m_params, m_context},
      m_paramsChanged{false},
      m_debounceTimeMs{debounceTimeMs}
{
    strcpy(m_status, "Ready");
    ImGui::init(plugin_getapi);
    m_ctx = ImGui::CreateContext(m_pluginName);
    m_lastParamChange = std::chrono::steady_clock::now();
}

template<typename ClientType>
FluComaPluginBase<ClientType>::~FluComaPluginBase() {
    if (m_ctx) {
        // ImGui::DestroyContext(m_ctx);
    }
}

template<typename ClientType>
bool FluComaPluginBase<ClientType>::readAudioSamples() {
    m_audioData.clear();
    
    MediaItem* item = GetSelectedMediaItem(0, 0);
    if (!item) {
        strcpy(m_status, "No item selected");
        return false;
    }

    MediaItem_Take* take = GetActiveTake(item);
    if (!take) {
        strcpy(m_status, "No active take in selected item");
        return false;
    }

    PCM_source* source = GetMediaItemTake_Source(take);
    if (!source) {
        strcpy(m_status, "Failed to get media source");
        return false;
    }

    double sampleRate = GetMediaSourceSampleRate(source);
    if (sampleRate <= 0) {
        strcpy(m_status, "Invalid sample rate");
        return false;
    }

    int numChannels = GetMediaSourceNumChannels(source);
    if (numChannels <= 0) {
        numChannels = 1;
    }

    double length = GetMediaItemInfo_Value(item, "D_LENGTH");
    double exactNumSamples = length * sampleRate;
    int64_t numSamples = static_cast<int64_t>(exactNumSamples + 0.5);
    
    if (numSamples <= 0) {
        strcpy(m_status, "File has no samples");
        return false;
    }

    AudioAccessor* accessor = CreateTakeAudioAccessor(take);
    if (!accessor) {
        strcpy(m_status, "Failed to create audio accessor");
        return false;
    }

    m_audioData.resize(numChannels * numSamples, 0.0f);

    const int blockSize = 8192; // Reasonable block size
    std::vector<double> buffer(blockSize * numChannels); // Temporary buffer for reading
    
    bool hasValidSamples = false;    
    for (int64_t sampleOffset=0; sampleOffset < numSamples; sampleOffset += blockSize) {
        int64_t remainingSamples = numSamples - sampleOffset;

        int samplesToRead = static_cast<int>(std::min<int64_t>(remainingSamples, blockSize));

        if (samplesToRead <= 0) {
            break;
        }
        
        double position = (static_cast<double>(sampleOffset) / sampleRate);

        int ret = GetAudioAccessorSamples(
            accessor, 
            sampleRate, 
            numChannels, 
            position, 
            samplesToRead,
            buffer.data()
        );
        
        if (ret == 1) {  // Audio data was successfully retrieved
            hasValidSamples = true;
            for (int sampleIdx = 0; sampleIdx < samplesToRead; sampleIdx++) {
                for (int chanIdx = 0; chanIdx < numChannels; chanIdx++) {
                    int sourceIdx = sampleIdx * numChannels + chanIdx;
                    int destIdx = chanIdx * numSamples + (sampleOffset + sampleIdx);
                    if (sourceIdx < buffer.size() && destIdx < m_audioData.size()) {
                        m_audioData[destIdx] = static_cast<float>(buffer[sourceIdx]);
                    }
                }
            }
        }
    }

    DestroyAudioAccessor(accessor);

    if (!hasValidSamples) {
        strcpy(m_status, "No valid audio samples found");
        return false;
    }
    return true;
}

template<typename ClientType>
bool FluComaPluginBase<ClientType>::shouldProcessDebounced() {
    // Check if debounce timer has elapsed
    auto currentTime = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        currentTime - m_lastParamChange).count();
        
    // Process if params changed and debounce time elapsed
    if (m_paramsChanged && elapsedMs >= m_debounceTimeMs) {
        m_paramsChanged = false;
        return true;
    }
    
    return false;
}

template<typename ClientType>
void FluComaPluginBase<ClientType>::resetDebounce() {
    m_paramsChanged = false;
}

template<typename ClientType>
void FluComaPluginBase<ClientType>::triggerDebounce() {
    m_paramsChanged = true;
    m_lastParamChange = std::chrono::steady_clock::now();
}