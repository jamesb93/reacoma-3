#pragma once
#include "reaper_imgui_functions.h"
#include "reaper_plugin_functions.h"
#include <clients/common/FluidContext.hpp>
#include <clients/common/FluidBaseClient.hpp>
#include <clients/common/ParameterSet.hpp>
#include <memory>
#include <vector>
#include <chrono>
using namespace fluid::client;

template<typename ClientType, typename PluginType>
class FlucomaPluginBase {
public:
    // Static methods for plugin lifecycle management
    static void start();
    static void loop();
    
    virtual ~FlucomaPluginBase();

protected:
    // Constructor
    FlucomaPluginBase(const char* pluginName);
    
    static std::unique_ptr<PluginType> s_inst;

    void frame();
    
    virtual void drawParameterControls() = 0;
    virtual bool haveParametersChanged() = 0;
    virtual void saveParameterValues() = 0;
    bool readAudioSamples();
    virtual bool applyAlgorithm() = 0;
    bool processAudio();
    virtual void setupParameters(int numChannels, int64_t numSamples, double sampleRate) = 0;
    virtual bool createMarkersFromResults() = 0;
    void drawProcessingModeUI();
    bool checkProcessingProgress();
    
    bool shouldProcess();
    bool shouldProcessDebounced();
    void resetDebounce();
    void triggerDebounce();
    
    void notifyParametersChanged();
    void notifyParameterReleased();
    
    ImGui_Context* m_ctx;

    char m_status[255];
    
    const char* m_pluginName;
    
    FluidContext m_context;
    
    typename ClientType::ParamSetType m_params;
    ClientType m_client;
    
    std::vector<float> m_audioData;
    
    bool m_previewMode = false;
    bool m_immediateMode = false;
    
    bool m_isProcessing = false;
    double m_processingProgress = 0.0;
    
    bool m_paramsChanged = false;
    bool m_paramReleased = true;
    bool m_pendingChanges = false;
    
    std::chrono::steady_clock::time_point m_lastParamChange;
    double m_debounceTimeMs = 16.0;
    
    bool m_anyControlActive = false;
};

template<typename ClientType, typename PluginType>
std::unique_ptr<PluginType> FlucomaPluginBase<ClientType, PluginType>::s_inst;

template<typename ClientType, typename PluginType>
void FlucomaPluginBase<ClientType, PluginType>::start() try {
    if (s_inst)
        ImGui::SetNextWindowFocus(s_inst->m_ctx);
    else {
        s_inst.reset(new PluginType());
    }
} catch (const ImGui_Error &e) {
    ShowMessageBox(e.what(), s_inst ? s_inst->m_pluginName : "FluCoMa Plugin", 0);
    s_inst.reset();
}

template<typename ClientType, typename PluginType>
void FlucomaPluginBase<ClientType, PluginType>::loop() {
    if (s_inst) {
        s_inst->frame();
    }
}

template<typename ClientType, typename PluginType>
FlucomaPluginBase<ClientType, PluginType>::FlucomaPluginBase(const char* pluginName)
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
    
    plugin_register("timer", (void *)&loop);
}

template<typename ClientType, typename PluginType>
FlucomaPluginBase<ClientType, PluginType>::~FlucomaPluginBase() {
    plugin_register("-timer", reinterpret_cast<void *>(&loop));
}

template<typename ClientType, typename PluginType>
void FlucomaPluginBase<ClientType, PluginType>::frame() {
    ImGui::SetNextWindowSize(m_ctx, 400, 210, ImGui::Cond_FirstUseEver);

    bool open{true};
    if (ImGui::Begin(m_ctx, m_pluginName, &open)) {
        if (m_isProcessing) {
            checkProcessingProgress();
            
            ImGui::ProgressBar(m_ctx, m_processingProgress / 100.0);
            ImGui::Text(m_ctx, m_status);
            
            if (ImGui::Button(m_ctx, "Cancel Processing")) {
                m_client.cancel();
                m_isProcessing = false;
                strcpy(m_status, "Processing cancelled");
            }
        }
        else {
            drawProcessingModeUI();
            
            bool wasActive = m_anyControlActive;
            m_anyControlActive = false;
            
            drawParameterControls();
            
            if (haveParametersChanged()) {
                notifyParametersChanged();
                saveParameterValues();
            }
            
            if (wasActive && !m_anyControlActive) {
                notifyParameterReleased();
            }
            
            bool shouldProcessNow = shouldProcess();
            
            if (!m_previewMode) {
                if (ImGui::Button(m_ctx, "Apply")) {
                    shouldProcessNow = true;
                }
            }
            
            if (shouldProcessNow) {
                resetDebounce();
                strcpy(m_status, "Processing...");
                applyAlgorithm();
            }

            ImGui::Text(m_ctx, m_status);
        }
        
        ImGui::End(m_ctx);
    }

    if (!open) return s_inst.reset();
}

template<typename ClientType, typename PluginType>
bool FlucomaPluginBase<ClientType, PluginType>::readAudioSamples() {
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
    const int blockSize = 8192;
    std::vector<double> buffer(blockSize * numChannels);
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
        
        if (ret == 1) {
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

template<typename ClientType, typename PluginType>
bool FlucomaPluginBase<ClientType, PluginType>::processAudio() {
    MediaItem* item = GetSelectedMediaItem(0, 0);
    if (!item) return false;
    
    MediaItem_Take* take = GetActiveTake(item);
    if (!take) return false;
    
    PCM_source* source = GetMediaItemTake_Source(take);
    if (!source->IsAvailable()) {
        return false;
    } 
    
    int numChannels = source->GetNumChannels();
    double sampleRate = source->GetSampleRate();
    int64_t numSamples = m_audioData.size() / numChannels;
    
    if (numChannels <= 0) {
        return false;
    }

    setupParameters(numChannels, numSamples, sampleRate);
    
    m_client = ClientType(m_params, m_context);

    if (m_previewMode && m_immediateMode) {
        m_client.setSynchronous(true);
        
        m_client.enqueue(m_params);
        Result result = m_client.process();
        
        if (!result.ok()) {
            strcpy(m_status, "Processing failed");
            return false;
        }
        
        if (!createMarkersFromResults()) {
            strcpy(m_status, "Failed to create markers");
            return false;
        }
        
        return true;
    } else {
        m_client.setSynchronous(false);
        
        m_client.enqueue(m_params);
        Result result = m_client.process();
        
        if (!result.ok()) {
            strcpy(m_status, "Failed to start processing");
            return false;
        }
        
        m_isProcessing = true;
        m_processingProgress = 0.0;
        strcpy(m_status, "Processing... 0%");
        
        return true;
    }
}

template<typename ClientType, typename PluginType>
void FlucomaPluginBase<ClientType, PluginType>::drawProcessingModeUI() {
    bool prevPreviewMode = m_previewMode;
    bool prevImmediateMode = m_immediateMode;
    
    ImGui::Text(m_ctx, "Processing Mode:");
    ImGui::Checkbox(m_ctx, "Preview Mode (auto-process)", &m_previewMode);
    ImGui::Checkbox(m_ctx, "Immediate Mode (process on change)", &m_immediateMode);
    
    if (ImGui::IsItemHovered(m_ctx)) {
        bool tooltipOpen = ImGui::BeginTooltip(m_ctx);
        if (tooltipOpen) {
            ImGui::Text(m_ctx, "When ON: Process on every parameter change (with debouncing)");
            ImGui::Text(m_ctx, "When OFF: Process only when controls are released");
            ImGui::EndTooltip(m_ctx);
        }
    }
    
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
        resetDebounce();
        m_paramReleased = true;
    }
    
    ImGui::Separator(m_ctx);
}

template<typename ClientType, typename PluginType>
bool FlucomaPluginBase<ClientType, PluginType>::checkProcessingProgress() {
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
            return true;
        } else {
            strcpy(m_status, "Failed to create markers");
            return false;
        }
    }
    
    return false; // Still processing
}

template<typename ClientType, typename PluginType>
bool FlucomaPluginBase<ClientType, PluginType>::shouldProcessDebounced() {
    auto currentTime = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        currentTime - m_lastParamChange).count();
    
    if (m_paramsChanged && elapsedMs >= m_debounceTimeMs) {
        m_paramsChanged = false;
        return true;
    }
    
    return false;
}

template<typename ClientType, typename PluginType>
void FlucomaPluginBase<ClientType, PluginType>::resetDebounce() {
    m_paramsChanged = false;
}

template<typename ClientType, typename PluginType>
void FlucomaPluginBase<ClientType, PluginType>::triggerDebounce() {
    m_paramsChanged = true;
    m_lastParamChange = std::chrono::steady_clock::now();
}

template<typename ClientType, typename PluginType>
void FlucomaPluginBase<ClientType, PluginType>::notifyParametersChanged() {
    if (m_previewMode && m_immediateMode) {
        triggerDebounce();
    }
    
    m_paramReleased = false;
    m_pendingChanges = true;
}

template<typename ClientType, typename PluginType>
void FlucomaPluginBase<ClientType, PluginType>::notifyParameterReleased() {
    m_paramReleased = true;
}

template<typename ClientType, typename PluginType>
bool FlucomaPluginBase<ClientType, PluginType>::shouldProcess() {
    if (m_isProcessing) {
        return false;
    }
    
    if (m_previewMode && m_immediateMode && m_paramsChanged) {
        // auto currentTime = std::chrono::steady_clock::now();
        // auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        //     currentTime - m_lastParamChange).count();
        // double remainingTime = m_debounceTimeMs - elapsedMs;
        // if (remainingTime < 0) remainingTime = 0;
        // char debounceMsg[64];
        // snprintf(debounceMsg, sizeof(debounceMsg),
        //         "Will process in %.1f ms...", remainingTime);
        // ImGui::Text(m_ctx, debounceMsg);
        
        return shouldProcessDebounced();
    }
    
    if (m_previewMode && !m_immediateMode && !m_anyControlActive) {
        if (m_pendingChanges && m_paramReleased) {
            m_pendingChanges = false;
            return true;
        }
    }
    
    return false;
}