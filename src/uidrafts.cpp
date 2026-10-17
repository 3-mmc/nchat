// nchat is distributed under the MIT license, see LICENSE for details.
#include "uidrafts.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
#include <unistd.h>
#include "strutil.h"

UiDrafts::Store UiDrafts::Load(const std::string& p_Path)
{
  Store drafts;
  std::ifstream file(p_Path);
  std::string line;
  if (!std::getline(file, line) || line != "nchat-drafts-v1") return drafts;
  while (std::getline(file, line))
  {
    std::istringstream row(line);
    std::string profile, chat, text, extra;
    int cursor;
    if (!(row >> profile >> chat >> cursor >> text) || (row >> extra)) continue;
    auto validHex = [](const std::string& value)
    {
      return !value.empty() && (value.size() % 2 == 0) &&
        value.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
    };
    if (!validHex(profile) || !validHex(chat) || !validHex(text)) continue;
    try
    {
      const auto draft = StrUtil::ToWString(StrUtil::StrFromHex(text));
      if (draft.empty()) continue;
      drafts[{StrUtil::StrFromHex(profile), StrUtil::StrFromHex(chat)}] =
        {draft, std::max(0, std::min(cursor, (int)draft.size()))};
    }
    catch (...) { /* Ignore malformed records, preserving other chats. */ }
  }
  return drafts;
}

bool UiDrafts::Save(const std::string& p_Path, const Store& p_Drafts)
{
  std::string data = "nchat-drafts-v1\n";
  for (const auto& item : p_Drafts)
  {
    if (item.first.first.empty() || item.first.second.empty() || item.second.first.empty()) continue;
    data += StrUtil::StrToHex(item.first.first) + "\t" + StrUtil::StrToHex(item.first.second) + "\t" +
      std::to_string(item.second.second) + "\t" + StrUtil::StrToHex(StrUtil::ToString(item.second.first)) + "\n";
  }
  // mkstemp creates an owner-only file beside the destination. Rename only
  // after a complete, synced write so an interrupted save keeps the old file.
  const std::string pattern = p_Path + ".XXXXXX";
  std::vector<char> temp(pattern.begin(), pattern.end());
  temp.push_back('\0');
  const int fd = mkstemp(temp.data());
  if (fd < 0) return false;
  size_t offset = 0;
  bool ok = true;
  while (offset < data.size())
  {
    const auto count = write(fd, data.data() + offset, data.size() - offset);
    if ((count < 0) && (errno == EINTR)) continue;
    if (count <= 0) { ok = false; break; }
    offset += (size_t)count;
  }
  if (ok && fsync(fd) != 0) ok = false;
  if (close(fd) != 0) ok = false;
  if (ok && rename(temp.data(), p_Path.c_str()) != 0) ok = false;
  if (!ok) unlink(temp.data());
  return ok;
}
