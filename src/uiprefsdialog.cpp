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
#include "uicolorconfig.h"
#include "uiimagepreview.h"
#include "uipreviewsample.h"
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
  choice("Preview renderer", "inline_image_backend", false,
         { { "symbols", "characters (portable)" }, { "sixel", "Sixel pixels (experimental)" } });
  choice("Preview style", "inline_image_style", false,
         { { "blocks", "blocks (smooth colour)" }, { "ascii", "ASCII (text art)" }, { "braille", "Braille (2x4 dots)" },
           { "half", "half blocks (1x2)" }, { "quadrants", "quadrants (2x2)" }, { "sextants", "sextants (2x3, font needed)" },
           { "stipple", "stipple (shading)" }, { "mixed", "mixed (experimental)" } });
  choice("Preview colours", "inline_image_color", false,
         { { "color", "foreground + background" }, { "foreground", "foreground / terminal bg" }, { "mono", "monochrome / text art" } });
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
  choice("Number / @handle in chat header", "show_contact_handle", false, s_OnOff);
  choice("Account colours and labels", "protocol_labels", false, s_OnOff);
  choice("Open written-in chats as tabs", "tabs_enabled", false, s_OnOff);
  choice("Archive chats", "archive_local", false, { { "1", "locally only" }, { "0", "on the service" } });
  choice("Pin chats", "pin_local", false, { { "1", "locally only" }, { "0", "on the service" } });
  choice("Desktop notifications", "desktop_notify_enabled", false, s_OnOff);
  choice("Reactions", "reactions_enabled", false, s_OnOff);
  choice("Activity animations", "activity_animations", false, s_OnOff);

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

  const int listWidth = GetListWidth();
  const int valueX = std::min(34, listWidth / 2);
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

    m_Items.push_back(StrUtil::TrimPadWString(StrUtil::ToWString(line), listWidth));
  }
}

int UiPrefsDialog::GetListWidth() const
{
  // Keep complete setting labels/values readable before adding the sample.
  return ((m_W >= 96) && (m_H >= 12)) ? (m_W * 3 / 5) : m_W;
}

void UiPrefsDialog::Draw()
{
  if (UiPixelGraphics::Clear(this)) redrawwin(m_Win);
  UpdateFooter();
  UiListDialog::Draw();
  const int listWidth = GetListWidth();
  if (listWidth < m_W) DrawPreview(listWidth);
}

void UiPrefsDialog::DrawPreview(int p_ListWidth)
{
  const int left = p_ListWidth + 3;
  const int width = m_W - left;
  const int maxRows = m_H - 9;
  const int pair = UiColorConfig::GetColorPair("dialog_color");
  wattrset(m_Win, pair);
  mvwvline(m_Win, 0, p_ListWidth + 1, ACS_VLINE, m_H);
  auto text = [&](int y, const std::string& value)
  {
    if ((y < 0) || (y >= m_H)) return;
    const auto line = StrUtil::TrimPadWString(StrUtil::ToWString(value), width);
    mvwaddnwstr(m_Win, y, left, line.c_str(), line.size());
  };
  wattron(m_Win, A_BOLD);
  text(0, "Live sample");
  wattroff(m_Win, A_BOLD);
  const std::string style = UiConfig::GetStr("inline_image_style");
  const std::string mode = UiConfig::GetStr("inline_image_color");
  text(1, UiPixelGraphics::Selected() ? "Sixel pixels / native image colours" : (style + " / " + mode));
  std::shared_ptr<const UiPixelImage> pixels;

  const std::string path = FileUtil::GetTempDir() + "/preview-sample.png";
  if (!FileUtil::Exists(path))
  {
    FileUtil::MkDir(FileUtil::GetTempDir());
    FileUtil::WriteFile(path, std::string(reinterpret_cast<const char*>(UiPreviewSample), sizeof(UiPreviewSample)));
  }
  if (!UiPixelGraphics::Selected() && (mode != "mono") &&
      (!has_colors() || (COLORS < 256) || (COLOR_PAIRS <= 256)))
  {
    text(3, "Choose monochrome on this terminal.");
  }
  else
  {
    pixels = UiImagePreview::GetPixels(path, width, maxRows, this);
    if (!pixels)
    {
      const auto& rows = UiImagePreview::GetRows(path, width, maxRows);
      if (rows.empty()) text(3, "Sample unavailable; is chafa installed?");
      else
      {
        // A child window lets the shared renderer start at column zero.
        WINDOW* sample = derwin(m_Win, maxRows, width, 3, left);
        if (sample)
        {
          wattrset(sample, A_NORMAL);
          for (size_t y = 0; y < rows.size(); ++y) UiImagePreview::DrawRow(sample, y, rows[y], width);
          wnoutrefresh(sample);
          delwin(sample);
        }
      }
    }
  }
  wattrset(m_Win, pair);
  const int requested = UiConfig::GetNum("inline_image_lines");
  text(m_H - 5, std::to_string(requested) + " rows requested; sample fits panel");
  text(m_H - 4, "More rows show more detail.");
  text(m_H - 3, "Font size follows the terminal.");
  if (UiPixelGraphics::Selected())
  {
    const std::string reason = UiPixelGraphics::FallbackReason(this);
    text(m_H - 3, pixels ? "Enable Sixel in terminal settings." :
         (reason.empty() ? "Encoding failed; showing characters." : reason));
    text(m_H - 1, "Styles/colours affect characters only.");
  }
  else if (style == "ascii") text(m_H - 1, "Tip: try monochrome for text art.");
  else if ((style == "sextants") || (style == "braille")) text(m_H - 1, "Missing shapes? Try half blocks.");
  else text(m_H - 1, "Left/Right changes the setting.");
  wrefresh(m_Win);
  if (pixels) UiPixelGraphics::Show(this, {{pixels, m_X + left, m_Y + 3}});
}

void UiPrefsDialog::UpdateFooter()
{
  const std::string hint = (GetListWidth() == m_W) ? "  Enlarge for sample" : "";
  SetFooter("Enter/Right change  Left back  Esc/^C close" + hint);
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
  if (p_Key == 27) // bare Escape, independent of the configured cancel key
  {
    m_Result = false;
    m_Running = false;
    return;
  }

  // no filter: printable keys are commands here
  const bool isMoveUp = (p_Key == '-') || (p_Key == (wint_t)UiKeyConfig::GetOffsettedKeyCode(KEY_SR, true));
  const bool isMoveDown = (p_Key == '+') || (p_Key == (wint_t)UiKeyConfig::GetOffsettedKeyCode(KEY_SF, true));
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

  // Function keys use an offset above Unicode; IsValidTextKey also accepts
  // those values. Dispatch configured commands before rejecting text so that
  // arrows, paging, resize and custom printable navigation bindings work.
  static const std::vector<std::string> commands =
  {
    "cancel", "quit", "other_commands_help", "left", "right", "ok",
    "prev_page", "next_page", "down", "up", "end", "home",
    "terminal_focus_in", "terminal_focus_out", "terminal_resize",
  };
  for (const auto& command : commands)
  {
    if (p_Key != (wint_t)UiKeyConfig::GetKey(command)) continue;

    const int oldIndex = m_Index;
    UiListDialog::KeyHandler(p_Key);
    if (!m_PrefItems.empty() && (m_PrefItems.at(m_Index).type == ItemHeader))
    {
      int direction = (m_Index < oldIndex) ? -1 : 1;
      if (p_Key == (wint_t)UiKeyConfig::GetKey("home")) direction = 1;
      while (m_PrefItems.at(m_Index).type == ItemHeader)
      {
        const int next = m_Index + direction;
        if ((next < 0) || (next >= (int)m_PrefItems.size()))
        {
          direction = -direction;
        }
        m_Index += direction;
      }
      Draw();
    }
    return;
  }

  if (p_Key == ' ')
  {
    OnSelect();
    Draw();
    return;
  }
}
