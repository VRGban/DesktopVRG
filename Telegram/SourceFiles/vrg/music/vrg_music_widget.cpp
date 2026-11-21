/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#include "vrg/music/vrg_music_widget.h"
#include <QtGui/QIconEngine>
#include "vrg/vrg_open_settings.h"

#include "vrg/vrg_conf.h"
#include "vrg/music/vrg_music_player.h"
#include "vrg/music/vrg_music_section.h"
#include "vrg/music/vrg_soundcloud_client.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"
#include "api/api_sending.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "base/weak_ptr.h"
#include "data/data_thread.h"
#include "main/main_session.h"
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <vector>
#include <QtCore/QAbstractListModel>
#include <QtCore/QRegularExpression>
#include <QtCore/QElapsedTimer>
#include <QtCore/QCoreApplication>
#include <QtWidgets/QFileDialog>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QProcess>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QScreen>
#include <QtGui/QCursor>
#include <QtGui/QFontMetrics>
#include <QtGui/QLinearGradient>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListView>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSlider>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QStyle>
#include <QtWidgets/QStyledItemDelegate>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMenu>
#include <QtGui/QShortcut>
#include <algorithm>
#include <cmath>
#include <functional>

namespace Vrg::Music {
namespace {

[[nodiscard]] bool LightUi() {
	return st::windowBg->c.lightness() >= 128;
}

[[nodiscard]] QColor LightVariant(const QColor &c) {
	if (c.hsvSaturation() > 70) {
		return c;
	}
	const auto clampv = [](int v) {
		return std::clamp(v, 0, 255);
	};
	const auto lightness = c.lightness();
	if (lightness <= 90) {
		return QColor(
			clampv(int(255 - c.red() * 1.4)),
			clampv(int(255 - c.green() * 1.4)),
			clampv(int(255 - c.blue() * 1.4)),
			c.alpha());
	} else if (lightness >= 200) {
		return QColor(
			clampv(0x11 + (255 - c.red())),
			clampv(0x11 + (255 - c.green())),
			clampv(0x11 + (255 - c.blue())),
			c.alpha());
	}
	return c;
}

[[nodiscard]] QColor Th(const QColor &c) {
	return LightUi() ? LightVariant(c) : c;
}

[[nodiscard]] QColor AccentBg(const QColor &a) {
	if (!LightUi()) {
		return QColor(a.red() / 2, a.green() / 2, a.blue() / 2);
	}
	return QColor(
		(a.red() * 22 + 255 * 78) / 100,
		(a.green() * 22 + 255 * 78) / 100,
		(a.blue() * 22 + 255 * 78) / 100);
}

[[nodiscard]] QColor AccentStart(const QColor &a) {
	if (!LightUi()) {
		return a;
	}
	return QColor(
		(a.red() * 40 + 255 * 60) / 100,
		(a.green() * 40 + 255 * 60) / 100,
		(a.blue() * 40 + 255 * 60) / 100);
}

[[nodiscard]] QColor ThQ(int r, int g, int b, int a = 255) {
	return Th(QColor(r, g, b, a));
}

[[nodiscard]] QString Themed(QString qss) {
	if (!LightUi()) {
		return qss;
	}
	static const auto hex = QRegularExpression(
		QStringLiteral("#([0-9a-fA-F]{6})\\b"));
	static const auto rgba = QRegularExpression(QStringLiteral(
		"rgba\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\)"));
	qss.replace(QStringLiteral("#fff;"), QStringLiteral("#ffffff;"));
	qss.replace(QStringLiteral(": white"), QStringLiteral(": #ffffff"));
	qss.replace(QStringLiteral(": black"), QStringLiteral(": #000000"));
	auto result = QString();
	auto last = 0;
	auto it = hex.globalMatch(qss);
	while (it.hasNext()) {
		const auto m = it.next();
		result += qss.mid(last, m.capturedStart() - last);
		result += LightVariant(QColor(m.captured(0))).name(QColor::HexRgb);
		last = m.capturedEnd();
	}
	result += qss.mid(last);
	qss = result;
	result.clear();
	last = 0;
	auto it2 = rgba.globalMatch(qss);
	while (it2.hasNext()) {
		const auto m = it2.next();
		result += qss.mid(last, m.capturedStart() - last);
		const auto c = LightVariant(QColor(
			m.captured(1).toInt(),
			m.captured(2).toInt(),
			m.captured(3).toInt(),
			m.captured(4).toInt()));
		result += QStringLiteral("rgba(%1,%2,%3,%4)")
			.arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
		last = m.capturedEnd();
	}
	result += qss.mid(last);
	return result;
}

#define kBackground ThQ(0x00, 0x00, 0x00)
#define kCard ThQ(0x11, 0x11, 0x13)
#define kActive ThQ(0x1c, 0x1c, 0x1f)
#define kText ThQ(0xff, 0xff, 0xff)
#define kTextMuted ThQ(0x8a, 0x8a, 0x8a)

bool IsMusicWidget(const QWidget *w) {
	for (auto p = w; p; p = p->parentWidget()) {
		if (p->property("vrgMusicRoot").toBool()
			|| p->objectName() == QStringLiteral("vrgMusic")) {
			return true;
		}
	}
	return false;
}

class ThemeFilter final : public QObject {
public:
	static ThemeFilter *Instance() {
		static auto *instance = new ThemeFilter();
		return instance;
	}

	void track(QWidget *w, const QString &qss) {
		w->setProperty("vrgOrig", qss);
		w->setProperty("vrgApplied", qss);
		for (const auto &x : _tracked) {
			if (x.data() == w) {
				return;
			}
		}
		_tracked.push_back(QPointer<QWidget>(w));
	}

	void addRoot(QWidget *root) {
		root->setProperty("vrgMusicRoot", true);
		_roots.push_back(QPointer<QWidget>(root));
	}

	bool eventFilter(QObject *obj, QEvent *e) override {
		if (e->type() != QEvent::StyleChange) {
			return false;
		}
		const auto w = qobject_cast<QWidget*>(obj);
		if (!w || !IsMusicWidget(w)) {
			return false;
		}
		const auto current = w->styleSheet();
		if (current.isEmpty()
			|| current == w->property("vrgApplied").toString()) {
			return false;
		}
		const auto themed = Themed(current);
		w->setProperty("vrgOrig", current);
		w->setProperty("vrgApplied", themed);
		_tracked.push_back(QPointer<QWidget>(w));
		if (themed != current) {
			w->setStyleSheet(themed);
		}
		return false;
	}

	void reapply() {
		for (auto i = _tracked.begin(); i != _tracked.end();) {
			const auto w = i->data();
			if (!w) {
				i = _tracked.erase(i);
				continue;
			}
			const auto themed = Themed(w->property("vrgOrig").toString());
			w->setProperty("vrgApplied", themed);
			w->setStyleSheet(themed);
			++i;
		}
		for (auto i = _roots.begin(); i != _roots.end();) {
			const auto root = i->data();
			if (!root) {
				i = _roots.erase(i);
				continue;
			}
			for (const auto child : root->findChildren<QWidget*>()) {
				child->update();
			}
			root->update();
			++i;
		}
	}

private:
	std::vector<QPointer<QWidget>> _tracked;
	std::vector<QPointer<QWidget>> _roots;

};

void EnsureThemeHook(QWidget *root) {
	static auto installed = false;
	if (!installed) {
		installed = true;
		qApp->installEventFilter(ThemeFilter::Instance());
		static rpl::lifetime lifetime;
		style::PaletteChanged(
		) | rpl::on_next([] {
			ThemeFilter::Instance()->reapply();
		}, lifetime);
	}
	ThemeFilter::Instance()->addRoot(root);
}

void SetQss(QWidget *w, const QString &qss) {
	if (!w) {
		return;
	}
	ThemeFilter::Instance()->track(w, qss);
	const auto themed = Themed(qss);
	w->setProperty("vrgApplied", themed);
	w->setStyleSheet(themed);
}

constexpr auto kRowHeight = 60;
constexpr auto kArtworkSize = 44;
constexpr auto kBarArtworkSize = 44;
constexpr auto kRowButtonSize = 28;
constexpr auto kTrackRole = Qt::UserRole + 1;

enum class Glyph {
	Play,
	Pause,
	Prev,
	Next,
	Shuffle,
	Repeat,
	RepeatOne,
	Volume,
	VolumeMuted,
	Plus,
	Check,
	Note,
	Download,
	PlaylistAdd,
	Trash,
	Heart,
	HeartFilled,
	Lyrics,
	Eq,
	Queue,
};

[[nodiscard]] QString FormatTime(qint64 ms) {
	const auto total = std::max(ms, qint64(0)) / 1000;
	const auto hours = total / 3600;
	const auto minutes = (total % 3600) / 60;
	const auto seconds = total % 60;
	return hours
		? QStringLiteral("%1:%2:%3")
			.arg(hours)
			.arg(minutes, 2, 10, QChar('0'))
			.arg(seconds, 2, 10, QChar('0'))
		: QStringLiteral("%1:%2")
			.arg(minutes)
			.arg(seconds, 2, 10, QChar('0'));
}

void PaintGlyph(QPainter &p, Glyph glyph, const QRectF &rect, QColor color) {
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.translate(rect.topLeft());
	p.scale(rect.width() / 24., rect.height() / 24.);

	auto stroke = QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	const auto fillPath = [&](const QPainterPath &path) {
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawPath(path);
	};
	const auto strokePath = [&](const QPainterPath &path) {
		p.setPen(stroke);
		p.setBrush(Qt::NoBrush);
		p.drawPath(path);
	};

	switch (glyph) {
	case Glyph::Play: {
		QPainterPath path;
		path.moveTo(7.5, 5.2);
		path.lineTo(19.2, 12.0);
		path.lineTo(7.5, 18.8);
		path.closeSubpath();
		fillPath(path);
	} break;
	case Glyph::Pause: {
		QPainterPath path;
		path.addRoundedRect(QRectF(7., 5.5, 3.2, 13.), 1.5, 1.5);
		path.addRoundedRect(QRectF(13.8, 5.5, 3.2, 13.), 1.5, 1.5);
		fillPath(path);
	} break;
	case Glyph::Prev: {
		QPainterPath path;
		path.moveTo(14.5, 6.2);
		path.lineTo(8.5, 12.0);
		path.lineTo(14.5, 17.8);
		p.setPen(QPen(color, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		p.drawPath(path);
	} break;
	case Glyph::Next: {
		QPainterPath path;
		path.moveTo(9.5, 6.2);
		path.lineTo(15.5, 12.0);
		path.lineTo(9.5, 17.8);
		p.setPen(QPen(color, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		p.drawPath(path);
	} break;
	case Glyph::Shuffle: {
		QPainterPath path;
		path.moveTo(3.5, 7.);
		path.lineTo(7.5, 7.);
		path.cubicTo(12., 7., 12., 17., 16.5, 17.);
		path.lineTo(20.5, 17.);
		path.moveTo(3.5, 17.);
		path.lineTo(7.5, 17.);
		path.cubicTo(9.4, 17., 10.4, 15.6, 11.2, 14.);
		path.moveTo(12.8, 10.);
		path.cubicTo(13.6, 8.4, 14.6, 7., 16.5, 7.);
		path.lineTo(20.5, 7.);
		path.moveTo(18., 4.5);
		path.lineTo(20.5, 7.);
		path.lineTo(18., 9.5);
		path.moveTo(18., 14.5);
		path.lineTo(20.5, 17.);
		path.lineTo(18., 19.5);
		strokePath(path);
	} break;
	case Glyph::Repeat:
	case Glyph::RepeatOne: {
		QPainterPath path;
		path.moveTo(4.5, 11.5);
		path.lineTo(4.5, 9.5);
		path.quadTo(4.5, 6.8, 7.2, 6.8);
		path.lineTo(19., 6.8);
		path.moveTo(16.8, 4.5);
		path.lineTo(19.2, 6.8);
		path.lineTo(16.8, 9.1);
		path.moveTo(19.5, 12.5);
		path.lineTo(19.5, 14.5);
		path.quadTo(19.5, 17.2, 16.8, 17.2);
		path.lineTo(5., 17.2);
		path.moveTo(7.2, 14.9);
		path.lineTo(4.8, 17.2);
		path.lineTo(7.2, 19.5);
		strokePath(path);
		if (glyph == Glyph::RepeatOne) {
			auto font = QFont();
			font.setPixelSize(7);
			font.setBold(true);
			p.setFont(font);
			p.setPen(color);
			p.drawText(QRectF(8., 8., 8., 8.), Qt::AlignCenter, QStringLiteral("1"));
		}
	} break;
	case Glyph::Volume:
	case Glyph::VolumeMuted: {
		QPainterPath speaker;
		speaker.moveTo(4., 9.5);
		speaker.lineTo(7.5, 9.5);
		speaker.lineTo(12., 5.8);
		speaker.lineTo(12., 18.2);
		speaker.lineTo(7.5, 14.5);
		speaker.lineTo(4., 14.5);
		speaker.closeSubpath();
		strokePath(speaker);
		QPainterPath waves;
		if (glyph == Glyph::Volume) {
			waves.moveTo(15.2, 9.2);
			waves.quadTo(17.2, 12., 15.2, 14.8);
			waves.moveTo(17.8, 6.8);
			waves.quadTo(21.2, 12., 17.8, 17.2);
		} else {
			waves.moveTo(15.8, 9.5);
			waves.lineTo(20.2, 14.5);
			waves.moveTo(20.2, 9.5);
			waves.lineTo(15.8, 14.5);
		}
		strokePath(waves);
	} break;
	case Glyph::Plus: {
		QPainterPath path;
		path.moveTo(12., 5.5);
		path.lineTo(12., 18.5);
		path.moveTo(5.5, 12.);
		path.lineTo(18.5, 12.);
		strokePath(path);
	} break;
	case Glyph::Check: {
		QPainterPath path;
		path.moveTo(5.5, 12.5);
		path.lineTo(10., 17.);
		path.lineTo(18.5, 7.5);
		strokePath(path);
	} break;
	case Glyph::Note: {
		QPainterPath path;
		path.addEllipse(QRectF(5.5, 14.5, 5., 4.));
		path.addEllipse(QRectF(13.5, 12.5, 5., 4.));
		fillPath(path);
		QPainterPath stems;
		stems.moveTo(10.2, 16.5);
		stems.lineTo(10.2, 6.2);
		stems.lineTo(18.2, 4.8);
		stems.lineTo(18.2, 14.5);
		strokePath(stems);
	} break;
	case Glyph::Download: {
		QPainterPath path;
		path.moveTo(12., 4.5);
		path.lineTo(12., 15.);
		path.moveTo(8., 11.);
		path.lineTo(12., 15.);
		path.lineTo(16., 11.);
		path.moveTo(5.5, 18.5);
		path.lineTo(18.5, 18.5);
		strokePath(path);
	} break;
	case Glyph::PlaylistAdd: {
		QPainterPath path;
		path.moveTo(4.5, 7.);
		path.lineTo(16., 7.);
		path.moveTo(4.5, 12.);
		path.lineTo(16., 12.);
		path.moveTo(4.5, 17.);
		path.lineTo(11.5, 17.);
		path.moveTo(17., 14.);
		path.lineTo(17., 20.);
		path.moveTo(14., 17.);
		path.lineTo(20., 17.);
		strokePath(path);
	} break;
	case Glyph::Trash: {
		QPainterPath path;
		path.moveTo(7., 7.);
		path.lineTo(17., 17.);
		path.moveTo(17., 7.);
		path.lineTo(7., 17.);
		strokePath(path);
	} break;
	case Glyph::Heart:
	case Glyph::HeartFilled: {
		QPainterPath path;
		path.moveTo(12., 19.2);
		path.cubicTo(4.5, 14.2, 3.8, 9.2, 6.2, 6.5);
		path.cubicTo(8.0, 4.5, 10.8, 5.2, 12., 7.4);
		path.cubicTo(13.2, 5.2, 16.0, 4.5, 17.8, 6.5);
		path.cubicTo(20.2, 9.2, 19.5, 14.2, 12., 19.2);
		if (glyph == Glyph::HeartFilled) {
			fillPath(path);
		} else {
			strokePath(path);
		}
	} break;
	case Glyph::Lyrics: {
		QPainterPath path;
		path.addRoundedRect(QRectF(4.5, 4.5, 15., 15.), 3., 3.);
		path.moveTo(8., 9.);
		path.lineTo(16., 9.);
		path.moveTo(8., 12.5);
		path.lineTo(14., 12.5);
		path.moveTo(8., 16.);
		path.lineTo(11.5, 16.);
		strokePath(path);
	} break;
	case Glyph::Eq: {
		QPainterPath path;
		path.moveTo(6., 5.);
		path.lineTo(6., 19.);
		path.moveTo(12., 5.);
		path.lineTo(12., 19.);
		path.moveTo(18., 5.);
		path.lineTo(18., 19.);
		strokePath(path);
		fillPath([&] {
			QPainterPath knobs;
			knobs.addEllipse(QPointF(6., 9.), 2.2, 2.2);
			knobs.addEllipse(QPointF(12., 15.), 2.2, 2.2);
			knobs.addEllipse(QPointF(18., 10.5), 2.2, 2.2);
			return knobs;
		}());
	} break;
	case Glyph::Queue: {
		QPainterPath path;
		path.moveTo(5., 7.);
		path.lineTo(19., 7.);
		path.moveTo(5., 12.);
		path.lineTo(14., 12.);
		path.moveTo(5., 17.);
		path.lineTo(11., 17.);
		strokePath(path);
		fillPath([&] {
			QPainterPath tri;
			tri.moveTo(16., 14.);
			tri.lineTo(21., 17.);
			tri.lineTo(16., 20.);
			tri.closeSubpath();
			return tri;
		}());
	} break;
	}
	p.restore();
}

[[nodiscard]] double MusicZoomFactor() {
	return std::clamp(Vrg::Conf::Int("music_zoom", 100), 100, 200) / 100.;
}

class GlyphIconEngine final : public QIconEngine {
public:
	GlyphIconEngine(Glyph glyph, QColor color, int size)
	: _glyph(glyph)
	, _color(color)
	, _size(size) {
	}

	void paint(
			QPainter *p,
			const QRect &rect,
			QIcon::Mode mode,
			QIcon::State state) override {
		p->save();
		p->setRenderHint(QPainter::Antialiasing);
		p->setRenderHint(QPainter::SmoothPixmapTransform);
		if (mode == QIcon::Disabled) {
			p->setOpacity(p->opacity() * 0.4);
		}
		PaintGlyph(*p, _glyph, QRectF(rect), Th(_color));
		p->restore();
	}

	QPixmap pixmap(
			const QSize &size,
			QIcon::Mode mode,
			QIcon::State state) override {
		const auto ratio = std::max(qApp ? qApp->devicePixelRatio() : 1., 2.)
			* MusicZoomFactor();
		auto image = QPixmap(size * ratio);
		image.setDevicePixelRatio(ratio);
		image.fill(Qt::transparent);
		auto p = QPainter(&image);
		paint(&p, QRect(QPoint(), size), mode, state);
		p.end();
		return image;
	}

	QSize actualSize(
			const QSize &size,
			QIcon::Mode mode,
			QIcon::State state) override {
		return size;
	}

	QIconEngine *clone() const override {
		return new GlyphIconEngine(_glyph, _color, _size);
	}

private:
	Glyph _glyph;
	QColor _color;
	int _size = 20;

};

[[nodiscard]] QIcon GlyphIcon(Glyph glyph, QColor color, int size = 20) {
	return QIcon(new GlyphIconEngine(glyph, color, size));
}

[[nodiscard]] QColor ExtractDominantColor(const QPixmap &pixmap) {
	if (pixmap.isNull()) {
		return QColor(0x16, 0x22, 0x29);
	}
	const auto img = pixmap.toImage().scaled(16, 16, Qt::IgnoreAspectRatio, Qt::FastTransformation);
	long rSum = 0, gSum = 0, bSum = 0, count = 0;
	for (int y = 0; y < img.height(); ++y) {
		for (int x = 0; x < img.width(); ++x) {
			const auto c = img.pixelColor(x, y);
			const auto s = c.saturation();
			const auto v = c.value();
			if (v > 30 && v < 240) {
				const auto weight = 1 + (s / 40);
				rSum += c.red() * weight;
				gSum += c.green() * weight;
				bSum += c.blue() * weight;
				count += weight;
			}
		}
	}
	if (count == 0) {
		return QColor(0x16, 0x22, 0x29);
	}
	auto result = QColor(int(rSum / count), int(gSum / count), int(bSum / count));
	int h, s, v;
	result.getHsv(&h, &s, &v);
	return QColor::fromHsv(h, std::min(s, 180), std::clamp(v / 2, 28, 72));
}

[[nodiscard]] QPixmap RoundedArtwork(
		const QPixmap &source,
		int size,
		qreal radius,
		qreal ratio) {
	auto target = QPixmap(QSize(size, size) * ratio);
	target.setDevicePixelRatio(ratio);
	target.fill(Qt::transparent);

	auto p = QPainter(&target);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);

	auto clip = QPainterPath();
	clip.addRoundedRect(QRectF(0, 0, size, size), radius, radius);
	p.setClipPath(clip);
	p.fillRect(0, 0, size, size, kActive);

	if (source.isNull()) {
		const auto pad = size * 0.25;
		PaintGlyph(
			p,
			Glyph::Note,
			QRectF(pad, pad, size - 2 * pad, size - 2 * pad),
			kTextMuted);
	} else {
		const auto scaled = source.scaled(
			QSize(size, size) * ratio,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
		const auto x = (scaled.width() - int(size * ratio)) / 2;
		const auto y = (scaled.height() - int(size * ratio)) / 2;
		p.drawPixmap(
			QRect(0, 0, size, size),
			scaled,
			QRect(x, y, int(size * ratio), int(size * ratio)));
	}
	return target;
}

[[nodiscard]] QPixmap CreateBlurredBackground(const QPixmap &source) {
	if (source.isNull()) {
		return {};
	}
	constexpr int kW = 64;
	constexpr int kH = 64;
	QImage img = source.toImage().convertToFormat(QImage::Format_ARGB32).scaled(
		kW, kH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	QImage tmp(kW, kH, QImage::Format_ARGB32);
	for (int pass = 0; pass < 3; ++pass) {
		for (int y = 0; y < kH; ++y) {
			for (int x = 0; x < kW; ++x) {
				int r = 0, g = 0, b = 0, cnt = 0;
				for (int dx = -2; dx <= 2; ++dx) {
					const int nx = std::clamp(x + dx, 0, kW - 1);
					const auto c = img.pixelColor(nx, y);
					r += c.red();
					g += c.green();
					b += c.blue();
					++cnt;
				}
				tmp.setPixelColor(x, y, QColor(r / cnt, g / cnt, b / cnt));
			}
		}
		for (int y = 0; y < kH; ++y) {
			for (int x = 0; x < kW; ++x) {
				int r = 0, g = 0, b = 0, cnt = 0;
				for (int dy = -2; dy <= 2; ++dy) {
					const int ny = std::clamp(y + dy, 0, kH - 1);
					const auto c = tmp.pixelColor(x, ny);
					r += c.red();
					g += c.green();
					b += c.blue();
					++cnt;
				}
				img.setPixelColor(x, y, QColor(r / cnt, g / cnt, b / cnt));
			}
		}
	}
	return QPixmap::fromImage(img);
}

class LyricsPageWidget final : public QWidget {
public:
	explicit LyricsPageWidget(QWidget *parent = nullptr)
	: QWidget(parent) {
		setAttribute(Qt::WA_TranslucentBackground);
	}
};

}

class SmoothLyricsWidget final : public QWidget {
	Q_OBJECT

public:
	struct Line {
		qint64 timeMs = 0;
		QString text;
	};

	explicit SmoothLyricsWidget(QWidget *parent = nullptr)
	: QWidget(parent) {
		setCursor(Qt::PointingHandCursor);
		_animTimer.setInterval(16);
		connect(&_animTimer, &QTimer::timeout, this, &SmoothLyricsWidget::stepAnimation);
	}

	void setStatusText(const QString &text) {
		_lines.clear();
		rebuildLayout();
		_statusText = text;
		_synced = false;
		_activeIdx = -1;
		_currentScroll = 0.0;
		_targetScroll = 0.0;
		_animTimer.stop();
		update();
	}

	void setPlainLyrics(const QString &plain) {
		_lines.clear();
		_statusText.clear();
		_synced = false;
		_activeIdx = -1;
		for (const auto &l : plain.split(QLatin1Char('\n'))) {
			const auto t = l.trimmed();
		if (!t.isEmpty()) {
				_lines.push_back({ 0, t });
			}
		}
		rebuildLayout();
		_currentScroll = 0.0;
		_targetScroll = 0.0;
		_animTimer.stop();
		update();
	}

	void setSyncedLyrics(const QVector<Line> &lines) {
		_lines = lines;
		rebuildLayout();
		_statusText.clear();
		_synced = true;
		_activeIdx = -1;
		_currentScroll = 0.0;
		_targetScroll = 0.0;
		update();
	}

	void setPosition(qint64 positionMs) {
		if (!_synced || _lines.isEmpty()) {
			return;
		}
		int idx = -1;
		for (int i = 0; i < int(_lines.size()); ++i) {
			if (_lines[i].timeMs <= positionMs) {
				idx = i;
			} else {
				break;
			}
		}
		if (idx != _activeIdx) {
			_activeIdx = idx;
			recomputeTargetScroll();
			startAnimation();
		}
	}

Q_SIGNALS:
	void seekRequested(qint64 timeMs);

protected:
	void resizeEvent(QResizeEvent *e) override {
		QWidget::resizeEvent(e);
		rebuildLayout();
		recomputeTargetScroll();
		_currentScroll = _targetScroll;
		update();
	}

	void wheelEvent(QWheelEvent *e) override {
		_targetScroll = std::clamp(
			_targetScroll - e->angleDelta().y() * 0.6,
			0.0,
			std::max(_totalH, 0.0));
		_currentScroll = _targetScroll;
		update();
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		if (e->button() != Qt::LeftButton || !_synced || _lines.isEmpty()) {
			return;
		}
		const auto clickY = e->position().y() - baseY() + _currentScroll;
		for (int i = 0; i < int(_lines.size()) && i < _rowTop.size(); ++i) {
			if (clickY >= _rowTop[i] && clickY < _rowTop[i] + _rowH[i]) {
				Q_EMIT seekRequested(_lines[i].timeMs);
				break;
			}
		}
	}

	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::TextAntialiasing);

		if (!_statusText.isEmpty()) {
			auto f = font();
			f.setPixelSize(18);
			p.setFont(f);
			p.setPen(ThQ(255, 255, 255, 150));
			p.drawText(rect(), Qt::AlignCenter, _statusText);
			return;
		}
		if (_lines.isEmpty()) {
			return;
		}

		if (_rowTop.size() != _lines.size()) {
			rebuildLayout();
		}
		const auto baseline = baseY();
		const auto maxTextW = _textW;
		const auto leftX = (width() - maxTextW) / 2;

		for (int i = 0; i < int(_lines.size()); ++i) {
			const auto rowH = _rowH[i];
			const auto y = baseline + _rowTop[i] - _currentScroll;
			if (y < -rowH || y > height() + rowH) {
				continue;
			}
			const bool isActive = _synced && (i == _activeIdx);
			const int dist = _synced ? std::abs(i - _activeIdx) : 0;
			int alpha = 230;
			if (_synced) {
				if (isActive) {
					alpha = 255;
				} else if (dist == 1) {
					alpha = 135;
				} else if (dist == 2) {
					alpha = 90;
				} else {
					alpha = 55;
				}
			}
			auto f = font();
			f.setPixelSize(qRound((isActive ? 24 : 20) * _scale));
			f.setWeight(isActive ? QFont::Bold : QFont::DemiBold);
			p.setFont(f);
			p.setPen(ThQ(255, 255, 255, alpha));
			p.drawText(
				QRectF(leftX, y, maxTextW, rowH),
				Qt::AlignCenter | Qt::TextWordWrap,
				_lines[i].text);
		}
	}

private:
	[[nodiscard]] double baseY() const {
		return _synced ? height() * 0.5 : 40.0;
	}

	void rebuildLayout() {
		_scale = std::clamp(width() / 1100., 0.7, 1.35);
		_textW = std::max(std::min(width() - 64, qRound(760 * _scale)), 80);
		_rowTop.resize(_lines.size());
		_rowH.resize(_lines.size());
		auto f = font();
		f.setPixelSize(qRound(24 * _scale));
		f.setWeight(QFont::Bold);
		const auto metrics = QFontMetricsF(f);
		const auto gap = 22. * _scale;
		auto y = 0.;
		for (int i = 0; i < int(_lines.size()); ++i) {
			const auto box = metrics.boundingRect(
				QRectF(0, 0, _textW, 100000),
				Qt::AlignCenter | Qt::TextWordWrap,
				_lines[i].text);
			const auto h = std::max(box.height(), metrics.height()) + gap;
			_rowTop[i] = y;
			_rowH[i] = h;
			y += h;
		}
		_totalH = y;
	}

	void recomputeTargetScroll() {
		if (_activeIdx < 0 || _activeIdx >= _rowTop.size()) {
			_targetScroll = 0.0;
			return;
		}
		_targetScroll = _rowTop[_activeIdx] + _rowH[_activeIdx] / 2;
	}

	void startAnimation() {
		if (_animTimer.isActive()) {
			return;
		}
		const auto hz = window() && window()->screen()
			? window()->screen()->refreshRate()
			: 60.;
		_animTimer.setInterval(std::clamp(
			int(std::floor(1000. / std::max(hz, 30.))),
			2,
			16));
		_frameClock.start();
		_animTimer.start();
	}

	void stepAnimation() {
		const auto dt = std::clamp(
			double(_frameClock.restart()),
			1.,
			50.);
		const auto diff = _targetScroll - _currentScroll;
		if (std::abs(diff) < 0.5) {
			_currentScroll = _targetScroll;
			_animTimer.stop();
		} else {
			const auto k = 1. - std::pow(1. - 0.28, dt / 16.667);
			_currentScroll += diff * k;
		}
		update();
	}

	QVector<Line> _lines;
	QString _statusText;
	bool _synced = false;
	int _activeIdx = -1;
	double _currentScroll = 0.0;
	double _targetScroll = 0.0;
	QTimer _animTimer;
	QElapsedTimer _frameClock;
	QVector<double> _rowTop;
	QVector<double> _rowH;
	double _totalH = 0.;
	double _scale = 1.;
	int _textW = 600;

};

namespace {

[[nodiscard]] QString StyleSheet() {
	return QStringLiteral(R"(
		#vrgMusic {
			background: #000000;
			color: #ffffff;
		}
		#vrgSearch {
			background: #111113;
			color: #ffffff;
			border: 1px solid #222226;
			border-radius: 10px;
			padding: 9px 14px;
			font-size: 14px;
			selection-background-color: #ffffff;
			selection-color: #000000;
		}
		#vrgSearch:focus {
			border-color: #3a3a42;
		}
		QPushButton#vrgTab {
			background: transparent;
			color: #8a8a8a;
			border: none;
			border-radius: 14px;
			padding: 6px 14px;
			font-size: 13px;
			font-weight: 600;
		}
		QPushButton#vrgTab:hover {
			color: #ffffff;
		}
		QPushButton#vrgTab:checked {
			background: #1c1c1f;
			color: #ffffff;
		}
		#vrgList {
			background: #000000;
			border: none;
			outline: none;
		}
		#vrgList QScrollBar:vertical {
			background: transparent;
			width: 5px;
			margin: 4px 2px 4px 0;
		}
		#vrgList QScrollBar::handle:vertical {
			background: #2a2a2e;
			border-radius: 2px;
			min-height: 24px;
		}
		#vrgList QScrollBar::handle:vertical:hover {
			background: #44444a;
		}
		#vrgList QScrollBar::add-line:vertical,
		#vrgList QScrollBar::sub-line:vertical,
		#vrgList QScrollBar::add-page:vertical,
		#vrgList QScrollBar::sub-page:vertical {
			background: transparent;
			height: 0;
		}
		#vrgStatus {
			color: #8a8a8a;
			font-size: 14px;
			padding: 24px;
		}
		#vrgError {
			background: #1a0f10;
			color: #f28b82;
			border: 1px solid #3c1e20;
			border-radius: 8px;
			padding: 7px 12px;
			font-size: 13px;
		}
		QToolButton#vrgCtrl {
			background: transparent;
			border: none;
			border-radius: 16px;
			padding: 5px;
		}
		QToolButton#vrgCtrl:hover {
			background: rgba(255, 255, 255, 26);
		}
		QToolButton#vrgPlay {
			background: #ffffff;
			border: none;
			border-radius: 18px;
		}
		QToolButton#vrgPlay:hover {
			background: #e6e6e6;
		}
		QToolButton#vrgPlay:disabled {
			background: #2a2a2e;
		}
		QLabel#vrgTime {
			color: rgba(255, 255, 255, 160);
			font-size: 11px;
		}
	)");
}

class CreatePlaylistBox final : public Ui::BoxContent {
public:
	CreatePlaylistBox(QWidget*, Fn<void(QString)> onCreated)
	: _onCreated(std::move(onCreated)) {
	}

protected:
	void prepare() override {
		setTitle(rpl::single(u"Новый плейлист"_q));
		_input = Ui::CreateChild<Ui::InputField>(
			this,
			st::defaultInputField,
			rpl::single(u"Название плейлиста"_q));
		_input->resizeToWidth(st::boxWidth - 2 * st::boxPadding.left());
		setDimensions(st::boxWidth, st::boxPadding.top() + _input->height() + st::boxPadding.bottom() + 16);

		const auto submit = [=] {
			const auto name = _input->getLastText().trimmed();
			if (name.isEmpty()) {
				_input->showError();
				return;
			}
			const auto cb = _onCreated;
			closeBox();
			if (cb) {
				cb(name);
			}
		};
		_input->submits() | rpl::on_next(submit, lifetime());
		addButton(rpl::single(u"Создать"_q), submit);
		addButton(rpl::single(u"Отмена"_q), [=] { closeBox(); });
	}

	void resizeEvent(QResizeEvent *e) override {
		BoxContent::resizeEvent(e);
		if (_input) {
			_input->moveToLeft(st::boxPadding.left(), st::boxPadding.top());
		}
	}

	void setInnerFocus() override {
		if (_input) {
			_input->setFocusFast();
		}
	}

private:
	Fn<void(QString)> _onCreated;
	Ui::InputField *_input = nullptr;

};

class EqualizerBox final : public Ui::BoxContent {
public:
	EqualizerBox(QWidget*, Player *player)
	: _player(player) {
	}

protected:
	void prepare() override {
		setTitle(rpl::single(u"Эквалайзер и усиление"_q));
		const auto content = Ui::CreateChild<QWidget>(this);
		SetQss(content, QStringLiteral(
			"background: #000000; border: none;"));
		const auto root = new QVBoxLayout(content);
		root->setContentsMargins(20, 14, 20, 14);
		root->setSpacing(14);

		auto *enableBtn = new QPushButton(
			_player->eqEnabled() ? QStringLiteral("Эквалайзер: ВКЛ") : QStringLiteral("Эквалайзер: ВЫКЛ"),
			content);
		enableBtn->setCursor(Qt::PointingHandCursor);
		SetQss(enableBtn, QStringLiteral(
			"QPushButton { background: #111113; color: #ffffff; border: none; "
			"border-radius: 10px; padding: 9px 14px; font-weight: 600; font-size: 13px; }"
			"QPushButton:hover { background: #1c1c1f; }"));
		root->addWidget(enableBtn);

		const auto presetsLayout = new QHBoxLayout();
		presetsLayout->setSpacing(6);
		const auto &presets = DefaultEqPresets();
		std::vector<QSlider*> bandSliders;
		std::vector<QLabel*> bandLabels;

		const auto bandsLayout = new QHBoxLayout();
		bandsLayout->setSpacing(12);
		static constexpr const char* kFreqNames[5] = {
			"60 Hz", "230 Hz", "910 Hz", "3.6 kHz", "14 kHz"
		};
		const auto gains = _player->eqGains();
		for (int i = 0; i < 5; ++i) {
			const auto col = new QVBoxLayout();
			col->setSpacing(6);
			auto *valLabel = new QLabel(
				(gains[i] > 0 ? QStringLiteral("+") : QString())
				+ QString::number(int(std::round(gains[i]))) + QStringLiteral(" dB"),
				content);
			valLabel->setAlignment(Qt::AlignCenter);
			SetQss(valLabel, QStringLiteral("color: #ffffff; font-size: 11px; border: none;"));

			auto *slider = new QSlider(Qt::Vertical, content);
			slider->setRange(-12, 12);
			slider->setValue(int(std::round(gains[i])));
			slider->setMinimumHeight(130);
			slider->setCursor(Qt::PointingHandCursor);
			SetQss(slider, QStringLiteral(
				"QSlider { border: none; background: transparent; }"
				"QSlider::groove:vertical { width: 4px; background: #26262a; border-radius: 2px; }"
				"QSlider::handle:vertical { height: 14px; width: 14px; margin: 0 -5px; border-radius: 7px; background: #ffffff; }"));

			auto *freqLabel = new QLabel(QString::fromLatin1(kFreqNames[i]), content);
			freqLabel->setAlignment(Qt::AlignCenter);
			SetQss(freqLabel, QStringLiteral("color: #8a8a8a; font-size: 11px; border: none;"));

			col->addWidget(valLabel);
			col->addWidget(slider, 0, Qt::AlignHCenter);
			col->addWidget(freqLabel);
			bandsLayout->addLayout(col);

			bandSliders.push_back(slider);
			bandLabels.push_back(valLabel);

			QObject::connect(slider, &QSlider::valueChanged, content, [this, i, valLabel](int v) {
				valLabel->setText(
					(v > 0 ? QStringLiteral("+") : QString())
					+ QString::number(v) + QStringLiteral(" dB"));
				_player->setEqBand(i, float(v));
			});
		}

		for (const auto &preset : presets) {
			auto *btn = new QPushButton(preset.name, content);
			btn->setCursor(Qt::PointingHandCursor);
			SetQss(btn, QStringLiteral(
				"QPushButton { background: #111113; color: #bbbbbb; border: none; "
				"border-radius: 8px; padding: 6px 8px; font-size: 11px; }"
				"QPushButton:hover { background: #1c1c1f; color: #ffffff; }"));
			QObject::connect(btn, &QPushButton::clicked, content, [=, this] {
				_player->setEqGains(preset.gainsDb);
				if (!_player->eqEnabled()) {
					_player->setEqEnabled(true);
					enableBtn->setText(QStringLiteral("Эквалайзер: ВКЛ"));
				}
				for (int i = 0; i < 5; ++i) {
					const QSignalBlocker blocker(bandSliders[i]);
					bandSliders[i]->setValue(int(std::round(preset.gainsDb[i])));
					bandLabels[i]->setText(
						(preset.gainsDb[i] > 0 ? QStringLiteral("+") : QString())
						+ QString::number(int(std::round(preset.gainsDb[i]))) + QStringLiteral(" dB"));
				}
			});
			presetsLayout->addWidget(btn);
		}

		QObject::connect(enableBtn, &QPushButton::clicked, content, [this, enableBtn] {
			const auto next = !_player->eqEnabled();
			_player->setEqEnabled(next);
			enableBtn->setText(next
				? QStringLiteral("Эквалайзер: ВКЛ")
				: QStringLiteral("Эквалайзер: ВЫКЛ"));
		});

		root->addLayout(presetsLayout);
		root->addLayout(bandsLayout);

		const auto boostHeader = new QHBoxLayout();
		auto *boostTitle = new QLabel(QStringLiteral("Усиление звука (Boost)"), content);
		SetQss(boostTitle, QStringLiteral("color: #ffffff; font-size: 13px; font-weight: 600; border: none;"));
		auto *boostPct = new QLabel(
			QStringLiteral("%1%").arg(int(std::round(_player->boost() * 100.f))),
			content);
		SetQss(boostPct, QStringLiteral("color: #ffffff; font-size: 13px; font-weight: 700; border: none;"));
		boostHeader->addWidget(boostTitle, 1);
		boostHeader->addWidget(boostPct);
		root->addLayout(boostHeader);

		auto *boostSlider = new QSlider(Qt::Horizontal, content);
		boostSlider->setRange(100, 1000);
		boostSlider->setValue(int(std::round(_player->boost() * 100.f)));
		boostSlider->setCursor(Qt::PointingHandCursor);
		SetQss(boostSlider, QStringLiteral(
			"QSlider { border: none; background: transparent; }"
			"QSlider::groove:horizontal { height: 5px; background: #26262a; border-radius: 2px; }"
			"QSlider::sub-page:horizontal { background: #ffffff; border-radius: 2px; }"
			"QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; background: #ffffff; }"));
		QObject::connect(boostSlider, &QSlider::valueChanged, content, [this, boostPct](int v) {
			boostPct->setText(QStringLiteral("%1%").arg(v));
			_player->setBoost(v / 100.f);
		});
		root->addWidget(boostSlider);

		_content = content;
		content->resize(400, 350);
		setDimensions(400, 350);
		addButton(rpl::single(u"Готово"_q), [=] { closeBox(); });
	}

	void resizeEvent(QResizeEvent *e) override {
		BoxContent::resizeEvent(e);
		if (_content) {
			_content->setGeometry(0, 0, width(), height());
		}
	}

private:
	Player * const _player;
	QWidget *_content = nullptr;

};

}

class SmoothSlider final : public QWidget {
	Q_OBJECT

public:
	explicit SmoothSlider(QWidget *parent = nullptr)
	: QWidget(parent) {
		setMouseTracking(true);
		setCursor(Qt::PointingHandCursor);
		setFixedHeight(16);
	}

	void setRange(qint64 min, qint64 max) {
		_min = min;
		_max = std::max(min, max);
		_value = std::clamp(_value, _min, _max);
		update();
	}

	void setValue(qint64 value) {
		if (_dragging) {
			return;
		}
		const auto clamped = std::clamp(value, _min, _max);
		if (_value != clamped) {
			_value = clamped;
			update();
		}
	}

	[[nodiscard]] qint64 value() const {
		return _value;
	}

	[[nodiscard]] qint64 maximum() const {
		return _max;
	}

Q_SIGNALS:
	void valueChanged(qint64 value);
	void sliderMoved(qint64 value);
	void sliderReleased(qint64 value);

protected:
	void enterEvent(QEnterEvent *e) override {
		Q_UNUSED(e);
		_hovered = true;
		update();
	}
	void leaveEvent(QEvent *e) override {
		Q_UNUSED(e);
		_hovered = false;
		update();
	}
	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton && isEnabled() && _max > _min) {
			_dragging = true;
			updateFromPos(e->position().x());
			Q_EMIT sliderMoved(_value);
		}
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		if (_dragging && isEnabled() && _max > _min) {
			updateFromPos(e->position().x());
			Q_EMIT sliderMoved(_value);
		}
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton && _dragging) {
			_dragging = false;
			updateFromPos(e->position().x());
			Q_EMIT sliderReleased(_value);
			Q_EMIT valueChanged(_value);
			update();
		}
	}
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);

		const auto trackH = (_hovered || _dragging) ? 5.0 : 3.0;
		const auto padX = 6.0;
		const auto trackW = std::max(width() - 2.0 * padX, 1.0);
		const auto trackY = (height() - trackH) / 2.0;

		p.setPen(Qt::NoPen);
		p.setBrush(ThQ(255, 255, 255, 45));
		p.drawRoundedRect(QRectF(padX, trackY, trackW, trackH), trackH / 2.0, trackH / 2.0);

		const auto frac = (_max > _min)
			? double(_value - _min) / double(_max - _min)
			: 0.0;
		const auto fillW = trackW * std::clamp(frac, 0.0, 1.0);
		if (fillW > 0.0) {
			p.setBrush(kText);
			p.drawRoundedRect(QRectF(padX, trackY, fillW, trackH), trackH / 2.0, trackH / 2.0);
		}
		if ((_hovered || _dragging) && isEnabled() && _max > _min) {
			const auto cx = padX + fillW;
			const auto cy = height() / 2.0;
			p.setBrush(kText);
			p.drawEllipse(QPointF(cx, cy), 5.5, 5.5);
		}
	}

private:
	void updateFromPos(double x) {
		const auto padX = 6.0;
		const auto trackW = std::max(width() - 2.0 * padX, 1.0);
		const auto frac = std::clamp((x - padX) / trackW, 0.0, 1.0);
		const auto next = _min + qint64(std::round(frac * double(_max - _min)));
		if (next != _value) {
			_value = next;
			Q_EMIT valueChanged(_value);
			update();
		}
	}

	qint64 _min = 0;
	qint64 _max = 100;
	qint64 _value = 0;
	bool _hovered = false;
	bool _dragging = false;

};

[[nodiscard]] QString SourceLabel(const TrackInfo &track) {
	return track.isYoutube()
		? QStringLiteral("YouTube \u00b7 ")
		: QStringLiteral("SoundCloud \u00b7 ");
}

class MarqueeLabel final : public QWidget {
public:
	explicit MarqueeLabel(QWidget *parent = nullptr)
	: QWidget(parent) {
		_timer.setInterval(30);
		connect(&_timer, &QTimer::timeout, this, &MarqueeLabel::tick);
	}

	void setText(const QString &title, const QString &artist) {
		if (_title == title && _artist == artist) {
			return;
		}
		_title = title;
		_artist = artist;
		_offset = 0;
		_pauseTicks = kPauseTicks;
		restart();
		update();
	}

	void refreshSettings() {
		_offset = 0;
		_pauseTicks = kPauseTicks;
		restart();
		update();
	}

	void setTextScale(double scale) {
		if (std::abs(_textScale - scale) < 0.001) {
			return;
		}
		_textScale = scale;
		updateGeometry();
		update();
	}

	QSize sizeHint() const override {
		return { 160, int(std::lround(38 * _textScale)) };
	}

protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.setRenderHint(QPainter::TextAntialiasing);
		p.setClipRect(rect());

		const auto tm = QFontMetrics(titleFont());
		const auto am = QFontMetrics(artistFont());
		const auto totalH = tm.height() + 2 + am.height();
		const auto top = (height() - totalH) / 2;
		const auto enabled = Vrg::Conf::Bool("player_marquee", true);

		const auto drawLine = [&](
				const QString &text,
				const QFont &f,
				const QFontMetrics &fm,
				QColor color,
				int y) {
			p.setFont(f);
			p.setPen(color);
			const auto textW = fm.horizontalAdvance(text);
			const auto overflow = textW - width();
			if (!enabled || overflow <= 0 || width() <= 0) {
				p.drawText(
					QRect(0, y, width(), fm.height()),
					Qt::AlignLeft | Qt::AlignVCenter,
					fm.elidedText(text, Qt::ElideRight, width()));
				return;
			}
			const auto maxOff = std::max(maxOverflowWidth() - width(), 1);
			const auto shift = std::min(_offset, overflow);
			p.drawText(
				QRect(-shift, y, textW + 4, fm.height()),
				Qt::AlignLeft | Qt::AlignVCenter,
				text);
			Q_UNUSED(maxOff);
		};

		drawLine(_title, titleFont(), tm, kText, top);
		drawLine(
			_artist,
			artistFont(),
			am,
			ThQ(255, 255, 255, 165),
			top + tm.height() + 2);
	}

	void resizeEvent(QResizeEvent *e) override {
		QWidget::resizeEvent(e);
		restart();
	}

	void showEvent(QShowEvent *e) override {
		QWidget::showEvent(e);
		restart();
	}

	void hideEvent(QHideEvent *e) override {
		QWidget::hideEvent(e);
		_timer.stop();
	}

private:
	static constexpr auto kPauseTicks = 75;
	[[nodiscard]] QFont titleFont() const {
		auto f = font();
		f.setPixelSize(std::max(int(std::lround(13 * _textScale)), 6));
		f.setWeight(QFont::DemiBold);
		return f;
	}
	[[nodiscard]] QFont artistFont() const {
		auto f = font();
		f.setPixelSize(std::max(int(std::lround(11 * _textScale)), 6));
		return f;
	}
	[[nodiscard]] int maxOverflowWidth() const {
		const auto tm = QFontMetrics(titleFont());
		const auto am = QFontMetrics(artistFont());
		return std::max(tm.horizontalAdvance(_title), am.horizontalAdvance(_artist));
	}
	void restart() {
		const auto enabled = Vrg::Conf::Bool("player_marquee", true);
		if (enabled && isVisible() && width() > 0 && maxOverflowWidth() > width()) {
			if (!_timer.isActive()) {
				_timer.start();
			}
		} else {
			_timer.stop();
			_offset = 0;
			_dir = 1;
		}
	}
	void tick() {
		const auto maxShift = maxOverflowWidth() - width();
		if (!isVisible() || width() <= 0 || maxShift <= 0) {
			_timer.stop();
			return;
		}
		if (_pauseTicks > 0) {
			--_pauseTicks;
			return;
		}
		_offset += _dir;
		if (_offset >= maxShift) {
			_offset = maxShift;
			_dir = -1;
			_pauseTicks = 40;
		} else if (_offset <= 0) {
			_offset = 0;
			_dir = 1;
			_pauseTicks = kPauseTicks;
		}
		update();
	}

	double _textScale = 1.;
	QString _title;
	QString _artist;
	QTimer _timer;
	int _offset = 0;
	int _dir = 1;
	int _pauseTicks = kPauseTicks;

};

class TrackListModel final : public QAbstractListModel {
public:
	using QAbstractListModel::QAbstractListModel;

	void setTracks(const QVector<TrackInfo> &tracks) {
		beginResetModel();
		_tracks = tracks;
		endResetModel();
	}

	[[nodiscard]] const QVector<TrackInfo> &tracks() const {
		return _tracks;
	}

	[[nodiscard]] TrackInfo track(int row) const {
		return (row >= 0 && row < _tracks.size()) ? _tracks[row] : TrackInfo();
	}

	int rowCount(const QModelIndex &parent = QModelIndex()) const override {
		return parent.isValid() ? 0 : int(_tracks.size());
	}

	QVariant data(const QModelIndex &index, int role) const override {
		if (!index.isValid() || index.row() >= _tracks.size()) {
			return {};
		}
		const auto &track = _tracks[index.row()];
		switch (role) {
		case Qt::DisplayRole:
			return track.title;
		case Qt::ToolTipRole:
			return track.artist.isEmpty()
				? track.title
				: (track.artist + QStringLiteral(" — ") + track.title);
		case kTrackRole:
			return QVariant::fromValue(track);
		}
		return {};
	}

private:
	QVector<TrackInfo> _tracks;

};

class TrackDelegate final : public QStyledItemDelegate {
public:
	TrackDelegate(
			Player *player,
			SoundCloudClient *client,
			QListView *view)
	: QStyledItemDelegate(view)
	, _player(player)
	, _client(client)
	, _view(view) {
	}

	std::function<void(const QModelIndex&)> activated;
	std::function<void(const QModelIndex&)> libraryToggled;
	std::function<void(const QModelIndex&, const QPoint&)> playlistMenuRequested;
	std::function<void(const QModelIndex&)> downloadRequested;
	bool playlistRemoveMode = false;
	bool showButtons = true;

	QSize sizeHint(
			const QStyleOptionViewItem &option,
			const QModelIndex &index) const override {
		Q_UNUSED(index);
		return { option.rect.width(), kRowHeight };
	}

	void paint(
			QPainter *p,
			const QStyleOptionViewItem &option,
			const QModelIndex &index) const override {
		const auto track = index.data(kTrackRole).value<TrackInfo>();
		const auto current = (_player->currentTrack().id == track.id)
			&& _player->state() != PlaybackState::Stopped;
		const auto hovered = (option.state & QStyle::State_MouseOver);
		const auto rect = option.rect;
		const auto ratio = _view->devicePixelRatioF() * MusicZoomFactor();

		p->save();
		p->setRenderHint(QPainter::Antialiasing);
		p->setRenderHint(QPainter::TextAntialiasing);

		const auto card = QRectF(rect).adjusted(6, 2, -6, -2);
		if (current || hovered) {
			p->setPen(Qt::NoPen);
			p->setBrush(current ? kActive : kCard);
			p->drawRoundedRect(card, 10, 10);
		}

		const auto art = QRect(
			rect.left() + 14,
			rect.top() + (rect.height() - kArtworkSize) / 2,
			kArtworkSize,
			kArtworkSize);
		auto pixmap = _client->cachedArtwork(track.artworkUrl);
		if (pixmap.isNull() && !track.artworkUrl.isEmpty()) {
			_client->fetchArtwork(track.artworkUrl);
		}
		QPixmap rounded;
		if (!pixmap.isNull()) {
			const auto key = track.artworkUrl
				+ QLatin1Char('@')
				+ QString::number(qRound(ratio * 100));
			if (const auto it = _artCache.constFind(key); it != _artCache.cend()) {
				rounded = *it;
			} else {
				if (_artCache.size() > 250) {
					_artCache.clear();
				}
				rounded = RoundedArtwork(pixmap, kArtworkSize, 8, ratio);
				_artCache.insert(key, rounded);
			}
		} else {
			rounded = RoundedArtwork(pixmap, kArtworkSize, 8, ratio);
		}
		p->drawPixmap(art.topLeft(), rounded);
		if (current) {
			p->setPen(Qt::NoPen);
			p->setBrush(QColor(0, 0, 0, 140));
			p->drawRoundedRect(QRectF(art), 8, 8);
			const auto glyph = (_player->state() == PlaybackState::Playing)
				? Glyph::Pause
				: Glyph::Play;
			PaintGlyph(*p, glyph, QRectF(art).adjusted(12, 12, -12, -12), kText);
		}

		const auto cursorPos = _view->viewport()->mapFromGlobal(QCursor::pos());
		const auto button = buttonRect(rect);
		const auto plButton = playlistButtonRect(rect);
		const auto dlButton = downloadButtonRect(rect);

		const auto paintBtn = [&](const QRect &r, Glyph g, QColor fg) {
			const auto isHov = hovered && r.contains(cursorPos);
			if (isHov) {
				p->setPen(Qt::NoPen);
				p->setBrush(current ? kCard : kActive);
				p->drawEllipse(QRectF(r));
			}
			PaintGlyph(
				*p,
				g,
				QRectF(r).adjusted(5, 5, -5, -5),
				isHov ? kText : fg);
		};

		if (showButtons) {
			if (playlistRemoveMode) {
				paintBtn(button, Glyph::Trash, kTextMuted);
			} else {
				const auto inLibrary = _player->isInLibrary(track.id);
				paintBtn(button, inLibrary ? Glyph::Check : Glyph::Plus, inLibrary ? kText : kTextMuted);
			}
			paintBtn(plButton, Glyph::PlaylistAdd, kTextMuted);
			const auto cached = _player->isTrackCached(track.id);
			const auto downloading = _player->isTrackDownloading(track.id);
			paintBtn(
				dlButton,
				cached ? Glyph::Check : Glyph::Download,
				cached ? QColor(0x5b, 0xe4, 0x84) : (downloading ? kText : kTextMuted));
		}

		auto timeFont = option.font;
		timeFont.setPixelSize(12);
		const auto timeWidth = QFontMetrics(timeFont).horizontalAdvance(
			QStringLiteral("00:00:00"));
		const auto time = QRect(
			(showButtons ? dlButton.left() - 8 : rect.right() - 14) - timeWidth,
			rect.top(),
			timeWidth,
			rect.height());
		p->setFont(timeFont);
		p->setPen(kTextMuted);
		p->drawText(
			time,
			Qt::AlignRight | Qt::AlignVCenter,
			FormatTime(track.durationMs));

		const auto textLeft = art.right() + 13;
		const auto textWidth = std::max(time.left() - 12 - textLeft, 0);
		auto titleFont = option.font;
		titleFont.setPixelSize(14);
		titleFont.setWeight(QFont::DemiBold);
		auto artistFont = option.font;
		artistFont.setPixelSize(12);
		const auto titleMetrics = QFontMetrics(titleFont);
		const auto artistMetrics = QFontMetrics(artistFont);
		const auto textHeight = titleMetrics.height() + 2 + artistMetrics.height();
		const auto top = rect.top() + (rect.height() - textHeight) / 2;

		p->setFont(titleFont);
		p->setPen(kText);
		p->drawText(
			QRect(textLeft, top, textWidth, titleMetrics.height()),
			Qt::AlignLeft | Qt::AlignVCenter,
			titleMetrics.elidedText(track.title, Qt::ElideRight, textWidth));
		auto badgeFont = artistFont;
		badgeFont.setPixelSize(9);
		badgeFont.setWeight(QFont::Bold);
		const auto isYt = track.isYoutube();
		const auto badgeText = isYt ? QStringLiteral("YT") : QStringLiteral("SC");
		const auto badgeW = QFontMetrics(badgeFont).horizontalAdvance(badgeText) + 10;
		const auto badgeH = 14;
		const auto artistY = top + titleMetrics.height() + 2;
		const auto badgeRect = QRectF(
			textLeft,
			artistY + (artistMetrics.height() - badgeH) / 2.,
			badgeW,
			badgeH);
		p->setPen(Qt::NoPen);
		p->setBrush(isYt ? QColor(0xe5, 0x39, 0x35) : QColor(0xff, 0x6a, 0x00));
		p->drawRoundedRect(badgeRect, 4, 4);
		p->setFont(badgeFont);
		p->setPen(QColor(255, 255, 255));
		p->drawText(badgeRect, Qt::AlignCenter, badgeText);
		const auto artistLeft = textLeft + badgeW + 6;
		const auto artistWidth = std::max(textWidth - badgeW - 6, 0);
		p->setFont(artistFont);
		p->setPen(kTextMuted);
		p->drawText(
			QRect(
				artistLeft,
				artistY,
				artistWidth,
				artistMetrics.height()),
			Qt::AlignLeft | Qt::AlignVCenter,
			artistMetrics.elidedText(track.artist, Qt::ElideRight, artistWidth));

		p->restore();
	}

	bool editorEvent(
			QEvent *event,
			QAbstractItemModel *model,
			const QStyleOptionViewItem &option,
			const QModelIndex &index) override {
		Q_UNUSED(model);
		if (event->type() == QEvent::MouseMove) {
			_view->viewport()->update(option.rect);
			return false;
		} else if (event->type() != QEvent::MouseButtonRelease) {
			return false;
		}
		const auto mouse = static_cast<QMouseEvent*>(event);
		if (mouse->button() != Qt::LeftButton
			|| !option.rect.contains(mouse->position().toPoint())) {
			return false;
		}
		const auto pt = mouse->position().toPoint();
		if (!showButtons) {
			if (activated) {
				activated(index);
			}
			return true;
		}
		if (buttonRect(option.rect).contains(pt)) {
			if (libraryToggled) {
				libraryToggled(index);
			}
		} else if (playlistButtonRect(option.rect).contains(pt)) {
			if (playlistMenuRequested) {
				playlistMenuRequested(index, mouse->globalPosition().toPoint());
			}
		} else if (downloadRequested) {
			if (downloadButtonRect(option.rect).contains(pt)) {
				downloadRequested(index);
			} else if (activated) {
				activated(index);
			}
		} else if (activated) {
			activated(index);
		}
		return true;
	}

	[[nodiscard]] static bool InButtons(const QRect &row, const QPoint &pt) {
		return buttonRect(row).contains(pt)
			|| playlistButtonRect(row).contains(pt)
			|| downloadButtonRect(row).contains(pt);
	}

private:
	[[nodiscard]] static QRect buttonRect(const QRect &row) {
		return QRect(
			row.right() - 14 - kRowButtonSize,
			row.top() + (row.height() - kRowButtonSize) / 2,
			kRowButtonSize,
			kRowButtonSize);
	}
	[[nodiscard]] static QRect playlistButtonRect(const QRect &row) {
		return buttonRect(row).translated(-(kRowButtonSize + 4), 0);
	}
	[[nodiscard]] static QRect downloadButtonRect(const QRect &row) {
		return playlistButtonRect(row).translated(-(kRowButtonSize + 4), 0);
	}

	Player * const _player;
	SoundCloudClient * const _client;
	QListView * const _view;
	mutable QHash<QString, QPixmap> _artCache;

};

class ListDragFilter final : public QObject {
public:
	ListDragFilter(
		QListView *list,
		Fn<bool()> canDrag,
		Fn<void(int, int)> onDrop,
		bool anywhere = false)
	: QObject(list)
	, _list(list)
	, _canDrag(std::move(canDrag))
	, _onDrop(std::move(onDrop))
	, _anywhere(anywhere) {
		_line = new QWidget(list->viewport());
		_line->setAttribute(Qt::WA_TransparentForMouseEvents);
		_line->setFixedHeight(2);
		SetQss(_line, QStringLiteral("background: #ffffff; border: none;"));
		_line->hide();
	}

	bool eventFilter(QObject *obj, QEvent *e) override {
		if (obj != _list->viewport()) {
			return false;
		}
		switch (e->type()) {
		case QEvent::MouseButtonPress: {
			const auto m = static_cast<QMouseEvent*>(e);
			reset();
			if (m->button() != Qt::LeftButton || !_canDrag()) {
				return false;
			}
			const auto pt = m->position().toPoint();
			const auto index = _list->indexAt(pt);
			if (!index.isValid()
				|| (!_anywhere && TrackDelegate::InButtons(_list->visualRect(index), pt))) {
				return false;
			}
			_row = index.row();
			_start = pt;
			return false;
		}
		case QEvent::MouseMove: {
			if (_row < 0) {
				return false;
			}
			const auto pt = static_cast<QMouseEvent*>(e)->position().toPoint();
			if (!_dragging) {
				if ((pt - _start).manhattanLength() < QApplication::startDragDistance()) {
					return false;
				}
				_dragging = true;
				_list->viewport()->setCursor(Qt::ClosedHandCursor);
			}
			updateDrop(pt);
			return true;
		}
		case QEvent::MouseButtonRelease: {
			if (_row >= 0 && _dragging) {
				const auto row = _row;
				const auto drop = _drop;
				reset();
				if (drop >= 0) {
					_onDrop(row, drop);
				}
				return true;
			}
			reset();
			return false;
		}
		default:
			return false;
		}
	}

private:
	void updateDrop(const QPoint &pt) {
		const auto count = _list->model()->rowCount();
		if (count <= 0) {
			return;
		}
		auto y = 0;
		const auto index = _list->indexAt(pt);
		if (index.isValid()) {
			const auto rect = _list->visualRect(index);
			const auto after = (pt.y() > rect.center().y());
			_drop = index.row() + (after ? 1 : 0);
			y = after ? rect.bottom() : rect.top();
		} else if (pt.y() < _list->viewport()->height() / 2) {
			_drop = 0;
			y = _list->visualRect(_list->model()->index(0, 0)).top();
		} else {
			_drop = count;
			y = _list->visualRect(_list->model()->index(count - 1, 0)).bottom();
		}
		_line->setGeometry(8, y - 1, _list->viewport()->width() - 16, 2);
		_line->show();
		_line->raise();
	}

	void reset() {
		_row = -1;
		_drop = -1;
		_dragging = false;
		_line->hide();
		_list->viewport()->unsetCursor();
	}

	QListView * const _list;
	Fn<bool()> _canDrag;
	Fn<void(int, int)> _onDrop;
	bool _anywhere = false;
	QWidget *_line = nullptr;
	QPoint _start;
	int _row = -1;
	int _drop = -1;
	bool _dragging = false;

};

class QueueBox final : public Ui::BoxContent {
public:
	QueueBox(QWidget*, Player *player)
	: _player(player) {
	}

protected:
	void prepare() override {
		setTitle(rpl::single(u"Очередь"_q));
		const auto content = Ui::CreateChild<QWidget>(this);
		SetQss(content, QStringLiteral("background: #000000; border: none;"));
		const auto root = new QVBoxLayout(content);
		root->setContentsMargins(0, 0, 0, 0);
		root->setSpacing(0);

		const auto nowCaption = new QLabel(QString::fromUtf8("СЕЙЧАС ИГРАЕТ"), content);
		SetQss(nowCaption, QStringLiteral(
			"color: #8a8a8a; font-size: 10px; font-weight: 700; letter-spacing: 1px; "
			"padding: 6px 20px 0 20px; border: none; background: transparent;"));
		root->addWidget(nowCaption);
		const auto now = new QLabel(content);
		now->setWordWrap(false);
		SetQss(now, QStringLiteral(
			"color: #ffffff; font-size: 15px; font-weight: 600; padding: 4px 20px 12px 20px; "
			"border: none; background: transparent;"));
		root->addWidget(now);

		const auto nextCaption = new QLabel(QString::fromUtf8("ДАЛЬШЕ"), content);
		SetQss(nextCaption, QStringLiteral(
			"color: #8a8a8a; font-size: 10px; font-weight: 700; letter-spacing: 1px; "
			"padding: 4px 20px 6px 20px; border: none; background: transparent;"));
		root->addWidget(nextCaption);

		const auto player = _player;
		const auto list = new QListView(content);
		const auto model = new TrackListModel(list);
		list->setModel(model);
		list->setMouseTracking(true);
		list->viewport()->setAttribute(Qt::WA_Hover);
		list->setSelectionMode(QAbstractItemView::NoSelection);
		list->setEditTriggers(QAbstractItemView::NoEditTriggers);
		list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
		list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		list->setUniformItemSizes(true);
		list->setFocusPolicy(Qt::NoFocus);
		list->setFrameShape(QFrame::NoFrame);
		list->setContextMenuPolicy(Qt::CustomContextMenu);
		SetQss(list, QStringLiteral(
			"QListView { background: transparent; border: none; outline: none; }"
			"QScrollBar:vertical { background: transparent; width: 8px; margin: 4px 2px 4px 0; }"
			"QScrollBar::handle:vertical { background: #2e2e34; border-radius: 3px; min-height: 36px; }"
			"QScrollBar::handle:vertical:hover { background: #46464e; }"
			"QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: none; background: none; }"
			"QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"));
		root->addWidget(list, 1);

		const auto delegate = new TrackDelegate(player, SharedClient(), list);
		delegate->showButtons = false;
		list->setItemDelegate(delegate);

		const auto empty = new QLabel(
			QString::fromUtf8("Очередь пуста.\nДобавьте трек через правую кнопку мыши → «Играть следующим»."),
			content);
		empty->setWordWrap(true);
		empty->setAlignment(Qt::AlignCenter);
		SetQss(empty, QStringLiteral(
			"color: #8a8a8a; font-size: 12px; padding: 36px 24px; border: none; background: transparent;"));
		root->addWidget(empty);

		const auto hint = new QLabel(
			QString::fromUtf8("Нажмите на трек, чтобы запустить. Тащите мышью, чтобы поменять порядок. Правая кнопка мыши убирает трек из очереди."),
			content);
		hint->setWordWrap(true);
		SetQss(hint, QStringLiteral(
			"color: #6a6a70; font-size: 11px; padding: 8px 20px 6px 20px; border: none; background: transparent;"));
		root->addWidget(hint);

		const auto refresh = [=] {
			const auto current = player->currentTrack();
			now->setText(current.valid()
				? QFontMetrics(now->font()).elidedText(
					current.title + (current.artist.isEmpty()
						? QString()
						: QString::fromUtf8(" — ") + current.artist),
					Qt::ElideRight,
					440)
				: QString::fromUtf8("Ничего не играет"));
			const auto tracks = player->upcoming();
			model->setTracks(tracks);
			list->setVisible(!tracks.isEmpty());
			empty->setVisible(tracks.isEmpty());
		};
		const auto later = [=] {
			QTimer::singleShot(0, content, refresh);
		};
		refresh();

		QObject::connect(player, &Player::queueChanged, content, later);
		QObject::connect(player, &Player::currentTrackChanged, content, later);
		QObject::connect(SharedClient(), &SoundCloudClient::artworkLoaded, content, [=] {
			list->viewport()->update();
		});

		delegate->activated = [=](const QModelIndex &index) {
			player->playUpcoming(index.row());
		};
		const auto menuHolder = std::make_shared<QPointer<Ui::PopupMenu>>();
		const auto showMenu = [=](int row, const QPoint &globalPos) {
			if (row < 0 || row >= model->rowCount()) {
				return;
			}
			if (const auto old = menuHolder->data()) {
				delete old;
			}
			*menuHolder = new Ui::PopupMenu(list, st::popupMenuWithIcons);
			(*menuHolder)->addAction(QString::fromUtf8("Убрать из очереди"), [=] {
				player->removeUpcoming(row);
			});
			(*menuHolder)->popup(globalPos);
		};
		QObject::connect(list, &QWidget::customContextMenuRequested, content, [=](const QPoint &pt) {
			const auto index = list->indexAt(pt);
			if (index.isValid()) {
				showMenu(index.row(), list->viewport()->mapToGlobal(pt));
			}
		});
		list->viewport()->installEventFilter(new ListDragFilter(
			list,
			[] { return true; },
			[=](int from, int drop) {
				const auto count = model->rowCount();
				if (from < 0 || from >= count || drop == from || drop == from + 1) {
					return;
				}
				player->moveUpcoming(from, (drop > from) ? (drop - 1) : drop);
			},
			true));

		_content = content;
		content->resize(500, 580);
		setDimensions(500, 580);
		addButton(rpl::single(u"Готово"_q), [=] { closeBox(); });
	}

	void resizeEvent(QResizeEvent *e) override {
		BoxContent::resizeEvent(e);
		if (_content) {
			_content->setGeometry(0, 0, width(), height());
		}
	}

private:
	Player * const _player;
	QWidget *_content = nullptr;

};

VRGMiniPlayerBar::VRGMiniPlayerBar(
	Player *player,
	SoundCloudClient *client,
	Window::SessionController *controller,
	QWidget *parent)
: QWidget(parent)
, _player(player)
, _client(client)
, _controller(controller) {
	EnsureThemeHook(this);
	setAttribute(Qt::WA_StyledBackground, false);
	setFixedHeight(86);

	const auto makeTool = [&](const QString &name, int size, int iconSize) {
		const auto b = new QToolButton(this);
		b->setObjectName(name);
		b->setFixedSize(size, size);
		b->setIconSize(QSize(iconSize, iconSize));
		b->setCursor(Qt::PointingHandCursor);
		b->setFocusPolicy(Qt::NoFocus);
		return b;
	};

	_artwork = new QLabel(this);
	_artwork->setFixedSize(kBarArtworkSize, kBarArtworkSize);
	_title = new MarqueeLabel(this);
	_like = makeTool(QStringLiteral("vrgCtrl"), 30, 17);
	_like->setToolTip(QStringLiteral("Добавить / убрать из Моей музыки"));

	_shuffle = makeTool(QStringLiteral("vrgCtrl"), 30, 17);
	_prev = makeTool(QStringLiteral("vrgCtrl"), 32, 18);
	_prev->setIcon(GlyphIcon(Glyph::Prev, QColor(0xff, 0xff, 0xff), 18));
	_play = makeTool(QStringLiteral("vrgPlay"), 36, 18);
	_next = makeTool(QStringLiteral("vrgCtrl"), 32, 18);
	_next->setIcon(GlyphIcon(Glyph::Next, QColor(0xff, 0xff, 0xff), 18));
	_repeat = makeTool(QStringLiteral("vrgCtrl"), 30, 17);

	_lyricsBtn = makeTool(QStringLiteral("vrgCtrl"), 30, 17);
	_lyricsBtn->setIcon(GlyphIcon(Glyph::Lyrics, QColor(0x8a, 0x8a, 0x8a), 17));
	_lyricsBtn->setToolTip(QStringLiteral("Текст песни"));

	_eqBtn = makeTool(QStringLiteral("vrgCtrl"), 30, 17);
	_eqBtn->setIcon(GlyphIcon(Glyph::Eq, QColor(0x8a, 0x8a, 0x8a), 17));
	_eqBtn->setToolTip(QStringLiteral("Эквалайзер и усиление (до 1000%)"));

	_volumeIcon = new QLabel(this);
	_volumeIcon->setFixedSize(20, 20);
	_volume = new SmoothSlider(this);
	_volume->setRange(0, 100);
	_volume->setFixedWidth(90);
	_volumePct = new QLabel(QStringLiteral("80%"), this);
	_volumePct->setObjectName(QStringLiteral("vrgTime"));
	_volumePct->setFixedWidth(36);

	_queueBtn = makeTool(QStringLiteral("vrgCtrl"), 30, 17);
	_queueBtn->setIcon(GlyphIcon(Glyph::Queue, QColor(0x8a, 0x8a, 0x8a), 17));
	_queueBtn->setToolTip(QStringLiteral("Очередь"));
	_addPlaylistBtn = makeTool(QStringLiteral("vrgCtrl"), 30, 17);
	_addPlaylistBtn->setIcon(GlyphIcon(Glyph::Plus, QColor(0x8a, 0x8a, 0x8a), 17));
	_addPlaylistBtn->setToolTip(QStringLiteral("Добавить в плейлист"));

	_elapsed = new QLabel(FormatTime(0), this);
	_elapsed->setObjectName(QStringLiteral("vrgTime"));
	_elapsed->setMinimumWidth(36);
	_elapsed->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	_seek = new SmoothSlider(this);
	_seek->setRange(0, 0);
	_total = new QLabel(FormatTime(0), this);
	_total->setObjectName(QStringLiteral("vrgTime"));
	_total->setMinimumWidth(36);

	const auto top = new QHBoxLayout();
	top->setContentsMargins(0, 0, 0, 0);
	top->setSpacing(6);
	const auto makeBox = [&](QHBoxLayout *&layout) {
		const auto box = new QWidget(this);
		layout = new QHBoxLayout(box);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(6);
		return box;
	};
	QHBoxLayout *leftLay = nullptr;
	QHBoxLayout *centerLay = nullptr;
	QHBoxLayout *rightLay = nullptr;
	const auto leftBox = makeBox(leftLay);
	const auto centerBox = makeBox(centerLay);
	const auto rightBox = makeBox(rightLay);
	leftLay->addWidget(_artwork);
	leftLay->addSpacing(6);
	leftLay->addWidget(_title, 1);
	leftLay->addWidget(_like);
	centerLay->addWidget(_repeat);
	centerLay->addWidget(_prev);
	centerLay->addWidget(_play);
	centerLay->addWidget(_next);
	centerLay->addWidget(_shuffle);
	rightLay->addStretch(1);
	rightLay->addWidget(_lyricsBtn);
	rightLay->addWidget(_eqBtn);
	rightLay->addWidget(_queueBtn);
	rightLay->addSpacing(4);
	rightLay->addWidget(_volumeIcon);
	rightLay->addWidget(_volume);
	rightLay->addWidget(_volumePct);
	rightLay->addWidget(_addPlaylistBtn);
	leftBox->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	rightBox->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	leftBox->setMinimumWidth(150);
	rightBox->setMinimumWidth(rightBox->sizeHint().width());
	top->addWidget(leftBox, 1);
	top->addWidget(centerBox, 0);
	top->addWidget(rightBox, 1);

	const auto bottom = new QHBoxLayout();
	bottom->setContentsMargins(6, 0, 6, 0);
	bottom->setSpacing(8);
	bottom->addWidget(_elapsed);
	bottom->addWidget(_seek, 1);
	bottom->addWidget(_total);

	const auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(20, 12, 20, 10);
	layout->setSpacing(2);
	layout->addLayout(top);
	layout->addLayout(bottom);

	connect(_play, &QToolButton::clicked, _player, &Player::togglePlayPause);
	connect(_prev, &QToolButton::clicked, _player, &Player::prev);
	connect(_next, &QToolButton::clicked, _player, &Player::next);
	connect(_like, &QToolButton::clicked, this, [this] {
		const auto t = _player->currentTrack();
		if (t.valid()) {
			_player->toggleLibrary(t);
		}
	});
	connect(_shuffle, &QToolButton::clicked, this, [this] {
		_player->setShuffle(!_player->shuffle());
	});
	connect(_repeat, &QToolButton::clicked, this, [this] {
		switch (_player->repeatMode()) {
		case RepeatMode::None: _player->setRepeatMode(RepeatMode::All); break;
		case RepeatMode::All: _player->setRepeatMode(RepeatMode::One); break;
		case RepeatMode::One: _player->setRepeatMode(RepeatMode::None); break;
		}
	});
	connect(_lyricsBtn, &QToolButton::clicked, this, &VRGMiniPlayerBar::lyricsToggled);
	connect(_eqBtn, &QToolButton::clicked, this, &VRGMiniPlayerBar::eqRequested);
	connect(_queueBtn, &QToolButton::clicked, this, &VRGMiniPlayerBar::queueRequested);
	connect(_addPlaylistBtn, &QToolButton::clicked, this, [this] {
		const auto t = _player->currentTrack();
		if (t.valid()) {
			Q_EMIT playlistRequested(
				t,
				_addPlaylistBtn->mapToGlobal(QPoint(0, 0)));
		}
	});

	connect(_volume, &SmoothSlider::valueChanged, this, [this](qint64 value) {
		_volumePct->setText(QStringLiteral("%1%").arg(value));
		_player->setVolume(float(value) / 100.f);
		_volumeIcon->setPixmap(GlyphIcon(
			value ? Glyph::Volume : Glyph::VolumeMuted,
			QColor(0xff, 0xff, 0xff), 18).pixmap(QSize(18, 18)));
	});
	connect(_seek, &SmoothSlider::sliderMoved, this, [this](qint64 value) {
		_elapsed->setText(FormatTime(value));
	});
	connect(_seek, &SmoothSlider::sliderReleased, this, [this](qint64 value) {
		_player->seek(value);
	});

	connect(_player, &Player::currentTrackChanged, this, &VRGMiniPlayerBar::updateTrack);
	connect(_player, &Player::playbackStateChanged, this, &VRGMiniPlayerBar::updateState);
	connect(_player, &Player::positionChanged, this, &VRGMiniPlayerBar::updatePosition);
	connect(_player, &Player::durationChanged, this, &VRGMiniPlayerBar::updateDuration);
	connect(_player, &Player::shuffleChanged, this, &VRGMiniPlayerBar::updateShuffle);
	connect(_player, &Player::repeatModeChanged, this, &VRGMiniPlayerBar::updateRepeat);
	connect(_player, &Player::libraryChanged, this, &VRGMiniPlayerBar::updateLibraryState);
	connect(_player, &Player::volumeChanged, this, [this](float volume) {
		const auto value = qint64(std::round(volume * 100.f));
		_volume->setValue(value);
		_volumePct->setText(QStringLiteral("%1%").arg(value));
	});
	connect(_client, &SoundCloudClient::artworkLoaded, this, [this](
			const QString &url,
			const QPixmap &pixmap) {
		if (url == _artworkUrl) {
			setArtwork(pixmap);
		}
	});

	const auto volVal = qint64(std::round(_player->volume() * 100.f));
	_volume->setValue(volVal);
	_volumePct->setText(QStringLiteral("%1%").arg(volVal));
	_volumeIcon->setPixmap(GlyphIcon(
		volVal ? Glyph::Volume : Glyph::VolumeMuted,
		QColor(0xff, 0xff, 0xff), 18).pixmap(QSize(18, 18)));
	updateTrack(_player->currentTrack());
	updateState(_player->state());
	updateShuffle(_player->shuffle());
	connect(_player, &Player::visualSettingsChanged, this, &VRGMiniPlayerBar::applyVisualSettings);
	applyVisualSettings();
	updateRepeat(_player->repeatMode());
	updateDuration(_player->duration());
	updatePosition(_player->position());
}

void VRGMiniPlayerBar::paintEvent(QPaintEvent *) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	const auto pill = QRectF(rect()).adjusted(10, 4, -10, -8);
	QPainterPath clip;
	clip.addRoundedRect(pill, 16, 16);
	p.setClipPath(clip);

	QLinearGradient grad(pill.left(), 0, pill.right(), 0);
	grad.setColorAt(0.0, AccentStart(_accentColor));
	grad.setColorAt(0.55, AccentBg(_accentColor));
	grad.setColorAt(1.0, ThQ(0x0c, 0x0c, 0x0e));
	p.fillRect(pill, grad);

	p.setClipping(false);
	p.setPen(QPen(ThQ(255, 255, 255, 24), 1.0));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(pill, 16, 16);
}

void VRGMiniPlayerBar::updateTrack(const TrackInfo &track) {
	const auto empty = !track.valid();
	_title->setText(
		empty ? QStringLiteral("Ничего не играет") : track.title,
		empty ? QString() : SourceLabel(track) + track.artist);
	_artworkUrl = track.artworkUrl;
	setArtwork(_client->cachedArtwork(_artworkUrl));
	if (!_artworkUrl.isEmpty()) {
		_client->fetchArtwork(_artworkUrl);
	}
	_play->setEnabled(!empty);
	_prev->setEnabled(!empty);
	_next->setEnabled(!empty);
	_seek->setEnabled(!empty);
	_like->setEnabled(!empty);
	_addPlaylistBtn->setEnabled(!empty);
	updateLibraryState();
}

void VRGMiniPlayerBar::applyVisualSettings() {
	if (_lyricsBtn) {
		_lyricsBtn->setVisible(Vrg::Conf::Bool("player_bar_lyrics", true));
	}
	if (_eqBtn) {
		_eqBtn->setVisible(Vrg::Conf::Bool("player_bar_eq", true));
	}
	const auto showVol = Vrg::Conf::Bool("player_bar_vol", true);
	if (_volumeIcon) {
		_volumeIcon->setVisible(showVol);
	}
	if (_volume) {
		_volume->setVisible(showVol);
	}
	if (_volumePct) {
		_volumePct->setVisible(showVol);
	}
	if (_title) {
		_title->refreshSettings();
	}
}

void VRGMiniPlayerBar::updateLibraryState() {
	const auto t = _player->currentTrack();
	const auto liked = t.valid() && _player->isInLibrary(t.id);
	_like->setIcon(GlyphIcon(
		liked ? Glyph::HeartFilled : Glyph::Heart,
		liked ? QColor(0xff, 0xff, 0xff) : QColor(0x8a, 0x8a, 0x8a),
		17));
}

void VRGMiniPlayerBar::updateState(PlaybackState state) {
	const auto playing = (state == PlaybackState::Playing)
		|| (state == PlaybackState::Loading);
	_play->setIcon(GlyphIcon(playing ? Glyph::Pause : Glyph::Play, QColor(0x00, 0x00, 0x00), 18));
	_seek->setEnabled(state != PlaybackState::Loading
		&& _player->currentTrack().valid());
}

void VRGMiniPlayerBar::updatePosition(qint64 positionMs) {
	_posMs = positionMs;
	_seek->setValue(positionMs);
	_elapsed->setText(FormatTime(positionMs));
	update();
}

void VRGMiniPlayerBar::updateDuration(qint64 durationMs) {
	_durMs = durationMs;
	_seek->setRange(0, std::max(durationMs, qint64(0)));
	_total->setText(FormatTime(durationMs));
}

void VRGMiniPlayerBar::updateShuffle(bool enabled) {
	_shuffle->setIcon(GlyphIcon(Glyph::Shuffle, enabled ? QColor(0xff, 0xff, 0xff) : QColor(0x8a, 0x8a, 0x8a), 17));
}

void VRGMiniPlayerBar::updateRepeat(RepeatMode mode) {
	switch (mode) {
	case RepeatMode::None:
		_repeat->setIcon(GlyphIcon(Glyph::Repeat, QColor(0x8a, 0x8a, 0x8a), 17));
		break;
	case RepeatMode::All:
		_repeat->setIcon(GlyphIcon(Glyph::Repeat, QColor(0xff, 0xff, 0xff), 17));
		break;
	case RepeatMode::One:
		_repeat->setIcon(GlyphIcon(Glyph::RepeatOne, QColor(0xff, 0xff, 0xff), 17));
		break;
	}
}

void VRGMiniPlayerBar::setArtwork(const QPixmap &pixmap) {
	_accentColor = ExtractDominantColor(pixmap);
	_artwork->setPixmap(RoundedArtwork(
		pixmap,
		kBarArtworkSize,
		kBarArtworkSize / 2.0,
		std::max(devicePixelRatioF(), 1.) * MusicZoomFactor()));
	update();
}

VRGMusicWidget::VRGMusicWidget(
	Window::SessionController *controller,
	QWidget *parent)
: QWidget(parent)
, _controller(controller)
, _client(SharedClient())
, _player(SharedPlayer()) {
	EnsureThemeHook(this);
	setObjectName(QStringLiteral("vrgMusic"));
	setAttribute(Qt::WA_StyledBackground);
	SetQss(this, StyleSheet());
	setMinimumWidth(420);
	setupUi();

	connect(_client, &SoundCloudClient::tracksFound, this, [this](
			const QVector<TrackInfo> &tracks) {
		_searching = false;
		_searchedOnce = true;
		_searchModel->setTracks(tracks);
		refreshStatus();
	});
	connect(_client, &SoundCloudClient::errorOccurred, this, [this](
			const QString &message) {
		_searching = false;
		showError(message);
		refreshStatus();
	});
	connect(_player, &Player::errorOccurred, this, &VRGMusicWidget::showError);
	connect(_player, &Player::currentTrackChanged, this, [this](const TrackInfo &track) {
		if (!track.isYoutube()
			|| Vrg::Conf::Bool("yt_experimental_seen", false)
			|| !_controller) {
			return;
		}
		Vrg::Conf::SetBool("yt_experimental_seen", true);
		_controller->show(Box([](not_null<Ui::GenericBox*> box) {
			box->setTitle(rpl::single(QString::fromUtf8("YouTube: экспериментальная функция")));
			box->addRow(
				object_ptr<Ui::FlatLabel>(
					box,
					rpl::single(QString::fromUtf8(
						"Воспроизведение с YouTube в VRG экспериментальное. "
						"Треки могут загружаться долго (до 30–50 секунд) или не играть совсем. "
						"SoundCloud работает стабильно. Источник можно выбрать в настройках плеера.")),
					st::boxLabel),
				st::boxRowPadding);
			box->addButton(rpl::single(QString::fromUtf8("Понятно")), [=] { box->closeBox(); });
		}));
	});
	connect(_client, &SoundCloudClient::artworkLoaded, this, [this](
			const QString &url,
			const QPixmap &pixmap) {
		_list->viewport()->update();
		if (_lyricsOpen && url == _player->currentTrack().artworkUrl) {
			_lyricsBg = CreateBlurredBackground(pixmap);
			_lyricsBgScaled = {};
			update();
		}
	});
	connect(_player, &Player::libraryChanged, this, [this] {
		applyLocalFilter();
		_list->viewport()->update();
		refreshCounters();
		refreshStatus();
	});
	connect(_player, &Player::playlistsChanged, this, [this] {
		if (!_activePlaylistId.isEmpty()) {
			if (const auto pl = _player->findPlaylist(_activePlaylistId)) {
				_playlistTracksModel->setTracks(pl->tracks);
				_playlistTitleLabel->setText(pl->name);
			} else {
				_activePlaylistId.clear();
			}
		}
		applyLocalFilter();
		refreshPlaylists();
		refreshCounters();
		refreshStatus();
	});
	connect(_player, &Player::trackCached, this, [this] {
		_list->viewport()->update();
		refreshCounters();
	});
	connect(_player, &Player::currentTrackChanged, this, [this](const TrackInfo &track) {
		_list->viewport()->update();
		if (_lyricsOpen && track.valid() && track.id != _lyricsTrackId) {
			_lyricsTrackId = track.id;
			_lyricsHeader->setText(QStringLiteral("%1 — %2").arg(track.artist, track.title));
			_lyricsBg = CreateBlurredBackground(
				_client->cachedArtwork(track.artworkUrl));
			_lyricsBgScaled = {};
			update();
			_lyricsView->setStatusText(QStringLiteral("Загрузка текста…"));
			_player->fetchLyrics(track);
		}
	});
	connect(_player, &Player::playbackStateChanged, this, [this] {
		_list->viewport()->update();
	});
	connect(_player, &Player::positionChanged, this, &VRGMusicWidget::updateLyricsDisplay);
	connect(_player, &Player::lyricsLoaded, this, [this](
			const QString &trackId,
			const QString &plain,
			const QString &synced) {
		if (trackId != _lyricsTrackId) {
			return;
		}
		QVector<SmoothLyricsWidget::Line> lines;
		if (!synced.isEmpty()) {
			static const QRegularExpression re(QStringLiteral("\\[(\\d+):(\\d+(?:\\.\\d+)?)\\](.*)"));
			for (const auto &line : synced.split(QLatin1Char('\n'))) {
				const auto m = re.match(line.trimmed());
				if (m.hasMatch()) {
					const auto mins = m.captured(1).toLongLong();
					const auto secs = m.captured(2).toDouble();
					const auto text = m.captured(3).trimmed();
					if (!text.isEmpty()) {
						lines.push_back({ mins * 60000 + qint64(secs * 1000.0), text });
					}
				}
			}
		}
		if (!lines.isEmpty()) {
			_lyricsView->setSyncedLyrics(lines);
			_lyricsView->setPosition(_player->position());
		} else if (!plain.isEmpty()) {
			_lyricsView->setPlainLyrics(plain);
		} else {
			_lyricsView->setStatusText(QStringLiteral("Текст для этого трека не найден."));
		}
	});

	_libraryModel->setTracks(_player->library());
	refreshPlaylists();
	refreshCounters();
	setTab(_player->library().isEmpty() ? Tab::Search : Tab::Library);
}

VRGMusicWidget::~VRGMusicWidget() = default;

Player *VRGMusicWidget::player() const {
	return _player;
}

SoundCloudClient *VRGMusicWidget::client() const {
	return _client;
}

void VRGMusicWidget::setupUi() {
	_searchModel = new TrackListModel(this);
	_libraryModel = new TrackListModel(this);
	_playlistTracksModel = new TrackListModel(this);

	_search = new QLineEdit(this);
	_search->setObjectName(QStringLiteral("vrgSearch"));
	_search->setPlaceholderText(QStringLiteral("Поиск"));
	_search->setClearButtonEnabled(true);

	_searchTab = new QPushButton(QStringLiteral("Поиск"), this);
	_libraryTab = new QPushButton(QStringLiteral("Моя музыка"), this);
	_playlistsTab = new QPushButton(QStringLiteral("Плейлисты"), this);
	for (const auto tab : { _searchTab, _libraryTab, _playlistsTab }) {
		tab->setObjectName(QStringLiteral("vrgTab"));
		tab->setCheckable(true);
		tab->setCursor(Qt::PointingHandCursor);
		tab->setFocusPolicy(Qt::NoFocus);
	}
	const auto group = new QButtonGroup(this);
	group->setExclusive(true);
	group->addButton(_searchTab);
	group->addButton(_libraryTab);
	group->addButton(_playlistsTab);

	_libraryBar = new QWidget(this);
	SetQss(_libraryBar, QStringLiteral("background: transparent;"));
	const auto libBarLayout = new QHBoxLayout(_libraryBar);
	libBarLayout->setContentsMargins(4, 2, 0, 2);
	libBarLayout->setSpacing(10);
	_libraryCounterLabel = new QLabel(_libraryBar);
	SetQss(_libraryCounterLabel, QStringLiteral(
		"color: #7c7c84; font-size: 13px; font-weight: 500; background: transparent; border: none;"));
	_libraryImportBtn = new QPushButton(QStringLiteral("+ Импорт"), _libraryBar);
	_libraryImportBtn->setFixedHeight(30);
	_libraryImportBtn->setCursor(Qt::PointingHandCursor);
	SetQss(_libraryImportBtn, QStringLiteral(
		"QPushButton { background: #18181b; color: #e4e4e7; border: none; "
		"border-radius: 15px; padding: 0px 14px; font-size: 12px; font-weight: 600; }"
		"QPushButton:hover { background: #ffffff; color: #000000; }"));
	_libraryDownloadAllBtn = new QPushButton(QStringLiteral("↓  Скачать всё"), _libraryBar);
	_libraryDownloadAllBtn->setFixedHeight(30);
	_libraryDownloadAllBtn->setCursor(Qt::PointingHandCursor);
	SetQss(_libraryDownloadAllBtn, QStringLiteral(
		"QPushButton { background: #18181b; color: #e4e4e7; border: none; "
		"border-radius: 15px; padding: 0px 16px; font-size: 12px; font-weight: 600; }"
		"QPushButton:hover { background: #ffffff; color: #000000; }"));
	libBarLayout->addWidget(_libraryCounterLabel);
	libBarLayout->addStretch(1);
	libBarLayout->addWidget(_libraryImportBtn);
	libBarLayout->addWidget(_libraryDownloadAllBtn);
	_libraryBar->hide();

	_playlistBar = new QWidget(this);
	SetQss(_playlistBar, QStringLiteral("background: #0a0a0c; border-radius: 10px;"));
	const auto plBarLayout = new QHBoxLayout(_playlistBar);
	plBarLayout->setContentsMargins(10, 6, 10, 6);
	plBarLayout->setSpacing(8);
	_playlistBackBtn = new QPushButton(QStringLiteral("← Назад"), _playlistBar);
	_playlistBackBtn->setCursor(Qt::PointingHandCursor);
	SetQss(_playlistBackBtn, QStringLiteral(
		"QPushButton { background: #1c1c1f; color: #ffffff; border: none; "
		"border-radius: 8px; padding: 5px 12px; font-size: 13px; }"
		"QPushButton:hover { background: #2c2c30; }"));
	_playlistTitleLabel = new QLabel(_playlistBar);
	SetQss(_playlistTitleLabel, QStringLiteral(
		"color: #ffffff; font-size: 14px; font-weight: 600; background: transparent;"));
	_playlistPlayAllBtn = new QPushButton(QStringLiteral("▶ Играть"), _playlistBar);
	_playlistPlayAllBtn->setCursor(Qt::PointingHandCursor);
	SetQss(_playlistPlayAllBtn, QStringLiteral(
		"QPushButton { background: #ffffff; color: #000000; border: none; "
		"border-radius: 8px; padding: 5px 12px; font-size: 13px; font-weight: 600; }"
		"QPushButton:hover { background: #e6e6e6; }"));
	_playlistImportBtn = new QPushButton(QStringLiteral("+ Импорт"), _playlistBar);
	_playlistImportBtn->setCursor(Qt::PointingHandCursor);
	SetQss(_playlistImportBtn, QStringLiteral(
		"QPushButton { background: #1c1c1f; color: #ffffff; border: none; "
		"border-radius: 8px; padding: 5px 12px; font-size: 13px; }"
		"QPushButton:hover { background: #2c2c30; }"));
	_playlistDownloadAllBtn = new QPushButton(QStringLiteral("↓ Скачать всё"), _playlistBar);
	_playlistDownloadAllBtn->setCursor(Qt::PointingHandCursor);
	SetQss(_playlistDownloadAllBtn, QStringLiteral(
		"QPushButton { background: #1c1c1f; color: #ffffff; border: none; "
		"border-radius: 8px; padding: 5px 12px; font-size: 13px; }"
		"QPushButton:hover { background: #2c2c30; }"));
	plBarLayout->addWidget(_playlistBackBtn);
	plBarLayout->addWidget(_playlistTitleLabel, 1);
	plBarLayout->addWidget(_playlistPlayAllBtn);
	plBarLayout->addWidget(_playlistImportBtn);
	plBarLayout->addWidget(_playlistDownloadAllBtn);
	_playlistBar->hide();

	_error = new QLabel(this);
	_error->setObjectName(QStringLiteral("vrgError"));
	_error->setWordWrap(true);
	connect(_error, &QLabel::linkActivated, this, [this](const QString &) {
		Vrg::OpenPlayerSettings(_controller);
	});
	_error->hide();

	_list = new QListView(this);
	_list->setObjectName(QStringLiteral("vrgList"));
	_list->setMouseTracking(true);
	_list->viewport()->setAttribute(Qt::WA_Hover);
	_list->setSelectionMode(QAbstractItemView::NoSelection);
	_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
	_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_list->setUniformItemSizes(true);
	_list->setFocusPolicy(Qt::NoFocus);

	_list->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(_list, &QWidget::customContextMenuRequested, this, [this](const QPoint &pt) {
		const auto index = _list->indexAt(pt);
		if (!index.isValid()) {
			return;
		}
		const auto track = currentModel()->track(index.row());
		if (track.valid()) {
			showPlaylistMenuForTrack(track, _list->viewport()->mapToGlobal(pt), true);
		}
	});

	_delegate = new TrackDelegate(_player, _client, _list);
	_delegate->activated = [this](const QModelIndex &index) {
		playFromList(index);
	};
	_delegate->libraryToggled = [this](const QModelIndex &index) {
		toggleLibrary(index);
	};
	_delegate->playlistMenuRequested = [this](
			const QModelIndex &index,
			const QPoint &globalPos) {
		const auto track = currentModel()->track(index.row());
		if (track.valid()) {
			showPlaylistMenuForTrack(track, globalPos);
		}
	};
	_delegate->downloadRequested = [this](const QModelIndex &index) {
		toggleDownload(index);
	};
	_list->setItemDelegate(_delegate);
	_list->viewport()->installEventFilter(new ListDragFilter(
		_list,
		[this] {
			const auto reorderable = (_tab == Tab::Library)
				|| (_tab == Tab::Playlists && !_activePlaylistId.isEmpty());
			return reorderable && _search->text().trimmed().isEmpty();
		},
		[this](int from, int drop) {
			const auto model = currentModel();
			const auto count = model ? model->rowCount() : 0;
			if (!model || from < 0 || from >= count || drop == from || drop == from + 1) {
				return;
			}
			const auto moved = model->track(from);
			const auto anchor = (drop < count)
				? model->track(drop)
				: model->track(count - 1);
			_player->reorderTrack(_activePlaylistId, moved.id, anchor.id, drop >= count);
		}));

	_status = new QLabel(this);
	_status->setObjectName(QStringLiteral("vrgStatus"));
	_status->setAlignment(Qt::AlignCenter);
	_status->setWordWrap(true);

	const auto playlistsScroll = new QScrollArea(this);
	playlistsScroll->setWidgetResizable(true);
	playlistsScroll->setFrameShape(QFrame::NoFrame);
	SetQss(playlistsScroll, QStringLiteral("background: #000000; border: none;"));
	_playlistsContainer = new QWidget(playlistsScroll);
	SetQss(_playlistsContainer, QStringLiteral("background: #000000;"));
	const auto plRootLayout = new QVBoxLayout(_playlistsContainer);
	plRootLayout->setContentsMargins(14, 8, 14, 14);
	plRootLayout->setSpacing(8);
	_newPlaylistBtn = new QPushButton(QStringLiteral("+ Новый плейлист"), _playlistsContainer);
	_newPlaylistBtn->setCursor(Qt::PointingHandCursor);
	SetQss(_newPlaylistBtn, QStringLiteral(
		"QPushButton { background: #1c1c1f; color: #ffffff; border: 1px solid #2a2a2e; "
		"border-radius: 10px; padding: 10px 14px; font-size: 14px; font-weight: 600; text-align: left; }"
		"QPushButton:hover { background: #26262a; }"));
	plRootLayout->addWidget(_newPlaylistBtn);
	plRootLayout->addStretch(1);
	playlistsScroll->setWidget(_playlistsContainer);

	_lyricsPage = new LyricsPageWidget(this);
	const auto lyricsLayout = new QVBoxLayout(_lyricsPage);
	lyricsLayout->setContentsMargins(24, 18, 24, 8);
	lyricsLayout->setSpacing(10);
	const auto lyricsTop = new QHBoxLayout();
	_lyricsWatermark = new QLabel(QStringLiteral("V R G   M U S I C"), _lyricsPage);
	SetQss(_lyricsWatermark, QStringLiteral(
		"color: rgba(255, 255, 255, 130); font-size: 11px; font-weight: 700; letter-spacing: 2px; background: transparent;"));
	_lyricsHeader = new QLabel(QStringLiteral("Текст песни"), _lyricsPage);
	_lyricsHeader->setAlignment(Qt::AlignCenter);
	SetQss(_lyricsHeader, QStringLiteral(
		"color: rgba(255, 255, 255, 220); font-size: 14px; font-weight: 600; background: transparent;"));
	_lyricsCloseBtn = new QPushButton(QStringLiteral("✕"), _lyricsPage);
	_lyricsCloseBtn->setFixedSize(34, 34);
	_lyricsCloseBtn->setCursor(Qt::PointingHandCursor);
	_lyricsCloseBtn->setToolTip(QStringLiteral("Закрыть текст"));
	SetQss(_lyricsCloseBtn, QStringLiteral(
		"QPushButton { background: rgba(255, 255, 255, 22); color: #ffffff; border: none; "
		"border-radius: 17px; font-size: 14px; font-weight: 600; }"
		"QPushButton:hover { background: rgba(255, 255, 255, 48); }"));
	const auto wmBox = new QWidget(_lyricsPage);
	wmBox->setFixedWidth(150);
	const auto wmLay = new QHBoxLayout(wmBox);
	wmLay->setContentsMargins(0, 0, 0, 0);
	wmLay->addWidget(_lyricsWatermark);
	wmLay->addStretch(1);
	const auto closeBox = new QWidget(_lyricsPage);
	closeBox->setFixedWidth(150);
	const auto closeLay = new QHBoxLayout(closeBox);
	closeLay->setContentsMargins(0, 0, 0, 0);
	closeLay->addStretch(1);
	closeLay->addWidget(_lyricsCloseBtn);
	_lyricsHeader->setAlignment(Qt::AlignCenter);
	lyricsTop->addWidget(wmBox);
	lyricsTop->addWidget(_lyricsHeader, 1);
	lyricsTop->addWidget(closeBox);

	_lyricsView = new SmoothLyricsWidget(_lyricsPage);
	connect(_lyricsView, &SmoothLyricsWidget::seekRequested, _player, &Player::seek);
	lyricsLayout->addLayout(lyricsTop);
	lyricsLayout->addWidget(_lyricsView, 1);

	_pages = new QStackedWidget(this);
	_pages->addWidget(_list);
	_pages->addWidget(_status);
	_pages->addWidget(playlistsScroll);
	_pages->addWidget(_lyricsPage);

	_bar = new VRGMiniPlayerBar(_player, _client, _controller, this);

	const auto tabs = new QHBoxLayout();
	tabs->setContentsMargins(0, 0, 0, 0);
	tabs->setSpacing(4);
	tabs->addWidget(_searchTab);
	tabs->addWidget(_libraryTab);
	tabs->addWidget(_playlistsTab);
	tabs->addStretch(1);

	_headerWidget = new QWidget(this);
	const auto header = new QVBoxLayout(_headerWidget);
	header->setContentsMargins(14, 14, 14, 6);
	header->setSpacing(10);
	header->addWidget(_search);
	header->addLayout(tabs);
	header->addWidget(_libraryBar);
	header->addWidget(_playlistBar);
	header->addWidget(_error);

	const auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(_headerWidget);
	layout->addWidget(_pages, 1);
	layout->addWidget(_bar);

	connect(_lyricsCloseBtn, &QPushButton::clicked, this, &VRGMusicWidget::toggleLyricsPanel);

	connect(_search, &QLineEdit::returnPressed, this, &VRGMusicWidget::runSearch);
	connect(_search, &QLineEdit::textChanged, this, &VRGMusicWidget::applyLocalFilter);
	connect(_searchTab, &QPushButton::clicked, this, [this] {
		setTab(Tab::Search);
	});
	connect(_libraryTab, &QPushButton::clicked, this, [this] {
		setTab(Tab::Library);
	});
	connect(_playlistsTab, &QPushButton::clicked, this, [this] {
		closePlaylist();
		setTab(Tab::Playlists);
	});
	connect(_libraryImportBtn, &QPushButton::clicked, this, [this] {
		showImportBox(QString());
	});
	connect(_libraryDownloadAllBtn, &QPushButton::clicked, this, [this] {
		for (const auto &track : _player->library()) {
			_player->cacheTrack(track);
		}
	});
	connect(_newPlaylistBtn, &QPushButton::clicked, this, [this] {
		createPlaylistPrompt();
	});
	connect(_playlistBackBtn, &QPushButton::clicked, this, &VRGMusicWidget::closePlaylist);
	connect(_playlistPlayAllBtn, &QPushButton::clicked, this, [this] {
		if (!_playlistTracksModel->tracks().isEmpty()) {
			_player->setQueue(_playlistTracksModel->tracks(), 0);
		}
	});
	connect(_playlistImportBtn, &QPushButton::clicked, this, [this] {
		if (!_activePlaylistId.isEmpty()) {
			showImportBox(_activePlaylistId);
		}
	});
	connect(_playlistDownloadAllBtn, &QPushButton::clicked, this, [this] {
		if (!_activePlaylistId.isEmpty()) {
			_player->cachePlaylist(_activePlaylistId);
		}
	});
	connect(_bar, &VRGMiniPlayerBar::lyricsToggled, this, &VRGMusicWidget::toggleLyricsPanel);
	connect(_bar, &VRGMiniPlayerBar::eqRequested, this, &VRGMusicWidget::showEqBox);
	connect(_bar, &VRGMiniPlayerBar::queueRequested, this, &VRGMusicWidget::showQueueBox);
	connect(_bar, &VRGMiniPlayerBar::playlistRequested, this, [this](TrackInfo track, QPoint globalPos) {
		showPlaylistMenuForTrack(track, globalPos, false);
	});
}

void VRGMusicWidget::setTab(Tab tab) {
	if (_lyricsOpen) {
		_lyricsOpen = false;
		if (_headerWidget) {
			_headerWidget->show();
		}
		Q_EMIT fullscreenLyricsChanged(false);
	}
	_tab = tab;
	_searchTab->setChecked(tab == Tab::Search);
	_libraryTab->setChecked(tab == Tab::Library);
	_playlistsTab->setChecked(tab == Tab::Playlists);
	_libraryBar->setVisible(tab == Tab::Library && !_player->library().isEmpty());
	if (tab != Tab::Playlists) {
		_activePlaylistId.clear();
		_playlistBar->hide();
	}
	_delegate->playlistRemoveMode = (tab == Tab::Playlists && !_activePlaylistId.isEmpty());
	_list->setModel(currentModel());
	_list->scrollToTop();
	applyLocalFilter();
	refreshStatus();
}

void VRGMusicWidget::applyLocalFilter() {
	const auto q = _search ? _search->text().trimmed().toLower() : QString();
	const auto matches = [&](const TrackInfo &t) {
		if (q.isEmpty()) {
			return true;
		}
		return t.title.toLower().contains(q)
			|| t.artist.toLower().contains(q);
	};
	if (_tab == Tab::Library) {
		QVector<TrackInfo> out;
		for (const auto &t : _player->library()) {
			if (matches(t)) {
				out.push_back(t);
			}
		}
		_libraryModel->setTracks(out);
	} else if (_tab == Tab::Playlists && !_activePlaylistId.isEmpty()) {
		if (const auto pl = _player->findPlaylist(_activePlaylistId)) {
			QVector<TrackInfo> out;
			for (const auto &t : pl->tracks) {
				if (matches(t)) {
					out.push_back(t);
				}
			}
			_playlistTracksModel->setTracks(out);
		}
	}
}

TrackListModel *VRGMusicWidget::currentModel() const {
	if (_tab == Tab::Search) {
		return _searchModel;
	} else if (_tab == Tab::Playlists && !_activePlaylistId.isEmpty()) {
		return _playlistTracksModel;
	}
	return _libraryModel;
}

void VRGMusicWidget::runSearch() {
	if (_tab == Tab::Library || (_tab == Tab::Playlists && !_activePlaylistId.isEmpty())) {
		applyLocalFilter();
		return;
	}
	const auto query = _search->text().trimmed();
	if (query.isEmpty()) {
		return;
	}
	_error->hide();
	_searching = true;
	_searchModel->setTracks({});
	setTab(Tab::Search);
	_client->searchTracks(query);
}

void VRGMusicWidget::refreshCounters() {
	const auto &lib = _player->library();
	const auto total = int(lib.size());
	int offline = 0;
	for (const auto &t : lib) {
		if (_player->isTrackCached(t.id)) {
			++offline;
		}
	}
	_libraryTab->setText(total > 0
		? QStringLiteral("Моя музыка · %1").arg(total)
		: QStringLiteral("Моя музыка"));
	const auto plCount = int(_player->playlists().size());
	_playlistsTab->setText(plCount > 0
		? QStringLiteral("Плейлисты · %1").arg(plCount)
		: QStringLiteral("Плейлисты"));
	_libraryCounterLabel->setText(
		QStringLiteral("Треков: %1   ·   Офлайн: %2 / %1").arg(total).arg(offline));
	_libraryBar->setVisible(_tab == Tab::Library && total > 0);
	if (!_activePlaylistId.isEmpty()) {
		if (const auto pl = _player->findPlaylist(_activePlaylistId)) {
			int plOffline = 0;
			for (const auto &t : pl->tracks) {
				if (_player->isTrackCached(t.id)) {
					++plOffline;
				}
			}
			const auto plTotal = int(pl->tracks.size());
			_playlistTitleLabel->setText(plTotal > 0
				? QStringLiteral("%1   ·   Офлайн: %2 / %3")
					.arg(pl->name).arg(plOffline).arg(plTotal)
				: pl->name);
		}
	}
}

void VRGMusicWidget::refreshStatus() {
	if (_lyricsOpen) {
		_pages->setCurrentWidget(_lyricsPage);
		return;
	}
	if (_tab == Tab::Playlists && _activePlaylistId.isEmpty()) {
		_playlistBar->hide();
		_pages->setCurrentIndex(2);
		return;
	}
	auto text = QString();
	if (_tab == Tab::Search) {
		if (_searching) {
			text = QStringLiteral("Ищем…");
		} else if (_searchModel->rowCount() == 0) {
			text = _searchedOnce
				? QStringLiteral("Ничего не найдено")
				: QStringLiteral("Введите название трека или исполнителя\nи нажмите Enter");
		}
	} else if (_tab == Tab::Library && _libraryModel->rowCount() == 0) {
		text = QStringLiteral("Здесь пока пусто.\nДобавляйте треки кнопкой +");
	} else if (_tab == Tab::Playlists && _playlistTracksModel->rowCount() == 0) {
		text = QStringLiteral("Плейлист пуст.\nДобавляйте треки кнопкой ≡+ из поиска или библиотеки");
	}
	_status->setText(text);
	_pages->setCurrentWidget(text.isEmpty()
		? static_cast<QWidget*>(_list)
		: static_cast<QWidget*>(_status));
}

void VRGMusicWidget::refreshPlaylists() {
	if (!_playlistsContainer) {
		return;
	}
	const auto layout = qobject_cast<QVBoxLayout*>(_playlistsContainer->layout());
	if (!layout) {
		return;
	}
	while (layout->count() > 2) {
		const auto item = layout->takeAt(1);
		if (const auto w = item->widget()) {
			w->deleteLater();
		}
		delete item;
	}
	const auto &playlists = _player->playlists();
	for (int i = 0; i < playlists.size(); ++i) {
		const auto pl = playlists[i];
		const auto card = new QWidget(_playlistsContainer);
		SetQss(card, QStringLiteral(
			"QWidget { background: #111113; border: 1px solid #1e1e22; border-radius: 12px; }"
			"QWidget:hover { background: #18181c; border-color: #2c2c32; }"));
		card->setCursor(Qt::PointingHandCursor);
		const auto row = new QHBoxLayout(card);
		row->setContentsMargins(10, 8, 10, 8);
		row->setSpacing(12);

		const auto cover = new QLabel(card);
		cover->setFixedSize(44, 44);
		SetQss(cover, QStringLiteral("border: none; background: transparent;"));
		QPixmap plPix;
		if (!pl.tracks.isEmpty() && !pl.tracks.first().artworkUrl.isEmpty()) {
			plPix = _client->cachedArtwork(pl.tracks.first().artworkUrl);
			if (plPix.isNull()) {
				_client->fetchArtwork(pl.tracks.first().artworkUrl);
			}
		}
		cover->setPixmap(RoundedArtwork(plPix, 44, 8, std::max(devicePixelRatioF(), 1.) * MusicZoomFactor()));

		const auto infoBtn = new QPushButton(
			QStringLiteral("%1\n%2 треков")
				.arg(pl.name)
				.arg(pl.tracks.size()),
			card);
		SetQss(infoBtn, QStringLiteral(
			"QPushButton { background: transparent; color: #ffffff; border: none; "
			"text-align: left; font-size: 14px; font-weight: 600; }"));
		infoBtn->setCursor(Qt::PointingHandCursor);
		connect(infoBtn, &QPushButton::clicked, this, [this, id = pl.id] {
			openPlaylist(id);
		});

		const auto playBtn = new QPushButton(QStringLiteral("▶"), card);
		playBtn->setFixedSize(32, 32);
		playBtn->setCursor(Qt::PointingHandCursor);
		playBtn->setToolTip(QStringLiteral("Играть плейлист"));
		SetQss(playBtn, QStringLiteral(
			"QPushButton { background: #ffffff; color: #000000; border: none; border-radius: 16px; font-size: 12px; }"
			"QPushButton:hover { background: #e0e0e0; }"));
		connect(playBtn, &QPushButton::clicked, this, [this, id = pl.id] {
			if (const auto found = _player->findPlaylist(id)) {
				if (!found->tracks.isEmpty()) {
					_player->setQueue(found->tracks, 0);
				}
			}
		});

		const auto dlBtn = new QPushButton(QStringLiteral("↓"), card);
		dlBtn->setFixedSize(32, 32);
		dlBtn->setCursor(Qt::PointingHandCursor);
		dlBtn->setToolTip(QStringLiteral("Скачать плейлист для офлайна"));
		SetQss(dlBtn, QStringLiteral(
			"QPushButton { background: #1c1c1f; color: #ffffff; border: none; border-radius: 16px; font-size: 14px; }"
			"QPushButton:hover { background: #2c2c30; }"));
		connect(dlBtn, &QPushButton::clicked, this, [this, id = pl.id] {
			_player->cachePlaylist(id);
		});

		const auto delBtn = new QPushButton(QStringLiteral("✕"), card);
		delBtn->setFixedSize(32, 32);
		delBtn->setCursor(Qt::PointingHandCursor);
		delBtn->setToolTip(QStringLiteral("Удалить плейлист"));
		SetQss(delBtn, QStringLiteral(
			"QPushButton { background: transparent; color: #8a8a8a; border: none; border-radius: 16px; font-size: 13px; }"
			"QPushButton:hover { background: #2c1c1c; color: #ff5555; }"));
		connect(delBtn, &QPushButton::clicked, this, [this, id = pl.id] {
			_player->deletePlaylist(id);
		});

		row->addWidget(cover);
		row->addWidget(infoBtn, 1);
		row->addWidget(playBtn);
		row->addWidget(dlBtn);
		row->addWidget(delBtn);
		layout->insertWidget(1 + i, card);
	}
}

void VRGMusicWidget::openPlaylist(const QString &playlistId) {
	const auto pl = _player->findPlaylist(playlistId);
	if (!pl) {
		return;
	}
	_activePlaylistId = playlistId;
	_playlistTitleLabel->setText(pl->name);
	_playlistTracksModel->setTracks(pl->tracks);
	_playlistBar->show();
	_delegate->playlistRemoveMode = true;
	_list->setModel(_playlistTracksModel);
	_list->scrollToTop();
	refreshCounters();
	refreshStatus();
}

void VRGMusicWidget::closePlaylist() {
	_activePlaylistId.clear();
	_playlistBar->hide();
	_delegate->playlistRemoveMode = false;
	refreshStatus();
}

void VRGMusicWidget::createPlaylistPrompt(std::optional<TrackInfo> autoAddTrack) {
	if (!_controller) {
		return;
	}
	_controller->show(Box<CreatePlaylistBox>([this, autoAddTrack](QString name) {
		const auto id = _player->createPlaylist(name);
		if (!id.isEmpty() && autoAddTrack && autoAddTrack->valid()) {
			_player->addTrackToPlaylist(id, *autoAddTrack);
		}
	}));
}

void VRGMusicWidget::showPlaylistMenuForTrack(
		const TrackInfo &track,
		const QPoint &globalPos,
		bool withQueue) {
	if (const auto old = _popupMenu.data()) {
		delete old;
	}
	_popupMenu = new Ui::PopupMenu(this, st::popupMenuWithIcons);
	if (withQueue) {
		_popupMenu->addAction(QString::fromUtf8("Играть следующим"), [this, track] {
			_player->enqueueNext(track);
			if (_controller) {
				_controller->showToast(QString::fromUtf8("Добавлено: играть следующим"));
			}
		});
		_popupMenu->addAction(QString::fromUtf8("В конец очереди"), [this, track] {
			_player->enqueueLast(track);
			if (_controller) {
				_controller->showToast(QString::fromUtf8("Добавлено в конец очереди"));
			}
		});
		_popupMenu->addSeparator();
	}
	_popupMenu->addAction(QStringLiteral("Отправить в чат…"), [this, track] {
		sendTrackToChat(track);
	});
	if (!_activePlaylistId.isEmpty()) {
		const auto activeId = _activePlaylistId;
		_popupMenu->addAction(QStringLiteral("Выше в плейлисте"), [this, activeId, track] {
			_player->moveTrackInPlaylist(activeId, track.id, -1);
		});
		_popupMenu->addAction(QStringLiteral("Ниже в плейлисте"), [this, activeId, track] {
			_player->moveTrackInPlaylist(activeId, track.id, +1);
		});
	}
	_popupMenu->addSeparator();
	const auto &playlists = _player->playlists();
	for (const auto &pl : playlists) {
		const auto already = pl.tracks.contains(track);
		const auto label = already
			? QStringLiteral("✓ %1").arg(pl.name)
			: pl.name;
		const auto plId = pl.id;
		_popupMenu->addAction(label, [this, plId, track, already] {
			if (already) {
				_player->removeTrackFromPlaylist(plId, track.id);
			} else {
				_player->addTrackToPlaylist(plId, track);
			}
		});
	}
	if (!playlists.isEmpty()) {
		_popupMenu->addSeparator();
	}
	_popupMenu->addAction(QStringLiteral("+ Новый плейлист…"), [this, track] {
		createPlaylistPrompt(track);
	});
	_popupMenu->popup(globalPos);
}

namespace {

QString SafeFileName(QString name) {
	name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")), QStringLiteral("_"));
	name = name.simplified();
	return name.isEmpty() ? QStringLiteral("track") : name.left(120);
}

void SendTrackToThread(
		Window::SessionController *ctrl,
		not_null<Data::Thread*> thread,
		const TrackInfo &track,
		QObject *context) {
	const auto player = SharedPlayer();
	const auto client = SharedClient();
	const auto toast = [ctrl](const QString &text) {
		if (ctrl) {
			ctrl->showToast(text);
		}
	};
	const auto dir = QDir::tempPath() + QStringLiteral("/vrg-send");
	QDir().mkpath(dir);
	const auto base = SafeFileName(track.artist.isEmpty()
		? track.title
		: (track.artist + QStringLiteral(" - ") + track.title));
	const auto out = dir + '/' + base + QStringLiteral(".mp3");
	const auto cover = dir + '/' + SafeFileName(track.id) + QStringLiteral(".jpg");
	const auto art = client->cachedArtwork(track.artworkUrl);
	const auto hasCover = !art.isNull() && art.toImage().save(cover, "JPG", 92);
	const auto weakSession = base::make_weak(&thread->session());
	const auto threadWeak = base::make_weak(thread.get());

	const auto run = [=](const QString &source, const QString &cleanupSrc = QString()) {
		auto args = QStringList{ "-y", "-loglevel", "error", "-i", source };
		if (hasCover) {
			args << "-i" << cover;
		}
		args << "-map" << "0:a:0";
		if (hasCover) {
			args << "-map" << "1:v:0" << "-c:v" << "mjpeg"
				<< "-disposition:v" << "attached_pic"
				<< "-metadata:s:v" << "title=Cover"
				<< "-metadata:s:v" << "comment=Cover (front)";
		}
		args << "-c:a" << "libmp3lame" << "-b:a" << "192k"
			<< "-id3v2_version" << "3"
			<< "-metadata" << (QStringLiteral("title=") + track.title)
			<< "-metadata" << (QStringLiteral("artist=") + track.artist)
			<< out;
		const auto proc = new QProcess(context);
		QObject::connect(proc, &QProcess::finished, context, [=](int code) {
			proc->deleteLater();
			QFile::remove(cover);
			if (!cleanupSrc.isEmpty()) {
				QFile::remove(cleanupSrc);
			}
			const auto session = weakSession.get();
			const auto target = threadWeak.get();
			if (code != 0 || !session || !target || !QFileInfo::exists(out)) {
				toast(QStringLiteral("Не удалось подготовить трек"));
				QFile::remove(out);
				return;
			}
			auto list = Storage::PrepareMediaList(
				QStringList{ out },
				512,
				session->premium());
			if (list.error != Ui::PreparedList::Error::None
				|| list.files.empty()) {
				toast(QStringLiteral("Не удалось отправить трек"));
				QFile::remove(out);
				return;
			}
			session->api().sendFiles(
				std::move(list),
				SendMediaType::File,
				nullptr,
				Api::SendAction(target));
			toast(QStringLiteral("Трек отправлен в чат"));
			QTimer::singleShot(15 * 1000, qApp, [out] {
				QFile::remove(out);
			});
		});
		QObject::connect(proc, &QProcess::errorOccurred, context, [=](QProcess::ProcessError e) {
			if (e == QProcess::FailedToStart) {
				proc->deleteLater();
				toast(QStringLiteral("Не найден ffmpeg"));
			}
		});
		proc->start(QStringLiteral("ffmpeg"), args);
	};

	if (player->isTrackCached(track.id)) {
		const auto bytes = Player::ReadCachedBytes(track.id);
		const auto tmpSrc = dir + '/' + SafeFileName(track.id) + QStringLiteral(".src");
		QFile tf(tmpSrc);
		if (!bytes.isEmpty() && tf.open(QIODevice::WriteOnly)) {
			tf.write(bytes);
			tf.close();
			toast(QStringLiteral("Готовлю трек…"));
			run(tmpSrc, tmpSrc);
			return;
		}
	}
	toast(QStringLiteral("Загрузка трека для отправки…"));
	const auto conn = std::make_shared<QMetaObject::Connection>();
	const auto targetId = track.id;
	*conn = QObject::connect(
		client,
		&SoundCloudClient::streamResolved,
		context,
		[conn, targetId, run](const QString &resolvedId, const QString &url) {
			if (resolvedId != targetId) {
				return;
			}
			QObject::disconnect(*conn);
			run(url);
		});
	client->resolveStreamUrl(track);
}

}

void VRGMusicWidget::sendTrackToChat(const TrackInfo &track) {
	if (!track.valid() || !_controller) {
		return;
	}
	const auto ctrl = _controller;
	Window::ShowChooseRecipientBox(ctrl, [this, ctrl, track](
			not_null<Data::Thread*> thread) {
		SendTrackToThread(ctrl, thread, track, this);
		return true;
	});
}

void VRGMusicWidget::paintEvent(QPaintEvent *e) {
	if (_lyricsOpen) {
		QPainter p(this);
		if (_lyricsBg.isNull()) {
			p.fillRect(rect(), ThQ(0x0c, 0x0c, 0x0e));
		} else {
			if (_lyricsBgScaled.isNull() || _lyricsBgScaledSize != size()) {
				_lyricsBgScaledSize = size();
				auto canvas = QPixmap(size());
				canvas.fill(ThQ(0x0c, 0x0c, 0x0e));
				const auto scaled = _lyricsBg.scaled(
					size(),
					Qt::KeepAspectRatioByExpanding,
					Qt::SmoothTransformation);
				QPainter cp(&canvas);
				cp.drawPixmap(
					(width() - scaled.width()) / 2,
					(height() - scaled.height()) / 2,
					scaled);
				cp.fillRect(canvas.rect(), ThQ(0, 0, 0, 145));
				_lyricsBgScaled = std::move(canvas);
			}
			p.drawPixmap(0, 0, _lyricsBgScaled);
		}
		return;
	}
	QWidget::paintEvent(e);
}

void VRGMusicWidget::toggleLyricsPanel() {
	_lyricsOpen = !_lyricsOpen;
	if (_headerWidget) {
		_headerWidget->setVisible(!_lyricsOpen);
	}
	Q_EMIT fullscreenLyricsChanged(_lyricsOpen);
	if (_lyricsOpen) {
		const auto t = _player->currentTrack();
		if (t.valid()) {
			_lyricsBg = CreateBlurredBackground(
				_client->cachedArtwork(t.artworkUrl));
			_lyricsBgScaled = {};
			if (t.id != _lyricsTrackId) {
				_lyricsTrackId = t.id;
				_lyricsHeader->setText(QStringLiteral("%1 — %2").arg(t.artist, t.title));
				_lyricsView->setStatusText(QStringLiteral("Загрузка текста…"));
				_player->fetchLyrics(t);
			}
		} else {
			_lyricsBg = {};
			_lyricsBgScaled = {};
			_lyricsHeader->setText(QStringLiteral("Текст песни"));
			_lyricsView->setStatusText(QStringLiteral("Включите трек, чтобы увидеть текст."));
		}
	}
	update();
	refreshStatus();
}

void VRGMusicWidget::updateLyricsDisplay(qint64 positionMs) {
	if (_lyricsOpen && _lyricsView) {
		_lyricsView->setPosition(positionMs);
	}
}

void VRGMusicWidget::showQueueBox() {
	if (_controller) {
		_controller->show(Box<QueueBox>(_player));
	}
}

void VRGMusicWidget::showEqBox() {
	if (_controller) {
		_controller->show(Box<EqualizerBox>(_player));
	}
}

void VRGMusicWidget::showImportBox(const QString &playlistId) {
	if (!_controller || !_player || !_client) {
		return;
	}
	const auto findPlaylist = [=]() -> const Playlist* {
		for (const auto &pl : _player->playlists()) {
			if (pl.id == playlistId) {
				return &pl;
			}
		}
		return nullptr;
	};
	const auto targetName = [&] {
		if (playlistId.isEmpty()) {
			return QString::fromUtf8("Моя музыка");
		}
		if (const auto pl = findPlaylist()) {
			return pl->name;
		}
		return QString::fromUtf8("Плейлист");
	}();

	const auto normalizeKey = [](const QString &s) {
		QString out;
		out.reserve(s.size());
		for (const auto ch : s.toLower()) {
			if (ch.isLetterOrNumber()) {
				out.append(ch);
			}
		}
		return out;
	};

	const auto isDuplicate = [=](const TrackInfo &candidate) {
		const auto targetTracks = playlistId.isEmpty()
			? _player->library()
			: (findPlaylist()
				? findPlaylist()->tracks
				: QVector<TrackInfo>());
		const auto candFull = normalizeKey(candidate.artist + candidate.title);
		const auto candTitle = normalizeKey(candidate.title);
		for (const auto &ex : targetTracks) {
			if (!ex.id.isEmpty() && ex.id == candidate.id) {
				return true;
			}
			const auto exFull = normalizeKey(ex.artist + ex.title);
			const auto exTitle = normalizeKey(ex.title);
			if (!candFull.isEmpty() && exFull == candFull) {
				return true;
			}
			if (!candTitle.isEmpty() && candTitle.size() >= 5 && exTitle == candTitle) {
				return true;
			}
		}
		return false;
	};

	_controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		box->setTitle(rpl::single(QString::fromUtf8("Импорт в «") + targetName + QString::fromUtf8("»")));

		const auto pickFileBtn = box->addRow(
			object_ptr<Ui::SettingsButton>(
				box,
				rpl::single(QString::fromUtf8("📄 Выбрать .txt файл...")),
				st::settingsButtonNoIcon),
			QMargins(0, 0, 0, 6));

		const auto infoBtn = Ui::CreateChild<Ui::AbstractButton>(pickFileBtn);
		infoBtn->resize(22, 22);
		infoBtn->setCursor(Qt::PointingHandCursor);
		infoBtn->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(infoBtn);
			p.setRenderHint(QPainter::Antialiasing);
			const auto over = infoBtn->isOver();
			const auto color = over ? ThQ(0xff, 0xff, 0xff) : QColor(0x8c, 0x8c, 0x8c);
			p.setPen(QPen(color, 1.5));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(QRectF(infoBtn->rect()).adjusted(2, 2, -2, -2));
			p.setFont(st::semiboldFont);
			p.drawText(infoBtn->rect(), Qt::AlignCenter, u"i"_q);
		}, infoBtn->lifetime());
		pickFileBtn->sizeValue() | rpl::on_next([=](QSize s) {
			infoBtn->moveToRight(16, (s.height() - infoBtn->height()) / 2, s.width());
		}, infoBtn->lifetime());
		infoBtn->setClickedCallback([=] {
			_controller->show(Box([](not_null<Ui::GenericBox*> infoBox) {
				infoBox->setTitle(rpl::single(QString::fromUtf8("Как импортировать")));
				infoBox->addRow(
					object_ptr<Ui::FlatLabel>(
						infoBox,
						rpl::single(QString::fromUtf8(
							"1. Текстом: вставь строки вида «Исполнитель - Название» "
							"(по одному треку на строку).\n\n"
							"2. Файлом: выбери .txt / .csv / .m3u список.\n\n"
							"3. Ссылкой: вставь ссылку на публичный плейлист или альбом "
							"Spotify (open.spotify.com/playlist/...) или SoundCloud "
							"(soundcloud.com/.../sets/...) или YouTube (youtube.com/playlist?list=...).")),
						st::boxLabel),
					st::boxRowPadding);
				infoBox->addButton(rpl::single(QString::fromUtf8("Закрыть")), [=] { infoBox->closeBox(); });
			}));
		});

		const auto input = box->addRow(
			object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				Ui::InputField::Mode::MultiLine,
				rpl::single(QString::fromUtf8("Список треков или ссылка Spotify / SoundCloud")),
				QString()),
			st::boxRowPadding);
		input->setMaxHeight(220);

		const auto statusLbl = box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(QString::fromUtf8("Дубликаты в этом списке будут автоматически пропущены.")),
				st::boxLabel),
			QMargins(24, 8, 24, 8));

		pickFileBtn->setClickedCallback([=] {
			const auto path = QFileDialog::getOpenFileName(
				this,
				QString::fromUtf8("Выберите файл со списком треков"),
				QDir::homePath(),
				QStringLiteral("Text files (*.txt *.csv *.m3u);;All files (*)"));
			if (path.isEmpty()) {
				return;
			}
			QFile f(path);
			if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
				input->setText(QString::fromUtf8(f.readAll()));
				statusLbl->setText(QString::fromUtf8("Файл загружен. Нажми «Импортировать»."));
			}
		});

		struct ImportState {
			QVector<WantedTrack> queries;
			QStringList missing;
			int index = 0;
			int added = 0;
			int skipped = 0;
			int notFound = 0;
			bool running = false;
			bool cancelled = false;
		};
		const auto state = std::make_shared<ImportState>();

		const auto step = std::make_shared<std::function<void()>>();
		const auto weakSelf = QPointer<VRGMusicWidget>(this);
		const auto weakBox = QPointer<Ui::GenericBox>(box.get());
		*step = [=] {
			if (!weakSelf || state->cancelled) {
				return;
			}
			if (state->index >= state->queries.size()) {
				state->running = false;
				const auto summary = QString::fromUtf8(
					"Готово! Добавлено: %1 · Дубликатов пропущено: %2 · Не найдено: %3")
					.arg(state->added)
					.arg(state->skipped)
					.arg(state->notFound);
				if (weakBox) {
					auto text = summary;
					if (!state->missing.isEmpty()) {
						text += QString::fromUtf8("\n\nНе найдены на SoundCloud:\n")
							+ state->missing.mid(0, 12).join('\n');
						if (state->missing.size() > 12) {
							text += QString::fromUtf8("\n… и ещё %1")
								.arg(state->missing.size() - 12);
						}
					}
					statusLbl->setText(text);
				}
				weakSelf->_controller->showToast(summary);
				return;
			}
			const auto q = state->queries[state->index];
			const auto label = q.artist.isEmpty()
				? q.title
				: (q.artist + QStringLiteral(" - ") + q.title);
			if (weakBox) {
				statusLbl->setText(QString::fromUtf8("Поиск %1 / %2: %3")
					.arg(state->index + 1)
					.arg(state->queries.size())
					.arg(label));
			}
			weakSelf->_client->searchBestTrack(q, [=](std::optional<TrackInfo> found) {
				if (!weakSelf || state->cancelled) {
					return;
				}
				if (!found || !found->valid()) {
					++state->notFound;
					state->missing.append(label);
				} else if (isDuplicate(*found)) {
					++state->skipped;
				} else {
					if (playlistId.isEmpty()) {
						weakSelf->_player->addToLibrary(*found);
					} else {
						weakSelf->_player->addTrackToPlaylistAtEnd(playlistId, *found);
					}
					++state->added;
				}
				++state->index;
				(*step)();
			});
		};

		box->addButton(rpl::single(QString::fromUtf8("Импортировать")), [=] {
			if (state->running) {
				return;
			}
			const auto rawText = input->getLastText().trimmed();
			if (rawText.isEmpty()) {
				statusLbl->setText(QString::fromUtf8("Вставь список, ссылку или выбери .txt файл."));
				return;
			}

			if (!rawText.contains('\n')
				&& rawText.contains(QStringLiteral("open.spotify.com/"))) {
				state->running = true;
				state->cancelled = false;
				statusLbl->setText(QString::fromUtf8("Загружаю плейлист Spotify..."));
				weakSelf->_client->fetchSpotifyTracks(rawText, [=](QVector<WantedTrack> tracks, QString err) {
					if (!weakSelf || state->cancelled) {
						return;
					}
					if (!err.isEmpty() || tracks.isEmpty()) {
						state->running = false;
						if (weakBox) {
							statusLbl->setText(err.isEmpty()
								? QString::fromUtf8("Треки не найдены")
								: err);
						}
						return;
					}
					state->queries = tracks;
					state->missing.clear();
					state->index = 0;
					state->added = 0;
					state->skipped = 0;
					state->notFound = 0;
					(*step)();
				});
				return;
			}

			if (!rawText.contains('\n')
				&& (rawText.contains(QStringLiteral("youtube.com/playlist"))
					|| (rawText.contains(QStringLiteral("youtube.com/watch"))
						&& rawText.contains(QStringLiteral("list="))))) {
				state->running = true;
				state->cancelled = false;
				statusLbl->setText(QString::fromUtf8("Загружаю плейлист YouTube..."));
				weakSelf->_client->fetchYoutubePlaylist(rawText, [=](QVector<TrackInfo> tracks, QString err) {
					if (!weakSelf || state->cancelled) {
						return;
					}
					state->running = false;
					if (!err.isEmpty() || tracks.isEmpty()) {
						if (weakBox) {
							statusLbl->setText(err.isEmpty()
								? QString::fromUtf8("Треки не найдены")
								: err);
						}
						return;
					}
					int added = 0;
					int skipped = 0;
					for (const auto &t : tracks) {
						if (isDuplicate(t)) {
							++skipped;
						} else {
							if (playlistId.isEmpty()) {
								weakSelf->_player->addToLibrary(t);
							} else {
								weakSelf->_player->addTrackToPlaylistAtEnd(playlistId, t);
							}
							++added;
						}
					}
					const auto summary = QString::fromUtf8(
						"Готово! Добавлено: %1 · Дубликатов пропущено: %2")
						.arg(added)
						.arg(skipped);
					if (weakBox) {
						statusLbl->setText(summary);
					}
					weakSelf->_controller->showToast(summary);
				});
				return;
			}

			if (!rawText.contains('\n')
				&& rawText.contains(QStringLiteral("soundcloud.com/"))) {
				state->running = true;
				state->cancelled = false;
				statusLbl->setText(QString::fromUtf8("Загружаю плейлист SoundCloud..."));
				weakSelf->_client->resolvePlaylistUrl(rawText, [=](QVector<TrackInfo> tracks, QString err) {
					if (!weakSelf || state->cancelled) {
						return;
					}
					state->running = false;
					if (!err.isEmpty() || tracks.isEmpty()) {
						if (weakBox) {
							statusLbl->setText(err.isEmpty()
								? QString::fromUtf8("Треки не найдены")
								: err);
						}
						return;
					}
					int added = 0;
					int skipped = 0;
					for (const auto &t : tracks) {
						if (isDuplicate(t)) {
							++skipped;
						} else {
							if (playlistId.isEmpty()) {
								weakSelf->_player->addToLibrary(t);
							} else {
								weakSelf->_player->addTrackToPlaylistAtEnd(playlistId, t);
							}
							++added;
						}
					}
					const auto summary = QString::fromUtf8(
						"Готово! Добавлено: %1 · Дубликатов пропущено: %2")
						.arg(added)
						.arg(skipped);
					if (weakBox) {
						statusLbl->setText(summary);
					}
					weakSelf->_controller->showToast(summary);
				});
				return;
			}

			const auto lines = rawText.split('\n', Qt::SkipEmptyParts);
			QVector<WantedTrack> queries;
			static const QRegularExpression prefixRx(QStringLiteral("^\\s*\\d+[\\.)]\\s*"));
			for (auto line : lines) {
				line = line.trimmed();
				if (line.isEmpty() || line.startsWith('#')) {
					continue;
				}
				line.remove(prefixRx);
				line = line.trimmed();
				if (!line.isEmpty()) {
					static const QRegularExpression dash(
						QStringLiteral("\\s[-\u2013\u2014]\\s"));
					const auto m = dash.match(line);
					if (m.hasMatch()) {
						queries.push_back({
							line.left(m.capturedStart()).trimmed(),
							line.mid(m.capturedEnd()).trimmed(),
							0,
						});
					} else {
						queries.push_back({ QString(), line, 0 });
					}
				}
			}
			if (queries.isEmpty()) {
				statusLbl->setText(QString::fromUtf8("Список пустой — вставь строки или выбери .txt файл."));
				return;
			}
			state->queries = queries;
			state->missing.clear();
			state->index = 0;
			state->added = 0;
			state->skipped = 0;
			state->notFound = 0;
			state->running = true;
			state->cancelled = false;
			(*step)();
		});
		box->addLeftButton(rpl::single(QString::fromUtf8("Закрыть")), [=] {
			state->cancelled = true;
			box->closeBox();
		});
	}));
}

void VRGMusicWidget::showError(const QString &message) {
	const auto withLink = message.contains(QStringLiteral("в настройках"));
	_error->setTextFormat(Qt::RichText);
	_error->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
	_error->setText(message.toHtmlEscaped()
		+ (withLink
			? QStringLiteral(" <a href=\"vrg://settings\" style=\"color:#ffffff;\">Открыть настройки</a>")
			: QString()));
	_error->show();
	const auto serial = ++_errorSerial;
	QTimer::singleShot(withLink ? 15000 : 6000, this, [this, serial] {
		if (serial == _errorSerial) {
			_error->hide();
		}
	});
}

void VRGMusicWidget::playFromList(const QModelIndex &index) {
	const auto model = currentModel();
	const auto track = model->track(index.row());
	if (!track.valid()) {
		return;
	}
	if (_player->currentTrack().id == track.id
		&& _player->state() != PlaybackState::Stopped) {
		if (_player->upcoming().isEmpty()) {
			_player->adoptQueueKeepPlaying(model->tracks());
		}
		_player->togglePlayPause();
		return;
	}
	_player->setQueue(model->tracks(), index.row());
}

void VRGMusicWidget::toggleLibrary(const QModelIndex &index) {
	const auto track = currentModel()->track(index.row());
	if (!track.valid()) {
		return;
	}
	if (_tab == Tab::Playlists && !_activePlaylistId.isEmpty()) {
		_player->removeTrackFromPlaylist(_activePlaylistId, track.id);
	} else {
		_player->toggleLibrary(track);
	}
}

void VRGMusicWidget::toggleDownload(const QModelIndex &index) {
	const auto track = currentModel()->track(index.row());
	if (track.valid()) {
		_player->cacheTrack(track);
	}
}

namespace {

QPointer<VRGMusicWidget> CachedMusicWidget;

}

class VRGChatsMiniBar final : public QWidget {
public:
	VRGChatsMiniBar(
		QWidget *parent,
		Window::SessionController *controller)
	: QWidget(parent)
	, _controller(controller)
	, _player(SharedPlayer())
	, _client(SharedClient()) {
		EnsureThemeHook(this);
		setFixedHeight(64);
		setMouseTracking(true);
		setCursor(Qt::PointingHandCursor);

		_miniSeek = new SmoothSlider(this);
		_miniSeek->setFixedHeight(8);
		_miniSeek->setRange(0, 0);

		_art = new QLabel(this);
		_art->setFixedSize(36, 36);

		_info = new MarqueeLabel(this);
		_timeLabel = new QLabel(this);
		SetQss(_timeLabel, QStringLiteral("color: rgba(255,255,255,135); font-size: 11px; background: transparent;"));

		const auto makeBtn = [&](Glyph g) {
			const auto b = new QToolButton(this);
			b->setFixedSize(28, 28);
			b->setIconSize(QSize(15, 15));
			b->setIcon(GlyphIcon(g, QColor(0xff, 0xff, 0xff), 15));
			b->setCursor(Qt::PointingHandCursor);
			SetQss(b, QStringLiteral(
				"QToolButton { background: transparent; border: none; border-radius: 14px; }"
				"QToolButton:hover { background: rgba(255,255,255,24); }"));
			return b;
		};
		_prev = makeBtn(Glyph::Prev);
		_play = new QToolButton(this);
		_play->setFixedSize(28, 28);
		_play->setIconSize(QSize(14, 14));
		_play->setIcon(GlyphIcon(Glyph::Play, QColor(0x00, 0x00, 0x00), 14));
		_play->setCursor(Qt::PointingHandCursor);
		SetQss(_play, QStringLiteral(
			"QToolButton { background: #ffffff; border: none; border-radius: 14px; }"
			"QToolButton:hover { background: rgba(255,255,255,225); }"));
		_next = makeBtn(Glyph::Next);

		_row = new QHBoxLayout();
		_row->setContentsMargins(14, 7, 12, 2);
		_row->setSpacing(8);
		_row->addWidget(_art);
		_row->addWidget(_info, 1);
		_row->addWidget(_timeLabel);
		_row->addWidget(_prev);
		_row->addWidget(_play);
		_row->addWidget(_next);

		const auto seekWrap = new QHBoxLayout();
		seekWrap->setContentsMargins(14, 0, 26, 5);
		seekWrap->addWidget(_miniSeek);

		const auto root = new QVBoxLayout(this);
		root->setContentsMargins(0, 0, 0, 0);
		root->setSpacing(0);
		root->addLayout(_row, 1);
		root->addLayout(seekWrap);

		_close = new QToolButton(this);
		_close->setFixedSize(16, 16);
		_close->setCursor(Qt::PointingHandCursor);
		_close->setFocusPolicy(Qt::NoFocus);
		_close->setText(QString::fromUtf8("\u00d7"));
		_close->setToolTip(QString::fromUtf8("Скрыть мини-плеер (включить обратно: настройки плеера)"));
		SetQss(_close, QStringLiteral(
			"QToolButton { background: rgba(0,0,0,120); color: white; border: none;"
			" border-radius: 8px; font-size: 13px; padding-bottom: 1px; }"
			"QToolButton:hover { background: rgba(255,255,255,70); }"));
		_close->hide();
		connect(_close, &QToolButton::clicked, this, [this] {
			Vrg::Conf::SetBool("player_minibar", false);
			_player->notifyVisualSettingsChanged();
			if (const auto p = parentWidget()) {
				QResizeEvent re(p->size(), p->size());
				QCoreApplication::sendEvent(p, &re);
			}
		});

		connect(_prev, &QToolButton::clicked, _player, &Player::prev);
		connect(_play, &QToolButton::clicked, _player, &Player::togglePlayPause);
		connect(_next, &QToolButton::clicked, _player, &Player::next);
		connect(_miniSeek, &SmoothSlider::sliderReleased, _player, &Player::seek);

		connect(_player, &Player::currentTrackChanged, this, &VRGChatsMiniBar::refresh);
		connect(_player, &Player::playbackStateChanged, this, &VRGChatsMiniBar::refresh);
		connect(_player, &Player::visualSettingsChanged, this, &VRGChatsMiniBar::applyVisualSettings);
		connect(_player, &Player::durationChanged, this, [this](qint64 d) {
			_miniSeek->setRange(0, std::max(d, qint64(0)));
			updateTimeLabel(_player->position(), d);
		});
		connect(_player, &Player::positionChanged, this, [this](qint64 p) {
			_miniSeek->setValue(p);
			updateTimeLabel(p, _player->duration());
		});
		connect(_client, &SoundCloudClient::artworkLoaded, this, [this](const QString &url) {
			if (url == _player->currentTrack().artworkUrl) {
				refresh();
			}
		});
		refresh();
	}

protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);

		const auto pill = QRectF(rect()).adjusted(6, 3, -6, -3);
		QPainterPath clip;
		clip.addRoundedRect(pill, 13, 13);
		p.setClipPath(clip);

		QLinearGradient grad(pill.left(), 0, pill.right(), 0);
		if (Vrg::Conf::Str("player_minibar_style") == QStringLiteral("minimal")) {
			p.fillRect(pill, ThQ(0x12, 0x12, 0x16));
		} else {
			grad.setColorAt(0.0, AccentStart(_accent));
			grad.setColorAt(0.55, AccentBg(_accent));
			grad.setColorAt(1.0, ThQ(0x0c, 0x0c, 0x0e));
			p.fillRect(pill, grad);
		}

		p.setClipping(false);
		p.setPen(QPen(ThQ(255, 255, 255, 24), 1.0));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(pill, 13, 13);

		const auto grip = gripRect();
		p.setPen(QPen(ThQ(255, 255, 255, 110), 1.4, Qt::SolidLine, Qt::RoundCap));
		for (const auto d : { 4, 8, 12 }) {
			p.drawLine(
				QPointF(grip.right() - d + 1, grip.bottom() - 2),
				QPointF(grip.right() - 2, grip.bottom() - d + 1));
		}
	}

	void enterEvent(QEnterEvent *e) override {
		QWidget::enterEvent(e);
		if (_close) {
			_close->move(width() - 6 - 16 - 2, 5);
			_close->show();
			_close->raise();
		}
	}

	void leaveEvent(QEvent *e) override {
		QWidget::leaveEvent(e);
		if (_close) {
			_close->hide();
		}
	}

	void resizeEvent(QResizeEvent *e) override {
		QWidget::resizeEvent(e);
		if (_close) {
			_close->move(width() - 6 - 16 - 2, 5);
		}
		if (e->size().width() != e->oldSize().width() && !_fitting) {
			_fitting = true;
			applyVisualSettings();
			_fitting = false;
		}
	}

	void wheelEvent(QWheelEvent *e) override {
		if (!(e->modifiers() & Qt::ControlModifier)) {
			e->ignore();
			return;
		}
		const auto current = std::clamp(Vrg::Conf::Int("player_mini_scale", 100), 100, 150);
		const auto next = std::clamp(current + (e->angleDelta().y() > 0 ? 5 : -5), 100, 150);
		if (next != current) {
			Vrg::Conf::SetInt("player_mini_scale", next);
			_player->notifyVisualSettingsChanged();
		}
		e->accept();
	}

	void contextMenuEvent(QContextMenuEvent *e) override {
		const auto track = _player->currentTrack();
		if (!_controller || !track.valid()) {
			return;
		}
		_menu = new Ui::PopupMenu(this, st::popupMenuWithIcons);
		const auto thread = _controller->activeChatCurrent().thread();
		if (thread) {
			const auto weak = base::make_weak(thread);
			_menu->addAction(QStringLiteral("Отправить в текущий чат"), [=] {
				if (const auto strong = weak.get()) {
					SendTrackToThread(_controller, strong, track, this);
				}
			});
		}
		_menu->addAction(QStringLiteral("Выбрать чат…"), [=] {
			Window::ShowChooseRecipientBox(_controller, [=](
					not_null<Data::Thread*> chosen) {
				SendTrackToThread(_controller, chosen, track, this);
				return true;
			});
		});
		_menu->popup(e->globalPos());
	}

	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton && gripRect().contains(e->pos())) {
			_resizing = true;
			_resizeStartScale = _scale;
			_resizeStartY = e->globalPosition().toPoint().y();
			_resizeStartX = e->globalPosition().toPoint().x();
			_resizeStartH = std::max(height(), 1);
			_resizeStartW = std::max(width(), 1);
			return;
		}
		if (e->button() == Qt::LeftButton
			&& Vrg::Conf::Bool("player_minibar_draggable", false)) {
			_dragging = true;
			_dragMoved = false;
			_pressGlobalPos = e->globalPosition().toPoint();
			_lastGlobalPos = _pressGlobalPos;
			return;
		}
		QWidget::mousePressEvent(e);
	}

	void mouseMoveEvent(QMouseEvent *e) override {
		if (_resizing) {
			const auto cur = e->globalPosition().toPoint();
			const auto dy = cur.y() - _resizeStartY;
			const auto dx = cur.x() - _resizeStartX;
			const auto scale = _resizeStartScale * double(_resizeStartH + dy) / _resizeStartH;
			const auto percent = std::clamp(int(std::lround(scale * 100.)), 100, 150);
			const auto maxW = parentWidget() ? std::max(parentWidget()->width() - 16, 220) : 900;
			const auto newW = std::clamp(_resizeStartW + dx, 220, maxW);
			auto changed = false;
			if (percent != Vrg::Conf::Int("player_mini_scale", 100)) {
				Vrg::Conf::SetInt("player_mini_scale", percent);
				changed = true;
			}
			if (newW != Vrg::Conf::Int("player_mini_width", 340)) {
				Vrg::Conf::SetInt("player_mini_width", newW);
				changed = true;
			}
			if (changed) {
				applyVisualSettings();
				if (const auto p = parentWidget()) {
					QResizeEvent re(p->size(), p->size());
					QCoreApplication::sendEvent(p, &re);
				}
			}
			return;
		}
		if (!_dragging) {
			setCursor(gripRect().contains(e->pos())
				? Qt::SizeFDiagCursor
				: Qt::PointingHandCursor);
			QWidget::mouseMoveEvent(e);
			return;
		}
		const auto cur = e->globalPosition().toPoint();
		if (!_dragMoved && (cur - _pressGlobalPos).manhattanLength() > 4) {
			_dragMoved = true;
			setCursor(Qt::ClosedHandCursor);
		}
		if (_dragMoved) {
			if (const auto p = parentWidget()) {
				const auto delta = cur - _lastGlobalPos;
				const auto maxX = std::max(4, p->width() - width() - 4);
				const auto maxY = std::max(4, p->height() - height() - 4);
				const auto nx = std::clamp(x() + delta.x(), 4, maxX);
				const auto ny = std::clamp(y() + delta.y(), 4, maxY);
				move(nx, ny);
				raise();
				Vrg::Conf::SetInt("player_minibar_x", nx);
				Vrg::Conf::SetInt("player_minibar_y", ny);
			}
		}
		_lastGlobalPos = cur;
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		if (_resizing && e->button() == Qt::LeftButton) {
			_resizing = false;
			_player->notifyVisualSettingsChanged();
			return;
		}
		if (_dragging && e->button() == Qt::LeftButton) {
			const auto wasMoved = _dragMoved;
			_dragging = false;
			_dragMoved = false;
			setCursor(Qt::PointingHandCursor);
			if (wasMoved) {
				return;
			}
		}
		if (e->button() == Qt::LeftButton && _controller) {
			_controller->showSection(
				std::make_shared<SectionMemento>(),
				Window::SectionShow(
					Window::SectionShow::Way::Forward,
					anim::type::instant,
					anim::activation::normal));
		}
	}

private:
	[[nodiscard]] QRect gripRect() const {
		return QRect(width() - 6 - 16, height() - 3 - 16, 16, 16);
	}

	void applyVisualSettings() {
		const auto t = _player->currentTrack();
		const auto bigOpen = CachedMusicWidget && CachedMusicWidget->isVisible();
		const auto enabled = Vrg::Conf::Bool("player_minibar", true);
		setVisible(enabled && t.valid() && !bigOpen);
		const auto oldH = height();
		const auto compact = (Vrg::Conf::Str("player_minibar_style") == QStringLiteral("compact"));
		const auto tk = std::clamp(Vrg::Conf::Int("player_mini_text", 100), 100, 160) / 100.;
		const auto barWidth = std::clamp(
			Vrg::Conf::Int("player_mini_width", 340),
			220,
			parentWidget() ? std::max(parentWidget()->width() - 16, 220) : 900);
		const auto avail = barWidth - 12;
		const auto wantK = std::clamp(Vrg::Conf::Int("player_mini_scale", 100), 100, 150) / 100.;
		const auto hideArt = (wantK * 178. + 60. > avail);
		const auto perK = hideArt ? 134. : 178.;
		const auto k = hideArt
			? std::min(wantK, std::max((avail - 60.) / perK, 1.0))
			: wantK;
		const auto timeFits = (k * perK + 30. + 80. * tk <= avail);
		_scale = k;
		const auto sc = [k](int v) {
			return std::max(int(std::lround(v * k)), 1);
		};
		const auto newH = sc(compact ? 50 : 64);
		setFixedHeight(newH);
		if (oldH != newH) {
			if (const auto p = parentWidget()) {
				QResizeEvent re(p->size(), p->size());
				QCoreApplication::sendEvent(p, &re);
			}
		}
		if (_art) {
			_art->setVisible(!hideArt && Vrg::Conf::Bool("player_mini_art", true));
			_art->setFixedSize(sc(compact ? 30 : 36), sc(compact ? 30 : 36));
			const auto pix = _client->cachedArtwork(_player->currentTrack().artworkUrl);
			if (!pix.isNull()) {
				_art->setPixmap(RoundedArtwork(pix, _art->width(), sc(9), std::max(devicePixelRatioF(), 1.) * MusicZoomFactor()));
			}
		}
		const auto radius = sc(28) / 2;
		const auto ghostStyle = QStringLiteral(
			"QToolButton { background: transparent; border: none; border-radius: %1px; }"
			"QToolButton:hover { background: rgba(255,255,255,24); }").arg(radius);
		const auto playStyle = QStringLiteral(
			"QToolButton { background: #ffffff; border: none; border-radius: %1px; }"
			"QToolButton:hover { background: rgba(255,255,255,225); }").arg(radius);
		for (const auto b : { _prev, _next }) {
			if (b) {
				b->setFixedSize(sc(28), sc(28));
				b->setIconSize(QSize(sc(15), sc(15)));
				SetQss(b, ghostStyle);
			}
		}
		if (_prev) {
			_prev->setIcon(GlyphIcon(Glyph::Prev, QColor(0xff, 0xff, 0xff), sc(15)));
		}
		if (_next) {
			_next->setIcon(GlyphIcon(Glyph::Next, QColor(0xff, 0xff, 0xff), sc(15)));
		}
		if (_play) {
			_play->setFixedSize(sc(28), sc(28));
			_play->setIconSize(QSize(sc(14), sc(14)));
			SetQss(_play, playStyle);
			const auto playing = (_player->state() == PlaybackState::Playing)
				|| (_player->state() == PlaybackState::Loading);
			_play->setIcon(GlyphIcon(playing ? Glyph::Pause : Glyph::Play, QColor(0x00, 0x00, 0x00), sc(14)));
		}
		if (_timeLabel) {
			SetQss(_timeLabel, QStringLiteral(
				"color: rgba(255,255,255,135); font-size: %1px; background: transparent;")
				.arg(std::max(int(std::lround(11 * tk)), 6)));
		}
		if (_info) {
			_info->setTextScale(tk);
		}
		if (_miniSeek) {
			_miniSeek->setFixedHeight(sc(8));
		}
		if (_row) {
			_row->setSpacing(sc(8));
			_row->setContentsMargins(sc(14), sc(7), sc(12), sc(2));
		}
		if (_timeLabel) {
			_timeLabel->setVisible(timeFits && Vrg::Conf::Bool("player_mini_time", true));
		}
		if (_miniSeek) {
			_miniSeek->setVisible(!compact && Vrg::Conf::Bool("player_mini_seek", true));
		}
		const auto showSkip = Vrg::Conf::Bool("player_mini_skip", true);
		if (_prev) {
			_prev->setVisible(showSkip);
		}
		if (_next) {
			_next->setVisible(showSkip);
		}
		if (_row) {
			while (_row->count() > 0) {
				_row->takeAt(0);
			}
			const auto flipped = (Vrg::Conf::Str("player_mini_layout") == QStringLiteral("flipped"));
			if (flipped) {
				_row->addWidget(_prev);
				_row->addWidget(_play);
				_row->addWidget(_next);
				_row->addWidget(_timeLabel);
				_row->addWidget(_info, 1);
				_row->addWidget(_art);
			} else {
				_row->addWidget(_art);
				_row->addWidget(_info, 1);
				_row->addWidget(_timeLabel);
				_row->addWidget(_prev);
				_row->addWidget(_play);
				_row->addWidget(_next);
			}
		}
		if (_info) {
			_info->refreshSettings();
		}
		update();
	}

	void refresh() {
		const auto t = _player->currentTrack();
		const auto bigOpen = CachedMusicWidget && CachedMusicWidget->isVisible();
		setVisible(Vrg::Conf::Bool("player_minibar", true) && t.valid() && !bigOpen);
		if (!t.valid()) {
			return;
		}
		_info->setText(t.title, SourceLabel(t) + t.artist);
		const auto pix = _client->cachedArtwork(t.artworkUrl);
		if (pix.isNull()) {
			_client->fetchArtwork(t.artworkUrl);
		}
		_accent = ExtractDominantColor(pix);
		const auto artSize = _art->width();
		_art->setPixmap(RoundedArtwork(pix, artSize, 9, std::max(devicePixelRatioF(), 1.) * MusicZoomFactor()));
		const auto playing = (_player->state() == PlaybackState::Playing)
			|| (_player->state() == PlaybackState::Loading);
		_play->setIcon(GlyphIcon(playing ? Glyph::Pause : Glyph::Play, QColor(0x00, 0x00, 0x00), std::max(int(std::lround(14 * _scale)), 1)));
		_miniSeek->setRange(0, std::max(_player->duration(), qint64(0)));
		_miniSeek->setValue(_player->position());
		updateTimeLabel(_player->position(), _player->duration());
		applyVisualSettings();
	}

	void updateTimeLabel(qint64 posMs, qint64 durMs) {
		if (!_timeLabel) {
			return;
		}
		_timeLabel->setText(
			FormatTime(posMs) + QStringLiteral(" / ") + FormatTime(durMs));
	}

	Window::SessionController * const _controller;
	Player * const _player;
	SoundCloudClient * const _client;
	QPointer<Ui::PopupMenu> _menu;
	SmoothSlider *_miniSeek = nullptr;
	QColor _accent = QColor(0x11, 0x11, 0x13);
	QLabel *_art = nullptr;
	MarqueeLabel *_info = nullptr;
	QHBoxLayout *_row = nullptr;
	QLabel *_timeLabel = nullptr;
	QToolButton *_prev = nullptr;
	QToolButton *_play = nullptr;
	QToolButton *_next = nullptr;
	QToolButton *_close = nullptr;
	double _scale = 1.;
	bool _fitting = false;
	bool _resizing = false;
	double _resizeStartScale = 1.;
	int _resizeStartY = 0;
	int _resizeStartX = 0;
	int _resizeStartH = 1;
	int _resizeStartW = 1;
	bool _dragging = false;
	bool _dragMoved = false;
	QPoint _pressGlobalPos;
	QPoint _lastGlobalPos;

};

VRGMusicWidget *AcquireMusicWidget(
		Window::SessionController *controller,
		QWidget *parent) {
	if (CachedMusicWidget && CachedMusicWidget->controller() != controller) {
		if (!CachedMusicWidget->parentWidget()) {
			delete CachedMusicWidget.data();
		}
		CachedMusicWidget = nullptr;
	}
	if (!CachedMusicWidget) {
		CachedMusicWidget = new VRGMusicWidget(controller, parent);
		QObject::connect(qApp, &QCoreApplication::aboutToQuit, [] {
			if (CachedMusicWidget && !CachedMusicWidget->parentWidget()) {
				delete CachedMusicWidget.data();
			}
		});
	} else {
		CachedMusicWidget->setParent(parent);
	}
	return CachedMusicWidget.data();
}

void ReleaseMusicWidget(VRGMusicWidget *widget) {
	if (!widget) {
		return;
	}
	widget->hide();
	widget->setParent(nullptr);
}

QWidget *CreateChatsMiniBar(
		QWidget *parent,
		Window::SessionController *controller) {
	return new VRGChatsMiniBar(parent, controller);
}

}

#include "vrg_music_widget.moc"
