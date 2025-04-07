#include "NoveltySlicePlugin.h"
#include "reaper_plugin_functions.h"
#include <algorithm>
#include <cmath>

constexpr const char *g_name{"Flucoma NoveltySlice"};

NoveltySlicePlugin::NoveltySlicePlugin()
    : FlucomaSlicerPluginBase<NoveltySliceClientType, NoveltySlicePlugin>(g_name),
      m_threshold(0.1f),
      m_kernelSize(3),
      m_prevThreshold(0.1f),
      m_prevKernelSize(3)
{
    strcpy(m_status, "Ready to slice");
    setSliceMarkerLabel("novelty");
    // setSliceBufferIndex(5);
}

NoveltySlicePlugin::~NoveltySlicePlugin() {
    // Base class destructor handles unregistering the timer
}

void NoveltySlicePlugin::drawParameterControls() {
    bool thresholdActive = ImGui::SliderDouble(m_ctx, "Threshold", &m_threshold, 0.0f, 1.0f, "%.2f");
    m_anyControlActive = ImGui::IsItemActive(m_ctx);
    bool kernelSizeActive = ImGui::SliderInt(m_ctx, "Kernel Size", &m_kernelSize, 3, 100);
    m_anyControlActive = m_anyControlActive || ImGui::IsItemActive(m_ctx);
}

bool NoveltySlicePlugin::haveParametersChanged() {
    return m_threshold != m_prevThreshold || m_kernelSize != m_prevKernelSize;
}

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
    
    return processAudio(); // This now calls the base class implementation
}

void NoveltySlicePlugin::setupParameters(int numChannels, int64_t numSamples, double sampleRate) {
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
    m_params.template set<9>(LongRuntimeMaxParam(3, 3), nullptr); // filterSize
    m_params.template set<10>(LongT::type(2), nullptr);        // minSliceLength
    
    m_params.template set<11>(fluid::client::FFTParams(1024, -1, -1), nullptr); // hopSize
}

fluid::client::BufferAdaptor* NoveltySlicePlugin::getSliceBuffer() {
    return m_params.template get<5>().get();
}