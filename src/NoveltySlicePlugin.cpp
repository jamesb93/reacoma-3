#include "NoveltySlicePlugin.h"
#include "VectorBufferAdaptor.h"
#include "reaper_plugin_functions.h"

#include <algorithm>
#include <cmath>

constexpr const char *g_name{"FluCoMa NoveltySlice"};
std::unique_ptr<NoveltySlicePlugin> NoveltySlicePlugin::s_inst;

static void reportError(const ImGui_Error &e) {
    ShowMessageBox(e.what(), g_name, 0);
}

NoveltySlicePlugin::NoveltySlicePlugin()
    : FluComaPluginBase<NoveltySliceClientType>(g_name),
      m_threshold(0.1f),
      m_kernelSize(3),
      m_prevThreshold(0.1f),
      m_prevKernelSize(3)
{
    strcpy(m_status, "Ready to slice");
    plugin_register("timer", (void *)NoveltySlicePlugin::loop);
}

NoveltySlicePlugin::~NoveltySlicePlugin() {
    plugin_register("-timer", reinterpret_cast<void *>(&loop));
}

void NoveltySlicePlugin::start() try {
    if (s_inst)
        ImGui::SetNextWindowFocus(s_inst->m_ctx);
    else {
        s_inst.reset(new NoveltySlicePlugin);
    }
} catch (const ImGui_Error &e) {
    reportError(e);
    s_inst.reset();
}

void NoveltySlicePlugin::loop() {
    if (s_inst) {
        s_inst->frame();
    }
}

// Parameter UI method - only implement the parameter controls
void NoveltySlicePlugin::drawParameterControls() {
    // Parameter sliders
    bool thresholdActive = ImGui::SliderDouble(m_ctx, "Threshold", &m_threshold, 0.0f, 1.0f, "%.2f");
    m_anyControlActive = ImGui::IsItemActive(m_ctx);
    
    bool kernelSizeActive = ImGui::SliderInt(m_ctx, "Kernel Size", &m_kernelSize, 3, 100);
    m_anyControlActive = m_anyControlActive || ImGui::IsItemActive(m_ctx);
}

// Check if parameters have changed
bool NoveltySlicePlugin::haveParametersChanged() {
    return m_threshold != m_prevThreshold || m_kernelSize != m_prevKernelSize;
}

// Save current parameter values
void NoveltySlicePlugin::saveParameterValues() {
    m_prevThreshold = m_threshold;
    m_prevKernelSize = m_kernelSize;
}

bool NoveltySlicePlugin::applyAlgorithm() {
    m_audioData.clear();
    
    if (!readAudioSamples()) {
        strcpy(m_status, "Audio data reading failed");
        return false;
    }
    
    return processAudio();
}

void NoveltySlicePlugin::setupNoveltySliceParameters(int numChannels, int64_t numSamples, double sampleRate) {
    // Setup input buffer
    auto inputBuffer = InputBufferT::type(
        new fluid::VectorBufferAdaptor(m_audioData, numChannels, numSamples, sampleRate)
    );

    // Setup output buffer for slice points
    int estimatedSlices = static_cast<int>(numSamples / 1024);
    auto outBuffer = std::make_shared<MemoryBufferAdaptor>(1, estimatedSlices, sampleRate);
    auto outputBuffer = BufferT::type(outBuffer);

    // Set up all parameters
    m_params.template set<0>(std::move(inputBuffer), nullptr);  // source buffer
    m_params.template set<1>(LongT::type(0), nullptr);         // startFrame
    m_params.template set<2>(LongT::type(-1), nullptr);        // numFrames (-1 = all)
    m_params.template set<3>(LongT::type(0), nullptr);         // startChan
    m_params.template set<4>(LongT::type(-1), nullptr);        // numChans (-1 = all)
    m_params.template set<5>(std::move(outputBuffer), nullptr); // indices buffer
    
    m_params.template set<6>(LongT::type(0), nullptr);         // algorithm (0 = Spectrum)
    m_params.template set<7>(LongRuntimeMaxParam(m_kernelSize, m_kernelSize), nullptr); // kernelSize
    m_params.template set<8>(FloatT::type(m_threshold), nullptr); // threshold
    m_params.template set<9>(LongRuntimeMaxParam(3, 3), nullptr);         // filterSize
    m_params.template set<10>(LongT::type(2), nullptr);        // minSliceLength
    
    m_params.template set<11>(fluid::client::FFTParams(1024, -1, -1), nullptr);// hopSize
}

bool NoveltySlicePlugin::processAudio() {
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

    setupNoveltySliceParameters(
        numChannels, 
        numSamples, 
        sampleRate
    );
    
    m_client = NoveltySliceClientType(m_params, m_context);

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

bool NoveltySlicePlugin::createMarkersFromResults() {
    MediaItem* item = GetSelectedMediaItem(0, 0);
    if (!item) return false;
    
    MediaItem_Take* take = GetActiveTake(item);
    if (!take) return false;
    

    PCM_source* source = GetMediaItemTake_Source(take);
    if (!source) return false;
    
    BufferAdaptor::ReadAccess readAccess(m_params.template get<5>().get());
    if (!readAccess.valid()) {
        strcpy(m_status, "Invalid output buffer");
        return false;
    }
    
    // Get slice points data
    auto slicesView = readAccess.samps(0);
    
    double sampleRate = GetMediaSourceSampleRate(source);
    double playRate = GetMediaItemTakeInfo_Value(take, "D_PLAYRATE");
    
    Undo_BeginBlock2(0);
    
    // Delete all existing take markers first
    int markerCount = GetNumTakeMarkers(take);
    for (int i = markerCount - 1; i >= 0; i--) {
        DeleteTakeMarker(take, i);
    }
    
    // Add new markers at slice points
    int numSlices = 0;
    for (fluid::index i = 0; i < slicesView.size(); i++) {
        if (slicesView(i) > 0) {
            double sliceTime = slicesView(i) / sampleRate / playRate;
            SetTakeMarker(take, -1, "slice", &sliceTime, nullptr);
            numSlices++;
        }
    }
    
    UpdateTimeline();
    Undo_EndBlock2(0, "FluCoMa: Add NoveltySlice Markers", -1);
    
    // Success
    char successMsg[256];
    snprintf(successMsg, sizeof(successMsg), 
             "NoveltySlice: Added %d slice markers", numSlices);
    strcpy(m_status, successMsg);
    
    return true;
}