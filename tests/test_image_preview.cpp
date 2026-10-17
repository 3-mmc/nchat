// Exercise real Chafa output, ncurses colour storage and cached preview stability.
#include <clocale>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <ncurses.h>
#include "fileutil.h"
#include "strutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
#include "uiimagepreview.h"

static void Check(bool ok, const std::string& message)
{
  if (!ok) throw std::runtime_error(message);
}
static std::string HexColor(int color)
{
  if (color < 0) return "#161b22";
  int r = 220, g = 220, b = 220;
  if (color >= 232) r = g = b = 8 + 10 * (color - 232);
  else if (color >= 16)
  {
    static const int levels[] = {0, 95, 135, 175, 215, 255};
    const int cube = color - 16;
    r = levels[cube / 36]; g = levels[(cube / 6) % 6]; b = levels[cube % 6];
  }
  char value[8];
  snprintf(value, sizeof(value), "#%02x%02x%02x", r & 255, g & 255, b & 255);
  return value;
}
static std::string Escape(wchar_t ch)
{
  if (ch == L'&') return "&amp;";
  if (ch == L'<') return "&lt;";
  if (ch == L'>') return "&gt;";
  return StrUtil::ToString(std::wstring(1, ch));
}
static void Sample(std::ofstream& out, const UiImageRows& rows, int index, const std::string& label)
{
  const int left = 24 + (index % 3) * 350;
  const int top = 60 + (index / 3) * 330;
  out << "<text x='" << left << "' y='" << top << "' fill='#fff' font-size='17'>" << label << "</text>\n";
  for (size_t y = 0; y < rows.size(); ++y)
  {
    for (size_t x = 0; x < rows[y].size(); ++x)
    {
      const auto& cell = rows[y][x];
      short fg = 7, bg = -1;
      if (cell.pair) pair_content(cell.pair, &fg, &bg);
      if (cell.attr & A_REVERSE) std::swap(fg, bg);
      const int cx = left + x * 8, cy = top + 14 + y * 16;
      out << "<rect x='" << cx << "' y='" << cy << "' width='8' height='16' fill='" << HexColor(bg) << "'/>";
      out << "<text x='" << cx << "' y='" << cy + 13 << "' fill='" << HexColor(fg)
          << "' font-family='DejaVu Sans Mono, monospace' font-size='13.3' xml:space='preserve'>"
          << Escape(cell.ch) << "</text>\n";
    }
  }
}
int main(int argc, char** argv)
{
  if (argc != 2) return 2;
  setlocale(LC_ALL, "C.UTF-8");
  const std::string dir = argv[1];
  FileUtil::SetApplicationDir(dir);
  UiConfig::Init();
  FILE* output = fopen("/dev/null", "w");
  FILE* input = fopen("/dev/null", "r");
  SCREEN* screen = newterm("xterm-256color", output, input);
  FileUtil::WriteFile(dir + "/color.conf", "history_name_recv_group_color_fg=usercolor\n");
  UiColorConfig::Init();
  const int preparedPalette = UiColorConfig::GetLastCustomColorId();
  const std::string image = dir + "/landscape.svg";
  FileUtil::WriteFile(image,
    "<svg xmlns='http://www.w3.org/2000/svg' width='512' height='384'>"
    "<rect width='512' height='384' fill='#58bbee'/>"
    "<circle cx='405' cy='83' r='45' fill='#ffd450'/>"
    "<path d='M0 280L155 75L320 280Z' fill='#6a7d9a'/>"
    "<path d='M105 140L155 75L205 140L175 127L155 145L137 127Z' fill='#f6f8ff'/>"
    "<path d='M0 285Q150 215 270 280Q420 215 512 265V384H0Z' fill='#40944f'/>"
    "<rect x='285' y='249' width='97' height='85' fill='#ffe4b7'/>"
    "<path d='M270 249L333 195L397 249Z' fill='#c74738'/>"
    "<rect x='325' y='285' width='25' height='49' fill='#613e30'/>"
    "<rect x='295' y='268' width='20' height='22' fill='#4897cb'/></svg>");
  try
  {
    UiConfig::SetNum("inline_image_lines", 16);
    const char* sheetPath = getenv("NCHAT_PREVIEW_SHEET");
    std::ofstream sheet;
    if (sheetPath)
    {
      sheet.open(sheetPath);
      sheet << "<svg xmlns='http://www.w3.org/2000/svg' width='1060' height='1060'>"
            << "<rect width='1060' height='1060' fill='#161b22'/>"
            << "<text x='24' y='28' fill='white' font-family='sans-serif' font-size='20'>"
            << "nchat preview styles — same image, 16 rows</text>";
    }
    const std::vector<std::string> styles = { "blocks", "ascii", "braille", "half", "quadrants", "sextants", "stipple", "mixed" };
    std::map<int, std::pair<short, short>> originals;
    int index = 0;
    for (const auto& style : styles)
    {
      UiConfig::SetStr("inline_image_style", style);
      for (const std::string mode : {"color", "foreground", "mono"})
      {
        UiConfig::SetStr("inline_image_color", mode);
        const auto& rows = UiImagePreview::GetRows(image, 40);
        Check(!rows.empty() && rows.size() <= 16, "style renders bounded rows: " + style + "/" + mode);
        for (const auto& row : rows)
        {
          Check(!row.empty() && row.size() <= 40, "bounded columns");
          for (const auto& cell : row)
          {
            Check(wcwidth(cell.ch) == 1, "all preview cells occupy one terminal column");
            if (style == "ascii") Check(cell.ch >= 32 && cell.ch < 127, "ASCII stays ASCII");
            if (style == "half") Check(cell.ch == L' ' || cell.ch == L'▀' || cell.ch == L'▄' || cell.ch == L'█', "half blocks use 1x2 cells");
            if (mode == "mono") { Check(cell.pair == 0, "monochrome has no colour pairs"); continue; }
            Check(cell.pair >= 256, "image pairs never overlap UI's 1..255 range");
            short fg, bg;
            pair_content(cell.pair, &fg, &bg);
            if (mode == "foreground") Check(bg == -1, "foreground mode keeps terminal background");
            originals[cell.pair] = {fg, bg};
          }
        }
        if (sheet.is_open() && (mode == (style == "ascii" ? "mono" : "color"))) Sample(sheet, rows, index++, style + (style == "ascii" ? " (mono)" : " (colour)"));
      }
    }
    if (sheet.is_open())
    {
      sheet << "<text x='724' y='720' fill='white' font-size='17'>Source image</text>"
            << "<g transform='translate(724 734) scale(0.625)'>" << FileUtil::ReadFile(image) << "</g></svg>";
    }
    Check(originals.size() > 8, "indexed SGR preserves distinct image colours");
    // Lazy UI colours used to overwrite alloc_pair's cached image colours.
    for (const auto& name : {"top_color", "dialog_color", "history_text_sent_color", "history_name_recv_group_color", "protocol_signal_color"})
      UiColorConfig::GetColorPair(name);
    UiColorConfig::GetUserColorPair("history_name_recv_group_color", "another-contact");
    Check(UiColorConfig::GetLastCustomColorId() == preparedPalette,
          "late contact and UI colours do not redefine the image palette");
    for (const auto& original : originals)
    {
      short fg, bg;
      pair_content(original.first, &fg, &bg);
      Check(std::make_pair(fg, bg) == original.second, "late UI colour cannot recolour cached preview");
    }
    // Exercise ncurses' extended pair path and ensure cropping cannot wrap.
    UiConfig::SetStr("inline_image_style", "half");
    UiConfig::SetStr("inline_image_color", "color");
    const auto& row = UiImagePreview::GetRows(image, 40).at(0);
    WINDOW* win = newwin(3, 7, 0, 0);
    UiImagePreview::DrawRow(win, 0, row, 6);
    cchar_t cell;
    wchar_t chars[CCHARW_MAX];
    attr_t attr;
    short legacyPair;
    int pair;
    mvwin_wch(win, 0, 0, &cell);
    getcchar(&cell, chars, &attr, &legacyPair, &pair);
    Check(pair == row[0].pair, "extended colour pair survives drawing");
    Check((mvwinch(win, 1, 0) & A_CHARTEXT) == ' ', "preview cropping never wraps");
    delwin(win);
    std::cout << "PASS: 8 styles x 3 colour modes, pair isolation, glyph widths and clipping\n";
  }
  catch (const std::exception& error)
  {
    endwin();
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  endwin();
  delscreen(screen);
  fclose(input);
  fclose(output);
  return 0;
}
