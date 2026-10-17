// goig.go
//
// Instagram DM protocol bridge for nchat, built on instameow from mautrix-meta
// (the library behind Beeper's Instagram bridge).
//
// nchat is distributed under the MIT license, see LICENSE for details.

package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"mime"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/rs/zerolog"
	"go.mau.fi/util/exhttp"
	"go.mau.fi/util/jsontime"
	"go.mau.fi/util/ptr"

	"go.mau.fi/mautrix-meta/pkg/instameow"
	"go.mau.fi/mautrix-meta/pkg/instameow/slidetypes"
	"go.mau.fi/mautrix-meta/pkg/messagix/cookies"
	"go.mau.fi/mautrix-meta/pkg/messagix/httpclient"
	"go.mau.fi/mautrix-meta/pkg/messagix/methods"
	"go.mau.fi/mautrix-meta/pkg/messagix/types"
	"go.mau.fi/mautrix-meta/pkg/msgconv/mediadl"
)

// keep in sync with enum FileStatus in protocol.h
var FileStatusNone = -1
var FileStatusNotDownloaded = 0
var FileStatusDownloaded = 1
var FileStatusDownloading = 2
var FileStatusDownloadFailed = 3

// keep in sync with enum Flag in status.h
var FlagNone = 0
var FlagOffline = (1 << 0)
var FlagConnecting = (1 << 1)
var FlagOnline = (1 << 2)
var FlagFetching = (1 << 3)
var FlagSending = (1 << 4)
var FlagUpdating = (1 << 5)
var FlagSyncing = (1 << 6)
var FlagAway = (1 << 7)

// keep in sync with enum NotifyType in igchat.cpp
var NotifyDirect = 0
var NotifyCache = 1
var NotifySendCached = 2

// keep in sync with enum DownloadFileAction in protocol.h
var DownloadFileActionNone = 0

const igchatVersion = 20261016
const downloadInfoVersion = 1
const maxMediaSize = 512 * 1024 * 1024

// ThreadMeta holds the identifiers needed to address a thread, persisted in threads.json
type ThreadMeta struct {
	LongID     string  `json:"long_id"` // ig thread igid, used for mark read / unsend
	FBID       int64   `json:"fbid"`    // thread key
	IsGroup    bool    `json:"is_group"`
	Title      string  `json:"title"`
	Members    []int64 `json:"members"`
	IsPinned   bool    `json:"is_pinned"`
	IsMuted    bool    `json:"is_muted"`
	OtherFBID  int64   `json:"other_fbid"` // 1:1 chats: the other participant
	Handle     string  `json:"handle"`     // 1:1 chats: @username of the other participant
	LastOwnMsg string  `json:"-"`
}

type sentInfo struct {
	senderId string
	fromMe   bool
}

type IgConn struct {
	connId  int
	path    string
	proxy   string
	log     zerolog.Logger
	logFile *os.File

	client  *instameow.Client
	cookies *cookies.Cookies
	ownFBID int64
	ownName string

	ctx    context.Context
	cancel context.CancelFunc

	mu       sync.Mutex
	threads  map[string]*ThreadMeta // chat id (short ig thread id) -> meta
	longToId map[string]string      // long thread id -> chat id
	users    map[int64]string       // fbid -> display name
	handles  map[int64]string       // fbid -> username
	msgInfo  map[string]sentInfo    // msg id -> sender, for edits
	lastErr  string
}

// http settings shared by instameow and the media downloader
var httpSettings = exhttp.ClientSettings{
	DialTimeout:           30 * time.Second,
	TLSHandshakeTimeout:   30 * time.Second,
	ResponseHeaderTimeout: 2 * time.Minute,
	IdleConnTimeout:       90 * time.Second,
}

var initMediaOnce sync.Once

// recoverPanic keeps a bug in the bridge from aborting nchat (a Go panic in a
// cgo call or goroutine would otherwise terminate the whole process)
func recoverPanic(where string) {
	if r := recover(); r != nil {
		LOG_WARNING(fmt.Sprintf("panic in %s: %v", where, r))
	}
}

var (
	connsMu sync.Mutex
	conns   = make(map[int]*IgConn)
	nextId  = 0
)

func GetConn(connId int) *IgConn {
	connsMu.Lock()
	defer connsMu.Unlock()
	return conns[connId]
}

func BoolToInt(b bool) int {
	if b {
		return 1
	}
	return 0
}

// --- persistence ---

func (c *IgConn) cookiesPath() string { return filepath.Join(c.path, "cookies.json") }
func (c *IgConn) threadsPath() string { return filepath.Join(c.path, "threads.json") }

func (c *IgConn) saveCookies() {
	if c.cookies == nil {
		return
	}
	data, err := json.Marshal(c.cookies)
	if err != nil {
		LOG_WARNING(fmt.Sprintf("marshal cookies: %v", err))
		return
	}
	if err = os.WriteFile(c.cookiesPath(), data, 0600); err != nil {
		LOG_WARNING(fmt.Sprintf("write cookies: %v", err))
	}
}

func (c *IgConn) loadCookies() bool {
	data, err := os.ReadFile(c.cookiesPath())
	if err != nil {
		return false
	}
	ck := &cookies.Cookies{Platform: types.Instagram}
	if err = json.Unmarshal(data, ck); err != nil {
		LOG_WARNING(fmt.Sprintf("unmarshal cookies: %v", err))
		return false
	}
	ck.Platform = types.Instagram
	c.cookies = ck
	return true
}

func (c *IgConn) saveThreads() {
	c.mu.Lock()
	data, err := json.Marshal(c.threads)
	c.mu.Unlock()
	if err == nil {
		_ = os.WriteFile(c.threadsPath(), data, 0600)
	}
}

func (c *IgConn) loadThreads() {
	data, err := os.ReadFile(c.threadsPath())
	if err != nil {
		return
	}
	threads := make(map[string]*ThreadMeta)
	if json.Unmarshal(data, &threads) == nil {
		c.threads = threads
		for id, t := range threads {
			if t.LongID != "" {
				c.longToId[t.LongID] = id
			}
		}
	}
}

func (c *IgConn) newClient() *instameow.Client {
	if c.cookies == nil {
		c.cookies = &cookies.Cookies{Platform: types.Instagram}
	}
	cli := instameow.NewClient(instameow.ClientParams{
		Cookies:      c.cookies,
		Log:          c.log,
		Settings:     httpSettings,
		EventHandler: c.handleEvent,
	})
	return cli
}

// --- init / setup / login ---

func IgInit(path string, proxy string) int {
	initMediaOnce.Do(func() { mediadl.SetHTTP(httpSettings) })

	connsMu.Lock()
	connId := nextId
	nextId++
	connsMu.Unlock()

	c := &IgConn{
		connId:   connId,
		path:     path,
		proxy:    proxy,
		threads:  make(map[string]*ThreadMeta),
		longToId: make(map[string]string),
		users:    make(map[int64]string),
		handles:  make(map[int64]string),
		msgInfo:  make(map[string]sentInfo),
	}

	_ = os.MkdirAll(filepath.Join(path, "tmp"), 0700)
	logFile, err := os.OpenFile(filepath.Join(path, "instameow.log"), os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0600)
	if err == nil {
		c.logFile = logFile
		c.log = zerolog.New(logFile).With().Timestamp().Str("component", "instameow").Logger().Level(zerolog.InfoLevel)
	} else {
		c.log = zerolog.Nop()
	}

	c.loadCookies()
	c.loadThreads()

	connsMu.Lock()
	conns[connId] = c
	connsMu.Unlock()
	return connId
}

func IgGetVersion() int {
	return igchatVersion
}

func IgGetLastError(connId int) string {
	c := GetConn(connId)
	if c == nil {
		return "no connection"
	}
	return c.lastErr
}

// IgSetupPassword returns 0 when logged in, 1 when a two-factor code is needed, -1 on failure
func IgSetupPassword(connId int, username string, password string) int {
	defer recoverPanic("IgSetupPassword")
	c := GetConn(connId)
	if c == nil {
		return -1
	}
	c.cookies = &cookies.Cookies{Platform: types.Instagram}
	c.client = c.newClient()
	ctx := context.Background()

	challenge, err := c.client.CreateInstagramWebSession(ctx, username, password)
	if errors.Is(err, instameow.ErrInstagramWebCookieConsentRequired) {
		challenge, err = c.client.ContinueInstagramWebSessionAfterCookieConsent(ctx, username, password)
	}
	if err != nil {
		switch {
		case errors.Is(err, instameow.ErrInstagramWebCredentialsRejected):
			c.lastErr = "Instagram didn't accept that username or password."
		case errors.Is(err, httpclient.ErrRateLimited):
			c.lastErr = "Instagram is rate limiting logins, wait a while and try again."
		case errors.Is(err, instameow.ErrInstagramWebCheckpointCAPTCHA),
			errors.Is(err, instameow.ErrInstagramWebCheckpointUnsupported),
			errors.Is(err, httpclient.ErrChallengeRequired),
			errors.Is(err, httpclient.ErrCheckpointRequired):
			c.lastErr = "Instagram wants an extra security check this client can't do - use the cookie login instead."
		default:
			c.lastErr = fmt.Sprintf("login failed: %v", err)
		}
		return -1
	}

	if challenge != nil {
		if challenge.ChallengeURL != "" || challenge.AuthPlatform {
			c.lastErr = "Instagram wants an extra security check this client can't do - use the cookie login instead."
			return -1
		}
		methods := []string{}
		if challenge.TOTP {
			methods = append(methods, "authenticator app")
		}
		if challenge.SMS {
			methods = append(methods, "SMS")
		}
		if challenge.WhatsApp {
			methods = append(methods, "WhatsApp")
		}
		if challenge.Email {
			methods = append(methods, "email")
		}
		c.lastErr = strings.Join(methods, " / ")
		return 1
	}

	return c.finishSetup()
}

func IgSetupTwoFactor(connId int, code string) int {
	defer recoverPanic("IgSetupTwoFactor")
	c := GetConn(connId)
	if c == nil || c.client == nil {
		return -1
	}
	err := c.client.CompleteInstagramWebSessionTwoFactor(context.Background(), code)
	if errors.Is(err, instameow.ErrInstagramWebTwoFactorCodeResent) {
		c.lastErr = "code rejected, a new SMS code was sent"
		return 1
	} else if errors.Is(err, instameow.ErrInstagramWebTwoFactorCodeRejected) {
		c.lastErr = "code rejected, try again"
		return 1
	} else if err != nil {
		c.lastErr = fmt.Sprintf("two-factor login failed: %v", err)
		return -1
	}
	return c.finishSetup()
}

var cookiePairRe = regexp.MustCompile(`([A-Za-z0-9_]+)=([^;'"\s]*)`)

// IgSetupCookies accepts a "Copy as cURL" command or a raw cookie header from instagram.com
func IgSetupCookies(connId int, input string) int {
	defer recoverPanic("IgSetupCookies")
	c := GetConn(connId)
	if c == nil {
		return -1
	}
	cookieStr := input
	lower := strings.ToLower(input)
	if idx := strings.Index(lower, "cookie: "); idx >= 0 {
		cookieStr = input[idx+len("cookie: "):]
	} else if idx := strings.Index(input, " -b '"); idx >= 0 {
		cookieStr = input[idx+len(" -b '"):]
	} else if idx := strings.Index(input, " -b \""); idx >= 0 {
		cookieStr = input[idx+len(" -b \""):]
	}
	if end := strings.IndexAny(cookieStr, "'\"\n"); end >= 0 {
		cookieStr = cookieStr[:end]
	}

	values := map[cookies.MetaCookieName]string{}
	for _, m := range cookiePairRe.FindAllStringSubmatch(cookieStr, -1) {
		values[cookies.MetaCookieName(m[1])] = m[2]
	}
	ck := &cookies.Cookies{Platform: types.Instagram}
	ck.UpdateValues(values)
	if missing := ck.GetMissingCookieNames(); len(missing) > 0 {
		c.lastErr = fmt.Sprintf("missing cookies: %v (make sure you're logged in to instagram.com)", missing)
		return -1
	}
	c.cookies = ck
	c.client = c.newClient()
	return c.finishSetup()
}

func (c *IgConn) finishSetup() int {
	viewer, _, err := c.client.LoadIndex(context.Background())
	if err != nil {
		c.lastErr = fmt.Sprintf("logged in, but loading the inbox failed: %v", err)
		return -1
	}
	c.ownName = viewer.GetName()
	c.saveCookies()
	c.lastErr = viewer.GetUsername()
	c.client = nil // a fresh client is created on login
	return 0
}

func IgGetSetupUsername(connId int) string {
	c := GetConn(connId)
	if c == nil {
		return ""
	}
	return c.lastErr
}

func IgLogin(connId int) int {
	defer recoverPanic("IgLogin")
	c := GetConn(connId)
	if c == nil {
		return -1
	}
	if c.cookies == nil || !c.cookies.IsLoggedIn() {
		LOG_WARNING("not logged in - run nchat --setup")
		CIgSetStatus(connId, FlagOffline)
		return -1
	}

	CIgSetStatus(connId, FlagConnecting)
	c.ctx, c.cancel = context.WithCancel(context.Background())
	c.client = c.newClient()

	viewer, mailbox, err := c.client.LoadIndex(c.ctx)
	if err != nil {
		LOG_WARNING(fmt.Sprintf("load index failed: %v", err))
		CIgClearStatus(connId, FlagConnecting)
		CIgSetStatus(connId, FlagOffline)
		return -1
	}
	c.ownFBID = c.client.GetOwnFBID()
	c.ownName = viewer.GetName()
	c.saveCookies()

	ownId := strconv.FormatInt(c.ownFBID, 10)
	CIgNewContactsNotify(connId, ownId, c.ownName, "", 1, 0, NotifyDirect)

	go func() {
		defer recoverPanic("sync mailbox")
		c.syncMailbox(mailbox)
	}()
	go func() {
		defer recoverPanic("connect")
		c.client.Connect(c.ctx)
	}()

	CIgClearStatus(connId, FlagConnecting)
	CIgSetStatus(connId, FlagOnline)
	return 0
}

func IgLogout(connId int) int {
	defer recoverPanic("IgLogout")
	c := GetConn(connId)
	if c == nil {
		return -1
	}
	if c.cancel != nil {
		c.cancel()
	}
	if c.client != nil {
		c.client.Disconnect()
	}
	c.saveCookies()
	c.saveThreads()
	CIgClearStatus(connId, FlagOnline)
	CIgSetStatus(connId, FlagOffline)
	return 0
}

func IgCleanup(connId int) int {
	c := GetConn(connId)
	if c == nil {
		return -1
	}
	if c.logFile != nil {
		_ = c.logFile.Close()
	}
	connsMu.Lock()
	delete(conns, connId)
	connsMu.Unlock()
	return 0
}

// --- threads / contacts ---

// atHandle formats a username for the contact phone field; the ui shows a
// phone starting with '@' as is, instead of prefixing it with '+'
func atHandle(username string) string {
	if username == "" {
		return ""
	}
	return "@" + username
}

func (c *IgConn) userName(u *slidetypes.User) string {
	if u == nil {
		return ""
	}
	if u.FullName != "" {
		return u.FullName
	}
	return u.Username
}

func (c *IgConn) registerThread(t *slidetypes.ThreadInfo) (string, *ThreadMeta) {
	if t == nil {
		return "", nil
	}
	chatId := t.ID
	if chatId == "" {
		chatId = t.ThreadFBID
	}

	c.mu.Lock()
	meta := c.threads[chatId]
	if meta == nil {
		meta = &ThreadMeta{}
		c.threads[chatId] = meta
	}
	if t.ThreadID != "" {
		meta.LongID = t.ThreadID
		c.longToId[t.ThreadID] = chatId
	}
	if t.ThreadKey != 0 {
		meta.FBID = t.ThreadKey
	}
	meta.IsGroup = t.IsGroup
	if t.IsPin != nil {
		meta.IsPinned = *t.IsPin
	}
	if t.IsMuted != nil {
		meta.IsMuted = *t.IsMuted
	}
	members := []int64{}
	names := []string{}
	for _, u := range t.Users {
		if u == nil {
			continue
		}
		fbid := u.InteropMessagingUserFBID
		if fbid == 0 {
			fbid, _ = strconv.ParseInt(u.FBIDV2, 10, 64)
		}
		if fbid == 0 {
			continue
		}
		c.users[fbid] = c.userName(u)
		if u.Username != "" {
			c.handles[fbid] = u.Username
		}
		members = append(members, fbid)
		if fbid != c.ownFBID {
			meta.OtherFBID = fbid
			if !t.IsGroup && u.Username != "" {
				meta.Handle = u.Username
			}
			names = append(names, c.userName(u))
		}
	}
	if len(members) > 0 {
		meta.Members = members
	}
	meta.Title = t.ThreadTitle
	if meta.Title == "" {
		meta.Title = strings.Join(names, ", ")
	}
	users := make(map[int64]string, len(c.users))
	for k, v := range c.users {
		users[k] = v
	}
	handles := make(map[int64]string, len(c.handles))
	for k, v := range c.handles {
		handles[k] = v
	}
	chatHandle := ""
	if !meta.IsGroup {
		chatHandle = atHandle(meta.Handle)
	}
	c.mu.Unlock()

	// contacts: group members, and the chat itself (named by title / other user)
	for _, fbid := range members {
		if fbid == c.ownFBID {
			continue
		}
		CIgNewContactsNotify(c.connId, strconv.FormatInt(fbid, 10), users[fbid], atHandle(handles[fbid]), 0, 0, NotifyDirect)
	}
	if meta.Title != "" {
		CIgNewContactsNotify(c.connId, chatId, meta.Title, chatHandle, 0, 0, NotifyDirect)
	}

	return chatId, meta
}

func (c *IgConn) ownWatermark(t *slidetypes.ThreadInfo) (own time.Time, other time.Time) {
	for _, rr := range t.SlideReadReceipts {
		if rr.ParticipantFBID == c.ownFBID {
			own = rr.WatermarkTimestampMS.Time
		} else if rr.WatermarkTimestampMS.Time.After(other) {
			other = rr.WatermarkTimestampMS.Time
		}
	}
	return
}

func (c *IgConn) notifyThread(t *slidetypes.ThreadInfo, withMessages bool) string {
	chatId, meta := c.registerThread(t)
	if chatId == "" {
		return ""
	}

	ownRead, otherRead := c.ownWatermark(t)
	lastTs := t.LastActivityTimestampMS.Time
	isUnread := t.MarkedAsUnread || (!ownRead.IsZero() && ownRead.Before(lastTs))
	CIgNewChatsNotify(c.connId, chatId, BoolToInt(isUnread), BoolToInt(meta.IsMuted), BoolToInt(meta.IsPinned), 0,
		int(lastTs.Unix()))

	if withMessages && t.SlideMessages != nil {
		for _, edge := range t.SlideMessages.Edges {
			if edge.Node != nil {
				c.notifyMessage(chatId, edge.Node, ownRead, otherRead, false, "")
			}
		}
	}
	return chatId
}

func (c *IgConn) syncMailbox(mailbox *slidetypes.Mailbox) {
	if mailbox == nil {
		return
	}
	CIgSetStatus(c.connId, FlagSyncing)
	defer CIgClearStatus(c.connId, FlagSyncing)

	for _, t := range mailbox.PinnedThreadsV2 {
		if t.AsIGDirectThread != nil {
			pinned := true
			t.AsIGDirectThread.IsPin = &pinned
			c.notifyThread(t.AsIGDirectThread, true)
		}
	}
	for _, edge := range mailbox.ThreadsByFolder.Edges {
		c.notifyThread(edge.Node.AsIGDirectThread, true)
	}

	// a few more pages of older threads
	cursor := mailbox.ThreadsByFolder.PageInfo.EndCursor
	hasNext := mailbox.ThreadsByFolder.PageInfo.HasNextPage
	for page := 0; hasNext && cursor != "" && page < 8 && c.ctx.Err() == nil; page++ {
		resp, err := c.client.PaginateMailbox(c.ctx, slidetypes.MakePaginateMailboxRequest(c.ownFBID, cursor, "INBOX", nil))
		if err != nil || resp == nil || resp.Mailbox == nil {
			LOG_WARNING(fmt.Sprintf("paginate mailbox: %v", err))
			break
		}
		for _, edge := range resp.Mailbox.ThreadsByFolder.Edges {
			c.notifyThread(edge.Node.AsIGDirectThread, true)
		}
		cursor = resp.Mailbox.ThreadsByFolder.PageInfo.EndCursor
		hasNext = resp.Mailbox.ThreadsByFolder.PageInfo.HasNextPage
	}

	c.saveThreads()
}

func (c *IgConn) ensureThread(chatId string) *ThreadMeta {
	c.mu.Lock()
	meta := c.threads[chatId]
	c.mu.Unlock()
	if meta != nil && meta.LongID != "" {
		return meta
	}
	if c.client == nil {
		return meta
	}
	resp, err := c.client.GetThread(c.ctx, slidetypes.MakeGetThreadInfoRequest(chatId))
	if err != nil || resp == nil || resp.ThreadInfo.AsIGDirectThread == nil {
		LOG_WARNING(fmt.Sprintf("get thread %s: %v", chatId, err))
		return meta
	}
	c.notifyThread(resp.ThreadInfo.AsIGDirectThread, false)
	c.saveThreads()
	c.mu.Lock()
	meta = c.threads[chatId]
	c.mu.Unlock()
	return meta
}

// --- messages ---

type DownloadInfo struct {
	Version    int    `json:"v"`
	URL        string `json:"url"`
	Mime       string `json:"mime"`
	TargetPath string `json:"path"`
}

var unsafeChars = regexp.MustCompile(`[^A-Za-z0-9._-]`)

func (c *IgConn) fileRef(msgId string, index int, url string, mimeType string, ext string) (string, string, int) {
	if url == "" {
		return "", "", FileStatusNone
	}
	if ext == "" {
		if exts, _ := mime.ExtensionsByType(mimeType); len(exts) > 0 {
			ext = exts[0]
		}
	}
	name := unsafeChars.ReplaceAllString(msgId, "_")
	if index > 0 {
		name += "_" + strconv.Itoa(index)
	}
	target := filepath.Join(c.path, "tmp", name+ext)
	info := DownloadInfo{Version: downloadInfoVersion, URL: url, Mime: mimeType, TargetPath: target}
	data, _ := json.Marshal(info)
	status := FileStatusNotDownloaded
	if _, err := os.Stat(target); err == nil {
		status = FileStatusDownloaded
	}
	return string(data), target, status
}

type msgPart struct {
	text     string
	url      string
	mimeType string
	ext      string
}

func adminText(frags []slidetypes.TextFragment) string {
	var sb strings.Builder
	for _, f := range frags {
		sb.WriteString(f.Plaintext)
	}
	return sb.String()
}

func attachmentParts(atts []*slidetypes.Attachment, video bool) []msgPart {
	parts := []msgPart{}
	for _, a := range atts {
		if a == nil {
			continue
		}
		url := a.AttachmentCDNURL
		if url == "" {
			url = a.AttachmentCDNFallbackURL
		}
		isVideo := video || a.DashManifest != "" || strings.Contains(strings.ToLower(a.Typename), "video")
		if isVideo {
			parts = append(parts, msgPart{url: url, mimeType: "video/mp4", ext: ".mp4"})
		} else {
			parts = append(parts, msgPart{url: url, mimeType: "image/jpeg", ext: ".jpg"})
		}
	}
	return parts
}

// convert message content to text and attachment parts
func convertContent(msg *slidetypes.Message) (string, []msgPart) {
	text := msg.TextBody
	parts := []msgPart{}
	switch content := msg.Content.Content.(type) {
	case *slidetypes.MessageContentText:
		if text == "" {
			text = content.TextBody
		}
	case *slidetypes.MessageContentAdminText:
		text = adminText(content.TextFragments)
	case *slidetypes.MessageContentImage:
		parts = attachmentParts(content.Attachments, false)
	case *slidetypes.MessageContentVideo:
		parts = attachmentParts(content.Videos, true)
	case *slidetypes.MessageContentMultiMedia:
		parts = attachmentParts(content.Attachments, false)
	case *slidetypes.MessageContentAudio:
		for _, a := range content.AudioAttachments {
			if a != nil {
				parts = append(parts, msgPart{url: a.AttachmentCDNURL, mimeType: "audio/mp4", ext: ".m4a"})
			}
		}
	case *slidetypes.MessageContentAnimatedMedia:
		for _, a := range content.AnimatedMedia {
			if a == nil {
				continue
			}
			if a.AttachmentWebpURL != "" {
				parts = append(parts, msgPart{url: a.AttachmentWebpURL, mimeType: "image/webp", ext: ".webp"})
			} else if a.AttachmentMP4URL != "" {
				parts = append(parts, msgPart{url: a.AttachmentMP4URL, mimeType: "video/mp4", ext: ".mp4"})
			}
		}
	case *slidetypes.MessageContentSticker:
		parts = append(parts, msgPart{url: content.PreviewURL, mimeType: "image/webp", ext: ".webp"})
		if text == "" {
			text = content.AltText
		}
	case *slidetypes.MessageContentRavenImage:
		text = strings.TrimSpace(text + " [view-once photo - open Instagram to view]")
	case *slidetypes.MessageContentRavenVideo:
		text = strings.TrimSpace(text + " [view-once video - open Instagram to view]")
	case *slidetypes.MessageContentXMA:
		lines := []string{}
		if content.XMATextBody != "" {
			lines = append(lines, content.XMATextBody)
		}
		if x := content.XMA; x != nil {
			title := x.TitleText
			if title == "" {
				title = x.XMATitle
			}
			if x.HeaderTitleText != "" {
				lines = append(lines, x.HeaderTitleText)
			}
			if title != "" {
				lines = append(lines, title)
			}
			if x.CaptionBodyText != "" {
				lines = append(lines, x.CaptionBodyText)
			}
			if x.TargetURL != "" {
				lines = append(lines, x.TargetURL)
			}
			img := x.PreviewImage
			if img == nil {
				img = x.XMAPreviewImage
			}
			if img != nil && img.URL != "" {
				parts = append(parts, msgPart{url: img.URL, mimeType: "image/jpeg", ext: ".jpg"})
			}
		}
		if text == "" {
			text = strings.Join(lines, "\n")
		} else if len(lines) > 0 {
			text = text + "\n" + strings.Join(lines, "\n")
		}
	case *slidetypes.MessageContentMusicSticker:
		text = strings.TrimSpace(fmt.Sprintf("♪ %s - %s", content.AudioTrack.Title, content.AudioTrack.DisplayArtist))
	default:
		if text == "" {
			text = msg.IGDSnippet
		}
	}
	if text == "" && len(parts) == 0 {
		text = msg.IGDSnippet
	}
	return text, parts
}

func msgID(msg *slidetypes.Message) string {
	if msg.MessageID != "" {
		return msg.MessageID
	}
	return msg.ID
}

// notifyMessage sends a message to nchat. Multi-attachment messages become one entry per attachment.
// history: when true, messages are added to a cached history batch (fromMsgId set by caller).
func (c *IgConn) notifyMessage(chatId string, msg *slidetypes.Message, ownRead time.Time, otherRead time.Time,
	history bool, fromMsgId string) {
	if msg == nil {
		return
	}
	id := msgID(msg)
	fromMe := msg.SenderFBID == c.ownFBID
	senderId := strconv.FormatInt(msg.SenderFBID, 10)

	c.mu.Lock()
	meta := c.threads[chatId]
	if meta != nil && !meta.IsGroup && !fromMe {
		senderId = chatId // 1:1 chats: the other person is the chat
	}
	if meta != nil && fromMe {
		meta.LastOwnMsg = id
	}
	if len(c.msgInfo) > 20000 {
		c.msgInfo = make(map[string]sentInfo)
	}
	c.msgInfo[id] = sentInfo{senderId: senderId, fromMe: fromMe}
	c.mu.Unlock()

	ts := msg.TimestampMS.Time
	isRead := 1
	if fromMe {
		isRead = BoolToInt(!otherRead.IsZero() && !otherRead.Before(ts))
	} else if !ownRead.IsZero() {
		isRead = BoolToInt(!ownRead.Before(ts))
	}
	isEdited := BoolToInt(len(msg.SlideEditHistory) > 0)
	quotedId := msg.RepliedToMessageID

	text, parts := convertContent(msg)
	type entry struct {
		id, text, fileId, filePath string
		fileStatus                 int
	}
	entries := []entry{}
	if len(parts) == 0 {
		entries = append(entries, entry{id: id, text: text, fileStatus: FileStatusNone})
	}
	for i, p := range parts {
		eid := id
		if i > 0 {
			eid = id + "#" + strconv.Itoa(i)
		}
		fileId, filePath, status := c.fileRef(id, i, p.url, p.mimeType, p.ext)
		etext := ""
		if i == 0 {
			etext = text
		}
		entries = append(entries, entry{id: eid, text: etext, fileId: fileId, filePath: filePath, fileStatus: status})
	}

	for _, e := range entries {
		if history {
			CIgNewHistoryMessagesNotify(c.connId, chatId, e.id, senderId, e.text, BoolToInt(fromMe), quotedId, e.fileId,
				e.filePath, e.fileStatus, int(ts.Unix()), isRead, isEdited, fromMsgId, NotifyCache)
		} else {
			CIgNewMessagesNotify(c.connId, chatId, e.id, senderId, e.text, BoolToInt(fromMe), quotedId, e.fileId,
				e.filePath, e.fileStatus, int(ts.Unix()), isRead, isEdited)
		}
	}

	if !history {
		for _, r := range msg.Reactions {
			if r == nil || r.Reaction == "" {
				continue
			}
			rSender := strconv.FormatInt(r.SenderFBID, 10)
			CIgNewMessageReactionNotify(c.connId, chatId, id, rSender, r.Reaction, BoolToInt(r.SenderFBID == c.ownFBID))
		}
	}
}

func IgGetMessages(connId int, chatId string, limit int, fromMsgId string, owner int) int {
	defer recoverPanic("IgGetMessages")
	c := GetConn(connId)
	if c == nil || c.client == nil {
		return -1
	}
	if limit <= 0 || limit > 50 {
		limit = 20
	}
	baseId := strings.SplitN(fromMsgId, "#", 2)[0]

	req := &slidetypes.PaginateMessagesRequest{
		ThreadID:                chatId,
		FirstN:                  limit,
		InitialMessagePageCount: limit,
	}
	if baseId != "" {
		req.OlderThanMessageID = &baseId
	}
	CIgSetStatus(connId, FlagFetching)
	resp, err := c.client.PaginateMessages(c.ctx, req)
	CIgClearStatus(connId, FlagFetching)
	if err != nil || resp == nil || resp.ThreadInfo.AsIGDirectThread == nil ||
		resp.ThreadInfo.AsIGDirectThread.Messages == nil {
		LOG_WARNING(fmt.Sprintf("paginate messages %s: %v", chatId, err))
		CIgNewHistoryMessagesNotify(connId, chatId, "", "", "", 0, "", "", "", FileStatusNone, 0, 1, 0, fromMsgId, NotifySendCached)
		return -1
	}

	msgs := []*slidetypes.Message{}
	for _, edge := range resp.ThreadInfo.AsIGDirectThread.Messages.Edges {
		if edge.Node != nil && msgID(edge.Node) != baseId {
			msgs = append(msgs, edge.Node)
		}
	}
	// newest first, as nchat expects for history batches
	sort.SliceStable(msgs, func(i, j int) bool { return msgs[i].TimestampMS.Time.After(msgs[j].TimestampMS.Time) })

	for _, m := range msgs {
		c.notifyMessage(chatId, m, time.Time{}, time.Time{}, true, fromMsgId)
	}
	CIgNewHistoryMessagesNotify(connId, chatId, "", "", "", 0, "", "", "", FileStatusNone, 0, 1, 0, fromMsgId, NotifySendCached)
	return 0
}

func newOTID() string {
	return strconv.FormatInt(methods.GenerateEpochID(), 10)
}

func IgSendMessage(connId int, chatId string, text string, quotedId string, quotedText string, quotedSender string,
	filePath string, fileType string, editMsgId string, editMsgSent int, mentionsJson string) int {
	defer recoverPanic("IgSendMessage")
	c := GetConn(connId)
	if c == nil || c.client == nil {
		return -1
	}
	ctx := c.ctx
	quotedId = strings.SplitN(quotedId, "#", 2)[0]
	var replyTo *string
	if quotedId != "" {
		replyTo = &quotedId
	}

	// edit
	if editMsgId != "" {
		_, err := c.client.EditMessage(ctx, &slidetypes.EditMessageRequest{
			ThreadID:           chatId,
			Body:               slidetypes.SensitiveString{Value: text},
			OfflineThreadingID: newOTID(),
			TargetMessageID:    strings.SplitN(editMsgId, "#", 2)[0],
		})
		if err != nil {
			LOG_WARNING(fmt.Sprintf("edit message: %v", err))
			return -1
		}
		return 0
	}

	// attachment
	if filePath != "" {
		meta := c.ensureThread(chatId)
		threadFBID := int64(0)
		if meta != nil {
			threadFBID = meta.FBID
		}
		data, err := os.ReadFile(filePath)
		if err != nil {
			LOG_WARNING(fmt.Sprintf("read file: %v", err))
			return -1
		}
		mimeType := mime.TypeByExtension(strings.ToLower(filepath.Ext(filePath)))
		if mimeType == "" {
			mimeType = "application/octet-stream"
		}
		isVoice := strings.HasPrefix(mimeType, "audio/")
		up, err := c.client.GetHTTP().SendMercuryUploadRequest(ctx, threadFBID, &httpclient.MercuryUploadMedia{
			Filename:    filepath.Base(filePath),
			MimeType:    mimeType,
			MediaData:   data,
			IsVoiceClip: isVoice,
		})
		if err != nil {
			LOG_WARNING(fmt.Sprintf("upload: %v", err))
			return -1
		}
		attachmentId := up.Payload.RealMetadata.GetFbId()
		if attachmentId == 0 {
			LOG_WARNING("upload returned no attachment id")
			return -1
		}
		_, err = c.client.SendMedia(ctx, &slidetypes.SendMediaRequest{
			AttachmentFBID:     strconv.FormatInt(attachmentId, 10),
			ThreadID:           chatId,
			OfflineThreadingID: newOTID(),
			ReplyToMessageID:   replyTo,
		})
		if err != nil {
			LOG_WARNING(fmt.Sprintf("send media: %v", err))
			return -1
		}
		if strings.TrimSpace(text) == "" {
			return 0
		}
		replyTo = nil
	}

	// text
	threadId := chatId
	_, err := c.client.SendMessage(ctx, &slidetypes.SendTextRequest{
		IGThreadIGID:       &threadId,
		OfflineThreadingID: newOTID(),
		ReplyToMessageID:   replyTo,
		Text:               slidetypes.SensitiveString{Value: text},
		Mentions:           []slidetypes.InputMention{},
		MentionedUserIDs:   []string{},
		Commands:           []slidetypes.InputCommand{},
		SendAttribution:    ptr.Ptr("igd_web_chat_tab:in_thread"),
	})
	if err != nil {
		LOG_WARNING(fmt.Sprintf("send message: %v", err))
		return -1
	}
	return 0
}

func IgGetGroupMembers(connId int, chatId string) int {
	defer recoverPanic("IgGetGroupMembers")
	c := GetConn(connId)
	if c == nil {
		return -1
	}
	meta := c.ensureThread(chatId)
	if meta == nil {
		return -1
	}
	type member struct {
		Id   string `json:"id"`
		Name string `json:"name"`
	}
	c.mu.Lock()
	members := []member{}
	for _, fbid := range meta.Members {
		members = append(members, member{Id: strconv.FormatInt(fbid, 10), Name: c.users[fbid]})
	}
	c.mu.Unlock()
	data, _ := json.Marshal(members)
	CIgNewGroupMembersNotify(connId, chatId, string(data))
	return 0
}

func IgGetContacts(connId int) int {
	defer recoverPanic("IgGetContacts")
	c := GetConn(connId)
	if c == nil {
		return -1
	}
	c.mu.Lock()
	type kv struct {
		id, name, handle string
	}
	list := []kv{}
	for fbid, name := range c.users {
		if fbid != c.ownFBID && name != "" {
			list = append(list, kv{strconv.FormatInt(fbid, 10), name, atHandle(c.handles[fbid])})
		}
	}
	for chatId, t := range c.threads {
		if t.Title != "" {
			handle := ""
			if !t.IsGroup {
				handle = atHandle(t.Handle)
			}
			list = append(list, kv{chatId, t.Title, handle})
		}
	}
	c.mu.Unlock()
	for i, e := range list {
		notify := NotifyCache
		if i == len(list)-1 {
			notify = NotifySendCached
		}
		CIgNewContactsNotify(connId, e.id, e.name, e.handle, 0, 0, notify)
	}
	return 0
}

func IgGetChats(connId int) int {
	return 0
}

func IgIsGroupChat(connId int, chatId string) int {
	c := GetConn(connId)
	if c == nil {
		return 0
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	if t := c.threads[chatId]; t != nil {
		return BoolToInt(t.IsGroup)
	}
	return 0
}

func IgMarkMessageRead(connId int, chatId string, senderId string, msgId string) int {
	defer recoverPanic("IgMarkMessageRead")
	c := GetConn(connId)
	if c == nil || c.client == nil {
		return -1
	}
	meta := c.ensureThread(chatId)
	if meta == nil || meta.LongID == "" {
		return -1
	}
	baseId := strings.SplitN(msgId, "#", 2)[0]
	_, err := c.client.MarkRead(c.ctx, &slidetypes.MarkReadRequest{
		Metadata: slidetypes.MarkReadMetadata{IGThreadIGID: meta.LongID},
		Data:     slidetypes.MarkReadData{MessageID: baseId, ItemID: ptr.Ptr("")},
	})
	if err != nil {
		LOG_WARNING(fmt.Sprintf("mark read: %v", err))
		return -1
	}
	_, _ = c.client.MarkReadValidation(c.ctx, &slidetypes.MarkReadRequest{
		Metadata: slidetypes.MarkReadMetadata{IGThreadIGID: meta.LongID},
		Data:     slidetypes.MarkReadData{MessageID: baseId, MessageTimestampMS: jsontime.UnixMilliString{Time: time.Now()}},
	})
	return 0
}

func IgDeleteMessage(connId int, chatId string, senderId string, msgId string) int {
	defer recoverPanic("IgDeleteMessage")
	c := GetConn(connId)
	if c == nil || c.client == nil {
		return -1
	}
	meta := c.ensureThread(chatId)
	if meta == nil || meta.LongID == "" {
		return -1
	}
	_, err := c.client.UnsendMessage(c.ctx, &slidetypes.UnsendMessageRequest{
		MessageID: strings.SplitN(msgId, "#", 2)[0],
		SendData:  slidetypes.SendData{ThreadID: meta.LongID},
	})
	if err != nil {
		LOG_WARNING(fmt.Sprintf("unsend: %v", err))
		return -1
	}
	return 0
}

func IgDeleteChat(connId int, chatId string) int {
	defer recoverPanic("IgDeleteChat")
	c := GetConn(connId)
	if c == nil || c.client == nil {
		return -1
	}
	_, err := c.client.DeleteThread(c.ctx, &slidetypes.DeleteThreadRequest{ThreadID: chatId})
	if err != nil {
		LOG_WARNING(fmt.Sprintf("delete thread: %v", err))
		return -1
	}
	return 0
}

func IgArchiveChat(connId int, chatId string, isArchived int) int {
	return -1 // not supported by Instagram, nchat's local archive is used instead
}

func IgPinChat(connId int, chatId string, isPinned int) int {
	defer recoverPanic("IgPinChat")
	c := GetConn(connId)
	if c == nil || c.client == nil {
		return -1
	}
	_, err := c.client.PinThread(c.ctx, &slidetypes.PinThreadRequest{ThreadID: chatId, Pin: isPinned == 1})
	if err != nil {
		LOG_WARNING(fmt.Sprintf("pin thread: %v", err))
		return -1
	}
	CIgUpdatePinNotify(connId, chatId, isPinned, int(time.Now().Unix()))
	return 0
}

func IgSendTyping(connId int, chatId string, isTyping int) int {
	return 0 // typing indicators are not sent
}

func IgDownloadFile(connId int, chatId string, msgId string, fileId string, action int) int {
	defer recoverPanic("IgDownloadFile")
	c := GetConn(connId)
	if c == nil {
		return -1
	}
	var info DownloadInfo
	if err := json.Unmarshal([]byte(fileId), &info); err != nil || info.Version != downloadInfoVersion {
		CIgNewMessageFileNotify(connId, chatId, msgId, fileId, "", FileStatusDownloadFailed, action)
		return -1
	}

	status := FileStatusDownloaded
	if _, err := os.Stat(info.TargetPath); os.IsNotExist(err) {
		CIgSetStatus(connId, FlagFetching)
		status = downloadTo(info)
		CIgClearStatus(connId, FlagFetching)
	}
	CIgNewMessageFileNotify(connId, chatId, msgId, fileId, info.TargetPath, status, action)
	return 0
}

func downloadTo(info DownloadInfo) int {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
	defer cancel()
	_, body, err := mediadl.DownloadMedia(ctx, info.Mime, info.URL, maxMediaSize)
	if err != nil {
		LOG_WARNING(fmt.Sprintf("download: %v", err))
		return FileStatusDownloadFailed
	}
	defer body.Close()
	tmpPath := info.TargetPath + ".part"
	f, err := os.Create(tmpPath)
	if err != nil {
		return FileStatusDownloadFailed
	}
	_, err = io.Copy(f, body)
	_ = f.Close()
	if err != nil {
		_ = os.Remove(tmpPath)
		return FileStatusDownloadFailed
	}
	if err = os.Rename(tmpPath, info.TargetPath); err != nil {
		return FileStatusDownloadFailed
	}
	return FileStatusDownloaded
}

func IgSendReaction(connId int, chatId string, senderId string, msgId string, emoji string, prevEmoji string) int {
	defer recoverPanic("IgSendReaction")
	c := GetConn(connId)
	if c == nil || c.client == nil {
		return -1
	}
	status := slidetypes.ReactionStatusCreated
	reaction := emoji
	if emoji == "" {
		status = slidetypes.ReactionStatusDeleted
		reaction = prevEmoji
	}
	baseId := strings.SplitN(msgId, "#", 2)[0]
	_, err := c.client.SendReaction(c.ctx, &slidetypes.CreateReactionRequest{Input: slidetypes.ReactionInput{
		Emoji:          reaction,
		MessageID:      baseId,
		ReactionStatus: status,
		ThreadID:       chatId,
	}})
	if err != nil {
		LOG_WARNING(fmt.Sprintf("send reaction: %v", err))
		return -1
	}
	return 0
}

// --- realtime events ---

func (c *IgConn) handleEvent(ctx context.Context, rawEvt slidetypes.ClientEvent) error {
	defer func() {
		if r := recover(); r != nil {
			LOG_WARNING(fmt.Sprintf("panic in event handler: %v", r))
		}
	}()

	switch evt := rawEvt.(type) {
	case *slidetypes.Connected:
		CIgClearStatus(c.connId, FlagConnecting)
		CIgSetStatus(c.connId, FlagOnline)
	case *slidetypes.Disconnected:
		LOG_INFO(fmt.Sprintf("disconnected: %v", evt.Error))
		CIgClearStatus(c.connId, FlagOnline)
		CIgSetStatus(c.connId, FlagConnecting)
	case *slidetypes.AuthError:
		LOG_WARNING(fmt.Sprintf("auth error: %v - run nchat --setup to log in again", evt.Error))
		CIgClearStatus(c.connId, FlagOnline)
		CIgSetStatus(c.connId, FlagOffline)
	case *slidetypes.ResnapshotRequired:
		go func() {
			defer recoverPanic("resnapshot")
			_, mailbox, err := c.client.LoadIndex(c.ctx)
			if err == nil {
				c.syncMailbox(mailbox)
			}
		}()
	case *slidetypes.Delta:
		c.handleDelta(evt)
	case *slidetypes.TypingNotification:
		chatId := evt.ThreadID
		c.mu.Lock()
		if id, ok := c.longToId[chatId]; ok {
			chatId = id
		}
		meta := c.threads[chatId]
		c.mu.Unlock()
		userId := strconv.FormatInt(evt.SenderID, 10)
		if meta != nil && !meta.IsGroup {
			userId = chatId
		}
		CIgNewTypingNotify(c.connId, chatId, userId, BoolToInt(evt.ActivityStatus == 1))
	}
	return nil
}

func (c *IgConn) handleDelta(d *slidetypes.Delta) {
	chatId := d.ThreadIGID
	if nm, ok := d.Data.(*slidetypes.NewMessageEvent); ok && chatId == "" && nm.Message != nil {
		chatId = nm.Message.ThreadFBID
	}
	if chatId == "" {
		return
	}
	c.mu.Lock()
	if id, ok := c.longToId[chatId]; ok {
		chatId = id
	}
	_, known := c.threads[chatId]
	c.mu.Unlock()

	switch evt := d.Data.(type) {
	case *slidetypes.NewMessageEvent:
		if !known {
			c.ensureThread(chatId)
		}
		c.notifyMessage(chatId, evt.Message, time.Time{}, time.Time{}, false, "")
	case *slidetypes.AdminMessageEvent:
		if !known {
			c.ensureThread(chatId)
		}
		c.notifyMessage(chatId, evt.Message, time.Time{}, time.Time{}, false, "")
	case *slidetypes.EditMessageEvent:
		c.mu.Lock()
		info, ok := c.msgInfo[evt.MessageID]
		c.mu.Unlock()
		if !ok {
			return
		}
		CIgNewMessagesNotify(c.connId, chatId, evt.MessageID, info.senderId, evt.TextBody, BoolToInt(info.fromMe), "",
			"", "", FileStatusNone, int(time.Now().Unix()), 1, 1)
	case *slidetypes.CreateReactionEvent:
		sender := strconv.FormatInt(evt.Reaction.SenderFBID, 10)
		CIgNewMessageReactionNotify(c.connId, chatId, evt.MessageID, sender, evt.Reaction.Reaction,
			BoolToInt(evt.Reaction.SenderFBID == c.ownFBID))
	case *slidetypes.DeleteReactionEvent:
		sender := strconv.FormatInt(evt.Reaction.SenderFBID, 10)
		CIgNewMessageReactionNotify(c.connId, chatId, evt.MessageID, sender, "",
			BoolToInt(evt.Reaction.SenderFBID == c.ownFBID))
	case *slidetypes.ReadReceiptEvent:
		if evt.ReadReceipt.ParticipantFBID == c.ownFBID {
			return
		}
		c.mu.Lock()
		last := ""
		if meta := c.threads[chatId]; meta != nil {
			last = meta.LastOwnMsg
		}
		c.mu.Unlock()
		if last != "" {
			CIgNewMessageStatusNotify(c.connId, chatId, last, 1)
		}
	case *slidetypes.MarkReadEvent:
		c.mu.Lock()
		meta := c.threads[chatId]
		c.mu.Unlock()
		if meta != nil {
			CIgNewChatsNotify(c.connId, chatId, 0, BoolToInt(meta.IsMuted), BoolToInt(meta.IsPinned), 0, 0)
		}
	case *slidetypes.DeleteMessageEvent:
		CIgDeleteMessageNotify(c.connId, chatId, evt.MessageID, 0)
	case *slidetypes.DeleteThreadEvent:
		CIgDeleteChatNotify(c.connId, chatId)
	case *slidetypes.PinThreadEvent:
		c.mu.Lock()
		if meta := c.threads[chatId]; meta != nil {
			meta.IsPinned = evt.IsPinned
		}
		c.mu.Unlock()
		CIgUpdatePinNotify(c.connId, chatId, BoolToInt(evt.IsPinned), int(time.Now().Unix()))
	case *slidetypes.MuteThreadEvent:
		c.mu.Lock()
		if meta := c.threads[chatId]; meta != nil {
			meta.IsMuted = evt.IsMutedNow
		}
		c.mu.Unlock()
		CIgUpdateMuteNotify(c.connId, chatId, BoolToInt(evt.IsMutedNow))
	case *slidetypes.UpdateThreadNameEvent:
		handle := ""
		c.mu.Lock()
		if meta := c.threads[chatId]; meta != nil {
			meta.Title = evt.ThreadName
			if !meta.IsGroup {
				handle = atHandle(meta.Handle)
			}
		}
		c.mu.Unlock()
		CIgNewContactsNotify(c.connId, chatId, evt.ThreadName, handle, 0, 0, NotifyDirect)
		if evt.Message != nil {
			c.notifyMessage(chatId, evt.Message, time.Time{}, time.Time{}, false, "")
		}
	case *slidetypes.ParticipantJoinEvent:
		if evt.Message != nil {
			c.notifyMessage(chatId, evt.Message, time.Time{}, time.Time{}, false, "")
		}
	case *slidetypes.ParticipantLeaveEvent:
		if evt.Message != nil {
			c.notifyMessage(chatId, evt.Message, time.Time{}, time.Time{}, false, "")
		}
	}
}

func main() {
}
