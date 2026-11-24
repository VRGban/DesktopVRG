/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "base/call_delayed.h"
#include "core/application.h"
#include "vrg/vrg_conf.h"
#include "vrg/vrg_settings.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Vrg {

inline void ScheduleDevOpen(not_null<Core::Application*> app) {
	if (Conf::Str("dev_open").isEmpty()) {
		return;
	}
	base::call_delayed(3000, app, [=] {
		const auto window = app->activeWindow();
		const auto controller = window
			? window->sessionController()
			: nullptr;
		if (!controller) {
			ScheduleDevOpen(app);
			return;
		}
		const auto page = Conf::Str("dev_open");
		if (page == "ghost") {
			controller->showSettings(Settings::VrgGhostId());
		} else if (page == "menu") {
			controller->showSettings(Settings::VrgSideMenuId());
		} else if (page == "plus") {
			controller->showSettings(Settings::VrgPlusId());
		} else {
			controller->showSettings(Settings::VrgId());
		}
	});
}

}

