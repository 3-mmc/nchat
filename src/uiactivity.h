// Transient chat activity, measured with a monotonic clock.
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>

struct UiActivityState
{
  std::wstring marker;
  std::string label;
};

class UiActivity
{
public:
  using Chat = std::pair<std::string, std::string>;
  static int64_t Now();
  void Incoming(const Chat& p_Chat, int64_t p_Now);
  void ClearFresh(const Chat& p_Chat);
  void BeginSend(const Chat& p_Chat, uint64_t p_Id, int64_t p_Now);
  bool FinishSend(const Chat& p_RequestChat, uint64_t p_Id, bool p_Success, int64_t p_Now);
  UiActivityState GetState(const Chat& p_Chat, int64_t p_Now, bool p_Animate) const;
  void SetFailureCount(const Chat& p_Chat, int p_Count);
  bool HasWork() const;
  void Prune(int64_t p_Now);
  void RemoveChat(const Chat& p_Chat);
  void MoveChat(const Chat& p_Old, const Chat& p_New);

private:
  struct Pending
  {
    Chat chat;
    Chat requestChat; // unchanged when the chat ID is migrated
    int64_t started = 0;
  };
  std::map<Chat, int64_t> m_FreshUntil;
  std::map<uint64_t, Pending> m_Pending;
  std::map<Chat, int> m_Failures;
  std::map<Chat, int64_t> m_SentUntil;
};
