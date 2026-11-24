/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "ui/layers/layer_widget.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include "history/view/history_view_list_widget.h"

class History;

namespace Ui {
class ElasticScroll;
class IconButton;
class FlatLabel;
class PlainShadow;
}

namespace Vrg {

class DeletedMemento final : public Window::SectionMemento {
public:
	explicit DeletedMemento(not_null<History*> history)
	: _history(history) {
	}

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

private:
	not_null<History*> _history;

};

class DeletedSectionWidget final
	: public Window::SectionWidget
	, private HistoryView::ListDelegate {
public:
	DeletedSectionWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<History*> history);

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
	void updateInnerVisibleArea();
	HistoryView::Context listContext() override;
	bool listScrollTo(int top, bool syntetic = true) override;
	void listCancelRequest() override;
	void listDeleteRequest() override;
	void listTryProcessKeyInput(not_null<QKeyEvent*> e) override;
	rpl::producer<Data::MessagesSlice> listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) override;
	bool listAllowsMultiSelect() override;
	bool listIsItemGoodForSelection(not_null<HistoryItem*> item) override;
	bool listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) override;
	void listSelectionChanged(HistoryView::SelectedItems &&items) override;
	void listMarkReadTill(not_null<HistoryItem*> item) override;
	void listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) override;
	HistoryView::MessagesBarData listMessagesBar(
		const std::vector<not_null<HistoryView::Element*>> &elements,
		bool markLastAsRead) override;
	void listContentRefreshed() override;
	void listUpdateDateLink(
		ClickHandlerPtr &link,
		not_null<HistoryView::Element*> view) override;
	bool listElementHideReply(not_null<const HistoryView::Element*> view) override;
	bool listElementShownUnread(not_null<const HistoryView::Element*> view) override;
	bool listIsGoodForAroundPosition(
		not_null<const HistoryView::Element*> view) override;
	void listSendBotCommand(
		const QString &command,
		const FullMsgId &context) override;
	void listSearch(
		const QString &query,
		const FullMsgId &context) override;
	void listHandleViaClick(not_null<UserData*> bot) override;
	not_null<Ui::ChatTheme*> listChatTheme() override;
	HistoryView::CopyRestrictionType listCopyRestrictionType(
		HistoryItem *item) override;
	HistoryView::CopyRestrictionType listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) override;
	HistoryView::CopyRestrictionType listSelectRestrictionType() override;
	auto listAllowedReactionsValue()
		-> rpl::producer<Data::AllowedReactions> override;
	void listShowPremiumToast(not_null<DocumentData*> document) override;
	void listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) override;
	void listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) override;
	void listPaintEmpty(
		Painter &p,
		const Ui::ChatPaintContext &context) override;
	QString listElementAuthorRank(
		not_null<const HistoryView::Element*> view) override;
	bool listElementHideTopicButton(
		not_null<const HistoryView::Element*> view) override;
	History *listTranslateHistory() override;
	void listAddTranslatedItems(
		not_null<HistoryView::TranslateTracker*> tracker) override;
	not_null<Window::SessionController*> listWindow() override;
	not_null<QWidget*> listEmojiInteractionsParent() override;
	not_null<const Ui::ChatStyle*> listChatStyle() override;
	rpl::producer<bool> listChatWideValue() override;
	std::unique_ptr<HistoryView::Reactions::Manager> listMakeReactionsManager(
		QWidget *wheelEventsTarget,
		Fn<void(QRect)> update) override;
	void listVisibleAreaUpdated() override;
	std::shared_ptr<Ui::Show> listUiShow() override;
	void listShowPollResults(
		not_null<PollData*> poll,
		FullMsgId context) override;
	void listCancelUploadLayer(not_null<HistoryItem*> item) override;
	bool listAnimationsPaused() override;
	Ui::MessageSendingAnimationController *listSendingAnimation() override;
	Ui::ChatPaintContext listPreparePaintContext(
		Ui::ChatPaintContextArgs &&args) override;
	bool listMarkingContentRead() override;
	bool listIgnorePaintEvent(QWidget *w, QPaintEvent *e) override;
	bool listShowReactPremiumError(
		not_null<HistoryItem*> item,
		const Data::ReactionId &id) override;
	base::unique_qptr<Ui::PopupMenu> listFillSenderUserpicMenu(
		PeerId userpicPeerId) override;
	void listWindowSetInnerFocus() override;
	bool listAllowsDragForward() override;
	void listLaunchDrag(
		std::unique_ptr<QMimeData> data,
		Fn<void()> finished) override;

	not_null<History*> _history;
	std::shared_ptr<Ui::ChatTheme> _theme;
	object_ptr<Ui::IconButton> _back;
	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::PlainShadow> _shadow;
	object_ptr<Ui::ElasticScroll> _scroll;
	HistoryView::ListWidget *_inner = nullptr;

};

}

