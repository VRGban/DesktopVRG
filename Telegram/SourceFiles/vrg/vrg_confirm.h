/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "ui/boxes/confirm_box.h"
#include "data/data_document.h"
#include "ui/layers/generic_box.h"
#include "vrg/vrg_conf.h"
#include "window/window_session_controller.h"

namespace Vrg {

[[nodiscard]] inline bool ConfirmDocument(
		not_null<Window::SessionController*> controller,
		not_null<DocumentData*> document,
		Fn<void()> proceed) {
	static bool approved = false;
	if (approved) {
		return false;
	}
	const auto isSticker = (document->sticker() != nullptr);
	const auto isGif = !isSticker
		&& (document->isGifv() || document->isAnimation());
	if (!((isSticker && Conf::Bool("confirm_sticker", false))
		|| (isGif && Conf::Bool("confirm_gif", false)))) {
		return false;
	}
	controller->show(Ui::MakeConfirmBox({
		.text = isSticker
			? QString::fromUtf8("Отправить этот стикер?")
			: QString::fromUtf8("Отправить эту GIF?"),
		.confirmed = [=](Fn<void()> close) {
			close();
			approved = true;
			proceed();
			approved = false;
		},
		.confirmText = QString::fromUtf8("Отправить"),
	}));
	return true;
}

}

