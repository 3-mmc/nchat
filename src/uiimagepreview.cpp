// uiimagepreview.cpp
//
// Inline image previews in the history view, rendered as configurable symbols
// by chafa and mapped onto the terminal's 256-colour palette.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uiimagepreview.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include <sys/stat.h>
#include <unistd.h>

#include "fileutil.h"
#include "log.h"
#include "strutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"

bool UiImagePreview::IsEnabled()
{
  // Low pair IDs belong to the UI; previews need a separate stable range.
  const bool hasColors = has_colors() && (COLORS >= 256) && (COLOR_PAIRS > 256);
  const bool pixels = UiPixelGraphics::Selected() && UiPixelGraphics::FallbackReason().empty();
  return (pixels || hasColors || (UiConfig::GetStr("inline_image_color") == "mono")) &&
    (UiConfig::GetStr("media_display") == "preview");
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
  const auto hasThumbnail = [&]()
  {
    struct stat st = {};
    return (stat(thumbPath.c_str(), &st) == 0) && (st.st_size > 0);
  };
  if (!hasThumbnail())
  {
    const std::string cmd = "ffmpegthumbnailer -s 512 -t 10% -i '" + StrUtil::EscapeSingleQuote(p_FilePath) +
      "' -o '" + StrUtil::EscapeSingleQuote(thumbPath) + "' </dev/null >/dev/null 2>&1";
    if ((system(cmd.c_str()) != 0) || !hasThumbnail())
    {
      // Some failed decodes leave an empty output file. Do not reuse it later.
      FileUtil::RmFile(thumbPath);
      LOG_WARNING("video thumbnail failed %s", p_FilePath.c_str());
      return std::string();
    }
  }

  return thumbPath;
}

const UiImageRows& UiImagePreview::GetRows(const std::string& p_FilePath, int p_MaxW, int p_MaxH)
{
  const int configuredH = std::max(1, (int)UiConfig::GetNum("inline_image_lines"));
  const int maxH = (p_MaxH > 0) ? std::min(configuredH, p_MaxH) : configuredH;
  static std::map<std::string, UiImageRows> cache;

  // palette entries redefined by hex colors may grow while drawing, so key on it too
  const std::string key = p_FilePath + "|" + std::to_string(p_MaxW) + "x" + std::to_string(maxH) + "|" +
    std::to_string(UiColorConfig::GetLastCustomColorId()) + "|" + UiConfig::GetStr("inline_image_style") +
    "|" + UiConfig::GetStr("inline_image_color");
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;

  if (cache.size() > 256)
  {
    cache.clear();
  }

  return cache[key] = Render(p_FilePath, p_MaxW, maxH);
}

std::shared_ptr<const UiPixelImage> UiImagePreview::GetPixels(const std::string& p_FilePath, int p_MaxW,
                                                           int p_MaxH, const void* p_Owner)
{
  const int configured = std::max(1, UiConfig::GetNum("inline_image_lines"));
  const int height = p_MaxH > 0 ? std::min(configured, p_MaxH) : configured;
  if (!UiPixelGraphics::FallbackReason(p_Owner).empty()) return nullptr;
  const std::string path = IsVideo(p_FilePath) ? GetVideoThumbnail(p_FilePath) : p_FilePath;
  if (path.empty()) return nullptr;
  return UiPixelGraphics::GetImage(path, p_MaxW, height, p_Owner);
}

void UiImagePreview::DrawRow(WINDOW* p_Win, int p_Y, const std::vector<UiImageCell>& p_Row, int p_MaxW)
{
  int x = 0;
  for (const auto& cell : p_Row)
  {
    const int width = wcwidth(cell.ch);
    if ((width <= 0) || (x + width > p_MaxW)) break;

    cchar_t cc;
    wchar_t wstr[2] = { cell.ch, L'\0' };
    int pair = cell.pair;
    setcchar(&cc, wstr, cell.attr, 0, &pair);
    mvwadd_wch(p_Win, p_Y, x, &cc);
    x += width;
  }
}

UiImageRows UiImagePreview::Render(const std::string& p_FilePath, int p_MaxW, int p_MaxH)
{
  UiImageRows rows;
  if ((p_MaxW <= 0) || (p_MaxH <= 0)) return rows;

  const std::string imagePath = IsVideo(p_FilePath) ? GetVideoThumbnail(p_FilePath) : p_FilePath;
  if (imagePath.empty()) return rows;

  const int w = std::min(p_MaxW, p_MaxH * 4);
  // Only fixed, known symbol classes enter the command, never raw config text.
  static const std::map<std::string, std::string> styles =
  {
    { "blocks", "block+border+space" },
    { "ascii", "ascii" },
    { "braille", "braille+space" },
    { "half", "vhalf+space" },
    { "quadrants", "quad+space" },
    { "sextants", "sextant+space" },
    { "stipple", "stipple+space" },
    { "mixed", "all-wide-ambiguous" },
  };
  const auto style = styles.find(UiConfig::GetStr("inline_image_style"));
  const std::string symbols = (style != styles.end()) ? style->second : styles.at("blocks");
  const std::string color = UiConfig::GetStr("inline_image_color");
  const bool monochrome = (color == "mono") || !has_colors() || (COLORS < 256) || (COLOR_PAIRS <= 256);
  // Quantize in Chafa before it chooses glyphs: rounding truecolour fg/bg
  // afterwards can collapse both to the same colour and lose image detail.
  const std::string colorOptions = monochrome ? "--colors none" :
    ((color == "foreground") ? "--colors 240 --fg-only" : "--colors 240");
  const std::string cmd = "chafa -f symbols " + colorOptions + " --symbols " + symbols + " --animate off "
    "--probe off --relative off --polite on -O 0 --size " + std::to_string(w) + "x" + std::to_string(p_MaxH) +
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

  if (pclose(pipe) != 0)
  {
    LOG_WARNING("image preview failed %s", p_FilePath.c_str());
    return rows;
  }

  // Parse indexed and truecolour SGR sequences into ncurses cells.
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
            else if (((v == 38) || (v == 48)) && (p + 2 < params.size()) && (params[p + 1] == "5"))
            {
              int idx = atoi(params[p + 2].c_str());
              // UI theme hex colours redefine a few xterm palette entries.
              // Remap those entries instead of displaying the theme colour.
              if ((idx >= 32) && (idx <= UiColorConfig::GetLastCustomColorId()))
              {
                static const int levels[] = { 0, 95, 135, 175, 215, 255 };
                if (idx < 232)
                {
                  const int cube = idx - 16;
                  idx = GetColorIndex(levels[cube / 36], levels[(cube / 6) % 6], levels[cube % 6]);
                }
                else
                {
                  const int value = 8 + ((idx - 232) * 10);
                  idx = GetColorIndex(value, value, value);
                }
              }
              ((v == 38) ? fg : bg) = ((idx >= 0) && (idx < COLORS)) ? idx : -1;
              p += 2;
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
    if (wch.empty() || (wcwidth(wch.at(0)) != 1)) continue;

    UiImageCell cell;
    cell.ch = wch.at(0);
    if (monochrome)
    {
      cell.attr = inverse ? A_REVERSE : A_NORMAL;
    }
    else
    {
      cell.pair = inverse ? GetPair(bg, fg) : GetPair(fg, bg);
    }
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
  static std::map<std::pair<int, int>, int> approximations;
  const auto approximate = approximations.find(std::make_pair(p_Fg, p_Bg));
  if (approximate != approximations.end()) return approximate->second;
  auto it = pairs.find(std::make_pair(p_Fg, p_Bg));
  if (it != pairs.end()) return it->second;

  // UI pairs grow upwards from 1. Reserve 256+ for previews so lazily
  // created UI colours never overwrite cached image cells. Do not recycle:
  // older cached/visible previews still reference these pair IDs.
  const int pair = 256 + (int)pairs.size();
  if ((pair >= std::min(COLOR_PAIRS, 32768)) || (init_pair(pair, p_Fg, p_Bg) == ERR))
  {
    // On exhaustion approximate with a stable existing pair, never reassign it.
    long bestDistance = -1;
    int bestPair = 0;
    for (const auto& existing : pairs)
    {
      long distance = 0;
      for (const auto colors : { std::make_pair(p_Fg, existing.first.first),
                                  std::make_pair(p_Bg, existing.first.second) })
      {
        if (colors.first == colors.second) continue;
        if ((colors.first < 0) || (colors.second < 0)) { distance += 3000000; continue; }
        // ncurses' initial color_content table need not match xterm's cube.
        const auto rgb = [](int idx) -> std::array<int, 3>
        {
          if (idx < 16)
          {
            short r, g, b;
            color_content(idx, &r, &g, &b);
            return {{r * 255 / 1000, g * 255 / 1000, b * 255 / 1000}};
          }
          if (idx >= 232)
          {
            const int value = 8 + (idx - 232) * 10;
            return {{value, value, value}};
          }
          static const int levels[] = {0, 95, 135, 175, 215, 255};
          const int cube = idx - 16;
          return {{levels[cube / 36], levels[(cube / 6) % 6], levels[cube % 6]}};
        };
        const auto first = rgb(colors.first), second = rgb(colors.second);
        for (size_t channel = 0; channel < first.size(); ++channel)
        {
          const long delta = first[channel] - second[channel];
          distance += delta * delta;
        }
      }
      if ((bestDistance < 0) || (distance < bestDistance))
      {
        bestDistance = distance;
        bestPair = existing.second;
      }
    }
    approximations[std::make_pair(p_Fg, p_Bg)] = bestPair;
    return bestPair;
  }

  pairs[std::make_pair(p_Fg, p_Bg)] = pair;
  return pair;
}
