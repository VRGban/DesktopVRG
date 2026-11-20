/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#include "vrg/music/vrg_music_section.h"

#include "vrg/music/vrg_music_widget.h"
#include "ui/layers/layer_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_window.h"
#include "styles/style_layers.h"

#include "vrg/vrg_conf.h"

#include "vrg/music/vrg_music_player.h"

#include <algorithm>
#include <QtGui/QKeyEvent>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QGraphicsProxyWidget>
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QGraphicsView>

namespace Vrg::Music {

object_ptr<Window::SectionWidget> SectionMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	auto result = object_ptr<SectionWidget>(parent, controller);
	result->setGeometry(geometry);
	return result;
}

SectionWidget::SectionWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Window::SectionWidget(parent, controller)
, _back(this, st::historyTopBarBack)
, _title(this, u"VRG Music"_q, st::boxTitle)
, _music(AcquireMusicWidget(controller.get(), this)) {
	_music->ensurePolished();
	_music->show();
	_fullscreenLyrics = _music->lyricsOpen();
	_back->setVisible(!_fullscreenLyrics);
	_title->setVisible(!_fullscreenLyrics);
	_back->setClickedCallback([=] {
		closeWithAnimation();
	});
	connect(SharedPlayer(), &Player::visualSettingsChanged, this, [this] {
		layoutMusic();
	});
	connect(_music, &VRGMusicWidget::fullscreenLyricsChanged, this, [this](bool active) {
		_fullscreenLyrics = active;
		_back->setVisible(!active);
		_title->setVisible(!active);
		layoutMusic();
		update();
	});
}

SectionWidget::~SectionWidget() {
	if (_proxy) {
		removeZoomView();
	}
	if (_music && _music->parentWidget() == this) {
		ReleaseMusicWidget(_music.data());
	}
}

bool SectionWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	return sameTypeAs(memento);
}

bool SectionWidget::sameTypeAs(not_null<Window::SectionMemento*> memento) {
	return dynamic_cast<SectionMemento*>(memento.get()) != nullptr;
}

QRect SectionWidget::floatPlayerAvailableRect() {
	return mapToGlobal(rect());
}

bool SectionWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return false;
}

void SectionWidget::doSetInnerFocus() {
	if (_music) {
		_music->setFocus();
	}
}

void SectionWidget::showEvent(QShowEvent *e) {
	Window::SectionWidget::showEvent(e);
	if (_entered || !_music) {
		return;
	}
	_entered = true;
	_closing = false;
	layoutMusic();
	installWheelFilter();
}

void SectionWidget::hideEvent(QHideEvent *e) {
	_entered = false;
	Window::SectionWidget::hideEvent(e);
}

void SectionWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		closeWithAnimation();
		return;
	}
	Window::SectionWidget::keyPressEvent(e);
}

void SectionWidget::closeWithAnimation() {
	if (_closing) {
		return;
	}
	_closing = true;
	controller()->showBackFromStack(Window::SectionShow(
		Window::SectionShow::Way::Backward,
		anim::type::instant,
		anim::activation::normal));
}

void SectionWidget::resizeEvent(QResizeEvent *e) {
	const auto topHeight = _fullscreenLyrics ? 0 : st::topBarHeight;
	_back->moveToLeft(0, (st::topBarHeight - _back->height()) / 2);
	_title->moveToLeft(
		_back->width() + st::lineWidth,
		(st::topBarHeight - _title->height()) / 2);
	Q_UNUSED(topHeight);
	layoutMusic();
}

void SectionWidget::paintEvent(QPaintEvent *e) {
	if (_fullscreenLyrics) {
		return;
	}
	Painter p(this);
	p.fillRect(rect(), st::topBarBg);
	p.fillRect(0, st::topBarHeight - st::lineWidth, width(), st::lineWidth, st::shadowFg);
}


void SectionWidget::layoutMusic() {
	if (!_music) {
		return;
	}
	const auto topHeight = _fullscreenLyrics ? 0 : st::topBarHeight;
	const auto area = QRect(0, topHeight, width(), height() - topHeight);
	const auto zoom = std::clamp(Vrg::Conf::Int("music_zoom", 100), 100, 200) / 100.;
	if (zoom <= 1.001) {
		if (_proxy) {
			removeZoomView();
		}
		_music->setGeometry(area);
		return;
	}
	const auto needed = std::max(
		_music->minimumSizeHint().width(),
		_music->minimumWidth());
	const auto fit = (needed > 0)
		? (area.width() / double(needed))
		: zoom;
	const auto zoomUsed = std::max(1., std::min(zoom, fit));
	if (zoomUsed <= 1.001) {
		if (_proxy) {
			removeZoomView();
		}
		_music->setGeometry(area);
		return;
	}
	if (!_proxy) {
		createZoomView();
	}
	_zoomView->setGeometry(area);
	_zoomScene->setSceneRect(0, 0, area.width(), area.height());
	_proxy->setScale(zoomUsed);
	_proxy->resize(QSizeF(area.width() / zoomUsed, area.height() / zoomUsed));
}

void SectionWidget::createZoomView() {
	_music->setParent(nullptr);
	_zoomScene = new QGraphicsScene(this);
	_zoomView = new QGraphicsView(_zoomScene, this);
	_zoomView->setFrameShape(QFrame::NoFrame);
	_zoomView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_zoomView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_zoomView->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	_zoomView->setBackgroundBrush(st::windowBg);
	_zoomView->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
	_zoomView->setRenderHint(QPainter::TextAntialiasing);
	_zoomView->setRenderHint(QPainter::SmoothPixmapTransform);
	_proxy = _zoomScene->addWidget(_music);
	_proxy->setPos(0, 0);
	_zoomView->show();
	_music->show();
	installWheelFilter();
}

void SectionWidget::removeZoomView() {
	if (!_proxy) {
		return;
	}
	if (_music) {
		_proxy->setWidget(nullptr);
		_music->setParent(this);
		_music->show();
	}
	_proxy = nullptr;
	delete _zoomView;
	_zoomView = nullptr;
	delete _zoomScene;
	_zoomScene = nullptr;
}

void SectionWidget::installWheelFilter() {
	if (!_music) {
		return;
	}
	_music->installEventFilter(this);
	for (const auto child : _music->findChildren<QWidget*>()) {
		child->installEventFilter(this);
	}
}

bool SectionWidget::eventFilter(QObject *watched, QEvent *event) {
	if (event->type() == QEvent::Wheel) {
		const auto wheel = static_cast<QWheelEvent*>(event);
		if (wheel->modifiers() & Qt::ControlModifier) {
			const auto current = std::clamp(Vrg::Conf::Int("music_zoom", 100), 100, 200);
			const auto next = std::clamp(
				current + (wheel->angleDelta().y() > 0 ? 10 : -10),
				100,
				200);
			if (next != current) {
				Vrg::Conf::SetInt("music_zoom", next);
				layoutMusic();
			}
			return true;
		}
	}
	return Window::SectionWidget::eventFilter(watched, event);
}

}
