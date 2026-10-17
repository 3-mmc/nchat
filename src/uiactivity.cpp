// nchat is distributed under the MIT license, see LICENSE for details.

#include "uiactivity.h"

#include <algorithm>
#include <chrono>

static const int64_t freshDurationMs = 6000;
static const int64_t sendingDelayMs = 1500;

int64_t UiActivity::Now()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}

void UiActivity::Incoming(const Chat& p_Chat, int64_t p_Now)
{
  m_FreshUntil[p_Chat] = p_Now + freshDurationMs;
}

void UiActivity::ClearFresh(const Chat& p_Chat)
{
  m_FreshUntil.erase(p_Chat);
}

void UiActivity::BeginSend(const Chat& p_Chat, uint64_t p_Id, int64_t p_Now)
{
  if (p_Id == 0) return;
  m_Pending[p_Id] = { p_Chat, p_Chat, p_Now };
  m_Failures.erase(p_Chat);
  m_SentUntil.erase(p_Chat);
}

bool UiActivity::FinishSend(const Chat& p_RequestChat, uint64_t p_Id, bool p_Success, int64_t p_Now)
{
  const auto pending = m_Pending.find(p_Id);
  if ((pending == m_Pending.end()) || (pending->second.requestChat != p_RequestChat)) return false;

  const Chat chat = pending->second.chat;
  const bool wasSlow = (p_Now - pending->second.started) >= sendingDelayMs;
  m_Pending.erase(pending);
  if (!p_Success) ++m_Failures[chat];
  else if (wasSlow) m_SentUntil[chat] = p_Now + 2000;
  return true;
}

UiActivityState UiActivity::GetState(const Chat& p_Chat, int64_t p_Now, bool p_Animate) const
{
  UiActivityState state;
  int count = 0;
  int64_t oldest = p_Now;
  for (const auto& pending : m_Pending)
  {
    if (pending.second.chat != p_Chat) continue;
    ++count;
    oldest = std::min(oldest, pending.second.started);
  }

  const auto failure = m_Failures.find(p_Chat);
  if (failure != m_Failures.end())
  {
    state.marker = L"!";
    state.label = "Send failed";
    if (failure->second > 1) state.label += " (" + std::to_string(failure->second) + ")";
  }
  if ((count > 0) && ((p_Now - oldest) >= sendingDelayMs))
  {
    static const wchar_t frames[] = L"|/-\\";
    const wchar_t frame = p_Animate ? frames[(p_Now / 250) % 4] : L'>';
    if (state.marker.empty()) state.marker = std::wstring(1, frame);
    const std::string progress = ((p_Now - oldest) >= 15000) ? "Still sending" : "Sending";
    const std::string sending = std::string(1, (char)frame) + " " + progress +
      ((count > 1) ? " (" + std::to_string(count) + ")" : "");
    state.label += state.label.empty() ? sending : ("; " + sending);
  }
  if (!state.marker.empty()) return state;

  const auto sent = m_SentUntil.find(p_Chat);
  if ((count == 0) && (sent != m_SentUntil.end()) && (p_Now < sent->second))
  {
    state.marker = L"\u2713";
    state.label = "Sent";
    return state;
  }
  const auto fresh = m_FreshUntil.find(p_Chat);
  if (p_Animate && (fresh != m_FreshUntil.end()) && (p_Now < fresh->second))
  {
    state.marker = ((p_Now / 400) % 2) ? L"\u25cf" : L"\u25cb";
  }
  return state;
}

void UiActivity::SetFailureCount(const Chat& p_Chat, int p_Count)
{
  if (p_Count > 0) m_Failures[p_Chat] = p_Count;
  else m_Failures.erase(p_Chat);
}

bool UiActivity::HasWork() const
{
  return !m_FreshUntil.empty() || !m_Pending.empty() || !m_SentUntil.empty();
}

void UiActivity::Prune(int64_t p_Now)
{
  for (auto* deadlines : { &m_FreshUntil, &m_SentUntil })
  {
    for (auto it = deadlines->begin(); it != deadlines->end();)
    {
      if (p_Now >= it->second) it = deadlines->erase(it);
      else ++it;
    }
  }
}

void UiActivity::RemoveChat(const Chat& p_Chat)
{
  m_FreshUntil.erase(p_Chat);
  m_SentUntil.erase(p_Chat);
  m_Failures.erase(p_Chat);
  for (auto it = m_Pending.begin(); it != m_Pending.end();)
  {
    if (it->second.chat == p_Chat) it = m_Pending.erase(it);
    else ++it;
  }
}

void UiActivity::MoveChat(const Chat& p_Old, const Chat& p_New)
{
  if (p_Old == p_New) return;
  for (auto* deadlines : { &m_FreshUntil, &m_SentUntil })
  {
    const auto old = deadlines->find(p_Old);
    if (old == deadlines->end()) continue;
    (*deadlines)[p_New] = std::max((*deadlines)[p_New], old->second);
    deadlines->erase(old);
  }
  const auto failure = m_Failures.find(p_Old);
  if (failure != m_Failures.end())
  {
    m_Failures[p_New] += failure->second;
    m_Failures.erase(failure);
  }
  for (auto& pending : m_Pending)
  {
    if (pending.second.chat == p_Old) pending.second.chat = p_New;
  }
}
