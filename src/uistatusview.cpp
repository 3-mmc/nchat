// uistatusview.cpp
//
// Copyright (c) 2019-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uistatusview.h"

#include <vector>

#include "apputil.h"
#include "strutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
#include "uihelpview.h"
#include "uimodel.h"

UiStatusView::UiStatusView(const UiViewParams& p_Params)
  : UiViewBase(p_Params)
{
}

void UiStatusView::Draw()
{
  if (!m_Enabled || !m_Dirty) return;
  m_Dirty = false;

  curs_set(0);

  std::pair<std::string, std::string>& currentChat = m_Model->GetCurrentChatLocked();
  std::string name = m_Model->GetContactListNameLocked(currentChat.first, currentChat.second, true /*p_AllowId*/,
                                                       true /*p_AllowAlias*/);
  if (!m_Model->GetEmojiEnabledLocked())
  {
    name = StrUtil::Textize(name);
  }

  int statusVPad = 1;
  static int colorPair = UiColorConfig::GetColorPair("status_color");
  static int attribute = UiColorConfig::GetAttribute("status_attr");

  werase(m_Win);
  wbkgd(m_Win, attribute | colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  // segments with their own attributes: name, account label, handle, chat status, send hint
  struct Segment
  {
    std::wstring text;
    attr_t attr;
  };
  std::vector<Segment> segments;
  const attr_t baseAttr = attribute | colorPair;

  if (currentChat.first.empty() && currentChat.second.empty())
  {
    // Empty status bar until current chat is set
  }
  else
  {
    segments.push_back({ std::wstring(statusVPad, ' ') + StrUtil::ToWString(name).substr(0, m_W / 2), baseAttr });

    const bool protocolLabels = UiConfig::GetBool("protocol_labels");
    if (protocolLabels && m_Model->IsMultipleProfilesLocked())
    {
      // coloured account chip (the bar's reverse attribute turns the protocol colour into background)
      const int protocolPair = UiColorConfig::GetProtocolColorPair(currentChat.first);
      segments.push_back({ L" ", baseAttr });
      segments.push_back({ L" " + StrUtil::ToWString(m_Model->GetProtocolLabelLocked(currentChat.first)) + L" ",
                           (protocolPair != 0) ? (A_REVERSE | A_BOLD | protocolPair) : (baseAttr | A_BOLD) });
    }
    else
    {
      segments.push_back({ StrUtil::ToWString(m_Model->GetProfileSuffixLocked(currentChat.first)), baseAttr });
    }

    if (UiConfig::GetBool("show_contact_handle"))
    {
      const std::string handle = m_Model->GetContactHandleLocked(currentChat.first, currentChat.second);
      if (!handle.empty())
      {
        segments.push_back({ L"  " + StrUtil::ToWString(handle), baseAttr | A_ITALIC });
      }
    }

    std::wstring wextra = StrUtil::ToWString(m_Model->GetChatStatusLocked(currentChat.first, currentChat.second));

    static const std::string phoneNumberIndicator = UiConfig::GetStr("phone_number_indicator");
    if (!phoneNumberIndicator.empty())
    {
      static std::string placeholder = "%1";
      static const bool isDynamicIndicator = (phoneNumberIndicator.find(placeholder) != std::string::npos);
      if (isDynamicIndicator)
      {
        std::string dynamicIndicator = phoneNumberIndicator;
        std::string phone = m_Model->GetContactPhoneLocked(currentChat.first, currentChat.second);
        StrUtil::ReplaceString(dynamicIndicator, placeholder, phone);
        wextra += L" " + StrUtil::ToWString(dynamicIndicator);
      }
      else
      {
        wextra += L" " + StrUtil::ToWString(phoneNumberIndicator);
      }
    }

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

    segments.push_back({ wextra, baseAttr });
  }

  // send / reply / save hint, right-aligned (view filter is shown above the chat list)
  std::wstring whint;
  static const std::string sendKey = UiHelpView::GetKeyDisplay("send_msg");
  if (!sendKey.empty() && !(currentChat.first.empty() && currentChat.second.empty()))
  {
    const char* action = m_Model->GetEditMessageActiveLocked() ? "Save"
      : (m_Model->GetSelectMessageActiveLocked() ? "Reply" : "Send");
    whint = StrUtil::ToWString(sendKey + " " + action) + L" ";
    if ((StrUtil::WStringWidth(whint) + 4) >= m_W)
    {
      whint.clear();
    }
  }

  const int maxX = m_W - StrUtil::WStringWidth(whint);
  int x = 0;
  for (const auto& segment : segments)
  {
    if (x >= maxX) break;

    std::wstring wtext = segment.text;
    if ((x + StrUtil::WStringWidth(wtext)) > maxX)
    {
      wtext = StrUtil::TrimPadWString(wtext, maxX - x);
    }

    wattroff(m_Win, baseAttr);
    wattron(m_Win, segment.attr);
    mvwaddnwstr(m_Win, 0, x, wtext.c_str(), wtext.size());
    wattroff(m_Win, segment.attr);
    wattron(m_Win, baseAttr);
    x += StrUtil::WStringWidth(wtext);
  }

  if (!whint.empty())
  {
    mvwaddnwstr(m_Win, 0, maxX, whint.c_str(), whint.size());
  }

  wattroff(m_Win, attribute | colorPair);
  wrefresh(m_Win);
}
