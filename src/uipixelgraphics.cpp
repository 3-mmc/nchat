// nchat is distributed under the MIT license, see LICENSE for details.
#include "uipixelgraphics.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ncurses.h>
#include "strutil.h"
#include "uiconfig.h"

namespace
{
std::map<const void*, std::vector<UiPixelPlacement>> active;
std::vector<const void*> dialogs;
uint64_t revision = 0;

bool CellSize(int& width, int& height)
{
  struct winsize size = {};
  if (!isatty(STDOUT_FILENO) || ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0 ||
      !size.ws_col || !size.ws_row || !size.ws_xpixel || !size.ws_ypixel) return false;
  width = size.ws_xpixel / size.ws_col;
  height = size.ws_ypixel / size.ws_row;
  return width >= 1 && width <= 128 && height >= 1 && height <= 256;
}
void Write(const std::string& text)
{
  fwrite(text.data(), 1, text.size(), stdout);
  fflush(stdout);
}
std::string Position(int x, int y)
{
  return "\033[" + std::to_string(y + 1) + ";" + std::to_string(x + 1) + "H";
}
std::shared_ptr<const UiPixelImage> Encode(const std::string& path, int maxWidth, int maxHeight,
                                         int cellWidth, int cellHeight)
{
  // With probing off Chafa usually uses 10x20 pixel cells. Validate the actual
  // raster, shrinking and retrying if its local terminal metrics differ.
  int columns = std::max(1, maxWidth / 10);
  int rows = std::max(1, maxHeight / 20);
  for (int attempt = 0; attempt < 3; ++attempt)
  {
    const std::string size = std::to_string(columns) + "x" + std::to_string(rows);
    const std::string command = "chafa --format sixels --colors 256 --dither diffusion --animate off "
      "--probe off --polite on --passthrough none --relative off --scale max --exact-size off "
      "--font-ratio 1/2 --size " + size + " --view-size " + size + " -- '" +
      StrUtil::EscapeSingleQuote(path) + "' </dev/null 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return nullptr;
    std::string output;
    char buffer[8192];
    bool oversized = false;
    while (true)
    {
      const auto count = read(fileno(pipe), buffer, sizeof(buffer));
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) break;
      if (output.size() + count <= 8 * 1024 * 1024) output.append(buffer, count);
      else oversized = true; // drain the pipe before waiting on the child
    }
    if (pclose(pipe) != 0 || oversized) return nullptr;
    const size_t start = output.find("\033P");
    if (start == std::string::npos) return nullptr;
    const size_t end = output.find("\033\\", start);
    const size_t raster = output.find("q\"", start);
    if (end == std::string::npos || raster == std::string::npos || raster >= end) return nullptr;
    int aspectX, aspectY, width, height;
    if (sscanf(output.c_str() + raster + 2, "%d;%d;%d;%d", &aspectX, &aspectY, &width, &height) != 4 ||
        aspectX != 1 || aspectY != 1 || width <= 0 || height <= 0) return nullptr;
    if (width <= maxWidth && height <= maxHeight)
    {
      auto image = std::make_shared<UiPixelImage>();
      image->data = output.substr(start, end + 2 - start); // no cursor moves or mode changes from Chafa
      image->pixelWidth = width;
      image->pixelHeight = height;
      image->columns = (width + cellWidth - 1) / cellWidth;
      image->rows = (height + cellHeight - 1) / cellHeight;
      return image;
    }
    const int nextColumns = std::max(1, columns * maxWidth / width);
    const int nextRows = std::max(1, rows * maxHeight / height);
    if (nextColumns == columns && nextRows == rows) return nullptr;
    columns = nextColumns;
    rows = nextRows;
  }
  return nullptr;
}
}

bool UiPixelGraphics::Selected()
{
  return UiConfig::GetStr("inline_image_backend") == "sixel";
}
std::string UiPixelGraphics::FallbackReason(const void* p_Owner)
{
  if (!Selected()) return "Character renderer selected";
  if (!dialogs.empty() && dialogs.back() != p_Owner) return "Dialog covers chat images";
  const char* tmux = getenv("TMUX");
  const char* screen = getenv("STY");
  if ((tmux && *tmux) || (screen && *screen)) return "Characters used inside tmux/screen";
  int width, height;
  if (!CellSize(width, height)) return "Characters: terminal pixel size unavailable";
  return "";
}
std::shared_ptr<const UiPixelImage> UiPixelGraphics::GetImage(const std::string& p_Path, int p_MaxW, int p_MaxH,
                                                            const void* p_Owner)
{
  if (!FallbackReason(p_Owner).empty() || p_MaxW <= 0 || p_MaxH <= 0) return nullptr;
  int cellWidth, cellHeight;
  if (!CellSize(cellWidth, cellHeight)) return nullptr;
  p_MaxH = std::min(p_MaxH, 48);
  p_MaxW = std::min(p_MaxW, p_MaxH * 4);
  const int maxWidth = std::min(2048, p_MaxW * cellWidth);
  const int maxHeight = std::min(2048, p_MaxH * cellHeight);
  struct stat st = {};
  if (stat(p_Path.c_str(), &st) != 0) return nullptr;
  const std::string key = p_Path + "|" + std::to_string(st.st_mtime) + ":" + std::to_string(st.st_size) + "|" +
    std::to_string(maxWidth) + "x" + std::to_string(maxHeight) + ":" +
    std::to_string(cellWidth) + "x" + std::to_string(cellHeight);
  static std::map<std::string, std::shared_ptr<const UiPixelImage>> cache;
  static size_t bytes = 0;
  const auto cached = cache.find(key);
  if (cached != cache.end()) return cached->second;
  auto image = Encode(p_Path, maxWidth, maxHeight, cellWidth, cellHeight);
  if (!image) return nullptr;
  if (bytes + image->data.size() > 32 * 1024 * 1024 || cache.size() >= 128)
  {
    cache.clear();
    bytes = 0;
  }
  bytes += image->data.size();
  cache[key] = image;
  return image;
}
void UiPixelGraphics::Show(const void* p_Owner, const std::vector<UiPixelPlacement>& p_Placements)
{
  Clear(p_Owner);
  if (!FallbackReason(p_Owner).empty()) return;
  std::vector<UiPixelPlacement> visible;
  std::string output;
  for (const auto& place : p_Placements)
  {
    if (!place.image || place.x < 0 || place.y < 0 ||
        place.x + place.image->columns > COLS || place.y + place.image->rows >= LINES) continue;
    output += Position(place.x, place.y) + place.image->data;
    visible.push_back(place);
  }
  if (output.empty()) return;
  // Save/restore the cursor around raw graphics so ncurses' cursor stays valid.
  Write("\0337" + output + "\0338");
  active[p_Owner] = std::move(visible);
}
bool UiPixelGraphics::Clear(const void* p_Owner)
{
  const auto found = active.find(p_Owner);
  if (found == active.end()) return false;
  std::string output;
  for (const auto& place : found->second)
  {
    const int width = std::min(place.image->columns, COLS - place.x);
    if (width <= 0 || place.x < 0 || place.y < 0) continue;
    for (int y = place.y; y < std::min(LINES, place.y + place.image->rows); ++y)
      output += Position(place.x, y) + "\033[" + std::to_string(width) + "X";
  }
  active.erase(found);
  if (!output.empty() && isatty(STDOUT_FILENO)) Write("\0337" + output + "\0338");
  return true;
}
void UiPixelGraphics::ClearAll()
{
  while (!active.empty()) Clear(active.begin()->first);
  ++revision;
}
void UiPixelGraphics::BeginDialog(const void* p_Owner)
{
  ClearAll();
  dialogs.push_back(p_Owner);
}
void UiPixelGraphics::EndDialog(const void* p_Owner)
{
  Clear(p_Owner);
  dialogs.erase(std::remove(dialogs.begin(), dialogs.end(), p_Owner), dialogs.end());
  ++revision;
}
uint64_t UiPixelGraphics::Revision()
{
  return revision;
}
