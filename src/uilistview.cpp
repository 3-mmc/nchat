// uilistview.cpp
//
// Copyright (c) 2019-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uilistview.h"

#include "strutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
#include "uimodel.h"

UiListView::UiListView(const UiViewParams& p_Params)
  : UiViewBase(p_Params)
{
  if (m_Enabled)
  {
    int paddedY = m_Y + 1;
    int paddedX = m_X + 1;
    m_PaddedH = m_H - 2;
    m_PaddedW = m_W - 2;
    m_PaddedWin = newwin(m_PaddedH, m_PaddedW, paddedY, paddedX);

    static int colorPair = UiColorConfig::GetColorPair("list_color");
    static int attribute = UiColorConfig::GetAttribute("list_attr");

    werase(m_Win);
    wbkgd(m_Win, attribute | colorPair | ' ');
    wrefresh(m_Win);
  }
}

UiListView::~UiListView()
{
  if (m_PaddedWin != nullptr)
  {
    delwin(m_PaddedWin);
    m_PaddedWin = nullptr;
  }
}

void UiListView::Draw()
{
  if (!m_Enabled || !m_Dirty) return;
  m_Dirty = false;

  curs_set(0);

  static int colorPair = UiColorConfig::GetColorPair("list_color");
  static int attribute = UiColorConfig::GetAttribute("list_attr");
  static int attributeSelected = UiColorConfig::GetAttribute("list_attr_selected");
  static int colorPairUnread = UiColorConfig::GetColorPair("list_color_unread");

  int index = std::max(0, m_Model->GetCurrentChatIndexLocked());
  const std::vector<std::pair<std::string, std::string>>& p_ChatVec = m_Model->GetChatVecLocked();

  const bool emojiEnabled = m_Model->GetEmojiEnabledLocked();
  const bool showProtocol = UiConfig::GetBool("protocol_labels") && m_Model->IsMultipleProfilesLocked() &&
    (m_PaddedW > 4);
  std::vector<std::string> names;
  std::vector<bool> unreads;
  for (auto& chatPair : p_ChatVec)
  {
    const std::string& name = m_Model->GetContactListNameLocked(chatPair.first, chatPair.second, true /*p_AllowId*/,
                                                                true /*p_AllowAlias*/);
    bool isUnread = m_Model->GetChatIsUnreadLocked(chatPair.first, chatPair.second);
    names.push_back(name);
    unreads.push_back(isUnread);
  }

  werase(m_PaddedWin);
  wbkgd(m_PaddedWin, attribute | colorPair | ' ');

  // header showing the active list view filter (Alt-f / Alt-o), list starts below it
  int top = 0;
  std::string viewLabel = m_Model->GetListViewLabelLocked();
  if (!viewLabel.empty() && (m_PaddedH > 1))
  {
    viewLabel = viewLabel.substr(1, viewLabel.size() - 2); // strip brackets
    std::wstring wheader = StrUtil::ToWString("\xe2\x96\xb8 " + viewLabel); // ▸
    wheader = StrUtil::TrimPadWString(wheader, m_PaddedW);
    wattron(m_PaddedWin, A_REVERSE | A_BOLD | colorPair);
    mvwaddnwstr(m_PaddedWin, 0, 0, wheader.c_str(), wheader.size());
    wattroff(m_PaddedWin, A_REVERSE | A_BOLD | colorPair);
    top = 1;
  }

  wattron(m_PaddedWin, attribute | colorPair);

  if (!names.empty())
  {
    int height = m_PaddedH - top;
    int count = names.size();
    int offset = std::min(std::max(0, index - ((height - 1) / 2)), std::max(0, count - height));
    int last = std::min((height + offset), count);
    for (int i = offset; i < last; ++i)
    {
      if (i == index)
      {
        wattroff(m_PaddedWin, attribute);
        wattron(m_PaddedWin, attributeSelected);
      }

      int y = i - offset + top;
      std::string name = names[i];
      if (!emojiEnabled)
      {
        name = StrUtil::Textize(name);
      }

      // coloured account marker in front of each chat when several accounts are set up
      const int markerW = showProtocol ? 1 : 0;
      if (showProtocol)
      {
        const int protocolPair = UiColorConfig::GetProtocolColorPair(p_ChatVec[i].first);
        wattroff(m_PaddedWin, attribute | attributeSelected | colorPair);
        wattron(m_PaddedWin, protocolPair);
        mvwaddnwstr(m_PaddedWin, y, 0, L"\u258e", 1); // ▎
        wattroff(m_PaddedWin, protocolPair);
        wattron(m_PaddedWin, colorPair | ((i == index) ? attributeSelected : attribute));
      }

      std::wstring wname = StrUtil::ToWString(name).substr(0, m_PaddedW - markerW);
      wname = StrUtil::TrimPadWString(wname, m_PaddedW - markerW);

      if (unreads[i])
      {
        wattron(m_PaddedWin, colorPairUnread);
      }

      mvwaddnwstr(m_PaddedWin, y, markerW, wname.c_str(), wname.size());

      if (unreads[i])
      {
        static const std::string unreadIndicator = " " + UiConfig::GetStr("unread_indicator");
        static const std::wstring wunread = StrUtil::ToWString(unreadIndicator);
        mvwaddnwstr(m_PaddedWin, y, (m_PaddedW - StrUtil::WStringWidth(wunread)), wunread.c_str(), wunread.size());

        wattron(m_PaddedWin, colorPair);
      }

      if (i == index)
      {
        wattroff(m_PaddedWin, attributeSelected);
        wattron(m_PaddedWin, attribute);
      }
    }
  }

  wattroff(m_PaddedWin, attribute | colorPair);
  wrefresh(m_PaddedWin);
}
