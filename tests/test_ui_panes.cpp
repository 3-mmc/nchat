// Headless UI regression: pane layout, independent drafts and send routing.
// Uses fake protocols; never opens real accounts or sends network messages.
// nchat is distributed under the MIT license, see LICENSE for details.

#include <algorithm>
#include <clocale>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <ncurses.h>
#include "appconfig.h"
#include "fileutil.h"
#include "strutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
#include "uihelpview.h"
#include "uikeyconfig.h"
#include "uikeyinput.h"
#include "uimodel.h"

struct Sent { std::string profile, chat, text; };
static std::vector<Sent> sent;
static std::string s_CheckDir;
static void Check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
class Fake : public Protocol
{
  std::string id;
public:
  explicit Fake(std::string value) : id(value) {}
  std::string GetProfileId() const override { return id; }
  std::string GetProfileDisplayName() const override { return ""; }
  bool HasFeature(ProtocolFeature) const override { return false; }
  bool IsGroupChat(const std::string&) const override { return false; }
  std::string GetSelfId() const override { return "self"; }
  bool SetupProfile(const std::string&, std::string&) override { return false; }
  bool LoadProfile(const std::string&, const std::string&) override { return false; }
  bool CloseProfile() override { return true; }
  bool Login() override { return true; }
  bool Logout() override { return true; }
  void SendRequest(std::shared_ptr<RequestMessage> request) override
  {
    if (request->GetMessageType() == SendMessageRequestType)
    {
      auto send = std::static_pointer_cast<SendMessageRequest>(request);
      sent.push_back({id, send->chatId, send->chatMessage.text});
    }
  }
  void SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>&) override {}
};
static std::wstring Line(int y, int x = 0, int width = -1)
{
  std::wstring result;
  if (width < 0) width = COLS - x;
  for (int col = x; col < x + width; ++col)
  {
    cchar_t cell;
    wchar_t chars[CCHARW_MAX];
    attr_t attr;
    short pair;
    mvwin_wch(curscr, y, col, &cell);
    getcchar(&cell, chars, &attr, &pair, nullptr);
    result += chars[0];
    if (wcwidth(chars[0]) == 2) ++col;
  }
  return result;
}
static void Key(UiModel& model, const std::string& name) { model.KeyHandler(UiKeyConfig::GetKey(name)); model.Draw(); }
static void Type(UiModel& model, const std::string& text) { for (unsigned char c : text) model.KeyHandler(c); model.Draw(); }
static void Select(UiModel& model, const std::wstring& name)
{
  for (int n = 0; n < 10; ++n)
  {
    model.Draw();
    if (Line(1, 23).find(name) != std::wstring::npos) return;
    Key(model, "next_chat");
  }
  throw std::runtime_error("cannot select " + StrUtil::ToString(name));
}
static void Seed(UiModel& model, const std::string& profile, const std::string& name, int64_t time)
{
  model.AddProtocol(std::make_shared<Fake>(profile));
  auto contacts = std::make_shared<NewContactsNotify>(profile);
  contacts->contactInfos.push_back({"same-chat-id", name, "15551234567"});
  model.MessageHandler(contacts);
  auto chats = std::make_shared<NewChatsNotify>(profile);
  chats->success = true;
  ChatInfo chat;
  chat.id = "same-chat-id";
  chat.lastMessageTime = time;
  chats->chatInfos.push_back(chat);
  model.MessageHandler(chats);
}
static void Resize(UiModel& model, int width, int height = 40)
{
  resizeterm(height, width);
  model.ReinitView();
  model.Draw();
}
static void CheckSidebar()
{
  for (int y = 0; y < LINES; ++y)
    Check((mvwinch(curscr, y, 22) & A_CHARTEXT) == (ACS_VLINE & A_CHARTEXT), "full-height sidebar separator");
  Check(Line(0, 0, 22).find(L"Chats") != std::wstring::npos, "sidebar title");
  Check(Line(LINES - 1, 0, 22).find_first_not_of(L" ") == std::wstring::npos, "help stays below conversation only");
}
static void CheckSend(size_t count, const std::string& profile, const std::string& text)
{
  Check(sent.size() == count, "one send request per nonempty draft");
  Check(sent.back().profile == profile && sent.back().chat == "same-chat-id" && sent.back().text == text,
        "send uses focused pane's account, chat and draft");
}
static void Snapshot(const std::string& name)
{
  std::ofstream out(s_CheckDir + "/" + name + ".txt");
  for (int y = 0; y < LINES; ++y) out << StrUtil::ToString(Line(y)) << '\n';
}
int main(int argc, char** argv)
{
  if (argc != 2)
  {
    std::cerr << "Usage: test_ui_panes <scratch-directory>\n";
    return 2;
  }
  s_CheckDir = argv[1];
  setlocale(LC_ALL, "C.UTF-8");
  const std::string dir = s_CheckDir + "/config";
  FileUtil::MkDir(dir);
  FileUtil::SetApplicationDir(dir);
  FileUtil::RmFile(dir + "/tabs.txt");
  AppConfig::Init();
  UiConfig::Init();
  UiConfig::SetNum("list_width", 22);
  UiConfig::SetStr("pinned_only_profiles", "");
  UiConfig::SetBool("protocol_labels", true);
  FILE* output = fopen("/dev/null", "w");
  FILE* input = fopen("/dev/null", "r");
  SCREEN* screen = newterm("xterm-256color", output, input);
  keypad(stdscr, true);
  resizeterm(40, 120);
  UiKeyConfig::Init(true);
  UiColorConfig::Init();
  try
  {
    UiModel model;
    model.Init();
    Seed(model, "Signal_test", "Alice", 300);
    Seed(model, "WhatsAppMd_test", "Bob", 200);
    Seed(model, "Instagram_test", "Carol", 100);
    Select(model, L"Alice");
    Type(model, "alice draft");
    CheckSidebar();
    Check(Line(34, 23).find(L"To Alice") != std::wstring::npos && Line(35, 23).find(L"alice draft") == 0, "single draft directly under conversation");
    Key(model, "toggle_tab");
    Select(model, L"Bob");
    Type(model, "bob draft");
    Key(model, "toggle_tab");
    Select(model, L"Alice");
    Key(model, "toggle_split");
    Check(Line(1, 23, 48).find(L"Bob") != std::wstring::npos && Line(1, 72, 48).find(L"Alice") != std::wstring::npos, "two distinct conversations");
    Check(Line(34, 23, 48).find(L"To Bob") != std::wstring::npos && Line(34, 72, 48).find(L"Draft for Alice") != std::wstring::npos, "recipient-labelled input fields");
    Check(Line(35, 23, 48).find(L"bob draft") == 0 && Line(35, 72, 48).find(L"alice draft") == 0, "independent drafts below their chats");
    Snapshot("split-before-focus");
    Key(model, "send_msg");
    CheckSend(1, "WhatsAppMd_test", "bob draft");
    Check(Line(35, 72, 48).find(L"alice draft") == 0, "sending left draft preserves right draft");
    Type(model, "bob second");
    ungetch(KEY_F(6));
    wint_t focusKey = 0;
    Check(UiKeyInput::GetWch(&focusKey) == KEY_CODE_YES, "read F6 through ncurses");
    Check(focusKey == (wint_t)UiKeyConfig::GetKey("focus_pane"), "F6 mapped correctly");
    model.KeyHandler(focusKey);
    model.Draw();
    Check(Line(1, 23, 48).find(L"Bob") != std::wstring::npos && Line(1, 72, 48).find(L"Alice") != std::wstring::npos, "focus does not move chats");
    Check(Line(34, 72, 48).find(L"To Alice") != std::wstring::npos && Line(35, 23, 48).find(L"bob second") == 0, "focus changes label, preserves drafts");
    int cy, cx;
    model.Draw();
    getyx(curscr, cy, cx);
    Check(cy == 35 && cx >= 72, "cursor in focused right input");
    Snapshot("split-after-focus");
    Key(model, "send_msg");
    CheckSend(2, "Signal_test", "alice draft");
    Check(Line(35, 23, 48).find(L"bob second") == 0, "sending right preserves left draft");
    Key(model, "send_msg");
    Check(sent.size() == 2, "empty focused field never sends other pane's pending draft");
    Key(model, "tab_2");
    Check(Line(34, 23, 48).find(L"To Bob") != std::wstring::npos && Line(34, 72, 48).find(L"Draft for Alice") != std::wstring::npos, "tab selects existing pane without duplicate chat");
    Key(model, "focus_pane");
    const std::string longDraft(75, 'z');
    Type(model, longDraft);
    Resize(model, 80);
    getyx(curscr, cy, cx);
    Check(cx >= 52, "focused draft wraps within right pane after resize");
    Resize(model, 40);
    Check(Line(1).find(L"Alice") != std::wstring::npos, "narrow fallback keeps focused chat");
    Key(model, "focus_pane");
    Check(Line(1).find(L"Alice") != std::wstring::npos, "cannot focus a hidden pane");
    Type(model, "!");
    Resize(model, 120);
    Check(Line(34, 72, 48).find(L"To Alice") != std::wstring::npos && Line(35, 23, 48).find(L"bob second") == 0, "resize restores pane slots and independent drafts");
    Key(model, "send_msg");
    CheckSend(3, "Signal_test", longDraft + "!");
    Key(model, "toggle_split");
    Check(Line(34, 23).find(L"To Alice") != std::wstring::npos, "closing split retains focused recipient");
    Select(model, L"Bob");
    Check(Line(35, 23).find(L"bob second") == 0, "draft survives split close");
    Key(model, "toggle_split");
    Check(Line(1, 23, 48).find(L"Alice") != std::wstring::npos && Line(1, 72, 48).find(L"Bob") != std::wstring::npos, "reopen split keeps drafts associated with chats");
    Key(model, "swap_split");
    Check(Line(1, 23, 48).find(L"Bob") != std::wstring::npos && Line(1, 72, 48).find(L"Alice") != std::wstring::npos && Line(35, 23, 48).find(L"bob second") == 0, "swap moves drafts with recipients");
    Key(model, "send_msg");
    CheckSend(4, "WhatsAppMd_test", "bob second");
    Resize(model, 29, 11);
    Check(Line(5).find(L"To Bob") != std::wstring::npos, "minimum-size terminal input is valid");
    Resize(model, 120);
    Key(model, "toggle_list");
    Check(Line(0).find(L"Chats") == std::wstring::npos && Line(34).find(L"To Bob") != std::wstring::npos, "hide sidebar expands input with conversation");
    Key(model, "toggle_list");
    CheckSidebar();
    auto removed = std::make_shared<DeleteChatNotify>("Signal_test");
    removed->success = true;
    removed->chatId = "same-chat-id";
    model.MessageHandler(removed);
    model.Draw();
    Check(Line(34).find(L"Draft for Alice") == std::wstring::npos && Line(34).find(L"To Bob") != std::wstring::npos, "deleting inactive chat removes its composer safely");
    Check(UiHelpView::GetKeyDisplay("focus_pane") == "F6", "function-key help label");
    model.Cleanup();
    std::cout << "PASS: full-height sidebar, per-pane drafts, F6 focus, actual send routing for both accounts, empty draft safety, tab selection, swap, narrow resize, close/reopen, hidden sidebar and deletion\n";
  }
  catch (const std::exception& error)
  {
    Snapshot("panes-failure");
    endwin();
    std::cerr << "FAIL: " << error.what() << "\n";
    return 1;
  }
  endwin();
  delscreen(screen);
  return 0;
}
