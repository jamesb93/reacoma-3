#pragma once

#include "reaper_imgui_functions.h"
#include <clients/common/FluidContext.hpp>
#include <clients/common/ParameterSet.hpp>
#include <clients/rt/NoveltySliceClient.hpp>
#include <memory>
#include <vector>
#include <chrono>

// Using declarations for FluCoMa types
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
    
    bool readAudioSamples();
    void visualizeAudio(int numChannels);

    ImGui_Context *m_ctx;
    double m_threshold;
    int m_kernelSize;
    char m_status[255];
    fluid::client::FluidContext m_context;
    ParamSetType m_params;
    Client m_client;
    
    // Audio data storage
    std::vector<float> m_audioData;
    
    // Debounce variables
    std::chrono::steady_clock::time_point m_lastParamChange;
    bool m_paramsChanged;
    double m_debounceTimeMs;
};