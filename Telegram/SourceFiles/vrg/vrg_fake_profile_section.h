/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "data/data_star_gift.h"
#include "ui/layers/layer_widget.h"
#include "window/section_memento.h"
#include "window/section_widget.h"

#include <optional>

class UserData;

namespace Main {
class Session;
}

namespace Ui {
class IconButton;
class FlatLabel;
class PlainShadow;
class ScrollArea;
class VerticalLayout;
}

namespace Vrg::FakeProfile {

void ResolveGiftSlug(
	not_null<Main::Session*> session,
	const QString &slug,
	Fn<void(std::optional<::Data::StarGift>)> onDone);

void ShowEditBox(
	not_null<Window::SessionController*> controller,
	Fn<void()> onUpdated = nullptr);

class Memento final : public Window::SectionMemento {
public:
	explicit Memento(not_null<UserData*> user) : _user(user) {
	}

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

private:
	not_null<UserData*> _user;

};

class SectionWidget final : public Window::SectionWidget {
public:
	SectionWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user);

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	bool sameTypeAs(not_null<Window::SectionMemento*> memento) override;
	QRect floatPlayerAvailableRect() override;
	bool floatPlayerHandleWheelEvent(QEvent *e) override;

protected:
	void doSetInnerFocus() override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void rebuildContent();
	void updateLayout();

	not_null<UserData*> _user;
	object_ptr<Ui::IconButton> _back;
	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::IconButton> _editBtn;
	object_ptr<Ui::PlainShadow> _shadow;
	object_ptr<Ui::ScrollArea> _scroll;
	Ui::VerticalLayout *_inner = nullptr;

};

}
