// uiprefsdialog.cpp
//
// Preferences dialog: media display, chat display options and accounts
// (status, order, pinned-only), stored in ui.conf / app.conf.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uiprefsdialog.h"

#include <algorithm>

#include "appconfig.h"
#include "fileutil.h"
#include "strutil.h"
#include "timeutil.h"
#include "uiconfig.h"
#include "uikeyconfig.h"
#include "uimodel.h"
#include "uitextinputdialog.h"

static const std::vector<std::pair<std::string, std::string>> s_OnOff = { { "1", "on" }, { "0", "off" } };

UiPrefsDialog::UiPrefsDialog(const UiDialogParams& p_Params)
  : UiListDialog(p_Params, false /*p_ShadeHidden*/)
{
  BuildItems();
  UpdateList();
  m_Index = 1; // first option below the first header
}

UiPrefsDialog::~UiPrefsDialog()
{
}

void UiPrefsDialog::BuildItems()
{
  m_PrefItems.clear();

  auto header = [&](const std::string& p_Label)
  {
    PrefItem item;
    item.type = ItemHeader;
    item.label = p_Label;
    m_PrefItems.push_back(item);
  };

  auto choice = [&](const std::string& p_Label, const std::string& p_Param, bool p_IsAppConfig,
                    const std::vector<std::pair<std::string, std::string>>& p_Choices)
  {
    PrefItem item;
    item.type = ItemChoice;
    item.label = p_Label;
    item.param = p_Param;
    item.isAppConfig = p_IsAppConfig;
    item.choices = p_Choices;
    m_PrefItems.push_back(item);
  };

  header("Media");
  choice("Images and videos", "media_display", false,
         { { "preview", "inline preview" }, { "name", "file name" }, { "path", "full path" } });
  choice("Preview height", "inline_image_lines", false,
         { { "6", "6 lines" }, { "8", "8 lines" }, { "10", "10 lines" }, { "12", "12 lines" },
           { "16", "16 lines" }, { "20", "20 lines" }, { "24", "24 lines" } });
  choice("Voice notes", "inline_audio", false, { { "1", "play inline" }, { "0", "open externally" } });
  choice("Download attachments", "attachment_prefetch", true,
         { { "0", "on open / save" }, { "1", "when selected" }, { "2", "when received" } });
  {
    PrefItem item;
    item.type = ItemFolder;
    item.label = "Media folder (saved files)";
    item.param = "downloads_dir";
    item.isAppConfig = true;
    m_PrefItems.push_back(item);
  }

  header("Chats");
  choice("Number / @handle in status bar", "show_contact_handle", false, s_OnOff);
  choice("Account colours and labels", "protocol_labels", false, s_OnOff);
  choice("Open written-in chats as tabs", "tabs_enabled", false, s_OnOff);
  choice("Archive chats", "archive_local", false, { { "1", "locally only" }, { "0", "on the service" } });
  choice("Desktop notifications", "desktop_notify_enabled", false, s_OnOff);
  choice("Reactions", "reactions_enabled", false, s_OnOff);

  header("Accounts  (Enter: pinned only, +/-: move)");
  for (const auto& profileId : m_Model->GetOrderedProfileIds())
  {
    PrefItem item;
    item.type = ItemAccount;
    item.profileId = profileId;
    m_PrefItems.push_back(item);
  }
}

std::string UiPrefsDialog::GetValue(const PrefItem& p_Item)
{
  return p_Item.isAppConfig ? AppConfig::GetStr(p_Item.param) : UiConfig::GetStr(p_Item.param);
}

void UiPrefsDialog::SetValue(const PrefItem& p_Item, const std::string& p_Value)
{
  if (p_Item.isAppConfig)
  {
    AppConfig::SetStr(p_Item.param, p_Value);
  }
  else
  {
    UiConfig::SetStr(p_Item.param, p_Value);
  }
}

void UiPrefsDialog::UpdateList()
{
  m_Items.clear();
  m_LastStates.clear();

  const int valueX = std::min(34, m_W / 2);
  for (const auto& item : m_PrefItems)
  {
    std::string line;
    if (item.type == ItemHeader)
    {
      line = "\xe2\x94\x80\xe2\x94\x80 " + item.label + " "; // ──
      while (StrUtil::WStringWidth(StrUtil::ToWString(line)) < m_W)
      {
        line += "\xe2\x94\x80";
      }
    }
    else if (item.type == ItemChoice)
    {
      const std::string value = GetValue(item);
      std::string display = value;
      for (const auto& choice : item.choices)
      {
        if (choice.first == value)
        {
          display = choice.second;
          break;
        }
      }

      line = "   " + item.label;
      line = StrUtil::ToString(StrUtil::TrimPadWString(StrUtil::ToWString(line), valueX)) +
        "\xe2\x80\xb9 " + display + " \xe2\x80\xba"; // ‹ ›
    }
    else if (item.type == ItemFolder)
    {
      std::string value = GetValue(item);
      if (value.empty())
      {
        value = "(default: " + FileUtil::GetDownloadsDir() + ")";
      }

      line = "   " + item.label;
      line = StrUtil::ToString(StrUtil::TrimPadWString(StrUtil::ToWString(line), valueX)) + value;
    }
    else if (item.type == ItemAccount)
    {
      const std::string& profileId = item.profileId;
      const size_t sep = profileId.find('_');
      const std::string account = (sep != std::string::npos) ? profileId.substr(sep + 1) : "";
      const std::string state = m_Model->GetProfileConnState(profileId);
      m_LastStates += state;
      const std::string dot = (state == "connected") ? "\xe2\x97\x8f" : "\xe2\x97\x8b"; // ● ○
      const bool pinnedOnly = m_Model->IsProfilePinnedOnly(profileId);

      line = "   " + m_Model->GetProtocolLabel(profileId) + "  " + account;
      line = StrUtil::ToString(StrUtil::TrimPadWString(StrUtil::ToWString(line), valueX)) +
        dot + " " + state + (pinnedOnly ? "   pinned chats only" : "   all chats");
    }

    m_Items.push_back(StrUtil::TrimPadWString(StrUtil::ToWString(line), m_W));
  }
}

void UiPrefsDialog::UpdateFooter()
{
  SetFooter("Enter/\xe2\x86\x92 change  \xe2\x86\x90 back  Esc/^C close");
}

void UiPrefsDialog::Cycle(int p_Delta)
{
  if ((m_Index < 0) || (m_Index >= (int)m_PrefItems.size())) return;

  const PrefItem& item = m_PrefItems.at(m_Index);
  if (item.type == ItemChoice)
  {
    const std::string value = GetValue(item);
    int pos = 0;
    for (size_t i = 0; i < item.choices.size(); ++i)
    {
      if (item.choices.at(i).first == value)
      {
        pos = i;
        break;
      }
    }

    const int count = item.choices.size();
    pos = (((pos + p_Delta) % count) + count) % count;
    SetValue(item, item.choices.at(pos).first);
  }
  else if (item.type == ItemAccount)
  {
    m_Model->SetProfilePinnedOnly(item.profileId, !m_Model->IsProfilePinnedOnly(item.profileId));
  }
  else if (item.type == ItemFolder)
  {
    EditFolder(item);
  }

  UpdateList();
}

void UiPrefsDialog::EditFolder(const PrefItem& p_Item)
{
  UiDialogParams params(m_Model, "Media Folder", 0.6, 5);
  UiTextInputDialog dialog(params, "Path: ", GetValue(p_Item));
  if (dialog.Run())
  {
    std::string path = dialog.GetInput();
    StrUtil::Trim(path);
    if (!path.empty())
    {
      path = FileUtil::ExpandPath(path);
      if (!FileUtil::IsDir(path))
      {
        FileUtil::MkDir(path);
      }
    }

    SetValue(p_Item, path);
  }
}

void UiPrefsDialog::OnSelect()
{
  Cycle(1);
}

void UiPrefsDialog::OnBack()
{
  Cycle(-1);
}

bool UiPrefsDialog::OnTimer()
{
  // refresh account connection states
  const int64_t now = TimeUtil::GetCurrentTimeMSec();
  if ((now - m_LastStateCheck) < 1000) return false;

  m_LastStateCheck = now;
  std::string states;
  for (const auto& item : m_PrefItems)
  {
    if (item.type == ItemAccount)
    {
      states += m_Model->GetProfileConnState(item.profileId);
    }
  }

  if (states == m_LastStates) return false;

  UpdateList();
  return true;
}

void UiPrefsDialog::KeyHandler(wint_t p_Key)
{
  // no filter: printable keys are commands here
  const bool isMoveUp = (p_Key == '-') || (p_Key == KEY_SR);
  const bool isMoveDown = (p_Key == '+') || (p_Key == KEY_SF);
  if (isMoveUp || isMoveDown)
  {
    if ((m_Index >= 0) && (m_Index < (int)m_PrefItems.size()) && (m_PrefItems.at(m_Index).type == ItemAccount))
    {
      const std::string profileId = m_PrefItems.at(m_Index).profileId;
      m_Model->MoveAccount(profileId, isMoveUp ? -1 : 1);
      BuildItems();
      for (size_t i = 0; i < m_PrefItems.size(); ++i)
      {
        if (m_PrefItems.at(i).profileId == profileId)
        {
          m_Index = i;
        }
      }

      UpdateList();
      Draw();
    }

    return;
  }

  if (StrUtil::IsValidTextKey(p_Key) && (p_Key != ' ')) return;

  if (p_Key == ' ')
  {
    OnSelect();
    Draw();
    return;
  }

  UiListDialog::KeyHandler(p_Key);
}
