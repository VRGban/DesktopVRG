/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#include "vrg/vrg_deleted_section.h"

#include "vrg/vrg_deleted.h"
#include "data/data_document.h"
#include "data/data_messages.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/reactions/history_view_reactions.h"
#include "history/view/reactions/history_view_reactions_button.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"

#include <algorithm>

namespace Vrg {

object_ptr<Window::SectionWidget> DeletedMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	Q_UNUSED(column);
	auto result = object_ptr<DeletedSectionWidget>(parent, controller, _history);
	result->setGeometry(geometry);
	return result;
}

DeletedSectionWidget::DeletedSectionWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<History*> history)
: Window::SectionWidget(parent, controller)
, _history(history)
, _theme(controller->defaultChatTheme())
, _back(this, st::historyTopBarBack)
, _title(
	this,
	_history->peer->name() + QString::fromUtf8(" · Удалённые"),
	st::boxTitle)
, _shadow(this)
, _scroll(this, st::historyScroll) {
	_back->setClickedCallback([=] {
		controller->showBackFromStack();
	});

	_inner = _scroll->setOwnedWidget(object_ptr<HistoryView::ListWidget>(
		this,
		&controller->session(),
		static_cast<HistoryView::ListDelegate*>(this)));
	_scroll->setHandleTouch(false);
	_inner->show();
	_scroll->show();
	_shadow->raise();
	_scroll->scrolls(
	) | rpl::on_next([=] {
		updateInnerVisibleArea();
	}, _scroll->lifetime());
	_inner->showAtPosition(Data::MaxMessagePosition, {}, nullptr);
}

void DeletedSectionWidget::updateInnerVisibleArea() {
	if (!_inner) {
		return;
	}
	const auto top = _scroll->scrollTop();
	_inner->setVisibleTopBottom(top, top + _scroll->height());
}

bool DeletedSectionWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	Q_UNUSED(params);
	return sameTypeAs(memento);
}

bool DeletedSectionWidget::sameTypeAs(
		not_null<Window::SectionMemento*> memento) {
	return dynamic_cast<DeletedMemento*>(memento.get()) != nullptr;
}

QRect DeletedSectionWidget::floatPlayerAvailableRect() {
	return mapToGlobal(rect());
}

bool DeletedSectionWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	Q_UNUSED(e);
	return false;
}

void DeletedSectionWidget::doSetInnerFocus() {
	if (_inner) {
		_inner->setFocus();
	}
}

void DeletedSectionWidget::resizeEvent(QResizeEvent *e) {
	Window::SectionWidget::resizeEvent(e);
	const auto topH = st::topBarHeight;
	_back->moveToLeft(0, (topH - _back->height()) / 2);
	_title->moveToLeft(
		_back->width() + st::lineWidth,
		(topH - _title->height()) / 2);
	_shadow->setGeometry(0, topH, width(), st::lineWidth);
	const auto scrollSize = QSize(width(), std::max(height() - topH, 0));
	_scroll->move(0, topH);
	if (_scroll->size() != scrollSize) {
		_scroll->resize(scrollSize);
		_inner->resizeToWidth(scrollSize.width(), scrollSize.height());
	}
	updateInnerVisibleArea();
}

void DeletedSectionWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(0, 0, width(), st::topBarHeight, st::topBarBg);
	const auto &bg = _theme->background();
	p.fillRect(
		0,
		st::topBarHeight,
		width(),
		height() - st::topBarHeight,
		bg.colorForFill.value_or(st::windowBg->c));
}

HistoryView::Context DeletedSectionWidget::listContext() {
	return HistoryView::Context::History;
}

bool DeletedSectionWidget::listScrollTo(int top, bool syntetic) {
	Q_UNUSED(syntetic);
	_scroll->scrollToY(top);
	return true;
}

void DeletedSectionWidget::listCancelRequest() {
	controller()->showBackFromStack();
}

void DeletedSectionWidget::listDeleteRequest() {
}

void DeletedSectionWidget::listTryProcessKeyInput(not_null<QKeyEvent*> e) {
	Q_UNUSED(e);
}

rpl::producer<Data::MessagesSlice> DeletedSectionWidget::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	Q_UNUSED(aroundId);
	Q_UNUSED(limitBefore);
	Q_UNUSED(limitAfter);
	const auto peerId = _history->peer->id;
	return rpl::single(rpl::empty) | rpl::map([=] {
		auto slice = Data::MessagesSlice();
		auto rawIds = Deleted::ListForPeer(peerId.value);
		std::sort(rawIds.begin(), rawIds.end());
		slice.ids.reserve(rawIds.size());
		for (const auto id : rawIds) {
			slice.ids.emplace_back(peerId, MsgId(id));
		}
		slice.fullCount = int(slice.ids.size());
		slice.skippedBefore = 0;
		slice.skippedAfter = 0;
		return slice;
	});
}

bool DeletedSectionWidget::listAllowsMultiSelect() {
	return false;
}

bool DeletedSectionWidget::listIsItemGoodForSelection(
		not_null<HistoryItem*> item) {
	Q_UNUSED(item);
	return false;
}

bool DeletedSectionWidget::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return first->id < second->id;
}

void DeletedSectionWidget::listSelectionChanged(
		HistoryView::SelectedItems &&items) {
	Q_UNUSED(items);
}

void DeletedSectionWidget::listMarkReadTill(not_null<HistoryItem*> item) {
	Q_UNUSED(item);
}

void DeletedSectionWidget::listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) {
	Q_UNUSED(items);
}

HistoryView::MessagesBarData DeletedSectionWidget::listMessagesBar(
		const std::vector<not_null<HistoryView::Element*>> &elements,
		bool markLastAsRead) {
	Q_UNUSED(elements);
	Q_UNUSED(markLastAsRead);
	return {};
}

void DeletedSectionWidget::listContentRefreshed() {
}

void DeletedSectionWidget::listUpdateDateLink(
		ClickHandlerPtr &link,
		not_null<HistoryView::Element*> view) {
	Q_UNUSED(link);
	Q_UNUSED(view);
}

bool DeletedSectionWidget::listElementHideReply(
		not_null<const HistoryView::Element*> view) {
	Q_UNUSED(view);
	return false;
}

bool DeletedSectionWidget::listElementShownUnread(
		not_null<const HistoryView::Element*> view) {
	Q_UNUSED(view);
	return false;
}

bool DeletedSectionWidget::listIsGoodForAroundPosition(
		not_null<const HistoryView::Element*> view) {
	Q_UNUSED(view);
	return true;
}

void DeletedSectionWidget::listSendBotCommand(
		const QString &command,
		const FullMsgId &context) {
	Q_UNUSED(command);
	Q_UNUSED(context);
}

void DeletedSectionWidget::listSearch(
		const QString &query,
		const FullMsgId &context) {
	Q_UNUSED(query);
	Q_UNUSED(context);
}

void DeletedSectionWidget::listHandleViaClick(not_null<UserData*> bot) {
	Q_UNUSED(bot);
}

not_null<Ui::ChatTheme*> DeletedSectionWidget::listChatTheme() {
	return _theme.get();
}

HistoryView::CopyRestrictionType DeletedSectionWidget::listCopyRestrictionType(
		HistoryItem *item) {
	Q_UNUSED(item);
	return HistoryView::CopyRestrictionType::None;
}

HistoryView::CopyRestrictionType DeletedSectionWidget::listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) {
	Q_UNUSED(item);
	return HistoryView::CopyRestrictionType::None;
}

HistoryView::CopyRestrictionType DeletedSectionWidget::listSelectRestrictionType() {
	return HistoryView::CopyRestrictionType::None;
}

auto DeletedSectionWidget::listAllowedReactionsValue()
	-> rpl::producer<Data::AllowedReactions> {
	return rpl::single(Data::AllowedReactions());
}

void DeletedSectionWidget::listShowPremiumToast(not_null<DocumentData*> document) {
	Q_UNUSED(document);
}

void DeletedSectionWidget::listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
	controller()->openPhoto(photo, { .id = context });
}

void DeletedSectionWidget::listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	controller()->openDocument(document, showInMediaView, { .id = context });
}

void DeletedSectionWidget::listPaintEmpty(
		Painter &p,
		const Ui::ChatPaintContext &context) {
	Q_UNUSED(p);
	Q_UNUSED(context);
}

QString DeletedSectionWidget::listElementAuthorRank(
		not_null<const HistoryView::Element*> view) {
	Q_UNUSED(view);
	return QString();
}

bool DeletedSectionWidget::listElementHideTopicButton(
		not_null<const HistoryView::Element*> view) {
	Q_UNUSED(view);
	return true;
}

History *DeletedSectionWidget::listTranslateHistory() {
	return nullptr;
}

void DeletedSectionWidget::listAddTranslatedItems(
		not_null<HistoryView::TranslateTracker*> tracker) {
	Q_UNUSED(tracker);
}

not_null<Window::SessionController*> DeletedSectionWidget::listWindow() {
	return controller();
}

not_null<QWidget*> DeletedSectionWidget::listEmojiInteractionsParent() {
	return this;
}

not_null<const Ui::ChatStyle*> DeletedSectionWidget::listChatStyle() {
	return controller()->chatStyle();
}

rpl::producer<bool> DeletedSectionWidget::listChatWideValue() {
	return rpl::single(false);
}

std::unique_ptr<HistoryView::Reactions::Manager> DeletedSectionWidget::listMakeReactionsManager(
		QWidget *wheelEventsTarget,
		Fn<void(QRect)> update) {
	Q_UNUSED(wheelEventsTarget);
	Q_UNUSED(update);
	return nullptr;
}

void DeletedSectionWidget::listVisibleAreaUpdated() {
}

std::shared_ptr<Ui::Show> DeletedSectionWidget::listUiShow() {
	return controller()->uiShow();
}

void DeletedSectionWidget::listShowPollResults(
		not_null<PollData*> poll,
		FullMsgId context) {
	Q_UNUSED(poll);
	Q_UNUSED(context);
}

void DeletedSectionWidget::listCancelUploadLayer(not_null<HistoryItem*> item) {
	Q_UNUSED(item);
}

bool DeletedSectionWidget::listAnimationsPaused() {
	return controller()->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
}

Ui::MessageSendingAnimationController *DeletedSectionWidget::listSendingAnimation() {
	return nullptr;
}

Ui::ChatPaintContext DeletedSectionWidget::listPreparePaintContext(
		Ui::ChatPaintContextArgs &&args) {
	return _theme->preparePaintContext(
		controller()->chatStyle(),
		rect(),
		rect(),
		args.clip,
		controller()->isGifPausedAtLeastFor(Window::GifPauseReason::Any));
}

bool DeletedSectionWidget::listMarkingContentRead() {
	return false;
}

bool DeletedSectionWidget::listIgnorePaintEvent(QWidget *w, QPaintEvent *e) {
	Q_UNUSED(w);
	Q_UNUSED(e);
	return false;
}

bool DeletedSectionWidget::listShowReactPremiumError(
		not_null<HistoryItem*> item,
		const Data::ReactionId &id) {
	Q_UNUSED(item);
	Q_UNUSED(id);
	return false;
}

base::unique_qptr<Ui::PopupMenu> DeletedSectionWidget::listFillSenderUserpicMenu(
		PeerId userpicPeerId) {
	Q_UNUSED(userpicPeerId);
	return nullptr;
}

void DeletedSectionWidget::listWindowSetInnerFocus() {
	setInnerFocus();
}

bool DeletedSectionWidget::listAllowsDragForward() {
	return false;
}

void DeletedSectionWidget::listLaunchDrag(
		std::unique_ptr<QMimeData> data,
		Fn<void()> finished) {
	Q_UNUSED(data);
	if (finished) {
		finished();
	}
}

}
