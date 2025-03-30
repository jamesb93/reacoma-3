#define REAIMGUIAPI_IMPLEMENT
#include "reaper_imgui_functions.h"

#define REAPERAPI_IMPLEMENT
#include <algorithm>
#include <algorithms/public/NoveltySegmentation.hpp>
#include <clients/common/BufferAdaptor.hpp>
#include <clients/common/MemoryBufferAdaptor.hpp>
#include <clients/common/FluidBaseClient.hpp>
#include <clients/common/FluidContext.hpp>
#include <clients/common/FluidNRTClientWrapper.hpp>
#include <clients/common/ParameterSet.hpp>
#include <clients/common/ParameterTypes.hpp>
#include <clients/rt/NoveltySliceClient.hpp>
#include <data/FluidMemory.hpp>
#include <data/FluidTensor.hpp>
#include <memory>
#include <vector>
#include <cmath> // For std::abs, std::min, std::max
#include <chrono> // For debounce timer

#include "VectorBufferAdaptor.h"

#include "reaper_plugin_functions.h"

using namespace fluid::client;
using namespace noveltyslice;
using Client = NRTThreadingNoveltySliceClient;
using ParamSetType = typename Client::ParamSetType;

class NoveltySlicePlugin {
    public:
        static void start();
        ~NoveltySlicePlugin();
    
    private:
        static void loop();
        static std::unique_ptr<NoveltySlicePlugin> s_inst;
    
        NoveltySlicePlugin();
        void frame();
        bool applyNoveltySlice();
        
        // New function signature - note it no longer returns the data
        bool readAudioSamples();
        void visualizeAudio(int numChannels);
    
        ImGui_Context *m_ctx;
        double m_threshold;
        int m_kernelSize;
        char m_status[255];
        fluid::client::FluidContext m_context;
        ParamSetType m_params;
        Client m_client;
        
        // Add this member variable
        std::vector<float> m_audioData;
        
        // Debounce variables
        std::chrono::steady_clock::time_point m_lastParamChange;
        bool m_paramsChanged;
        double m_debounceTimeMs;
    };

constexpr const char *g_name{"FluCoMa NoveltySlice"};
static int g_actionId;
std::unique_ptr<NoveltySlicePlugin> NoveltySlicePlugin::s_inst;

static void reportError(const ImGui_Error &e) {
    ShowMessageBox(e.what(), g_name, 0);
}

NoveltySlicePlugin::NoveltySlicePlugin()
    : m_ctx{},
      m_threshold(0.1f),
      m_kernelSize(3),
      m_status{"Ready to slice"},
      m_params{Client::getParameterDescriptors(), fluid::FluidDefaultAllocator()},
      m_client{m_params, m_context},
      m_paramsChanged(false),
      m_debounceTimeMs(30.0) { // 500ms debounce delay
    ImGui::init(plugin_getapi);
    m_ctx = ImGui::CreateContext(g_name);
    if (!m_ctx) {
        // Handle error
    } else {
        // Success
    }
    m_lastParamChange = std::chrono::steady_clock::now();
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

void NoveltySlicePlugin::frame() {
    ImGui::SetNextWindowSize(m_ctx, 400, 200, ImGui::Cond_FirstUseEver);

    bool open{true};
    if (ImGui::Begin(m_ctx, g_name, &open)) {
        // Store previous values to detect changes
        double prevThreshold = m_threshold;
        int prevKernelSize = m_kernelSize;
        
        // Display the sliders
        ImGui::SliderDouble(m_ctx, "Threshold", &m_threshold, 0.0f, 1.0f, "%.2f");
        ImGui::SliderInt(m_ctx, "Kernel Size", &m_kernelSize, 3, 100);
        
        bool paramsJustChanged = (prevThreshold != m_threshold || prevKernelSize != m_kernelSize);
        
        // Set flag and update time when params change
        if (paramsJustChanged) {
            m_paramsChanged = true;
            m_lastParamChange = std::chrono::steady_clock::now();
        }
        
        // Check if debounce timer has elapsed
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - m_lastParamChange).count();
            
        bool shouldProcess = false;
        
        // Process if params changed and debounce time elapsed
        if (m_paramsChanged && elapsedMs >= m_debounceTimeMs) {
            shouldProcess = true;
            m_paramsChanged = false;
            
            // Update status to show processing is happening
            strcpy(m_status, "Processing...");
        }
        
        // Also process if button is clicked
        if (ImGui::Button(m_ctx, "Apply NoveltySlice")) {
            shouldProcess = true;
            m_paramsChanged = false;
        }
        
        // Show debounce countdown if debouncing
        if (m_paramsChanged) {
            char debounceMsg[64];
            double remainingTime = m_debounceTimeMs - elapsedMs;
            if (remainingTime < 0) remainingTime = 0;
            
            snprintf(debounceMsg, sizeof(debounceMsg), 
                "Will process in %.1f ms...", remainingTime);
            ImGui::Text(m_ctx, debounceMsg);
        }
        
        if (shouldProcess) {
            applyNoveltySlice();
        }

        ImGui::Text(m_ctx, m_status);
        ImGui::End(m_ctx);
    }

    if (!open) return s_inst.reset();
}

bool NoveltySlicePlugin::readAudioSamples() {
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
        } else {
        }
    }

    DestroyAudioAccessor(accessor);

    if (!hasValidSamples) {
        strcpy(m_status, "No valid audio samples found");
        return false;
    }
    return true;
}

bool NoveltySlicePlugin::applyNoveltySlice() {
    m_audioData.clear();
    
    if (!readAudioSamples()) {
        strcpy(m_status, "Audio data reading failed");
        return false;
    }
    
    MediaItem* item = GetSelectedMediaItem(0, 0);
    if (!item) return false;
    
    MediaItem_Take* take = GetActiveTake(item);
    if (!take) return false;
    
    PCM_source* source = GetMediaItemTake_Source(take);
    if (!source) return false;
    
    int numChannels = GetMediaSourceNumChannels(source);
    if (numChannels <= 0) numChannels = 1;
    
    double sampleRate = GetMediaSourceSampleRate(source);
    int64_t numSamples = m_audioData.size() / numChannels;
    
    auto inputBuffer = InputBufferT::type(
        new fluid::VectorBufferAdaptor(m_audioData, numChannels, numSamples, sampleRate)
    );

    int estimatedSlices = static_cast<int>(numSamples / 1024);
    auto outBuffer = std::make_shared<MemoryBufferAdaptor>(1, estimatedSlices, sampleRate);
    auto outputBuffer = BufferT::type(outBuffer);

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
    m_client = Client(m_params, m_context);

    m_client.enqueue(m_params);
    Result result = m_client.process();
    
    auto startTime = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(10); // Adjust timeout as needed
    
    while(result.ok()) {
        ProcessState state = m_client.checkProgress(result);
        
        if (state == ProcessState::kDone || state == ProcessState::kDoneStillProcessing) {
            break;
        }
        
        auto currentTime = std::chrono::steady_clock::now();
        if (currentTime - startTime > timeout) {
            strcpy(m_status, "Processing timed out");
            return false;
        }

        // TODO: timeout?
    }
    
    if (!result.ok()) {
        strcpy(m_status, "Processing failed");
        return false;
    }
    
    BufferAdaptor::ReadAccess readAccess(m_params.template get<5>().get());
    if (!readAccess.valid()) {
        strcpy(m_status, "Invalid output buffer");
        return false;
    }
    
    // Get slice points data
    auto slicesView = readAccess.samps(0);
    
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

static bool commandHook(KbdSectionInfo *sec, const int command, const int val,
                        const int valhw, const int relmode, HWND hwnd) {
    if (command != g_actionId) {
        return false;
    }

    NoveltySlicePlugin::start();
    return true;
}

template <typename T>
void GetReaperFunc(reaper_plugin_info_t *rec, const char *funcName,
                   T &funcPtr) {
    funcPtr = reinterpret_cast<T>(rec->GetFunc(funcName));
}

extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
    REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t *rec) {
    if (!rec || rec->caller_version != REAPER_PLUGIN_VERSION) return 0;

    GetReaperFunc(rec, "plugin_getapi", plugin_getapi);
    GetReaperFunc(rec, "plugin_register", plugin_register);
    GetReaperFunc(rec, "ShowMessageBox", ShowMessageBox);
    GetReaperFunc(rec, "GetSelectedMediaItem", GetSelectedMediaItem);
    GetReaperFunc(rec, "GetActiveTake", GetActiveTake);
    GetReaperFunc(rec, "GetMediaItemTake_Source", GetMediaItemTake_Source);
    GetReaperFunc(rec, "GetMediaSourceSampleRate", GetMediaSourceSampleRate);
    GetReaperFunc(rec, "ShowConsoleMsg", ShowConsoleMsg);
    GetReaperFunc(rec, "GetMediaSourceNumChannels", GetMediaSourceNumChannels);
    GetReaperFunc(rec, "GetAudioAccessorSamples", GetAudioAccessorSamples);
    GetReaperFunc(rec, "GetMediaItemInfo_Value", GetMediaItemInfo_Value);
    GetReaperFunc(rec, "GetMediaItemTakeInfo_Value", GetMediaItemTakeInfo_Value);
    GetReaperFunc(rec, "CreateTakeAudioAccessor", CreateTakeAudioAccessor);
    GetReaperFunc(rec, "DestroyAudioAccessor", DestroyAudioAccessor);
    GetReaperFunc(rec, "UpdateTimeline", UpdateTimeline);
    GetReaperFunc(rec, "Undo_BeginBlock2", Undo_BeginBlock2);
    GetReaperFunc(rec, "Undo_EndBlock2", Undo_EndBlock2);
    
    // Take marker functions
    GetReaperFunc(rec, "SetTakeMarker", SetTakeMarker);
    GetReaperFunc(rec, "GetTakeMarker", GetTakeMarker);
    GetReaperFunc(rec, "GetNumTakeMarkers", GetNumTakeMarkers);
    GetReaperFunc(rec, "DeleteTakeMarker", DeleteTakeMarker);

    custom_action_register_t action{
        0, "FLUCOMA_NOVELTYSLICE",
        "FluCoMa: Apply NoveltySlice to selected item"};
    g_actionId = plugin_register("custom_action", &action);

    plugin_register("hookcommand2", reinterpret_cast<void *>(&commandHook));

    return 1;
}