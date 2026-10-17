// uistatusview.h
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include "uiviewbase.h"

class UiStatusView : public UiViewBase
{
public:
  UiStatusView(const UiViewParams& p_Params, bool p_IsSplit = false);

  virtual void Draw();

private:
  bool m_IsSplit = false;
};
