/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "apiwrap.h"
#include "base/weak_ptr.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "ui/boxes/confirm_box.h"
#include "window/window_session_controller.h"

#include <QtCore/QPointer>
#include <memory>
#include <vector>

namespace Vrg::PeerActions {

namespace Detail {

[[nodiscard]] inline MsgId ExtractMsgId(const MTPMessage &msg) {
	return msg.match([](const MTPDmessage &d) {
		return MsgId(d.vid().v);
	}, [](const MTPDmessageService &d) {
		return MsgId(d.vid().v);
	}, [](const MTPDmessageEmpty &d) {
		return MsgId(d.vid().v);
	});
}

[[nodiscard]] inline bool IsOutMessage(const MTPMessage &msg) {
	return msg.match([](const MTPDmessage &d) {
		return d.is_out();
	}, [](const MTPDmessageService &d) {
		return d.is_out();
	}, [](const MTPDmessageEmpty &) {
		return false;
	});
}

[[nodiscard]] inline const QVector<MTPMessage> *ExtractMessages(
		const MTPmessages_Messages &result) {
	return result.match([](const MTPDmessages_messages &d) {
		return &d.vmessages().v;
	}, [](const MTPDmessages_messagesSlice &d) {
		return &d.vmessages().v;
	}, [](const MTPDmessages_channelMessages &d) {
		return &d.vmessages().v;
	}, [](const MTPDmessages_messagesNotModified &) -> const QVector<MTPMessage>* {
		return nullptr;
	});
}

inline void RunDeleteStep(
		base::weak_ptr<Window::SessionController> controller,
		not_null<PeerData*> peer,
		MsgId offsetId,
		std::shared_ptr<int> totalDeleted) {
	auto &session = peer->session();
	const auto self = session.user();
	const auto useFromId = !peer->isUser();
	using Flag = MTPmessages_Search::Flag;
	session.api().request(MTPmessages_Search(
		MTP_flags(useFromId ? Flag::f_from_id : Flag()),
		peer->input(),
		MTP_string(),
		(useFromId ? self->input() : MTP_inputPeerEmpty()),
		MTP_inputPeerEmpty(),
		MTP_vector<MTPReaction>(),
		MTP_int(0),
		MTP_inputMessagesFilterEmpty(),
		MTP_int(0),
		MTP_int(0),
		MTP_int(offsetId.bare),
		MTP_int(0),
		MTP_int(100),
		MTP_int(0),
		MTP_int(0),
		MTP_long(0)
	)).done([=](const MTPmessages_Messages &result) {
		const auto messages = ExtractMessages(result);
		if (!messages || messages->isEmpty()) {
			if (controller) {
				controller->showToast(*totalDeleted > 0
					? QString::fromUtf8("Удалено ваших сообщений: %1").arg(*totalDeleted)
					: QString::fromUtf8("Ваших сообщений не найдено"));
			}
			return;
		}
		auto nextOffset = MsgId(0);
		auto batch = MessageIdsList();
		for (const auto &m : *messages) {
			const auto mid = ExtractMsgId(m);
			if (!nextOffset || mid < nextOffset) {
				nextOffset = mid;
			}
			if (mid && IsOutMessage(m)) {
				batch.emplace_back(peer->id, mid);
			}
		}
		if (!batch.empty()) {
			*totalDeleted += int(batch.size());
			peer->owner().histories().deleteMessages(batch, true);
		}
		if (!nextOffset || nextOffset == offsetId) {
			if (controller) {
				controller->showToast(QString::fromUtf8("Удалено ваших сообщений: %1")
					.arg(*totalDeleted));
			}
			return;
		}
		RunDeleteStep(controller, peer, nextOffset, totalDeleted);
	}).fail([=](const MTP::Error &error) {
		if (controller) {
			controller->showToast(QString::fromUtf8("Ошибка удаления: ") + error.type());
		}
	}).send();
}

}

inline void JumpToFirstMessage(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	const auto weak = base::make_weak(controller.get());
	peer->session().api().request(MTPmessages_GetHistory(
		peer->input(),
		MTP_int(1),
		MTP_int(0),
		MTP_int(-1),
		MTP_int(1),
		MTP_int(0),
		MTP_int(0),
		MTP_long(0)
	)).done([=](const MTPmessages_Messages &result) {
		if (!weak) {
			return;
		}
		const auto messages = Detail::ExtractMessages(result);
		if (!messages || messages->isEmpty()) {
			weak->showToast(QString::fromUtf8("В этом чате пока нет сообщений"));
			return;
		}
		const auto firstId = Detail::ExtractMsgId(messages->front());
		if (!firstId) {
			weak->showToast(QString::fromUtf8("Не удалось найти первое сообщение"));
			return;
		}
		weak->showPeerHistory(peer, Window::SectionShow::Way::Forward, firstId);
	}).fail([=](const MTP::Error &) {
		if (weak) {
			weak->showToast(QString::fromUtf8("Не удалось загрузить историю чата"));
		}
	}).send();
}

inline void DeleteAllMyMessages(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	const auto weak = base::make_weak(controller.get());
	controller->show(Ui::MakeConfirmBox({
		.text = QString::fromUtf8(
			"Удалить все ваши сообщения в этом чате (для всех, где возможно)? "
			"Отменить это действие нельзя."),
		.confirmed = [=](Fn<void()> close) {
			close();
			if (!weak) {
				return;
			}
			weak->showToast(QString::fromUtf8("Ищу и удаляю ваши сообщения..."));
			Detail::RunDeleteStep(
				weak,
				peer,
				MsgId(0),
				std::make_shared<int>(0));
		},
		.confirmText = QString::fromUtf8("Удалить все"),
	}));
}

}
