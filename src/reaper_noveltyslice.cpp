#define REAIMGUIAPI_IMPLEMENT
#include "reaper_imgui_functions.h"

#define REAPERAPI_IMPLEMENT
#include <algorithm>
#include <algorithms/public/NoveltySegmentation.hpp>
#include <clients/common/BufferAdaptor.hpp>
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
    
    // New functions for audio handling
    bool readAudioSamples(std::vector<float>& audioData);
    void visualizeAudio(const std::vector<float>& audioData, int numChannels);

    ImGui_Context *m_ctx;
    float m_threshold;
    int m_kernelSize;
    char m_status[255];
    fluid::client::FluidContext m_context;
    ParamSetType m_params;
    Client m_client;
};

constexpr const char *g_name{"FluCoMa NoveltySlice"};
static int g_actionId;
std::unique_ptr<NoveltySlicePlugin> NoveltySlicePlugin::s_inst;

static void reportError(const ImGui_Error &e) {
    ShowMessageBox(e.what(), g_name, 0);
}

NoveltySlicePlugin::NoveltySlicePlugin()
    : m_ctx{},
      m_threshold(0.5f),
      m_kernelSize(20),
      m_status{"Ready to slice"},
      m_params{Client::getParameterDescriptors(), fluid::FluidDefaultAllocator()},
      m_client{m_params, m_context} {
    ImGui::init(plugin_getapi);
    m_ctx = ImGui::CreateContext(g_name);
    if (!m_ctx) {
        // Handle error
    } else {
        // Success
    }
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
        // Add sliders for parameters
        // ImGui::SliderFloat(m_ctx, "Threshold", &m_threshold, 0.0f, 1.0f, "%.2f");
        // ImGui::SliderInt(m_ctx, "Kernel Size", &m_kernelSize, 1, 100);

        if (ImGui::Button(m_ctx, "Apply NoveltySlice")) {
            applyNoveltySlice();
        }

        ImGui::Text(m_ctx, m_status);
        ImGui::End(m_ctx);
    }

    if (!open) return s_inst.reset();
}

bool NoveltySlicePlugin::readAudioSamples(std::vector<float>& audioData) {
    // Clear any existing data
    audioData.clear();
    
    ShowConsoleMsg("NoveltySlice: Starting readAudioSamples()...\n");
    
    // Get the selected item
    MediaItem* item = GetSelectedMediaItem(0, 0);
    if (!item) {
        ShowConsoleMsg("NoveltySlice: Error - No item selected\n");
        strcpy(m_status, "No item selected");
        return false;
    }
    ShowConsoleMsg("NoveltySlice: Selected item found\n");

    // Get the active take
    MediaItem_Take* take = GetActiveTake(item);
    if (!take) {
        ShowConsoleMsg("NoveltySlice: Error - No active take in selected item\n");
        strcpy(m_status, "No active take in selected item");
        return false;
    }
    ShowConsoleMsg("NoveltySlice: Active take found\n");

    // Get source information
    PCM_source* source = GetMediaItemTake_Source(take);
    if (!source) {
        ShowConsoleMsg("NoveltySlice: Error - Failed to get media source\n");
        strcpy(m_status, "Failed to get media source");
        return false;
    }
    ShowConsoleMsg("NoveltySlice: Media source obtained\n");

    // Get sample rate and channel count
    double sampleRate = GetMediaSourceSampleRate(source);
    if (sampleRate <= 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "NoveltySlice: Error - Invalid sample rate: %.2f\n", sampleRate);
        ShowConsoleMsg(msg);
        strcpy(m_status, "Invalid sample rate");
        return false;
    }

    int numChannels = GetMediaSourceNumChannels(source);
    if (numChannels <= 0) {
        ShowConsoleMsg("NoveltySlice: Warning - Could not determine channel count, defaulting to 1\n");
        numChannels = 1; // Fallback if we couldn't get channels
    }

    // Get item position and length
    double startTime = GetMediaItemInfo_Value(item, "D_POSITION");
    double length = GetMediaItemInfo_Value(item, "D_LENGTH");
    int64_t numSamples = static_cast<int64_t>(length * sampleRate);
    
    char infoMsg[256];
    snprintf(infoMsg, sizeof(infoMsg), 
             "NoveltySlice: Audio specs - Sample rate: %.2f Hz, Channels: %d, Length: %.3f sec, Total samples: %lld\n", 
             sampleRate, numChannels, length, numSamples);
    ShowConsoleMsg(infoMsg);
    
    // Check for empty files
    if (numSamples <= 0) {
        ShowConsoleMsg("NoveltySlice: Error - File has no samples\n");
        strcpy(m_status, "File has no samples");
        return false;
    }

    // Create audio accessor
    ShowConsoleMsg("NoveltySlice: Creating audio accessor...\n");
    AudioAccessor* accessor = CreateTakeAudioAccessor(take);
    if (!accessor) {
        ShowConsoleMsg("NoveltySlice: Error - Failed to create audio accessor\n");
        strcpy(m_status, "Failed to create audio accessor");
        return false;
    }
    ShowConsoleMsg("NoveltySlice: Audio accessor created successfully\n");

    // Temporary interleaved buffer
    ShowConsoleMsg("NoveltySlice: Allocating interleaved buffer...\n");
    std::vector<float> interleavedData(numChannels * numSamples, 0.0f);
    snprintf(infoMsg, sizeof(infoMsg), "NoveltySlice: Allocated interleaved buffer of size %zu\n", interleavedData.size());
    ShowConsoleMsg(infoMsg);

    // Read audio data in blocks
    const int samplesPerBlock = 4096; // Reasonable block size
    std::vector<double> tempBuffer(numChannels * samplesPerBlock);
    
    bool hasValidSamples = false;
    char statusMsg[256];
    int blocksProcessed = 0;
    int validBlocks = 0;
    
    ShowConsoleMsg("NoveltySlice: Starting block-by-block audio reading...\n");
    
    for (int64_t sampleOffset = 0; sampleOffset < numSamples; sampleOffset += samplesPerBlock) {
        // Calculate how many samples to read in this block
        int samplesToRead = std::min(samplesPerBlock, static_cast<int>(numSamples - sampleOffset));
        
        // Calculate the time position for this block
        double position = startTime + (static_cast<double>(sampleOffset) / sampleRate);
        
        // Only log every 10th block to avoid console spam, except first and last block
        if (blocksProcessed % 10 == 0 || sampleOffset == 0 || sampleOffset + samplesToRead >= numSamples) {
            snprintf(infoMsg, sizeof(infoMsg), 
                    "NoveltySlice: Reading block %d - Offset: %lld, Position: %.3f sec, Samples: %d\n", 
                    blocksProcessed, sampleOffset, position, samplesToRead);
            ShowConsoleMsg(infoMsg);
        }
        
        // Read the audio samples
        int ret = GetAudioAccessorSamples(accessor, sampleRate, numChannels, 
                                         position, samplesToRead, tempBuffer.data());
        
        if (ret == 1) {  // Audio data was successfully retrieved
            hasValidSamples = true;
            validBlocks++;
            
            // Log sample values for the first block to verify data
            if (sampleOffset == 0) {
                ShowConsoleMsg("NoveltySlice: First block sample values: ");
                for (int i = 0; i < std::min(10, samplesToRead * numChannels); i++) {
                    char sampleVal[32];
                    snprintf(sampleVal, sizeof(sampleVal), "%.4f ", tempBuffer[i]);
                    ShowConsoleMsg(sampleVal);
                }
                ShowConsoleMsg("\n");
            }
            
            // Convert double to float and copy to interleaved buffer
            for (int i = 0; i < samplesToRead * numChannels; ++i) {
                interleavedData[sampleOffset * numChannels + i] = static_cast<float>(tempBuffer[i]);
            }
        } else {
            // Log error for failed block read
            snprintf(infoMsg, sizeof(infoMsg), 
                    "NoveltySlice: Failed to read block %d - ret: %d\n", 
                    blocksProcessed, ret);
            ShowConsoleMsg(infoMsg);
        }
        
        blocksProcessed++;
    }

    // Clean up the accessor
    ShowConsoleMsg("NoveltySlice: Destroying audio accessor...\n");
    DestroyAudioAccessor(accessor);
    ShowConsoleMsg("NoveltySlice: Audio accessor destroyed\n");

    if (!hasValidSamples) {
        ShowConsoleMsg("NoveltySlice: Error - No valid audio samples found\n");
        strcpy(m_status, "No valid audio samples found");
        return false;
    }
    
    snprintf(infoMsg, sizeof(infoMsg), 
            "NoveltySlice: Successfully read %d of %d blocks\n", 
            validBlocks, blocksProcessed);
    ShowConsoleMsg(infoMsg);

    // Now deinterleave the data for FluCoMa
    ShowConsoleMsg("NoveltySlice: Deinterleaving audio data...\n");
    // FluCoMa expects: [all samples for channel 0, all samples for channel 1, ...]
    // Currently we have: [chan0sample0, chan1sample0, chan0sample1, chan1sample1, ...]
    
    // Resize the output vector to hold all audio in non-interleaved format
    audioData.resize(numChannels * numSamples, 0.0f);
    snprintf(infoMsg, sizeof(infoMsg), "NoveltySlice: Allocated deinterleaved buffer of size %zu\n", audioData.size());
    ShowConsoleMsg(infoMsg);
    
    // Deinterleave the data
    for (int64_t sampleIdx = 0; sampleIdx < numSamples; sampleIdx++) {
        for (int chanIdx = 0; chanIdx < numChannels; chanIdx++) {
            // Source: interleaved format (sample-major)
            int sourceIdx = sampleIdx * numChannels + chanIdx;
            
            // Destination: non-interleaved format (channel-major)
            int destIdx = chanIdx * numSamples + sampleIdx;
            
            audioData[destIdx] = interleavedData[sourceIdx];
        }
    }

    // Log first few samples of deinterleaved data to verify format
    ShowConsoleMsg("NoveltySlice: First few deinterleaved samples (channel 0): ");
    for (int i = 0; i < std::min(10, static_cast<int>(numSamples)); i++) {
        char sampleVal[32];
        snprintf(sampleVal, sizeof(sampleVal), "%.4f ", audioData[i]);
        ShowConsoleMsg(sampleVal);
    }
    ShowConsoleMsg("\n");
    
    if (numChannels > 1) {
        ShowConsoleMsg("NoveltySlice: First few deinterleaved samples (channel 1): ");
        for (int i = 0; i < std::min(10, static_cast<int>(numSamples)); i++) {
            char sampleVal[32];
            snprintf(sampleVal, sizeof(sampleVal), "%.4f ", audioData[numSamples + i]);
            ShowConsoleMsg(sampleVal);
        }
        ShowConsoleMsg("\n");
    }

    // Success message with basic stats
    snprintf(statusMsg, sizeof(statusMsg), "Read %lld samples, %d channels", numSamples, numChannels);
    strcpy(m_status, statusMsg);
    
    ShowConsoleMsg("NoveltySlice: readAudioSamples() completed successfully\n");
    return true;
}

bool NoveltySlicePlugin::applyNoveltySlice() {
    // Container for the audio data
    std::vector<float> audioData;
    
    // Read the audio samples - this will deinterleave the data
    if (!readAudioSamples(audioData)) {
        return false; // readAudioSamples will set the status and error messages
    } else {
        strcpy(m_status, "reading went well");
    }
    return true;
    
    // Get item and take to determine channel count and sample rate
    MediaItem* item = GetSelectedMediaItem(0, 0);
    MediaItem_Take* take = GetActiveTake(item);
    PCM_source* source = GetMediaItemTake_Source(take);
    int numChannels = GetMediaSourceNumChannels(source);
    if (numChannels <= 0) numChannels = 1;
    double sampleRate = GetMediaSourceSampleRate(source);
    int64_t numSamples = audioData.size() / numChannels;
    
    // Now create a buffer adaptor for FluCoMa
    auto inputBuffer = InputBufferT::type(
        new fluid::VectorBufferAdaptor(audioData, numChannels, numSamples, sampleRate)
    );
    
    // Initialize output buffer with appropriate capacity
    std::vector<float> outputData(numSamples / 10); // Estimate: at most 1 slice per 10 samples
    auto outputBuffer = BufferT::type(
        new fluid::VectorBufferAdaptor(outputData, 1, outputData.size(), sampleRate)
    );

    // Set up parameters for the NoveltySlice client
    m_params.template set<0>(std::move(inputBuffer), nullptr);   // Input buffer
    m_params.template set<1>(FloatT::type(m_kernelSize), nullptr); // Feature kernel size
    m_params.template set<2>(FloatT::type(m_threshold), nullptr);  // Threshold
    m_params.template set<3>(LongT::type(1), nullptr);           // Min slice length
    m_params.template set<4>(LongT::type(0), nullptr);           // FFT size (0=auto)
    m_params.template set<5>(std::move(outputBuffer), nullptr);  // Output buffer
    m_params.template set<6>(FloatT::type(1.0), nullptr);        // Filter size
    
    // Process the audio
    Result result = m_client.process();
    
    if (!result.ok()) {
        strcpy(m_status, result.message().c_str());
        return false;
    }
    
    // Success
    strcpy(m_status, "NoveltySlice processed successfully");
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
    GetReaperFunc(rec, "AddProjectMarker", AddProjectMarker);
    GetReaperFunc(rec, "UpdateTimeline", UpdateTimeline);

    custom_action_register_t action{
        0, "FLUCOMA_NOVELTYSLICE",
        "FluCoMa: Apply NoveltySlice to selected item"};
    g_actionId = plugin_register("custom_action", &action);

    plugin_register("hookcommand2", reinterpret_cast<void *>(&commandHook));

    return 1;
}