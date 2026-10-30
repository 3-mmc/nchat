// Deterministic protocol peer for exercising messages Dummy does not emit.
#include "serve.h"
#include "appconfig.h"
#include "fileutil.h"
#include "log.h"
#include "messagecache.h"
#include "protocolutil.h"

#include <iostream>
#include <stdexcept>

class Fixture : public Protocol
{
public:
  explicit Fixture(const std::string& p_Id)
    : m_Id(p_Id) { }
  std::string GetProfileId() const override { return m_Id; }
  std::string GetProfileDisplayName() const override { return "Fixture"; }
  std::string GetSelfId() const override { return "self"; }
  bool HasFeature(ProtocolFeature) const override { return true; }
  bool IsGroupChat(const std::string&) const override { return true; }
  bool SetupProfile(const std::string&, std::string&) override { return false; }
  bool LoadProfile(const std::string&, const std::string&) override { return true; }
  bool CloseProfile() override { return true; }
  bool Logout() override { return true; }
  bool Login() override
  {
    // Deliberate incidental C and C++ output must not escape onto the wire.
    std::cout << "library diagnostic\n";
    printf("another diagnostic\n");
    std::cerr << "library warning\n";
    if (m_Id == "Fixture_2")
    {
      auto control = std::make_shared<ProtocolUiControlNotify>(m_Id);
      control->isTakeControl = true;
      Emit(control);
      if (control->isUiAvailable) throw std::runtime_error("interactive access was not denied");
    }
    return true;
  }
  void SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_Handler) override
  { m_Handler = p_Handler; }

  void Emit(std::shared_ptr<ServiceMessage> p_Message)
  {
    MessageCache::AddFromServiceMessage(m_Id, p_Message);
    if (m_Handler) m_Handler(p_Message);
  }

  void SendRequest(std::shared_ptr<RequestMessage> p_Request) override
  {
    switch (p_Request->GetMessageType())
    {
      case GetChatsRequestType:
        {
          auto contacts = std::make_shared<NewContactsNotify>(m_Id);
          ContactInfo contact; contact.id = "chat"; contact.name = "Chat name"; contact.phone = "123";
          contacts->contactInfos.push_back(contact); Emit(contacts);
          auto chats = std::make_shared<NewChatsNotify>(m_Id);
          ChatInfo chat; chat.id = "chat"; chat.isUnread = true; chat.lastMessageTime = 1700000000123;
          chats->chatInfos.push_back(chat); chats->success = true; Emit(chats);
          break;
        }
      case GetMessagesRequestType:
      case GetMessageRequestType:
        {
          auto messages = std::make_shared<NewMessagesNotify>(m_Id);
          messages->success = true; messages->sequence = true; messages->chatId = "chat";
          if (p_Request->GetMessageType() == GetMessagesRequestType)
            messages->fromMsgId = std::static_pointer_cast<GetMessagesRequest>(p_Request)->fromMsgId;
          ChatMessage msg; msg.id = "message"; msg.senderId = "sender"; msg.text = "original";
          msg.timeSent = 1700000000123; msg.quotedId = "q"; msg.quotedText = "quote";
          msg.quotedSender = "quoted sender";
          msg.mentions["Name"] = "user"; msg.reactions.senderEmojis["You"] = "old";
          msg.reactions.emojiCounts["old"] = 1;
          FileInfo file; file.fileId = "download-id"; file.fileStatus = FileStatusNotDownloaded;
          file.fileType = "text/plain"; msg.fileInfo = ProtocolUtil::FileInfoToHex(file);
          messages->chatMessages.push_back(msg);
          msg.id = "older"; msg.timeSent -= 1000;
          messages->chatMessages.push_back(msg); Emit(messages);
          break;
        }
      case SendMessageRequestType:
        {
          auto request = std::static_pointer_cast<SendMessageRequest>(p_Request);
          auto sent = std::make_shared<SendMessageNotify>(m_Id);
          sent->success = true; sent->chatId = request->chatId;
          sent->chatMessage = request->chatMessage; sent->clientRequestId = request->clientRequestId;
          Emit(sent); break;
        }
      case EditMessageRequestType:
        {
          auto request = std::static_pointer_cast<EditMessageRequest>(p_Request);
          if (request->chatMessage.senderId != "sender" || request->chatMessage.fileInfo.empty())
            throw std::runtime_error("edit lost metadata");
          auto messages = std::make_shared<NewMessagesNotify>(m_Id);
          messages->success = true; messages->chatId = request->chatId;
          auto message = request->chatMessage; message.isEdited = true;
          messages->chatMessages.push_back(message); Emit(messages); break;
        }
      case DownloadFileRequestType:
        {
          auto request = std::static_pointer_cast<DownloadFileRequest>(p_Request);
          if (request->fileId != "download-id") throw std::runtime_error("wrong file id");
          auto event = std::make_shared<NewMessageFileNotify>(m_Id);
          event->chatId = request->chatId; event->msgId = request->msgId;
          FileInfo file; file.filePath = "/tmp/download"; file.fileStatus = FileStatusDownloaded;
          event->fileInfo = ProtocolUtil::FileInfoToHex(file); Emit(event); break;
        }
      case SendReactionRequestType:
        {
          auto request = std::static_pointer_cast<SendReactionRequest>(p_Request);
          if (request->senderId != "sender" || request->prevEmoji != "old")
            throw std::runtime_error("wrong reaction metadata");
          auto event = std::make_shared<NewMessageReactionsNotify>(m_Id);
          event->chatId = request->chatId; event->msgId = request->msgId;
          event->reactions.senderEmojis["You"] = request->emoji;
          event->reactions.updateCountBasedOnSender = true; Emit(event); break;
        }
      case GetGroupMembersRequestType:
        {
          auto event = std::make_shared<NewGroupMembersNotify>(m_Id);
          event->chatId = std::static_pointer_cast<GetGroupMembersRequest>(p_Request)->chatId;
          ContactInfo member; member.id = "user"; member.name = "Member";
          event->contactInfos.push_back(member); Emit(event); break;
        }
      case SendTypingRequestType:
        {
          auto request = std::static_pointer_cast<SendTypingRequest>(p_Request);
          auto event = std::make_shared<ReceiveTypingNotify>(m_Id);
          event->chatId = request->chatId; event->userId = "user"; event->isTyping = request->isTyping;
          Emit(event); break;
        }
      case PinChatRequestType:
        {
          auto request = std::static_pointer_cast<PinChatRequest>(p_Request);
          auto event = std::make_shared<UpdatePinNotify>(m_Id);
          event->success = true; event->chatId = request->chatId; event->isPinned = request->isPinned;
          Emit(event); break;
        }
      case ArchiveChatRequestType:
        {
          auto request = std::static_pointer_cast<ArchiveChatRequest>(p_Request);
          auto event = std::make_shared<UpdateArchivedNotify>(m_Id);
          event->success = true; event->chatId = request->chatId; event->isArchived = request->isArchived;
          Emit(event); break;
        }
      case MarkMessageReadRequestType:
        {
          auto request = std::static_pointer_cast<MarkMessageReadRequest>(p_Request);
          auto event = std::make_shared<NewMessageStatusNotify>(m_Id);
          event->chatId = request->chatId; event->msgId = request->msgId; event->isRead = true;
          Emit(event); break;
        }
      case DeleteMessageRequestType:
        {
          auto request = std::static_pointer_cast<DeleteMessageRequest>(p_Request);
          auto event = std::make_shared<DeleteMessageNotify>(m_Id);
          event->chatId = request->chatId; event->msgId = request->msgId; event->success = true;
          Emit(event); break;
        }
      case SetCurrentChatRequestType:
        if (std::static_pointer_cast<SetCurrentChatRequest>(p_Request)->chatId != "chat")
          throw std::runtime_error("wrong current chat");
        break;
      case GetContactsRequestType:
        {
          auto status = std::make_shared<ReceiveStatusNotify>(m_Id);
          status->userId = "user"; status->timeSeen = 1700000000123; Emit(status);
          auto mute = std::make_shared<UpdateMuteNotify>(m_Id);
          mute->chatId = "chat"; mute->success = true; mute->isMuted = true; Emit(mute);
          auto pin = std::make_shared<NewMessageIsPinnedNotify>(m_Id);
          pin->chatId = "chat"; pin->msgId = "message"; pin->isPinned = true; Emit(pin);
          auto deletion = std::make_shared<DeleteChatNotify>(m_Id);
          deletion->chatId = "other-chat"; deletion->success = true; Emit(deletion);
          Emit(std::make_shared<RequestAppExitNotify>(m_Id));
          Emit(std::make_shared<ServiceMessage>(m_Id));
          break;
        }
      default:
        throw std::runtime_error("unexpected request type");
    }
  }
private:
  std::string m_Id;
  std::function<void(std::shared_ptr<ServiceMessage>)> m_Handler;
};

int main(int argc, char** argv)
{
  if (argc != 4) return 1;
  FileUtil::SetApplicationDir(argv[3]);
  Log::Init(std::string(argv[3]) + "/log.txt");
  AppConfig::Init();
  MessageCache::Init();
  Serve server;
  for (const auto& id : { "Fixture_1", "Fixture_2" })
  {
    MessageCache::AddProfile(id, false, 1, false, false);
    server.AddProtocol("Fixture", std::make_shared<Fixture>(id));
  }
  server.Run();
  AppConfig::Cleanup();
  Log::Cleanup(false);
  return 0;
}
