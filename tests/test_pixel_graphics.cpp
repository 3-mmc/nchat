// Sixel encoding and overlay lifecycle on a private pseudo-terminal; no real accounts.
#include <atomic>
#include <clocale>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <thread>
#include <sys/ioctl.h>
#include <unistd.h>
#include <ncurses.h>
#include "appconfig.h"
#include "fileutil.h"
#include "protocolutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
#include "uikeyconfig.h"
#include "uimodel.h"
#include "uipixelgraphics.h"
#include "uiprefsdialog.h"
#include "uipreviewsample.h"
#include "uitextinputdialog.h"

static void Check(bool ok, const std::string& message)
{
  if (!ok) throw std::runtime_error(message);
}
class Capture
{
public:
  int master = -1, saved = -1;
  std::atomic<bool> stop{false};
  std::thread reader;
  std::string data;
  Capture()
  {
    master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    Check(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0, "create private PTY");
    const int slave = open(ptsname(master), O_RDWR | O_NOCTTY);
    Check(slave >= 0, "open PTY slave");
    saved = dup(STDOUT_FILENO);
    fflush(stdout);
    dup2(slave, STDOUT_FILENO);
    close(slave);
    Metrics(120, 40, 10, 20);
    reader = std::thread([&]()
    {
      char buffer[8192];
      while (true)
      {
        const auto n = read(master, buffer, sizeof(buffer));
        if (n > 0) data.append(buffer, n);
        else if (stop) break;
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });
  }
  void Metrics(int columns, int rows, int cw, int ch)
  {
    struct winsize size = {};
    size.ws_col = columns; size.ws_row = rows;
    size.ws_xpixel = columns * cw; size.ws_ypixel = rows * ch;
    Check(ioctl(STDOUT_FILENO, TIOCSWINSZ, &size) == 0, "set test pixel geometry");
  }
  void Finish()
  {
    if (saved < 0) return;
    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved); saved = -1;
    stop = true;
    reader.join();
    close(master); master = -1;
  }
  ~Capture() { Finish(); }
};
class Fake : public Protocol
{
public:
  std::string GetProfileId() const override { return "Dummy_pixels"; }
  std::string GetProfileDisplayName() const override { return ""; }
  bool HasFeature(ProtocolFeature) const override { return false; }
  bool IsGroupChat(const std::string&) const override { return false; }
  std::string GetSelfId() const override { return "self"; }
  bool SetupProfile(const std::string&, std::string&) override { return false; }
  bool LoadProfile(const std::string&, const std::string&) override { return false; }
  bool CloseProfile() override { return true; }
  bool Login() override { return true; }
  bool Logout() override { return true; }
  void SendRequest(std::shared_ptr<RequestMessage>) override {}
  void SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>&) override {}
};
class Prefs : public UiPrefsDialog
{
public:
  explicit Prefs(UiModel* model) : UiPrefsDialog(UiDialogParams(model, "Preferences", 0.95, 0.85)) {}
  void Show() { Draw(); }
};
static void Key(UiModel& model, const std::string& name)
{
  model.KeyHandler(UiKeyConfig::GetKey(name)); model.Draw();
}
static void Photo(UiModel& model, const std::string& chat, const std::string& path)
{
  auto message = std::make_shared<NewMessagesNotify>("Dummy_pixels");
  message->success = true; message->cached = true; message->chatId = chat;
  ChatMessage photo;
  photo.id = "photo-" + chat; photo.senderId = chat; photo.isOutgoing = false; photo.isRead = true;
  photo.timeSent = 100;
  FileInfo file;
  file.fileStatus = FileStatusDownloaded; file.filePath = path; file.fileType = "image/png";
  photo.fileInfo = ProtocolUtil::FileInfoToHex(file);
  message->chatMessages.push_back(photo);
  model.MessageHandler(message);
}
static std::string Phase(const std::string& data, const std::string& from, const std::string& to)
{
  const auto begin = data.find(from), end = data.find(to, begin);
  Check(begin != std::string::npos && end != std::string::npos, "capture phase markers");
  return data.substr(begin, end - begin);
}
int main(int argc, char** argv)
{
  if (argc != 2) return 2;
  setlocale(LC_ALL, "C.UTF-8");
  FileUtil::SetApplicationDir(argv[1]);
  AppConfig::Init(); UiConfig::Init();
  UiConfig::SetStr("inline_image_backend", "sixel");
  UiConfig::SetStr("media_display", "preview");
  UiConfig::SetNum("inline_image_lines", 10);
  UiConfig::SetStr("pinned_only_profiles", "");
  unsetenv("TMUX"); unsetenv("STY");
  FILE* output = fopen("/dev/null", "w"), *input = fopen("/dev/null", "r");
  SCREEN* screen = newterm("xterm-256color", output, input);
  resizeterm(40, 120);
  UiKeyConfig::Init(true); UiColorConfig::Init();
  const std::string imagePath = std::string(argv[1]) + "/sample's.png";
  FileUtil::WriteFile(imagePath, std::string((const char*)UiPreviewSample, sizeof(UiPreviewSample)));
  try
  {
    Check(!UiPixelGraphics::GetImage(imagePath, 40, 10), "redirected output falls back without raw graphics");
    Capture capture;
    const int owner = 1, modal = 2, nested = 3;
    auto image = UiPixelGraphics::GetImage(imagePath, 40, 10, &owner);
    Check(image && image->data.find("\033P") == 0 && image->data.substr(image->data.size() - 2) == "\033\\",
          "real Chafa Sixel raster is extracted without other control sequences");
    Check(image->columns <= 40 && image->rows <= 10 && image->pixelWidth <= 400 && image->pixelHeight <= 200,
          "raster fits exact terminal cell rectangle");
    Check(UiPixelGraphics::GetImage(imagePath, 40, 10, &owner) == image, "encoded raster is cached");
    UiPixelGraphics::Show(&owner, {{image, 23, 2}});
    Check(UiPixelGraphics::Clear(&owner), "clear tracks an emitted overlay");
    Check(!UiPixelGraphics::Clear(&owner), "repeated clear is harmless");
    UiPixelGraphics::Show(&owner, {{image, 119, 2}, {image, 1, 39}});
    Check(!UiPixelGraphics::Clear(&owner), "out-of-bounds placements never draw or scroll terminal");
    UiPixelGraphics::Show(&owner, {{image, 23, 2}});
    const auto revision = UiPixelGraphics::Revision();
    UiPixelGraphics::BeginDialog(&modal);
    Check(UiPixelGraphics::Revision() != revision && !UiPixelGraphics::Clear(&owner), "modal clears old chat graphics");
    Check(!UiPixelGraphics::GetImage(imagePath, 40, 10, &owner), "covered chat cannot repaint over modal");
    Check((bool)UiPixelGraphics::GetImage(imagePath, 40, 10, &modal), "top modal may draw its own sample");
    UiPixelGraphics::Show(&modal, {{image, 60, 3}});
    UiPixelGraphics::BeginDialog(&nested);
    Check(!UiPixelGraphics::GetImage(imagePath, 40, 10, &modal), "nested dialog hides preference sample");
    UiPixelGraphics::EndDialog(&nested);
    Check((bool)UiPixelGraphics::GetImage(imagePath, 40, 10, &modal), "return restores parent sample eligibility");
    UiPixelGraphics::EndDialog(&modal);
    setenv("TMUX", "test", 1);
    Check(!UiPixelGraphics::GetImage(imagePath, 40, 10), "multiplexers conservatively use character fallback");
    unsetenv("TMUX");
    capture.Metrics(120, 40, 0, 0);
    Check(!UiPixelGraphics::GetImage(imagePath, 40, 10), "missing pixel metrics uses character fallback");
    capture.Metrics(120, 40, 8, 18);
    auto resized = UiPixelGraphics::GetImage(imagePath, 40, 10);
    Check(resized && resized != image && resized->pixelWidth <= 320 && resized->pixelHeight <= 180,
          "font-size changes regenerate bounded raster");
    Check(!UiPixelGraphics::GetImage(imagePath + "missing", 40, 10), "missing file falls back");
    UiConfig::SetStr("inline_image_backend", "symbols");
    Check(!UiPixelGraphics::GetImage(imagePath, 40, 10), "default backend emits no graphics");
    UiConfig::SetStr("inline_image_backend", "sixel");
    capture.Metrics(120, 40, 10, 20);
    {
      UiModel model; model.Init(); model.AddProtocol(std::make_shared<Fake>());
      auto contacts = std::make_shared<NewContactsNotify>("Dummy_pixels");
      contacts->contactInfos.push_back({"alice", "Alice", ""});
      contacts->contactInfos.push_back({"bob", "Bob", ""});
      model.MessageHandler(contacts);
      auto chats = std::make_shared<NewChatsNotify>("Dummy_pixels"); chats->success = true;
      ChatInfo alice; alice.id = "alice"; alice.lastMessageTime = 200;
      ChatInfo bob; bob.id = "bob"; bob.lastMessageTime = 100;
      chats->chatInfos = {alice, bob}; model.MessageHandler(chats);
      Photo(model, "alice", imagePath);
      Key(model, "next_chat"); Key(model, "tab_1");
      // Make Alice current deterministically by inspecting the header.
      for (int n = 0; n < 3; ++n)
      {
        wchar_t text[30] = {};
        mvwinnwstr(curscr, 1, 23, text, 29);
        if (std::wstring(text).find(L"Alice") != std::wstring::npos) break;
        Key(model, "next_chat");
      }
      fputs("[ALICE]", stdout);
      model.ReinitView(); model.Draw();
      fputs("[BOB]", stdout);
      Key(model, "next_chat");
      fputs("[SPLIT]", stdout);
      Photo(model, "bob", imagePath);
      Key(model, "toggle_split");
      fputs("[PREFS]", stdout);
      {
        Prefs preferences(&model); preferences.Show();
        fputs("[NESTED]", stdout);
        { UiTextInputDialog dialog(UiDialogParams(&model, "Test input", 0.6, 5), "Path: ", ""); }
        fputs("[RETURN]", stdout);
        preferences.Show();
      }
      fputs("[RESTORE]", stdout);
      model.Draw();
      fputs("[CROP]", stdout);
      resizeterm(18, 45); model.ReinitView(); model.Draw();
      fputs("[END]", stdout);
      model.Cleanup();
    }
    UiPixelGraphics::ClearAll(); capture.Finish();
    Check(Phase(capture.data, "[ALICE]", "[BOB]").find("\033P") != std::string::npos, "chat image uses actual pixel backend");
    const auto bob = Phase(capture.data, "[BOB]", "[SPLIT]");
    Check(bob.find('X') != std::string::npos && bob.find("\033P") == std::string::npos,
          "switching to empty chat erases old pixels without redrawing them");
    const auto split = Phase(capture.data, "[SPLIT]", "[PREFS]");
    const auto firstRaster = split.find("\033P");
    Check(firstRaster != std::string::npos && split.find("\033P", firstRaster + 2) != std::string::npos,
          "split view draws pixel previews for both chats");
    Check(Phase(capture.data, "[PREFS]", "[NESTED]").find("\033P") != std::string::npos, "preferences displays pixel sample");
    Check(Phase(capture.data, "[NESTED]", "[RETURN]").find("\033P") == std::string::npos, "nested input stays free of overlays");
    Check(Phase(capture.data, "[RETURN]", "[RESTORE]").find("\033P") != std::string::npos, "sample returns after nested dialog");
    Check(Phase(capture.data, "[RESTORE]", "[CROP]").find("\033P") != std::string::npos, "chat pixels return after closing preferences");
    Check(Phase(capture.data, "[CROP]", "[END]").find("\033P") == std::string::npos, "partially clipped images use character fallback");
    std::cout << "PASS: Sixel raster geometry, cache, fallback, chat switching, split view, modal/nested cleanup and clipping\n";
  }
  catch (const std::exception& error)
  {
    endwin(); std::cerr << "FAIL: " << error.what() << '\n'; return 1;
  }
  endwin(); delscreen(screen); fclose(input); fclose(output); return 0;
}
