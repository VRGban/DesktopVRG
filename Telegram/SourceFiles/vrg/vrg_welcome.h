/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "base/call_delayed.h"
#include "core/application.h"
#include "mainwindow.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "vrg/vrg_conf.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_preview.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"
#include "styles/style_chat.h"

#include <QtCore/QFile>
#include <QtGui/QFontDatabase>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainterPath>

namespace Vrg {

inline void ApplyVrgTheme() {
	auto file = QFile(u":/gui/vrg.tdesktop-theme"_q);
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto bytes = file.readAll();
	if (auto preview = Window::Theme::PreviewFromFile(
			bytes,
			u"vrg.tdesktop-theme"_q,
			{})) {
		if (Window::Theme::Apply(std::move(preview))) {
			Window::Theme::KeepApplied();
		}
	}
}

inline void ApplyVrgThemeOnce() {
	if (Conf::Bool("vrg_theme_once", false)) {
		return;
	}
	Conf::SetBool("vrg_theme_once", true);
	if (Conf::Bool("welcome_done", false)) {
		return;
	}
	ApplyVrgTheme();
}

[[nodiscard]] inline QString FontFamily(const QString &chosen) {
	if (!chosen.isEmpty() || !Conf::Bool("font_inter", true)) {
		return chosen;
	}
	QFontDatabase::addApplicationFont(u":/gui/fonts/InterVariable.ttf"_q);
	const auto families = QFontDatabase::families();
	return families.contains(u"Inter"_q)
		? u"Inter"_q
		: families.contains(u"Inter Variable"_q)
		? u"Inter Variable"_q
		: chosen;
}

inline void ApplyDevThemeIfSet() {
	const auto path = Conf::Str("theme_dev_path");
	if (path.isEmpty() || !QFile::exists(path)) {
		return;
	}
	if (auto preview = Window::Theme::PreviewFromFile({}, path, {})) {
		if (Window::Theme::Apply(std::move(preview))) {
			Window::Theme::KeepApplied();
		}
	}
}

class Welcome final : public Ui::RpWidget {
public:
	explicit Welcome(QWidget *parent) : RpWidget(parent) {
		setMouseTracking(true);
		setFocusPolicy(Qt::StrongFocus);
		_appear.start([=] { update(); }, 0., 1., 350, anim::easeOutCirc);
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::TextAntialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.fillRect(rect(), Qt::black);

		layout();
		const auto shown = _appear.value(1.);
		paintChrome(p);
		p.save();
		p.setOpacity(shown);
		p.translate(0, int((1. - shown) * 24));
		switch (_slide) {
		case 0: paintHero(p); break;
		case 1: paintLook(p); break;
		case 2: paintGhost(p); break;
		default: paintDone(p); break;
		}
		p.restore();
		paintPill(p, _button, buttonText(), _over == Hit::Button);
	}

	void mouseMoveEvent(QMouseEvent *e) override {
		const auto now = hitTest(e->pos());
		if (_over != now) {
			_over = now;
			setCursor((now == Hit::None) ? style::cur_default : style::cur_pointer);
			update();
		}
	}

	void mousePressEvent(QMouseEvent *e) override {
		switch (hitTest(e->pos())) {
		case Hit::Button: next(); break;
		case Hit::Skip: finish(); break;
		case Hit::CardVrg: _vrgTheme = true; update(); break;
		case Hit::CardKeep: _vrgTheme = false; update(); break;
		case Hit::Ghost: _ghost = !_ghost; update(); break;
		case Hit::Dot0: go(0); break;
		case Hit::Dot1: go(1); break;
		case Hit::Dot2: go(2); break;
		case Hit::Dot3: go(3); break;
		case Hit::None: break;
		}
	}

	void keyPressEvent(QKeyEvent *e) override {
		switch (e->key()) {
		case Qt::Key_Return:
		case Qt::Key_Enter:
		case Qt::Key_Right:
		case Qt::Key_Down: next(); break;
		case Qt::Key_Left:
		case Qt::Key_Up: go(std::max(_slide - 1, 0)); break;
		case Qt::Key_Escape: finish(); break;
		default: RpWidget::keyPressEvent(e);
		}
	}

private:
	enum class Hit {
		None, Button, Skip, CardVrg, CardKeep, Ghost,
		Dot0, Dot1, Dot2, Dot3,
	};
	static constexpr auto kSlides = 4;

	[[nodiscard]] static QString Ru(const char *text) {
		return QString::fromUtf8(text);
	}

	[[nodiscard]] QFont font(int px, int weight) const {
		auto result = QFont(st::normalFont->f.family());
		result.setPixelSize(px);
		result.setWeight(QFont::Weight(weight));
		return result;
	}

	[[nodiscard]] int heroSize() const {
		return std::clamp(width() / 9, 40, 104);
	}

	void layout() {
		const auto w = width();
		const auto h = height();
		_column = QRect((w - std::min(w - 64, 760)) / 2, 0, std::min(w - 64, 760), h);
		const auto bw = 240;
		const auto bh = 56;
		_button = QRect((w - bw) / 2, h - 150, bw, bh);
		const auto skipFont = QFontMetrics(font(15, QFont::DemiBold));
		const auto skipW = skipFont.horizontalAdvance(Ru("Пропустить"));
		_skip = (_slide < kSlides - 1)
			? QRect(w - 32 - skipW, 28, skipW, 24)
			: QRect();
		const auto dotsW = kSlides * 10 + (kSlides - 1) * 12;
		for (auto i = 0; i != kSlides; ++i) {
			_dots[i] = QRect(
				(w - dotsW) / 2 + i * 22 - 6,
				h - 66 - 6,
				22,
				22);
		}
		const auto cardW = std::min((_column.width() - 24) / 2, 320);
		const auto cardH = 220;
		const auto cardsTop = h / 2 - cardH / 2 + 30;
		_cardVrg = QRect(w / 2 - 12 - cardW, cardsTop, cardW, cardH);
		_cardKeep = QRect(w / 2 + 12, cardsTop, cardW, cardH);
		_ghostRect = QRect(_column.x(), h / 2 - 20, _column.width(), 110);
	}

	[[nodiscard]] Hit hitTest(QPoint point) {
		layout();
		if (_button.contains(point)) {
			return Hit::Button;
		} else if (_skip.contains(point)) {
			return Hit::Skip;
		} else if (_slide == 1 && _cardVrg.contains(point)) {
			return Hit::CardVrg;
		} else if (_slide == 1 && _cardKeep.contains(point)) {
			return Hit::CardKeep;
		} else if (_slide == 2 && _ghostRect.contains(point)) {
			return Hit::Ghost;
		}
		const Hit dots[] = { Hit::Dot0, Hit::Dot1, Hit::Dot2, Hit::Dot3 };
		for (auto i = 0; i != kSlides; ++i) {
			if (_dots[i].contains(point)) {
				return dots[i];
			}
		}
		return Hit::None;
	}

	[[nodiscard]] QString buttonText() const {
		switch (_slide) {
		case 0: return Ru("Начать");
		case kSlides - 1: return Ru("Поехали");
		default: return Ru("Дальше");
		}
	}

	void go(int slide) {
		if (slide == _slide || slide < 0 || slide >= kSlides) {
			return;
		}
		_slide = slide;
		_over = Hit::None;
		_appear.start([=] { update(); }, 0., 1., 350, anim::easeOutCirc);
		update();
	}

	void next() {
		if (_slide == kSlides - 1) {
			finish();
		} else {
			go(_slide + 1);
		}
	}

	void finish() {
		Conf::SetBool("welcome_done", true);
		if (_ghost) {
			for (const auto key : {
				"ghost_read",
				"ghost_typing",
				"ghost_online",
				"ghost_offline_send",
				"ghost_stories",
			}) {
				Conf::SetBool(key, true);
			}
		}
		const auto theme = _vrgTheme && _visitedLook;
		hide();
		deleteLater();
		if (theme) {
			ApplyVrgTheme();
		}
	}

	void paintChrome(QPainter &p) {
		const auto size = 34;
		const auto logoRect = QRect(32, 23, size, size);
		auto circle = QPainterPath();
		circle.addEllipse(logoRect);
		p.save();
		p.setClipPath(circle);
		p.drawImage(logoRect, _logo);
		p.restore();
		p.setPen(Qt::white);
		p.setFont(font(17, QFont::Black));
		p.drawText(
			QRect(logoRect.right() + 12, 23, 120, size),
			Qt::AlignLeft | Qt::AlignVCenter,
			u"VRG"_q);

		if (!_skip.isEmpty()) {
			p.setFont(font(15, QFont::DemiBold));
			p.setPen((_over == Hit::Skip) ? Qt::white : QColor(0x8c, 0x8c, 0x8c));
			p.drawText(_skip, Qt::AlignRight | Qt::AlignVCenter, Ru("Пропустить"));
		}

		for (auto i = 0; i != kSlides; ++i) {
			const auto c = _dots[i].center();
			const auto active = (i == _slide);
			p.setPen(Qt::NoPen);
			p.setBrush(active ? QColor(Qt::white) : QColor(0x3a, 0x3a, 0x3a));
			const auto r = active ? 5 : 4;
			p.drawEllipse(QPointF(c), r, r);
		}
	}

	int paintSolid(QPainter &p, const QString &text, int top, const QFont &f) {
		p.setFont(f);
		p.setPen(Qt::white);
		const auto m = QFontMetrics(f);
		p.drawText(QRect(0, top, width(), m.height()), Qt::AlignHCenter, text);
		return m.height();
	}

	int paintOutline(QPainter &p, const QString &text, int top, const QFont &f) {
		const auto m = QFontMetrics(f);
		const auto x = (width() - m.horizontalAdvance(text)) / 2;
		auto path = QPainterPath();
		path.addText(x, top + m.ascent(), f, text);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(Qt::white, std::max(2, f.pixelSize() / 36)));
		p.drawPath(path);
		return m.height();
	}

	int paintMuted(QPainter &p, const QString &text, int top, int px = 18) {
		const auto f = font(px, QFont::Normal);
		p.setFont(f);
		p.setPen(QColor(0x8c, 0x8c, 0x8c));
		const auto r = QRect(_column.x(), top, _column.width(), 200);
		auto bounds = QRect();
		p.drawText(r, Qt::AlignHCenter | Qt::TextWordWrap, text, &bounds);
		return bounds.height();
	}

	void paintLabel(QPainter &p, const QString &text, int top) {
		p.setFont(font(13, QFont::Bold));
		p.setPen(QColor(0x8c, 0x8c, 0x8c));
		p.drawText(QRect(0, top, width(), 20), Qt::AlignHCenter, text);
	}

	void paintHero(QPainter &p) {
		const auto f = font(heroSize(), QFont::Black);
		const auto lineH = QFontMetrics(f).height();
		auto top = height() / 2 - lineH - 70;
		top += paintSolid(p, u"Telegram,"_q, top, f) - lineH / 8;
		top += paintOutline(p, Ru("как надо."), top, f) + 28;
		paintMuted(
			p,
			Ru("VRG — клиент для Telegram. Открытый код, "
				"свой плеер, режим призрака."),
			top);
	}

	void paintLook(QPainter &p) {
		_visitedLook = true;
		const auto f = font(std::clamp(width() / 16, 30, 56), QFont::Black);
		auto top = _cardVrg.top() - 150;
		paintLabel(p, Ru("01 — ОФОРМЛЕНИЕ"), top);
		top += 32;
		paintSolid(p, Ru("Выберите вид."), top, f);

		paintCard(p, _cardVrg, _vrgTheme, Ru("Чёрно-белая VRG"), true);
		paintCard(p, _cardKeep, !_vrgTheme, Ru("Как сейчас"), false);
	}

	void paintCard(
			QPainter &p,
			QRect rect,
			bool selected,
			const QString &title,
			bool vrg) {
		const auto hovered = (_over == (vrg ? Hit::CardVrg : Hit::CardKeep));
		p.setPen(selected
			? QPen(Qt::white, 2)
			: QPen(hovered ? QColor(0x6a, 0x6a, 0x6a) : QColor(0x2a, 0x2a, 0x2a), 1));
		p.setBrush(QColor(0x0b, 0x0b, 0x0c));
		p.drawRoundedRect(rect, 18, 18);

		const auto inner = rect.adjusted(20, 20, -20, -64);
		const auto bg = vrg ? QColor(Qt::black) : st::windowBg->c;
		const auto in = vrg ? QColor(0x14, 0x14, 0x16) : st::msgInBg->c;
		const auto out = vrg ? QColor(0x2a, 0x2a, 0x2e) : st::msgOutBg->c;
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(inner, 10, 10);
		const auto bw = inner.width() * 6 / 10;
		p.setBrush(in);
		p.drawRoundedRect(QRect(inner.x() + 12, inner.y() + 14, bw, 26), 13, 13);
		p.setBrush(out);
		p.drawRoundedRect(
			QRect(inner.right() - 12 - bw * 8 / 10, inner.y() + 50, bw * 8 / 10, 26),
			13,
			13);
		p.setBrush(in);
		p.drawRoundedRect(QRect(inner.x() + 12, inner.y() + 86, bw * 7 / 10, 26), 13, 13);

		p.setPen(Qt::white);
		p.setFont(font(17, QFont::Bold));
		p.drawText(
			QRect(rect.x() + 20, rect.bottom() - 52, rect.width() - 60, 40),
			Qt::AlignLeft | Qt::AlignVCenter,
			title);
		const auto check = QRect(rect.right() - 44, rect.bottom() - 44, 24, 24);
		p.setPen(QPen(selected ? Qt::white : QColor(0x4a, 0x4a, 0x4a), 2));
		p.setBrush(selected ? QColor(Qt::white) : QColor(Qt::transparent));
		p.drawEllipse(check);
		if (selected) {
			p.setPen(QPen(Qt::black, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			auto tick = QPainterPath();
			tick.moveTo(check.x() + 6.5, check.y() + 12.5);
			tick.lineTo(check.x() + 10.5, check.y() + 16.5);
			tick.lineTo(check.x() + 17.5, check.y() + 8.5);
			p.setBrush(Qt::NoBrush);
			p.drawPath(tick);
		}
	}

	void paintGhost(QPainter &p) {
		const auto f = font(std::clamp(width() / 16, 30, 56), QFont::Black);
		auto top = _ghostRect.top() - 150;
		paintLabel(p, Ru("02 — ПРИЗРАК"), top);
		top += 32;
		paintSolid(p, Ru("Вас не видно."), top, f);

		const auto r = _ghostRect;
		const auto hovered = (_over == Hit::Ghost);
		p.setPen(_ghost
			? QPen(Qt::white, 2)
			: QPen(hovered ? QColor(0x6a, 0x6a, 0x6a) : QColor(0x2a, 0x2a, 0x2a), 1));
		p.setBrush(QColor(0x0b, 0x0b, 0x0c));
		p.drawRoundedRect(r, 18, 18);

		p.setPen(Qt::white);
		p.setFont(font(19, QFont::Bold));
		p.drawText(
			QRect(r.x() + 24, r.y() + 18, r.width() - 140, 28),
			Qt::AlignLeft | Qt::AlignVCenter,
			Ru("Полный призрак"));
		p.setPen(QColor(0x8c, 0x8c, 0x8c));
		p.setFont(font(14, QFont::Normal));
		p.drawText(
			QRect(r.x() + 24, r.y() + 50, r.width() - 140, 48),
			Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
			Ru("Не отмечать прочитанным, скрыть «печатает» и «в сети», "
				"смотреть истории незаметно."));

		const auto sw = QRect(r.right() - 24 - 64, r.center().y() - 18, 64, 36);
		p.setPen(Qt::NoPen);
		p.setBrush(_ghost ? QColor(Qt::white) : QColor(0x2a, 0x2a, 0x2a));
		p.drawRoundedRect(sw, 18, 18);
		p.setBrush(_ghost ? QColor(Qt::black) : QColor(0x8c, 0x8c, 0x8c));
		const auto knob = _ghost
			? QRect(sw.right() - 31, sw.y() + 5, 26, 26)
			: QRect(sw.x() + 5, sw.y() + 5, 26, 26);
		p.drawEllipse(knob);

		paintMuted(
			p,
			Ru("Отдельные пункты можно включить позже в «Настройки VRG»."),
			r.bottom() + 24,
			15);
	}

	void paintDone(QPainter &p) {
		const auto f = font(heroSize(), QFont::Black);
		const auto lineH = QFontMetrics(f).height();
		auto top = height() / 2 - lineH - 20;
		top += paintOutline(p, Ru("Готово."), top, f) + 24;
		paintMuted(
			p,
			Ru("Всё меняется в «Настройки VRG». Новости — в @VRGclient."),
			top);
	}

	void paintPill(QPainter &p, QRect rect, const QString &text, bool over) {
		p.setPen(Qt::NoPen);
		p.setBrush(over ? QColor(0xdd, 0xdd, 0xdd) : QColor(Qt::white));
		p.drawRoundedRect(rect, rect.height() / 2., rect.height() / 2.);
		p.setPen(Qt::black);
		p.setFont(font(17, QFont::Bold));
		p.drawText(rect, Qt::AlignCenter, text + QString::fromUtf8("  →"));
	}

	const QImage _logo = QImage(u":/gui/art/logo_256.png"_q);
	Ui::Animations::Simple _appear;
	int _slide = 0;
	bool _vrgTheme = true;
	bool _visitedLook = false;
	bool _ghost = false;
	Hit _over = Hit::None;
	QRect _column;
	QRect _button;
	QRect _skip;
	QRect _dots[kSlides];
	QRect _cardVrg;
	QRect _cardKeep;
	QRect _ghostRect;

};

inline void ShowWelcome(not_null<Window::SessionController*> controller) {
	const auto parent = controller->widget()->bodyWidget();
	if (!parent) {
		return;
	}
	const auto welcome = Ui::CreateChild<Welcome>(parent);
	parent->sizeValue(
	) | rpl::on_next([=](QSize size) {
		welcome->setGeometry(0, 0, size.width(), size.height());
	}, welcome->lifetime());
	welcome->show();
	welcome->raise();
	welcome->setFocus();
}

inline void ScheduleWelcome(not_null<Core::Application*> app) {
	if (Conf::Bool("welcome_done", false)) {
		return;
	}
	base::call_delayed(1500, app, [=] {
		const auto window = app->activeWindow();
		const auto controller = window
			? window->sessionController()
			: nullptr;
		if (!controller) {
			ScheduleWelcome(app);
			return;
		}
		ShowWelcome(controller);
	});
}

}
