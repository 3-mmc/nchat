// uiaudioplayer.cpp
//
// Inline playback of voice notes / audio attachments in the background with
// ffplay, so the chat view stays on screen.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uiaudioplayer.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fileutil.h"
#include "log.h"
#include "strutil.h"
#include "timeutil.h"
#include "uiconfig.h"

pid_t UiAudioPlayer::m_Pid = -1;
std::string UiAudioPlayer::m_FilePath;
int64_t UiAudioPlayer::m_StartTimeMs = 0;
int UiAudioPlayer::m_LastElapsedSec = -1;

bool UiAudioPlayer::IsEnabled()
{
  return UiConfig::GetBool("inline_audio");
}

bool UiAudioPlayer::IsAudio(const std::string& p_FilePath)
{
  static const std::set<std::string> audioExts =
  {
    ".ogg", ".oga", ".opus", ".m4a", ".mp3", ".aac", ".wav", ".amr", ".flac",
  };

  return audioExts.count(StrUtil::ToLower(FileUtil::GetFileExt(p_FilePath))) > 0;
}

void UiAudioPlayer::Toggle(const std::string& p_FilePath)
{
  const bool wasPlayingThis = (m_Pid > 0) && (m_FilePath == p_FilePath);
  Stop();
  if (wasPlayingThis) return;

  pid_t pid = fork();
  if (pid == 0)
  {
    setsid();
    int devNull = open("/dev/null", O_RDWR);
    if (devNull >= 0)
    {
      dup2(devNull, STDIN_FILENO);
      dup2(devNull, STDOUT_FILENO);
      dup2(devNull, STDERR_FILENO);
    }

    execlp("ffplay", "ffplay", "-nodisp", "-autoexit", "-hide_banner", "-loglevel", "quiet",
           p_FilePath.c_str(), (char*)nullptr);
    _exit(127);
  }
  else if (pid < 0)
  {
    LOG_WARNING("failed to start ffplay");
    return;
  }

  LOG_TRACE("audio play %s pid %d", p_FilePath.c_str(), pid);
  m_Pid = pid;
  m_FilePath = p_FilePath;
  m_StartTimeMs = TimeUtil::GetCurrentTimeMSec();
  m_LastElapsedSec = -1;
}

void UiAudioPlayer::Stop()
{
  if (m_Pid <= 0) return;

  kill(m_Pid, SIGTERM);
  waitpid(m_Pid, nullptr, 0);
  m_Pid = -1;
  m_FilePath.clear();
}

bool UiAudioPlayer::IsPlaying()
{
  return (m_Pid > 0);
}

bool UiAudioPlayer::Poll()
{
  // returns true when the status display needs a refresh
  if (m_Pid <= 0) return false;

  if (waitpid(m_Pid, nullptr, WNOHANG) == m_Pid)
  {
    m_Pid = -1;
    m_FilePath.clear();
    return true;
  }

  const int elapsedSec = (int)((TimeUtil::GetCurrentTimeMSec() - m_StartTimeMs) / 1000);
  if (elapsedSec != m_LastElapsedSec)
  {
    m_LastElapsedSec = elapsedSec;
    return true;
  }

  return false;
}

std::string UiAudioPlayer::GetStatus(const std::string& p_FilePath)
{
  if (!IsEnabled() || !IsAudio(p_FilePath)) return std::string();

  const int durationSec = GetDurationSec(p_FilePath);
  const std::string duration = (durationSec >= 0) ? FormatTime(durationSec) : "";
  if ((m_Pid > 0) && (m_FilePath == p_FilePath))
  {
    const int elapsedSec = (int)((TimeUtil::GetCurrentTimeMSec() - m_StartTimeMs) / 1000);
    return " \xe2\x96\xb6 " + FormatTime(elapsedSec) + (duration.empty() ? "" : "/" + duration); // ▶
  }

  return duration.empty() ? "" : (" (" + duration + ")");
}

int UiAudioPlayer::GetDurationSec(const std::string& p_FilePath)
{
  static std::map<std::string, int> durations;
  auto it = durations.find(p_FilePath);
  if (it != durations.end()) return it->second;

  int durationSec = -1;
  const std::string cmd = "ffprobe -v error -show_entries format=duration -of csv=p=0 -- '" +
    StrUtil::EscapeSingleQuote(p_FilePath) + "' </dev/null 2>/dev/null";
  FILE* pipe = popen(cmd.c_str(), "r");
  if (pipe != nullptr)
  {
    char buf[64] = { 0 };
    char* line = nullptr;
    while (((line = fgets(buf, sizeof(buf), pipe)) == nullptr) && ferror(pipe) && (errno == EINTR))
    {
      clearerr(pipe); // retry when interrupted by a signal
    }

    if (line != nullptr)
    {
      durationSec = (int)(atof(buf) + 0.5);
    }

    pclose(pipe);
  }

  durations[p_FilePath] = durationSec;
  return durationSec;
}

std::string UiAudioPlayer::FormatTime(int p_Sec)
{
  char buf[16];
  snprintf(buf, sizeof(buf), "%d:%02d", p_Sec / 60, p_Sec % 60);
  return buf;
}
