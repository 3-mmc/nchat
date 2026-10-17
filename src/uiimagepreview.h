// uiimagepreview.h
//
// Inline image previews in the history view, rendered as configurable symbols
// by chafa and mapped onto the terminal's 256-colour palette.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>
#include "uipixelgraphics.h"
#include <vector>

#include <ncurses.h>

struct UiImageCell
{
  wchar_t ch = L' ';
  int pair = 0;
  attr_t attr = A_NORMAL;
};

typedef std::vector<std::vector<UiImageCell>> UiImageRows;

class UiImagePreview
{
public:
  static bool IsEnabled();
  static bool IsPreviewable(const std::string& p_FilePath);
  static const UiImageRows& GetRows(const std::string& p_FilePath, int p_MaxW, int p_MaxH = 0);
  static std::shared_ptr<const UiPixelImage> GetPixels(const std::string& p_FilePath, int p_MaxW, int p_MaxH = 0,
                                                     const void* p_Owner = nullptr);
  static void DrawRow(WINDOW* p_Win, int p_Y, const std::vector<UiImageCell>& p_Row, int p_MaxW);

private:
  static bool IsVideo(const std::string& p_FilePath);
  static std::string GetVideoThumbnail(const std::string& p_FilePath);
  static UiImageRows Render(const std::string& p_FilePath, int p_MaxW, int p_MaxH);
  static int GetColorIndex(int p_R, int p_G, int p_B);
  static int GetPair(int p_Fg, int p_Bg);
};
