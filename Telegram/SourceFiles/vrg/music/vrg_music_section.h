/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "ui/effects/animations.h"

#include <QtCore/QPointer>

class QGraphicsView;
class QGraphicsScene;
class QGraphicsProxyWidget;
class QWheelEvent;
#include "ui/layers/layer_widget.h"
#include "window/section_memento.h"
#include "window/section_widget.h"

namespace Ui {
class IconButton;
class FlatLabel;
}

namespace Vrg::Music {

class VRGMusicWidget;

class SectionMemento final : public Window::SectionMemento {
public:
	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

};

class SectionWidget final : public Window::SectionWidget {
public:
	SectionWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~SectionWidget();

	bool isFullWidthSection() const override {
		return true;
	}
	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	bool sameTypeAs(not_null<Window::SectionMemento*> memento) override;
	QRect floatPlayerAvailableRect() override;
	bool floatPlayerHandleWheelEvent(QEvent *e) override;

protected:
	void doSetInnerFocus() override;
	void showEvent(QShowEvent *e) override;
	void hideEvent(QHideEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void closeWithAnimation();
	void layoutMusic();
	void createZoomView();
	void removeZoomView();
	void installWheelFilter();

	QGraphicsView *_zoomView = nullptr;
	QGraphicsScene *_zoomScene = nullptr;
	QGraphicsProxyWidget *_proxy = nullptr;

	object_ptr<Ui::IconButton> _back;
	object_ptr<Ui::FlatLabel> _title;
	QPointer<VRGMusicWidget> _music;
	bool _fullscreenLyrics = false;
	bool _entered = false;
	bool _closing = false;

};

}
