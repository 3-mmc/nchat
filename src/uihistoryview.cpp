// uihistoryview.cpp
//
// Copyright (c) 2019-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uihistoryview.h"

#include <algorithm>
#include <cstdlib>

#include "appconfig.h"
#include "apputil.h"
#include "fileutil.h"
#include "log.h"
#include "protocolutil.h"
#include "strutil.h"
#include "timeutil.h"
#include "uicolorconfig.h"
#include "uiaudioplayer.h"
#include "uiconfig.h"
#include "uiimagepreview.h"
#include "uimodel.h"

UiHistoryView::UiHistoryView(const UiViewParams& p_Params, Pane p_Pane)
  : UiViewBase(p_Params)
  , m_Pane(p_Pane)
{
  if (m_Enabled)
  {
    int hpad = (m_X == 0) ? 0 : 1;
    int vpad = 1;
    int paddedY = m_Y + vpad;
    int paddedX = m_X + hpad;
    m_PaddedH = m_H - (vpad * 2);
    m_PaddedW = m_W - (hpad * 2);
    m_PaddedWin = newwin(m_PaddedH, m_PaddedW, paddedY, paddedX);

    static int attributeTextNormal = UiColorConfig::GetAttribute("history_text_attr");
    static int colorPairTextRecv = UiColorConfig::GetColorPair("history_text_recv_color");
    werase(m_Win);
    wbkgd(m_Win, attributeTextNormal | colorPairTextRecv | ' ');
    wrefresh(m_Win);
  }
}

UiHistoryView::~UiHistoryView()
{
  UiPixelGraphics::Clear(this);
  if (m_PaddedWin != nullptr)
  {
    delwin(m_PaddedWin);
    m_PaddedWin = nullptr;
  }
}

void UiHistoryView::Draw()
{
  const uint64_t revision = UiPixelGraphics::Revision();
  if (m_GraphicsRevision != revision) m_Dirty = true;
  m_GraphicsRevision = revision;
  if (!m_Enabled || !m_Dirty) return;
  m_Dirty = false;
  if (UiPixelGraphics::Clear(this)) redrawwin(m_PaddedWin);
  std::vector<UiPixelPlacement> placements;

  curs_set(0);

  static int colorPairTextSent = UiColorConfig::GetColorPair("history_text_sent_color");
  static int colorPairTextRecv = UiColorConfig::GetColorPair("history_text_recv_color");
  static int colorPairTextQuoted = UiColorConfig::GetColorPair("history_text_quoted_color");
  static int colorPairTextReaction = UiColorConfig::GetColorPair("history_text_reaction_color");
  static int colorPairTextAttachment = UiColorConfig::GetColorPair("history_text_attachment_color");
  static int attributeTextNormal = UiColorConfig::GetAttribute("history_text_attr");
  static int attributeTextSelected = UiColorConfig::GetAttribute("history_text_attr_selected");

  static int colorPairNameSent = UiColorConfig::GetColorPair("history_name_sent_color");
  static int colorPairNameRecv = UiColorConfig::GetColorPair("history_name_recv_color");
  static int attributeNameNormal = UiColorConfig::GetAttribute("history_name_attr");
  static int attributeNameSelected = UiColorConfig::GetAttribute("history_name_attr_selected");

  static std::wstring attachmentIndicator =
    StrUtil::ToWString(UiConfig::GetStr("attachment_indicator") + " ");
  static std::wstring quoteIndicator = L"> ";
  static std::wstring imageRowIndicator = L"\uF8FF"; // placeholder line for an inline image row

  // Selection and scrolling follow focus; F6 makes the other pane active.
  const bool isSplitPane = (m_Pane == PaneSplit);
  std::pair<std::string, std::string> currentChat =
    isSplitPane ? m_Model->GetSplitChatLocked() : m_Model->GetCurrentChatLocked();
  DrawPaneHeader(currentChat);

  const bool emojiEnabled = m_Model->GetEmojiEnabledLocked();
  static const bool developerMode = AppUtil::GetDeveloperMode();

  std::vector<std::string>& messageVec =
    m_Model->GetMessageVecLocked(currentChat.first, currentChat.second);
  std::unordered_map<std::string, ChatMessage>& messages =
    m_Model->GetMessagesLocked(currentChat.first, currentChat.second);
  int messageOffset = isSplitPane ? 0 :
    std::max(m_Model->GetMessageOffsetLocked(currentChat.first, currentChat.second), 0);
  messageOffset = std::min(messageOffset, (int)messageVec.size());

  werase(m_PaddedWin);
  wbkgd(m_PaddedWin, attributeTextNormal | colorPairTextRecv | ' ');

  m_HistoryShowCount = 0;

  bool firstMessage = true;
  int y = m_PaddedH - 1;
  for (auto it = std::next(messageVec.begin(), messageOffset); it != messageVec.end(); ++it)
  {
    bool isSelectedMessage = firstMessage && !isSplitPane && m_Model->GetSelectMessageActiveLocked();

    auto msgIt = messages.find(*it);
    if (msgIt == messages.end())
    {
      LOG_WARNING("message %s missing", it->c_str());
      continue;
    }

    ChatMessage& msg = msgIt->second;

    int attributeText = isSelectedMessage ? attributeTextSelected : attributeTextNormal;
    int colorPairText = [&]()
    {
      if (msg.isOutgoing) return colorPairTextSent;

      if (msg.senderId == currentChat.second) return colorPairTextRecv;

      static bool isUserColor = UiColorConfig::IsUserColor("history_text_recv_group_color");
      if (!isUserColor)
      {
        static int colorPairGroup = UiColorConfig::GetColorPair("history_text_recv_group_color");
        return colorPairGroup;
      }

      int colorPairGroup = UiColorConfig::GetUserColorPair("history_text_recv_group_color",
                                                           msg.senderId);
      return colorPairGroup;
    }();

    const UiImageRows* imageRows = nullptr;
    std::shared_ptr<const UiPixelImage> imagePixels;
    bool pixelVisible = false;
    std::vector<std::wstring> wlines;
    if (!msg.text.empty())
    {
      std::string text = msg.text;
      StrUtil::SanitizeMessageStr(text);
      if (!emojiEnabled)
      {
        text = StrUtil::Textize(text);
      }

      wlines = StrUtil::WordWrap(StrUtil::ToWString(text), m_PaddedW, false, false, false, 2);
    }

    // Quoted message
    if (!msg.quotedId.empty())
    {
      std::string quotedText;
      auto quotedIt = messages.find(msg.quotedId);
      if (quotedIt != messages.end())
      {
        if (!quotedIt->second.text.empty())
        {
          quotedText = StrUtil::Split(quotedIt->second.text, '\n').at(0);
          if (!emojiEnabled)
          {
            quotedText = StrUtil::Textize(quotedText);
          }
        }
        else if (!quotedIt->second.fileInfo.empty())
        {
          FileInfo fileInfo = ProtocolUtil::FileInfoFromHex(quotedIt->second.fileInfo);
          quotedText = FileUtil::BaseName(fileInfo.filePath);
        }
      }
      else
      {
        m_Model->FetchCachedMessageLocked(currentChat.first, currentChat.second, msg.quotedId);
        quotedText = "";
      }

      int maxQuoteLen = m_PaddedW - 3;
      std::wstring quote = quoteIndicator + StrUtil::ToWString(quotedText);
      if (StrUtil::WStringWidth(quote) > maxQuoteLen)
      {
        quote = StrUtil::TrimPadWString(quote, maxQuoteLen) + L"...";
      }

      wlines.insert(wlines.begin(), quote);
    }

    // File attachment
    if (!msg.fileInfo.empty())
    {
      FileInfo fileInfo = ProtocolUtil::FileInfoFromHex(msg.fileInfo);

      // special case handling selection-triggered download, and handling cache's old setting
      static const bool isAttachmentPrefetchAll =
        (AppConfig::GetNum("attachment_prefetch") == AttachmentPrefetchAll);
      static const bool isAttachmentPrefetchSelected =
        (AppConfig::GetNum("attachment_prefetch") == AttachmentPrefetchSelected);
      if (isAttachmentPrefetchAll || (isSelectedMessage && isAttachmentPrefetchSelected))
      {
        if (!UiModel::IsAttachmentDownloaded(fileInfo) && UiModel::IsAttachmentDownloadable(fileInfo))
        {
          m_Model->DownloadAttachmentLocked(currentChat.first, currentChat.second, *it,
                                            fileInfo.fileId, DownloadFileActionNone);
          fileInfo = ProtocolUtil::FileInfoFromHex(msg.fileInfo);
        }
      }

      std::string fileName = FileUtil::BaseName(fileInfo.filePath);
      if ((UiConfig::GetStr("media_display") == "path") && (fileInfo.fileStatus == FileStatusDownloaded))
      {
        static const std::string homeDir = (getenv("HOME") != nullptr) ? getenv("HOME") : "";
        fileName = fileInfo.filePath;
        if (!homeDir.empty() && StrUtil::StartsWith(fileName, homeDir + "/"))
        {
          fileName = "~" + fileName.substr(homeDir.size());
        }
      }

      std::string fileStatus;
      if (fileInfo.fileStatus == FileStatusNone)
      {
        // should not happen
        static const std::string statusNone = " -";
        fileStatus = statusNone;
      }
      else if (fileInfo.fileStatus == FileStatusNotDownloaded)
      {
        static const std::string statusNotDownloaded = " " + UiConfig::GetStr("downloadable_indicator");
        fileStatus = statusNotDownloaded;
      }
      else if (fileInfo.fileStatus == FileStatusDownloaded)
      {
        static const std::string statusDownloaded = "";
        fileStatus = statusDownloaded + UiAudioPlayer::GetStatus(fileInfo.filePath);
      }
      else if (fileInfo.fileStatus == FileStatusDownloading)
      {
        static const std::string statusDownloading = " " + UiConfig::GetStr("syncing_indicator");
        fileStatus = statusDownloading;
      }
      else if (fileInfo.fileStatus == FileStatusDownloadFailed)
      {
        static const std::string statusDownloadFailed = " " + UiConfig::GetStr("failed_indicator");
        fileStatus = statusDownloadFailed;
      }

      std::wstring fileStr = attachmentIndicator + StrUtil::ToWString(fileName + fileStatus);

      // Inline image preview
      if (UiImagePreview::IsEnabled() && (fileInfo.fileStatus == FileStatusDownloaded) &&
          UiImagePreview::IsPreviewable(fileInfo.filePath))
      {
        imagePixels = UiImagePreview::GetPixels(fileInfo.filePath, m_PaddedW, 0, this);
        imageRows = &UiImagePreview::GetRows(fileInfo.filePath, m_PaddedW, imagePixels ? imagePixels->rows : 0);
        const size_t height = imagePixels ? imagePixels->rows : imageRows->size();
        for (size_t row = height; row > 0; --row)
        {
          wlines.insert(wlines.begin(), imageRowIndicator + std::to_wstring(row - 1));
        }
      }

      wlines.insert(wlines.begin(), fileStr);
    }

    // Reactions
    int reactionLines = 0;
    static bool reactionsEnabled = UiConfig::GetBool("reactions_enabled");
    if (reactionsEnabled)
    {
      std::string selfEmoji;
      auto sit = msg.reactions.senderEmojis.find(s_ReactionsSelfId);
      if (sit != msg.reactions.senderEmojis.end())
      {
        selfEmoji = sit->second;
      }

      // Allow also if we have self emoji, even if not yet consolidated into count
      if (!msg.reactions.emojiCounts.empty() || !selfEmoji.empty())
      {
        bool foundSelf = false;
        std::string reactionsText;
        std::multimap<float, std::string> emojiCountsSorted;
        for (const auto& emojiCount : msg.reactions.emojiCounts)
        {
          float count = emojiCount.second;
          if (emojiCount.first == selfEmoji)
          {
            count += 0.1; // for equal count, prioritize own selected reaction
            foundSelf = true;
          }

          emojiCountsSorted.insert(std::make_pair(count, emojiCount.first));
        }

        if (!foundSelf && !selfEmoji.empty())
        {
          LOG_DEBUG("insert missing reaction for self");
          emojiCountsSorted.insert(std::make_pair(1.1, selfEmoji));
        }

        bool firstReaction = true;
        for (auto emojiCount = emojiCountsSorted.rbegin(); emojiCount != emojiCountsSorted.rend(); ++emojiCount)
        {
          reactionsText += (firstReaction ? " " : "  ");
          if (emojiCount->second == selfEmoji)
          {
            // Highlight own reaction emoji
            reactionsText += "" + emojiCount->second + "*";
          }
          else
          {
            reactionsText += emojiCount->second;
          }

          if (emojiCount->first > 1.5)
          {
            reactionsText += " " + FileUtil::GetSuffixedCount(static_cast<ssize_t>(emojiCount->first));
          }

          firstReaction = false;
        }

        if (!reactionsText.empty())
        {
          if (!emojiEnabled)
          {
            reactionsText = StrUtil::Textize(reactionsText);
          }

          const int maxReactionsLen = m_PaddedW - 4;
          std::wstring reactions = StrUtil::ToWString(reactionsText);
          if (StrUtil::WStringWidth(reactions) > maxReactionsLen)
          {
            reactions = StrUtil::TrimPadWString(reactions, maxReactionsLen) + L"... ";
          }
          else
          {
            reactions += L" ";
          }

          wlines.insert(wlines.end(), reactions);
          reactionLines = 1;
        }
      }
    }

    const int maxMessageLines = (m_PaddedH - 1);
    if (firstMessage && ((int)wlines.size() > maxMessageLines))
    {
      wlines.resize(maxMessageLines - 1);
      wlines.push_back(L"[...]");
      reactionLines = 0;
    }

    for (auto wline = wlines.rbegin(); wline != wlines.rend(); ++wline)
    {
      if ((imageRows != nullptr) && (wline->rfind(imageRowIndicator, 0) == 0))
      {
        const size_t row = std::stoul(wline->substr(imageRowIndicator.size()));
        if (imagePixels && row == (size_t)(imagePixels->rows - 1))
        {
          const int top = y - (int)row;
          pixelVisible = top >= 0;
          if (pixelVisible)
          {
            int winY, winX;
            getbegyx(m_PaddedWin, winY, winX);
            placements.push_back({imagePixels, winX, winY + top});
          }
        }
        // A clipped image uses character rows; a raw Sixel is never allowed
        // to extend above the history pane or over headers/other panes.
        if (!pixelVisible && row < imageRows->size())
        {
          UiImagePreview::DrawRow(m_PaddedWin, y, imageRows->at(row), m_PaddedW);
        }

        if (--y < 0) break;

        continue;
      }

      bool isAttachment = (wline->rfind(attachmentIndicator, 0) == 0);
      bool isQuote = (wline->rfind(quoteIndicator, 0) == 0);
      bool isReaction = (reactionLines == 1) && (std::distance(wline, wlines.rbegin()) == 0);

      if (isAttachment)
      {
        wattron(m_PaddedWin, attributeText | colorPairTextAttachment);
      }
      else if (isQuote)
      {
        wattron(m_PaddedWin, attributeText | colorPairTextQuoted);
      }
      else if (isReaction)
      {
        wattron(m_PaddedWin, attributeTextNormal | colorPairTextReaction);
      }
      else
      {
        wattron(m_PaddedWin, attributeText | colorPairText);
      }

      const std::wstring wdisp = isReaction ? *wline : StrUtil::TrimPadWString(*wline, m_PaddedW);
      mvwaddnwstr(m_PaddedWin, y, 0, wdisp.c_str(), std::min((int)wdisp.size(), m_PaddedW));

      if (isAttachment)
      {
        wattroff(m_PaddedWin, attributeText | colorPairTextAttachment);
      }
      else if (isQuote)
      {
        wattroff(m_PaddedWin, attributeText | colorPairTextQuoted);
      }
      else if (isReaction)
      {
        wattroff(m_PaddedWin, attributeTextNormal | colorPairTextReaction);
      }
      else
      {
        wattroff(m_PaddedWin, attributeText | colorPairText);
      }

      if (--y < 0) break;
    }

    if (y < 0) break;

    int attributeName = isSelectedMessage ? attributeNameSelected : attributeNameNormal;
    int colorPairName = [&]()
    {
      if (msg.isOutgoing) return colorPairNameSent;

      if (msg.senderId == currentChat.second) return colorPairNameRecv;

      static bool isUserColor = UiColorConfig::IsUserColor("history_name_recv_group_color");
      if (!isUserColor)
      {
        static int colorPairGroup = UiColorConfig::GetColorPair("history_name_recv_group_color");
        return colorPairGroup;
      }

      int colorPairGroup = UiColorConfig::GetUserColorPair("history_name_recv_group_color",
                                                           msg.senderId);
      return colorPairGroup;
    }();

    wattron(m_PaddedWin, attributeName | colorPairName);
    std::string name = m_Model->GetContactNameLocked(currentChat.first, msg.senderId);
    if (!emojiEnabled)
    {
      name = StrUtil::Textize(name);
    }

    std::wstring wsender = StrUtil::ToWString(name);
    std::wstring wtime;
    if (developerMode)
    {
      wtime = L" (" + StrUtil::ToWString(std::to_string(msg.timeSent)) + L")";
    }
    else
    {
      if (msg.timeSent != std::numeric_limits<int64_t>::max())
      {
        wtime = L" (" + StrUtil::ToWString(TimeUtil::GetTimeString(msg.timeSent, false /* p_IsExport */)) + L")";
      }
    }

    m_Model->MarkReadLocked(currentChat.first, currentChat.second, *it, (!msg.isOutgoing && !msg.isRead));

    static const std::string readIndicator = " " + UiConfig::GetStr("read_indicator");
    static const std::string editedIndicator = " " + UiConfig::GetStr("edited_indicator");
    static const std::string pinnedIndicator = " " + UiConfig::GetStr("pinned_indicator");
    std::wstring wreceipt = StrUtil::ToWString(msg.isRead ? readIndicator : "");
    std::wstring wedited = StrUtil::ToWString(msg.isEdited ? editedIndicator : "");
    std::wstring wpinned = StrUtil::ToWString(msg.isPinned ? pinnedIndicator : "");
    std::wstring wheader = wsender + wtime + wreceipt + wedited + wpinned;

    if (developerMode)
    {
      wheader = wheader +
        L" msg " + StrUtil::ToWString(msg.id) +
        L" user " + StrUtil::ToWString(msg.senderId);
    }

    std::wstring wdisp = StrUtil::TrimPadWString(wheader, m_PaddedW);
    mvwaddnwstr(m_PaddedWin, y, 0, wdisp.c_str(), std::min((int)wdisp.size(), m_PaddedW));

    wattroff(m_PaddedWin, attributeName | colorPairName);

    ++m_HistoryShowCount;

    if (--y < 0) break;

    if (--y < 0) break;

    firstMessage = false;
  }

  wrefresh(m_PaddedWin);
  UiPixelGraphics::Show(this, placements);
}

void UiHistoryView::DrawPaneHeader(const std::pair<std::string, std::string>& p_Chat)
{
  // Identity lives once above each conversation; tabs remain compact navigation.
  static const int attribute = UiColorConfig::GetAttribute("history_text_attr");
  static const int colorPair = UiColorConfig::GetColorPair("history_text_recv_color");
  const attr_t baseAttr = attribute | colorPair;
  const int hpad = (m_X == 0) ? 0 : 1;
  const int endX = std::max(hpad, m_W - hpad);
  const bool isActive = (m_Pane != PaneSplit);

  werase(m_Win);
  wbkgd(m_Win, baseAttr | ' ');
  if (p_Chat.first.empty() && p_Chat.second.empty())
  {
    wrefresh(m_Win);
    return;
  }

  std::string name = m_Model->GetContactListNameLocked(p_Chat.first, p_Chat.second, true /*p_AllowId*/,
                                                       true /*p_AllowAlias*/);
  if (!m_Model->GetEmojiEnabledLocked()) name = StrUtil::Textize(name);

  struct Segment
  {
    std::wstring text;
    attr_t attr;
  };
  std::vector<Segment> left;
  const std::wstring prefix = (m_Pane == PaneMain) ? L"\u25b8 " : L"";
  const std::wstring wname = StrUtil::ToWString(name);
  left.push_back({ prefix + wname, baseAttr | (isActive ? A_BOLD : A_DIM) });

  std::string handle;
  if (UiConfig::GetBool("show_contact_handle"))
  {
    handle = m_Model->GetContactHandleLocked(p_Chat.first, p_Chat.second);
    if (!handle.empty()) left.push_back({ L"  " + StrUtil::ToWString(handle), baseAttr | A_ITALIC });
  }

  const std::string phoneIndicator = UiConfig::GetStr("phone_number_indicator");
  if (!phoneIndicator.empty())
  {
    std::string indicator = phoneIndicator;
    StrUtil::ReplaceString(indicator, "%1", m_Model->GetContactPhoneLocked(p_Chat.first, p_Chat.second));
    left.push_back({ L" " + StrUtil::ToWString(indicator), baseAttr });
  }

  Segment account = { L"", baseAttr };
  if (UiConfig::GetBool("protocol_labels") && m_Model->IsMultipleProfilesLocked())
  {
    const int protocolPair = UiColorConfig::GetProtocolColorPair(p_Chat.first);
    account = { L" " + StrUtil::ToWString(m_Model->GetProtocolLabelLocked(p_Chat.first)) + L" ",
                protocolPair ? (A_REVERSE | A_BOLD | protocolPair) : (baseAttr | A_BOLD) };
  }
  else
  {
    account.text = StrUtil::ToWString(m_Model->GetProfileSuffixLocked(p_Chat.first));
  }
  const int accountX = std::max(hpad, endX - StrUtil::WStringWidth(account.text));
  const int leftEnd = std::max(hpad, accountX - (account.text.empty() ? 0 : 1));
  // Keep useful identity text in narrow panes rather than fragments of a number.
  if (!handle.empty())
  {
    const int handleWidth = StrUtil::WStringWidth(left.at(1).text);
    const int minNameWidth = std::min(8, StrUtil::WStringWidth(left.front().text));
    if ((handleWidth + minNameWidth) > (leftEnd - hpad))
    {
      left.erase(left.begin() + 1);
    }
  }
  int detailWidth = 0;
  for (size_t i = 1; i < left.size(); ++i) detailWidth += StrUtil::WStringWidth(left.at(i).text);
  const int nameWidth = std::max(1, leftEnd - hpad - detailWidth);
  if (StrUtil::WStringWidth(left.front().text) > nameWidth)
  {
    left.front().text = StrUtil::TrimPadWString(left.front().text, nameWidth - 1) + L"\u2026";
  }

  auto draw = [&](const Segment& p_Segment, int p_X, int p_End)
  {
    if (p_X >= p_End) return p_X;
    std::wstring text = p_Segment.text;
    if (StrUtil::WStringWidth(text) > (p_End - p_X))
    {
      text = StrUtil::TrimPadWString(text, p_End - p_X);
    }
    wattrset(m_Win, p_Segment.attr);
    mvwaddnwstr(m_Win, 0, p_X, text.c_str(), text.size());
    return p_X + StrUtil::WStringWidth(text);
  };
  int x = hpad;
  for (const auto& segment : left) x = draw(segment, x, leftEnd);
  draw(account, accountX, endX);
  wattrset(m_Win, baseAttr);
  wrefresh(m_Win);
}

int UiHistoryView::GetHistoryShowCount()
{
  return m_HistoryShowCount;
}
