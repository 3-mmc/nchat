// cgoig.go
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

package main

// #cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
// #cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup
// extern void IgNewContactsNotify(int p_ConnId, char* p_ChatId, char* p_Name, char* p_Phone, int p_IsSelf, int p_IsAlias, int p_Notify);
// extern void IgNewChatsNotify(int p_ConnId, char* p_ChatId, int p_IsUnread, int p_IsMuted, int p_IsPinned, int p_IsArchived, int p_LastMessageTime);
// extern void IgNewGroupMembersNotify(int p_ConnId, char* p_ChatId, char* p_MembersJson);
// extern void IgNewMessagesNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text, int p_FromMe, char* p_QuotedId, char* p_FileId, char* p_FilePath, int p_FileStatus, int p_TimeSent, int p_IsRead, int p_IsEdited);
// extern void IgNewHistoryMessagesNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text, int p_FromMe, char* p_QuotedId, char* p_FileId, char* p_FilePath, int p_FileStatus, int p_TimeSent, int p_IsRead, int p_IsEdited, char* p_FromMsgId, int p_Notify);
// extern void IgNewStatusNotify(int p_ConnId, char* p_UserId, int p_IsOnline, int p_TimeSeen);
// extern void IgNewTypingNotify(int p_ConnId, char* p_ChatId, char* p_UserId, int p_IsTyping);
// extern void IgNewMessageStatusNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, int p_IsRead);
// extern void IgNewMessageFileNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_FileId, char* p_FilePath, int p_FileStatus, int p_Action);
// extern void IgNewMessageReactionNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text, int p_FromMe);
// extern void IgDeleteChatNotify(int p_ConnId, char* p_ChatId);
// extern void IgMoveChatNotify(int p_ConnId, char* p_ChatId, char* p_NewChatId);
// extern void IgDeleteMessageNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, int p_IsOutgoing);
// extern void IgUpdateMuteNotify(int p_ConnId, char* p_ChatId, int p_IsMuted);
// extern void IgUpdateArchivedNotify(int p_ConnId, char* p_ChatId, int p_IsArchived);
// extern void IgUpdatePinNotify(int p_ConnId, char* p_ChatId, int p_IsPinned, int p_TimePinned);
// extern void IgReinit(int p_ConnId);
// extern void IgSetProtocolUiControl(int p_ConnId, int p_IsTakeControl);
// extern void IgSetStatus(int p_ConnId, int p_Flags);
// extern void IgClearStatus(int p_ConnId, int p_Flags);
// extern int IgAppConfigGetNum(char* p_Param);
// extern void IgAppConfigSetNum(char* p_Param, int p_Value);
// extern int IgHasGui();
// extern void IgLogTrace(char* p_Filename, int p_LineNo, char* p_Message);
// extern void IgLogDebug(char* p_Filename, int p_LineNo, char* p_Message);
// extern void IgLogInfo(char* p_Filename, int p_LineNo, char* p_Message);
// extern void IgLogWarning(char* p_Filename, int p_LineNo, char* p_Message);
// extern void IgLogError(char* p_Filename, int p_LineNo, char* p_Message);
import "C"

import (
	"path/filepath"
	"runtime"
)

//export CIgInit
func CIgInit(path *C.char, proxy *C.char) int {
	return IgInit(C.GoString(path), C.GoString(proxy))
}

//export CIgLogin
func CIgLogin(connId int) int {
	return IgLogin(connId)
}

//export CIgLogout
func CIgLogout(connId int) int {
	return IgLogout(connId)
}

//export CIgCleanup
func CIgCleanup(connId int) int {
	return IgCleanup(connId)
}

//export CIgGetVersion
func CIgGetVersion() int {
	return IgGetVersion()
}

//export CIgGetMessages
func CIgGetMessages(connId int, chatId *C.char, limit int, fromMsgId *C.char, owner int) int {
	return IgGetMessages(connId, C.GoString(chatId), limit, C.GoString(fromMsgId), owner)
}

//export CIgSendMessage
func CIgSendMessage(connId int, chatId *C.char, text *C.char, quotedId *C.char, quotedText *C.char, quotedSender *C.char, filePath *C.char, fileType *C.char, editMsgId *C.char, editMsgSent int, mentionsJson *C.char) int {
	return IgSendMessage(connId, C.GoString(chatId), C.GoString(text), C.GoString(quotedId), C.GoString(quotedText), C.GoString(quotedSender), C.GoString(filePath), C.GoString(fileType), C.GoString(editMsgId), editMsgSent, C.GoString(mentionsJson))
}

//export CIgGetGroupMembers
func CIgGetGroupMembers(connId int, chatId *C.char) int {
	return IgGetGroupMembers(connId, C.GoString(chatId))
}

//export CIgGetContacts
func CIgGetContacts(connId int) int {
	return IgGetContacts(connId)
}

//export CIgGetChats
func CIgGetChats(connId int) int {
	return IgGetChats(connId)
}

//export CIgMarkMessageRead
func CIgMarkMessageRead(connId int, chatId *C.char, senderId *C.char, msgId *C.char) int {
	return IgMarkMessageRead(connId, C.GoString(chatId), C.GoString(senderId), C.GoString(msgId))
}

//export CIgDeleteMessage
func CIgDeleteMessage(connId int, chatId *C.char, senderId *C.char, msgId *C.char) int {
	return IgDeleteMessage(connId, C.GoString(chatId), C.GoString(senderId), C.GoString(msgId))
}

//export CIgSetupPassword
func CIgSetupPassword(connId int, username *C.char, password *C.char) int {
	return IgSetupPassword(connId, C.GoString(username), C.GoString(password))
}

//export CIgSetupTwoFactor
func CIgSetupTwoFactor(connId int, code *C.char) int {
	return IgSetupTwoFactor(connId, C.GoString(code))
}

//export CIgSetupCookies
func CIgSetupCookies(connId int, input *C.char) int {
	return IgSetupCookies(connId, C.GoString(input))
}

//export CIgGetLastError
func CIgGetLastError(connId int) *C.char {
	return C.CString(IgGetLastError(connId))
}

//export CIgIsGroupChat
func CIgIsGroupChat(connId int, chatId *C.char) int {
	return IgIsGroupChat(connId, C.GoString(chatId))
}

//export CIgDeleteChat
func CIgDeleteChat(connId int, chatId *C.char) int {
	return IgDeleteChat(connId, C.GoString(chatId))
}

//export CIgArchiveChat
func CIgArchiveChat(connId int, chatId *C.char, isArchived int) int {
	return IgArchiveChat(connId, C.GoString(chatId), isArchived)
}

//export CIgPinChat
func CIgPinChat(connId int, chatId *C.char, isPinned int) int {
	return IgPinChat(connId, C.GoString(chatId), isPinned)
}

//export CIgSendTyping
func CIgSendTyping(connId int, chatId *C.char, isTyping int) int {
	return IgSendTyping(connId, C.GoString(chatId), isTyping)
}

//export CIgDownloadFile
func CIgDownloadFile(connId int, chatId *C.char, msgId *C.char, fileId *C.char, action int) int {
	return IgDownloadFile(connId, C.GoString(chatId), C.GoString(msgId), C.GoString(fileId), action)
}

//export CIgSendReaction
func CIgSendReaction(connId int, chatId *C.char, senderId *C.char, msgId *C.char, emoji *C.char, prevEmoji *C.char) int {
	return IgSendReaction(connId, C.GoString(chatId), C.GoString(senderId), C.GoString(msgId), C.GoString(emoji), C.GoString(prevEmoji))
}

func CIgNewContactsNotify(connId int, chatId string, name string, phone string, isSelf int, isAlias int, notify int) {
	C.IgNewContactsNotify(C.int(connId), C.CString(chatId), C.CString(name), C.CString(phone), C.int(isSelf), C.int(isAlias), C.int(notify))
}

func CIgNewChatsNotify(connId int, chatId string, isUnread int, isMuted int, isPinned int, isArchived int, lastMessageTime int) {
	C.IgNewChatsNotify(C.int(connId), C.CString(chatId), C.int(isUnread), C.int(isMuted), C.int(isPinned), C.int(isArchived), C.int(lastMessageTime))
}

func CIgNewGroupMembersNotify(connId int, chatId string, membersJson string) {
	C.IgNewGroupMembersNotify(C.int(connId), C.CString(chatId), C.CString(membersJson))
}

func CIgNewMessagesNotify(connId int, chatId string, msgId string, senderId string, text string, fromMe int, quotedId string, fileId string, filePath string, fileStatus int, timeSent int, isRead int, isEdited int) {
	C.IgNewMessagesNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.CString(senderId), C.CString(text), C.int(fromMe), C.CString(quotedId), C.CString(fileId), C.CString(filePath), C.int(fileStatus), C.int(timeSent), C.int(isRead), C.int(isEdited))
}

func CIgNewHistoryMessagesNotify(connId int, chatId string, msgId string, senderId string, text string, fromMe int, quotedId string, fileId string, filePath string, fileStatus int, timeSent int, isRead int, isEdited int, fromMsgId string, notify int) {
	C.IgNewHistoryMessagesNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.CString(senderId), C.CString(text), C.int(fromMe), C.CString(quotedId), C.CString(fileId), C.CString(filePath), C.int(fileStatus), C.int(timeSent), C.int(isRead), C.int(isEdited), C.CString(fromMsgId), C.int(notify))
}

func CIgNewStatusNotify(connId int, userId string, isOnline int, timeSeen int) {
	C.IgNewStatusNotify(C.int(connId), C.CString(userId), C.int(isOnline), C.int(timeSeen))
}

func CIgNewTypingNotify(connId int, chatId string, userId string, isTyping int) {
	C.IgNewTypingNotify(C.int(connId), C.CString(chatId), C.CString(userId), C.int(isTyping))
}

func CIgNewMessageStatusNotify(connId int, chatId string, msgId string, isRead int) {
	C.IgNewMessageStatusNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.int(isRead))
}

func CIgNewMessageFileNotify(connId int, chatId string, msgId string, fileId string, filePath string, fileStatus int, action int) {
	C.IgNewMessageFileNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.CString(fileId), C.CString(filePath), C.int(fileStatus), C.int(action))
}

func CIgNewMessageReactionNotify(connId int, chatId string, msgId string, senderId string, text string, fromMe int) {
	C.IgNewMessageReactionNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.CString(senderId), C.CString(text), C.int(fromMe))
}

func CIgDeleteChatNotify(connId int, chatId string) {
	C.IgDeleteChatNotify(C.int(connId), C.CString(chatId))
}

func CIgMoveChatNotify(connId int, chatId string, newChatId string) {
	C.IgMoveChatNotify(C.int(connId), C.CString(chatId), C.CString(newChatId))
}

func CIgDeleteMessageNotify(connId int, chatId string, msgId string, isOutgoing int) {
	C.IgDeleteMessageNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.int(isOutgoing))
}

func CIgUpdateMuteNotify(connId int, chatId string, isMuted int) {
	C.IgUpdateMuteNotify(C.int(connId), C.CString(chatId), C.int(isMuted))
}

func CIgUpdateArchivedNotify(connId int, chatId string, isArchived int) {
	C.IgUpdateArchivedNotify(C.int(connId), C.CString(chatId), C.int(isArchived))
}

func CIgUpdatePinNotify(connId int, chatId string, isPinned int, timePinned int) {
	C.IgUpdatePinNotify(C.int(connId), C.CString(chatId), C.int(isPinned), C.int(timePinned))
}

func CIgReinit(connId int) {
	C.IgReinit(C.int(connId))
}

func CIgSetProtocolUiControl(connId int, isTakeControl int) {
	C.IgSetProtocolUiControl(C.int(connId), C.int(isTakeControl))
}

func CIgSetStatus(connId int, flags int) {
	C.IgSetStatus(C.int(connId), C.int(flags))
}

func CIgClearStatus(connId int, flags int) {
	C.IgClearStatus(C.int(connId), C.int(flags))
}

func CIgAppConfigGetNum(param string) int {
	return int(C.IgAppConfigGetNum(C.CString(param)))
}

func CIgAppConfigSetNum(param string, value int) {
	C.IgAppConfigSetNum(C.CString(param), C.int(value))
}

func CIgHasGui() int {
	return int(C.IgHasGui())
}

func LOG_TRACE(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.IgLogTrace(C.CString(filename), C.int(lineNo), C.CString(message))
}

func LOG_DEBUG(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.IgLogDebug(C.CString(filename), C.int(lineNo), C.CString(message))
}

func LOG_INFO(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.IgLogInfo(C.CString(filename), C.int(lineNo), C.CString(message))
}

func LOG_WARNING(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.IgLogWarning(C.CString(filename), C.int(lineNo), C.CString(message))
}

func LOG_ERROR(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.IgLogError(C.CString(filename), C.int(lineNo), C.CString(message))
}
