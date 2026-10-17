// uihistoryview.h
//
// Copyright (c) 2019-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

#include "uiviewbase.h"

class UiHistoryView : public UiViewBase
{
public:
  enum Pane
  {
    PaneSingle = 0,
    PaneMain, // split view: focused conversation, on either side
    PaneSplit, // split view: other conversation
  };

  UiHistoryView(const UiViewParams& p_Params, Pane p_Pane = PaneSingle);
  virtual ~UiHistoryView();

  virtual void Draw();
  int GetHistoryShowCount();

private:
  std::string GetTimeString(int64_t p_TimeSent);
  void DrawPaneHeader(const std::pair<std::string, std::string>& p_Chat);

private:
  WINDOW* m_PaddedWin = nullptr;
  int m_PaddedH = 0;
  int m_PaddedW = 0;
  int m_HistoryShowCount = 0;
  Pane m_Pane = PaneSingle;
};
