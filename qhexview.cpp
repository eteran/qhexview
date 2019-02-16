/*
Copyright (C) 2006 - 2013 Evan Teran
                          eteran@alum.rit.edu

Copyright (C) 2010        Hugues Bruant
                          hugues.bruant@gmail.com

This file can be used under one of two licenses.

1. The GNU Public License, version 2.0, in COPYING-gpl2
2. A BSD-Style License, in COPYING-bsd2.

The license chosen is at the discretion of the user of this software.
*/

#include "qhexview.h"

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QFontDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QScrollBar>
#include <QSignalMapper>
#include <QTextStream>
#include <QtGlobal>
#include <QtEndian>
#include <QStringBuilder>

#include <cctype>
#include <climits>
#include <cmath>
#include <memory>

namespace {

//------------------------------------------------------------------------------
// Name: is_printable
// Desc: determines if a character has a printable ascii symbol
//------------------------------------------------------------------------------
constexpr bool is_printable(unsigned char ch) {

	// if it's standard ascii use isprint/isspace, otherwise go with our observations
	if(ch < 0x80) {
		return std::isprint(ch) || std::isspace(ch);
	} else {
		return (ch & 0xff) >= 0xa0;
	}
}

//------------------------------------------------------------------------------
// Name: add_toggle_action_to_menu
// Desc: convenience function used to add a checkable menu item to the context menu
//------------------------------------------------------------------------------
QAction *add_toggle_action_to_menu(QMenu *menu, const QString &caption, bool checked, QObject *receiver, const char *slot) {
	auto action = new QAction(caption, menu);
	action->setCheckable(true);
	action->setChecked(checked);
	menu->addAction(action);
	QObject::connect(action, SIGNAL(toggled(bool)), receiver, slot);
	return action;
}

}

//------------------------------------------------------------------------------
// Name: QHexView
// Desc: constructor
//------------------------------------------------------------------------------
QHexView::QHexView(QWidget *parent) : QAbstractScrollArea(parent) {

#if QT_POINTER_SIZE == 4
	address_size_ = Address32;
#else
	address_size_ = Address64;
#endif

	// default to a simple monospace font
	setFont(QFont("Monospace", 8));
	setShowAddressSeparator(true);
}

//------------------------------------------------------------------------------
// Name: setShowAddressSeparator
// Desc:
//------------------------------------------------------------------------------
void QHexView::setShowAddressSeparator(bool value) {
	show_address_separator_ = value;
	updateScrollbars();
}

//------------------------------------------------------------------------------
// Name: formatAddress
// Desc:
//------------------------------------------------------------------------------
QString QHexView::formatAddress(address_t address) {

	static char buffer[32];

	switch(address_size_) {
	case Address32:
		{
		    const uint16_t hi = (address >> 16) & 0xffff;
			const uint16_t lo = (address & 0xffff);

			if(show_address_separator_) {
				qsnprintf(buffer, sizeof(buffer), "%04x:%04x", hi, lo);
			} else {
				qsnprintf(buffer, sizeof(buffer), "%04x%04x", hi, lo);
			}
		}
		return QString::fromLocal8Bit(buffer);
	case Address64:
		{
		    const uint32_t hi = (address >> 32) & 0xffffffff;
			const uint32_t lo = (address & 0xffffffff);

			if(show_address_separator_) {
				qsnprintf(buffer, sizeof(buffer), "%08x:%08x", hi, lo);
			} else {
				qsnprintf(buffer, sizeof(buffer), "%08x%08x", hi, lo);

			}
		}
		return QString::fromLocal8Bit(buffer);
	}

	return QString();
}

//------------------------------------------------------------------------------
// Name: repaint
// Desc:
//------------------------------------------------------------------------------
void QHexView::repaint() {
	viewport()->repaint();
}

//------------------------------------------------------------------------------
// Name: dataSize
// Desc: returns how much data we are viewing
//------------------------------------------------------------------------------
int64_t QHexView::dataSize() const {
	return data_ ? data_->size() : 0;
}

//------------------------------------------------------------------------------
// Name: setFont
// Desc: overloaded version of setFont, calculates font metrics for later
//------------------------------------------------------------------------------
void QHexView::setFont(const QFont &f) {

	QFont font(f);
	font.setStyleStrategy(QFont::ForceIntegerMetrics);

	// recalculate all of our metrics/offsets
	const QFontMetrics fm(font);
	font_width_  = fm.width('X');
	font_height_ = fm.height();

	updateScrollbars();

	// TODO(eteran): assert that we are using a fixed font & find out if we care?
	QAbstractScrollArea::setFont(font);
}

//------------------------------------------------------------------------------
// Name: createStandardContextMenu
// Desc: creates the 'standard' context menu for the widget
//------------------------------------------------------------------------------
QMenu *QHexView::createStandardContextMenu() {

	auto menu = new QMenu(this);

	menu->addAction(tr("Set &Font"), this, SLOT(mnuSetFont()));
	menu->addSeparator();
	add_toggle_action_to_menu(menu, tr("Show A&ddress"),  show_address_,  this, SLOT(setShowAddress(bool)));
	add_toggle_action_to_menu(menu, tr("Show &Hex"),      show_hex_,      this, SLOT(setShowHexDump(bool)));
	add_toggle_action_to_menu(menu, tr("Show &Ascii"),    show_ascii_,    this, SLOT(setShowAsciiDump(bool)));
	add_toggle_action_to_menu(menu, tr("Show &Comments"), show_comments_, this, SLOT(setShowComments(bool)));

	if(user_can_set_word_width_ || user_can_set_row_width_) {
		menu->addSeparator();
	}

	if(user_can_set_word_width_) {
		auto wordWidthMapper = new QSignalMapper(menu);

		auto wordMenu = new QMenu(tr("Set Word Width"), menu);
		QAction *const a1 = add_toggle_action_to_menu(wordMenu, tr("1 Byte"),  word_width_ == 1, wordWidthMapper, SLOT(map()));
		QAction *const a2 = add_toggle_action_to_menu(wordMenu, tr("2 Bytes"), word_width_ == 2, wordWidthMapper, SLOT(map()));
		QAction *const a3 = add_toggle_action_to_menu(wordMenu, tr("4 Bytes"), word_width_ == 4, wordWidthMapper, SLOT(map()));
		QAction *const a4 = add_toggle_action_to_menu(wordMenu, tr("8 Bytes"), word_width_ == 8, wordWidthMapper, SLOT(map()));

		wordWidthMapper->setMapping(a1, 1);
		wordWidthMapper->setMapping(a2, 2);
		wordWidthMapper->setMapping(a3, 4);
		wordWidthMapper->setMapping(a4, 8);

		connect(wordWidthMapper, SIGNAL(mapped(int)), this, SLOT(setWordWidth(int)));
		menu->addMenu(wordMenu);
	}

	if(user_can_set_row_width_) {
		auto rowWidthMapper = new QSignalMapper(menu);

		auto rowMenu = new QMenu(tr("Set Row Width"), menu);
		QAction *const a5 = add_toggle_action_to_menu(rowMenu, tr("1 Word"),   row_width_ == 1, rowWidthMapper, SLOT(map()));
		QAction *const a6 = add_toggle_action_to_menu(rowMenu, tr("2 Words"),  row_width_ == 2, rowWidthMapper, SLOT(map()));
		QAction *const a7 = add_toggle_action_to_menu(rowMenu, tr("4 Words"),  row_width_ == 4, rowWidthMapper, SLOT(map()));
		QAction *const a8 = add_toggle_action_to_menu(rowMenu, tr("8 Words"),  row_width_ == 8, rowWidthMapper, SLOT(map()));
		QAction *const a9 = add_toggle_action_to_menu(rowMenu, tr("16 Words"), row_width_ == 16, rowWidthMapper, SLOT(map()));

		rowWidthMapper->setMapping(a5, 1);
		rowWidthMapper->setMapping(a6, 2);
		rowWidthMapper->setMapping(a7, 4);
		rowWidthMapper->setMapping(a8, 8);
		rowWidthMapper->setMapping(a9, 16);

		connect(rowWidthMapper, SIGNAL(mapped(int)), this, SLOT(setRowWidth(int)));
		menu->addMenu(rowMenu);
	}

	menu->addSeparator();
	menu->addAction(tr("&Copy Selection To Clipboard"), this, SLOT(mnuCopy()));
	menu->addAction(tr("&Copy Address To Clipboard"), this, SLOT(mnuAddrCopy()));
	return menu;
}

//------------------------------------------------------------------------------
// Name: contextMenuEvent
// Desc: default context menu event, simply shows standard menu
//------------------------------------------------------------------------------
void QHexView::contextMenuEvent(QContextMenuEvent *event) {
	QMenu *const menu = createStandardContextMenu();
	menu->exec(event->globalPos());
	delete menu;
}

//------------------------------------------------------------------------------
// Name: normalizedOffset
// Desc:
//------------------------------------------------------------------------------
int64_t QHexView::normalizedOffset() const {

	int64_t offset = static_cast<int64_t>(verticalScrollBar()->value()) * bytesPerRow();

	if(origin_ != 0) {
		if(offset > 0) {
			offset += origin_;
			offset -= bytesPerRow();
		}
	}

	return offset;
}

//------------------------------------------------------------------------------
// Name: mnuCopy
// Desc:
//------------------------------------------------------------------------------
void QHexView::mnuCopy() {
	if(hasSelectedText()) {

		QString s;
		QTextStream ss(&s);

		// current actual offset (in bytes)
		const int chars_per_row = bytesPerRow();
		int64_t offset = normalizedOffset();

		const int64_t end       = std::max(selection_start_, selection_end_);
		const int64_t start     = std::min(selection_start_, selection_end_);
		const int64_t data_size = dataSize();

		// offset now refers to the first visible byte
		while(offset < end) {

			if((offset + chars_per_row) > start) {

				data_->seek(offset);
				const QByteArray row_data = data_->read(chars_per_row);

				if(!row_data.isEmpty()) {
					if(show_address_) {
						const address_t address_rva = address_offset_ + offset;
						const QString addressBuffer = formatAddress(address_rva);
						ss << addressBuffer << '|';
					}

					if(show_hex_) {
						drawHexDumpToBuffer(ss, offset, data_size, row_data);
						ss << "|";
					}

					if(show_ascii_) {
						drawAsciiDumpToBuffer(ss, offset, data_size, row_data);
						ss << "|";
					}

					if(show_comments_ && commentServer_) {
						drawCommentsToBuffer(ss, offset, data_size);
					}
				}

				ss << "\n";
			}
			offset += chars_per_row;
		}

		QApplication::clipboard()->setText(s);

		// TODO(eteran): do we want to trample the X11-selection too?
		QApplication::clipboard()->setText(s, QClipboard::Selection);
	}
}

//------------------------------------------------------------------------------
// Name: mnuAddrCopy
// Desc: Copy the starting address of the selected bytes
//------------------------------------------------------------------------------
void QHexView::mnuAddrCopy() {
	if(hasSelectedText()) {

		auto s = QString("0x%1").arg(selectedBytesAddress(), 0, 16);
		QApplication::clipboard()->setText(s);

		// TODO(eteran): do we want to trample the X11-selection too?
		QApplication::clipboard()->setText(s, QClipboard::Selection);
	}
}

//------------------------------------------------------------------------------
// Name: mnuSetFont
// Desc: slot used to set the font of the widget based on dialog selector
//------------------------------------------------------------------------------
void QHexView::mnuSetFont() {
	setFont(QFontDialog::getFont(0, font(), this));
}

//------------------------------------------------------------------------------
// Name: clear
// Desc: clears all data from the view
//------------------------------------------------------------------------------
void QHexView::clear() {
	data_ = nullptr;
	viewport()->update();
}

//------------------------------------------------------------------------------
// Name: hasSelectedText
// Desc: returns true if any text is selected
//------------------------------------------------------------------------------
bool QHexView::hasSelectedText() const {
	return !(selection_start_ == -1 || selection_end_ == -1);
}

//------------------------------------------------------------------------------
// Name: isInViewableArea
// Desc: returns true if the word at the given index is in the viewable area
//------------------------------------------------------------------------------
bool QHexView::isInViewableArea(int64_t index) const {

	const int64_t firstViewableWord = static_cast<int64_t>(verticalScrollBar()->value()) * row_width_;
	const int64_t viewableLines     = viewport()->height() / font_height_;
	const int64_t viewableWords     = viewableLines * row_width_;
	const int64_t lastViewableWord  = firstViewableWord + viewableWords;

	return index >= firstViewableWord && index < lastViewableWord;
}

//------------------------------------------------------------------------------
// Name: keyPressEvent
// Desc:
//------------------------------------------------------------------------------
void QHexView::keyPressEvent(QKeyEvent *event) {

	if(event == QKeySequence::SelectAll) {
		selectAll();
		viewport()->update();
	} else if(event == QKeySequence::MoveToStartOfDocument) {
		scrollTo(0);
	} else if(event == QKeySequence::MoveToEndOfDocument) {
		scrollTo(dataSize() - bytesPerRow());
	} else if(event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_Down) {
		int64_t offset = normalizedOffset();
		if(offset + 1 < dataSize()) {
			scrollTo(offset + 1);
		}
	} else if(event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_Up) {
		int64_t offset = normalizedOffset();
		if(offset > 0) {
			scrollTo(offset - 1);
		}
	} else if(event->modifiers() & Qt::ShiftModifier && hasSelectedText()) {
		// Attempting to match the highlighting behavior of common text
		// editors where highlighting to the left or up will keep the
		// first character (byte in our case) highlighted while also
		// extending back or up.
		auto dir = event->key();
		switch(dir) {
		case Qt::Key_Right:
			if (selection_start_ == selection_end_) {
				selection_start_ -= word_width_;
			}
			if(selection_end_ / word_width_ < dataSize()) {
				selection_end_ += word_width_;
			}
			break;
		case Qt::Key_Left:
			if ((selection_end_ - word_width_) == selection_start_) {
				selection_start_ += word_width_;
				selection_end_ -= word_width_;
			}
			if (selection_end_ / word_width_ > 0) {
				selection_end_ -= word_width_;
			}
			break;
		case Qt::Key_Down:
			selection_end_ += row_width_;
			selection_end_ = std::min(selection_end_, dataSize() * word_width_);
			break;
		case Qt::Key_Up:
			if ((selection_end_ - word_width_) == selection_start_) {
				selection_start_ += word_width_;
			}
			selection_end_ -= row_width_;
			if (selection_end_ == 0) {
				 selection_end_ = 0;
			}
			break;
		default:
			break;
		}
		viewport()->update();
	} else {
		QAbstractScrollArea::keyPressEvent(event);
	}
}

//------------------------------------------------------------------------------
// Name: vertline3
// Desc: returns the x coordinate of the 3rd line
//------------------------------------------------------------------------------
int QHexView::vertline3() const {
	if(show_ascii_) {
		const int elements = bytesPerRow();
		return asciiDumpLeft() + (elements * font_width_) + (font_width_ / 2);
	} else {
		return vertline2();
	}
}

//------------------------------------------------------------------------------
// Name: vertline2
// Desc: returns the x coordinate of the 2nd line
//------------------------------------------------------------------------------
int QHexView::vertline2() const {
	if(show_hex_) {
		const int elements = row_width_ * (charsPerWord() + 1) - 1;
		return hexDumpLeft() + (elements * font_width_) + (font_width_ / 2);
	} else {
		return vertline1();
	}
}

//------------------------------------------------------------------------------
// Name: vertline1
// Desc: returns the x coordinate of the 1st line
//------------------------------------------------------------------------------
int QHexView::vertline1() const {
	if(show_address_) {
		const int elements = addressLen();
		return (elements * font_width_) + (font_width_ / 2);
	} else {
		return 0;
	}
}

//------------------------------------------------------------------------------
// Name: hexDumpLeft
// Desc: returns the x coordinate of the hex-dump field left edge
//------------------------------------------------------------------------------
int QHexView::hexDumpLeft() const {
	return vertline1() + (font_width_ / 2);
}

//------------------------------------------------------------------------------
// Name: asciiDumpLeft
// Desc: returns the x coordinate of the ascii-dump field left edge
//------------------------------------------------------------------------------
int QHexView::asciiDumpLeft() const {
	return vertline2() + (font_width_ / 2);
}

//------------------------------------------------------------------------------
// Name: commentLeft
// Desc: returns the x coordinate of the comment field left edge
//------------------------------------------------------------------------------
int QHexView::commentLeft() const {
	return vertline3() + (font_width_ / 2);
}

//------------------------------------------------------------------------------
// Name: charsPerWord
// Desc: returns how many characters each word takes up
//------------------------------------------------------------------------------
int QHexView::charsPerWord() const {
	return word_width_ * 2;
}

//------------------------------------------------------------------------------
// Name: addressLen
// Desc: returns the lenth in characters the address will take up
//------------------------------------------------------------------------------
int QHexView::addressLen() const {
	const int addressLength = (address_size_ * CHAR_BIT) / 4;
	return addressLength + (show_address_separator_ ? 1 : 0);
}

//------------------------------------------------------------------------------
// Name: updateScrollbars
// Desc: recalculates scrollbar maximum value base on lines total and lines viewable
//------------------------------------------------------------------------------
void QHexView::updateScrollbars() {
	const int64_t sz = dataSize();
	const int bpr = bytesPerRow();

	const int maxval = sz / bpr + ((sz % bpr) ? 1 : 0) - viewport()->height() / font_height_;

	verticalScrollBar()->setMaximum(std::max(0, maxval));
	horizontalScrollBar()->setMaximum(std::max(0, ((vertline3() - viewport()->width()) / font_width_)));
}

//------------------------------------------------------------------------------
// Name: scrollTo
// Desc: scrolls view to given byte offset
//------------------------------------------------------------------------------
void QHexView::scrollTo(address_t offset) {

	const int bpr = bytesPerRow();
	origin_ = offset % bpr;
	address_t address = offset / bpr;

	updateScrollbars();

	if(origin_ != 0) {
		++address;
	}

	verticalScrollBar()->setValue(address);
	viewport()->update();
}

//------------------------------------------------------------------------------
// Name: setShowAddress
// Desc: sets if we are to display the address column
//------------------------------------------------------------------------------
void QHexView::setShowAddress(bool show) {
	show_address_ = show;
	updateScrollbars();
	viewport()->update();
}

//------------------------------------------------------------------------------
// Name: setShowHexDump
// Desc: sets if we are to display the hex-dump column
//------------------------------------------------------------------------------
void QHexView::setShowHexDump(bool show) {
	show_hex_ = show;
	updateScrollbars();
	viewport()->update();
}

//------------------------------------------------------------------------------
// Name: setShowComments
// Desc: sets if we are to display the comments column
//------------------------------------------------------------------------------
void QHexView::setShowComments(bool show) {
	show_comments_ = show;
	updateScrollbars();
	viewport()->update();
}

//------------------------------------------------------------------------------
// Name: setShowAsciiDump
// Desc: sets if we are to display the ascii-dump column
//------------------------------------------------------------------------------
void QHexView::setShowAsciiDump(bool show) {
	show_ascii_ = show;
	updateScrollbars();
	viewport()->update();
}

//------------------------------------------------------------------------------
// Name: setRowWidth
// Desc: sets the row width (units is words)
//------------------------------------------------------------------------------
void QHexView::setRowWidth(int rowWidth) {
	Q_ASSERT(rowWidth >= 0);
	row_width_ = rowWidth;
	updateScrollbars();
	viewport()->update();
}

//------------------------------------------------------------------------------
// Name: setWordWidth
// Desc: sets how many bytes represent a word
//------------------------------------------------------------------------------
void QHexView::setWordWidth(int wordWidth) {
	Q_ASSERT(wordWidth >= 0);
	word_width_ = wordWidth;
	updateScrollbars();
	viewport()->update();
}

//------------------------------------------------------------------------------
// Name: bytesPerRow
//------------------------------------------------------------------------------
int QHexView::bytesPerRow() const {
	return row_width_ * word_width_;
}

//------------------------------------------------------------------------------
// Name: pixelToWord
//------------------------------------------------------------------------------
int64_t QHexView::pixelToWord(int x, int y) const {
	int64_t word = -1;

	switch(highlighting_) {
	case Highlighting_Data:
#if 0
		// Make pixels outside the word correspond to the nearest word, not to the right-hand one
		x -= font_width_ / 2;
#endif
		// the right edge of a box is kinda quirky, so we pretend there is one
		// extra character there
		x = qBound(vertline1(), x, static_cast<int>(vertline2() + font_width_));

		// the selection is in the data view portion
		x -= vertline1();

		// scale x/y down to character from pixels
		x = x / font_width_ + (fmod(x, font_width_) >= font_width_ / 2 ? 1 : 0);
		y /= font_height_;

		// make x relative to rendering mode of the bytes
		x /= (charsPerWord() + 1);
		break;
	case Highlighting_Ascii:
		x = qBound(asciiDumpLeft(), x, vertline3());

		// the selection is in the ascii view portion
		x -= asciiDumpLeft();

		// scale x/y down to character from pixels
		x /= font_width_;
		y /= font_height_;

		// make x relative to rendering mode of the bytes
		x /= word_width_;
		break;
	default:
		Q_ASSERT(0);
		break;
	}

	// starting offset in bytes
	int64_t start_offset = normalizedOffset();

	// convert byte offset to word offset, rounding up
	start_offset /= static_cast<unsigned int>(word_width_);

	if((origin_ % word_width_) != 0) {
		start_offset += 1;
	}

	word = ((y * row_width_) + x) + start_offset;

	return word;
}

//------------------------------------------------------------------------------
// Name: updateToolTip
//------------------------------------------------------------------------------
void QHexView::updateToolTip() {
	if(selectedBytesSize() <= 0) {
		return;
	}

	auto sb = selectedBytes();
	const address_t start = selectedBytesAddress();
	const address_t end = selectedBytesAddress() + sb.size();

	QString tooltip = //noWordWrap % addr;
		QString("<p style='white-space:pre'>")	//prevent word wrap
		% QString("<b>Addr: </b>") % formatAddress(start) % " - " % formatAddress(end)
		% QString("<br><b>Hex:</b> 0x") % sb.toHex()
		% QString("<br><b>UInt32:</b> ") % QString::number(qFromLittleEndian<quint32>(sb.data()))
		% QString("<br><b>Int32:</b> ") % QString::number(qFromLittleEndian<qint32>(sb.data()))
		% QString("</p>");

	setToolTip(tooltip);
}

//------------------------------------------------------------------------------
// Name: mouseDoubleClickEvent
//------------------------------------------------------------------------------
void QHexView::mouseDoubleClickEvent(QMouseEvent *event) {
	if(event->button() == Qt::LeftButton) {
		const int x = event->x() + horizontalScrollBar()->value() * font_width_;
		const int y = event->y();
		if(x >= vertline1() && x < vertline2()) {

			highlighting_ = Highlighting_Data;

			const int64_t offset = pixelToWord(x, y);
			int64_t byte_offset = offset * word_width_;
			if(origin_) {
				if(origin_ % word_width_) {
					byte_offset -= word_width_ - (origin_ % word_width_);
				}
			}

			selection_start_ = byte_offset;
			selection_end_ = selection_start_ + word_width_;
			viewport()->update();
		} else if(x < vertline1()) {
			highlighting_ = Highlighting_Data;

			const int64_t offset = pixelToWord(vertline1(), y);
			int64_t byte_offset = offset * word_width_;
			if(origin_) {
				if(origin_ % word_width_) {
					byte_offset -= word_width_ - (origin_ % word_width_);
				}
			}

			const unsigned chars_per_row = bytesPerRow();

			selection_start_ = byte_offset;
			selection_end_ = byte_offset + chars_per_row;
			viewport()->update();
		}
	}

	updateToolTip();
}

//------------------------------------------------------------------------------
// Name: mousePressEvent
//------------------------------------------------------------------------------
void QHexView::mousePressEvent(QMouseEvent *event) {

	if(event->button() == Qt::LeftButton) {
		const int x = event->x() + horizontalScrollBar()->value() * font_width_;
		const int y = event->y();

		if(x < vertline2()) {
			highlighting_ = Highlighting_Data;
		} else if(x >= vertline2()) {
			highlighting_ = Highlighting_Ascii;
		}

		const int64_t offset = pixelToWord(x, y);
		int64_t byte_offset = offset * word_width_;
		if(origin_) {
			if(origin_ % word_width_) {
				byte_offset -= word_width_ - (origin_ % word_width_);
			}
		}

		if(offset < dataSize()) {
			if (hasSelectedText() && (event->modifiers() & Qt::ShiftModifier)) {
				selection_end_ = byte_offset;
			} else {
				selection_start_ = byte_offset;
				selection_end_ = selection_start_ + word_width_;
			}
		} else {
			selection_start_ = selection_end_ = -1;
		}
		viewport()->update();
	}
	if (event->button() == Qt::RightButton) {

	}

	updateToolTip();
}

//------------------------------------------------------------------------------
// Name: mouseMoveEvent
//------------------------------------------------------------------------------
void QHexView::mouseMoveEvent(QMouseEvent *event) {
	if(highlighting_ != Highlighting_None) {
		const int x = event->x() + horizontalScrollBar()->value() * font_width_;
		const int y = event->y();

		const int64_t offset = pixelToWord(x, y);

		if(selection_start_ != -1) {
			if(offset == -1) {
				selection_end_ = row_width_;
			} else {

				int64_t byte_offset = (offset * word_width_);

				if(origin_) {
					if(origin_ % word_width_) {
						byte_offset -= word_width_ - (origin_ % word_width_);
					}

				}
				selection_end_ = byte_offset;
				if (selection_end_ == selection_start_) {
					selection_end_ += word_width_;
				}
			}

			if(selection_end_ < 0) {
				selection_end_ = 0;
			}

			if(!isInViewableArea(selection_end_)) {
				ensureVisible(selection_end_);
			}

		}
		viewport()->update();
		updateToolTip();
	}
}

//------------------------------------------------------------------------------
// Name: mouseReleaseEvent
//------------------------------------------------------------------------------
void QHexView::mouseReleaseEvent(QMouseEvent *event) {
	if(event->button() == Qt::LeftButton) {
		highlighting_ = Highlighting_None;
	}
}

//------------------------------------------------------------------------------
// Name: ensureVisible
//------------------------------------------------------------------------------
void QHexView::ensureVisible(int64_t index) {
	Q_UNUSED(index);
#if 0
	if(index < normalizedOffset()) {
		while(index < normalizedOffset()) {
			verticalScrollBar()->setValue(verticalScrollBar()->value() - 1);
		}
		viewport()->update();
	} else {
		while(index > normalizedOffset()) {
			verticalScrollBar()->setValue(verticalScrollBar()->value() + 1);
		}
		viewport()->update();
	}
#endif
}

//------------------------------------------------------------------------------
// Name: setData
//------------------------------------------------------------------------------
void QHexView::setData(QIODevice *d) {
	if (d->isSequential() || !d->size()) {
		internal_buffer_ = std::make_unique<QBuffer>();
		internal_buffer_->setData(d->readAll());
		internal_buffer_->open(QBuffer::ReadOnly);
		data_ = internal_buffer_.get();
	} else {
		data_ = d;
	}

	if(data_->size() > Q_INT64_C(0xffffffff)) {
		address_size_ = Address64;
	}

	deselect();
	updateScrollbars();
	viewport()->update();
}

//------------------------------------------------------------------------------
// Name: resizeEvent
//------------------------------------------------------------------------------
void QHexView::resizeEvent(QResizeEvent *) {
	updateScrollbars();
}

//------------------------------------------------------------------------------
// Name: setAddressOffset
//------------------------------------------------------------------------------
void QHexView::setAddressOffset(address_t offset) {
	address_offset_ = offset;
}

//------------------------------------------------------------------------------
// Name: isSelected
//------------------------------------------------------------------------------
bool QHexView::isSelected(int64_t index) const {

	bool ret = false;
	if(index < dataSize()) {
		if(selection_start_ != selection_end_) {
			if(selection_start_ < selection_end_) {
				ret = (index >= selection_start_ && index < selection_end_);
			} else {
				ret = (index >= selection_end_ && index < selection_start_);
			}
		}
	}
	return ret;
}

//------------------------------------------------------------------------------
// Name: drawComments
//------------------------------------------------------------------------------
void QHexView::drawComments(QPainter &painter, uint64_t offset, int row, uint64_t size) const {

	Q_UNUSED(size);

	painter.setPen(palette().color(QPalette::Text));

	const address_t address = address_offset_ + offset;
	const QString comment   = commentServer_->comment(address, word_width_);

	painter.drawText(
		commentLeft(),
		row,
		comment.length() * font_width_,
		font_height_,
		Qt::AlignTop,
		comment
		);
}

//------------------------------------------------------------------------------
// Name: drawAsciiDumpToBuffer
//------------------------------------------------------------------------------
void QHexView::drawAsciiDumpToBuffer(QTextStream &stream, uint64_t offset, uint64_t size, const QByteArray &row_data) const {
	// i is the byte index
	const int chars_per_row = bytesPerRow();
	for(int i = 0; i < chars_per_row; ++i) {
		const uint64_t index = offset + i;
		if(index < size) {
			if(isSelected(index)) {
				const unsigned char ch = row_data[i];
				const bool printable = is_printable(ch) && ch != '\f' && ch != '\t' && ch != '\r' && ch != '\n' && ch < 0x80;
				const char byteBuffer(printable ? ch : unprintable_char_);
				stream << byteBuffer;
			} else {
				stream << ' ';
			}
		} else {
			break;
		}
	}
}

//------------------------------------------------------------------------------
// Name: drawCommentsToBuffer
//------------------------------------------------------------------------------
void QHexView::drawCommentsToBuffer(QTextStream &stream, uint64_t offset, uint64_t size) const {
	Q_UNUSED(size);
	const address_t address = address_offset_ + offset;
	const QString comment   = commentServer_->comment(address, word_width_);
	stream << comment;
}

//------------------------------------------------------------------------------
// Name: formatBytes
// Desc: formats bytes in a way that's suitable for rendering in a hexdump
//       having this as a separate function serves two purposes.
//       #1 no code duplication between the buffer and QPainter versions
//       #2 this encourages NRVO of the return value more than an integrated
//------------------------------------------------------------------------------
QString QHexView::formatBytes(const QByteArray &row_data, int index) const {
	union {
		uint64_t q;
		uint32_t d;
		uint16_t w;
		uint8_t  b;
	} value = { 0 };

	char byte_buffer[32];

	switch(word_width_) {
	case 1:
		value.b |= (row_data[index + 0] & 0xff);
		qsnprintf(byte_buffer, sizeof(byte_buffer), "%02x", value.b);
		break;
	case 2:
		value.w |= (row_data[index + 0] & 0xff);
		value.w |= (row_data[index + 1] & 0xff) << 8;
		qsnprintf(byte_buffer, sizeof(byte_buffer), "%04x", value.w);
		break;
	case 4:
		value.d |= (row_data[index + 0] & 0xff);
		value.d |= (row_data[index + 1] & 0xff) << 8;
		value.d |= (row_data[index + 2] & 0xff) << 16;
		value.d |= (row_data[index + 3] & 0xff) << 24;
		qsnprintf(byte_buffer, sizeof(byte_buffer), "%08x", value.d);
		break;
	case 8:
		// we need the cast to ensure that it won't assume 32-bit
		// and drop bits shifted more that 31
		value.q |= static_cast<uint64_t>(row_data[index + 0] & 0xff);
		value.q |= static_cast<uint64_t>(row_data[index + 1] & 0xff) << 8;
		value.q |= static_cast<uint64_t>(row_data[index + 2] & 0xff) << 16;
		value.q |= static_cast<uint64_t>(row_data[index + 3] & 0xff) << 24;
		value.q |= static_cast<uint64_t>(row_data[index + 4] & 0xff) << 32;
		value.q |= static_cast<uint64_t>(row_data[index + 5] & 0xff) << 40;
		value.q |= static_cast<uint64_t>(row_data[index + 6] & 0xff) << 48;
		value.q |= static_cast<uint64_t>(row_data[index + 7] & 0xff) << 56;
		qsnprintf(byte_buffer, sizeof(byte_buffer), "%016llx", value.q);
		break;
	}

	return byte_buffer;
}

//------------------------------------------------------------------------------
// Name: drawHexDumpToBuffer
//------------------------------------------------------------------------------
void QHexView::drawHexDumpToBuffer(QTextStream &stream, uint64_t offset, uint64_t size, const QByteArray &row_data) const {

	Q_UNUSED(size);

	// i is the word we are currently rendering
	for(int i = 0; i < row_width_; ++i) {

		// index of first byte of current 'word'
		const uint64_t index = offset + (i * word_width_);

		// equal <=, not < because we want to test the END of the word we
		// about to render, not the start, it's allowed to end at the very last
		// byte
		if(index + word_width_ <= size) {
			const QString byteBuffer = formatBytes(row_data, i * word_width_);

			if(isSelected(index)) {
				stream << byteBuffer;
			} else {
				stream << QString(byteBuffer.length(), ' ');
			}

			if(i != (row_width_ - 1)) {
				stream << ' ';
			}
		} else {
			break;
		}
	}
}

//------------------------------------------------------------------------------
// Name: drawHexDump
//------------------------------------------------------------------------------
void QHexView::drawHexDump(QPainter &painter, uint64_t offset, int row, uint64_t size, int *word_count, const QByteArray &row_data) const {
	const int hex_dump_left = hexDumpLeft();

	// i is the word we are currently rendering
	for(int i = 0; i < row_width_; ++i) {

		// index of first byte of current 'word'
		const uint64_t index = offset + (i * word_width_);

		// equal <=, not < because we want to test the END of the word we
		// about to render, not the start, it's allowed to end at the very last
		// byte
		if(index + word_width_ <= size) {

			const QString byteBuffer = formatBytes(row_data, i * word_width_);

			const int drawLeft  = hex_dump_left + (i * (charsPerWord() + 1) * font_width_);
			const int drawWidth = charsPerWord() * font_width_;

			if(isSelected(index)) {
			
				const QPalette::ColorGroup group = hasFocus() ? QPalette::Active : QPalette::Inactive;

				painter.fillRect(
					QRectF(
						drawLeft,
						row,
						drawWidth,
						font_height_),
					palette().color(group, QPalette::Highlight)
					);

				// should be highlight the space between us and the next word?
				if(i != (row_width_ - 1)) {
					if(isSelected(index + 1)) {
						painter.fillRect(
							QRectF(
								drawLeft + drawWidth,
								row,
								font_width_,
								font_height_),
							palette().color(group, QPalette::Highlight)
							);

					}
				}

				painter.setPen(palette().color(group, QPalette::HighlightedText));
			} else {
				painter.setPen(QPen((*word_count & 1) ? even_word_ : palette().color(QPalette::Text)));

				// implement cold zone stuff
				if(cold_zone_end_ > address_offset_ && offset < cold_zone_end_ - address_offset_) {
					painter.setPen(QPen(Qt::gray));
				}
			}

			painter.drawText(
				drawLeft,
				row,
				byteBuffer.length() * font_width_,
				font_height_,
				Qt::AlignTop,
				byteBuffer
				);

			++(*word_count);
		} else {
			break;
		}
	}
}

//------------------------------------------------------------------------------
// Name: drawAsciiDump
//------------------------------------------------------------------------------
void QHexView::drawAsciiDump(QPainter &painter, uint64_t offset, int row, uint64_t size, const QByteArray &row_data) const {
	const int ascii_dump_left = asciiDumpLeft();

	// i is the byte index
	const int chars_per_row = bytesPerRow();
	for(int i = 0; i < chars_per_row; ++i) {

		const uint64_t index = offset + i;

		if(index < size) {
			const char ch        = row_data[i];
			const int drawLeft   = ascii_dump_left + i * font_width_;
			const bool printable = is_printable(ch);

			// drawing a selected character
			if(isSelected(index)) {
			
				const QPalette::ColorGroup group = hasFocus() ? QPalette::Active : QPalette::Inactive;

				painter.fillRect(
					QRectF(
						drawLeft,
						row,
						font_width_,
						font_height_),
					palette().color(group, QPalette::Highlight)
				);
	
				painter.setPen(palette().color(group, QPalette::HighlightedText));

			} else {
				painter.setPen(QPen(printable ? palette().color(QPalette::Text) : non_printable_text_));

				// implement cold zone stuff
				if(cold_zone_end_ > address_offset_ && offset < cold_zone_end_ - address_offset_) {
					painter.setPen(QPen(Qt::gray));
				}
			}

			const QString byteBuffer(printable ? ch : unprintable_char_);

			painter.drawText(
				drawLeft,
				row,
				font_width_,
				font_height_,
				Qt::AlignTop,
				byteBuffer
			);
		} else {
			break;
		}
	}
}

//------------------------------------------------------------------------------
// Name: paintEvent
//------------------------------------------------------------------------------
void QHexView::paintEvent(QPaintEvent * event) {

	Q_UNUSED(event);
	QPainter painter(viewport());
	painter.translate(-horizontalScrollBar()->value() * font_width_, 0);

	int word_count = 0;

	// pixel offset of this row
	int row = 0;

	const int chars_per_row = bytesPerRow();

	// current actual offset (in bytes), we do this manually because we have the else
	// case unlike the helper function
	int64_t offset = verticalScrollBar()->value() * chars_per_row;

	if(origin_ != 0) {
		if(offset > 0) {
			offset += origin_;
			offset -= chars_per_row;
		} else {
			origin_ = 0;
			updateScrollbars();
		}
	}

	const int64_t data_size = dataSize();
	const int widget_height = height();

	while(row + font_height_ < widget_height && offset < data_size) {

		data_->seek(offset);
		const QByteArray row_data = data_->read(chars_per_row);

		if(!row_data.isEmpty()) {
			if(show_address_) {
				const address_t address_rva = address_offset_ + offset;
				const QString addressBuffer = formatAddress(address_rva);
				painter.setPen(QPen(address_color_));

				// implement cold zone stuff
				if(cold_zone_end_ > address_offset_ && static_cast<address_t>(offset) < cold_zone_end_ - address_offset_) {
					painter.setPen(QPen(Qt::gray));
				}

				painter.drawText(0, row, addressBuffer.length() * font_width_, font_height_, Qt::AlignTop, addressBuffer);
			}

			if(show_hex_) {
				drawHexDump(painter, offset, row, data_size, &word_count, row_data);
			}

			if(show_ascii_) {
				drawAsciiDump(painter, offset, row, data_size, row_data);
			}

			if(show_comments_ && commentServer_) {
				drawComments(painter, offset, row, data_size);
			}
		}

		offset += chars_per_row;
		row += font_height_;
	}

	painter.setPen(palette().color(QPalette::Shadow));

	if(show_address_ && show_vertline1_) {
		const int vertline1_x = vertline1();
		painter.drawLine(vertline1_x, 0, vertline1_x, widget_height);
	}

	if(show_hex_ && show_vertline2_) {
		const int vertline2_x = vertline2();
		painter.drawLine(vertline2_x, 0, vertline2_x, widget_height);
	}

	if(show_ascii_ && show_vertline3_) {
		const int vertline3_x = vertline3();
		painter.drawLine(vertline3_x, 0, vertline3_x, widget_height);
	}
}

//------------------------------------------------------------------------------
// Name: selectAll
//------------------------------------------------------------------------------
void QHexView::selectAll() {
	selection_start_ = 0;
	selection_end_   = dataSize();
}

//------------------------------------------------------------------------------
// Name: deselect
//------------------------------------------------------------------------------
void QHexView::deselect() {
	selection_start_ = -1;
	selection_end_   = -1;
}

//------------------------------------------------------------------------------
// Name: allBytes
//------------------------------------------------------------------------------
QByteArray QHexView::allBytes() const {
	data_->seek(0);
	return data_->readAll();
}

//------------------------------------------------------------------------------
// Name: selectedBytes
//------------------------------------------------------------------------------
QByteArray QHexView::selectedBytes() const {
	if(hasSelectedText()) {
		const int64_t s = std::min(selection_start_, selection_end_);
		const int64_t e = std::max(selection_start_, selection_end_);

		data_->seek(s);
		return data_->read(e - s);
	}

	return QByteArray();
}

//------------------------------------------------------------------------------
// Name: selectedBytesAddress
//------------------------------------------------------------------------------
QHexView::address_t QHexView::selectedBytesAddress() const {
	const address_t select_base = std::min(selection_start_, selection_end_);
	return select_base + address_offset_;
}

//------------------------------------------------------------------------------
// Name: selectedBytesSize
//------------------------------------------------------------------------------
uint64_t QHexView::selectedBytesSize() const {

	uint64_t ret;
	if(selection_end_ > selection_start_) {
		ret = selection_end_ - selection_start_;
	} else {
		ret = selection_start_ - selection_end_;
	}

	return ret;
}

//------------------------------------------------------------------------------
// Name: addressOffset
//------------------------------------------------------------------------------
QHexView::address_t QHexView::addressOffset() const {
	return address_offset_;
}

//------------------------------------------------------------------------------
// Name: showHexDump
//------------------------------------------------------------------------------
bool QHexView::showHexDump() const {
	return show_hex_;
}

//------------------------------------------------------------------------------
// Name: showAddress
//------------------------------------------------------------------------------
bool QHexView::showAddress() const {
	return show_address_;
}

//------------------------------------------------------------------------------
// Name: showAsciiDump
//------------------------------------------------------------------------------
bool QHexView::showAsciiDump() const {
	return show_ascii_;
}

//------------------------------------------------------------------------------
// Name: showComments
//------------------------------------------------------------------------------
bool QHexView::showComments() const {
	return show_comments_;
}

//------------------------------------------------------------------------------
// Name: wordWidth
//------------------------------------------------------------------------------
int QHexView::wordWidth() const {
	return word_width_;
}

//------------------------------------------------------------------------------
// Name: rowWidth
//------------------------------------------------------------------------------
int QHexView::rowWidth() const {
	return row_width_;
}

//------------------------------------------------------------------------------
// Name: firstVisibleAddress
//------------------------------------------------------------------------------
QHexView::address_t QHexView::firstVisibleAddress() const {
	// current actual offset (in bytes)
	int64_t offset = normalizedOffset();
	return offset + addressOffset();
}

//------------------------------------------------------------------------------
// Name: setAddressSize
//------------------------------------------------------------------------------
void QHexView::setAddressSize(AddressSize address_size) {
	address_size_ = address_size;
	viewport()->update();
}

//------------------------------------------------------------------------------
// Name: addressSize
//------------------------------------------------------------------------------
QHexView::AddressSize QHexView::addressSize() const {
	return address_size_;
}

//------------------------------------------------------------------------------
// Name: setColdZoneEnd
//------------------------------------------------------------------------------
void QHexView::setColdZoneEnd(address_t offset) {
	cold_zone_end_ = offset;
}

//------------------------------------------------------------------------------
// Name: userConfigWordWidth
//------------------------------------------------------------------------------
bool QHexView::userConfigWordWidth() const {
	return user_can_set_word_width_;
}

//------------------------------------------------------------------------------
// Name: userConfigRowWidth
//------------------------------------------------------------------------------
bool QHexView::userConfigRowWidth() const {
	return user_can_set_row_width_;
}

//------------------------------------------------------------------------------
// Name: setUserConfigWordWidth
//------------------------------------------------------------------------------
void QHexView::setUserConfigWordWidth(bool value) {
	user_can_set_word_width_ = value;
	viewport()->update();
}

//------------------------------------------------------------------------------
// Name: setUserConfigRowWidth
//------------------------------------------------------------------------------
void QHexView::setUserConfigRowWidth(bool value) {
	user_can_set_row_width_ = value;
	viewport()->update();
}
