// Real preferences navigation and live sample rendering in an off-screen terminal.
#include <clocale>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <ncurses.h>
#include "appconfig.h"
#include "fileutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
#include "uikeyconfig.h"
#include "uimodel.h"
#include "uiprefsdialog.h"

static void Check(bool ok, const std::string& message)
{
  if (!ok) throw std::runtime_error(message);
}
class Prefs : public UiPrefsDialog
{
public:
  explicit Prefs(UiModel* model) : UiPrefsDialog(UiDialogParams(model, "Preferences", 0.95, 0.85)) {}
  void Show() { Draw(); }
  bool HasPreview() { return GetListWidth() < m_W; }
  bool IsOpen() { return m_Running; }
  void Key(const std::string& name) { KeyHandler(UiKeyConfig::GetKey(name)); }
  bool TitleVisible()
  {
    std::wstring text;
    for (int x = 0; x < 11; ++x)
      text += (wchar_t)(mvwinch(curscr, m_Y, m_X + GetListWidth() + 3 + x) & A_CHARTEXT);
    return text == L"Live sample";
  }
  std::vector<unsigned long> Sample()
  {
    std::vector<unsigned long> result;
    if (!HasPreview()) return result;
    for (int y = 3; y < m_H - 6; ++y)
    {
      for (int x = GetListWidth() + 3; x < m_W; ++x)
      {
        cchar_t cell;
        wchar_t chars[CCHARW_MAX];
        attr_t attr;
        short legacy;
        int pair;
        mvwin_wch(curscr, m_Y + y, m_X + x, &cell);
        getcchar(&cell, chars, &attr, &legacy, &pair);
        result.push_back((unsigned long)chars[0]);
        result.push_back((unsigned long)pair);
      }
    }
    return result;
  }
};
int main(int argc, char** argv)
{
  if (argc != 2) return 2;
  setlocale(LC_ALL, "C.UTF-8");
  FileUtil::SetApplicationDir(argv[1]);
  AppConfig::Init();
  UiConfig::Init();
  UiConfig::SetStr("inline_image_style", "blocks");
  UiConfig::SetStr("inline_image_color", "color");
  UiConfig::SetNum("inline_image_lines", 10);
  UiConfig::SetStr("media_display", "name"); // prospective samples also work when previews are disabled
  FILE* output = fopen("/dev/null", "w");
  FILE* input = fopen("/dev/null", "r");
  SCREEN* screen = newterm("xterm-256color", output, input);
  resizeterm(40, 140);
  UiKeyConfig::Init(true);
  UiColorConfig::Init();
  try
  {
    UiModel model;
    model.Init();
    {
      Prefs dialog(&model);
      dialog.Show();
      Check(dialog.HasPreview() && dialog.TitleVisible(), "wide preferences shows live sample on right");
      Check(FileUtil::Exists(FileUtil::GetTempDir() + "/preview-sample.png"), "built-in PNG needs no external assets");
      const auto blocks = dialog.Sample();
      dialog.Key("down"); // renderer
      dialog.Key("right"); // Sixel; off-screen output must fall back safely
      Check(UiConfig::GetStr("inline_image_backend") == "sixel", "optional Sixel backend selectable");
      dialog.Key("left"); // characters
      dialog.Key("down"); // style
      dialog.Key("right"); // ASCII
      Check(UiConfig::GetStr("inline_image_style") == "ascii", "style changes through real arrow navigation");
      Check(dialog.Sample() != blocks, "sample updates immediately with style");
      const auto ascii = dialog.Sample();
      dialog.Key("down"); // colours
      dialog.Key("right"); // foreground
      Check(dialog.Sample() != ascii, "sample updates immediately with colour mode");
      const auto foreground = dialog.Sample();
      dialog.Key("down"); // height
      dialog.Key("right"); // 12
      Check(UiConfig::GetNum("inline_image_lines") == 12 && dialog.Sample() != foreground,
            "sample updates immediately with height");
      resizeterm(24, 80);
      dialog.Key("terminal_resize");
      Check(!dialog.HasPreview(), "narrow terminal preserves full-width settings");
      dialog.Key("up"); // colours
      dialog.Key("right"); // mono
      Check(UiConfig::GetStr("inline_image_color") == "mono", "settings still editable at narrow width");
      resizeterm(40, 140);
      dialog.Key("terminal_resize");
      Check(dialog.HasPreview() && dialog.TitleVisible(), "widening restores preview without reopening");
      dialog.Key("home");
      dialog.Key("down"); // renderer
      dialog.Key("down"); // style
      const std::string before = UiConfig::GetStr("inline_image_style");
      for (int i = 0; i < 8; ++i) dialog.Key("right");
      Check(UiConfig::GetStr("inline_image_style") == before, "all styles cycle and wrap with live samples");
      dialog.KeyHandler(27);
      Check(!dialog.IsOpen(), "Escape closes preferences");
    }
    model.Cleanup();
    std::cout << "PASS: live settings sample, style/colour/height changes, small-window fallback and resize\n";
  }
  catch (const std::exception& error)
  {
    endwin();
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  endwin();
  delscreen(screen);
  fclose(input);
  fclose(output);
  return 0;
}
