// uistatusview.cpp
//
// Copyright (c) 2019-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uistatusview.h"

#include <algorithm>

#include "apputil.h"
#include "strutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
#include "uihelpview.h"
#include "uimodel.h"

UiStatusView::UiStatusView(const UiViewParams& p_Params, bool p_IsSplit)
  : UiViewBase(p_Params)
  , m_IsSplit(p_IsSplit)
{
}

void UiStatusView::Draw()
{
  if (!m_Enabled || !m_Dirty) return;
  m_Dirty = false;

  curs_set(0);

  const auto currentChat = m_IsSplit ? m_Model->GetSplitChatLocked() : m_Model->GetCurrentChatLocked();
  int statusVPad = 1;
  static int colorPair = UiColorConfig::GetColorPair("status_color");
  static int attribute = UiColorConfig::GetAttribute("status_attr");

  werase(m_Win);
  wbkgd(m_Win, attribute | colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  std::wstring text;
  if (!currentChat.first.empty() || !currentChat.second.empty())
  {
    std::wstring wextra = StrUtil::ToWString(m_Model->GetChatStatusLocked(currentChat.first, currentChat.second));
    const auto activity = m_Model->GetChatActivityLocked(currentChat.first, currentChat.second);
    if (!activity.label.empty()) wextra = StrUtil::ToWString(activity.label) + wextra;

    static const bool developerMode = AppUtil::GetDeveloperMode();
    if (developerMode)
    {
      wextra = wextra + L" chat " + StrUtil::ToWString(currentChat.second);
      int64_t lastMessageTime = m_Model->GetLastMessageTimeLocked(currentChat.first, currentChat.second);
      wextra = wextra + L" time " + StrUtil::ToWString(std::to_string(lastMessageTime));
      std::string phone = m_Model->GetContactPhoneLocked(currentChat.first, currentChat.second);
      if (!phone.empty())
      {
        wextra = wextra + L" phone " + StrUtil::ToWString(phone);
      }
    }

    text = std::wstring(statusVPad, L' ') + wextra;
  }

  // send / reply / save hint, right-aligned (view filter is shown above the chat list)
  std::wstring whint;
  static const std::string sendKey = UiHelpView::GetKeyDisplay("send_msg");
  if (!sendKey.empty() && !(currentChat.first.empty() && currentChat.second.empty()))
  {
    const char* action = m_Model->GetEditMessageActiveLocked() ? "Save"
      : (m_Model->GetSelectMessageActiveLocked() ? "Reply" : "Send");
    whint = StrUtil::ToWString(m_IsSplit ? (UiHelpView::GetKeyDisplay("focus_pane") + " Focus") :
                                  (sendKey + " " + action)) + L" ";
    if ((StrUtil::WStringWidth(whint) + 4) >= m_W) whint.clear();
  }

  const int hintX = std::max(0, m_W - StrUtil::WStringWidth(whint));
  if (StrUtil::WStringWidth(text) > hintX)
  {
    text = StrUtil::TrimPadWString(text, std::max(0, hintX - 1));
  }
  mvwaddnwstr(m_Win, 0, 0, text.c_str(), text.size());
  if (!whint.empty()) mvwaddnwstr(m_Win, 0, hintX, whint.c_str(), whint.size());

  wattroff(m_Win, attribute | colorPair);
  wrefresh(m_Win);
}
