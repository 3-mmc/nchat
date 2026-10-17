// nchat is distributed under the MIT license, see LICENSE for details.
#pragma once

#include <map>
#include <string>
#include <utility>

class UiDrafts
{
public:
  using Chat = std::pair<std::string, std::string>;
  using Draft = std::pair<std::wstring, int>;
  using Store = std::map<Chat, Draft>;
  static Store Load(const std::string& p_Path);
  static bool Save(const std::string& p_Path, const Store& p_Drafts);
};
