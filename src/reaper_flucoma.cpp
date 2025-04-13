#define REAIMGUIAPI_IMPLEMENT
#include "reaper_imgui_functions.h"
#define REAPERAPI_IMPLEMENT
#include "reaper_plugin_functions.h"
#include "NoveltySlicePlugin.cpp"
#include "HPSSPlugin.cpp"

struct PluginAction {
    const char* id;
    const char* description;
    void (*handler)();
};

static const PluginAction g_pluginActions[] = {
    {"REACOMA_NOVELTYSLICE", "ReaCoMa: NoveltySlice", NoveltySlicePlugin::start},
    {"REACOMA_HPSS", "ReaCoMa: HPSS", HPSSPlugin::start},
};

static const size_t g_numActions = sizeof(g_pluginActions) / sizeof(g_pluginActions[0]);
static int g_actionIds[g_numActions];

static bool commandHook(KbdSectionInfo *sec, const int command, const int val,
                       const int valhw, const int relmode, HWND hwnd) {
    for (size_t i = 0; i < g_numActions; i++) {
        if (command == g_actionIds[i]) {
            g_pluginActions[i].handler();
            return true;
        }
    }
    return false;
}

template <typename T>
void GetReaperFunc(reaper_plugin_info_t *rec, const char *funcName, T &funcPtr) {
    funcPtr = reinterpret_cast<T>(rec->GetFunc(funcName));
}

template <typename... Args>
void GetReaperFuncs(reaper_plugin_info_t *rec, Args&&... args) {}

template <typename T, typename... Args>
void GetReaperFuncs(reaper_plugin_info_t *rec, const char* funcName, T& funcPtr, Args&&... args) {
    GetReaperFunc(rec, funcName, funcPtr);
    GetReaperFuncs(rec, std::forward<Args>(args)...);
}

extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
    REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t *rec) {
    if (!rec || rec->caller_version != REAPER_PLUGIN_VERSION) return 0;
    
    GetReaperFuncs(rec,
        "plugin_getapi", plugin_getapi,
        "plugin_register", plugin_register,
        "ShowMessageBox", ShowMessageBox,
        "GetSelectedMediaItem", GetSelectedMediaItem,
        "GetActiveTake", GetActiveTake,
        "GetNumTakeMarkers", GetNumTakeMarkers,
        "DeleteTakeMarker", DeleteTakeMarker,
        "SetTakeMarker", SetTakeMarker,
        "GetMediaItemTake_Source", GetMediaItemTake_Source,
        "GetMediaSourceSampleRate", GetMediaSourceSampleRate,
        "ShowConsoleMsg", ShowConsoleMsg,
        "GetMediaSourceNumChannels", GetMediaSourceNumChannels,
        "GetAudioAccessorSamples", GetAudioAccessorSamples,
        "GetMediaItemInfo_Value", GetMediaItemInfo_Value,
        "GetMediaItemTakeInfo_Value", GetMediaItemTakeInfo_Value,
        "CreateTakeAudioAccessor", CreateTakeAudioAccessor,
        "DestroyAudioAccessor", DestroyAudioAccessor,
        "UpdateTimeline", UpdateTimeline,
        "Undo_BeginBlock2", Undo_BeginBlock2,
        "Undo_EndBlock2", Undo_EndBlock2,
        "AddTakeToMediaItem", AddTakeToMediaItem,
        "SetMediaItemTakeInfo_Value", SetMediaItemTakeInfo_Value,
        "GetSetMediaItemTakeInfo_String", GetSetMediaItemTakeInfo_String,
        "SetActiveTake", SetActiveTake,
        "UpdateItemInProject", UpdateItemInProject,
        "PCM_Source_CreateFromFileEx", PCM_Source_CreateFromFileEx,
        "PCM_Source_Destroy", PCM_Source_Destroy,
        "InsertMedia", InsertMedia,
        "UpdateArrange", UpdateArrange
    );
    
    for (size_t i = 0; i < g_numActions; i++) {
        custom_action_register_t action{
            0, g_pluginActions[i].id, g_pluginActions[i].description
        };
        g_actionIds[i] = plugin_register("custom_action", &action);
    }
    
    plugin_register("hookcommand2", reinterpret_cast<void *>(&commandHook));
    return 1;
}