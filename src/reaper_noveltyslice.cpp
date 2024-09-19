#define REAIMGUIAPI_IMPLEMENT
#include "reaper_imgui_functions.h"

#define REAPERAPI_IMPLEMENT
#include <algorithms/public/NoveltySegmentation.hpp>
#include <clients/common/FluidBaseClient.hpp>
#include <clients/common/FluidContext.hpp>
#include <clients/common/FluidNRTClientWrapper.hpp>
#include <clients/common/ParameterSet.hpp>
#include <clients/common/ParameterTypes.hpp>
#include <clients/rt/NoveltySliceClient.hpp>
#include <data/FluidMemory.hpp>
#include "reaper_plugin_functions.h"
#include <memory>

using namespace fluid::client;
using namespace noveltyslice;
using Client = ClientWrapper<NoveltySliceClient>;
using ParamSetType = typename Client::ParamSetType;

class NoveltySlicePlugin
{
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
  int m_threshold;
  int m_kernelSize;
  char m_status[255];
  fluid::client::FluidContext m_context;
  ParamSetType m_params;
  Client m_client;
};

constexpr const char *g_name{"FluCoMa NoveltySlice"};
static int g_actionId;
std::unique_ptr<NoveltySlicePlugin> NoveltySlicePlugin::s_inst;

static void reportError(const ImGui_Error &e)
{
  ShowMessageBox(e.what(), g_name, 0);
}

NoveltySlicePlugin::NoveltySlicePlugin()
    : m_ctx{}, m_status{"Ready to slice"},
      m_params{noveltyslice::NoveltySliceClient::getParameterDescriptors(),
               fluid::FluidDefaultAllocator()},
      m_client{m_params, m_context}
{
  ImGui::init(plugin_getapi);
  m_ctx = ImGui::CreateContext(g_name);
  if (!m_ctx)
  {
    ShowConsoleMsg("Failed to create ImGui context\n");
  }
  else
  {
    ShowConsoleMsg("ImGui context created successfully\n");
  }
  plugin_register("timer", (void *)NoveltySlicePlugin::loop);
}

NoveltySlicePlugin::~NoveltySlicePlugin()
{
  ShowConsoleMsg("NoveltySlicePlugin::~NoveltySlicePlugin() called\n");
  plugin_register("-timer", reinterpret_cast<void *>(&loop));
}

void NoveltySlicePlugin::start()
try
{
  ShowConsoleMsg("NoveltySlicePlugin::start() called\n");
  if (s_inst)
    ImGui::SetNextWindowFocus(s_inst->m_ctx);
  else
  {
    s_inst.reset(new NoveltySlicePlugin);
    ShowConsoleMsg("New NoveltySlicePlugin instance created\n");
  }
}
catch (const ImGui_Error &e)
{
  reportError(e);
  s_inst.reset();
}

void NoveltySlicePlugin::loop()
{
  ShowConsoleMsg("NoveltySlicePlugin::loop() called\n");
  if (s_inst)
  {
    s_inst->frame();
  }
  else
  {
    ShowConsoleMsg("NoveltySlicePlugin instance is null in loop()\n");
  }
}

void NoveltySlicePlugin::frame()
{
  ShowConsoleMsg("NoveltySlicePlugin::frame() called\n");
  ImGui::SetNextWindowSize(m_ctx, 400, 150, ImGui::Cond_FirstUseEver);

  bool open{true};
  if (ImGui::Begin(m_ctx, g_name, &open))
  {
    ShowConsoleMsg("ImGui::Begin() succeeded\n");
    // ImGui::SliderInt(m_ctx, "Threshold", &m_threshold, 0, 100);
    // ImGui::SliderInt(m_ctx, "Kernel Size", &m_kernelSize, 1, 10);

    if (ImGui::Button(m_ctx, "Apply NoveltySlice"))
    {
      if (applyNoveltySlice())
        strcpy(m_status, "NoveltySlice applied successfully");
      else
        strcpy(m_status, "Failed to apply NoveltySlice");
    }

    ImGui::Text(m_ctx, m_status);
    ImGui::End(m_ctx);
  }
  else
  {
    ShowConsoleMsg("ImGui::Begin() failed\n");
  }

  if (!open)
    return s_inst.reset();
}

bool NoveltySlicePlugin::applyNoveltySlice()
{
  MediaItem *item = GetSelectedMediaItem(0, 0);
  if (!item)
    return false;

  MediaItem_Take *take = GetActiveTake(item);
  if (!take)
    return false;

  PCM_source *source = GetMediaItemTake_Source(take);
  if (!source)
    return false;

  double sampleRate = GetMediaSourceSampleRate(source);

  // Show the sample rate in a message box
  ShowMessageBox(std::to_string(sampleRate).c_str(), g_name, 0);

  

  return true;
}

static bool commandHook(KbdSectionInfo *sec, const int command, const int val,
                        const int valhw, const int relmode, HWND hwnd)
{
  if (command != g_actionId)
  {
    return false;
  }

  NoveltySlicePlugin::start();
  return true;
}

extern "C" REAPER_PLUGIN_DLL_EXPORT int
REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE instance,
                         reaper_plugin_info_t *rec)
{
  if (!rec)
    return 0;
  if (rec->caller_version != REAPER_PLUGIN_VERSION)
    return 0;

  plugin_getapi =
      reinterpret_cast<decltype(plugin_getapi)>(rec->GetFunc("plugin_getapi"));
  plugin_register = reinterpret_cast<decltype(plugin_register)>(
      rec->GetFunc("plugin_register"));
  ShowMessageBox = reinterpret_cast<decltype(ShowMessageBox)>(
      rec->GetFunc("ShowMessageBox"));

  GetSelectedMediaItem = reinterpret_cast<decltype(GetSelectedMediaItem)>(
      rec->GetFunc("GetSelectedMediaItem"));
  GetActiveTake =
      reinterpret_cast<decltype(GetActiveTake)>(rec->GetFunc("GetActiveTake"));
  GetMediaItemTake_Source = reinterpret_cast<decltype(GetMediaItemTake_Source)>(
      rec->GetFunc("GetMediaItemTake_Source"));
  GetMediaSourceSampleRate =
      reinterpret_cast<decltype(GetMediaSourceSampleRate)>(
          rec->GetFunc("GetMediaSourceSampleRate"));

  ShowConsoleMsg = reinterpret_cast<decltype(ShowConsoleMsg)>(
      rec->GetFunc("ShowConsoleMsg"));

  custom_action_register_t action{
      0, "FLUCOMA_NOVELTYSLICE",
      "FluCoMa: Apply NoveltySlice to selected item"};
  g_actionId = plugin_register("custom_action", &action);

  plugin_register("hookcommand2", reinterpret_cast<void *>(&commandHook));

  return 1;
}