// Optional Sixel overlays kept separate from ncurses' character canvas.
// nchat is distributed under the MIT license, see LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct UiPixelImage
{
  std::string data;
  int columns = 0;
  int rows = 0;
  int pixelWidth = 0;
  int pixelHeight = 0;
};
struct UiPixelPlacement
{
  std::shared_ptr<const UiPixelImage> image;
  int x = 0;
  int y = 0;
};
class UiPixelGraphics
{
public:
  static bool Selected();
  static std::string FallbackReason(const void* p_Owner = nullptr);
  static std::shared_ptr<const UiPixelImage> GetImage(const std::string& p_Path, int p_MaxW, int p_MaxH,
                                                      const void* p_Owner = nullptr);
  static void Show(const void* p_Owner, const std::vector<UiPixelPlacement>& p_Placements);
  static bool Clear(const void* p_Owner);
  static void ClearAll();
  static void BeginDialog(const void* p_Owner);
  static void EndDialog(const void* p_Owner);
  static uint64_t Revision();
};
