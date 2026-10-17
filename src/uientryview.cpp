// uientryview.cpp
//
// Copyright (c) 2019-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uientryview.h"

#include <algorithm>

#include "strutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
#include "uihelpview.h"
#include "uimodel.h"

UiEntryView::UiEntryView(const UiViewParams& p_Params, bool p_IsSplit)
  : UiViewBase(p_Params)
  , m_IsSplit(p_IsSplit)
{
}

void UiEntryView::Draw()
{
  if (!m_Enabled) return;

  if (!m_Dirty)
  {
    if (!m_IsSplit)
    {
      wmove(m_Win, m_CursY, m_CursX);
      wrefresh(m_Win);
    }
    return;
  }

  m_Dirty = false;

  curs_set(0);

  std::wstring input = m_Model->GetEntryStrLocked(m_IsSplit);
  const int inputPos = m_Model->GetEntryPosLocked(m_IsSplit);
  std::wstring line;
  std::vector<std::wstring> lines;
  int cx = 0;
  int cy = 0;
  lines = StrUtil::WordWrap(input, m_W, false, false, false, 2, inputPos, cy, cx);

  static int colorPair = UiColorConfig::GetColorPair("entry_color");
  static int attribute = UiColorConfig::GetAttribute("entry_attr");

  werase(m_Win);
  wbkgd(m_Win, attribute | colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  const auto chat = m_IsSplit ? m_Model->GetSplitChatLocked() : m_Model->GetCurrentChatLocked();
  std::string name = m_Model->GetContactListNameLocked(chat.first, chat.second, true, true);
  if (!m_Model->GetEmojiEnabledLocked()) name = StrUtil::Textize(name);
  std::wstring label;
  if (chat.first.empty() && chat.second.empty()) label = L" No chat selected";
  else label = (m_IsSplit ? L" Draft for " : L"\u25b8 To ") + StrUtil::ToWString(name);
  if (m_IsSplit) label += StrUtil::ToWString("  [" + UiHelpView::GetKeyDisplay("focus_pane") + " focus]");
  label = StrUtil::TrimPadWString(label, m_W);
  wattrset(m_Win, attribute | colorPair | (m_IsSplit ? A_DIM : (A_BOLD | A_REVERSE)));
  mvwaddnwstr(m_Win, 0, 0, label.c_str(), label.size());
  wattrset(m_Win, attribute | colorPair | (m_IsSplit ? A_DIM : A_NORMAL));

  const int inputH = m_H - 1;
  int yoffs = (cy < inputH) ? 0 : (cy - inputH + 1);

  for (int i = 0; i < inputH; ++i)
  {
    if ((i + yoffs) < (int)lines.size())
    {
      line = lines.at(i + yoffs).c_str();
      line.erase(std::remove(line.begin(), line.end(), EMOJI_PAD), line.end());
      mvwaddwstr(m_Win, i + 1, 0, line.c_str());
    }
  }

  wattrset(m_Win, attribute | colorPair);

  m_CursX = std::max(0, std::min(m_W - 1, cx));
  m_CursY = 1 + (cy - yoffs);

  wmove(m_Win, m_CursY, m_CursX);
  wrefresh(m_Win);
}
