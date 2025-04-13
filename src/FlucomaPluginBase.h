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
#include <algorithm>
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
        m_audioData.clear();

        if (!readAudioSamples()) {
            strcpy(m_status, "Audio data reading failed");
            return false;
        }

        return processAudio();
    }
    
    bool processAudio();
    virtual void setupParameters(int numChannels, int64_t numSamples, double sampleRate) = 0;
    
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
        
        // Remember the active state before drawing
        bool wasActive = m_parameterManager.isAnyControlActive();
        
        // Draw parameters
        bool paramsChanged = m_parameterManager.drawAll(m_ctx);
        
        // Check current active state
        bool isActive = m_parameterManager.isAnyControlActive();
        
        // Handle parameter changes
        if (paramsChanged || m_parameterManager.anyParameterChanged()) {
            notifyParametersChanged();
            m_parameterManager.saveAllValues();
        }
        
        // Check for parameter release (was active but now isn't)
        if (wasActive && !isActive) {
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
    
    if (m_previewMode && !m_immediateMode && m_pendingChanges) {
        strcpy(m_status, "Parameter released - preparing to process");
    }
}


template<typename ClientType, typename PluginType, typename CategoryTag>
bool FlucomaPluginBase<ClientType, PluginType, CategoryTag>::shouldProcess() {
    if (m_isProcessing) {
        return false;
    }
    
    if (m_previewMode && m_immediateMode && m_paramsChanged) {
        return shouldProcessDebounced();
    }
    
    if (m_previewMode && !m_immediateMode) {
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
    MediaItem* item = GetSelectedMediaItem(0, 0);
    if (!item) {
        strcpy(m_status, "No item selected");
        return false;
    }

    MediaItem_Take* originalTake = GetActiveTake(item);
    if (!originalTake) {
        strcpy(m_status, "No active take in selected item");
        return false;
    }

    PCM_source* originalSource = GetMediaItemTake_Source(originalTake);
    
    double sampleRate = GetMediaSourceSampleRate(originalSource);

    fluid::client::BufferAdaptor* outputBuffer = getOutputBuffer();
    if (!outputBuffer) {
        strcpy(m_status, "Invalid output buffer from processing");
        return false;
    }

    fluid::client::BufferAdaptor::ReadAccess readAccess(outputBuffer);
    if (!readAccess.valid()) {
        strcpy(m_status, "Invalid output buffer access");
        return false;
    }

    size_t numFrames = readAccess.numFrames();
    size_t numOutputChannels = readAccess.numChans();

    if (numFrames == 0 || numOutputChannels == 0) {
        strcpy(m_status, "Output buffer is empty");
        return false;
    }

    // --- 3. Prepare for and Write Temporary WAV File ---
    // Consider making the temp path more robust (e.g., GetResourcePath + GUID)
    const char* wavFilePath = "/Users/jby/output_flucoma_temp.wav"; // Temp filename

    PCM_sink* (*PCM_Sink_CreateEx_ptr)(ReaProject*, const char*, const char*, int, int, int, bool);
    PCM_sink* (*PCM_Sink_Create_ptr)(const char*, const char*, void*, int, int, int, bool);
    *(void**)&PCM_Sink_CreateEx_ptr = plugin_getapi("PCM_Sink_CreateEx");
    *(void**)&PCM_Sink_Create_ptr = plugin_getapi("PCM_Sink_Create");

    PCM_sink* sink = nullptr;
     if (PCM_Sink_CreateEx_ptr) {
        sink = PCM_Sink_CreateEx_ptr(nullptr, wavFilePath, nullptr, 0, (int)numOutputChannels, (int)sampleRate, false);
    } else if (PCM_Sink_Create_ptr) {
        sink = PCM_Sink_Create_ptr(wavFilePath, nullptr, nullptr, 0, (int)numOutputChannels, (int)sampleRate, false);
    }

    if (!sink) {
        if (!PCM_Sink_CreateEx_ptr && !PCM_Sink_Create_ptr) strcpy(m_status, "Failed to get PCM_Sink_Create/Ex API");
        else snprintf(m_status, sizeof(m_status), "Failed to create temp WAV sink for %s", wavFilePath);
        return false;
    }

    // Prepare data and write
    std::vector<ReaSample*> channelPointers(numOutputChannels);
    std::vector<std::vector<ReaSample>> channelData(numOutputChannels, std::vector<ReaSample>(numFrames));

    for (size_t ch = 0; ch < numOutputChannels; ++ch) {
        channelPointers[ch] = channelData[ch].data();
        auto channelView = readAccess.samps(ch);
        size_t framesToCopy = std::min(numFrames, static_cast<size_t>(channelView.size())); // Cast for std::min
        for (size_t i = 0; i < framesToCopy; ++i) {
            channelData[ch][i] = static_cast<ReaSample>(channelView(i));
        }
    }

    sink->WriteDoubles(channelPointers.data(), (int)numFrames, (int)numOutputChannels, 0, 1);
    sink->Extended(PCM_SINK_EXT_DONE, nullptr, nullptr, nullptr);
    delete sink;
    sink = nullptr;

    // PCM_source* newSource = PCM_Source_CreateFromFileEx(wavFilePath, false); // false = allow MIDI import if it were MIDI (not relevant here)

    // --- 5. Clean Up Temp File (Now that the source is loaded) ---
    // It's generally safe to delete the temp file now, REAPER's source likely keeps its own handle or copy.
    // remove(wavFilePath);

    // if (!newSource) {
    //     snprintf(m_status, sizeof(m_status), "Failed to create source from temp file %s", wavFilePath);
    //     // No need to delete sink or file here, already done
    //     return false;
    // }

    // --- 6. Add New Take and Set its Source ---
    Undo_BeginBlock2(0); // Start undo block

    InsertMedia(wavFilePath, 0);

    // if (!AddTakeToMediaItem) { // Check if API function is available
    //     strcpy(m_status, "Failed to get AddTakeToMediaItem API");
    //     Undo_EndBlock2(0, "Plugin Action (API Fail)", -1);
    //     PCM_Source_Destroy(newSource); // Clean up the source we created
    //     return false;
    // }

    // MediaItem_Take* newTake = AddTakeToMediaItem(item);

    // if (!newTake) {
    //     strcpy(m_status, "Failed to add new take to item");
    //     Undo_EndBlock2(0, "Plugin Action (Take Fail)", -1);
    //     PCM_Source_Destroy(newSource); // Clean up the source we created
    //     return false;
    // }

    // // Set the source for the new take using the recommended API
    // if (!SetMediaItemTakeInfo_Value) {
    //      strcpy(m_status, "Failed to get SetMediaItemTakeInfo_Value API");
    //      // We have a new take, but can't set its source. Best effort: leave take empty? Or delete?
    //      // Let's report error and leave the empty take for now.
    //      Undo_EndBlock2(0, "Plugin Action (API Fail)", -1);
    //      PCM_Source_Destroy(newSource); // Clean up source
    //      // Don't return false, take was added, just empty. Or decide to handle differently.
    // }
    // else
    // {
    //    // Cast the pointer to double via INT_PTR as per API docs/common practice for P_SOURCE
    //    bool sourceSet = SetMediaItemTakeInfo_Value(newTake, "P_SOURCE", (double)(INT_PTR)newSource);
    //    if(!sourceSet) {
    //        // This usually shouldn't fail if the pointer/paramname is valid
    //        strcpy(m_status, "Failed to set source for new take");
    //        // Handle error - maybe destroy the new take?
    //        PCM_Source_Destroy(newSource); // Clean up the source we created but couldn't assign
    //        // Decide whether to proceed or return false
    //    }
    //    // If sourceSet is true, REAPER now 'owns' newSource, do NOT destroy it manually.
    // }


    // // Set the name for the new take (optional, but good practice)
    // if (GetSetMediaItemTakeInfo_String) {
    //     char takeName[256];
    //     const char* baseName = m_pluginName ? m_pluginName : "Plugin"; // Use your plugin's name
    //     // Maybe add original take name too?
    //     // const char* origTakeName = GetTakeName(originalTake);
    //     // snprintf(takeName, sizeof(takeName), "%s (%s Output)", origTakeName ? origTakeName : "", baseName);
    //     snprintf(takeName, sizeof(takeName), "%s Output", baseName);
    //     GetSetMediaItemTakeInfo_String(newTake, "P_NAME", takeName, true); // true = set
    // }

    // // Make the new take active
    // if (SetActiveTake) {
    //     SetActiveTake(newTake);
    // } else {
    //      strcpy(m_status, "Failed to get SetActiveTake API"); // Non-fatal
    // }

    // --- 7. Update UI ---
    // UpdateItemInProject(item); // Update the item which now has a new active take
    UpdateArrange(); // May or may not be needed depending on context

    // --- 8. Finalize Undo Block and Status ---
    char undoName[128];
    snprintf(undoName, sizeof(undoName), "%s: Add Processed Take", m_pluginName ? m_pluginName : "Plugin");
    Undo_EndBlock2(0, undoName, -1); // -1 = auto-consolidate undo point if possible

    snprintf(m_status, sizeof(m_status), "%s: Added new take with processed audio", m_pluginName ? m_pluginName : "Plugin");

    return true;
}