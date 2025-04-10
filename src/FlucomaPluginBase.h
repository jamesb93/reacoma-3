#pragma once
#include "reaper_imgui_functions.h"
#include "reaper_plugin_functions.h"
#include "ParameterManager.h" // Include our new parameter management system
#include <clients/common/FluidContext.hpp>
#include <clients/common/FluidBaseClient.hpp>
#include <clients/common/ParameterSet.hpp>
#include <memory>
#include <vector>
#include <chrono>
using namespace fluid::client;
using namespace flucoma; // Using our parameter namespace

struct SlicerTag {};
struct AudioTag {};

template<typename ClientType, typename PluginType, typename CategoryTag>
class FlucomaPluginBase {
public:
    static void start();
    static void loop();
    
    virtual ~FlucomaPluginBase();

protected:
    FlucomaPluginBase(const char* pluginName);
    
    static std::unique_ptr<PluginType> s_inst;

    void frame();
    
    // Since parameters are now initialized in the constructor, this is now optional
    virtual void initParameters() {}
    
    virtual void setupParameterControls() {}; // Optional method to setup additional GUI elements
    virtual bool applyAlgorithm() = 0;
    
    bool readAudioSamples();
    
    // Process implementation that varies by category tag
    bool process() {
        return processImpl(CategoryTag{});
    }
    
    bool processImpl(SlicerTag) {
        m_audioData.clear();
        
        if (!readAudioSamples()) {
            strcpy(m_status, "Audio data reading failed");
            return false;
        }
        
        return processAudio();
    }
    
    bool processImpl(AudioTag) {
        return false;
    }
    
    bool processAudio();
    virtual void setupParameters(int numChannels, int64_t numSamples, double sampleRate) = 0;
    
    // Output buffer accessor that varies by category tag
    fluid::client::BufferAdaptor* getOutputBuffer() { 
        return getOutputBufferImpl(CategoryTag{});
    }
    
    fluid::client::BufferAdaptor* getOutputBufferImpl(SlicerTag) {
        return m_params.template get<5>().get();
    }
    
    fluid::client::BufferAdaptor* getOutputBufferImpl(AudioTag) {
        return m_params.template get<5>().get();
    }
    
    const char* getUndoLabel() const {
        return "";
    }
    
    void drawProcessingModeUI();
    bool checkProcessingProgress();
    
    bool shouldProcess();
    bool shouldProcessDebounced();
    void resetDebounce();
    void triggerDebounce();
    
    void notifyParametersChanged();
    void notifyParameterReleased();

    bool handleResults() {
        return handleResultsImpl(CategoryTag{});
    }
    bool handleResultsImpl(SlicerTag);
    bool handleResultsImpl(AudioTag);
    
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
    
    // Parameter management
    ParameterManager m_parameterManager;
};

//
// Implementation for template methods
//

template<typename ClientType, typename PluginType, typename CategoryTag>
std::unique_ptr<PluginType> FlucomaPluginBase<ClientType, PluginType, CategoryTag>::s_inst;

template<typename ClientType, typename PluginType, typename CategoryTag>
void FlucomaPluginBase<ClientType, PluginType, CategoryTag>::start() try {
    if (s_inst)
        ImGui::SetNextWindowFocus(s_inst->m_ctx);
    else {
        s_inst.reset(new PluginType());
    }
} catch (const ImGui_Error &e) {
    ShowMessageBox(e.what(), s_inst ? s_inst->m_pluginName : "FluCoMa Plugin", 0);
    s_inst.reset();
}

template<typename ClientType, typename PluginType, typename CategoryTag>
void FlucomaPluginBase<ClientType, PluginType, CategoryTag>::loop() {
    if (s_inst) {
        s_inst->frame();
    }
}

template<typename ClientType, typename PluginType, typename CategoryTag>
FlucomaPluginBase<ClientType, PluginType, CategoryTag>::FlucomaPluginBase(const char* pluginName)
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
      m_debounceTimeMs{16.0}
{
    strcpy(m_status, "Ready");
    ImGui::init(plugin_getapi);
    m_ctx = ImGui::CreateContext(m_pluginName);
    m_lastParamChange = std::chrono::steady_clock::now();
    
    plugin_register("timer", (void *)&loop);
    
    // Call initParameters as a hook for derived classes that don't initialize parameters in constructor
    initParameters();
}

template<typename ClientType, typename PluginType, typename CategoryTag>
FlucomaPluginBase<ClientType, PluginType, CategoryTag>::~FlucomaPluginBase() {
    plugin_register("-timer", reinterpret_cast<void *>(&loop));
}

template<typename ClientType, typename PluginType, typename CategoryTag>
void FlucomaPluginBase<ClientType, PluginType, CategoryTag>::frame() {
    ImGui::SetNextWindowSize(m_ctx, 400, 210, ImGui::Cond_FirstUseEver);
    
    bool open{true};
    if (ImGui::Begin(m_ctx, m_pluginName, &open)) {
        if (m_isProcessing) {
            checkProcessingProgress();
        }
        
        ImGui::BeginDisabled(m_ctx, m_isProcessing);
        
        drawProcessingModeUI();
        
        // Draw parameters and track if any are active
        setupParameterControls(); // Any custom setup before drawing parameters
        
        bool paramsChanged = m_parameterManager.drawAll(m_ctx);
        
        if (paramsChanged || m_parameterManager.anyParameterChanged()) {
            notifyParametersChanged();
            m_parameterManager.saveAllValues();
        }
        
        bool wasActive = m_parameterManager.isAnyControlActive();
        if (!wasActive && m_parameterManager.isAnyControlActive()) {
            notifyParameterReleased();
        }
        
        ImGui::ProgressBar(m_ctx, m_isProcessing ? m_processingProgress / 100.0 : 0.0);
        
        bool shouldProcessNow = shouldProcess();
        
        if (!m_previewMode) {
            if (ImGui::Button(m_ctx, "Apply")) {
                shouldProcessNow = true;
            }
            
            ImGui::SameLine(m_ctx);
        }
        
        ImGui::EndDisabled(m_ctx);
        
        ImGui::BeginDisabled(m_ctx, !m_isProcessing);
        if (ImGui::Button(m_ctx, "Cancel Processing")) {
            m_client.cancel();
            m_isProcessing = false;
            strcpy(m_status, "Processing cancelled");
        }
        ImGui::EndDisabled(m_ctx);
        
        ImGui::Text(m_ctx, m_status);
        
        if (shouldProcessNow && !m_isProcessing) {
            resetDebounce();
            strcpy(m_status, "Processing...");
            applyAlgorithm();
        }
        
        ImGui::End(m_ctx);
    }
    
    if (!open) return s_inst.reset();
}

template<typename ClientType, typename PluginType, typename CategoryTag>
bool FlucomaPluginBase<ClientType, PluginType, CategoryTag>::readAudioSamples() {
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

template<typename ClientType, typename PluginType, typename CategoryTag>
bool FlucomaPluginBase<ClientType, PluginType, CategoryTag>::processAudio() {
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
        
        if (!handleResults()) {
            strcpy(m_status, "Failed to handle results");
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

template<typename ClientType, typename PluginType, typename CategoryTag>
void FlucomaPluginBase<ClientType, PluginType, CategoryTag>::drawProcessingModeUI() {
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

template<typename ClientType, typename PluginType, typename CategoryTag>
bool FlucomaPluginBase<ClientType, PluginType, CategoryTag>::checkProcessingProgress() {
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
        
        if (handleResults()) {
            return true;
        } else {
            strcpy(m_status, "Failed to create markers");
            return false;
        }
    }
    
    return false; // Still processing
}

template<typename ClientType, typename PluginType, typename CategoryTag>
bool FlucomaPluginBase<ClientType, PluginType, CategoryTag>::shouldProcessDebounced() {
    auto currentTime = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        currentTime - m_lastParamChange).count();
    
    if (m_paramsChanged && elapsedMs >= m_debounceTimeMs) {
        m_paramsChanged = false;
        return true;
    }
    
    return false;
}

template<typename ClientType, typename PluginType, typename CategoryTag>
void FlucomaPluginBase<ClientType, PluginType, CategoryTag>::resetDebounce() {
    m_paramsChanged = false;
}

template<typename ClientType, typename PluginType, typename CategoryTag>
void FlucomaPluginBase<ClientType, PluginType, CategoryTag>::triggerDebounce() {
    m_paramsChanged = true;
    m_lastParamChange = std::chrono::steady_clock::now();
}

template<typename ClientType, typename PluginType, typename CategoryTag>
void FlucomaPluginBase<ClientType, PluginType, CategoryTag>::notifyParametersChanged() {
    if (m_previewMode && m_immediateMode) {
        triggerDebounce();
    }
    
    m_paramReleased = false;
    m_pendingChanges = true;
}

template<typename ClientType, typename PluginType, typename CategoryTag>
void FlucomaPluginBase<ClientType, PluginType, CategoryTag>::notifyParameterReleased() {
    m_paramReleased = true;
}

template<typename ClientType, typename PluginType, typename CategoryTag>
bool FlucomaPluginBase<ClientType, PluginType, CategoryTag>::shouldProcess() {
    if (m_isProcessing) {
        return false;
    }
    
    if (m_previewMode && m_immediateMode && m_paramsChanged) {
        return shouldProcessDebounced();
    }
    
    if (m_previewMode && !m_immediateMode && !m_parameterManager.isAnyControlActive()) {
        if (m_pendingChanges && m_paramReleased) {
            m_pendingChanges = false;
            return true;
        }
    }
    
    return false;
}

template<typename ClientType, typename PluginType, typename CategoryTag>
bool FlucomaPluginBase<ClientType, PluginType, CategoryTag>::handleResultsImpl(SlicerTag) {
    MediaItem* item = GetSelectedMediaItem(0, 0);
    if (!item) return false;
    
    MediaItem_Take* take = GetActiveTake(item);
    if (!take) return false;
    
    PCM_source* source = GetMediaItemTake_Source(take);
    if (!source) return false;
    
    fluid::client::BufferAdaptor* outputBuffer = getOutputBuffer();
    if (!outputBuffer) {
        strcpy(m_status, "Invalid output buffer");
        return false;
    }
    
    fluid::client::BufferAdaptor::ReadAccess readAccess(outputBuffer);
    if (!readAccess.valid()) {
        strcpy(m_status, "Invalid output buffer access");
        return false;
    }
    
    auto markerView = readAccess.samps(0);    
    double sampleRate = GetMediaSourceSampleRate(source);
    double playRate = GetMediaItemTakeInfo_Value(take, "D_PLAYRATE");
    
    Undo_BeginBlock2(0);
    
    int markerCount = GetNumTakeMarkers(take);
    for (int i = markerCount - 1; i >= 0; i--) {
        DeleteTakeMarker(take, i);
    }
    
    int numMarkers = 0;
    const char* markerLabel = "";
    
    for (fluid::index i = 0; i < markerView.size(); i++) {
        if (markerView(i) > 0) {
            double markerTime = markerView(i) / sampleRate / playRate;
            SetTakeMarker(take, -1, markerLabel, &markerTime, nullptr);
            numMarkers++;
        }
    }
    
    UpdateTimeline();
    Undo_EndBlock2(0, getUndoLabel(), -1);
    
    // Success
    char successMsg[256];
    snprintf(successMsg, sizeof(successMsg), 
             "%s: Added %d %s markers", m_pluginName, numMarkers, markerLabel);
    strcpy(m_status, successMsg);
    
    return true;
}

template<typename ClientType, typename PluginType, typename CategoryTag>
bool FlucomaPluginBase<ClientType, PluginType, CategoryTag>::handleResultsImpl(AudioTag) {
    // Audio processing result handler would go here
    // This might involve creating a new take, new media item, or modifying audio buffers
    
    MediaItem* item = GetSelectedMediaItem(0, 0);
    if (!item) return false;
    
    // Example implementation for audio processors (placeholder)
    strcpy(m_status, "Audio processing complete");
    return true;
}