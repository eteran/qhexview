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
#include <QBuffer>
#include <memory>
#include <cstdint>

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
	using address_t = uint64_t;

private:
	class CommentServerBase {
	public:
		virtual ~CommentServerBase() = default;
		virtual QString comment(address_t address, int size) const = 0;
	};

	template <class T>
	class CommentServerWrapper : public CommentServerBase {
	public:
		explicit CommentServerWrapper(const T *commentServer) : commentServer_(commentServer) {
		}

		QString comment(address_t address, int size) const override {
			return commentServer_->comment(address, size);
		}
	private:
		const T *commentServer_;
	};

public:
	explicit QHexView(QWidget *parent = nullptr);
	~QHexView() override = default;

public:
	// We use type erasure to accept ANY type which has a QString comment(const edb::address_t &) method
	template <class T>
	void setCommentServer(T *p) {
		commentServer_ = std::make_unique<CommentServerWrapper<T>>(p);
	}

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;

public Q_SLOTS:
	void setUserConfigWordWidth(bool);
	void setUserConfigRowWidth(bool);
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
	bool userConfigWordWidth() const;
	bool userConfigRowWidth() const;
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
	void setColdZoneEnd(address_t offset);

	address_t selectedBytesAddress() const;
	uint64_t selectedBytesSize() const;
	QByteArray selectedBytes() const;
	QByteArray allBytes() const;
	QMenu *createStandardContextMenu();

	bool hasSelectedText() const;

public Q_SLOTS:
	void clear();
	void selectAll();
	void deselect();
	void mnuSetFont();
	void mnuCopy();
	void mnuAddrCopy();

private:
	QString formatAddress(address_t address);
	QString formatBytes(const QByteArray &row_data, int index) const;
	bool isInViewableArea(int64_t index) const;
	bool isSelected(int64_t index) const;
	int asciiDumpLeft() const;
	int commentLeft() const;
	int hexDumpLeft() const;
	int vertline1() const;
	int vertline2() const;
	int vertline3() const;
	int64_t dataSize() const;
	int64_t normalizedOffset() const;
	int64_t pixelToWord(int x, int y) const;
	int addressLen() const;
	int bytesPerRow() const;
	int charsPerWord() const;
	void drawAsciiDump(QPainter &painter, uint64_t offset, int row, uint64_t size, const QByteArray &row_data) const;
	void drawAsciiDumpToBuffer(QTextStream &stream, uint64_t offset, uint64_t size, const QByteArray &row_data) const;
	void drawComments(QPainter &painter, uint64_t offset, int row, uint64_t size) const;
	void drawCommentsToBuffer(QTextStream &stream, uint64_t offset, uint64_t size) const;
	void drawHexDump(QPainter &painter, uint64_t offset, int row, uint64_t size, int *word_count, const QByteArray &row_data) const;
	void drawHexDumpToBuffer(QTextStream &stream, uint64_t offset, uint64_t size, const QByteArray &row_data) const;
	void ensureVisible(int64_t index);
	void updateScrollbars();
	void updateToolTip();

private:
	std::unique_ptr<CommentServerBase> commentServer_;
	std::unique_ptr<QBuffer>           internal_buffer_;
	QColor                             address_color_           = Qt::red; // color of the address in display
	QColor                             even_word_               = Qt::blue;
	QColor                             non_printable_text_      = Qt::red;
	QIODevice*                         data_                   = nullptr;
	address_t                          address_offset_          = 0;       // this is the offset that our base address is relative to
	address_t                          origin_                  = 0;
	address_t                          cold_zone_end_           = 0;       // base_address - cold_zone_end_ will be displayed as gray
	bool                               user_can_set_word_width_ = true;
	bool                               user_can_set_row_width_  = true;
	bool                               show_address_            = true;    // should we show the address display?
	bool                               show_ascii_              = true;    // should we show the ascii display?
	bool                               show_comments_           = true;
	bool                               show_hex_                = true;    // should we show the hex display?
	bool                               show_address_separator_  = true;    // should we show ':' character in address to separate high/low portions
	bool                               show_vertline1_          = true;
	bool                               show_vertline2_          = true;
	bool                               show_vertline3_          = true;
	char                               unprintable_char_        = '.';
	int                                font_height_             = 0;       // height of a character in this font
	int                                font_width_              = 0;       // width of a character in this font
	int                                row_width_               = 16;      // amount of 'words' per row
	int                                word_width_              = 1;       // size of a 'word' in bytes
	int64_t                            selection_end_           = -1;      // index of last selected word (or -1)
	int64_t                            selection_start_         = -1;      // index of first selected word (or -1)

	
	enum {
		Highlighting_None,
		Highlighting_Data,
		Highlighting_Ascii
	} highlighting_ = Highlighting_None;
	
	AddressSize address_size_;
};

#endif
