// uiimagepreview.cpp
//
// Inline image previews in the history view, rendered as colour block symbols
// by chafa and mapped onto the terminal's 256-colour palette.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uiimagepreview.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include <unistd.h>

#include "fileutil.h"
#include "log.h"
#include "strutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"

bool UiImagePreview::IsEnabled()
{
  // media_display can be changed at runtime from the preferences dialog
  static const bool hasColors = has_colors() && (COLORS >= 256);
  return hasColors && (UiConfig::GetStr("media_display") == "preview");
}

bool UiImagePreview::IsPreviewable(const std::string& p_FilePath)
{
  static const std::set<std::string> imageExts =
  {
    ".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".tif", ".tiff", ".avif", ".heic", ".jxl",
  };

  std::string ext = StrUtil::ToLower(FileUtil::GetFileExt(p_FilePath));
  return (imageExts.count(ext) || IsVideo(p_FilePath)) && FileUtil::Exists(p_FilePath);
}

bool UiImagePreview::IsVideo(const std::string& p_FilePath)
{
  static const std::set<std::string> videoExts =
  {
    ".mp4", ".m4v", ".mov", ".mkv", ".webm", ".3gp", ".f4v", ".avi",
  };

  return videoExts.count(StrUtil::ToLower(FileUtil::GetFileExt(p_FilePath)));
}

std::string UiImagePreview::GetVideoThumbnail(const std::string& p_FilePath)
{
  // first-frame thumbnail, cached next to nchat's other temp files
  static const std::string thumbDir = FileUtil::GetApplicationDir() + "/temp/thumbs";
  FileUtil::MkDir(thumbDir);
  const std::string thumbPath = thumbDir + "/" + FileUtil::BaseName(p_FilePath) + ".png";
  if (!FileUtil::Exists(thumbPath))
  {
    const std::string cmd = "ffmpegthumbnailer -s 512 -t 10% -i '" + StrUtil::EscapeSingleQuote(p_FilePath) +
      "' -o '" + StrUtil::EscapeSingleQuote(thumbPath) + "' </dev/null >/dev/null 2>&1";
    if ((system(cmd.c_str()) != 0) || !FileUtil::Exists(thumbPath))
    {
      LOG_WARNING("video thumbnail failed %s", p_FilePath.c_str());
      return std::string();
    }
  }

  return thumbPath;
}

const UiImageRows& UiImagePreview::GetRows(const std::string& p_FilePath, int p_MaxW)
{
  const int maxH = std::max(1, (int)UiConfig::GetNum("inline_image_lines"));
  static std::map<std::string, UiImageRows> cache;

  // palette entries redefined by hex colors may grow while drawing, so key on it too
  const std::string key = p_FilePath + "|" + std::to_string(p_MaxW) + "x" + std::to_string(maxH) + "|" +
    std::to_string(UiColorConfig::GetLastCustomColorId());
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;

  if (cache.size() > 256)
  {
    cache.clear();
  }

  return cache[key] = Render(p_FilePath, p_MaxW, maxH);
}

void UiImagePreview::DrawRow(WINDOW* p_Win, int p_Y, const std::vector<UiImageCell>& p_Row, int p_MaxW)
{
  int x = 0;
  for (const auto& cell : p_Row)
  {
    if (x >= p_MaxW) break;

    cchar_t cc;
    wchar_t wstr[2] = { cell.ch, L'\0' };
    int pair = cell.pair;
    setcchar(&cc, wstr, A_NORMAL, 0, &pair);
    mvwadd_wch(p_Win, p_Y, x++, &cc);
  }
}

UiImageRows UiImagePreview::Render(const std::string& p_FilePath, int p_MaxW, int p_MaxH)
{
  UiImageRows rows;
  const std::string imagePath = IsVideo(p_FilePath) ? GetVideoThumbnail(p_FilePath) : p_FilePath;
  if (imagePath.empty()) return rows;

  const int w = std::min(p_MaxW, p_MaxH * 4);
  const std::string cmd = "chafa -f symbols --colors full --symbols block+border+space --animate off "
    "--polite on -O 0 --size " + std::to_string(w) + "x" + std::to_string(p_MaxH) +
    " -- '" + StrUtil::EscapeSingleQuote(imagePath) + "' </dev/null 2>/dev/null";

  FILE* pipe = popen(cmd.c_str(), "r");
  if (pipe == nullptr)
  {
    LOG_WARNING("failed to run chafa");
    return rows;
  }

  // read() retrying on EINTR, as signals (e.g. SIGWINCH on resize) would end fread() early
  std::string out;
  char buf[4096];
  const int fd = fileno(pipe);
  while (true)
  {
    const ssize_t len = read(fd, buf, sizeof(buf));
    if (len > 0)
    {
      out.append(buf, len);
    }
    else if ((len < 0) && (errno == EINTR))
    {
      continue;
    }
    else
    {
      break;
    }
  }

  pclose(pipe);

  // parse SGR truecolor sequences and UTF-8 symbols into cells
  int fg = -1;
  int bg = -1;
  bool inverse = false;
  std::vector<UiImageCell> row;
  size_t i = 0;
  while (i < out.size())
  {
    const unsigned char c = out[i];
    if (c == 0x1b)
    {
      size_t end = i + 1;
      if ((end < out.size()) && (out[end] == '['))
      {
        ++end;
        while ((end < out.size()) && !((out[end] >= 0x40) && (out[end] <= 0x7e))) ++end;
        if ((end < out.size()) && (out[end] == 'm'))
        {
          std::vector<std::string> params = StrUtil::Split(out.substr(i + 2, end - i - 2), ';');
          if (params.empty()) params.push_back("0");
          for (size_t p = 0; p < params.size(); ++p)
          {
            const int v = params[p].empty() ? 0 : atoi(params[p].c_str());
            if (((v == 38) || (v == 48)) && (p + 4 < params.size()) && (params[p + 1] == "2"))
            {
              const int idx = GetColorIndex(atoi(params[p + 2].c_str()), atoi(params[p + 3].c_str()),
                                            atoi(params[p + 4].c_str()));
              ((v == 38) ? fg : bg) = idx;
              p += 4;
            }
            else if (v == 0)
            {
              fg = -1;
              bg = -1;
              inverse = false;
            }
            else if (v == 7)
            {
              inverse = true;
            }
            else if (v == 27)
            {
              inverse = false;
            }
            else if (v == 39)
            {
              fg = -1;
            }
            else if (v == 49)
            {
              bg = -1;
            }
          }
        }
      }

      i = end + 1;
      continue;
    }

    if (c == '\n')
    {
      rows.push_back(row);
      row.clear();
      ++i;
      continue;
    }

    if (c < 0x20)
    {
      ++i;
      continue;
    }

    size_t n = (c < 0x80) ? 1 : ((c >> 5) == 0x6) ? 2 : ((c >> 4) == 0xe) ? 3 : 4;
    std::wstring wch = StrUtil::ToWString(out.substr(i, n));
    i += n;
    if (wch.empty()) continue;

    UiImageCell cell;
    cell.ch = wch.at(0);
    cell.pair = inverse ? GetPair(bg, fg) : GetPair(fg, bg);
    row.push_back(cell);
  }

  if (!row.empty())
  {
    rows.push_back(row);
  }

  LOG_TRACE("image preview %s %d rows", p_FilePath.c_str(), (int)rows.size());
  return rows;
}

int UiImagePreview::GetColorIndex(int p_R, int p_G, int p_B)
{
  // nearest xterm-256 palette entry (16..255), skipping entries redefined by hex colors
  struct Entry
  {
    int idx;
    int r;
    int g;
    int b;
  };

  static int paletteCustomId = -1;
  static std::vector<Entry> palette;
  static std::map<int, int> cache;
  const int lastCustomId = UiColorConfig::GetLastCustomColorId();
  if (paletteCustomId != lastCustomId)
  {
    paletteCustomId = lastCustomId;
    palette.clear();
    cache.clear();
    static const int levels[6] = { 0, 95, 135, 175, 215, 255 };
    for (int idx = 16; idx < 256; ++idx)
    {
      if ((idx >= 32) && (idx <= lastCustomId)) continue;

      Entry entry;
      entry.idx = idx;
      if (idx < 232)
      {
        const int c = idx - 16;
        entry.r = levels[(c / 36) % 6];
        entry.g = levels[(c / 6) % 6];
        entry.b = levels[c % 6];
      }
      else
      {
        entry.r = entry.g = entry.b = 8 + ((idx - 232) * 10);
      }

      palette.push_back(entry);
    }
  }

  const int key = (p_R << 16) | (p_G << 8) | p_B;
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;

  int best = -1;
  long bestDist = -1;
  for (const auto& entry : palette)
  {
    // weighted euclidean distance, roughly perceptual
    const long dr = p_R - entry.r;
    const long dg = p_G - entry.g;
    const long db = p_B - entry.b;
    const long dist = (2 * dr * dr) + (4 * dg * dg) + (3 * db * db);
    if ((bestDist < 0) || (dist < bestDist))
    {
      bestDist = dist;
      best = entry.idx;
    }
  }

  cache[key] = best;
  return best;
}

int UiImagePreview::GetPair(int p_Fg, int p_Bg)
{
  static std::map<std::pair<int, int>, int> pairs;
  auto it = pairs.find(std::make_pair(p_Fg, p_Bg));
  if (it != pairs.end()) return it->second;

  int pair = alloc_pair(p_Fg, p_Bg);
  if (pair < 0)
  {
    pair = 0;
  }

  pairs[std::make_pair(p_Fg, p_Bg)] = pair;
  return pair;
}
