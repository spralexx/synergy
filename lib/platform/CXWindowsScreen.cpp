/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2002 Chris Schoeneman
 * 
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file COPYING that should have accompanied this file.
 * 
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "CXWindowsScreen.h"
#include "CXWindowsClipboard.h"
#include "CXWindowsScreenSaver.h"
#include "CXWindowsUtil.h"
#include "CClipboard.h"
#include "IScreenReceiver.h"
#include "IPrimaryScreenReceiver.h"
#include "XScreen.h"
#include "CLock.h"
#include "CThread.h"
#include "CLog.h"
#include "CStopwatch.h"
#include "CStringUtil.h"
#include "IJob.h"
#include <cstring>
#if defined(X_DISPLAY_MISSING)
#	error X11 is required to build synergy
#else
#	include <X11/X.h>
#	include <X11/Xutil.h>
#	define XK_XKB_KEYS
#	include <X11/keysymdef.h>
#	if defined(HAVE_X11_EXTENSIONS_XTEST_H)
#		include <X11/extensions/XTest.h>
#	else
#		error The XTest extension is required to build synergy
#	endif
#	if HAVE_X11_EXTENSIONS_XINERAMA_H
		// Xinerama.h may lack extern "C" for inclusion by C++
		extern "C" {
#		include <X11/extensions/Xinerama.h>
		}
#	endif
#endif
#if UNIX_LIKE
#	if HAVE_POLL
#		include <sys/poll.h>
#	else
#		if HAVE_SYS_SELECT_H
#			include <sys/select.h>
#		endif
#		if HAVE_SYS_TIME_H
#			include <sys/time.h>
#		endif
#		if HAVE_SYS_TYPES_H
#			include <sys/types.h>
#		endif
#		if HAVE_UNISTD_H
#			include <unistd.h>
#		endif
#	endif
#endif
#include "CArch.h"

// map "Internet" keys to KeyIDs
static const KeySym		g_map1008FF[] =
{
	/* 0x00 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x08 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x10 */ 0, kKeyAudioDown, kKeyAudioMute, kKeyAudioUp,
	/* 0x14 */ kKeyAudioPlay, kKeyAudioStop, kKeyAudioPrev, kKeyAudioNext,
	/* 0x18 */ kKeyWWWHome, kKeyAppMail, 0, kKeyWWWSearch, 0, 0, 0, 0,
	/* 0x20 */ 0, 0, 0, 0, 0, 0, kKeyWWWBack, kKeyWWWForward,
	/* 0x28 */ kKeyWWWStop, kKeyWWWRefresh, 0, 0, 0, 0, 0, 0,
	/* 0x30 */ kKeyWWWFavorites, 0, kKeyAppMedia, 0, 0, 0, 0, 0,
	/* 0x38 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x40 */ kKeyAppUser1, kKeyAppUser2, 0, 0, 0, 0, 0, 0,
	/* 0x48 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x50 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x58 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x60 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x68 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x70 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x78 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x80 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x88 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x90 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x98 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xa0 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xa8 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xb0 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xb8 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xc0 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xc8 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xd0 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xd8 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xe0 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xe8 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xf0 */ 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xf8 */ 0, 0, 0, 0, 0, 0, 0, 0
};


//
// CXWindowsScreen
//

CXWindowsScreen*		CXWindowsScreen::s_screen = NULL;

CXWindowsScreen::CXWindowsScreen(IScreenReceiver* receiver,
				IPrimaryScreenReceiver* primaryReceiver) :
	m_isPrimary(primaryReceiver != NULL),
	m_display(NULL),
	m_root(None),
	m_window(None),
	m_receiver(receiver),
	m_primaryReceiver(primaryReceiver),
	m_isOnScreen(m_isPrimary),
	m_x(0), m_y(0),
	m_w(0), m_h(0),
	m_xCursor(0), m_yCursor(0),
	m_xCenter(0), m_yCenter(0),
	m_keyState(NULL),
	m_keyMapper(),
	m_im(NULL),
	m_ic(NULL),
	m_lastKeycode(0),
	m_atomQuit(None),
	m_screensaver(NULL),
	m_screensaverNotify(false),
	m_atomScreensaver(None),
	m_oneShotTimer(NULL),
	m_xtestIsXineramaUnaware(true)
{
	assert(s_screen   == NULL);
	assert(m_receiver != NULL);

	s_screen = this;

	// no clipboards to start with
	for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
		m_clipboard[id] = NULL;
	}
}

CXWindowsScreen::~CXWindowsScreen()
{
	assert(s_screen  != NULL);
	assert(m_display == NULL);

	delete m_oneShotTimer;
	s_screen = NULL;
}

void
CXWindowsScreen::open(IKeyState* keyState)
{
	assert(m_display == NULL);

	try {
		// set the X I/O error handler so we catch the display disconnecting
		XSetIOErrorHandler(&CXWindowsScreen::ioErrorHandler);

		// get the DISPLAY
		const char* display = getenv("DISPLAY");
		if (display == NULL) {
			display = ":0.0";
		}

		// open the display
		LOG((CLOG_DEBUG "XOpenDisplay(\"%s\")", display));
		m_display = XOpenDisplay(display);
		if (m_display == NULL) {
			throw XScreenUnavailable(60.0);
		}

		// verify the availability of the XTest extension
		if (!m_isPrimary) {
			int majorOpcode, firstEvent, firstError;
			if (!XQueryExtension(m_display, XTestExtensionName,
								&majorOpcode, &firstEvent, &firstError)) {
				LOG((CLOG_ERR "XTEST extension not available"));
				throw XScreenOpenFailure();
			}
		}

		// get root window
		m_root = DefaultRootWindow(m_display);

		// get shape of default screen
		m_x = 0;
		m_y = 0;
		m_w = WidthOfScreen(DefaultScreenOfDisplay(m_display));
		m_h = HeightOfScreen(DefaultScreenOfDisplay(m_display));
		LOG((CLOG_DEBUG "screen shape: %d,%d %dx%d", m_x, m_y, m_w, m_h));

		// get center of default screen
		m_xCenter = m_x + (m_w >> 1);
		m_yCenter = m_y + (m_h >> 1);

		// check if xinerama is enabled and there is more than one screen.
		// get center of first Xinerama screen.  Xinerama appears to have
		// a bug when XWarpPointer() is used in combination with
		// XGrabPointer().  in that case, the warp is successful but the
		// next pointer motion warps the pointer again, apparently to
		// constrain it to some unknown region, possibly the region from
		// 0,0 to Wm,Hm where Wm (Hm) is the minimum width (height) over
		// all physical screens.  this warp only seems to happen if the
		// pointer wasn't in that region before the XWarpPointer().  the
		// second (unexpected) warp causes synergy to think the pointer
		// has been moved when it hasn't.  to work around the problem,
		// we warp the pointer to the center of the first physical
		// screen instead of the logical screen.
		m_xinerama = false;
#if HAVE_X11_EXTENSIONS_XINERAMA_H
		int eventBase, errorBase;
		if (XineramaQueryExtension(m_display, &eventBase, &errorBase) &&
			XineramaIsActive(m_display)) {
			int numScreens;
			XineramaScreenInfo* screens;
			screens = XineramaQueryScreens(m_display, &numScreens);
			if (screens != NULL) {
				if (numScreens > 1) {
					m_xinerama = true;
					m_xCenter  = screens[0].x_org + (screens[0].width  >> 1);
					m_yCenter  = screens[0].y_org + (screens[0].height >> 1);
				}
				XFree(screens);
			}
		}
#endif

		// create the window
		m_window = createWindow();
		if (m_window == None) {
			throw XScreenOpenFailure();
		}
		LOG((CLOG_DEBUG "window is 0x%08x", m_window));

		if (m_isPrimary) {
			// start watching for events on other windows
			selectEvents(m_root);

			// prepare to use input methods
			openIM();
		}

		// initialize the clipboards
		for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
			m_clipboard[id] = new CXWindowsClipboard(m_display, m_window, id);
		}

		// initialize the screen saver
		m_atomScreensaver = XInternAtom(m_display,
										"SYNERGY_SCREENSAVER", False);
		m_screensaver     = new CXWindowsScreenSaver(this, m_display);
	}
	catch (...) {
		close();
		throw;
	}

	// save the IKeyState
	m_keyState = keyState;

	// we'll send ourself an event of this type to exit the main loop
	m_atomQuit = XInternAtom(m_display, "SYNERGY_QUIT", False);

	if (!m_isPrimary) {
		// become impervious to server grabs
		XTestGrabControl(m_display, True);
	}
}

void
CXWindowsScreen::close()
{
	// done with m_keyState
	m_keyState = NULL;

	// done with screen saver
	delete m_screensaver;

	// destroy clipboards
	for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
		delete m_clipboard[id];
		m_clipboard[id] = NULL;
	}

	// done with input methods
	if (m_ic != NULL) {
		XDestroyIC(m_ic);
	}
	if (m_im != NULL) {
		XCloseIM(m_im);
	}

	// done with window
	if (m_window != None) {
		XDestroyWindow(m_display, m_window);
	}

	// close the display
	if (m_display != NULL) {
		XCloseDisplay(m_display);
	}

	// restore error handler
	XSetIOErrorHandler(NULL);

	// reset state
	m_atomQuit        = None;
	m_screensaver     = NULL;
	m_atomScreensaver = None;
	m_ic              = NULL;
	m_im              = NULL;
	m_window          = None;
	m_root            = None;
	m_display         = NULL;
}

void
CXWindowsScreen::enable()
{
	CLock lock(&m_mutex);

	if (!m_isPrimary) {
		// get the keyboard control state
		XKeyboardState keyControl;
		XGetKeyboardControl(m_display, &keyControl);
		m_autoRepeat = (keyControl.global_auto_repeat == AutoRepeatModeOn);

		// move hider window under the cursor center
		XMoveWindow(m_display, m_window, m_xCenter, m_yCenter);

		// raise and show the window
		// FIXME -- take focus?
		XMapRaised(m_display, m_window);

		// warp the mouse to the cursor center
		fakeMouseMove(m_xCenter, m_yCenter);
	}
}

void
CXWindowsScreen::disable()
{
	CLock lock(&m_mutex);

	// release input context focus
	if (m_ic != NULL) {
		XUnsetICFocus(m_ic);
	}

	// unmap the hider/grab window.  this also ungrabs the mouse and
	// keyboard if they're grabbed.
	XUnmapWindow(m_display, m_window);

	// restore auto-repeat state
	if (!m_isPrimary && m_autoRepeat) {
		XAutoRepeatOn(m_display);
	}
}

void
CXWindowsScreen::mainLoop()
{
	// wait for an event in a cancellable way and don't lock the
	// display while we're waiting.  we use CLock to ensure that
	// we unlock on exit but we directly unlock/lock the mutex
	// for certain sections when we mustn't hold the lock.  it's
	// very important that these sections not return (even by
	// exception or cancellation) without first reestablishing
	// the lock.
	XEvent event;
	CLock lock(&m_mutex);

	for (;;) {

#if UNIX_LIKE

		// compute timeout to next timer
		double dtimeout;
		{
			CLock timersLock(&m_timersMutex);
			dtimeout = (m_timers.empty() ? -1.0 : m_timers.top());
			if (m_oneShotTimer != NULL &&
				(dtimeout == -1.0 || *m_oneShotTimer < dtimeout)) {
				dtimeout = *m_oneShotTimer;
			}
		}

		// use poll() to wait for a message from the X server or for timeout.
		// this is a good deal more efficient than polling and sleeping.
#if HAVE_POLL
		struct pollfd pfds[1];
		pfds[0].fd     = ConnectionNumber(m_display);
		pfds[0].events = POLLIN;
		int timeout    = static_cast<int>(1000.0 * dtimeout);
#else
		struct timeval timeout;
		struct timeval* timeoutPtr;
		if (dtimeout < 0.0) {
			timeoutPtr = NULL;
		}
		else {
			timeout.tv_sec  = static_cast<int>(dtimeout);
			timeout.tv_usec = static_cast<int>(1.0e+6 *
									(dtimeout - timeout.tv_sec));
			timeoutPtr      = &timeout;
		}

		// initialize file descriptor sets
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(ConnectionNumber(m_display), &rfds);
#endif

		// wait for message from X server or for timeout.  also check
		// if the thread has been cancelled.  poll() should return -1
		// with EINTR when the thread is cancelled.
		CThread::testCancel();
		m_mutex.unlock();
#if HAVE_POLL
		poll(pfds, 1, timeout);
#else
		select(ConnectionNumber(m_display) + 1,
							SELECT_TYPE_ARG234 &rfds,
							SELECT_TYPE_ARG234 NULL,
							SELECT_TYPE_ARG234 NULL,
							SELECT_TYPE_ARG5   timeoutPtr);
#endif
		m_mutex.lock();
		CThread::testCancel();

		// process timers
		processTimers();

#else // !UNIX_LIKE

		// poll for pending events and process timers
		while (XPending(m_display) == 0) {
			// check timers
			if (processTimers()) {
				continue;
			}

			// wait
			try {
				m_mutex.unlock();
				CThread::sleep(0.01);
				m_mutex.lock();
			}
			catch (...) {
				m_mutex.lock();
				throw;
			}
		}

#endif // !UNIX_LIKE

		// process events
		while (XPending(m_display) > 0) {
			// get the event
			XNextEvent(m_display, &event);
			if (isQuitEvent(&event)) {
				return;
			}

			// process the event
			onEvent(&event);
		}
	}
}

void
CXWindowsScreen::exitMainLoop()
{
	// send ourself a quit event.  this will wake up the event loop
	// and cause it to exit.
	if (m_atomQuit != None) {
		XEvent event;
		event.xclient.type         = ClientMessage;
		event.xclient.display      = m_display;
		event.xclient.window       = m_window;
		event.xclient.message_type = m_atomQuit;
		event.xclient.format       = 32;
		event.xclient.data.l[0]    = 0;
		event.xclient.data.l[1]    = 0;
		event.xclient.data.l[2]    = 0;
		event.xclient.data.l[3]    = 0;
		event.xclient.data.l[4]    = 0;
		CLock lock(&m_mutex);
		CXWindowsUtil::CErrorLock errorLock(m_display);
		XSendEvent(m_display, m_window, False, 0, &event);
	}
}

void
CXWindowsScreen::enter()
{
	CLock lock(&m_mutex);

	// release input context focus
	if (m_ic != NULL) {
		XUnsetICFocus(m_ic);
	}

	// unmap the hider/grab window.  this also ungrabs the mouse and
	// keyboard if they're grabbed.
	XUnmapWindow(m_display, m_window);

/* maybe call this if entering for the screensaver
	// set keyboard focus to root window.  the screensaver should then
	// pick up key events for when the user enters a password to unlock. 
	XSetInputFocus(m_display, PointerRoot, PointerRoot, CurrentTime);
*/

	if (!m_isPrimary) {
		// get the keyboard control state
		XKeyboardState keyControl;
		XGetKeyboardControl(m_display, &keyControl);
		m_autoRepeat = (keyControl.global_auto_repeat == AutoRepeatModeOn);

		// turn off auto-repeat.  we do this so fake key press events don't
		// cause the local server to generate their own auto-repeats of
		// those keys.
		XAutoRepeatOff(m_display);
	}

	// now on screen
	m_isOnScreen = true;
}

bool
CXWindowsScreen::leave()
{
	CLock lock(&m_mutex);

	if (!m_isPrimary) {
		// restore the previous keyboard auto-repeat state.  if the user
		// changed the auto-repeat configuration while on the client then
		// that state is lost.  that's because we can't get notified by
		// the X server when the auto-repeat configuration is changed so
		// we can't track the desired configuration.
		if (m_autoRepeat) {
			XAutoRepeatOn(m_display);
		}

		// move hider window under the cursor center
		XMoveWindow(m_display, m_window, m_xCenter, m_yCenter);
	}

	// raise and show the window
	// FIXME -- take focus?
	XMapRaised(m_display, m_window);

	// grab the mouse and keyboard, if primary and possible
	if (m_isPrimary && !grabMouseAndKeyboard()) {
		XUnmapWindow(m_display, m_window);
		return false;
	}

	// now warp the mouse.  we warp after showing the window so we're
	// guaranteed to get the mouse leave event and to prevent the
	// keyboard focus from changing under point-to-focus policies.
	if (m_isPrimary) {
		warpCursor(m_xCenter, m_yCenter);
	}
	else {
		fakeMouseMove(m_xCenter, m_yCenter);
	}

	// set input context focus to our window
	if (m_ic != NULL) {
		XmbResetIC(m_ic);
		XSetICFocus(m_ic);
	}

	// now off screen
	m_isOnScreen = false;

	return true;
}

bool
CXWindowsScreen::setClipboard(ClipboardID id, const IClipboard* clipboard)
{
	CLock lock(&m_mutex);

	// fail if we don't have the requested clipboard
	if (m_clipboard[id] == NULL) {
		return false;
	}

	// get the actual time.  ICCCM does not allow CurrentTime.
	Time timestamp = CXWindowsUtil::getCurrentTime(
								m_display, m_clipboard[id]->getWindow());

	if (clipboard != NULL) {
		// save clipboard data
		return CClipboard::copy(m_clipboard[id], clipboard, timestamp);
	}
	else {
		// assert clipboard ownership
		if (!m_clipboard[id]->open(timestamp)) {
			return false;
		}
		m_clipboard[id]->empty();
		m_clipboard[id]->close();
		return true;
	}
}

void
CXWindowsScreen::checkClipboards()
{
	// do nothing, we're always up to date
}

void
CXWindowsScreen::openScreensaver(bool notify)
{
	CLock lock(&m_mutex);
	assert(m_screensaver != NULL);

	m_screensaverNotify = notify;
	if (m_screensaverNotify) {
		m_screensaver->setNotify(m_window);
	}
	else {
		m_screensaver->disable();
	}
}

void
CXWindowsScreen::closeScreensaver()
{
	CLock lock(&m_mutex);
	if (m_screensaver != NULL) {
		if (m_screensaverNotify) {
			m_screensaver->setNotify(None);
		}
		else {
			m_screensaver->enable();
		}
	}
}

void
CXWindowsScreen::screensaver(bool activate)
{
	CLock lock(&m_mutex);
	assert(m_screensaver != NULL);

	if (activate) {
		m_screensaver->activate();
	}
	else {
		m_screensaver->deactivate();
	}
}

void
CXWindowsScreen::resetOptions()
{
	m_xtestIsXineramaUnaware = true;
}

void
CXWindowsScreen::setOptions(const COptionsList& options)
{
	for (UInt32 i = 0, n = options.size(); i < n; i += 2) {
		if (options[i] == kOptionXTestXineramaUnaware) {
			m_xtestIsXineramaUnaware = (options[i + 1] != 0);
			LOG((CLOG_DEBUG1 "XTest is Xinerama unaware %s", m_xtestIsXineramaUnaware ? "true" : "false"));
		}
	}
}

void
CXWindowsScreen::updateKeys()
{
	CLock lock(&m_mutex);

	// update keyboard and mouse button mappings
	m_keyMapper.update(m_display, m_keyState);
	updateButtons();
}

bool
CXWindowsScreen::isPrimary() const
{
	return m_isPrimary;
}

bool
CXWindowsScreen::getClipboard(ClipboardID id, IClipboard* clipboard) const
{
	assert(clipboard != NULL);

	// block others from using the display while we get the clipboard
	CLock lock(&m_mutex);

	// fail if we don't have the requested clipboard
	if (m_clipboard[id] == NULL) {
		return false;
	}

	// get the actual time.  ICCCM does not allow CurrentTime.
	Time timestamp = CXWindowsUtil::getCurrentTime(
								m_display, m_clipboard[id]->getWindow());

	// copy the clipboard
	return CClipboard::copy(clipboard, m_clipboard[id], timestamp);
}

void
CXWindowsScreen::getShape(SInt32& x, SInt32& y, SInt32& w, SInt32& h) const
{
	x = m_x;
	y = m_y;
	w = m_w;
	h = m_h;
}

void
CXWindowsScreen::getCursorPos(SInt32& x, SInt32& y) const
{
	CLock lock(&m_mutex);
	assert(m_display != NULL);

	Window root, window;
	int mx, my, xWindow, yWindow;
	unsigned int mask;
	if (XQueryPointer(m_display, m_root, &root, &window,
								&mx, &my, &xWindow, &yWindow, &mask)) {
		x = mx;
		y = my;
	}
	else {
		x = m_xCenter;
		y = m_yCenter;
	}
}

void
CXWindowsScreen::reconfigure(UInt32)
{
	// do nothing
}

void
CXWindowsScreen::warpCursor(SInt32 x, SInt32 y)
{
	CLock lock(&m_mutex);

	// warp mouse
	warpCursorNoFlush(x, y);

	// remove all input events before and including warp
	XEvent event;
	while (XCheckMaskEvent(m_display, PointerMotionMask |
								ButtonPressMask | ButtonReleaseMask |
								KeyPressMask | KeyReleaseMask |
								KeymapStateMask,
								&event)) {
		// do nothing
	}

	// save position as last position
	m_xCursor = x;
	m_yCursor = y;
}

UInt32
CXWindowsScreen::addOneShotTimer(double timeout)
{
	CLock lock(&m_timersMutex);
	// FIXME -- support multiple one-shot timers
	m_oneShotTimer = new CTimer(NULL, m_time.getTime(), timeout);
	return 0;
}

SInt32
CXWindowsScreen::getJumpZoneSize() const
{
	return 1;
}

bool
CXWindowsScreen::isAnyMouseButtonDown() const
{
	CLock lock(&m_mutex);

	// query the pointer to get the button state
	Window root, window;
	int xRoot, yRoot, xWindow, yWindow;
	unsigned int state;
	if (XQueryPointer(m_display, m_root, &root, &window,
								&xRoot, &yRoot, &xWindow, &yWindow, &state)) {
		return ((state & (Button1Mask | Button2Mask | Button3Mask |
							Button4Mask | Button5Mask)) != 0);
	}

	return false;
}

const char*
CXWindowsScreen::getKeyName(KeyButton keycode) const
{
	CLock lock(&m_mutex);

	KeySym keysym = XKeycodeToKeysym(m_display, keycode, 0);
	char* name    = XKeysymToString(keysym);
	if (name != NULL) {
		return name;
	}
	else {
		static char buffer[20];
		return strcpy(buffer,
					CStringUtil::print("keycode %d", keycode).c_str());
	}
}

void
CXWindowsScreen::fakeKeyEvent(KeyButton keycode, bool press) const
{
	CLock lock(&m_mutex);
	XTestFakeKeyEvent(m_display, keycode, press ? True : False, CurrentTime);
	XFlush(m_display);
}

bool
CXWindowsScreen::fakeCtrlAltDel() const
{
	// pass keys through unchanged
	return false;
}

void
CXWindowsScreen::fakeMouseButton(ButtonID button, bool press) const
{
	const unsigned int xButton = mapButtonToX(button);
	if (xButton != 0) {
		CLock lock(&m_mutex);
		XTestFakeButtonEvent(m_display, xButton,
							press ? True : False, CurrentTime);
		XFlush(m_display);
	}
}

void
CXWindowsScreen::fakeMouseMove(SInt32 x, SInt32 y) const
{
	CLock lock(&m_mutex);
	if (m_xinerama && m_xtestIsXineramaUnaware) {
		XWarpPointer(m_display, None, m_root, 0, 0, 0, 0, x, y);
	}
	else {
		XTestFakeMotionEvent(m_display, DefaultScreen(m_display),
							x, y, CurrentTime);
	}
	XFlush(m_display);
}

void
CXWindowsScreen::fakeMouseWheel(SInt32 delta) const
{
	// choose button depending on rotation direction
	const unsigned int xButton = mapButtonToX(static_cast<ButtonID>(
												(delta >= 0) ? -1 : -2));
	if (xButton == 0) {
		return;
	}

	// now use absolute value of delta
	if (delta < 0) {
		delta = -delta;
	}

	// send as many clicks as necessary
	CLock lock(&m_mutex);
	for (; delta >= 120; delta -= 120) {
		XTestFakeButtonEvent(m_display, xButton, True, CurrentTime);
		XTestFakeButtonEvent(m_display, xButton, False, CurrentTime);
	}
	XFlush(m_display);
}

KeyButton
CXWindowsScreen::mapKey(IKeyState::Keystrokes& keys,
				const IKeyState& keyState, KeyID id,
				KeyModifierMask desiredMask,
				bool isAutoRepeat) const
{
	return m_keyMapper.mapKey(keys, keyState, id, desiredMask, isAutoRepeat);
}

bool
CXWindowsScreen::isQuitEvent(XEvent* event) const
{
	return (m_atomQuit                  != None &&
			event->type                 == ClientMessage &&
			event->xclient.window       == m_window &&
			event->xclient.message_type == m_atomQuit);
}

Window
CXWindowsScreen::createWindow() const
{
	// default window attributes.  we don't want the window manager
	// messing with our window and we don't want the cursor to be
	// visible inside the window.
	XSetWindowAttributes attr;
	attr.do_not_propagate_mask = 0;
	attr.override_redirect     = True;
	attr.cursor                = createBlankCursor();

	// adjust attributes and get size and shape
	SInt32 x, y, w, h;
	if (m_isPrimary) {
		// grab window attributes.  this window is used to capture user
		// input when the user is focused on another client.  it covers
		// the whole screen.
		attr.event_mask = PointerMotionMask |
							 ButtonPressMask | ButtonReleaseMask |
							 KeyPressMask | KeyReleaseMask |
							 KeymapStateMask | PropertyChangeMask;
		x = m_x;
		y = m_y;
		w = m_w;
		h = m_h;
	}
	else {
		// cursor hider window attributes.  this window is used to hide the
		// cursor when it's not on the screen.  the window is hidden as soon
		// as the cursor enters the screen or the display's real mouse is
		// moved.  we'll reposition the window as necessary so its
		// position here doesn't matter.  it only needs to be 1x1 because
		// it only needs to contain the cursor's hotspot.
		attr.event_mask = LeaveWindowMask;
		x = 0;
		y = 0;
		w = 1;
		h = 1;
	}

	// create and return the window
	return XCreateWindow(m_display, m_root, x, y, w, h, 0, 0,
							InputOnly, CopyFromParent,
							CWDontPropagate | CWEventMask |
							CWOverrideRedirect | CWCursor,
							&attr);
}

void
CXWindowsScreen::openIM()
{
	// open the input methods
	XIM im = XOpenIM(m_display, NULL, NULL, NULL);
	if (im == NULL) {
		return;
	}

	// find the appropriate style.  synergy supports XIMPreeditNothing
	// only at the moment.
	XIMStyles* styles;
	if (XGetIMValues(im, XNQueryInputStyle, &styles, NULL) != NULL ||
		styles == NULL) {
		LOG((CLOG_WARN "cannot get IM styles"));
		XCloseIM(im);
		return;
	}
	XIMStyle style = 0;
	for (unsigned short i = 0; i < styles->count_styles; ++i) {
		style = styles->supported_styles[i];
		if ((style & XIMPreeditNothing) != 0) {
			if ((style & (XIMStatusNothing | XIMStatusNone)) != 0) {
				break;
			}
		}
	}
	XFree(styles);
	if (style == 0) {
		LOG((CLOG_WARN "no supported IM styles"));
		XCloseIM(im);
		return;
	}

	// create an input context for the style and tell it about our window
	XIC ic = XCreateIC(im, XNInputStyle, style, XNClientWindow, m_window, NULL);
	if (ic == NULL) {
		LOG((CLOG_WARN "cannot create IC"));
		XCloseIM(im);
		return;
	}

	// find out the events we must select for and do so
	unsigned long mask;
	if (XGetICValues(ic, XNFilterEvents, &mask, NULL) != NULL) {
		LOG((CLOG_WARN "cannot get IC filter events"));
		XDestroyIC(ic);
		XCloseIM(im);
		return;
	}

	// we have IM
	m_im          = im;
	m_ic          = ic;
	m_lastKeycode = 0;

	// select events on our window that IM requires
	XWindowAttributes attr;
	XGetWindowAttributes(m_display, m_window, &attr);
	XSelectInput(m_display, m_window, attr.your_event_mask | mask);
}

void
CXWindowsScreen::addTimer(IJob* job, double timeout)
{
	CLock lock(&m_timersMutex);
	removeTimerNoLock(job);
	m_timers.push(CTimer(job, m_time.getTime(), timeout));
}

void
CXWindowsScreen::removeTimer(IJob* job)
{
	CLock lock(&m_timersMutex);
	removeTimerNoLock(job);
}

void
CXWindowsScreen::removeTimerNoLock(IJob* job)
{
	// do it the hard way.  first collect all jobs that are not
	// the removed job.
	CTimerPriorityQueue::container_type tmp;
	for (CTimerPriorityQueue::iterator index = m_timers.begin();
								index != m_timers.end(); ++index) {
		if (index->getJob() != job) {
			tmp.push_back(*index);
		}
	}

	// now swap in the new list
	m_timers.swap(tmp);
}

void
CXWindowsScreen::onEvent(XEvent* xevent)
{
	assert(xevent != NULL);

	// let input methods try to handle event first
	if (m_ic != NULL) {
		// XFilterEvent() may eat the event and generate a new KeyPress
		// event with a keycode of 0 because there isn't an actual key
		// associated with the keysym.  but the KeyRelease may pass
		// through XFilterEvent() and keep its keycode.  this means
		// there's a mismatch between KeyPress and KeyRelease keycodes.
		// since we use the keycode on the client to detect when a key
		// is released this won't do.  so we remember the keycode on
		// the most recent KeyPress (and clear it on a matching
		// KeyRelease) so we have a keycode for a synthesized KeyPress.
		if (xevent->type == KeyPress && xevent->xkey.keycode != 0) {
			m_lastKeycode = xevent->xkey.keycode;
		}
		else if (xevent->type == KeyRelease &&
			xevent->xkey.keycode == m_lastKeycode) {
			m_lastKeycode = 0;
		}

		// now filter the event
		if (XFilterEvent(xevent, None)) {
			return;
		}
	}

	switch (xevent->type) {
	case CreateNotify:
		if (m_isPrimary) {
			// select events on new window
			selectEvents(xevent->xcreatewindow.window);
		}
		return;

	case MappingNotify:
		if (XPending(m_display) > 0) {
			XEvent tmpEvent;
			XPeekEvent(m_display, &tmpEvent);
			if (tmpEvent.type == MappingNotify) {
				// discard this MappingNotify since another follows.
				// we tend to get a bunch of these in a row.
				return;
			}
		}

		// keyboard mapping changed
		XRefreshKeyboardMapping(&xevent->xmapping);
		m_keyState->updateKeys();
		break;

	case LeaveNotify:
		if (!m_isPrimary) {
			// mouse moved out of hider window somehow.  hide the window.
			XUnmapWindow(m_display, m_window);
		}
		break;

	case SelectionClear:
		{
			// we just lost the selection.  that means someone else
			// grabbed the selection so this screen is now the
			// selection owner.  report that to the receiver.
			ClipboardID id = getClipboardID(xevent->xselectionclear.selection);
			if (id != kClipboardEnd) {
				LOG((CLOG_DEBUG "lost clipboard %d ownership at time %d", id, xevent->xselectionclear.time));
				m_clipboard[id]->lost(xevent->xselectionclear.time);
				m_receiver->onGrabClipboard(id);
				return;
			}
		}
		break;

	case SelectionNotify:
		// notification of selection transferred.  we shouldn't
		// get this here because we handle them in the selection
		// retrieval methods.  we'll just delete the property
		// with the data (satisfying the usual ICCCM protocol).
		if (xevent->xselection.property != None) {
			XDeleteProperty(m_display,
								xevent->xselection.requestor,
								xevent->xselection.property);
		}
		return;

	case SelectionRequest:
		{
			// somebody is asking for clipboard data
			ClipboardID id = getClipboardID(
								xevent->xselectionrequest.selection);
			if (id != kClipboardEnd) {
				m_clipboard[id]->addRequest(
								xevent->xselectionrequest.owner,
								xevent->xselectionrequest.requestor,
								xevent->xselectionrequest.target,
								xevent->xselectionrequest.time,
								xevent->xselectionrequest.property);
				return;
			}
		}
		break;

	case PropertyNotify:
		// property delete may be part of a selection conversion
		if (xevent->xproperty.state == PropertyDelete) {
			processClipboardRequest(xevent->xproperty.window,
								xevent->xproperty.time,
								xevent->xproperty.atom);
			return;
		}
		break;

	case ClientMessage:
		if (m_isPrimary &&
			xevent->xclient.message_type == m_atomScreensaver &&
			xevent->xclient.format       == 32) {
			// screen saver activation/deactivation event
			m_primaryReceiver->onScreensaver(xevent->xclient.data.l[0] != 0);
			return;
		}
		break;

	case DestroyNotify:
		// looks like one of the windows that requested a clipboard
		// transfer has gone bye-bye.
		destroyClipboardRequest(xevent->xdestroywindow.window);
		break;

	case KeyPress:
		if (m_isPrimary) {
			onKeyPress(xevent->xkey);
		}
		return;

	case KeyRelease:
		if (m_isPrimary) {
			onKeyRelease(xevent->xkey);
		}
		return;

	case ButtonPress:
		if (m_isPrimary) {
			onMousePress(xevent->xbutton);
		}
		return;

	case ButtonRelease:
		if (m_isPrimary) {
			onMouseRelease(xevent->xbutton);
		}
		return;

	case MotionNotify:
		if (m_isPrimary) {
			onMouseMove(xevent->xmotion);
		}
		return;

	default:
		break;
	}

	// let screen saver have a go
	m_screensaver->onPreDispatch(xevent);
}

void
CXWindowsScreen::onKeyPress(XKeyEvent& xkey)
{
	LOG((CLOG_DEBUG1 "event: KeyPress code=%d, state=0x%04x", xkey.keycode, xkey.state));
	const KeyModifierMask mask = m_keyMapper.mapModifier(xkey.state);
	KeyID key                  = mapKeyFromX(&xkey);
	if (key != kKeyNone) {
		// check for ctrl+alt+del emulation
		if ((key == kKeyPause || key == kKeyBreak) &&
			(mask & (KeyModifierControl | KeyModifierAlt)) ==
					(KeyModifierControl | KeyModifierAlt)) {
			// pretend it's ctrl+alt+del
			LOG((CLOG_DEBUG "emulate ctrl+alt+del"));
			key = kKeyDelete;
		}

		// get which button.  see call to XFilterEvent() in onEvent()
		// for more info.
		KeyButton keycode = static_cast<KeyButton>(xkey.keycode);
		if (keycode == 0) {
			keycode = static_cast<KeyButton>(m_lastKeycode);
		}

		// handle key
		m_primaryReceiver->onKeyDown(key, mask, keycode);
		KeyModifierMask keyMask = m_keyState->getMaskForKey(keycode);
		if (m_keyState->isHalfDuplex(keyMask)) {
			m_primaryReceiver->onKeyUp(key, mask | keyMask, keycode);
		}
	}
}

Bool
CXWindowsScreen::findKeyEvent(Display*, XEvent* xevent, XPointer arg)
{
	CKeyEventInfo* filter = reinterpret_cast<CKeyEventInfo*>(arg);
	return (xevent->type         == filter->m_event &&
			xevent->xkey.window  == filter->m_window &&
			xevent->xkey.time    == filter->m_time &&
			xevent->xkey.keycode == filter->m_keycode) ? True : False;
}

void
CXWindowsScreen::onKeyRelease(XKeyEvent& xkey)
{
	const KeyModifierMask mask = m_keyMapper.mapModifier(xkey.state);
	KeyID key                  = mapKeyFromX(&xkey);
	if (key != kKeyNone) {
		// check if this is a key repeat by getting the next
		// KeyPress event that has the same key and time as
		// this release event, if any.  first prepare the
		// filter info.
		CKeyEventInfo filter;
		filter.m_event   = KeyPress;
		filter.m_window  = xkey.window;
		filter.m_time    = xkey.time;
		filter.m_keycode = xkey.keycode;

		// now check for event
		bool hasPress;
		{
			XEvent xevent2;
			hasPress = (XCheckIfEvent(m_display, &xevent2,
							&CXWindowsScreen::findKeyEvent,
							(XPointer)&filter) == True);
		}

		// check for ctrl+alt+del emulation
		if ((key == kKeyPause || key == kKeyBreak) &&
			(mask & (KeyModifierControl | KeyModifierAlt)) ==
					(KeyModifierControl | KeyModifierAlt)) {
			// pretend it's ctrl+alt+del and ignore autorepeat
			LOG((CLOG_DEBUG "emulate ctrl+alt+del"));
			key      = kKeyDelete;
			hasPress = false;
		}

		KeyButton keycode = static_cast<KeyButton>(xkey.keycode);
		if (!hasPress) {
			// no press event follows so it's a plain release
			LOG((CLOG_DEBUG1 "event: KeyRelease code=%d, state=0x%04x", keycode, xkey.state));
			KeyModifierMask keyMask = m_keyState->getMaskForKey(keycode);
			if (m_keyState->isHalfDuplex(keyMask)) {
				m_primaryReceiver->onKeyDown(key, mask, keycode);
			}
			m_primaryReceiver->onKeyUp(key, mask, keycode);
		}
		else {
			// found a press event following so it's a repeat.
			// we could attempt to count the already queued
			// repeats but we'll just send a repeat of 1.
			// note that we discard the press event.
			LOG((CLOG_DEBUG1 "event: repeat code=%d, state=0x%04x", keycode, xkey.state));
			m_primaryReceiver->onKeyRepeat(key, mask, 1, keycode);
		}
	}
}

void
CXWindowsScreen::onMousePress(const XButtonEvent& xbutton)
{
	LOG((CLOG_DEBUG1 "event: ButtonPress button=%d", xbutton.button));
	const ButtonID button = mapButtonFromX(&xbutton);
	if (button != kButtonNone) {
		m_primaryReceiver->onMouseDown(button);
	}
}

void
CXWindowsScreen::onMouseRelease(const XButtonEvent& xbutton)
{
	LOG((CLOG_DEBUG1 "event: ButtonRelease button=%d", xbutton.button));
	const ButtonID button = mapButtonFromX(&xbutton);
	if (button != kButtonNone) {
		m_primaryReceiver->onMouseUp(button);
	}
	else if (xbutton.button == 4) {
		// wheel forward (away from user)
		m_primaryReceiver->onMouseWheel(120);
	}
	else if (xbutton.button == 5) {
		// wheel backward (toward user)
		m_primaryReceiver->onMouseWheel(-120);
	}
}

void
CXWindowsScreen::onMouseMove(const XMotionEvent& xmotion)
{
	LOG((CLOG_DEBUG2 "event: MotionNotify %d,%d", xmotion.x_root, xmotion.y_root));

	// compute motion delta (relative to the last known
	// mouse position)
	SInt32 x = xmotion.x_root - m_xCursor;
	SInt32 y = xmotion.y_root - m_yCursor;

	// save position to compute delta of next motion
	m_xCursor = xmotion.x_root;
	m_yCursor = xmotion.y_root;

	if (xmotion.send_event) {
		// we warped the mouse.  discard events until we
		// find the matching sent event.  see
		// warpCursorNoFlush() for where the events are
		// sent.  we discard the matching sent event and
		// can be sure we've skipped the warp event.
		XEvent xevent;
		do {
			XMaskEvent(m_display, PointerMotionMask, &xevent);
		} while (!xevent.xany.send_event);
	}
	else if (m_isOnScreen) {
		// motion on primary screen
		m_primaryReceiver->onMouseMovePrimary(m_xCursor, m_yCursor);
	}
	else {
		// motion on secondary screen.  warp mouse back to
		// center.
		//
		// my lombard (powerbook g3) running linux and
		// using the adbmouse driver has two problems:
		// first, the driver only sends motions of +/-2
		// pixels and, second, it seems to discard some
		// physical input after a warp.  the former isn't a
		// big deal (we're just limited to every other
		// pixel) but the latter is a PITA.  to work around
		// it we only warp when the mouse has moved more
		// than s_size pixels from the center.
		static const SInt32 s_size = 32;
		if (xmotion.x_root - m_xCenter < -s_size ||
			xmotion.x_root - m_xCenter >  s_size ||
			xmotion.y_root - m_yCenter < -s_size ||
			xmotion.y_root - m_yCenter >  s_size) {
			warpCursorNoFlush(m_xCenter, m_yCenter);
		}

		// send event if mouse moved.  do this after warping
		// back to center in case the motion takes us onto
		// the primary screen.  if we sent the event first
		// in that case then the warp would happen after
		// warping to the primary screen's enter position,
		// effectively overriding it.
		if (x != 0 || y != 0) {
			m_primaryReceiver->onMouseMoveSecondary(x, y);
		}
	}
}

Cursor
CXWindowsScreen::createBlankCursor() const
{
	// this seems just a bit more complicated than really necessary

	// get the closet cursor size to 1x1
	unsigned int w, h;
	XQueryBestCursor(m_display, m_root, 1, 1, &w, &h);

	// make bitmap data for cursor of closet size.  since the cursor
	// is blank we can use the same bitmap for shape and mask:  all
	// zeros.
	const int size = ((w + 7) >> 3) * h;
	char* data = new char[size];
	memset(data, 0, size);

	// make bitmap
	Pixmap bitmap = XCreateBitmapFromData(m_display, m_root, data, w, h);

	// need an arbitrary color for the cursor
	XColor color;
	color.pixel = 0;
	color.red   = color.green = color.blue = 0;
	color.flags = DoRed | DoGreen | DoBlue;

	// make cursor from bitmap
	Cursor cursor = XCreatePixmapCursor(m_display, bitmap, bitmap,
								&color, &color, 0, 0);

	// don't need bitmap or the data anymore
	delete[] data;
	XFreePixmap(m_display, bitmap);

	return cursor;
}

bool
CXWindowsScreen::processTimers()
{
	bool oneShot = false;
	std::vector<IJob*> jobs;
	{
		CLock lock(&m_timersMutex);

		// get current time
		const double time = m_time.getTime();

		// done if no timers have expired
		if ((m_oneShotTimer == NULL || *m_oneShotTimer > time) &&
			(m_timers.empty() || m_timers.top() > time)) {
			return false;
		}

		// handle one shot timers
		if (m_oneShotTimer != NULL) {
			*m_oneShotTimer -= time;
			if (*m_oneShotTimer <= 0.0) {
				delete m_oneShotTimer;
				m_oneShotTimer = NULL;
				oneShot = true;
			}
		}

		// subtract current time from all timers.  note that this won't
		// change the order of elements in the priority queue (except
		// for floating point round off which we'll ignore).
		for (CTimerPriorityQueue::iterator index = m_timers.begin();
								index != m_timers.end(); ++index) {
			(*index) -= time;
		}

		// process all timers at or below zero, saving the jobs
		if (!m_timers.empty()) {
			while (m_timers.top() <= 0.0) {
				CTimer timer = m_timers.top();
				jobs.push_back(timer.getJob());
				timer.reset();
				m_timers.pop();
				m_timers.push(timer);
			}
		}

		// reset the clock
		m_time.reset();
	}

	// now notify of the one shot timers
	if (oneShot) {
		m_mutex.unlock();
		m_primaryReceiver->onOneShotTimerExpired(0);
		m_mutex.lock();
	}

	// now run the jobs.  note that if one of these jobs removes
	// a timer later in the jobs list and deletes that job pointer
	// then this will crash when it tries to run that job.
	for (std::vector<IJob*>::iterator index = jobs.begin();
								index != jobs.end(); ++index) {
		(*index)->run();
	}
}

ClipboardID
CXWindowsScreen::getClipboardID(Atom selection) const
{
	for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
		if (m_clipboard[id] != NULL &&
			m_clipboard[id]->getSelection() == selection) {
			return id;
		}
	}
	return kClipboardEnd;
}

void
CXWindowsScreen::processClipboardRequest(Window requestor,
				Time time, Atom property)
{
	// check every clipboard until one returns success
	for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
		if (m_clipboard[id] != NULL &&
			m_clipboard[id]->processRequest(requestor, time, property)) {
			break;
		}
	}
}

void
CXWindowsScreen::destroyClipboardRequest(Window requestor)
{
	// check every clipboard until one returns success
	for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
		if (m_clipboard[id] != NULL &&
			m_clipboard[id]->destroyRequest(requestor)) {
			break;
		}
	}
}

int
CXWindowsScreen::ioErrorHandler(Display*)
{
	// the display has disconnected, probably because X is shutting
	// down.  X forces us to exit at this point.  that's arguably
	// a flaw in X but, realistically, it's difficult to gracefully
	// handle not having a Display* anymore.  we'll simply log the
	// error, notify the subclass (which must not use the display
	// so we set it to NULL), and exit.
	LOG((CLOG_WARN "X display has unexpectedly disconnected"));
	s_screen->m_display = NULL;
	s_screen->m_receiver->onError();
	LOG((CLOG_CRIT "quiting due to X display disconnection"));
	exit(17);
}

void
CXWindowsScreen::selectEvents(Window w) const
{
	// ignore errors while we adjust event masks.  windows could be
	// destroyed at any time after the XQueryTree() in doSelectEvents()
	// so we must ignore BadWindow errors.
	CXWindowsUtil::CErrorLock lock(m_display);

	// adjust event masks
	doSelectEvents(w);
}

void
CXWindowsScreen::doSelectEvents(Window w) const
{
	// we want to track the mouse everywhere on the display.  to achieve
	// that we select PointerMotionMask on every window.  we also select
	// SubstructureNotifyMask in order to get CreateNotify events so we
	// select events on new windows too.
	//
	// note that this can break certain clients due a design flaw of X.
	// X will deliver a PointerMotion event to the deepest window in the
	// hierarchy that contains the pointer and has PointerMotionMask
	// selected by *any* client.  if another client doesn't select
	// motion events in a subwindow so the parent window will get them
	// then by selecting for motion events on the subwindow we break
	// that client because the parent will no longer get the events.

	// FIXME -- should provide some workaround for event selection
	// design flaw.  perhaps only select for motion events on windows
	// that already do or are top-level windows or don't propagate
	// pointer events.  or maybe an option to simply poll the mouse.

	// we don't want to adjust our grab window
	if (w == m_window) {
		return;
	}

	// select events of interest.  do this before querying the tree so
	// we'll get notifications of children created after the XQueryTree()
	// so we won't miss them.
	XSelectInput(m_display, w, PointerMotionMask | SubstructureNotifyMask);

	// recurse on child windows
	Window rw, pw, *cw;
	unsigned int nc;
	if (XQueryTree(m_display, w, &rw, &pw, &cw, &nc)) {
		for (unsigned int i = 0; i < nc; ++i) {
			doSelectEvents(cw[i]);
		}
		XFree(cw);
	}
}

KeyID
CXWindowsScreen::mapKeyFromX(XKeyEvent* event) const
{
	// convert to a keysym
	KeySym keysym;
	if (event->type == KeyPress && m_ic != NULL) {
		// do multibyte lookup.  can only call XmbLookupString with a
		// key press event and a valid XIC so we checked those above.
		char scratch[32];
		int n        = sizeof(scratch) / sizeof(scratch[0]);
		char* buffer = scratch;
		int status;
		n = XmbLookupString(m_ic, event, buffer, n, &keysym, &status);
		if (status == XBufferOverflow) {
			// not enough space.  grow buffer and try again.
			buffer = new char[n];
			n = XmbLookupString(m_ic, event, buffer, n, &keysym, &status);
			delete[] buffer;
		}

		// see what we got.  since we don't care about the string
		// we'll just look for a keysym.
		switch (status) {
		default:
		case XLookupNone:
		case XLookupChars:
			keysym = 0;
			break;

		case XLookupKeySym:
		case XLookupBoth:
			break;
		}
	}
	else {
		// plain old lookup
		char dummy[1];
		XLookupString(event, dummy, 0, &keysym, NULL);
	}

	// convert key
	switch (keysym & 0xffffff00) {
	case 0x0000:
		// Latin-1
		return static_cast<KeyID>(keysym);

	case 0xfe00:
		// ISO 9995 Function and Modifier Keys
		if (keysym == XK_ISO_Left_Tab) {
			return kKeyLeftTab;
		}
		return kKeyNone;

	case 0xff00:
		// MISCELLANY
		return static_cast<KeyID>(keysym - 0xff00 + 0xef00);

	case 0x1008ff00:
		// "Internet" keys
		return g_map1008FF[keysym & 0xff];

	default: {
		// lookup character in table
		UInt32 key = CXWindowsUtil::mapKeySymToUCS4(keysym);
		if (key != 0x0000ffff) {
			return static_cast<KeyID>(key);
		}

		// unknown character
		return kKeyNone;
	}
	}
}

ButtonID
CXWindowsScreen::mapButtonFromX(const XButtonEvent* event) const
{
	unsigned int button = event->button;

	// first three buttons map to 1, 2, 3 (kButtonLeft, Middle, Right)
	if (button >= 1 && button <= 3) {
		return static_cast<ButtonID>(button);
	}

	// buttons 4 and 5 are ignored here.  they're used for the wheel.
	// buttons 6, 7, etc and up map to 4, 5, etc.
	else if (button >= 6) {
		return static_cast<ButtonID>(button - 2);
	}

	// unknown button
	else {
		return kButtonNone;
	}
}

unsigned int
CXWindowsScreen::mapButtonToX(ButtonID id) const
{
	// map button -1 to button 4 (+wheel)
	if (id == static_cast<ButtonID>(-1)) {
		id = 4;
	}

	// map button -2 to button 5 (-wheel)
	else if (id == static_cast<ButtonID>(-2)) {
		id = 5;
	}

	// map buttons 4, 5, etc. to 6, 7, etc. to make room for buttons
	// 4 and 5 used to simulate the mouse wheel.
	else if (id >= 4) {
		id += 2;
	}

	// check button is in legal range
	if (id < 1 || id > m_buttons.size()) {
		// out of range
		return 0;
	}

	// map button
	return static_cast<unsigned int>(m_buttons[id - 1]);
}

void
CXWindowsScreen::warpCursorNoFlush(SInt32 x, SInt32 y)
{
	assert(m_window != None);

	// send an event that we can recognize before the mouse warp
	XEvent eventBefore;
	eventBefore.type                = MotionNotify;
	eventBefore.xmotion.display     = m_display;
	eventBefore.xmotion.window      = m_window;
	eventBefore.xmotion.root        = m_root;
	eventBefore.xmotion.subwindow   = m_window;
	eventBefore.xmotion.time        = CurrentTime;
	eventBefore.xmotion.x           = x;
	eventBefore.xmotion.y           = y;
	eventBefore.xmotion.x_root      = x;
	eventBefore.xmotion.y_root      = y;
	eventBefore.xmotion.state       = 0;
	eventBefore.xmotion.is_hint     = NotifyNormal;
	eventBefore.xmotion.same_screen = True;
	XEvent eventAfter               = eventBefore;
	XSendEvent(m_display, m_window, False, 0, &eventBefore);

	// warp mouse
	XWarpPointer(m_display, None, m_root, 0, 0, 0, 0, x, y);

	// send an event that we can recognize after the mouse warp
	XSendEvent(m_display, m_window, False, 0, &eventAfter);
	XSync(m_display, False);

	LOG((CLOG_DEBUG2 "warped to %d,%d", x, y));
}

void
CXWindowsScreen::updateButtons()
{
	// query the button mapping
	UInt32 numButtons = XGetPointerMapping(m_display, NULL, 0);
	unsigned char* tmpButtons = new unsigned char[numButtons];
	XGetPointerMapping(m_display, tmpButtons, numButtons);

	// find the largest logical button id
	unsigned char maxButton = 0;
	for (UInt32 i = 0; i < numButtons; ++i) {
		if (tmpButtons[i] > maxButton) {
			maxButton = tmpButtons[i];
		}
	}

	// allocate button array
	m_buttons.resize(maxButton);

	// fill in button array values.  m_buttons[i] is the physical
	// button number for logical button i+1.
	for (UInt32 i = 0; i < numButtons; ++i) {
		m_buttons[i] = 0;
	}
	for (UInt32 i = 0; i < numButtons; ++i) {
		m_buttons[tmpButtons[i] - 1] = i + 1;
	}

	// clean up
	delete[] tmpButtons;
}

bool
CXWindowsScreen::grabMouseAndKeyboard()
{
	// grab the mouse and keyboard.  keep trying until we get them.
	// if we can't grab one after grabbing the other then ungrab
	// and wait before retrying.  give up after s_timeout seconds.
	static const double s_timeout = 1.0;
	int result;
	CStopwatch timer;
	do {
		// keyboard first
		do {
			result = XGrabKeyboard(m_display, m_window, True,
								GrabModeAsync, GrabModeAsync, CurrentTime);
			assert(result != GrabNotViewable);
			if (result != GrabSuccess) {
				LOG((CLOG_DEBUG2 "waiting to grab keyboard"));
				ARCH->sleep(0.05);
				if (timer.getTime() >= s_timeout) {
					LOG((CLOG_DEBUG2 "grab keyboard timed out"));
					return false;
				}
			}
		} while (result != GrabSuccess);
		LOG((CLOG_DEBUG2 "grabbed keyboard"));

		// now the mouse
		result = XGrabPointer(m_display, m_window, True, 0,
								GrabModeAsync, GrabModeAsync,
								m_window, None, CurrentTime);
		assert(result != GrabNotViewable);
		if (result != GrabSuccess) {
			// back off to avoid grab deadlock
			XUngrabKeyboard(m_display, CurrentTime);
			LOG((CLOG_DEBUG2 "ungrabbed keyboard, waiting to grab pointer"));
			ARCH->sleep(0.05);
			if (timer.getTime() >= s_timeout) {
				LOG((CLOG_DEBUG2 "grab pointer timed out"));
				return false;
			}
		}
	} while (result != GrabSuccess);

	LOG((CLOG_DEBUG1 "grabbed pointer and keyboard"));
	return true;
}


//
// CXWindowsScreen::CTimer
//

CXWindowsScreen::CTimer::CTimer(IJob* job, double startTime, double resetTime) :
	m_job(job),
	m_timeout(resetTime),
	m_time(resetTime),
	m_startTime(startTime)
{
	assert(m_timeout > 0.0);
}

CXWindowsScreen::CTimer::~CTimer()
{
	// do nothing
}

void
CXWindowsScreen::CTimer::run()
{
	if (m_job != NULL) {
		m_job->run();
	}
}

void
CXWindowsScreen::CTimer::reset()
{
	m_time      = m_timeout;
	m_startTime = 0.0;
}

CXWindowsScreen::CTimer::CTimer&
CXWindowsScreen::CTimer::operator-=(double dt)
{
	m_time     -= dt - m_startTime;
	m_startTime = 0.0;
	return *this;
}

CXWindowsScreen::CTimer::operator double() const
{
	return m_time;
}

bool
CXWindowsScreen::CTimer::operator<(const CTimer& t) const
{
	return m_time < t.m_time;
}
