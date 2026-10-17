// uitopview.cpp
//
// Copyright (c) 2019-2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uitopview.h"

#include <algorithm>
#include <string>
#include <vector>

#include "appconfig.h"
#include "apputil.h"
#include "status.h"
#include "strutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
#include "uimodel.h"

UiTopView::UiTopView(const UiViewParams& p_Params)
  : UiViewBase(p_Params)
{
}

void UiTopView::Draw()
{
  static const bool awayStatusIndication = UiConfig::GetBool("away_status_indication");
  static const uint32_t fullMask = ~static_cast<uint32_t>(0);
  static const uint32_t statusMask = fullMask & (awayStatusIndication ? fullMask : ~Status::FlagAway);

  static uint32_t lastStatus = 0;
  uint32_t status = Status::Get(statusMask);
  m_Dirty |= (status != lastStatus);
  lastStatus = status;

  if (!m_Dirty) return;
  m_Dirty = false;

  m_Model->OnStatusUpdateLocked(status);

  if (!m_Enabled) return;

  curs_set(0);

  int topPadLeft = 1;
  int topPadRight = 1;
  static int colorPair = UiColorConfig::GetColorPair("top_color");
  static int attribute = UiColorConfig::GetAttribute("top_attr");

  werase(m_Win);
  wbkgd(m_Win, attribute | colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  // simple proxy config check
  static const std::string statusSuffixStr = []()
  {
    const std::string proxyHost = AppConfig::GetStr("proxy_host");
    const int proxyPort = AppConfig::GetNum("proxy_port");
    const bool proxyEnabled = (!proxyHost.empty() && (proxyPort != 0));
    if (proxyEnabled)
    {
      std::string proxyIndicator = UiConfig::GetStr("proxy_indicator");
      return " " + proxyIndicator;
    }

    return std::string("");
  }();

  static const bool topShowVersion = UiConfig::GetBool("top_show_version");
  static const std::string appNameVersion = AppUtil::GetAppName(topShowVersion);
  const std::string statusStr = Status::ToString(status) + statusSuffixStr;
  std::wstring topWStrLeft = StrUtil::ToWString(std::string(topPadLeft, ' ') + appNameVersion);
  std::wstring topWStrRight = StrUtil::ToWString(statusStr + std::string(topPadRight, ' '));
  int topStrLeftWidth = StrUtil::WStringWidth(topWStrLeft);
  int topStrRightWidth = StrUtil::WStringWidth(topWStrRight);

  std::wstring topWStr = topWStrLeft + std::wstring(std::max(0, m_W - topStrLeftWidth - topStrRightWidth), ' ') +
    topWStrRight;
  mvwaddnwstr(m_Win, 0, 0, topWStr.c_str(), topWStr.size());

  // tab bar: replaces the app name, "1 name" per tab, current tab raised (not reversed)
  const std::vector<std::pair<std::string, std::string>> tabs = m_Model->GetTabsLocked();
  if (!tabs.empty())
  {
    const std::pair<std::string, std::string>& currentChat = m_Model->GetCurrentChatLocked();
    const std::pair<std::string, std::string> splitChat = m_Model->GetSplitChatLocked();
    const bool splitEnabled = m_Model->GetSplitEnabledLocked();
    const bool showProtocol = m_Model->IsMultipleProfilesLocked() && UiConfig::GetBool("protocol_labels");
    const bool emojiEnabled = m_Model->GetEmojiEnabledLocked();
    const int maxX = m_W - topStrRightWidth - 1;
    const int maxNameW = std::max(4, std::min(16, (maxX / (int)tabs.size()) - 5));

    std::wstring blank(std::max(0, maxX), ' ');
    mvwaddnwstr(m_Win, 0, 0, blank.c_str(), blank.size());

    int x = 0;
    for (size_t i = 0; i < tabs.size(); ++i)
    {
      const std::pair<std::string, std::string>& tab = tabs.at(i);
      std::string name = m_Model->GetContactListNameLocked(tab.first, tab.second, true /*p_AllowId*/,
                                                           true /*p_AllowAlias*/);
      if (!emojiEnabled)
      {
        name = StrUtil::Textize(name);
      }

      std::wstring wname = StrUtil::ToWString(name);
      if (StrUtil::WStringWidth(wname) > maxNameW)
      {
        wname = StrUtil::TrimPadWString(wname, maxNameW - 1) + L"\u2026"; // …
      }

      const bool isCurrent = (tab == currentChat);
      const bool isSplit = splitEnabled && (tab == splitChat);
      const bool isUnread = !isCurrent && m_Model->GetChatIsUnreadLocked(tab.first, tab.second);
      const auto activity = m_Model->GetChatActivityLocked(tab.first, tab.second);
      const std::wstring activityMarker = activity.marker.empty() ? (isUnread ? L"\u2022" : L"") : activity.marker;
      const std::wstring wnum = std::to_wstring(i + 1);
      const std::wstring wtab = L" " + wnum + L" " + wname + activityMarker +
        (isSplit ? L" \u25e8" : L"") + L" "; // ◨
      const int tabW = StrUtil::WStringWidth(wtab) + (showProtocol ? 1 : 0);
      if ((x + tabW) > maxX) break;

      if (showProtocol)
      {
        // coloured account marker, drawn without the bar's reverse attribute
        const int protocolPair = UiColorConfig::GetProtocolColorPair(tab.first);
        wattroff(m_Win, attribute | colorPair);
        wattron(m_Win, protocolPair);
        mvwaddnwstr(m_Win, 0, x, L"\u258c", 1); // ▌
        wattroff(m_Win, protocolPair);
        wattron(m_Win, attribute | colorPair);
        ++x;
      }

      int tabAttr = attribute | colorPair;
      if (isCurrent)
      {
        tabAttr = (colorPair | A_BOLD) & ~A_REVERSE;
      }
      else if (isUnread)
      {
        tabAttr |= A_BOLD;
      }

      wattroff(m_Win, attribute | colorPair);
      wattron(m_Win, tabAttr);
      mvwaddnwstr(m_Win, 0, x, wtab.c_str(), wtab.size());
      wattroff(m_Win, tabAttr);
      wattron(m_Win, attribute | colorPair);
      x += StrUtil::WStringWidth(wtab);
    }
  }

  wattroff(m_Win, attribute | colorPair);
  wrefresh(m_Win);
}
