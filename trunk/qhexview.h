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

#ifndef QHEXVIEW_H_
#define QHEXVIEW_H_

#include <QAbstractScrollArea>
#include <QSharedPointer>

class QBuffer;
class QByteArray;
class QIODevice;
class QMenu;
class QString;
class QTextStream;

class QHexView : public QAbstractScrollArea {
	Q_OBJECT

public:
	enum AddressSize {
		Address32 = 4,
		Address64 = 8
	};
	
public:
	typedef quint64 address_t;

	class CommentServerInterface {
	public:
		typedef QSharedPointer<CommentServerInterface> pointer;
	public:
		virtual ~CommentServerInterface() {}
	public:
		virtual void set_comment(address_t address, const QString &comment) = 0;
		virtual QString comment(address_t address, int size) const = 0;
		virtual void clear() = 0;
	};

public:
	QHexView(QWidget *parent = 0);
	virtual ~QHexView();

	CommentServerInterface::pointer commentServer() const;
	void setCommentServer(const CommentServerInterface::pointer &p);

protected:
	virtual void paintEvent(QPaintEvent *event);
	virtual void resizeEvent(QResizeEvent *event);
	virtual void mousePressEvent(QMouseEvent *event);
	virtual void mouseMoveEvent(QMouseEvent *event);
	virtual void mouseReleaseEvent(QMouseEvent *event);
	virtual void keyPressEvent(QKeyEvent *event);
	virtual void mouseDoubleClickEvent(QMouseEvent *event);
	virtual void contextMenuEvent(QContextMenuEvent *event);

public Q_SLOTS:
	void setShowAddress(bool);
	void setShowAsciiDump(bool);
	void setShowHexDump(bool);
	void setShowComments(bool);
	void setWordWidth(int);
	void setRowWidth(int);
	void setFont(const QFont &font);
	void setShowAddressSeparator(bool value);
	void repaint();

public:
	address_t firstVisibleAddress() const;
	address_t addressOffset() const;
	bool showHexDump() const;
	bool showAddress() const;
	bool showAsciiDump() const;
	bool showComments() const;
	QColor lineColor() const;
	QColor addressColor() const;
	int wordWidth() const;
	int rowWidth() const;
	AddressSize addressSize() const;

public:
	QIODevice *data() const { return data_; }

	void setData(QIODevice *d);
	void setAddressOffset(address_t offset);
	void scrollTo(address_t offset);
	void setAddressSize(AddressSize address_size);

	address_t selectedBytesAddress() const;
	quint64 selectedBytesSize() const;
	QByteArray selectedBytes() const;
	QByteArray allBytes() const;
	QMenu *createStandardContextMenu();

public Q_SLOTS:
	void clear();
	void selectAll();
	void deselect();
	bool hasSelectedText() const;
	void mnuSetFont();
	void mnuCopy();

private:
	QString formatAddress(address_t address);
	QString formatBytes(const QByteArray &row_data, int index) const;
	bool isInViewableArea(qint64 index) const;
	bool isSelected(qint64 index) const;
	int asciiDumpLeft() const;
	int commentLeft() const;
	int hexDumpLeft() const;
	int line1() const;
	int line2() const;
	int line3() const;
	qint64 dataSize() const;
	qint64 pixelToWord(int x, int y) const;
	unsigned int addressLen() const;
	unsigned int bytesPerRow() const;
	unsigned int charsPerWord() const;
	void drawAsciiDump(QPainter &painter, quint64 offset, unsigned int row, quint64 size, const QByteArray &row_data) const;
	void drawAsciiDumpToBuffer(QTextStream &stream, quint64 offset, quint64 size, const QByteArray &row_data) const;
	void drawComments(QPainter &painter, quint64 offset, unsigned int row, quint64 size) const;
	void drawCommentsToBuffer(QTextStream &stream, quint64 offset, quint64 size) const;
	void drawHexDump(QPainter &painter, quint64 offset, unsigned int row, quint64 size, int *word_count, const QByteArray &row_data) const;
	void drawHexDumpToBuffer(QTextStream &stream, quint64 offset, quint64 size, const QByteArray &row_data) const;
	void updateScrollbars();

private:
	CommentServerInterface::pointer comment_server_;
	QBuffer                        *internal_buffer_;
	QColor                          address_color_;          // color of the address in display
	QColor                          even_word_;
	QColor                          non_printable_text_;
	QIODevice                      *data_;
	address_t  					    address_offset_;         // this is the offset that our base address is relative to
	address_t  					    origin_;
	bool       					    show_address_;           // should we show the address display?
	bool       					    show_ascii_;             // should we show the ascii display?
	bool       					    show_comments_;
	bool       					    show_hex_;               // should we show the hex display?
	bool 						    show_address_separator_; // should we show ':' character in address to separate high/low portions
	bool 						    show_line1_;
	bool 						    show_line2_;
	bool 						    show_line3_;
	char   						    unprintable_char_;
	int       					    font_height_;            // height of a character in this font
	int        					    row_width_;              // amount of 'words' per row
	int       					    word_width_;             // size of a 'word' in bytes
	qint64    					    selection_end_;          // index of last selected word (or -1)
	qint64    					    selection_start_;        // index of first selected word (or -1)
	qreal    					    font_width_;             // width of a character in this font
	
	enum {
		Highlighting_None,
		Highlighting_Data,
		Highlighting_Ascii
	} highlighting_;
	
	AddressSize address_size_;
};

#endif
