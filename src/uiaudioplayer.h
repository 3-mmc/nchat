// uiaudioplayer.h
//
// Inline playback of voice notes / audio attachments in the background with
// ffplay, so the chat view stays on screen.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

#include <sys/types.h>

class UiAudioPlayer
{
public:
  static bool IsEnabled();
  static bool IsAudio(const std::string& p_FilePath);
  static void Toggle(const std::string& p_FilePath);
  static void Stop();
  static bool IsPlaying();
  static bool Poll();
  static std::string GetStatus(const std::string& p_FilePath);

private:
  static int GetDurationSec(const std::string& p_FilePath);
  static std::string FormatTime(int p_Sec);

private:
  static pid_t m_Pid;
  static std::string m_FilePath;
  static int64_t m_StartTimeMs;
  static int m_LastElapsedSec;
};
