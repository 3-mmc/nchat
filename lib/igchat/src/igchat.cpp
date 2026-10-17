// igchat.cpp
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "igchat.h"

#include <iostream>
#include <limits>

#include <sys/stat.h>

#include "appconfig.h"
#include "fileutil.h"
#include "libcgoig.h"
#include "log.h"
#include "messagecache.h"
#include "protocolutil.h"
#include "setuputil.h"
#include "status.h"
#include "strutil.h"
#include "timeutil.h"

// keep in sync with vars in gosg.go
enum NotifyType
{
  NotifyDirect = 0,
  NotifyCache = 1,
  NotifySendCached = 2,
};

std::mutex IgChat::s_ConnIdMapMutex;
std::map<int, IgChat*> IgChat::s_ConnIdMap;

extern "C" IgChat* CreateIgChat()
{
  return new IgChat();
}

IgChat::IgChat()
{
  m_ProfileId = GetName();
  m_IgchatVersion = CIgGetVersion();
}

IgChat::~IgChat()
{
}

std::string IgChat::GetProfileId() const
{
  return m_ProfileId;
}

std::string IgChat::GetProfileDisplayName() const
{
  std::string profileDisplayName = m_Config.Get("profile_display_name");
  return profileDisplayName;
}

bool IgChat::HasFeature(ProtocolFeature p_ProtocolFeature) const
{
  // Instagram allows editing within 15 minutes
  static int customFeatures =
    FeatureEditMessagesWithinFifteenMins |
    FeatureAutoGetContactsOnLogin |
    FeaturePinChat;
  return (p_ProtocolFeature & customFeatures);
}

bool IgChat::IsGroupChat(const std::string& p_ChatId) const
{
  if (m_ConnId == -1) return false;

  return (CIgIsGroupChat(m_ConnId, const_cast<char*>(p_ChatId.c_str())) == 1);
}

std::string IgChat::GetSelfId() const
{
  std::unique_lock<std::mutex> lock(m_Mutex);
  return m_SelfUserId;
}

bool IgChat::SetupProfile(const std::string& p_ProfilesDir, std::string& p_ProfileId)
{
  m_IsSetup = true;

  std::cout << "\n";
  std::cout << "NOTICE:\n";
  std::cout << "This is an unofficial Instagram client implementation (based on mautrix-meta).\n";
  std::cout << "Meta may flag accounts that use unofficial clients - use at your own risk.\n";
  std::cout << "You may abort this setup wizard by pressing CTRL-C.\n";
  std::cout << "\n";

  // log in using a temporary profile dir, renamed once the username is known
  const std::string tmpProfileDir = p_ProfilesDir + "/" + m_ProfileId + "_setup";
  FileUtil::RmDir(tmpProfileDir);
  mkdir(tmpProfileDir.c_str(), 0777);

  std::string proxyUrl = GetProxyUrl();
  int connId = CIgInit(const_cast<char*>(tmpProfileDir.c_str()), const_cast<char*>(proxyUrl.c_str()));
  if (connId == -1)
  {
    m_IsSetup = false;
    return false;
  }

  std::cout << "Login method:\n";
  std::cout << "  1. Username and password (with two-factor code if enabled)\n";
  std::cout << "  2. Cookies from a browser logged in to instagram.com\n";
  std::cout << "Select method (1): ";
  std::string method;
  std::getline(std::cin, method);
  StrUtil::Trim(method);

  auto lastError = [&]()
  {
    char* err = CIgGetLastError(connId);
    std::string str(err);
    free(err);
    return str;
  };

  int setupRv = -1;
  if (method == "2")
  {
    std::cout << "\nIn your browser, open instagram.com (logged in) and the developer tools (F12),\n";
    std::cout << "go to Network, reload, right-click any request to instagram.com and pick\n";
    std::cout << "Copy > Copy as cURL. Paste it here (or just the cookie header value) and press Enter:\n";
    std::string input;
    std::string line;
    while (std::getline(std::cin, line))
    {
      input += line + "\n";
      // multi-line cURL commands continue with a trailing backslash
      if (line.empty() || (line.back() != '\\')) break;
    }

    setupRv = CIgSetupCookies(connId, const_cast<char*>(input.c_str()));
  }
  else
  {
    std::cout << "Username: ";
    std::string username;
    std::getline(std::cin, username);
    StrUtil::Trim(username);
    std::cout << "Password: ";
    std::string password = StrUtil::GetPass();
    std::cout << "\n";

    setupRv = CIgSetupPassword(connId, const_cast<char*>(username.c_str()), const_cast<char*>(password.c_str()));
    while (setupRv == 1)
    {
      std::cout << "Two-factor code (" << lastError() << "): ";
      std::string code;
      std::getline(std::cin, code);
      setupRv = CIgSetupTwoFactor(connId, const_cast<char*>(code.c_str()));
    }
  }

  if (setupRv != 0)
  {
    std::cout << "Login failed: " << lastError() << "\n";
    CIgCleanup(connId);
    FileUtil::RmDir(tmpProfileDir);
    m_IsSetup = false;
    return false;
  }

  const std::string username = lastError();
  std::cout << "Logged in as " << username << "\n";
  CIgCleanup(connId);

  m_ProfileId = m_ProfileId + "_" + username;
  std::string profileDir = p_ProfilesDir + "/" + m_ProfileId;
  FileUtil::RmDir(profileDir);
  FileUtil::Move(tmpProfileDir, profileDir);
  m_ProfileDir = profileDir;

  p_ProfileId = m_ProfileId;

  connId = CIgInit(const_cast<char*>(profileDir.c_str()), const_cast<char*>(proxyUrl.c_str()));
  if (connId == -1)
  {
    m_IsSetup = false;
    return false;
  }

  m_ConnId = connId;
  AddInstance(m_ConnId, this);
  MessageCache::AddProfile(m_ProfileId, false /*p_CheckSequence*/, s_CacheDirVersion, true /*p_IsSetup*/,
                           false /*p_AllowReadOnly*/);

  InitConfig();
  Init();

  bool rv = Login();
  if (!rv)
  {
    Cleanup();
    CleanupConfig();
    m_IsSetup = false;
    return false;
  }

  m_IsSetup = false;
  return true;
}

bool IgChat::LoadProfile(const std::string& p_ProfilesDir, const std::string& p_ProfileId)
{
  LOG_INFO("load instagram profile");

  if (!p_ProfilesDir.empty() && !p_ProfileId.empty())
  {
    m_ProfileDir = p_ProfilesDir + "/" + p_ProfileId;
    m_ProfileId = p_ProfileId;
  }

  bool isRemoved = false;
  MessageCache::AddProfile(m_ProfileId, false /*p_CheckSequence*/, s_CacheDirVersion, false /*p_IsSetup*/,
                           false /*p_AllowReadOnly*/, &isRemoved);
  if (isRemoved)
  {
    LOG_INFO("cache removed - remove profile to force reauth");
    FileUtil::RmDir(m_ProfileDir);
  }

  std::string proxyUrl = GetProxyUrl();
  m_ConnId = CIgInit(const_cast<char*>(m_ProfileDir.c_str()), const_cast<char*>(proxyUrl.c_str()));
  if (m_ConnId == -1) return false;

  AddInstance(m_ConnId, this);

  m_ProfileDirVersion = FileUtil::GetDirVersion(m_ProfileDir);

  InitConfig();
  Init();

  return true;
}

bool IgChat::CloseProfile()
{
  if ((m_IgchatVersion != m_ProfileDirVersion) && m_WasOnline)
  {
    LOG_INFO("update profile to %d", m_IgchatVersion);
    FileUtil::SetDirVersion(m_ProfileDir, m_IgchatVersion);
  }

  int rv = CIgCleanup(m_ConnId);
  RemoveInstance(m_ConnId);
  m_ConnId = -1;

  Cleanup();
  CleanupConfig();

  return (rv == 0);
}

void IgChat::Init()
{
}

void IgChat::InitConfig()
{
  const std::map<std::string, std::string> defaultConfig =
  {
    { "profile_display_name", "" },
  };
  const std::string configPath(m_ProfileDir + std::string("/instagram.conf"));
  m_Config = Config(configPath, defaultConfig);
}

void IgChat::Cleanup()
{
}

void IgChat::CleanupConfig()
{
  m_Config.Save();
}

bool IgChat::Login()
{
  int rv = 0;
  if (!m_Running)
  {
    m_Running = true;
    m_Thread = std::thread(&IgChat::Process, this);

    rv = CIgLogin(m_ConnId);

    {
      std::shared_ptr<ConnectNotify> connectNotify = std::make_shared<ConnectNotify>(m_ProfileId);
      connectNotify->success = (rv == 0);
      m_WasOnline = connectNotify->success;

      std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
      deferNotifyRequest->serviceMessage = connectNotify;
      SendRequest(deferNotifyRequest);
    }
  }
  return (rv == 0);
}

bool IgChat::Logout()
{
  int rv = 0;
  if (m_Running)
  {
    rv = CIgLogout(m_ConnId);
    Status::Clear(m_ProfileId, Status::FlagOnline);

    std::unique_lock<std::mutex> lock(m_ProcessMutex);
    m_Running = false;
    m_ProcessCondVar.notify_one();
  }

  if (m_Thread.joinable())
  {
    m_Thread.join();
  }
  return (rv == 0);
}

void IgChat::Process()
{
  while (m_Running)
  {
    std::shared_ptr<RequestMessage> requestMessage;

    {
      std::unique_lock<std::mutex> lock(m_ProcessMutex);
      while (m_RequestsQueue.empty() && m_Running)
      {
        m_ProcessCondVar.wait(lock);
      }

      if (!m_Running)
      {
        break;
      }

      if (!m_MessageHandler)
      {
        m_ProcessCondVar.wait(lock);
        continue;
      }

      requestMessage = m_RequestsQueue.front();
      m_RequestsQueue.pop_front();
    }

    PerformRequest(requestMessage);
  }
}

void IgChat::SendRequest(std::shared_ptr<RequestMessage> p_RequestMessage)
{
  std::unique_lock<std::mutex> lock(m_ProcessMutex);
  m_RequestsQueue.push_back(p_RequestMessage);
  m_ProcessCondVar.notify_one();
}

void IgChat::SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler)
{
  m_MessageHandler = p_MessageHandler;
  m_ProcessCondVar.notify_one();
}

void IgChat::CallMessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage)
{
  MessageCache::AddFromServiceMessage(m_ProfileId, p_ServiceMessage);

  if (!m_MessageHandler)
  {
    LOG_DEBUG("message handler not set");
    return;
  }

  m_MessageHandler(p_ServiceMessage);
}

void IgChat::PerformRequest(std::shared_ptr<RequestMessage> p_RequestMessage)
{
  bool requestIntervalDelay = true;

  switch (p_RequestMessage->GetMessageType())
  {
    case GetContactsRequestType:
      {
        LOG_DEBUG("get contacts");
        std::shared_ptr<GetContactsRequest> getContactsRequest = std::static_pointer_cast<GetContactsRequest>(
          p_RequestMessage);
        CIgGetContacts(m_ConnId);
      }
      break;

    case GetChatsRequestType:
      {
        LOG_DEBUG("get chats");
        std::shared_ptr<GetChatsRequest> getChatsRequest =
          std::static_pointer_cast<GetChatsRequest>(p_RequestMessage);
        MessageCache::FetchChats(m_ProfileId, getChatsRequest->chatIds);
      }
      break;

    case GetStatusRequestType:
      {
        // presence is not supported
        LOG_DEBUG("get status - not supported by Instagram");
      }
      break;

    case GetMessageRequestType:
      {
        LOG_DEBUG("get message");
        std::shared_ptr<GetMessageRequest> getMessageRequest =
          std::static_pointer_cast<GetMessageRequest>(p_RequestMessage);
        MessageCache::FetchOneMessage(m_ProfileId, getMessageRequest->chatId, getMessageRequest->msgId,
                                      false /*p_Sync*/);
      }
      break;

    case GetMessagesRequestType:
      {
        LOG_DEBUG("get messages");
        std::shared_ptr<GetMessagesRequest> getMessagesRequest =
          std::static_pointer_cast<GetMessagesRequest>(p_RequestMessage);

        if (MessageCache::FetchMessagesFrom(m_ProfileId, getMessagesRequest->chatId,
                                            getMessagesRequest->fromMsgId,
                                            getMessagesRequest->limit, false /* p_Sync */))
        {
          break;
        }

        CIgGetMessages(m_ConnId,
                       const_cast<char*>(getMessagesRequest->chatId.c_str()),
                       getMessagesRequest->limit,
                       const_cast<char*>(getMessagesRequest->fromMsgId.c_str()),
                       0 /* owner */);
      }
      break;

    case SendMessageRequestType:
      {
        LOG_DEBUG("send message");
        Status::Set(m_ProfileId, Status::FlagSending);
        std::shared_ptr<SendMessageRequest> sendMessageRequest =
          std::static_pointer_cast<SendMessageRequest>(p_RequestMessage);
        std::string chatId = sendMessageRequest->chatId;
        std::string editMsgId = "";
        std::string text = sendMessageRequest->chatMessage.text;
        std::string quotedId = sendMessageRequest->chatMessage.quotedId;
        std::string quotedText = sendMessageRequest->chatMessage.quotedText;
        std::string quotedSender = sendMessageRequest->chatMessage.quotedSender;
        std::string filePath;
        std::string fileType;
        int editMsgSent = 0;
        if (!sendMessageRequest->chatMessage.fileInfo.empty())
        {
          FileInfo fileInfo =
            ProtocolUtil::FileInfoFromHex(sendMessageRequest->chatMessage.fileInfo);
          filePath = fileInfo.filePath;
          fileType = fileInfo.fileType;
        }

        std::string mentionsJson = ProtocolUtil::MentionsToJson(sendMessageRequest->chatMessage.mentions);

        int rv =
          CIgSendMessage(m_ConnId, const_cast<char*>(chatId.c_str()), const_cast<char*>(text.c_str()),
                         const_cast<char*>(quotedId.c_str()), const_cast<char*>(quotedText.c_str()),
                         const_cast<char*>(quotedSender.c_str()), const_cast<char*>(filePath.c_str()),
                         const_cast<char*>(fileType.c_str()), const_cast<char*>(editMsgId.c_str()),
                         editMsgSent, const_cast<char*>(mentionsJson.c_str()));
        Status::Clear(m_ProfileId, Status::FlagSending);

        std::shared_ptr<SendMessageNotify> sendMessageNotify = std::make_shared<SendMessageNotify>(m_ProfileId);
        sendMessageNotify->success = (rv == 0);
        sendMessageNotify->chatId = sendMessageRequest->chatId;
        sendMessageNotify->chatMessage = sendMessageRequest->chatMessage;
        CallMessageHandler(sendMessageNotify);
      }
      break;

    case EditMessageRequestType:
      {
        LOG_DEBUG("edit message");
        Status::Set(m_ProfileId, Status::FlagSending);
        std::shared_ptr<EditMessageRequest> editMessageRequest =
          std::static_pointer_cast<EditMessageRequest>(p_RequestMessage);
        std::string chatId = editMessageRequest->chatId;
        std::string editMsgId = editMessageRequest->msgId;
        std::string text = editMessageRequest->chatMessage.text;
        std::string quotedId = editMessageRequest->chatMessage.quotedId;
        std::string quotedText = editMessageRequest->chatMessage.quotedText;
        std::string quotedSender = editMessageRequest->chatMessage.quotedSender;
        std::string filePath;
        std::string fileType;
        int editMsgSent = editMessageRequest->chatMessage.timeSent / 1000;
        if (!editMessageRequest->chatMessage.fileInfo.empty())
        {
          FileInfo fileInfo =
            ProtocolUtil::FileInfoFromHex(editMessageRequest->chatMessage.fileInfo);
          filePath = fileInfo.filePath;
          fileType = fileInfo.fileType;
        }

        std::string editMentionsJson = ProtocolUtil::MentionsToJson(editMessageRequest->chatMessage.mentions);

        CIgSendMessage(m_ConnId, const_cast<char*>(chatId.c_str()), const_cast<char*>(text.c_str()),
                       const_cast<char*>(quotedId.c_str()), const_cast<char*>(quotedText.c_str()),
                       const_cast<char*>(quotedSender.c_str()), const_cast<char*>(filePath.c_str()),
                       const_cast<char*>(fileType.c_str()), const_cast<char*>(editMsgId.c_str()),
                       editMsgSent, const_cast<char*>(editMentionsJson.c_str()));
        Status::Clear(m_ProfileId, Status::FlagSending);
      }
      break;

    case MarkMessageReadRequestType:
      {
        LOG_DEBUG("mark message read");
        std::shared_ptr<MarkMessageReadRequest> markMessageReadRequest =
          std::static_pointer_cast<MarkMessageReadRequest>(p_RequestMessage);
        std::string chatId = markMessageReadRequest->chatId;
        std::string msgId = markMessageReadRequest->msgId;
        std::string senderId = markMessageReadRequest->senderId;

        int rv = CIgMarkMessageRead(m_ConnId, const_cast<char*>(chatId.c_str()),
                                    const_cast<char*>(senderId.c_str()),
                                    const_cast<char*>(msgId.c_str()));

        std::shared_ptr<MarkMessageReadNotify> markMessageReadNotify =
          std::make_shared<MarkMessageReadNotify>(m_ProfileId);
        markMessageReadNotify->success = (rv == 0);
        markMessageReadNotify->chatId = markMessageReadRequest->chatId;
        markMessageReadNotify->msgId = markMessageReadRequest->msgId;
        CallMessageHandler(markMessageReadNotify);
      }
      break;

    case DeleteMessageRequestType:
      {
        LOG_DEBUG("delete message");
        Status::Set(m_ProfileId, Status::FlagUpdating);
        std::shared_ptr<DeleteMessageRequest> deleteMessageRequest =
          std::static_pointer_cast<DeleteMessageRequest>(p_RequestMessage);
        std::string chatId = deleteMessageRequest->chatId;
        std::string senderId = deleteMessageRequest->senderId;
        std::string msgId = deleteMessageRequest->msgId;

        CIgDeleteMessage(m_ConnId, const_cast<char*>(chatId.c_str()),
                         const_cast<char*>(senderId.c_str()),
                         const_cast<char*>(msgId.c_str()));
        Status::Clear(m_ProfileId, Status::FlagUpdating);

        std::shared_ptr<DeleteMessageNotify> deleteMessageNotify = std::make_shared<DeleteMessageNotify>(m_ProfileId);
        deleteMessageNotify->success = true;
        deleteMessageNotify->chatId = deleteMessageRequest->chatId;
        deleteMessageNotify->msgId = deleteMessageRequest->msgId;
        deleteMessageNotify->isOutgoing = true;
        CallMessageHandler(deleteMessageNotify);
      }
      break;

    case DeleteChatRequestType:
      {
        LOG_DEBUG("delete chat");
        Status::Set(m_ProfileId, Status::FlagUpdating);
        std::shared_ptr<DeleteChatRequest> deleteChatRequest =
          std::static_pointer_cast<DeleteChatRequest>(p_RequestMessage);
        std::string chatId = deleteChatRequest->chatId;

        CIgDeleteChat(m_ConnId, const_cast<char*>(chatId.c_str()));
        Status::Clear(m_ProfileId, Status::FlagUpdating);

        std::shared_ptr<DeleteChatNotify> deleteChatNotify = std::make_shared<DeleteChatNotify>(m_ProfileId);
        deleteChatNotify->success = true;
        deleteChatNotify->chatId = deleteChatRequest->chatId;
        CallMessageHandler(deleteChatNotify);
      }
      break;

    case ArchiveChatRequestType:
      {
        LOG_DEBUG("archive chat");
        Status::Set(m_ProfileId, Status::FlagUpdating);
        std::shared_ptr<ArchiveChatRequest> archiveChatRequest =
          std::static_pointer_cast<ArchiveChatRequest>(p_RequestMessage);
        std::string chatId = archiveChatRequest->chatId;
        int32_t isArchived = archiveChatRequest->isArchived ? 1 : 0;

        CIgArchiveChat(m_ConnId, const_cast<char*>(chatId.c_str()), isArchived);
        Status::Clear(m_ProfileId, Status::FlagUpdating);
      }
      break;

    case PinChatRequestType:
      {
        LOG_DEBUG("pin chat");
        Status::Set(m_ProfileId, Status::FlagUpdating);
        std::shared_ptr<PinChatRequest> pinChatRequest =
          std::static_pointer_cast<PinChatRequest>(p_RequestMessage);
        std::string chatId = pinChatRequest->chatId;
        int32_t isPinned = pinChatRequest->isPinned ? 1 : 0;

        CIgPinChat(m_ConnId, const_cast<char*>(chatId.c_str()), isPinned);
        Status::Clear(m_ProfileId, Status::FlagUpdating);
      }
      break;

    case SendTypingRequestType:
      {
        LOG_DEBUG("send typing");
        std::shared_ptr<SendTypingRequest> sendTypingRequest =
          std::static_pointer_cast<SendTypingRequest>(p_RequestMessage);
        std::string chatId = sendTypingRequest->chatId;
        int32_t isTyping = sendTypingRequest->isTyping ? 1 : 0;

        int rv = CIgSendTyping(m_ConnId, const_cast<char*>(chatId.c_str()), isTyping);

        std::shared_ptr<SendTypingNotify> sendTypingNotify = std::make_shared<SendTypingNotify>(m_ProfileId);
        sendTypingNotify->success = (rv == 0);
        sendTypingNotify->chatId = sendTypingRequest->chatId;
        sendTypingNotify->isTyping = sendTypingRequest->isTyping;
        CallMessageHandler(sendTypingNotify);
      }
      break;

    case SetStatusRequestType:
      {
        // setting online/away status is not supported
        std::shared_ptr<SetStatusRequest> setStatusRequest =
          std::static_pointer_cast<SetStatusRequest>(p_RequestMessage);

        std::shared_ptr<SetStatusNotify> setStatusNotify = std::make_shared<SetStatusNotify>(m_ProfileId);
        setStatusNotify->success = true;
        setStatusNotify->isOnline = setStatusRequest->isOnline;
        CallMessageHandler(setStatusNotify);
      }
      break;

    case CreateChatRequestType:
      {
        LOG_DEBUG("create chat");

        std::shared_ptr<CreateChatRequest> createChatRequest =
          std::static_pointer_cast<CreateChatRequest>(p_RequestMessage);
        std::string userId = createChatRequest->userId;

        std::shared_ptr<CreateChatNotify> createChatNotify = std::make_shared<CreateChatNotify>(m_ProfileId);
        createChatNotify->success = true;
        createChatNotify->chatInfo.id = userId;
        CallMessageHandler(createChatNotify);
      }
      break;

    case DownloadFileRequestType:
      {
        std::shared_ptr<DownloadFileRequest> downloadFileRequest =
          std::static_pointer_cast<DownloadFileRequest>(p_RequestMessage);
        std::string chatId = downloadFileRequest->chatId;
        std::string msgId = downloadFileRequest->msgId;
        std::string fileId = downloadFileRequest->fileId;
        DownloadFileAction downloadFileAction = downloadFileRequest->downloadFileAction;

        CIgDownloadFile(m_ConnId,
                        const_cast<char*>(chatId.c_str()),
                        const_cast<char*>(msgId.c_str()),
                        const_cast<char*>(fileId.c_str()),
                        downloadFileAction
                        );
      }
      break;

    case SendReactionRequestType:
      {
        LOG_DEBUG("send reaction");

        std::shared_ptr<SendReactionRequest> sendReactionRequest =
          std::static_pointer_cast<SendReactionRequest>(p_RequestMessage);
        std::string chatId = sendReactionRequest->chatId;
        std::string senderId = sendReactionRequest->senderId;
        std::string msgId = sendReactionRequest->msgId;
        std::string emoji = sendReactionRequest->emoji;
        std::string prevEmoji = sendReactionRequest->prevEmoji;

        CIgSendReaction(m_ConnId,
                        const_cast<char*>(chatId.c_str()),
                        const_cast<char*>(senderId.c_str()),
                        const_cast<char*>(msgId.c_str()),
                        const_cast<char*>(emoji.c_str()),
                        const_cast<char*>(prevEmoji.c_str())
                        );
      }
      break;

    case ReinitRequestType:
      {
        LOG_DEBUG("reinit");

        CIgLogout(m_ConnId);
        CloseProfile();
        LoadProfile("", "");
        CIgLogin(m_ConnId);
      }
      break;

    case DeferNotifyRequestType:
      {
        std::shared_ptr<DeferNotifyRequest> deferNotifyRequest =
          std::static_pointer_cast<DeferNotifyRequest>(p_RequestMessage);
        CallMessageHandler(deferNotifyRequest->serviceMessage);
        requestIntervalDelay = false;
      }
      break;

    case SetCurrentChatRequestType:
      {
        // No handling needed
      }
      break;

    case FindMessageRequestType:
      {
        LOG_DEBUG("Find message");
        std::shared_ptr<FindMessageRequest> findMessageRequest =
          std::static_pointer_cast<FindMessageRequest>(p_RequestMessage);

        MessageCache::FindMessage(m_ProfileId,
                                  findMessageRequest->chatId,
                                  findMessageRequest->fromMsgId,
                                  findMessageRequest->lastMsgId,
                                  findMessageRequest->findText,
                                  findMessageRequest->findMsgId,
                                  findMessageRequest->findPinned);
      }
      break;

    case GetGroupMembersRequestType:
      {
        LOG_DEBUG("get group members");
        std::shared_ptr<GetGroupMembersRequest> getGroupMembersRequest =
          std::static_pointer_cast<GetGroupMembersRequest>(p_RequestMessage);
        std::string chatId = getGroupMembersRequest->chatId;
        CIgGetGroupMembers(m_ConnId, const_cast<char*>(chatId.c_str()));
      }
      break;

    default:
      LOG_DEBUG("unknown request %d", p_RequestMessage->GetMessageType());
      break;
  }

  if (requestIntervalDelay)
  {
    TimeUtil::Sleep(0.050);
  }
}

std::string IgChat::GetProxyUrl() const
{
  // Not supported, consider removing code.
  const std::string proxyHost = AppConfig::GetStr("proxy_host");
  const int proxyPort = AppConfig::GetNum("proxy_port");
  if (!proxyHost.empty() && (proxyPort != 0))
  {
    std::string proxyUrl = "socks5://";
    const std::string proxyUser = AppConfig::GetStr("proxy_user");
    const std::string proxyPass = AppConfig::GetStr("proxy_pass");
    if (!proxyUser.empty())
    {
      proxyUrl += proxyUser + ":" + proxyPass + "@";
    }

    proxyUrl += proxyHost + ":" + std::to_string(proxyPort);
    return proxyUrl;
  }

  return "";
}

void IgChat::SetProtocolUiControl(bool p_IsTakeControl)
{
  if (m_IsSetup)
  {
    LOG_TRACE("set protocol ui control %d ignored during setup", p_IsTakeControl);
    return;
  }

  LOG_TRACE("set protocol ui control %d", p_IsTakeControl);
  std::shared_ptr<ProtocolUiControlNotify> protocolUiControlNotify =
    std::make_shared<ProtocolUiControlNotify>(m_ProfileId);
  protocolUiControlNotify->isTakeControl = p_IsTakeControl;
  CallMessageHandler(protocolUiControlNotify);

  // if attempting to take control, but failed to do so, keep retrying
  while (p_IsTakeControl && !protocolUiControlNotify->isTakeControl)
  {
    TimeUtil::Sleep(0.500);
    LOG_TRACE("set protocol ui control retry");
    protocolUiControlNotify->isTakeControl = p_IsTakeControl;
    CallMessageHandler(protocolUiControlNotify);
  }

  TimeUtil::Sleep(0.100); // wait more than GetKey timeout
}

void IgChat::SetStatus(int p_Flags)
{
  Status::Set(m_ProfileId, p_Flags);
}

void IgChat::ClearStatus(int p_Flags)
{
  Status::Clear(m_ProfileId, p_Flags);
}

void IgChat::AddContactInfo(const ContactInfo& p_ContactInfo)
{
  std::unique_lock<std::mutex> lock(m_Mutex);
  m_ContactInfos.push_back(p_ContactInfo);

  if (p_ContactInfo.isSelf)
  {
    m_SelfUserId = p_ContactInfo.id;
  }
}

std::vector<ContactInfo> IgChat::GetContactInfos()
{
  std::unique_lock<std::mutex> lock(m_Mutex);
  return m_ContactInfos;
}

void IgChat::ClearContactInfos()
{
  std::unique_lock<std::mutex> lock(m_Mutex);
  m_ContactInfos.clear();
}

void IgChat::AddHistoryMessage(const ChatMessage& p_ChatMessage)
{
  std::unique_lock<std::mutex> lock(m_Mutex);
  m_HistoryMessages.push_back(p_ChatMessage);
}

std::vector<ChatMessage> IgChat::GetHistoryMessages()
{
  std::unique_lock<std::mutex> lock(m_Mutex);
  return m_HistoryMessages;
}

void IgChat::ClearHistoryMessages()
{
  std::unique_lock<std::mutex> lock(m_Mutex);
  m_HistoryMessages.clear();
}

void IgChat::AddInstance(int p_ConnId, IgChat* p_Instance)
{
  std::unique_lock<std::mutex> lock(s_ConnIdMapMutex);
  s_ConnIdMap[p_ConnId] = p_Instance;
}

void IgChat::RemoveInstance(int p_ConnId)
{
  std::unique_lock<std::mutex> lock(s_ConnIdMapMutex);
  s_ConnIdMap.erase(p_ConnId);
}

IgChat* IgChat::GetInstance(int p_ConnId)
{
  std::unique_lock<std::mutex> lock(s_ConnIdMapMutex);
  auto it = s_ConnIdMap.find(p_ConnId);
  return (it != s_ConnIdMap.end()) ? it->second : nullptr;
}

void IgNewContactsNotify(int p_ConnId, char* p_ChatId, char* p_Name, char* p_Phone, int p_IsSelf, int p_IsAlias,
                         int p_Notify)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    ContactInfo contactInfo;
    contactInfo.id = std::string(p_ChatId);
    contactInfo.name = std::string(p_Name);
    contactInfo.phone = std::string(p_Phone); // instagram: @username
    contactInfo.isSelf = (p_IsSelf == 1);
    contactInfo.isAlias = (p_IsAlias == 1);

    std::shared_ptr<NewContactsNotify> newContactsNotify;

    if (p_Notify == NotifyDirect)
    {
      newContactsNotify = std::make_shared<NewContactsNotify>(instance->GetProfileId());
      newContactsNotify->fullSync = false;
      newContactsNotify->contactInfos.push_back(contactInfo);
    }
    else if (p_Notify == NotifyCache)
    {
      instance->AddContactInfo(contactInfo);
    }
    else if (p_Notify == NotifySendCached)
    {
      instance->AddContactInfo(contactInfo);

      newContactsNotify = std::make_shared<NewContactsNotify>(instance->GetProfileId());
      newContactsNotify->fullSync = true;
      newContactsNotify->contactInfos = instance->GetContactInfos();
      instance->ClearContactInfos();
    }

    if (newContactsNotify)
    {
      std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
      deferNotifyRequest->serviceMessage = newContactsNotify;
      instance->SendRequest(deferNotifyRequest);
    }
  }

  free(p_ChatId);
  free(p_Name);
  free(p_Phone);
}

void IgNewChatsNotify(int p_ConnId, char* p_ChatId, int p_IsUnread, int p_IsMuted, int p_IsPinned,
                      int p_IsArchived, int p_LastMessageTime)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    ChatInfo chatInfo;
    chatInfo.id = std::string(p_ChatId);
    chatInfo.isUnread = (p_IsUnread == 1);
    chatInfo.isUnreadMention = false;
    chatInfo.isMuted = (p_IsMuted == 1);
    chatInfo.isPinned = (p_IsPinned == 1);
    chatInfo.isArchived = (p_IsArchived == 1);
    chatInfo.lastMessageTime = ((int64_t)p_LastMessageTime) * 1000;

    std::shared_ptr<NewChatsNotify> newChatsNotify = std::make_shared<NewChatsNotify>(instance->GetProfileId());
    newChatsNotify->success = true;
    newChatsNotify->chatInfos.push_back(chatInfo);

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = newChatsNotify;
    instance->SendRequest(deferNotifyRequest);
  }

  free(p_ChatId);
}

void IgNewGroupMembersNotify(int p_ConnId, char* p_ChatId, char* p_MembersJson)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    std::shared_ptr<NewGroupMembersNotify> notify =
      std::make_shared<NewGroupMembersNotify>(instance->GetProfileId());
    notify->chatId = std::string(p_ChatId);

    std::string json = std::string(p_MembersJson);
    notify->contactInfos = ProtocolUtil::ContactInfosFromJson(json);

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = notify;
    instance->SendRequest(deferNotifyRequest);
  }

  free(p_ChatId);
  free(p_MembersJson);
}

void IgNewMessagesNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text, int p_FromMe,
                         char* p_ReplyId, char* p_FileId, char* p_FilePath, int p_FileStatus, int p_TimeSent,
                         int p_IsRead, int p_IsEdited)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    ChatMessage chatMessage;
    chatMessage.id = std::string(p_MsgId);
    chatMessage.senderId = std::string(p_SenderId);
    chatMessage.text = std::string(p_Text);
    chatMessage.isOutgoing = (p_FromMe == 1);
    chatMessage.quotedId = std::string(p_ReplyId);
    chatMessage.timeSent = (((int64_t)p_TimeSent) * 1000) + (std::hash<std::string>{ }(chatMessage.id) % 256);
    chatMessage.isRead = (p_IsRead == 1);
    chatMessage.isEdited = (p_IsEdited == 1);

    if (p_IsEdited)
    {
      std::vector<ChatMessage> chatMessages;
      if (MessageCache::GetOneMessage(instance->GetProfileId(), std::string(p_ChatId), std::string(p_MsgId),
                                      chatMessages))
      {
        // retain original sent time and file info
        chatMessage.timeSent = chatMessages.at(0).timeSent;
        chatMessage.fileInfo = chatMessages.at(0).fileInfo;
      }
    }
    else
    {
      std::string fileId = std::string(p_FileId);
      if (!fileId.empty())
      {
        FileInfo fileInfo;
        fileInfo.fileStatus = (FileStatus)p_FileStatus;
        fileInfo.fileId = fileId;
        fileInfo.filePath = std::string(p_FilePath);
        chatMessage.fileInfo = ProtocolUtil::FileInfoToHex(fileInfo);
      }
    }

    std::shared_ptr<NewMessagesNotify> newMessagesNotify =
      std::make_shared<NewMessagesNotify>(instance->GetProfileId());
    newMessagesNotify->success = true;
    newMessagesNotify->chatId = std::string(p_ChatId);
    newMessagesNotify->chatMessages.push_back(chatMessage);
    newMessagesNotify->fromMsgId = "";
    newMessagesNotify->cached = false;
    newMessagesNotify->sequence = true;

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = newMessagesNotify;
    instance->SendRequest(deferNotifyRequest);
  }

  free(p_ChatId);
  free(p_MsgId);
  free(p_SenderId);
  free(p_Text);
  free(p_ReplyId);
  free(p_FileId);
  free(p_FilePath);
}

void IgNewHistoryMessagesNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text,
                                int p_FromMe, char* p_QuotedId, char* p_FileId, char* p_FilePath, int p_FileStatus,
                                int p_TimeSent, int p_IsRead, int p_IsEdited, char* p_FromMsgId, int p_Notify)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    std::shared_ptr<NewMessagesNotify> newMessagesNotify;

    if (p_Notify == NotifyCache)
    {
      ChatMessage chatMessage;
      chatMessage.id = std::string(p_MsgId);
      chatMessage.senderId = std::string(p_SenderId);
      chatMessage.text = std::string(p_Text);
      chatMessage.isOutgoing = (p_FromMe == 1);
      chatMessage.quotedId = std::string(p_QuotedId);
      chatMessage.timeSent = (((int64_t)p_TimeSent) * 1000) + (std::hash<std::string>{ }(chatMessage.id) % 256);
      chatMessage.isRead = (p_IsRead == 1);
      chatMessage.isEdited = (p_IsEdited == 1);

      std::string fileId = std::string(p_FileId);
      if (!fileId.empty())
      {
        FileInfo fileInfo;
        fileInfo.fileStatus = (FileStatus)p_FileStatus;
        fileInfo.fileId = fileId;
        fileInfo.filePath = std::string(p_FilePath);
        chatMessage.fileInfo = ProtocolUtil::FileInfoToHex(fileInfo);
      }

      instance->AddHistoryMessage(chatMessage);
    }
    else if (p_Notify == NotifySendCached)
    {
      ChatMessage chatMessage;
      chatMessage.id = std::string(p_MsgId);
      chatMessage.senderId = std::string(p_SenderId);
      chatMessage.text = std::string(p_Text);
      chatMessage.isOutgoing = (p_FromMe == 1);
      chatMessage.quotedId = std::string(p_QuotedId);
      chatMessage.timeSent = (((int64_t)p_TimeSent) * 1000) + (std::hash<std::string>{ }(chatMessage.id) % 256);
      chatMessage.isRead = (p_IsRead == 1);
      chatMessage.isEdited = (p_IsEdited == 1);

      std::string fileId = std::string(p_FileId);
      if (!fileId.empty())
      {
        FileInfo fileInfo;
        fileInfo.fileStatus = (FileStatus)p_FileStatus;
        fileInfo.fileId = fileId;
        fileInfo.filePath = std::string(p_FilePath);
        chatMessage.fileInfo = ProtocolUtil::FileInfoToHex(fileInfo);
      }

      if (!chatMessage.id.empty())
      {
        instance->AddHistoryMessage(chatMessage);
      }

      newMessagesNotify = std::make_shared<NewMessagesNotify>(instance->GetProfileId());
      newMessagesNotify->success = true;
      newMessagesNotify->chatId = std::string(p_ChatId);
      newMessagesNotify->chatMessages = instance->GetHistoryMessages();
      instance->ClearHistoryMessages();
      newMessagesNotify->fromMsgId = std::string(p_FromMsgId);
      newMessagesNotify->cached = false;
      newMessagesNotify->sequence = true;
    }

    if (newMessagesNotify)
    {
      std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
      deferNotifyRequest->serviceMessage = newMessagesNotify;
      instance->SendRequest(deferNotifyRequest);
    }
  }

  free(p_ChatId);
  free(p_MsgId);
  free(p_SenderId);
  free(p_Text);
  free(p_QuotedId);
  free(p_FileId);
  free(p_FilePath);
  free(p_FromMsgId);
}

void IgNewStatusNotify(int p_ConnId, char* p_UserId, int /*p_IsOnline*/, int /*p_TimeSeen*/)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    // presence status is not supported, but keep the interface for consistency
    (void)instance;
  }

  free(p_UserId);
}

void IgNewTypingNotify(int p_ConnId, char* p_ChatId, char* p_UserId, int p_IsTyping)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    std::string chatId(p_ChatId);
    std::string userId(p_UserId);

    if (!chatId.empty())
    {
      std::shared_ptr<ReceiveTypingNotify> receiveTypingNotify =
        std::make_shared<ReceiveTypingNotify>(instance->GetProfileId());
      receiveTypingNotify->chatId = chatId;
      receiveTypingNotify->userId = userId;
      receiveTypingNotify->isTyping = (p_IsTyping == 1);

      std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
      deferNotifyRequest->serviceMessage = receiveTypingNotify;
      instance->SendRequest(deferNotifyRequest);
    }
  }

  free(p_ChatId);
  free(p_UserId);
}

void IgNewMessageStatusNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, int p_IsRead)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    std::shared_ptr<NewMessageStatusNotify> newMessageStatusNotify =
      std::make_shared<NewMessageStatusNotify>(instance->GetProfileId());
    newMessageStatusNotify->chatId = std::string(p_ChatId);
    newMessageStatusNotify->msgId = std::string(p_MsgId);
    newMessageStatusNotify->isRead = (p_IsRead == 1);

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = newMessageStatusNotify;
    instance->SendRequest(deferNotifyRequest);
  }

  free(p_ChatId);
  free(p_MsgId);
}

void IgNewMessageFileNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_FileId, char* p_FilePath,
                            int p_FileStatus, int p_Action)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    FileInfo fileInfo;
    fileInfo.fileStatus = static_cast<FileStatus>(p_FileStatus);
    // retain file id (provided by the caller) so the attachment can be downloaded again if its
    // file is later removed, ex: when user clears the profile tmp dir between sessions.
    fileInfo.fileId = std::string(p_FileId);
    fileInfo.filePath = std::string(p_FilePath);

    std::shared_ptr<NewMessageFileNotify> newMessageFileNotify =
      std::make_shared<NewMessageFileNotify>(instance->GetProfileId());
    newMessageFileNotify->chatId = std::string(p_ChatId);
    newMessageFileNotify->msgId = std::string(p_MsgId);
    newMessageFileNotify->fileInfo = ProtocolUtil::FileInfoToHex(fileInfo);
    newMessageFileNotify->downloadFileAction = static_cast<DownloadFileAction>(p_Action);

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = newMessageFileNotify;
    instance->SendRequest(deferNotifyRequest);
  }

  free(p_ChatId);
  free(p_MsgId);
  free(p_FileId);
  free(p_FilePath);
}

void IgNewMessageReactionNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text,
                                int p_FromMe)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    const std::string senderId = (p_FromMe == 1) ? s_ReactionsSelfId : std::string(p_SenderId);

    Reactions reactions;
    // go via message cache for consolidation and count before handled by ui
    reactions.needConsolidationWithCache = true;
    reactions.updateCountBasedOnSender = true;
    reactions.replaceCount = false;
    reactions.senderEmojis[senderId] = std::string(p_Text);

    std::shared_ptr<NewMessageReactionsNotify> newMessageReactionsNotify =
      std::make_shared<NewMessageReactionsNotify>(instance->GetProfileId());
    newMessageReactionsNotify->chatId = std::string(p_ChatId);
    newMessageReactionsNotify->msgId = std::string(p_MsgId);
    newMessageReactionsNotify->reactions = reactions;

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = newMessageReactionsNotify;
    instance->SendRequest(deferNotifyRequest);
  }

  free(p_ChatId);
  free(p_MsgId);
  free(p_SenderId);
  free(p_Text);
}

void IgDeleteChatNotify(int p_ConnId, char* p_ChatId)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    std::shared_ptr<DeleteChatNotify> deleteChatNotify = std::make_shared<DeleteChatNotify>(instance->GetProfileId());
    deleteChatNotify->success = true;
    deleteChatNotify->chatId = std::string(p_ChatId);

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = deleteChatNotify;
    instance->SendRequest(deferNotifyRequest);
  }

  free(p_ChatId);
}

void IgMoveChatNotify(int p_ConnId, char* p_ChatId, char* p_NewChatId)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    std::shared_ptr<MoveChatNotify> moveChatNotify = std::make_shared<MoveChatNotify>(instance->GetProfileId());
    moveChatNotify->chatId = std::string(p_ChatId);
    moveChatNotify->newChatId = std::string(p_NewChatId);

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = moveChatNotify;
    instance->SendRequest(deferNotifyRequest);
  }

  free(p_ChatId);
  free(p_NewChatId);
}

void IgDeleteMessageNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, int p_IsOutgoing)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    static const int messageDelete = AppConfig::GetNum("message_delete");
    const bool isOutgoing = (p_IsOutgoing != 0);

    if (!isOutgoing &&
        ((messageDelete == MessageDeleteReplace) || (messageDelete == MessageDeletePrefix)))
    {
      std::vector<ChatMessage> chatMessages;
      if (MessageCache::GetOneMessage(instance->GetProfileId(), std::string(p_ChatId), std::string(p_MsgId),
                                      chatMessages))
      {
        ChatMessage chatMessage = chatMessages.front();
        chatMessage.isRead = true;
        chatMessage.isDeleted = true;

        if (messageDelete == MessageDeleteReplace)
        {
          chatMessage.text = std::string("[This message was deleted]");
          chatMessage.fileInfo.clear();
        }
        else // MessageDeletePrefix
        {
          if (!StrUtil::StartsWith(chatMessage.text, "[This message was deleted]"))
          {
            chatMessage.text = std::string("[This message was deleted]\n") + chatMessage.text;
          }
        }

        std::shared_ptr<NewMessagesNotify> newMessagesNotify =
          std::make_shared<NewMessagesNotify>(instance->GetProfileId());
        newMessagesNotify->success = true;
        newMessagesNotify->chatId = std::string(p_ChatId);
        newMessagesNotify->chatMessages = std::vector<ChatMessage>({ chatMessage });
        newMessagesNotify->cached = false;
        newMessagesNotify->sequence = true;

        std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
        deferNotifyRequest->serviceMessage = newMessagesNotify;
        instance->SendRequest(deferNotifyRequest);
      }
    }
    else // erase: mode 1, or any self-deletion
    {
      std::shared_ptr<DeleteMessageNotify> deleteMessageNotify =
        std::make_shared<DeleteMessageNotify>(instance->GetProfileId());
      deleteMessageNotify->success = true;
      deleteMessageNotify->chatId = std::string(p_ChatId);
      deleteMessageNotify->msgId = std::string(p_MsgId);
      deleteMessageNotify->isOutgoing = isOutgoing;

      std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
      deferNotifyRequest->serviceMessage = deleteMessageNotify;
      instance->SendRequest(deferNotifyRequest);
    }
  }

  free(p_ChatId);
  free(p_MsgId);
}

void IgUpdateMuteNotify(int p_ConnId, char* p_ChatId, int p_IsMuted)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    std::shared_ptr<UpdateMuteNotify> updateMuteNotify = std::make_shared<UpdateMuteNotify>(instance->GetProfileId());
    updateMuteNotify->success = true;
    updateMuteNotify->chatId = std::string(p_ChatId);
    updateMuteNotify->isMuted = (p_IsMuted == 1);

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = updateMuteNotify;
    instance->SendRequest(deferNotifyRequest);
  }

  free(p_ChatId);
}

void IgUpdateArchivedNotify(int p_ConnId, char* p_ChatId, int p_IsArchived)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    std::shared_ptr<UpdateArchivedNotify> updateArchivedNotify =
      std::make_shared<UpdateArchivedNotify>(instance->GetProfileId());
    updateArchivedNotify->success = true;
    updateArchivedNotify->chatId = std::string(p_ChatId);
    updateArchivedNotify->isArchived = p_IsArchived;

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = updateArchivedNotify;
    instance->SendRequest(deferNotifyRequest);
  }

  free(p_ChatId);
}

void IgUpdatePinNotify(int p_ConnId, char* p_ChatId, int p_IsPinned, int p_TimePinned)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    std::shared_ptr<UpdatePinNotify> updatePinNotify = std::make_shared<UpdatePinNotify>(instance->GetProfileId());
    updatePinNotify->success = true;
    updatePinNotify->chatId = std::string(p_ChatId);
    updatePinNotify->isPinned = (p_IsPinned == 1);
    updatePinNotify->timePinned = ((int64_t)p_TimePinned) * 1000;

    std::shared_ptr<DeferNotifyRequest> deferNotifyRequest = std::make_shared<DeferNotifyRequest>();
    deferNotifyRequest->serviceMessage = updatePinNotify;
    instance->SendRequest(deferNotifyRequest);
  }

  free(p_ChatId);
}

void IgReinit(int p_ConnId)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    std::shared_ptr<ReinitRequest> reinitRequest = std::make_shared<ReinitRequest>();
    instance->SendRequest(reinitRequest);
  }
}

void IgSetProtocolUiControl(int p_ConnId, int p_IsTakeControl)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    instance->SetProtocolUiControl(p_IsTakeControl == 1);
  }
}

void IgSetStatus(int p_ConnId, int p_Flags)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    instance->SetStatus(p_Flags);
  }
}

void IgClearStatus(int p_ConnId, int p_Flags)
{
  IgChat* instance = IgChat::GetInstance(p_ConnId);
  if (instance != nullptr)
  {
    instance->ClearStatus(p_Flags);
  }
}

int IgAppConfigGetNum(char* p_Param)
{
  int value = AppConfig::GetNum(std::string(p_Param));
  free(p_Param);
  return value;
}

void IgAppConfigSetNum(char* p_Param, int p_Value)
{
  AppConfig::SetBool(std::string(p_Param), (p_Value != 0));
  free(p_Param);
}

int IgHasGui()
{
  return SetupUtil::HasGui() ? 1 : 0;
}

void IgLogTrace(char* p_Filename, int p_LineNo, char* p_Message)
{
  Log::Trace(p_Filename, p_LineNo, "%s", p_Message);
  free(p_Filename);
  free(p_Message);
}

void IgLogDebug(char* p_Filename, int p_LineNo, char* p_Message)
{
  Log::Debug(p_Filename, p_LineNo, "%s", p_Message);
  free(p_Filename);
  free(p_Message);
}

void IgLogInfo(char* p_Filename, int p_LineNo, char* p_Message)
{
  Log::Info(p_Filename, p_LineNo, "%s", p_Message);
  free(p_Filename);
  free(p_Message);
}

void IgLogWarning(char* p_Filename, int p_LineNo, char* p_Message)
{
  Log::Warning(p_Filename, p_LineNo, "%s", p_Message);
  free(p_Filename);
  free(p_Message);
}

void IgLogError(char* p_Filename, int p_LineNo, char* p_Message)
{
  Log::Error(p_Filename, p_LineNo, "%s", p_Message);
  free(p_Filename);
  free(p_Message);
}
