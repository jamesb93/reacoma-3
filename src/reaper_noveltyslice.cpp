#define REAIMGUIAPI_IMPLEMENT
#include "reaper_imgui_functions.h"

#define REAPERAPI_IMPLEMENT
#include "reaper_plugin_functions.h"
#include "NoveltySlicePlugin.h"

static int g_actionId;

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