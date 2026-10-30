// serve.cpp - JSON lines frontend; no terminal or UI configuration.
#include "serve.h"

#include <algorithm>
#include <cerrno>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <cereal/external/rapidjson/document.h>
#include <cereal/external/rapidjson/stringbuffer.h>
#include <cereal/external/rapidjson/writer.h>

#include "apputil.h"
#include "cacheutil.h"
#include "fileutil.h"
#include "log.h"
#include "messagecache.h"
#include "protocolutil.h"
#include "timeutil.h"

namespace json = CEREAL_RAPIDJSON_NAMESPACE;
using Value = json::Value;
using Allocator = json::Document::AllocatorType;

namespace
{
  volatile sig_atomic_t s_Terminated = 0;
  void Terminate(int p_Signal) { s_Terminated = p_Signal; }

  Value String(const std::string& p_Str, Allocator& p_Allocator)
  {
    return Value(p_Str.data(), p_Str.size(), p_Allocator);
  }

  void Put(Value& p_Object, const char* p_Key, const std::string& p_Value, Allocator& p_Allocator)
  {
    p_Object.AddMember(Value(p_Key, p_Allocator), String(p_Value, p_Allocator), p_Allocator);
  }

  void Put(Value& p_Object, const char* p_Key, bool p_Value, Allocator& p_Allocator)
  {
    p_Object.AddMember(Value(p_Key, p_Allocator), Value(p_Value), p_Allocator);
  }

  void Put(Value& p_Object, const char* p_Key, int64_t p_Value, Allocator& p_Allocator)
  {
    p_Object.AddMember(Value(p_Key, p_Allocator), Value(p_Value), p_Allocator);
  }

  Value File(const std::string& p_Hex, Allocator& p_Allocator)
  {
    if (p_Hex.empty()) return Value();
    const FileInfo info = ProtocolUtil::FileInfoFromHex(p_Hex);
    Value result(json::kObjectType);
    Put(result, "path", info.filePath, p_Allocator);
    Put(result, "status", int64_t(info.fileStatus), p_Allocator);
    Put(result, "id", info.fileId, p_Allocator);
    Put(result, "type", info.fileType, p_Allocator);
    return result;
  }

  Value ReactionsJson(const Reactions& p_Reactions, Allocator& p_Allocator)
  {
    Value result(json::kObjectType), senders(json::kObjectType), counts(json::kObjectType);
    for (const auto& entry : p_Reactions.senderEmojis)
    {
      senders.AddMember(String(entry.first, p_Allocator), String(entry.second, p_Allocator), p_Allocator);
    }
    for (const auto& entry : p_Reactions.emojiCounts)
    {
      counts.AddMember(String(entry.first, p_Allocator), Value(uint64_t(entry.second)), p_Allocator);
    }
    result.AddMember("sender_emojis", senders, p_Allocator);
    result.AddMember("emoji_counts", counts, p_Allocator);
    return result;
  }

  Value Contact(const ContactInfo& p_Contact, Allocator& p_Allocator)
  {
    Value result(json::kObjectType);
    Put(result, "id", p_Contact.id, p_Allocator);
    Put(result, "name", p_Contact.name, p_Allocator);
    Put(result, "phone", p_Contact.phone, p_Allocator);
    Put(result, "self", p_Contact.isSelf, p_Allocator);
    return result;
  }

  Value Message(const ChatMessage& p_Message, Allocator& p_Allocator)
  {
    Value result(json::kObjectType);
    Put(result, "id", p_Message.id, p_Allocator);
    Put(result, "sender", p_Message.senderId, p_Allocator);
    Put(result, "text", p_Message.text, p_Allocator);
    Put(result, "time", p_Message.timeSent, p_Allocator);
    Put(result, "outgoing", p_Message.isOutgoing, p_Allocator);
    Put(result, "read", p_Message.isRead, p_Allocator);
    Put(result, "edited", p_Message.isEdited, p_Allocator);
    Put(result, "deleted", p_Message.isDeleted, p_Allocator);
    Put(result, "pinned", p_Message.isPinned, p_Allocator);
    Value quoted;
    if (!p_Message.quotedId.empty())
    {
      quoted.SetObject();
      Put(quoted, "id", p_Message.quotedId, p_Allocator);
      Put(quoted, "text", p_Message.quotedText, p_Allocator);
      Put(quoted, "sender", p_Message.quotedSender, p_Allocator);
    }
    result.AddMember("quoted", quoted, p_Allocator);
    result.AddMember("file", File(p_Message.fileInfo, p_Allocator), p_Allocator);
    result.AddMember("reactions", ReactionsJson(p_Message.reactions, p_Allocator), p_Allocator);
    Value mentions(json::kObjectType);
    for (const auto& entry : p_Message.mentions)
    {
      mentions.AddMember(String(entry.first, p_Allocator), String(entry.second, p_Allocator), p_Allocator);
    }
    result.AddMember("mentions", mentions, p_Allocator);
    return result;
  }

  // Bound parser/destructor recursion even for malicious, deeply nested input.
  bool BoundedNesting(const std::string& p_Line)
  {
    size_t depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (char ch : p_Line)
    {
      if (quoted)
      {
        if (escaped) escaped = false;
        else if (ch == '\\') escaped = true;
        else if (ch == '"') quoted = false;
      }
      else if (ch == '"') quoted = true;
      else if ((ch == '{') || (ch == '['))
      {
        if (++depth > 64) return false;
      }
      else if (((ch == '}') || (ch == ']')) && depth) --depth;
    }
    return true;
  }

  std::string Field(const Value& p_Object, const char* p_Key)
  {
    auto it = p_Object.FindMember(p_Key);
    if (it == p_Object.MemberEnd()) throw std::runtime_error(std::string("missing field ") + p_Key);
    if (!it->value.IsString()) throw std::runtime_error(std::string("expected string field ") + p_Key);
    std::string result(it->value.GetString(), it->value.GetStringLength());
    if (result.find('\0') != std::string::npos) throw std::runtime_error(std::string("NUL in field ") + p_Key);
    return result;
  }

  bool BoolField(const Value& p_Object, const char* p_Key)
  {
    auto it = p_Object.FindMember(p_Key);
    if (it == p_Object.MemberEnd()) throw std::runtime_error(std::string("missing field ") + p_Key);
    if (!it->value.IsBool()) throw std::runtime_error(std::string("expected boolean field ") + p_Key);
    return it->value.GetBool();
  }
}

class Serve::Impl
{
public:
  Impl();
  ~Impl();
  void Write(const Value& p_Object);
  void Hello();
  void Enqueue(std::shared_ptr<ServiceMessage> p_Message);
  void Drain();
  void Event(std::shared_ptr<ServiceMessage> p_Message);
  void Request(const std::string& p_Line);
  void Run();
  ChatMessage GetMessage(const std::string& p_Profile, const std::string& p_Chat, const std::string& p_Id);

  int m_Input = -1;
  int m_Output = -1;
  int m_Error = -1;
  bool m_Running = true;
  std::mutex m_WriteMutex;
  std::mutex m_QueueMutex;
  std::deque<std::shared_ptr<ServiceMessage>> m_Queue;
  std::map<std::string, std::shared_ptr<Protocol>> m_Protocols;
  std::map<std::string, std::string> m_Names;
  std::map<std::string, std::string> m_Skipped;
  std::map<std::string, std::map<std::string, ContactInfo>> m_Contacts;
  std::map<std::string, std::map<std::string, std::map<std::string, ChatMessage>>> m_Messages;
  std::set<std::string> m_NeedsSetup;
  std::map<std::string, std::map<std::string, std::deque<std::string>>> m_CacheFrom;
};

Serve::Impl::Impl()
{
  struct sigaction action = { };
  action.sa_handler = Terminate;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_ONSTACK;
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);
  sigaction(SIGHUP, &action, nullptr);

  // Keep private protocol descriptors. Incidental library output cannot corrupt
  // JSON, and an authentication helper cannot consume client requests on stdin.
  m_Input = dup(STDIN_FILENO);
  m_Output = dup(STDOUT_FILENO);
  m_Error = dup(STDERR_FILENO);
  int nullFd = open("/dev/null", O_RDWR);
  if ((m_Input < 0) || (m_Output < 0) || (m_Error < 0) || (nullFd < 0))
    throw std::runtime_error("cannot open serve descriptors");
  fflush(stdout);
  if ((dup2(nullFd, STDIN_FILENO) < 0) || (dup2(nullFd, STDOUT_FILENO) < 0) ||
      (dup2(nullFd, STDERR_FILENO) < 0))
    throw std::runtime_error("cannot isolate serve descriptors");
  close(nullFd);
  signal(SIGPIPE, SIG_IGN);
}

Serve::Impl::~Impl()
{
  dup2(m_Error, STDERR_FILENO);
  close(m_Error);
  close(m_Input);
  close(m_Output);
}

void Serve::Impl::Write(const Value& p_Object)
{
  json::StringBuffer buffer;
  json::Writer<json::StringBuffer> writer(buffer);
  p_Object.Accept(writer);
  std::string line(buffer.GetString(), buffer.GetSize());
  line += '\n';
  std::lock_guard<std::mutex> lock(m_WriteMutex);
  size_t offset = 0;
  while (offset < line.size())
  {
    ssize_t count = write(m_Output, line.data() + offset, line.size() - offset);
    if ((count < 0) && (errno == EINTR) && !s_Terminated) continue;
    if (count <= 0) { m_Running = false; break; }
    offset += count;
  }
}

void Serve::Impl::Hello()
{
  json::Document result(json::kObjectType);
  auto& allocator = result.GetAllocator();
  result.AddMember("event", "hello", allocator);
  result.AddMember("version", 1, allocator);
  Put(result, "nchat", AppUtil::GetAppVersion(), allocator);
  Value profiles(json::kArrayType), skipped(json::kArrayType);
  const std::vector<std::pair<ProtocolFeature, std::string>> features =
  {
    { FeatureAutoGetChatsOnLogin, "auto_get_chats_on_login" },
    { FeatureTypingTimeout, "typing_timeout" },
    { FeatureEditMessagesWithinTwoDays, "edit_messages_within_two_days" },
    { FeatureEditMessagesWithinFifteenMins, "edit_messages_within_fifteen_mins" },
    { FeatureLimitedReactions, "limited_reactions" },
    { FeatureMarkReadEveryView, "mark_read_every_view" },
    { FeatureAutoGetContactsOnLogin, "auto_get_contacts_on_login" },
    { FeaturePinChat, "pin_chat" }, { FeatureArchiveChat, "archive_chat" },
    { FeaturePinMessage, "pin_message" },
    { FeaturePresenceRequiresOnline, "presence_requires_online" },
    { FeatureSendAcknowledgement, "send_acknowledgement" },
  };
  for (const auto& entry : m_Protocols)
  {
    Value profile(json::kObjectType), flags(json::kArrayType);
    Put(profile, "profile", entry.first, allocator);
    Put(profile, "protocol", m_Names.at(entry.first), allocator);
    Put(profile, "name", entry.second->GetProfileDisplayName(), allocator);
    Put(profile, "self", entry.second->GetSelfId(), allocator);
    for (const auto& feature : features)
    {
      if (entry.second->HasFeature(feature.first)) flags.PushBack(String(feature.second, allocator), allocator); }
    profile.AddMember("features", flags, allocator);
    profiles.PushBack(profile, allocator);
  }
  for (const auto& entry : m_Skipped)
  {
    Value profile(json::kObjectType);
    Put(profile, "profile", entry.first, allocator);
    Put(profile, "why", entry.second, allocator);
    skipped.PushBack(profile, allocator);
  }
  result.AddMember("profiles", profiles, allocator);
  result.AddMember("skipped", skipped, allocator);
  Write(result);
}

void Serve::Impl::Enqueue(std::shared_ptr<ServiceMessage> p_Message)
{
  // This notification is a synchronous negotiation, unlike other events.
  if (p_Message->GetMessageType() == ProtocolUiControlNotifyType)
    std::static_pointer_cast<ProtocolUiControlNotify>(p_Message)->isUiAvailable = false;
  std::lock_guard<std::mutex> lock(m_QueueMutex);
  m_Queue.push_back(p_Message);
}

void Serve::Impl::Drain()
{
  std::deque<std::shared_ptr<ServiceMessage>> queue;
  {
    std::lock_guard<std::mutex> lock(m_QueueMutex);
    queue.swap(m_Queue);
  }
  for (auto& message : queue)
  {
    Event(message);
  }
}

ChatMessage Serve::Impl::GetMessage(const std::string& p_Profile, const std::string& p_Chat,
                                    const std::string& p_Id)
{
  auto& messages = m_Messages[p_Profile][p_Chat];
  auto it = messages.find(p_Id);
  if (it != messages.end() && !it->second.id.empty()) return it->second;
  std::vector<ChatMessage> cached;
  if (MessageCache::GetOneMessage(p_Profile, p_Chat, p_Id, cached)) return cached.front();
  throw std::runtime_error("message metadata unavailable; get_messages or get_message first");
}

void Serve::Impl::Request(const std::string& p_Line)
{
  json::Document request;
  if (BoundedNesting(p_Line))
    request.Parse<json::kParseValidateEncodingFlag>(p_Line.data(), p_Line.size());
  json::Document reply(json::kObjectType);
  auto& allocator = reply.GetAllocator();
  Value id;
  if (!request.HasParseError() && request.IsObject() && request.HasMember("id"))
    id.CopyFrom(request["id"], allocator);
  reply.AddMember("id", id, allocator);
  try
  {
    if (request.HasParseError() || !request.IsObject() || p_Line.find('\0') != std::string::npos)
      throw std::runtime_error("expected JSON object");
    if (!request.HasMember("id")) throw std::runtime_error("missing field id");
    if (!request["id"].IsUint64()) throw std::runtime_error("id must be an unsigned 64-bit integer");
    std::string op = Field(request, "op");
    if (op == "quit")
    {
      m_Running = false;
    }
    else
    {
      static const std::set<std::string> ops =
      {
        "get_chats", "get_contacts", "get_messages", "get_message", "set_current_chat", "send_message",
        "edit_message", "delete_message", "mark_read", "send_typing", "send_reaction", "download_file",
        "get_members", "pin_chat", "archive_chat",
      };
      if (!ops.count(op)) throw std::runtime_error("unknown op " + op);
      std::string profile = Field(request, "profile");
      auto it = m_Protocols.find(profile);
      if (it == m_Protocols.end()) throw std::runtime_error("no profile " + profile);
      if (m_NeedsSetup.count(profile)) throw std::runtime_error("profile needs setup " + profile);
      std::shared_ptr<RequestMessage> message;
      if (op == "get_chats") message = std::make_shared<GetChatsRequest>();
      else if (op == "get_contacts") message = std::make_shared<GetContactsRequest>();
      else
      {
        std::string chat = Field(request, "chat");
        if (op == "get_messages")
        {
          auto msg = std::make_shared<GetMessagesRequest>();
          msg->chatId = chat;
          msg->fromMsgId = Field(request, "from");
          if (!request.HasMember("limit")) throw std::runtime_error("missing field limit");
          if (!request["limit"].IsInt() || request["limit"].GetInt() < 1 || request["limit"].GetInt() > 1000)
            throw std::runtime_error("limit must be an integer between 1 and 1000");
          msg->limit = request["limit"].GetInt();
          if (!MessageCache::FetchMessagesFrom(profile, chat, msg->fromMsgId, msg->limit, false)) message = msg;
          else m_CacheFrom[profile][chat].push_back(msg->fromMsgId);
        }
        else if (op == "get_message")
        {
          auto msg = std::make_shared<GetMessageRequest>();
          msg->chatId = chat; msg->msgId = Field(request, "message");
          if (!MessageCache::FetchOneMessage(profile, chat, msg->msgId, false)) message = msg;
          else m_CacheFrom[profile][chat].push_back("");
        }
        else if (op == "set_current_chat")
        {
          auto msg = std::make_shared<SetCurrentChatRequest>(); msg->chatId = chat; message = msg;
        }
        else if (op == "send_message")
        {
          auto msg = std::make_shared<SendMessageRequest>();
          msg->chatId = chat; msg->clientRequestId = request["id"].GetUint64();
          msg->chatMessage.text = Field(request, "text");
          msg->chatMessage.timeSent = TimeUtil::GetCurrentTimeMSec();
          msg->chatMessage.senderId = it->second->GetSelfId();
          if (request.HasMember("quoted"))
          {
            msg->chatMessage.quotedId = Field(request, "quoted");
            if (!msg->chatMessage.quotedId.empty())
            {
              auto quoted = GetMessage(profile, chat, msg->chatMessage.quotedId);
              msg->chatMessage.quotedText = quoted.text; msg->chatMessage.quotedSender = quoted.senderId;
            }
          }
          if (request.HasMember("file"))
          {
            FileInfo file;
            file.filePath = Field(request, "file");
            if (file.filePath.empty() || !FileUtil::Exists(file.filePath))
              throw std::runtime_error("file does not exist");
            file.fileType = FileUtil::GetMimeType(file.filePath);
            msg->chatMessage.fileInfo = ProtocolUtil::FileInfoToHex(file);
          }
          message = msg;
        }
        else if (op == "send_typing")
        {
          auto msg = std::make_shared<SendTypingRequest>();
          msg->chatId = chat; msg->isTyping = BoolField(request, "typing"); message = msg;
        }
        else if (op == "get_members")
        {
          auto msg = std::make_shared<GetGroupMembersRequest>(); msg->chatId = chat; message = msg;
        }
        else if (op == "pin_chat")
        {
          auto msg = std::make_shared<PinChatRequest>();
          msg->chatId = chat; msg->isPinned = BoolField(request, "on"); message = msg;
        }
        else if (op == "archive_chat")
        {
          auto msg = std::make_shared<ArchiveChatRequest>();
          msg->chatId = chat; msg->isArchived = BoolField(request, "on"); message = msg;
        }
        else
        {
          std::string messageId = Field(request, "message");
          if (op == "edit_message")
          {
            auto msg = std::make_shared<EditMessageRequest>();
            msg->chatId = chat; msg->msgId = messageId;
            const std::string text = Field(request, "text");
            msg->chatMessage = GetMessage(profile, chat, messageId);
            msg->chatMessage.text = text; message = msg;
          }
          else if (op == "delete_message")
          {
            auto msg = std::make_shared<DeleteMessageRequest>();
            msg->chatId = chat; msg->msgId = messageId;
            if (m_Names.at(profile) == "WhatsAppMd") msg->senderId = GetMessage(profile, chat, messageId).senderId;
            message = msg;
          }
          else if (op == "mark_read")
          {
            auto msg = std::make_shared<MarkMessageReadRequest>();
            msg->chatId = chat; msg->msgId = messageId;
            if (m_Names.at(profile) == "WhatsAppMd") msg->senderId = GetMessage(profile, chat, messageId).senderId;
            message = msg;
          }
          else if (op == "download_file")
          {
            auto msg = std::make_shared<DownloadFileRequest>();
            msg->chatId = chat; msg->msgId = messageId;
            auto original = GetMessage(profile, chat, messageId);
            if (original.fileInfo.empty()) throw std::runtime_error("message has no file");
            msg->fileId = ProtocolUtil::FileInfoFromHex(original.fileInfo).fileId; message = msg;
          }
          else if (op == "send_reaction")
          {
            auto msg = std::make_shared<SendReactionRequest>();
            msg->chatId = chat; msg->msgId = messageId; msg->emoji = Field(request, "emoji");
            auto original = GetMessage(profile, chat, messageId);
            msg->senderId = original.senderId;
            const auto& emojis = original.reactions.senderEmojis;
            auto previous = emojis.find(s_ReactionsSelfId);
            if (previous != emojis.end()) msg->prevEmoji = previous->second;
            message = msg;
          }
        }
      }
      if (message) it->second->SendRequest(message);
    }
    reply.AddMember("ok", true, allocator);
  }
  catch (const std::exception& error)
  {
    reply.AddMember("ok", false, allocator);
    Put(reply, "error", std::string(error.what()), allocator);
  }
  Write(reply);
}

void Serve::Impl::Event(std::shared_ptr<ServiceMessage> p_Message)
{
  const std::string& profile = p_Message->profileId;
  json::Document result(json::kObjectType);
  auto& allocator = result.GetAllocator();
  Put(result, "profile", profile, allocator);
  switch (p_Message->GetMessageType())
  {
    case ConnectNotifyType:
      {
        auto msg = std::static_pointer_cast<ConnectNotify>(p_Message);
        result.AddMember("event", "connected", allocator);
        Put(result, "ok", msg->success, allocator);
      }
      break;
    case NewContactsNotifyType:
      {
        auto msg = std::static_pointer_cast<NewContactsNotify>(p_Message);
        result.AddMember("event", "contacts", allocator);
        Value contacts(json::kArrayType);
        if (msg->fullSync) m_Contacts[profile].clear();
        for (const auto& contact : msg->contactInfos)
        {
          m_Contacts[profile][contact.id] = contact;
          contacts.PushBack(Contact(contact, allocator), allocator);
        }
        result.AddMember("contacts", contacts, allocator);
      }
      break;
    case NewChatsNotifyType:
      {
        auto msg = std::static_pointer_cast<NewChatsNotify>(p_Message);
        result.AddMember("event", "chats", allocator);
        Value chats(json::kArrayType);
        Put(result, "ok", msg->success, allocator);
        for (const auto& chat : msg->chatInfos)
        {
          const auto& contact = m_Contacts[profile][chat.id];
          Value item(json::kObjectType);
          Put(item, "id", chat.id, allocator);
          Put(item, "name", contact.isSelf ? std::string("You") :
              (contact.name.empty() ? chat.id : contact.name), allocator);
          Put(item, "unread", chat.isUnread, allocator);
          Put(item, "unread_mention", chat.isUnreadMention, allocator);
          Put(item, "muted", chat.isMuted, allocator);
          Put(item, "pinned", chat.isPinned, allocator);
          Put(item, "archived", chat.isArchived, allocator);
          Put(item, "last_message_time", chat.lastMessageTime, allocator);
          chats.PushBack(item, allocator);
        }
        result.AddMember("chats", chats, allocator);
      }
      break;
    case NewMessagesNotifyType:
      {
        auto msg = std::static_pointer_cast<NewMessagesNotify>(p_Message);
        result.AddMember("event", "messages", allocator);
        Put(result, "chat", msg->chatId, allocator);
        auto& origins = m_CacheFrom[profile][msg->chatId];
        std::string from = msg->fromMsgId;
        if (msg->cached && !origins.empty()) { from = origins.front(); origins.pop_front(); }
        Put(result, "from", from, allocator);
        Put(result, "ok", msg->success, allocator);
        Put(result, "cached", msg->cached, allocator);
        Value messages(json::kArrayType);
        for (auto message : msg->chatMessages)
        {
          auto& previous = m_Messages[profile][msg->chatId][message.id];
          if (message.reactions.needConsolidationWithCache)
          {
            CacheUtil::UpdateReactions(previous.reactions, message.reactions);
          }
          previous = message;
          messages.PushBack(Message(message, allocator), allocator);
        }
        result.AddMember("messages", messages, allocator);
      }
      break;
    case SendMessageNotifyType:
      {
        auto msg = std::static_pointer_cast<SendMessageNotify>(p_Message);
        result.AddMember("event", "sent", allocator);
        Put(result, "chat", msg->chatId, allocator);
        Put(result, "ok", msg->success, allocator);
        auto protocol = m_Protocols.find(profile);
        if (protocol != m_Protocols.end() && protocol->second->HasFeature(FeatureSendAcknowledgement))
          result.AddMember("request", Value(msg->clientRequestId), allocator);
        if (msg->success || !msg->chatMessage.id.empty())
        {
          result.AddMember("message", Message(msg->chatMessage, allocator), allocator);
          if (!msg->chatMessage.id.empty()) m_Messages[profile][msg->chatId][msg->chatMessage.id] = msg->chatMessage;
        }
      }
      break;
    case NewMessageStatusNotifyType:
      {
        auto msg = std::static_pointer_cast<NewMessageStatusNotify>(p_Message);
        result.AddMember("event", "message_status", allocator);
        Put(result, "chat", msg->chatId, allocator);
        Put(result, "message", msg->msgId, allocator);
        Put(result, "read", msg->isRead, allocator);
        m_Messages[profile][msg->chatId][msg->msgId].isRead = msg->isRead;
      }
      break;
    case NewMessageFileNotifyType:
      {
        auto msg = std::static_pointer_cast<NewMessageFileNotify>(p_Message);
        result.AddMember("event", "message_file", allocator);
        Put(result, "chat", msg->chatId, allocator);
        Put(result, "message", msg->msgId, allocator);
        result.AddMember("file", File(msg->fileInfo, allocator), allocator);
        m_Messages[profile][msg->chatId][msg->msgId].fileInfo = msg->fileInfo;
      }
      break;
    case DeleteMessageNotifyType:
      {
        auto msg = std::static_pointer_cast<DeleteMessageNotify>(p_Message);
        result.AddMember("event", "deleted", allocator);
        Put(result, "chat", msg->chatId, allocator);
        Put(result, "message", msg->msgId, allocator);
        Put(result, "ok", msg->success, allocator);
        if (msg->success) m_Messages[profile][msg->chatId].erase(msg->msgId);
      }
      break;
    case DeleteChatNotifyType:
      {
        auto msg = std::static_pointer_cast<DeleteChatNotify>(p_Message);
        result.AddMember("event", "deleted", allocator);
        Put(result, "chat", msg->chatId, allocator);
        Put(result, "ok", msg->success, allocator);
        if (msg->success) { m_Messages[profile].erase(msg->chatId); }
      }
      break;
    case ReceiveTypingNotifyType:
      {
        auto msg = std::static_pointer_cast<ReceiveTypingNotify>(p_Message);
        result.AddMember("event", "typing", allocator);
        Put(result, "chat", msg->chatId, allocator);
        Put(result, "user", msg->userId, allocator);
        Put(result, "typing", msg->isTyping, allocator);
      }
      break;
    case ReceiveStatusNotifyType:
      {
        auto msg = std::static_pointer_cast<ReceiveStatusNotify>(p_Message);
        result.AddMember("event", "status", allocator);
        Put(result, "user", msg->userId, allocator);
        Put(result, "online", msg->isOnline, allocator);
        Put(result, "last_seen", msg->timeSeen, allocator);
      }
      break;
    case NewMessageReactionsNotifyType:
      {
        auto msg = std::static_pointer_cast<NewMessageReactionsNotify>(p_Message);
        result.AddMember("event", "reactions", allocator);
        Put(result, "chat", msg->chatId, allocator);
        Put(result, "message", msg->msgId, allocator);
        auto& reactions = m_Messages[profile][msg->chatId][msg->msgId].reactions;
        Reactions update = msg->reactions;
        CacheUtil::UpdateReactions(reactions, update);
        reactions = update;
        result.AddMember("reactions", ReactionsJson(reactions, allocator), allocator);
      }
      break;
    case NewMessageIsPinnedNotifyType:
      {
        auto msg = std::static_pointer_cast<NewMessageIsPinnedNotify>(p_Message);
        result.AddMember("event", "pinned", allocator);
        Put(result, "chat", msg->chatId, allocator);
        Put(result, "message", msg->msgId, allocator);
        Put(result, "on", msg->isPinned, allocator);
        m_Messages[profile][msg->chatId][msg->msgId].isPinned = msg->isPinned;
      }
      break;
    case UpdatePinNotifyType:
      {
        auto msg = std::static_pointer_cast<UpdatePinNotify>(p_Message);
        result.AddMember("event", "pinned", allocator);
        Put(result, "chat", msg->chatId, allocator);
        Put(result, "on", msg->isPinned, allocator);
        Put(result, "ok", msg->success, allocator);
      }
      break;
    case UpdateMuteNotifyType:
      {
        auto msg = std::static_pointer_cast<UpdateMuteNotify>(p_Message);
        result.AddMember("event", "muted", allocator);
        Put(result, "chat", msg->chatId, allocator);
        Put(result, "on", msg->isMuted, allocator);
        Put(result, "ok", msg->success, allocator);
      }
      break;
    case UpdateArchivedNotifyType:
      {
        auto msg = std::static_pointer_cast<UpdateArchivedNotify>(p_Message);
        result.AddMember("event", "archived", allocator);
        Put(result, "chat", msg->chatId, allocator);
        Put(result, "on", msg->isArchived, allocator);
        Put(result, "ok", msg->success, allocator);
      }
      break;
    case NewGroupMembersNotifyType:
      {
        auto msg = std::static_pointer_cast<NewGroupMembersNotify>(p_Message);
        result.AddMember("event", "members", allocator);
        Put(result, "chat", msg->chatId, allocator);
        Value members(json::kArrayType);
        for (const auto& member : msg->contactInfos)
        {
          members.PushBack(Contact(member, allocator), allocator);
        }
        result.AddMember("members", members, allocator);
      }
      break;
    case ProtocolUiControlNotifyType:
      {
        auto msg = std::static_pointer_cast<ProtocolUiControlNotify>(p_Message);
        result.AddMember("event", "needs_setup", allocator);
        m_NeedsSetup.insert(profile);
        (void)msg;
      }
      break;
    case RequestAppExitNotifyType:
      {
        auto msg = std::static_pointer_cast<RequestAppExitNotify>(p_Message);
        result.AddMember("event", "exit", allocator);
        // A profile asking to exit must not disconnect unrelated profiles.
        (void)msg;
      }
      break;
    default:
      result.AddMember("event", "other", allocator);
      Put(result, "type", int64_t(p_Message->GetMessageType()), allocator);
      break;
  }
  Write(result);
}

void Serve::Impl::Run()
{
  Hello();
  auto handler = [this](std::shared_ptr<ServiceMessage> message) { Enqueue(message); };
  MessageCache::SetMessageHandler(handler);
  for (auto& entry : m_Protocols)
  {
    const bool dummy = (m_Names.at(entry.first) == "Dummy");
    entry.second->SetMessageHandler([this, dummy](std::shared_ptr<ServiceMessage> message)
    {
      // Real protocols maintain their cache in CallMessageHandler; Dummy does not.
      if (dummy) MessageCache::AddFromServiceMessage(message->profileId, message);
      Enqueue(message);
    });
  }
  std::thread login([this]
  {
    for (auto& entry : m_Protocols)
    {
      if (s_Terminated) break;
      entry.second->Login();
    }
  });
  for (auto& entry : m_Protocols)
  {
    MessageCache::FetchContacts(entry.first);
  }

  std::string pending;
  bool oversized = false;
  // Bound malformed input independently of JSON parsing (including a line with
  // no newline). One error reply is produced once the offending line ends.
  const size_t maxLine = 1024 * 1024;
  while (m_Running && !s_Terminated)
  {
    Drain();
    pollfd input = { m_Input, POLLIN, 0 };
    int ready = poll(&input, 1, 50);
    if ((ready < 0) && (errno == EINTR)) continue;
    if (ready < 0) break;
    if (ready == 0) continue;
    char buffer[4096];
    ssize_t count = read(m_Input, buffer, sizeof(buffer));
    if ((count < 0) && (errno == EINTR)) continue;
    if (count <= 0)
    {
      if (m_Running && (oversized || !pending.empty())) Request(oversized ? std::string() : pending);
      break;
    }
    for (ssize_t i = 0; (i < count) && m_Running && !s_Terminated; ++i)
    {
      if (buffer[i] == '\n')
      {
        Request(oversized ? std::string() : pending);
        pending.clear(); oversized = false;
        Drain();
      }
      else if (!oversized)
      {
        if (pending.size() >= maxLine) { pending.clear(); oversized = true; }
        else pending += buffer[i];
      }
    }
  }
  if (login.joinable()) login.join();
  for (auto& entry : m_Protocols)
  {
    entry.second->Logout();
    entry.second->SetMessageHandler(nullptr);
    entry.second->CloseProfile();
  }
  // Join the cache before destroying this handler or any of its captured state.
  MessageCache::Cleanup();
  Drain();
}

Serve::Serve()
  : m_Impl(new Impl()) { }
Serve::~Serve() = default;

void Serve::AddProtocol(const std::string& p_Name, std::shared_ptr<Protocol> p_Protocol)
{
  if (p_Name == "Dummy")
    MessageCache::AddProfile(p_Protocol->GetProfileId(), false, 1, false, false);
  m_Impl->m_Names[p_Protocol->GetProfileId()] = p_Name;
  m_Impl->m_Protocols[p_Protocol->GetProfileId()] = p_Protocol;
}

void Serve::AddSkipped(const std::string& p_Profile, const std::string& p_Why)
{
  m_Impl->m_Skipped[p_Profile] = p_Why;
}

void Serve::Run() { m_Impl->Run(); }
