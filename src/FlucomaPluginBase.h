#pragma once
#include "reaper_imgui_functions.h"
#include <clients/common/FluidContext.hpp>
#include <clients/common/FluidBaseClient.hpp>
#include <clients/common/ParameterSet.hpp>
#include <memory>
#include <vector>
#include <chrono>
using namespace fluid::client;

template<typename ClientType>
class FluComaPluginBase {
public:
    virtual ~FluComaPluginBase();

protected:
    // Constructor
    FluComaPluginBase(const char* pluginName);

    // Main UI frame processing - final implementation in base class
    void frame();
    
    // Virtual method for derived classes to implement parameter UI
    virtual void drawParameterControls() = 0;
    
    // Virtual method to check if parameters have changed from previous values
    virtual bool haveParametersChanged() = 0;
    
    // Virtual method to save current parameters as previous values
    virtual void saveParameterValues() = 0;
    
    // Common audio data reading functionality
    bool readAudioSamples();
    
    // Apply the algorithm - must be implemented by derived classes
    virtual bool applyAlgorithm() = 0;
    
    // Process the audio with the current parameters
    virtual bool processAudio() = 0;
    
    // Create markers from the processing results
    virtual bool createMarkersFromResults() = 0;
    
    // Processing mode UI elements - call this from the base class's frame() method
    void drawProcessingModeUI();
    
    // Check if processing has completed
    bool checkProcessingProgress();
    
    // Debounce handling
    bool shouldProcess();
    bool shouldProcessDebounced();
    void resetDebounce();
    void triggerDebounce();
    
    // Signal that parameters were changed
    void notifyParametersChanged();
    
    // Signal that a parameter control was released
    void notifyParameterReleased();
    
    // UI Context
    ImGui_Context* m_ctx;
    
    // Status message
    char m_status[255];
    
    // Plugin name
    const char* m_pluginName;
    
    // FluCoMa context
    FluidContext m_context;
    
    // Parameter set and client
    typename ClientType::ParamSetType m_params;
    ClientType m_client;
    
    // Audio data storage
    std::vector<float> m_audioData;
    
    // Processing mode flags
    bool m_previewMode = false; // Automatic vs. manual processing
    bool m_immediateMode = false; // Parameter change vs. parameter release
    
    // Processing state
    bool m_isProcessing = false;
    double m_processingProgress = 0.0;
    
    // Parameter state tracking
    bool m_paramsChanged = false;
    bool m_paramReleased = true; // Track if parameters have been released
    bool m_pendingChanges = false;
    
    // Debounce variables
    std::chrono::steady_clock::time_point m_lastParamChange;
    double m_debounceTimeMs = 16.0;
    
    // Flag to indicate if any controls are currently active
    bool m_anyControlActive = false;
};

// Implementation of template methods
template<typename ClientType>
FluComaPluginBase<ClientType>::FluComaPluginBase(const char* pluginName)
    : m_ctx{},
      m_pluginName{pluginName},
      m_context{},
      m_params{ClientType::getParameterDescriptors(), fluid::FluidDefaultAllocator()},
      m_client{m_params, m_context},
      m_isProcessing{false},
      m_processingProgress{0.0},
      m_previewMode{false},
      m_immediateMode{false},
      m_paramsChanged{false},
      m_paramReleased{true},
      m_debounceTimeMs{16.0},
      m_anyControlActive{false}
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
void FluComaPluginBase<ClientType>::frame() {
    ImGui::SetNextWindowSize(m_ctx, 400, 210, ImGui::Cond_FirstUseEver);

    bool open{true};
    if (ImGui::Begin(m_ctx, m_pluginName, &open)) {
        // Check if async processing is complete
        if (m_isProcessing) {
            checkProcessingProgress();
            
            // Show progress bar
            ImGui::ProgressBar(m_ctx, m_processingProgress / 100.0);
            ImGui::Text(m_ctx, m_status);
            
            // Add a cancel button
            if (ImGui::Button(m_ctx, "Cancel Processing")) {
                // Reset the processing state
                m_isProcessing = false;
                strcpy(m_status, "Processing cancelled");
            }
        }
        else {
            // Draw the processing mode UI elements
            drawProcessingModeUI();
            
            bool wasActive = m_anyControlActive;
            m_anyControlActive = false;
            
            // Draw parameter controls from derived class
            drawParameterControls();
            
            // Check for parameter changes
            if (haveParametersChanged()) {
                notifyParametersChanged();
                saveParameterValues();
            }
            
            // Notify base class when controls are released
            if (wasActive && !m_anyControlActive) {
                notifyParameterReleased();
            }
            
            // Check if we should process based on the current state
            bool shouldProcessNow = shouldProcess();
            
            // Show Apply button in manual mode
            if (!m_previewMode) {
                if (ImGui::Button(m_ctx, "Apply")) {
                    shouldProcessNow = true;
                }
            }
            
            // Process if needed
            if (shouldProcessNow) {
                resetDebounce();
                strcpy(m_status, "Processing...");
                applyAlgorithm();
            }

            ImGui::Text(m_ctx, m_status);
        }
        
        ImGui::End(m_ctx);
    }

    if (!open) return;
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
        
        if (ret == 1) { // Audio data was successfully retrieved
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
void FluComaPluginBase<ClientType>::drawProcessingModeUI() {
    // Store previous values to detect changes
    bool prevPreviewMode = m_previewMode;
    bool prevImmediateMode = m_immediateMode;
    
    // Mode selection checkboxes
    ImGui::Text(m_ctx, "Processing Mode:");
    ImGui::Checkbox(m_ctx, "Preview Mode (auto-process)", &m_previewMode);
    ImGui::Checkbox(m_ctx, "Immediate Mode (process on change)", &m_immediateMode);
    
    // Tooltip explanations
    if (ImGui::IsItemHovered(m_ctx)) {
        // Store the return value to fix the warning
        bool tooltipOpen = ImGui::BeginTooltip(m_ctx);
        if (tooltipOpen) {
            ImGui::Text(m_ctx, "When ON: Process on every parameter change (with debouncing)");
            ImGui::Text(m_ctx, "When OFF: Process only when controls are released");
            ImGui::EndTooltip(m_ctx);
        }
    }
    
    // Handle mode toggling - only update status message, don't trigger processing
    if (prevPreviewMode != m_previewMode || prevImmediateMode != m_immediateMode) {
        if (m_previewMode) {
            if (m_immediateMode) {
                strcpy(m_status, "Auto-processing on parameter change");
            } else {
                strcpy(m_status, "Auto-processing on parameter release");
            }
        } else {
            strcpy(m_status, "Manual processing with Apply button");
        }
        // Reset any pending debounce to prevent immediate processing
        resetDebounce();
        // Make sure we don't process just because mode changed
        m_paramReleased = true;
    }
    
    ImGui::Separator(m_ctx);
}

template<typename ClientType>
bool FluComaPluginBase<ClientType>::checkProcessingProgress() {
    if (!m_isProcessing) return false;
    
    Result result;
    ProcessState processState = m_client.checkProgress(result);
    m_processingProgress = m_client.progress() * 100.0;
    snprintf(m_status, sizeof(m_status), "Processing... %.0f%%", m_processingProgress);
    
    if (processState == ProcessState::kDone || processState == ProcessState::kDoneStillProcessing) {
        m_isProcessing = false;
        if (!result.ok()) {
            strcpy(m_status, "Processing failed");
            return false;
        }
        
        if (createMarkersFromResults()) {
            // Success handled by createMarkersFromResults
            return true;
        } else {
            strcpy(m_status, "Failed to create markers");
            return false;
        }
    }
    
    return false; // Still processing
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

template<typename ClientType>
void FluComaPluginBase<ClientType>::notifyParametersChanged() {
    // Called when a parameter value changes
    if (m_previewMode && m_immediateMode) {
        // In immediate mode with preview, use debouncing
        triggerDebounce();
    }
    
    // Always mark that parameters have changed and need release
    m_paramReleased = false;
    m_pendingChanges = true;
}

template<typename ClientType>
void FluComaPluginBase<ClientType>::notifyParameterReleased() {
    // Called when a parameter control is released
    m_paramReleased = true;
}

template<typename ClientType>
bool FluComaPluginBase<ClientType>::shouldProcess() {
    // Never process if we're already processing
    if (m_isProcessing) {
        return false;
    }
    
    // In immediate mode with preview, check debounce
    if (m_previewMode && m_immediateMode && m_paramsChanged) {
        // Show countdown
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - m_lastParamChange).count();
        double remainingTime = m_debounceTimeMs - elapsedMs;
        if (remainingTime < 0) remainingTime = 0;
        char debounceMsg[64];
        snprintf(debounceMsg, sizeof(debounceMsg),
                "Will process in %.1f ms...", remainingTime);
        ImGui::Text(m_ctx, debounceMsg);
        
        // Check if debounce time elapsed
        return shouldProcessDebounced();
    }
    
    // In non-immediate mode with preview, check for parameter release
    if (m_previewMode && !m_immediateMode && !m_anyControlActive) {
        // If parameters were changed and now released
        if (m_pendingChanges && m_paramReleased) {
            m_pendingChanges = false; // Reset the pending changes flag
            return true;
        }
    }
    
    return false;
}