// igchat.h
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <condition_variable>
#include <deque>
#include <map>
#include <thread>

#include "config.h"
#include "protocol.h"
#include "sysutil.h"

class IgChat : public Protocol
{
public:
  IgChat();
  virtual ~IgChat();
  static std::string GetName() { return "Instagram"; }
  static std::string GetLibName() { return "libigchat"; }
  static std::string GetCreateFunc() { return "CreateIgChat"; }
  static std::string GetSetupMessage() { return ""; }

  std::string GetProfileId() const;
  std::string GetProfileDisplayName() const;
  bool HasFeature(ProtocolFeature p_ProtocolFeature) const;
  bool IsGroupChat(const std::string& p_ChatId) const;
  std::string GetSelfId() const;

  bool SetupProfile(const std::string& p_ProfilesDir, std::string& p_ProfileId);
  bool LoadProfile(const std::string& p_ProfilesDir, const std::string& p_ProfileId);
  bool CloseProfile();

  bool Login();
  bool Logout();

  void Process();

  void SendRequest(std::shared_ptr<RequestMessage> p_RequestMessage);
  void SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler);

  void SetProtocolUiControl(bool p_IsTakeControl);
  void SetStatus(int p_Flags);
  void ClearStatus(int p_Flags);

  void AddContactInfo(const ContactInfo& p_ContactInfo);
  std::vector<ContactInfo> GetContactInfos();
  void ClearContactInfos();

  void AddHistoryMessage(const ChatMessage& p_ChatMessage);
  std::vector<ChatMessage> GetHistoryMessages();
  void ClearHistoryMessages();

public:
  static void AddInstance(int p_ConnId, IgChat* p_Instance);
  static void RemoveInstance(int p_ConnId);
  static IgChat* GetInstance(int p_ConnId);

private:
  void Init();
  void InitConfig();
  void Cleanup();
  void CleanupConfig();
  void CallMessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage);
  void PerformRequest(std::shared_ptr<RequestMessage> p_RequestMessage);
  std::string GetProxyUrl() const;

private:
  std::string m_ProfileId;
  std::function<void(std::shared_ptr<ServiceMessage>)> m_MessageHandler;

  bool m_Running = false;
  std::thread m_Thread;
  std::deque<std::shared_ptr<RequestMessage>> m_RequestsQueue;
  std::mutex m_ProcessMutex;
  std::condition_variable m_ProcessCondVar;

  int m_ConnId = -1;
  std::string m_ProfileDir;
  Config m_Config;
  int m_IgchatVersion = 0;
  int m_ProfileDirVersion = 0;
  bool m_WasOnline = false;
  bool m_IsSetup = false;

  mutable std::mutex m_Mutex;
  std::vector<ContactInfo> m_ContactInfos;
  std::vector<ChatMessage> m_HistoryMessages;
  std::string m_SelfUserId;

  static std::mutex s_ConnIdMapMutex;
  static std::map<int, IgChat*> s_ConnIdMap;
  static const int s_CacheDirVersion = 0;
};

extern "C" {
void IgNewContactsNotify(int p_ConnId, char* p_ChatId, char* p_Name, char* p_Phone, int p_IsSelf, int p_IsAlias,
                         int p_Notify);
void IgNewChatsNotify(int p_ConnId, char* p_ChatId, int p_IsUnread, int p_IsMuted, int p_IsPinned,
                      int p_IsArchived, int p_LastMessageTime);
void IgNewGroupMembersNotify(int p_ConnId, char* p_ChatId, char* p_MembersJson);
void IgNewMessagesNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text, int p_FromMe,
                         char* p_ReplyId, char* p_FileId, char* p_FilePath, int p_FileStatus, int p_TimeSent,
                         int p_IsRead, int p_IsEdited);
void IgNewHistoryMessagesNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text,
                                int p_FromMe, char* p_QuotedId, char* p_FileId, char* p_FilePath, int p_FileStatus,
                                int p_TimeSent, int p_IsRead, int p_IsEdited, char* p_FromMsgId, int p_Notify);
void IgNewStatusNotify(int p_ConnId, char* p_UserId, int p_IsOnline, int p_TimeSeen);
void IgNewTypingNotify(int p_ConnId, char* p_ChatId, char* p_UserId, int p_IsTyping);
void IgNewMessageStatusNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, int p_IsRead);
void IgNewMessageFileNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_FileId, char* p_FilePath,
                            int p_FileStatus, int p_Action);
void IgNewMessageReactionNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text,
                                int p_FromMe);
void IgDeleteChatNotify(int p_ConnId, char* p_ChatId);
void IgMoveChatNotify(int p_ConnId, char* p_ChatId, char* p_NewChatId);
void IgDeleteMessageNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, int p_IsOutgoing);
void IgUpdateMuteNotify(int p_ConnId, char* p_ChatId, int p_IsMuted);
void IgUpdateArchivedNotify(int p_ConnId, char* p_ChatId, int p_IsArchived);
void IgUpdatePinNotify(int p_ConnId, char* p_ChatId, int p_IsPinned, int p_TimePinned);
void IgReinit(int p_ConnId);
void IgSetProtocolUiControl(int p_ConnId, int p_IsTakeControl);
void IgSetStatus(int p_ConnId, int p_Flags);
void IgClearStatus(int p_ConnId, int p_Flags);
int IgAppConfigGetNum(char* p_Param);
void IgAppConfigSetNum(char* p_Param, int p_Value);
int IgHasGui();
void IgLogTrace(char* p_Filename, int p_LineNo, char* p_Message);
void IgLogDebug(char* p_Filename, int p_LineNo, char* p_Message);
void IgLogInfo(char* p_Filename, int p_LineNo, char* p_Message);
void IgLogWarning(char* p_Filename, int p_LineNo, char* p_Message);
void IgLogError(char* p_Filename, int p_LineNo, char* p_Message);
}

extern "C" IgChat* CreateIgChat();
