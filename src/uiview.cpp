// uiview.cpp
//
// Copyright (c) 2019-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uiview.h"

#include <algorithm>

#include "log.h"
#include "uiconfig.h"
#include "uientryview.h"
#include "uihelpview.h"
#include "uihistoryview.h"
#include "uikeyconfig.h"
#include "uilistborderview.h"
#include "uilistview.h"
#include "uiscreen.h"
#include "uistatusview.h"
#include "uitopview.h"
#include "uiviewbase.h"
#include "uimodel.h"

UiView::UiView(UiModel* p_UiModel)
  : m_UiModel(p_UiModel)
{
  m_EmojiEnabled = UiConfig::GetBool("emoji_enabled");
  m_HelpEnabled = UiConfig::GetBool("help_enabled");
  m_ListEnabled = UiConfig::GetBool("list_enabled");
  m_StatusEnabled = UiConfig::GetBool("status_enabled");
  m_TopEnabled = UiConfig::GetBool("top_enabled");
  m_ListWidth = UiConfig::GetNum("list_width");
  m_EntryHeight = std::max(1, UiConfig::GetNum("entry_height"));
}

UiView::~UiView()
{
  UiConfig::SetBool("emoji_enabled", m_EmojiEnabled);
  UiConfig::SetBool("help_enabled", m_HelpEnabled);
  UiConfig::SetBool("list_enabled", m_ListEnabled);
  UiConfig::SetBool("top_enabled", m_TopEnabled);
  UiConfig::SetNum("list_width", m_ListWidth);
}

void UiView::Init()
{
  m_UiScreen = std::make_shared<UiScreen>();

  const int screenW = m_UiScreen->W();
  const int screenH = m_UiScreen->H();
  const int listW = std::min(m_ListWidth, std::max(0, screenW - 21));
  const bool showList = m_ListEnabled && (listW >= 3);
  m_UiListView = std::make_shared<UiListView>(UiViewParams(0, 0, listW, screenH, showList, m_UiModel));
  m_UiListBorderView = std::make_shared<UiListBorderView>(
    UiViewParams(showList ? listW : 0, 0, 1, screenH, showList, m_UiModel));

  // Everything belonging to a conversation starts after the full-height sidebar.
  const int chatX = showList ? listW + 1 : 0;
  const int chatW = screenW - chatX;
  m_UiTopView = std::make_shared<UiTopView>(UiViewParams(chatX, 0, chatW, 1, m_TopEnabled, m_UiModel));
  m_UiHelpView = std::make_shared<UiHelpView>(
    UiViewParams(chatX, screenH - 1, chatW, 1, m_HelpEnabled, m_UiModel));

  const int topH = m_UiTopView->H();
  const int helpH = m_UiHelpView->H();
  const int statusH = m_StatusEnabled ? 1 : 0;
  // One recipient label plus the configured input height; leave room for history.
  const int entryH = std::min(m_EntryHeight + 1, std::max(2, screenH - topH - helpH - statusH - 3));
  const int entryY = screenH - helpH - entryH;
  const int statusY = entryY - statusH;
  const int historyH = statusY - topH;
  const bool split = m_UiModel->GetSplitEnabledLocked() && (chatW >= 41);
  const bool focusRight = split && m_UiModel->GetSplitFocusRightLocked();
  const int leftW = split ? (chatW - 1) / 2 : chatW;
  const int rightW = chatW - leftW - 1;
  const int rightX = chatX + leftW + 1;
  const int activeX = focusRight ? rightX : chatX;
  const int activeW = focusRight ? rightW : leftW;

  m_UiHistoryView = std::make_shared<UiHistoryView>(
    UiViewParams(activeX, topH, activeW, historyH, m_HistoryEnabled, m_UiModel),
    split ? UiHistoryView::PaneMain : UiHistoryView::PaneSingle);
  m_UiStatusView = std::make_shared<UiStatusView>(
    UiViewParams(activeX, statusY, activeW, statusH, m_StatusEnabled, m_UiModel));
  m_UiEntryView = std::make_shared<UiEntryView>(
    UiViewParams(activeX, entryY, activeW, entryH, m_EntryEnabled, m_UiModel));

  if (split)
  {
    const int otherX = focusRight ? chatX : rightX;
    const int otherW = focusRight ? leftW : rightW;
    m_UiSplitBorderView = std::make_shared<UiListBorderView>(
      UiViewParams(chatX + leftW, topH, 1, screenH - topH - helpH, true, m_UiModel));
    m_UiSplitHistoryView = std::make_shared<UiHistoryView>(
      UiViewParams(otherX, topH, otherW, historyH, m_HistoryEnabled, m_UiModel), UiHistoryView::PaneSplit);
    m_UiSplitStatusView = std::make_shared<UiStatusView>(
      UiViewParams(otherX, statusY, otherW, statusH, m_StatusEnabled, m_UiModel), true);
    m_UiSplitEntryView = std::make_shared<UiEntryView>(
      UiViewParams(otherX, entryY, otherW, entryH, m_EntryEnabled, m_UiModel), true);
  }
  else
  {
    m_UiSplitBorderView.reset();
    m_UiSplitHistoryView.reset();
    m_UiSplitStatusView.reset();
    m_UiSplitEntryView.reset();
  }
}

void UiView::Draw()
{
  m_UiTopView->Draw();
  m_UiHelpView->Draw();
  m_UiStatusView->Draw();
  m_UiListView->Draw();
  m_UiListBorderView->Draw();
  m_UiHistoryView->Draw();
  if (m_UiSplitHistoryView)
  {
    m_UiSplitBorderView->Draw();
    m_UiSplitHistoryView->Draw();
    m_UiSplitStatusView->Draw();
    m_UiSplitEntryView->Draw();
  }
  m_UiEntryView->Draw();
  curs_set(1);
}

void UiView::TerminalBell()
{
  LOG_DEBUG("bell");
  beep();
}

void UiView::SetEmojiEnabled(bool p_Enabled)
{
  m_EmojiEnabled = p_Enabled;
}

bool UiView::GetEmojiEnabled()
{
  return m_EmojiEnabled;
}

void UiView::SetTopEnabled(bool p_Enabled)
{
  m_TopEnabled = p_Enabled;
}

bool UiView::GetTopEnabled()
{
  return m_TopEnabled;
}

void UiView::SetHelpEnabled(bool p_Enabled)
{
  m_HelpEnabled = p_Enabled;
}

bool UiView::GetHelpEnabled()
{
  return m_HelpEnabled;
}

void UiView::SetListEnabled(bool p_Enabled)
{
  m_ListEnabled = p_Enabled;
}

bool UiView::GetListEnabled()
{
  return m_ListEnabled;
}

void UiView::SetListDirty(bool p_Dirty)
{
  m_UiListView->SetDirty(p_Dirty);
}

void UiView::SetStatusDirty(bool p_Dirty)
{
  m_UiStatusView->SetDirty(p_Dirty);
  if (m_UiSplitStatusView) m_UiSplitStatusView->SetDirty(p_Dirty);
}

void UiView::SetHistoryDirty(bool p_Dirty)
{
  m_UiHistoryView->SetDirty(p_Dirty);
  if (m_UiSplitHistoryView)
  {
    m_UiSplitHistoryView->SetDirty(p_Dirty);
  }
}

void UiView::SetTopDirty(bool p_Dirty)
{
  m_UiTopView->SetDirty(p_Dirty);
}

void UiView::SetHelpDirty(bool p_Dirty)
{
  m_UiHelpView->SetDirty(p_Dirty);
}

void UiView::SetEntryDirty(bool p_Dirty)
{
  m_UiEntryView->SetDirty(p_Dirty);
  if (m_UiSplitEntryView) m_UiSplitEntryView->SetDirty(p_Dirty);
}

int UiView::GetHistoryShowCount()
{
  return m_UiHistoryView->GetHistoryShowCount();
}

int UiView::GetHistoryLines()
{
  return m_UiHistoryView->H();
}

int UiView::GetEntryWidth()
{
  return m_UiEntryView->W();
}

int UiView::GetScreenWidth()
{
  return m_UiScreen->W();
}

int UiView::GetScreenHeight()
{
  return m_UiScreen->H();
}

void UiView::DecreaseListWidth()
{
  if (m_ListWidth > 0)
  {
    --m_ListWidth;
  }
}

void UiView::IncreaseListWidth()
{
  if (m_ListWidth < m_UiScreen->W())
  {
    ++m_ListWidth;
  }
}

bool UiView::HasSplitView()
{
  return m_UiSplitHistoryView != nullptr;
}
