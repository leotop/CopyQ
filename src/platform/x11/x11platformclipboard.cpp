/*
    Copyright (c) 2018, Lukas Holecek <hluk@email.cz>

    This file is part of CopyQ.

    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <QApplication>

#include "x11platformclipboard.h"

#include "common/mimetypes.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <QClipboard>
#include <QX11Info>

namespace {

constexpr auto minCheckAgainIntervalMs = 50;
constexpr auto maxCheckAgainIntervalMs = 500;

/// Return true only if selection is incomplete, i.e. mouse button or shift key is pressed.
bool isSelectionIncomplete()
{
    if (!QX11Info::isPlatformX11())
        return false;

    auto display = QX11Info::display();

    // If mouse button or shift is pressed then assume that user is selecting text.
    XEvent event{};
    XQueryPointer(display, DefaultRootWindow(display),
                  &event.xbutton.root, &event.xbutton.window,
                  &event.xbutton.x_root, &event.xbutton.y_root,
                  &event.xbutton.x, &event.xbutton.y,
                  &event.xbutton.state);

    return event.xbutton.state & (Button1Mask | ShiftMask);
}

} // namespace

X11PlatformClipboard::X11PlatformClipboard()
{
    m_timerCheckAgain.setSingleShot(true);
    connect( &m_timerCheckAgain, &QTimer::timeout, this, &X11PlatformClipboard::check );
}

void X11PlatformClipboard::setFormats(const QStringList &formats)
{
    m_formats = formats;
}

QVariantMap X11PlatformClipboard::data(ClipboardMode mode, const QStringList &) const
{
    return mode == ClipboardMode::Clipboard ? m_clipboardData : m_selectionData;
}

void X11PlatformClipboard::setData(ClipboardMode mode, const QVariantMap &dataMap)
{
    // WORKAROUND: Avoid getting X11 warning "QXcbClipboard: SelectionRequest too old".
    QCoreApplication::processEvents();
    DummyClipboard::setData(mode, dataMap);
}

void X11PlatformClipboard::onChanged(int)
{
    check();
}

void X11PlatformClipboard::check()
{
    // Omit checking clipboard and selection too fast.
    if ( m_timerCheckAgain.isActive() )
        return;

    // Fetch clipboard and selection data first to avoid it being invalidated.
    const QVariantMap clipboardData = DummyClipboard::data(ClipboardMode::Clipboard, m_formats);

    // Always assume that only plain text can be in primary selection buffer.
    // Asking a app for bigger data when mouse selection changes can make the app hang for a moment.
    const QVariantMap selectionData = DummyClipboard::data( ClipboardMode::Selection, QStringList(mimeText) );

    if (m_clipboardData != clipboardData) {
        m_clipboardData = clipboardData;
        emit changed(ClipboardMode::Clipboard);
    }

    if ( m_selectionData != selectionData && !isSelectionIncomplete() ) {
        m_selectionData = selectionData;
        emit changed(ClipboardMode::Selection);
    }

    // Check clipboard and selection again if some signals where
    // not delivered or older data was received after new one.
    m_checkAgainIntervalMs = m_checkAgainIntervalMs * 2 + minCheckAgainIntervalMs;
    if (m_checkAgainIntervalMs < maxCheckAgainIntervalMs)
        m_timerCheckAgain.start(m_checkAgainIntervalMs);
    else
        m_checkAgainIntervalMs = 0;
}
