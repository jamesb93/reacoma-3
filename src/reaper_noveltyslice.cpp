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

#include "reaper_plugin_functions.h"

using namespace fluid::client;
using namespace noveltyslice;
using Client = ClientWrapper<NoveltySliceClient>;
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
    ParamSetType &initParams();

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
      m_status{"Ready to slice"},
      m_params{noveltyslice::NoveltySliceClient::getParameterDescriptors(),
               fluid::FluidDefaultAllocator()},
      m_client{m_params, m_context} {
    ImGui::init(plugin_getapi);
    m_ctx = ImGui::CreateContext(g_name);
    if (!m_ctx) {
    } else {
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
    ImGui::SetNextWindowSize(m_ctx, 400, 150, ImGui::Cond_FirstUseEver);

    bool open{true};
    if (ImGui::Begin(m_ctx, g_name, &open)) {
        // ImGui::SliderInt(m_ctx, "Threshold", &m_threshold, 0, 100);
        // ImGui::SliderInt(m_ctx, "Kernel Size", &m_kernelSize, 1, 10);

        if (ImGui::Button(m_ctx, "Apply NoveltySlice")) {
            if (applyNoveltySlice())
                strcpy(m_status, "NoveltySlice applied successfully");
            else
                strcpy(m_status, "Failed to apply NoveltySlice");
        }

        ImGui::Text(m_ctx, m_status);
        ImGui::End(m_ctx);
    }

    if (!open) return s_inst.reset();
}

namespace fluid {

class VectorBufferAdaptor : public BufferAdaptor {
   public:
    VectorBufferAdaptor(std::vector<float> &data, index numChannels,
                        index numFrames, double sampleRate)
        : mData(data.data(), 0, numFrames, numChannels),
          mSampleRate(sampleRate),
          mAcquired(false) {}

    bool acquire() const override { return !mAcquired && (mAcquired = true); }
    void release() const override { mAcquired = false; }

    bool valid() const override { return numFrames() > 0; }
    bool exists() const override { return true; }

    const Result resize(index frames, index channels,
                        double sampleRate) override {
        return Result{Result::Status::kError, "Resize not supported"};
    }

    std::string asString() const override { return "VectorBufferAdaptor"; }

    FluidTensorView<float, 2> allFrames() override { return mData; }

    FluidTensorView<const float, 2> allFrames() const override { return mData; }

    FluidTensorView<float, 1> samps(index channel) override {
        return mData.col(channel);
    }

    FluidTensorView<float, 1> samps(index offset, index nframes,
                                    index chanoffset) override {
        return mData(Slice(offset, nframes), Slice(chanoffset, 1)).col(0);
    }

    FluidTensorView<const float, 1> samps(index channel) const override {
        return mData.col(channel);
    }

    FluidTensorView<const float, 1> samps(index offset, index nframes,
                                          index chanoffset) const override {
        return mData(Slice(offset, nframes), Slice(chanoffset, 1)).col(0);
    }

    index numFrames() const override { return mData.rows(); }
    index numChans() const override { return mData.cols(); }
    double sampleRate() const override { return mSampleRate; }

   private:
    FluidTensorView<float, 2> mData;
    double mSampleRate;
    mutable bool mAcquired;
};
}  // namespace fluid

bool NoveltySlicePlugin::applyNoveltySlice() {
    MediaItem *item = GetSelectedMediaItem(0, 0);
    if (!item) {
        strcpy(m_status, "No item selected");
        return false;
    }

    MediaItem_Take *take = GetActiveTake(item);
    if (!take) {
        strcpy(m_status, "No active take in selected item");
        return false;
    }

    double sampleRate = GetMediaSourceSampleRate(GetMediaItemTake_Source(take));
    double startTime = GetMediaItemInfo_Value(item, "D_POSITION");
    double endTime = startTime + GetMediaItemInfo_Value(item, "D_LENGTH");

    // Try to get actual channel count
    PCM_source *source = GetMediaItemTake_Source(take);
    int numChannels = 1;
    if (source) {
        numChannels = GetMediaSourceNumChannels(source);
        if (numChannels <= 0)
            numChannels = 1;  // Fallback if we couldn't get channels
    }

    int64_t numSamples =
        static_cast<int64_t>((endTime - startTime) * sampleRate);

    AudioAccessor *accessor = CreateTakeAudioAccessor(take);
    if (!accessor) {
        strcpy(m_status, "Failed to create audio accessor");
        return false;
    }

    std::vector<float> audioData(numChannels * numSamples, 0.0f);
    std::vector<double> tempBuffer(numChannels * 1024);

    bool hasValidSamples = false;
    int samplesPerBlock = 1024;
    char msgBuf[256];

    // Read audio data
    for (int64_t offset = 0; offset < numSamples; offset += samplesPerBlock) {
        int samplesToRead =
            std::min(samplesPerBlock, static_cast<int>(numSamples - offset));
        double position = startTime + (offset / sampleRate);

        int ret =
            GetAudioAccessorSamples(accessor, sampleRate, numChannels, position,
                                    samplesToRead, tempBuffer.data());

        if (ret == 1)  // Audio data was successfully retrieved
        {
            hasValidSamples = true;
            // Convert double to float and copy to audioData
            for (int i = 0; i < samplesToRead * numChannels; ++i) {
                audioData[offset * numChannels + i] =
                    static_cast<float>(tempBuffer[i]);
            }
        }
    }

    DestroyAudioAccessor(accessor);

    if (!hasValidSamples) {
        strcpy(m_status, "No valid audio samples found in selection");
        return false;
    }

    // Create buffer adaptor for the audio
    auto inputBuffer = std::make_shared<fluid::VectorBufferAdaptor>(
        audioData, numChannels, numSamples, sampleRate);

    // // Set up parameters - use the set method, not reset
    // // Parameter 0 is typically the input buffer
    // m_params.set<0>(inputBuffer);

    // // Create output container for slice points
    std::vector<double> slicePoints;

    ShowConsoleMsg("Processing audio with NoveltySlice...\n");

    // // Process with the correct context parameter
    Result result = m_client.process();

    // if (!result.ok()) {
    //     snprintf(m_status, sizeof(m_status), "Error: %s",
    //              result.message().c_str());
    //     return false;
    // }

    // // Get the slice points from the output parameter
    // auto outputBuffer = m_params.get<1>();
    // if (!outputBuffer) {
    //     strcpy(m_status, "Failed to get output buffer");
    //     return false;
    // }

    // // Extract slice points from the output buffer
    // // The actual way to access data depends on the specific output type
    // // This is an approximation - adjust based on actual return type
    // const auto &outputData = outputBuffer->get();
    // if (outputData.size() == 0) {
    //     strcpy(m_status, "No slice points found");
    //     return false;
    // }

    // // Create markers from slice points
    // int sliceCount = 0;
    // for (const auto &slicePoint : outputData) {
    //     double markerPosition = startTime + slicePoint / sampleRate;
    //     AddProjectMarker(nullptr, false, markerPosition, 0, "Slice", -1);
    //     sliceCount++;
    // }

    // snprintf(m_status, sizeof(m_status), "Created %d slice markers",
    //          sliceCount);
    // UpdateTimeline();

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
    GetReaperFunc(rec, "GetMediaItemTakeInfo_Value",
                  GetMediaItemTakeInfo_Value);
    GetReaperFunc(rec, "GetMediaSourceNumChannels", GetMediaSourceNumChannels);
    GetReaperFunc(rec, "CreateTakeAudioAccessor", CreateTakeAudioAccessor);
    GetReaperFunc(rec, "GetAudioAccessorSamples", GetAudioAccessorSamples);
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