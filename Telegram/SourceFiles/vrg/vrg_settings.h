/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "settings/settings_common_session.h"
#include "storage/localstorage.h"
#include "ui/boxes/choose_font_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_theme.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/checkbox.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "core/ui_integration.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/continuous_sliders.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "data/data_user.h"
#include "vrg/vrg_autoreply.h"
#include "vrg/vrg_backup.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "storage/cache/storage_cache_database.h"
#include "base/unixtime.h"
#include "vrg/vrg_badges.h"
#include "vrg/vrg_chat_lock.h"
#include "vrg/vrg_conf.h"
#include "vrg/vrg_fake_profile.h"
#include "vrg/vrg_fake_profile_section.h"
#include "vrg/vrg_ghost.h"
#include "vrg/vrg_security.h"
#include "vrg/vrg_templates.h"
#include "vrg/vrg_updater.h"
#include "vrg/vrg_voice_changer.h"
#include "vrg/vrg_welcome.h"
#include "vrg/music/vrg_music_player.h"
#include "window/section_widget.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Settings {

[[nodiscard]] inline Type VrgId();
[[nodiscard]] inline Type VrgGhostId();
[[nodiscard]] inline Type VrgSideMenuId();
[[nodiscard]] inline Type VrgPlusId();
[[nodiscard]] inline Type VrgDeletedId();
[[nodiscard]] inline Type VrgLookId();
[[nodiscard]] inline Type VrgPlayerId();
[[nodiscard]] inline Type VrgMiscId();
[[nodiscard]] inline Type VrgSecurityId();
[[nodiscard]] inline Type VrgAutoReplyId();
[[nodiscard]] inline Type VrgVoiceChangerId();
[[nodiscard]] inline Type VrgFakeProfileId();
[[nodiscard]] inline Type VrgSupportId();

namespace VrgDetail {

inline constexpr auto kVersion = "1.0.6";

[[nodiscard]] inline QString Ru(const char *text) {
	return QString::fromUtf8(text);
}

inline void AddHeader(not_null<Ui::VerticalLayout*> container) {
	const auto header = container->add(
		object_ptr<Ui::FixedHeightWidget>(container, 260));
	const auto logo = QImage(u":/gui/art/logo_256.png"_q);
	header->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(header);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		const auto size = 128;
		const auto rect = QRect(
			(header->width() - size) / 2,
			20,
			size,
			size);
		auto circle = QPainterPath();
		circle.addEllipse(rect);
		p.setClipPath(circle);
		p.drawImage(rect, logo);
		p.setClipping(false);
		p.setPen(st::windowFg);
		p.setFont(st::semiboldFont);
		p.drawText(
			QRect(0, 160, header->width(), 24),
			Qt::AlignHCenter,
			QString("VRG v") + kVersion);
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(24, 188, header->width() - 48, 66),
			Qt::AlignHCenter | Qt::TextWordWrap,
			Ru("Модифицированный Telegram с запрещённым функционалом."));
	}, header->lifetime());
}

inline void MarkAsVrg(not_null<Ui::SettingsButton*> button) {
	const auto overlay = Ui::CreateChild<Ui::RpWidget>(button.get());
	overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto logo = QImage(u":/gui/art/logo_256.png"_q);
	const auto size = 36;
	button->sizeValue(
	) | rpl::on_next([=](QSize s) {
		overlay->setGeometry(0, 0, s.width(), s.height());
	}, overlay->lifetime());
	overlay->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(overlay);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		const auto rect = QRect(
			st::settingsButton.iconLeft - 6,
			(overlay->height() - size) / 2,
			size,
			size);
		auto circle = QPainterPath();
		circle.addEllipse(rect);
		p.setClipPath(circle);
		p.drawImage(rect, logo);
	}, overlay->lifetime());
	overlay->show();
}

[[nodiscard]] inline not_null<Ui::VerticalLayout*> AddCard(
		not_null<Ui::VerticalLayout*> container) {
	return container;
}

inline void AddCaption(
		not_null<Ui::VerticalLayout*> container,
		const QString &text) {
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			text.toUpper(),
			st::defaultFlatLabel),
		QMargins(st::settingsButton.iconLeft, 22, 28, 6));
	label->setTextColorOverride(QColor(0x8c, 0x8c, 0x8c));
}

inline void AddNote(
		not_null<Ui::VerticalLayout*> container,
		const QString &text) {
	Ui::AddDividerText(container, rpl::single(text));
}

inline void InfoBox(
		not_null<Ui::GenericBox*> box,
		const QString &title,
		const QString &text) {
	box->setTitle(rpl::single(title));
	box->addRow(
		object_ptr<Ui::FlatLabel>(box, text, st::boxLabel),
		st::boxRowPadding);
	box->addButton(rpl::single(Ru("Понятно")), [=] { box->closeBox(); });
}

inline void AddInfo(
		not_null<Ui::SettingsButton*> row,
		const QString &title,
		const QString &text) {
	const auto info = Ui::CreateChild<Ui::AbstractButton>(row.get());
	info->resize(26, 26);
	info->setCursor(style::cur_pointer);
	row->sizeValue(
	) | rpl::on_next([=](QSize size) {
		info->move(size.width() - 112, (size.height() - info->height()) / 2);
	}, info->lifetime());
	info->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(info);
		p.setRenderHint(QPainter::Antialiasing);
		const auto over = info->isOver();
		const auto color = over ? QColor(0xff, 0xff, 0xff) : QColor(0x8c, 0x8c, 0x8c);
		p.setPen(QPen(color, 1.5));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(QRectF(info->rect()).adjusted(3, 3, -3, -3));
		p.setFont(st::semiboldFont);
		p.drawText(info->rect(), Qt::AlignCenter, u"i"_q);
	}, info->lifetime());
	info->setClickedCallback([=] {
		if (const auto window = Core::App().activeWindow()) {
			window->show(Box(InfoBox, title, text));
		}
	});
	info->show();
}

inline void PromptRestart(not_null<Window::SessionController*> controller) {
	controller->show(Box([](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Ru("Требуется перезапуск")));
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				Ru("Чтобы изменения вступили в силу, нужно перезапустить VRG."),
				st::boxLabel),
			st::boxRowPadding);
		box->addButton(rpl::single(Ru("Перезапустить")), [] {
			Core::Restart();
		});
		box->addButton(rpl::single(Ru("Позже")), [=] {
			box->closeBox();
		});
	}));
}

inline void AddConfSwitch(
		not_null<Ui::VerticalLayout*> container,
		const QString &text,
		const QString &key,
		bool fallback,
		rpl::producer<bool> external = rpl::never<bool>(),
		const QString &info = QString(),
		Fn<void(bool)> onChanged = nullptr) {
	const auto row = container->add(object_ptr<Button>(
		container,
		rpl::single(text),
		st::settingsButtonNoIcon));
	row->toggleOn(rpl::merge(
		rpl::single(Vrg::Conf::Bool(key, fallback)),
		std::move(external))
	)->toggledChanges(
	) | rpl::on_next([=](bool value) {
		Vrg::Conf::SetBool(key, value);
		if (onChanged) {
			onChanged(value);
		}
	}, container->lifetime());
	if (!info.isEmpty()) {
		AddInfo(row, text, info);
	}
}

inline void AddConfSlider(
		not_null<Ui::VerticalLayout*> container,
		const QString &title,
		const QString &key,
		int minVal,
		int maxVal,
		int step,
		int fallback,
		Fn<void(int)> onChanged = nullptr) {
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			rpl::single(title),
			st::defaultSubsectionTitle),
		st::defaultSubsectionTitlePadding);
	auto swl = MakeSliderWithLabel(
		container,
		st::settingsScale,
		st::settingsScaleLabel,
		st::normalFont->spacew * 2,
		st::settingsScaleLabel.style.font->width("000%"),
		true);
	container->add(
		std::move(swl.widget),
		st::settingsBigScalePadding);
	const auto slider = swl.slider;
	const auto label = swl.label;
	const auto count = ((maxVal - minVal) / step) + 1;
	const auto convert = [=](int index) {
		return minVal + index * step;
	};
	const auto current = std::clamp(
		Vrg::Conf::Int(key, fallback),
		minVal,
		maxVal);
	const auto format = [](int v) {
		return QString::number(v) + QChar('%');
	};
	label->setText(format(current));
	slider->setPseudoDiscrete(
		count,
		convert,
		current,
		[=](int v) {
			label->setText(format(v));
		},
		[=](int v) {
			label->setText(format(v));
			Vrg::Conf::SetInt(key, v);
			if (onChanged) {
				onChanged(v);
			}
		});
}

[[nodiscard]] inline QString TimeShiftText(int minutes) {
	if (!minutes) {
		return Ru("без сдвига");
	}
	const auto sign = (minutes < 0) ? QString::fromUtf8("−") : QString("+");
	const auto abs = std::abs(minutes);
	const auto hours = abs / 60;
	const auto rest = abs % 60;
	auto result = sign + QString::number(hours) + Ru(" ч");
	if (rest) {
		result += QChar(' ') + QString::number(rest) + Ru(" мин");
	}
	return result;
}

inline void ApplyTimeShift() {
	base::unixtime::set_display_shift(Vrg::Conf::Int("time_shift_min", 0) * 60);
}

inline void TimeShiftBox(
		not_null<Ui::GenericBox*> box,
		Fn<void(QString)> chosen) {
	box->setTitle(rpl::single(Ru("Время в клиенте")));
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			Ru("Сдвигает время на вашем экране: у сообщений, в списке чатов, в датах. "
				"Другие люди видят настоящее время. Полностью применяется после перезапуска."),
			st::boxLabel),
		st::boxRowPadding);
	const auto current = Vrg::Conf::Int("time_shift_min", 0);
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(current);
	for (auto minutes = -720; minutes <= 720; minutes += 30) {
		box->addRow(
			object_ptr<Ui::Radiobutton>(
				box,
				group,
				minutes,
				TimeShiftText(minutes),
				st::messagePrivacyCheck),
			st::settingsSendTypePadding);
	}
	group->setChangedCallback([=](int value) {
		Vrg::Conf::SetInt("time_shift_min", value);
		ApplyTimeShift();
		chosen(TimeShiftText(value));
		box->closeBox();
	});
	box->addButton(rpl::single(Ru("Закрыть")), [=] { box->closeBox(); });
}

inline void IconBox(
		not_null<Ui::GenericBox*> box,
		Fn<void(QString)> chosen) {
	box->setTitle(rpl::single(Ru("Иконка приложения")));
	const auto current = Vrg::Conf::Str("app_icon");
	const auto start = (current == "white") ? 1 : (current == "clear") ? 2 : 0;
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(start);
	const auto add = [&](int value, const QString &text) {
		box->addRow(
			object_ptr<Ui::Radiobutton>(
				box,
				group,
				value,
				text,
				st::messagePrivacyCheck),
			st::settingsSendTypePadding);
	};
	add(0, Ru("Белый знак на чёрном"));
	add(1, Ru("Чёрный знак на белом"));
	add(2, Ru("Знак без фона"));
	group->setChangedCallback([=](int value) {
		const auto name = (value == 1)
			? QString("white")
			: (value == 2)
			? QString("clear")
			: QString("black");
		Vrg::Conf::Set("app_icon", name);
		chosen(name);
		box->closeBox();
	});
	box->addButton(rpl::single(Ru("Закрыть")), [=] { box->closeBox(); });
}

[[nodiscard]] inline QString IconName(const QString &key) {
	return (key == "white")
		? Ru("Чёрный на белом")
		: (key == "clear")
		? Ru("Без фона")
		: Ru("Белый на чёрном");
}

inline void ChoiceBox(
		not_null<Ui::GenericBox*> box,
		const QString &title,
		const QString &key,
		std::vector<std::pair<QString, QString>> options,
		Fn<void(QString)> chosen) {
	box->setTitle(rpl::single(title));
	const auto current = Vrg::Conf::Str(key);
	auto start = 0;
	for (auto i = 0; i != int(options.size()); ++i) {
		if (options[i].first == current) {
			start = i;
		}
	}
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(start);
	for (auto i = 0; i != int(options.size()); ++i) {
		box->addRow(
			object_ptr<Ui::Radiobutton>(
				box,
				group,
				i,
				options[i].second,
				st::messagePrivacyCheck),
			st::settingsSendTypePadding);
	}
	group->setChangedCallback([=](int index) {
		const auto &value = options[index].first;
		Vrg::Conf::Set(key, value);
		chosen(value);
		box->closeBox();
	});
	box->addButton(rpl::single(Ru("Закрыть")), [=] { box->closeBox(); });
}

inline void AccountsLimitBox(
		not_null<Ui::GenericBox*> box,
		Fn<void(int)> chosen) {
	box->setTitle(rpl::single(Ru("Лимит аккаунтов")));
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(
		Vrg::Conf::Int("accounts_max", 10));
	const auto add = [&](int value, const QString &text) {
		box->addRow(
			object_ptr<Ui::Radiobutton>(
				box,
				group,
				value,
				text,
				st::messagePrivacyCheck),
			st::settingsSendTypePadding);
	};
	add(3, "3");
	add(5, "5");
	add(10, "10");
	add(Vrg::kMaxAccounts, "99+");
	group->setChangedCallback([=](int value) {
		Vrg::Conf::SetInt("accounts_max", value);
		chosen(value);
		box->closeBox();
	});
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

[[nodiscard]] inline QString LimitText(int value) {
	return (value >= Vrg::kMaxAccounts) ? QString("99+") : QString::number(value);
}

}

class VrgGhost : public Section<VrgGhost> {
public:
	VrgGhost(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		using VrgDetail::Ru;
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
		struct Item { const char *text; const char *key; const char *info; };
		static const auto items = std::vector<Item>{
			{ "Не отмечать сообщения прочитанными", "ghost_read",
				"VRG не сообщает Telegram, что вы прочитали сообщения: "
				"у собеседника останется одна галочка. Сообщение отметится "
				"прочитанным, только если вы сами ответите в чате или "
				"нажмёте «Отметить прочитанным»." },
			{ "Не показывать «печатает…»", "ghost_typing",
				"Собеседники не увидят «печатает…», «записывает голосовое» "
				"и подобные статусы, пока вы пишете." },
			{ "Не показывать, что я в сети", "ghost_online",
				"VRG не сообщает, что вы в сети: в вашем профиле у других "
				"будет «был(а) недавно». Если с этим же аккаунтом открыт "
				"другой клиент (например, телефон), он по-прежнему "
				"отмечает вас онлайн." },
			{ "Уходить в офлайн после отправки", "ghost_offline_send",
				"Сразу после отправки сообщения VRG переводит вас в офлайн. "
				"Между отправкой и переключением может пройти доля "
				"секунды." },
			{ "Смотреть истории незаметно", "ghost_stories",
				"Вы смотрите истории, но не попадаете в список "
				"просмотревших." },
		};
		const auto all = content->lifetime().make_state<
			rpl::event_stream<bool>>();
		auto everything = true;
		for (const auto &item : items) {
			everything = everything && Vrg::Conf::Bool(item.key, false);
		}

		VrgDetail::AddCaption(content, Ru("Режим"));
		const auto mode = VrgDetail::AddCard(content);
		mode->add(object_ptr<Button>(
			mode,
			rpl::single(Ru("Полный призрак")),
			st::settingsButtonNoIcon
		))->toggleOn(rpl::single(everything))->toggledChanges(
		) | rpl::on_next([=](bool value) {
			for (const auto &item : items) {
				Vrg::Conf::SetBool(item.key, value);
			}
			all->fire_copy(value);
		}, content->lifetime());
		VrgDetail::AddCaption(content, Ru("Скрытность"));
		const auto card = VrgDetail::AddCard(content);
		for (const auto &item : items) {
			VrgDetail::AddConfSwitch(
				card,
				Ru(item.text),
				item.key,
				false,
				all->events(),
				Ru(item.info));
		}

		VrgDetail::AddCaption(content, Ru("Дополнительно"));
		const auto extra = VrgDetail::AddCard(content);
		VrgDetail::AddConfSwitch(
			extra,
			Ru("Не палить прослушивание ГС / кружочков"),
			"ghost_voice_contents",
			false,
			rpl::never<bool>(),
			Ru("Даже если основная нечиталка выключена, VRG не сообщает серверу, "
				"что вы прослушали голосовое или посмотрели кружочек."));
		VrgDetail::AddConfSwitch(
			extra,
			Ru("Читать чат при ручном ответе"),
			"ghost_read_on_send",
			false,
			rpl::never<bool>(),
			Ru("Если нечиталка включена, при отправке сообщения вручную чат "
				"автоматически отметится прочитанным на сервере."));
		VrgDetail::AddConfSwitch(
			extra,
			Ru("Отправлять все сообщения без звука"),
			"ghost_silent_send",
			false,
			rpl::never<bool>(),
			Ru("Все ваши исходящие сообщения будут уходить без уведомления "
				"у собеседника (silent = true)."));

		const auto overrides = Vrg::Ghost::ListReadOverrides();
		if (!overrides.empty()) {
			VrgDetail::AddCaption(content, Ru("Исключения по чатам (нажмите чтобы убрать)"));
			for (const auto &entry : overrides) {
				const auto modeText = (entry.mode == 1)
					? Ru("Всегда читать")
					: Ru("Никогда не читать");
				const auto peerId = entry.peerId;
				const auto row = AddButtonWithLabel(
					content,
					rpl::single(entry.title),
					rpl::single(modeText),
					st::settingsButtonNoIcon);
				row->setClickedCallback([=] {
					Vrg::Ghost::SetReadOverride(peerId, 0);
					row->hide();
					controller->showToast(Ru("Исключение для чата убрано"));
				});
			}
		}
		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return rpl::single(VrgDetail::Ru("Призрак"));
	}
};

class VrgSideMenu : public Section<VrgSideMenu> {
public:
	VrgSideMenu(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		using VrgDetail::Ru;
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
		VrgDetail::AddCaption(content, Ru("Пункты меню"));
		const auto card = VrgDetail::AddCard(content);
		const auto add = [&](const char *text, const char *key, bool on) {
			VrgDetail::AddConfSwitch(
				card,
				Ru(text),
				QString("menu_") + key,
				on);
		};
		add("Мой профиль", "profile", true);
		add("Мой фейк-профиль", "fake_profile", true);
		add("Создать группу", "group", true);
		add("Создать канал", "channel", true);
		add("Контакты", "contacts", false);
		add("Звонки", "calls", false);
		add("Режим призрака", "ghost", true);
		add("Прочитать все", "readall", true);
		add("Добавить аккаунт (QR)", "addaccount", true);
		add("Избранное", "saved", true);
		add("Музыка", "music", true);
		add("Деньги", "money", true);
		add("Ночной режим", "night", false);
		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return rpl::single(VrgDetail::Ru("Боковое меню"));
	}
};

class VrgPlus : public Section<VrgPlus> {
public:
	VrgPlus(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		using VrgDetail::Ru;
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
		VrgDetail::AddCaption(content, Ru("Аккаунты"));
		const auto card = VrgDetail::AddCard(content);
		const auto label = content->lifetime().make_state<
			rpl::variable<QString>>(VrgDetail::LimitText(
				Vrg::Conf::Int("accounts_max", 10)));
		AddButtonWithLabel(
			card,
			rpl::single(Ru("Лимит аккаунтов")),
			label->value(),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			controller->show(Box(VrgDetail::AccountsLimitBox, [=](int v) {
				*label = VrgDetail::LimitText(v);
			}));
		});
		VrgDetail::AddCaption(content, Ru("Другое"));
		const auto other = VrgDetail::AddCard(content);
		VrgDetail::AddConfSwitch(
			other,
			Ru("Отключить рекламу"),
			"ads_off",
			false,
			rpl::never<bool>(),
			Ru("Скрывает спонсорские сообщения в каналах и ботах. "
				"Применяется к новым открытым чатам."));
		VrgDetail::AddConfSwitch(
			other,
			Ru("Локальный Telegram Premium"),
			"premium_badge",
			false,
			rpl::never<bool>(),
			Ru("Активирует значок Telegram Premium на вашем экране и "
				"локальные клиентские возможности интерфейса (без серверных "
				"функций вроде загрузки 4 ГБ и премиум-стикеров)."));
		VrgDetail::AddConfSwitch(
			other,
			Ru("Мгновенные уведомления без задержки"),
			"disable_notify_delay",
			false,
			rpl::never<bool>(),
			Ru("Убирает задержку десктопных уведомлений, когда параллельно "
				"активен телефон или другое устройство."));
		VrgDetail::AddCaption(content, Ru("Время"));
		const auto shiftLabel = content->lifetime().make_state<
			rpl::variable<QString>>(VrgDetail::TimeShiftText(
				Vrg::Conf::Int("time_shift_min", 0)));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Время в клиенте")),
			shiftLabel->value(),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			controller->show(Box(VrgDetail::TimeShiftBox, [=](QString text) {
				*shiftLabel = text;
			}));
		});
		VrgDetail::AddCaption(content, Ru("Автоответчик"));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Настроить автоответчик")),
			rpl::single(Vrg::Conf::Bool("autoreply_on", false) ? Ru("вкл") : Ru("выкл")),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			controller->showSettings(VrgAutoReplyId());
		});
		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return rpl::single(QString("VRG+"));
	}
};

class VrgAutoReply : public Section<VrgAutoReply> {
public:
	VrgAutoReply(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		using VrgDetail::Ru;
		using Options = std::vector<std::pair<QString, QString>>;
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

		VrgDetail::AddCaption(content, Ru("Основное"));
		VrgDetail::AddConfSwitch(
			content,
			Ru("Включить автоответчик"),
			"autoreply_on",
			false,
			rpl::never<bool>(),
			Ru("Отвечает на новые личные сообщения, пока VRG запущен."));

		const auto cdName = [](const QString &key) {
			return (key == "15") ? Ru("15 минут")
				: (key == "30") ? Ru("30 минут")
				: (key == "180") ? Ru("3 часа")
				: (key == "720") ? Ru("12 часов")
				: Ru("1 час");
		};
		const auto cdVar = content->lifetime().make_state<
			rpl::variable<QString>>(cdName(Vrg::Conf::Str("autoreply_cooldown")));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Пауза для приветствия")),
			cdVar->value(),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			controller->show(Box(VrgDetail::ChoiceBox,
				Ru("Пауза между приветствиями"),
				QString("autoreply_cooldown"),
				Options{
					{ "15", Ru("15 минут") },
					{ "30", Ru("30 минут") },
					{ "60", Ru("1 час") },
					{ "180", Ru("3 часа") },
					{ "720", Ru("12 часов") },
				},
				Fn<void(QString)>([=](QString v) { *cdVar = cdName(v); })));
		});

		const auto cmdCdName = [](const QString &key) {
			return (key == "60") ? Ru("1 минута")
				: (key == "300") ? Ru("5 минут")
				: (key == "900") ? Ru("15 минут")
				: (key == "-1") ? Ru("Как у приветствия")
				: Ru("5 секунд");
		};
		const auto cmdCdVar = content->lifetime().make_state<
			rpl::variable<QString>>(cmdCdName(Vrg::Conf::Str("autoreply_cmd_cooldown")));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Пауза для команд")),
			cmdCdVar->value(),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			controller->show(Box(VrgDetail::ChoiceBox,
				Ru("Пауза между командами"),
				QString("autoreply_cmd_cooldown"),
				Options{
					{ "5", Ru("5 секунд") },
					{ "60", Ru("1 минута") },
					{ "300", Ru("5 минут") },
					{ "900", Ru("15 минут") },
					{ "-1", Ru("Как у приветствия") },
				},
				Fn<void(QString)>([=](QString v) { *cmdCdVar = cmdCdName(v); })));
		});

		VrgDetail::AddCaption(content, Ru("Сообщения"));
		const auto openEditor = [=](
				const QString &boxTitle,
				const QString &exampleHint,
				const QString &richKey,
				const QString &confKey) {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				box->setTitle(rpl::single(boxTitle));
				box->setWidth(st::boxWideWidth);
				const auto hint = box->addRow(
					object_ptr<Ui::FlatLabel>(
						box,
						exampleHint,
						st::boxDividerLabel),
					st::boxRowPadding);
				hint->setTextColorOverride(QColor(0x8c, 0x8c, 0x8c));
				const auto field = box->addRow(
					object_ptr<Ui::InputField>(
						box,
						st::defaultInputField,
						Ui::InputField::Mode::MultiLine,
						rpl::single(QString())),
					st::boxRowPadding);
				field->setMinHeight(140);
				field->setMaxHeight(260);
				const auto style = InitMessageField(
					controller,
					field,
					[=](not_null<DocumentData*>) { return true; });
				box->lifetime().add([style] {});
				field->setTextWithTags(
					Vrg::AutoReply::LoadRich(richKey, confKey),
					Ui::InputField::HistoryAction::Clear);
				Ui::Emoji::SuggestionsController::Init(
					box,
					field,
					&controller->session(),
					{
						.suggestCustomEmoji = true,
						.allowCustomWithoutPremium = [=](not_null<DocumentData*>) {
							return true;
						},
					});
				box->setFocusCallback([=] {
					field->setFocusFast();
				});
				box->addButton(rpl::single(Ru("Сохранить")), [=] {
					const auto tagged = field->getTextWithTags();
					Vrg::Conf::Set(confKey, Vrg::AutoReply::Pack(tagged.text));
					Vrg::AutoReply::SaveRich(richKey, tagged);
					box->closeBox();
					controller->showToast(Ru("Сохранено"));
				});
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			}));
		};

		AddButtonWithLabel(
			content,
			rpl::single(Ru("Текст приветствия")),
			rpl::single(Ru("изменить")),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			openEditor(
				Ru("Текст приветствия"),
				Ru("Пример: Меня нет на месте. Напиши /вызов чтобы позвать"),
				QStringLiteral("greeting"),
				QStringLiteral("autoreply_text"));
		});

		AddButtonWithLabel(
			content,
			rpl::single(Ru("Команды (/команда -> ответ)")),
			rpl::single(Ru("изменить")),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			openEditor(
				Ru("Команды автоответчика"),
				Ru("Пример: /вызов -> Сейчас отвечу"),
				QStringLiteral("cmds"),
				QStringLiteral("autoreply_cmds"));
		});

		VrgDetail::AddCaption(content, Ru("Не отвечать этим людям"));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("+ Исключить человека из автоответчика…")),
			rpl::single(QString()),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			Window::ShowChooseRecipientBox(controller, [=](
					not_null<Data::Thread*> thread) {
				const auto peer = thread->peer();
				Vrg::AutoReply::AddIgnored(
					uint64(peer->id.value),
					peer->name());
				controller->showToast(
					Ru("Исключён из автоответчика: ") + peer->name());
				controller->showSettings(VrgAutoReplyId());
				return true;
			});
		});

		for (const auto &entry : Vrg::AutoReply::ListIgnored()) {
			const auto peerId = entry.peerId;
			const auto title = entry.title;
			const auto row = AddButtonWithLabel(
				content,
				rpl::single(title),
				rpl::single(Ru("убрать")),
				st::settingsButtonNoIcon);
			row->setClickedCallback([=] {
				controller->show(Ui::MakeConfirmBox({
					.text = Ru("Вы точно хотите убрать %1 из исключений автоответчика?").arg(title),
					.confirmed = [=](Fn<void()> close) {
						close();
						Vrg::AutoReply::RemoveIgnored(peerId);
						delete row;
						content->resizeToWidth(content->width());
						controller->showToast(Ru("Убран из исключений: ") + title);
					},
					.confirmText = Ru("Убрать"),
				}));
			});
		}

		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return rpl::single(VrgDetail::Ru("Автоответчик"));
	}
};

class VrgDeleted : public Section<VrgDeleted> {
public:
	VrgDeleted(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		using VrgDetail::Ru;
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
		VrgDetail::AddCaption(content, Ru("Режим шпиона"));
		const auto card = VrgDetail::AddCard(content);
		VrgDetail::AddConfSwitch(
			card,
			Ru("Сохранять удалённые сообщения"),
			"keep_deleted",
			true,
			rpl::never<bool>(),
			Ru("Когда вы или собеседник удаляете сообщение, оно остаётся "
				"у вас в чате с пометкой «удалено». Сообщения сохраняются "
				"навсегда между перезапусками VRG."));
		VrgDetail::AddConfSwitch(
			card,
			Ru("Сохранять одноразовые медиа"),
			"keep_ttl",
			true,
			rpl::never<bool>(),
			Ru("Одноразовые фото, видео, кружочки и голосовые сообщения "
				"не сгорают после просмотра, сохраняются между запусками "
				"и доступны для скачивания через контекстное меню."));
		VrgDetail::AddConfSwitch(
			card,
			Ru("Сохранять историю правок"),
			"keep_edited",
			true,
			rpl::never<bool>(),
			Ru("Записывает старые версии отредактированных сообщений на диск, "
				"чтобы их можно было посмотреть через меню сообщения даже после "
				"перезапуска VRG."));
		VrgDetail::AddConfSwitch(
			card,
			Ru("Разрешить копирование и пересылку"),
			"allow_copy_restricted",
			true,
			rpl::never<bool>(),
			Ru("Снимает запрет на копирование текста, пересылку и скачивание "
				"медиа в каналах и закрытых чатах, а также отключает защиту от скриншотов."));

		VrgDetail::AddCaption(content, Ru("Кэш"));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Очистить кэш")),
			rpl::single(QString()),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			controller->show(Ui::MakeConfirmBox({
				.text = Ru("Вы уверены, что хотите очистить кэш?"),
				.confirmed = [=](Fn<void()> close) {
					close();
					auto &data = controller->session().data();
					data.cache().clear();
					data.cacheBigFile().clear();
					controller->showToast(Ru("Кэш очищен"));
				},
				.confirmText = Ru("Очистить"),
			}));
		});

		VrgDetail::AddCaption(content, Ru("Вид"));
		using Options = std::vector<std::pair<QString, QString>>;
		const auto markName = [](const QString &key) {
			return (key == "text")
				? VrgDetail::Ru("слово «удалено»")
				: (key == "none")
				? VrgDetail::Ru("без пометки")
				: VrgDetail::Ru("значок корзины");
		};
		const auto mark = content->lifetime().make_state<
			rpl::variable<QString>>(markName(Vrg::Conf::Str("deleted_mark")));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Пометка")),
			mark->value(),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			controller->show(Box(VrgDetail::ChoiceBox,
				VrgDetail::Ru("Пометка удалённых"),
				QString("deleted_mark"),
				Options{
					{ "icon", VrgDetail::Ru("Значок корзины") },
					{ "text", VrgDetail::Ru("Слово «удалено»") },
					{ "none", VrgDetail::Ru("Без пометки") },
				},
				Fn<void(QString)>([=](QString v) { *mark = markName(v); })));
		});
		const auto markColorName = [](const QString &key) {
			return (key == "#ff5c5c") ? VrgDetail::Ru("Красный")
				: (key == "#ff9f43") ? VrgDetail::Ru("Оранжевый")
				: (key == "#4dabf7") ? VrgDetail::Ru("Синий")
				: (key == "#b197fc") ? VrgDetail::Ru("Фиолетовый")
				: (key == "#51cf66") ? VrgDetail::Ru("Зелёный")
				: VrgDetail::Ru("Стандартный");
		};
		const auto markColor = content->lifetime().make_state<
			rpl::variable<QString>>(markColorName(Vrg::Conf::Str("deleted_mark_color")));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Цвет значка удалённых")),
			markColor->value(),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			controller->show(Box(VrgDetail::ChoiceBox,
				VrgDetail::Ru("Цвет значка удалённых"),
				QString("deleted_mark_color"),
				Options{
					{ "default", VrgDetail::Ru("Стандартный") },
					{ "#ff5c5c", VrgDetail::Ru("Красный") },
					{ "#ff9f43", VrgDetail::Ru("Оранжевый") },
					{ "#4dabf7", VrgDetail::Ru("Синий") },
					{ "#b197fc", VrgDetail::Ru("Фиолетовый") },
					{ "#51cf66", VrgDetail::Ru("Зелёный") },
				},
				Fn<void(QString)>([=](QString v) { *markColor = markColorName(v); })));
		});
		const auto dimName = [](const QString &key) {
			return (key == "1")
				? QString("75%")
				: (key == "2")
				? QString("50%")
				: VrgDetail::Ru("обычная");
		};
		const auto dim = content->lifetime().make_state<
			rpl::variable<QString>>(dimName(Vrg::Conf::Str("deleted_dim")));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Прозрачность удалённых")),
			dim->value(),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			controller->show(Box(VrgDetail::ChoiceBox,
				VrgDetail::Ru("Прозрачность удалённых"),
				QString("deleted_dim"),
				Options{
					{ "0", VrgDetail::Ru("Обычная") },
					{ "1", VrgDetail::Ru("Слегка прозрачные (75%)") },
					{ "2", VrgDetail::Ru("Сильно прозрачные (50%)") },
				},
				Fn<void(QString)>([=](QString v) { *dim = dimName(v); })));
		});
		VrgDetail::AddNote(
			content,
			Ru("Новая пометка и прозрачность применяются к сообщениям, "
				"которые отрисовываются заново: переоткройте чат."));
		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return rpl::single(VrgDetail::Ru("Удалённые сообщения"));
	}
};

class VrgLook : public Section<VrgLook> {
public:
	VrgLook(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		using VrgDetail::Ru;
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

		VrgDetail::AddCaption(content, Ru("Тема"));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Чёрно-белая тема VRG")),
			rpl::single(Ru("применить")),
			st::settingsButton,
			{ &st::menuIconPalette }
		)->setClickedCallback([=] {
			Vrg::ApplyVrgTheme();
		});

		VrgDetail::AddCaption(content, Ru("Иконка приложения"));
		const auto label = content->lifetime().make_state<
			rpl::variable<QString>>(VrgDetail::IconName(
				Vrg::Conf::Str("app_icon")));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Иконка")),
			label->value(),
			st::settingsButton,
			{ &st::menuIconInfo }
		)->setClickedCallback([=] {
			controller->show(Box(VrgDetail::IconBox, [=](QString name) {
				*label = VrgDetail::IconName(name);
				VrgDetail::PromptRestart(controller);
			}));
		});

		VrgDetail::AddCaption(content, Ru("Список чатов и сообщения"));
		VrgDetail::AddConfSwitch(
			content,
			Ru("Скруглённые карточки чатов"),
			"rounded_chats",
			true,
			rpl::never<bool>(),
			Ru("Отрисовывает активный и выделенный чат в виде аккуратной "
				"скруглённой карточки с отступами. Применяется сразу."));
		VrgDetail::AddConfSwitch(
			content,
			Ru("Скрыть истории сверху"),
			"hide_stories",
			false,
			rpl::never<bool>(),
			Ru("Убирает панель кружочков историй над списком чатов. "
				"Применится после перезапуска."),
			[=](bool) {
				VrgDetail::PromptRestart(controller);
			});
		VrgDetail::AddConfSwitch(
			content,
			Ru("Скрывать похожие каналы"),
			"hide_similar_channels",
			true,
			rpl::never<bool>(),
			Ru("Убирает блок рекомендаций похожих каналов под постами и при подписке."));
		VrgDetail::AddConfSwitch(
			content,
			Ru("Скрыть папку «Все чаты»"),
			"hide_all_chats",
			false,
			rpl::never<bool>(),
			Ru("Убирает стандартную вкладку «Все чаты», оставляя только ваши "
				"пользовательские папки. Применится после перезапуска."),
			[=](bool) {
				VrgDetail::PromptRestart(controller);
			});
		VrgDetail::AddConfSwitch(
			content,
			Ru("Скрывать системные сообщения в группах"),
			"hide_service_messages",
			false,
			rpl::never<bool>(),
			Ru("Скрывает в группах сообщения о входе/выходе участников, "
				"смене фото, названия и закрепах."));

		using Options = std::vector<std::pair<QString, QString>>;
		const auto radiusName = [](const QString &key) {
			return (key == "2") ? VrgDetail::Ru("Острые (2px)")
				: (key == "6") ? VrgDetail::Ru("Слегка (6px)")
				: (key == "10") ? VrgDetail::Ru("Среднее (10px)")
				: (key == "18") ? VrgDetail::Ru("Сильное (18px)")
				: (key == "24") ? VrgDetail::Ru("Максимальное (24px)")
				: VrgDetail::Ru("Стандартное (14px)");
		};
		const auto radiusVar = content->lifetime().make_state<
			rpl::variable<QString>>(radiusName(Vrg::Conf::Str("bubble_radius")));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Скругление пузырей сообщений")),
			radiusVar->value(),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			controller->show(Box(VrgDetail::ChoiceBox,
				VrgDetail::Ru("Скругление пузырей"),
				QString("bubble_radius"),
				Options{
					{ "-1", VrgDetail::Ru("Стандартное (14px)") },
					{ "2", VrgDetail::Ru("Острые (2px)") },
					{ "6", VrgDetail::Ru("Слегка (6px)") },
					{ "10", VrgDetail::Ru("Среднее (10px)") },
					{ "18", VrgDetail::Ru("Сильное (18px)") },
					{ "24", VrgDetail::Ru("Максимальное (24px)") },
				},
				Fn<void(QString)>([=](QString v) { *radiusVar = radiusName(v); })));
		});

		VrgDetail::AddCaption(content, Ru("Поле ввода сообщений"));
		VrgDetail::AddConfSwitch(
			content,
			Ru("Скрыть кнопку подарков"),
			"hide_compose_gifts",
			false);
		VrgDetail::AddConfSwitch(
			content,
			Ru("Скрыть кнопку отложенных сообщений"),
			"hide_compose_scheduled",
			false);
		VrgDetail::AddConfSwitch(
			content,
			Ru("Скрыть кнопку беззвучной отправки"),
			"hide_compose_silent",
			false);
		VrgDetail::AddConfSwitch(
			content,
			Ru("Скрыть кнопку команд бота (/)"),
			"hide_compose_bot",
			false);
		VrgDetail::AddConfSwitch(
			content,
			Ru("Секунды у времени сообщений"),
			"show_seconds",
			false,
			rpl::never<bool>(),
			Ru("Показывает время сообщений с секундами (14:50:08). "
				"Применится к сообщениям, которые отрисовываются заново: "
				"переоткройте чат."));

		VrgDetail::AddCaption(content, Ru("Шрифт"));
		VrgDetail::AddConfSwitch(
			content,
			Ru("Шрифт Inter (как на сайте)"),
			"font_inter",
			true,
			rpl::never<bool>(),
			Ru("Использовать шрифт Inter по умолчанию вместо стандартного "
				"шрифта Telegram. Применится после перезапуска."),
			[=](bool) {
				VrgDetail::PromptRestart(controller);
			});
		{
			auto *const appSettings = &Core::App().settings();
			const auto familyVar = content->lifetime().make_state<
				rpl::variable<QString>>(appSettings->customFontFamily());
			auto fontLabel = familyVar->value() | rpl::map([](QString family) {
				return family.isEmpty()
					? (Vrg::Conf::Bool("font_inter", true)
						? Ru("Inter (по умолчанию)")
						: tr::lng_font_default(tr::now))
					: (family == style::SystemFontTag())
					? tr::lng_font_system(tr::now)
					: family;
			});
			const auto themeLifetime = content->lifetime().make_state<
				rpl::lifetime>();
			AddButtonWithLabel(
				content,
				rpl::single(Ru("Выбрать системный шрифт")),
				std::move(fontLabel),
				st::settingsButton,
				{ &st::menuIconFont }
			)->setClickedCallback([=] {
				const auto save = [=](QString chosen) {
					*familyVar = chosen;
					appSettings->setCustomFontFamily(chosen);
					Local::writeSettings();
					Core::Restart();
				};
				themeLifetime->destroy();
				const auto theme = std::shared_ptr<Ui::ChatTheme>(
					Window::Theme::DefaultChatThemeOn(*themeLifetime));
				const auto generateBg = [=] {
					const auto size = st::boxWidth;
					const auto ratio = style::DevicePixelRatio();
					auto result = QImage(
						QSize(size, size) * ratio,
						QImage::Format_ARGB32_Premultiplied);
					auto p = QPainter(&result);
					Window::SectionWidget::PaintBackground(
						p,
						theme.get(),
						QSize(size, size * 3),
						QRect(0, 0, size, size));
					p.end();
					return result;
				};
				controller->show(Box(
					Ui::ChooseFontBox,
					generateBg,
					familyVar->current(),
					save));
			});
		}

		VrgDetail::AddCaption(content, Ru("Размер текста"));
		VrgDetail::AddConfSlider(
			content,
			Ru("Размер текста во всём интерфейсе (без масштаба окон)"),
			"text_scale",
			80,
			140,
			5,
			100,
			[=](int) {
				VrgDetail::PromptRestart(controller);
			});

		VrgDetail::AddCaption(content, Ru("Приветствие"));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Экран приветствия")),
			rpl::single(QString()),
			st::settingsButton,
			{ &st::menuIconShowInChat }
		)->setClickedCallback([=] {
			Vrg::ShowWelcome(controller);
		});
		Ui::AddSkip(content);
		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return rpl::single(VrgDetail::Ru("Оформление"));
	}
};

class VrgPlayer : public Section<VrgPlayer> {
public:
	VrgPlayer(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		using VrgDetail::Ru;
		using Options = std::vector<std::pair<QString, QString>>;
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
		const auto notify = [](bool) {
			Vrg::Music::SharedPlayer()->notifyVisualSettingsChanged();
		};

		VrgDetail::AddCaption(content, Ru("Мини-плеер в чатах"));
		VrgDetail::AddConfSwitch(
			content,
			Ru("Показывать мини-плеер в чатах"),
			"player_minibar",
			true,
			rpl::never<bool>(),
			Ru("Показывает компактную панель плеера поверх окна."),
			notify);
		VrgDetail::AddConfSwitch(
			content,
			Ru("Перетаскивание мини-плеера"),
			"player_minibar_draggable",
			false,
			rpl::never<bool>(),
			Ru("Позволяет свободно перетаскивать мини-плеер мышкой в любое "
				"место окна. Позиция сохраняется между запусками."),
			notify);
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Сбросить позицию мини-плеера")),
			rpl::single(QString()),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			Vrg::Conf::SetInt("player_minibar_x", 8);
			Vrg::Conf::SetInt("player_minibar_y", -1);
			notify(true);
			controller->showToast(Ru("Позиция мини-плеера сброшена"));
		});

		const auto styleName = [](const QString &key) {
			return (key == "compact")
				? VrgDetail::Ru("Компактный (48px)")
				: (key == "minimal")
				? VrgDetail::Ru("Минимал (без градиента)")
				: VrgDetail::Ru("Стеклянная карточка");
		};
		const auto styleVar = content->lifetime().make_state<
			rpl::variable<QString>>(styleName(Vrg::Conf::Str("player_minibar_style")));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Стиль мини-плеера")),
			styleVar->value(),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			controller->show(Box(VrgDetail::ChoiceBox,
				VrgDetail::Ru("Стиль мини-плеера"),
				QString("player_minibar_style"),
				Options{
					{ "card", VrgDetail::Ru("Стеклянная карточка") },
					{ "compact", VrgDetail::Ru("Компактный (48px)") },
					{ "minimal", VrgDetail::Ru("Минимал (без градиента)") },
				},
				Fn<void(QString)>([=](QString v) {
					*styleVar = styleName(v);
					notify(true);
				})));
		});

		const auto layoutName = [](const QString &key) {
			return (key == "flipped")
				? VrgDetail::Ru("Кнопки слева · Обложка справа")
				: VrgDetail::Ru("Обложка слева · Кнопки справа");
		};
		const auto layoutVar = content->lifetime().make_state<
			rpl::variable<QString>>(layoutName(Vrg::Conf::Str("player_mini_layout")));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Расположение элементов")),
			layoutVar->value(),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			controller->show(Box(VrgDetail::ChoiceBox,
				VrgDetail::Ru("Расположение элементов"),
				QString("player_mini_layout"),
				Options{
					{ "default", VrgDetail::Ru("Обложка слева · Кнопки справа") },
					{ "flipped", VrgDetail::Ru("Кнопки слева · Обложка справа") },
				},
				Fn<void(QString)>([=](QString v) {
					*layoutVar = layoutName(v);
					notify(true);
				})));
		});

		VrgDetail::AddConfSlider(
			content,
			Ru("Размер мини-плеера"),
			"player_mini_scale",
			100,
			150,
			5,
			100,
			[=](int) { notify(true); });
		VrgDetail::AddConfSlider(
			content,
			Ru("Размер текста в мини-плеере"),
			"player_mini_text",
			100,
			160,
			5,
			100,
			[=](int) { notify(true); });
		VrgDetail::AddConfSwitch(
			content,
			Ru("Показывать обложку трека"),
			"player_mini_art",
			true,
			rpl::never<bool>(),
			QString(),
			notify);
		VrgDetail::AddConfSwitch(
			content,
			Ru("Показывать время (MM:SS)"),
			"player_mini_time",
			true,
			rpl::never<bool>(),
			QString(),
			notify);
		VrgDetail::AddConfSwitch(
			content,
			Ru("Показывать полосу перемотки"),
			"player_mini_seek",
			true,
			rpl::never<bool>(),
			QString(),
			notify);
		VrgDetail::AddConfSwitch(
			content,
			Ru("Кнопки Назад / Вперёд в мини-плеере"),
			"player_mini_skip",
			true,
			rpl::never<bool>(),
			QString(),
			notify);

		VrgDetail::AddCaption(content, Ru("Окно музыки"));
		VrgDetail::AddConfSlider(
			content,
			Ru("Масштаб окна VRG Music (Ctrl + колесо мыши)"),
			"music_zoom",
			100,
			200,
			10,
			100,
			[=](int) { notify(true); });

		VrgDetail::AddCaption(content, Ru("Источники музыки"));
		{
			const auto sourceName = [](const QString &key) {
				return (key == "sc")
					? VrgDetail::Ru("Только SoundCloud")
					: (key == "yt")
					? VrgDetail::Ru("Только YouTube")
					: VrgDetail::Ru("SoundCloud и YouTube");
			};
			const auto sourceVar = content->lifetime().make_state<
				rpl::variable<QString>>(sourceName(Vrg::Conf::Str("search_sources")));
			AddButtonWithLabel(
				content,
				rpl::single(Ru("Где искать музыку")),
				sourceVar->value(),
				st::settingsButtonNoIcon
			)->setClickedCallback([=] {
				controller->show(Box(VrgDetail::ChoiceBox,
					VrgDetail::Ru("Где искать музыку"),
					QString("search_sources"),
					Options{
						{ "all", VrgDetail::Ru("SoundCloud и YouTube") },
						{ "sc", VrgDetail::Ru("Только SoundCloud") },
						{ "yt", VrgDetail::Ru("Только YouTube") },
					},
					Fn<void(QString)>([=](QString v) {
						*sourceVar = sourceName(v);
					})));
			});
		}

		{
			const auto browserName = [](const QString &key) {
				return key.isEmpty() ? VrgDetail::Ru("Выключено") : (key == "firefox")
					? QString("Firefox")
					: (key == "chrome") ? QString("Chrome")
					: (key == "chromium") ? QString("Chromium")
					: (key == "brave") ? QString("Brave")
					: (key == "edge") ? QString("Edge")
					: (key == "vivaldi") ? QString("Vivaldi")
					: key;
			};
			const auto browserVar = content->lifetime().make_state<
				rpl::variable<QString>>(browserName(Vrg::Conf::Str("yt_cookies_browser")));
			AddButtonWithLabel(
				content,
				rpl::single(Ru("Входить в YouTube через браузер")),
				browserVar->value(),
				st::settingsButtonNoIcon
			)->setClickedCallback([=] {
				controller->show(Box(VrgDetail::ChoiceBox,
					VrgDetail::Ru("Входить в YouTube через браузер"),
					QString("yt_cookies_browser"),
					Options{
						{ "", VrgDetail::Ru("Выключено") },
						{ "firefox", QString("Firefox") },
						{ "chrome", QString("Chrome") },
						{ "chromium", QString("Chromium") },
						{ "brave", QString("Brave") },
						{ "edge", QString("Edge") },
						{ "vivaldi", QString("Vivaldi") },
					},
					Fn<void(QString)>([=](QString v) {
						*browserVar = browserName(v);
					})));
			});
			VrgDetail::AddNote(
				content,
				Ru("Если YouTube просит войти: войди в Google в выбранном "
					"браузере, и VRG возьмёт оттуда куки. Пароль VRG не видит, "
					"данные остаются на этом компьютере."));
		}

		VrgDetail::AddCaption(content, Ru("Большой плеер и текст"));
		VrgDetail::AddConfSwitch(
			content,
			Ru("Радио (похожие треки)"),
			"player_radio",
			false,
			rpl::never<bool>(),
			Ru("Когда плейлист или очередь доходят до конца, автоматически "
				"подгружает похожие треки из SoundCloud."),
			notify);
		VrgDetail::AddConfSwitch(
			content,
			Ru("Бегущая строка для длинных названий"),
			"player_marquee",
			true,
			rpl::never<bool>(),
			Ru("Плавно прокручивает длинное название трека и имя артиста "
				"вместо обрезания троеточием."),
			notify);
		VrgDetail::AddConfSwitch(
			content,
			Ru("Кнопка текста песни"),
			"player_bar_lyrics",
			true,
			rpl::never<bool>(),
			QString(),
			notify);
		VrgDetail::AddConfSwitch(
			content,
			Ru("Кнопка эквалайзера"),
			"player_bar_eq",
			true,
			rpl::never<bool>(),
			QString(),
			notify);
		VrgDetail::AddConfSwitch(
			content,
			Ru("Регулировка громкости"),
			"player_bar_vol",
			true,
			rpl::never<bool>(),
			QString(),
			notify);
		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return rpl::single(VrgDetail::Ru("Плеер"));
	}
};

class VrgMisc : public Section<VrgMisc> {
public:
	VrgMisc(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		using VrgDetail::Ru;
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

		VrgDetail::AddCaption(content, Ru("Профиль"));
		VrgDetail::AddConfSwitch(
			content,
			Ru("Показывать ID в профиле"),
			"show_user_id",
			true,
			rpl::never<bool>(),
			Ru("Добавляет строку с Telegram ID в профиль пользователей, "
				"групп и каналов. Клик копирует ID в буфер."));
		VrgDetail::AddConfSwitch(
			content,
			Ru("Показывать дату регистрации"),
			"show_reg_date",
			true,
			rpl::never<bool>(),
			Ru("Показывает примерный месяц и год регистрации аккаунта "
				"в профиле пользователя по диапазону Telegram ID."));

		VrgDetail::AddCaption(content, Ru("Шаблоны и резервное копирование"));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Быстрые шаблоны ответов")),
			rpl::single(QString()),
			st::settingsButton,
			{ &st::menuIconSavedMessages }
		)->setClickedCallback([=] {
			Vrg::Templates::ShowManageBox(controller->uiShow());
		});
		AddDividerText(
			content,
			rpl::single(Ru(
				"Заготовки текста для быстрой вставки: нажмите правой кнопкой "
				"мыши в поле ввода сообщения → «Быстрые шаблоны».")));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Экспорт настроек и музыки")),
			rpl::single(QString()),
			st::settingsButton,
			{ &st::menuIconExport }
		)->setClickedCallback([=] {
			Vrg::Backup::Export(controller);
		});
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Импорт настроек и музыки")),
			rpl::single(QString()),
			st::settingsButton,
			{ &st::menuIconRestore }
		)->setClickedCallback([=] {
			Vrg::Backup::Import(controller);
		});

		VrgDetail::AddCaption(content, Ru("Подтверждения"));
		VrgDetail::AddConfSwitch(
			content,
			Ru("Авточтение реакций и упоминаний"),
			"auto_read_reactions",
			true,
			rpl::never<bool>(),
			Ru("Автоматически сбрасывает непрочитанные реакции (♡) и упоминания (@) "
				"при входе в чат, чтобы не нажимать их вручную."));
		VrgDetail::AddConfSwitch(
			content,
			Ru("Подтверждение перед звонком"),
			"confirm_call",
			false,
			rpl::never<bool>(),
			Ru("Спрашивает подтверждение перед началом исходящего звонка, "
				"чтобы не позвонить случайно."));
		VrgDetail::AddConfSwitch(
			content,
			Ru("Подтверждение перед отправкой стикера"),
			"confirm_sticker",
			false,
			rpl::never<bool>(),
			Ru("Показывает окно подтверждения при клике на стикер."));
		VrgDetail::AddConfSwitch(
			content,
			Ru("Подтверждение перед отправкой GIF"),
			"confirm_gif",
			false,
			rpl::never<bool>(),
			Ru("Показывает окно подтверждения при выборе GIF."));


		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return rpl::single(VrgDetail::Ru("Удобство"));
	}
};

class VrgSecurity : public Section<VrgSecurity> {
public:
	VrgSecurity(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		using VrgDetail::Ru;
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

		VrgDetail::AddCaption(content, Ru("Защита и пароли"));

		const auto promptPassword = [=](
				const QString &title,
				const QString &placeholder,
				Fn<bool(const QString&)> checkFn,
				Fn<void(const QString&)> onOk) {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				box->setTitle(rpl::single(title));
				const auto wrap = box->addRow(
					object_ptr<Ui::FixedHeightWidget>(box, 58),
					st::boxRowPadding);
				const auto field = Ui::CreateChild<Ui::PasswordInput>(
					wrap,
					st::defaultInputField,
					rpl::single(placeholder));
				wrap->widthValue() | rpl::on_next([=](int w) {
					field->resizeToWidth(w);
					wrap->setFixedHeight(std::max(54, field->height() + 4));
				}, wrap->lifetime());
				const auto submit = [=] {
					const auto txt = field->getLastText();
					if (checkFn && !checkFn(txt)) {
						field->showError();
						controller->showToast(Ru("Неверный пароль"));
						return;
					}
					box->closeBox();
					onOk(txt);
				};
				QObject::connect(field, &Ui::PasswordInput::submitted, submit);
				box->addButton(rpl::single(Ru("Подтвердить")), submit);
				box->addLeftButton(tr::lng_cancel(), [=] { box->closeBox(); });
				box->setFocusCallback([=] { field->setFocusFast(); });
			}));
		};

		const auto requireMasterOrRun = [=](Fn<void()> onOk) {
			if (!Vrg::Security::HasMaster()) {
				onOk();
				return;
			}
			promptPassword(
				Ru("Мастер-пароль VRG"),
				Ru("Введите мастер-пароль"),
				[](const QString &p) { return Vrg::Security::VerifyMaster(p); },
				[=](const QString &) { onOk(); });
		};

		const auto masterLabel = content->lifetime().make_state<
			rpl::variable<QString>>(
				Vrg::Security::HasMaster() ? Ru("установлен") : Ru("не задан"));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Мастер-пароль VRG")),
			masterLabel->value(),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			requireMasterOrRun([=] {
				promptPassword(
					Vrg::Security::HasMaster()
						? Ru("Новый мастер-пароль")
						: Ru("Мастер-пароль VRG"),
					Vrg::Security::HasMaster()
						? Ru("Новый пароль (пусто = снять)")
						: Ru("Введите пароль"),
					nullptr,
					[=](const QString &p) {
						Vrg::Security::SetMaster(p);
						*masterLabel = Vrg::Security::HasMaster()
							? Ru("установлен")
							: Ru("не задан");
						controller->showToast(p.isEmpty()
							? Ru("Мастер-пароль снят")
							: Ru("Мастер-пароль сохранён"));
					});
			});
		});

		AddButtonWithLabel(
			content,
			rpl::single(Ru("Пароли на смену аккаунтов")),
			rpl::single(QString()),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			requireMasterOrRun([=] {
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					box->setTitle(rpl::single(Ru("Пароли аккаунтов")));
					for (const auto &[idx, account] : Core::App().domain().accounts()) {
						const auto s = account->maybeSession();
						if (!s) {
							continue;
						}
						const auto uid = s->userId().bare;
						const auto name = s->user()->name();
						const auto hasLock = Vrg::Security::HasAccountLock(uid);
						const auto btn = box->addRow(
							object_ptr<Ui::SettingsButton>(
								box,
								rpl::single(name + (hasLock ? Ru("  🔒") : QString())),
								st::settingsButtonNoIcon),
							QMargins(0, 0, 0, 0));
						btn->setClickedCallback([=] {
							const auto askNew = [=] {
								promptPassword(
									Ru("Пароль для ") + name,
									Ru("Новый пароль (пусто = снять)"),
									nullptr,
									[=](const QString &p) {
										Vrg::Security::SetAccountLock(uid, p);
										controller->showToast(p.isEmpty()
											? Ru("Пароль снят с аккаунта")
											: Ru("Пароль установлен на аккаунт"));
										box->closeBox();
									});
							};
							if (hasLock && !Vrg::Security::HasMaster()) {
								promptPassword(
									Ru("Текущий пароль для ") + name,
									Ru("Введите текущий пароль"),
									[=](const QString &p) {
										return Vrg::Security::VerifyAccount(uid, p);
									},
									[=](const QString &) { askNew(); });
							} else {
								askNew();
							}
						});
					}
					box->addButton(tr::lng_close(), [=] { box->closeBox(); });
				}));
			});
		});

		AddButtonWithLabel(
			content,
			rpl::single(Ru("Сбросить пароли чатов")),
			rpl::single(QString()),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			const auto locked = Vrg::ChatLock::ListLocked();
			if (locked.empty()) {
				controller->showToast(Ru("Нет чатов с паролем"));
				return;
			}
			const auto masterVerified = Vrg::Security::HasMaster();
			requireMasterOrRun([=] {
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					box->setTitle(rpl::single(Ru("Сбросить пароль чата")));
					for (const auto &entry : locked) {
						const auto peerId = entry.peerId;
						const auto title = entry.title;
						const auto btn = box->addRow(
							object_ptr<Ui::SettingsButton>(
								box,
								rpl::single(title),
								st::settingsButtonNoIcon),
							QMargins(0, 0, 0, 0));
						btn->setClickedCallback([=] {
							if (masterVerified) {
								Vrg::ChatLock::ClearPassword(peerId);
								btn->hide();
								controller->showToast(Ru("Пароль снят: ") + title);
							} else {
								promptPassword(
									Ru("Введите текущий пароль чата"),
									Ru("Пароль для ") + title,
									[=](const QString &p) {
										return Vrg::ChatLock::Verify(peerId, p);
									},
									[=](const QString &) {
										Vrg::ChatLock::ClearPassword(peerId);
										btn->hide();
										controller->showToast(Ru("Пароль снят: ") + title);
									});
							}
						});
					}
					box->addButton(tr::lng_close(), [=] { box->closeBox(); });
				}));
			});
		});

		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return rpl::single(VrgDetail::Ru("Защита и пароли"));
	}
};

class VrgVoiceChanger : public Section<VrgVoiceChanger> {
public:
	VrgVoiceChanger(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		using VrgDetail::Ru;
		namespace VC = Vrg::VoiceChanger;
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

		VrgDetail::AddCaption(content, Ru("Голосовые сообщения"));
		VrgDetail::AddConfSwitch(
			content,
			Ru("Включить изменение голоса (ГС)"),
			"voice_changer_enabled",
			false,
			rpl::never<bool>(),
			Ru("Автоматически обрабатывает записанные голосовые сообщения "
				"перед отправкой через выбранный пресет или ползунки. "
				"Видеосообщения (кружочки) не затрагиваются."));

		VrgDetail::AddCaption(content, Ru("Пресеты"));
		const auto &presets = VC::Presets();
		auto initialIndex = 0;
		const auto currentPreset = VC::PresetId();
		for (int i = 0; i < int(presets.size()); ++i) {
			if (presets[i].id == currentPreset) {
				initialIndex = i;
				break;
			}
		}
		const auto customIndex = int(presets.size()) - 1;
		const auto presetGroup = std::make_shared<Ui::RadiobuttonGroup>(initialIndex);
		for (int i = 0; i < int(presets.size()); ++i) {
			const auto &p = presets[i];
			content->add(
				object_ptr<Ui::Radiobutton>(
					content,
					presetGroup,
					i,
					p.title),
				st::boxRowPadding);
		}
		Ui::AddSkip(content);

		VrgDetail::AddCaption(content, Ru("Ручная настройка (ползунки)"));

		const auto syncing = content->lifetime().make_state<bool>(false);
		const auto markCustom = [=] {
			if (*syncing) {
				return;
			}
			VC::SetPresetId(u"custom"_q);
			*syncing = true;
			presetGroup->setValue(customIndex);
			*syncing = false;
		};

		struct SliderHandle {
			Fn<void(int)> syncValue;
		};
		const auto makeParamSlider = [&](
				const QString &title,
				int minVal,
				int maxVal,
				int step,
				int initialVal,
				Fn<QString(int)> formatLabel,
				Fn<void(int)> onSave) -> SliderHandle {
			content->add(
				object_ptr<Ui::FlatLabel>(
					content,
					rpl::single(title),
					st::defaultSubsectionTitle),
				st::defaultSubsectionTitlePadding);
			auto swl = MakeSliderWithLabel(
				content,
				st::settingsScale,
				st::settingsScaleLabel,
				st::normalFont->spacew * 2,
				st::settingsScaleLabel.style.font->width("+100%"),
				true);
			content->add(
				std::move(swl.widget),
				st::settingsBigScalePadding);
			const auto slider = swl.slider;
			const auto label = swl.label;
			const auto count = ((maxVal - minVal) / step) + 1;
			const auto convert = [=](int index) {
				return minVal + index * step;
			};
			label->setText(formatLabel(initialVal));
			slider->setPseudoDiscrete(
				count,
				convert,
				initialVal,
				[=](int val) {
					label->setText(formatLabel(val));
				},
				[=](int val) {
					label->setText(formatLabel(val));
					if (!*syncing) {
						onSave(val);
						markCustom();
					}
				});
			return SliderHandle{
				.syncValue = [=](int val) {
					const auto clamped = std::clamp(val, minVal, maxVal);
					label->setText(formatLabel(clamped));
					slider->setValue(
						float64(clamped - minVal) / float64(maxVal - minVal));
				},
			};
		};

		const auto fmtSigned = [](int v, const char *unit) {
			const auto sign = (v > 0) ? QString("+") : QString();
			return sign + QString::number(v) + QString::fromUtf8(unit);
		};

		const auto pitchSlider = makeParamSlider(
			Ru("Тон (высота голоса)"),
			-12, 12, 1, VC::Pitch(),
			[=](int v) { return fmtSigned(v, " пт"); },
			[](int v) { VC::SetPitch(v); });

		const auto bassSlider = makeParamSlider(
			Ru("Бас (низкие частоты)"),
			-6, 12, 1, VC::Bass(),
			[=](int v) { return fmtSigned(v, " дБ"); },
			[](int v) { VC::SetBass(v); });

		const auto trebleSlider = makeParamSlider(
			Ru("Верха (чёткость)"),
			-6, 12, 1, VC::Treble(),
			[=](int v) { return fmtSigned(v, " дБ"); },
			[](int v) { VC::SetTreble(v); });

		const auto tempoSlider = makeParamSlider(
			Ru("Скорость (темп)"),
			75, 135, 5, VC::Tempo(),
			[](int v) { return QString::number(v) + '%'; },
			[](int v) { VC::SetTempo(v); });

		const auto echoSlider = makeParamSlider(
			Ru("Эхо"),
			0, 100, 5, VC::Echo(),
			[](int v) { return QString::number(v) + '%'; },
			[](int v) { VC::SetEcho(v); });

		const auto robotExt = content->lifetime().make_state<rpl::event_stream<bool>>();
		const auto robotBtn = AddButtonWithIcon(
			content,
			rpl::single(Ru("Эффект робота / рации")),
			st::settingsButtonNoIcon);
		robotBtn->toggleOn(rpl::single(VC::Robot()) | rpl::then(robotExt->events()));
		robotBtn->toggledValue() | rpl::on_next([=](bool on) {
			if (!*syncing) {
				VC::SetRobot(on);
				markCustom();
			}
		}, content->lifetime());

		const auto formantExt = content->lifetime().make_state<rpl::event_stream<bool>>();
		const auto formantBtn = AddButtonWithIcon(
			content,
			rpl::single(Ru("Сохранять форманты (естественный тембр)")),
			st::settingsButtonNoIcon);
		formantBtn->toggleOn(
			rpl::single(VC::PreserveFormants()) | rpl::then(formantExt->events()));
		formantBtn->toggledValue() | rpl::on_next([=](bool on) {
			if (!*syncing) {
				VC::SetPreserveFormants(on);
				markCustom();
			}
		}, content->lifetime());

		const auto syncAllControls = [=] {
			*syncing = true;
			pitchSlider.syncValue(VC::Pitch());
			bassSlider.syncValue(VC::Bass());
			trebleSlider.syncValue(VC::Treble());
			tempoSlider.syncValue(VC::Tempo());
			echoSlider.syncValue(VC::Echo());
			robotExt->fire(VC::Robot());
			formantExt->fire(VC::PreserveFormants());
			*syncing = false;
		};

		presetGroup->setChangedCallback([=](int index) {
			if (*syncing || index < 0 || index >= int(presets.size())) {
				return;
			}
			VC::ApplyPreset(presets[index].id);
			if (presets[index].id != u"custom"_q) {
				syncAllControls();
			}
		});

		Ui::AddSkip(content);
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Сбросить на «Мужской» по умолчанию")),
			rpl::single(QString()),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			VC::ApplyPreset(u"male"_q);
			*syncing = true;
			presetGroup->setValue(0);
			*syncing = false;
			syncAllControls();
			controller->showToast(Ru("Пресет сброшен на «Мужской»"));
		});

		Ui::AddSkip(content);
		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return rpl::single(VrgDetail::Ru("Изменение голоса"));
	}
};

class VrgFakeProfile : public Section<VrgFakeProfile> {
public:
	VrgFakeProfile(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		using VrgDetail::Ru;
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

		VrgDetail::AddCaption(content, Ru("Мой фейк-профиль"));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Открыть фейк-профиль")),
			rpl::single(QString()),
			st::settingsButton,
			{ &st::menuIconStealth }
		)->setClickedCallback([=] {
			controller->showSection(
				std::make_shared<Vrg::FakeProfile::Memento>(
					controller->session().user()));
		});

		AddButtonWithLabel(
			content,
			rpl::single(Ru("Настроить (номер +888, юзы, NFT-гифты)")),
			rpl::single(QString()),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			Vrg::FakeProfile::ShowEditBox(controller);
		});

		VrgDetail::AddCaption(content, Ru("Сброс"));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Сбросить фейк-профиль до основного")),
			rpl::single(QString()),
			st::settingsButtonNoIcon
		)->setClickedCallback([=] {
			Vrg::FakeProfile::Reset();
			controller->showToast(Ru("Фейк-профиль сброшен до основного аккаунта"));
		});

		Ui::AddSkip(content);
		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return rpl::single(VrgDetail::Ru("Фейк-профиль"));
	}
};

class VrgMain : public Section<VrgMain> {
public:
	VrgMain(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		using VrgDetail::Ru;
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

		VrgDetail::AddHeader(content);
		VrgDetail::AddCaption(content, Ru("Категории"));

		const auto open = [&](
				const char *name,
				Type (*id)(),
				const style::icon *icon) {
			AddButtonWithLabel(
				content,
				rpl::single(Ru(name)),
				rpl::single(QString()),
				st::settingsButton,
				{ icon }
			)->setClickedCallback([=] {
				controller->showSettings(id());
			});
		};
		open("VRG+", VrgPlusId, &st::menuIconPremium);
		open("Призрак", VrgGhostId, &st::menuIconStealth);
		open("Боковое меню", VrgSideMenuId, &st::menuIconSettings);
		open("Удалённые сообщения", VrgDeletedId, &st::menuIconDelete);
		open("Оформление", VrgLookId, &st::menuIconPalette);
		open("Плеер", VrgPlayerId, &st::menuIconSoundOn);
		open("Изменение голоса", VrgVoiceChangerId, &st::menuIconSoundSelect);
		open("Фейк-профиль", VrgFakeProfileId, &st::menuIconStealth);
		open("Автоответчик", VrgAutoReplyId, &st::menuIconBot);
		open("Удобство", VrgMiscId, &st::menuIconInfo);
		open("Защита и пароли", VrgSecurityId, &st::menuIconLock);

		VrgDetail::AddCaption(content, Ru("Ссылки"));
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Канал")),
			rpl::single(QString("@VRGclient")),
			st::settingsButton,
			{ &st::menuIconChannel }
		)->setClickedCallback([=] {
			UrlClickHandler::Open(u"https://t.me/VRGclient"_q);
		});
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Сайт")),
			rpl::single(QString("vrgclient.app")),
			st::settingsButton,
			{ &st::menuIconInfo }
		)->setClickedCallback([=] {
			File::OpenUrl(u"https://vrgclient.app"_q);
		});
		AddButtonWithLabel(
			content,
			rpl::single(QString("GitHub")),
			rpl::single(QString("VRGban")),
			st::settingsButton,
			{ &st::menuIconInfo }
		)->setClickedCallback([=] {
			File::OpenUrl(u"https://github.com/VRGban"_q);
		});
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Поддержать проект")),
			rpl::single(Ru("Boosty / Крипта")),
			st::settingsButton,
			{ &st::menuIconSavedMessages }
		)->setClickedCallback([=] {
			controller->showSettings(VrgSupportId());
		});
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Проверить обновления")),
			rpl::single(QString("v") + QString::fromLatin1(Vrg::Updater::kCurrentVersion)),
			st::settingsButton,
			{ &st::menuIconInfo }
		)->setClickedCallback([=] {
			Vrg::Updater::Check(controller->uiShow(), false);
		});
		Ui::AddSkip(content);

		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return rpl::single(QString("VRG"));
	}
};

class VrgSupport : public Section<VrgSupport> {
public:
	VrgSupport(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		using VrgDetail::Ru;
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

		const auto banner = content->add(object_ptr<Ui::RpWidget>(content));
		banner->resize(banner->width(), 88);
		banner->setMinimumHeight(88);
		banner->paintRequest() | rpl::on_next([=] {
			QPainter p(banner);
			const auto kinds = std::array<QString, 4>{
				u"supporter"_q,
				u"supp2_crown"_q,
				u"supp2_moon"_q,
				u"supp2_bolt"_q };
			const auto size = 40;
			const auto gap = 22;
			const auto total = int(kinds.size()) * size
				+ (int(kinds.size()) - 1) * gap;
			auto x = (banner->width() - total) / 2;
			for (const auto &kind : kinds) {
				Vrg::Badges::Paint(p, QRect(x, 24, size, size), kind);
				x += size + gap;
			}
		}, banner->lifetime());

		auto supportText = TextWithEntities{ Ru(
			"Поддерживая VRG, вы помогаете с серверами и сборками и "
			"получаете значок с анимацией в профиле навсегда.\n\n"
			"• От 3 $ (~245 ₽) — VRG Supporter #N: значок-сердце и "
			"доступ в закрытый чат. Значок менять нельзя.\n"
			"• От 50 $ — VRG Luna #N: можно выбирать и менять значок "
			"(корона, луна, молния или сердце) и доступ в закрытый чат.\n\n"
			"Оплатить можно рублями или криптой.\n\nПосле оплаты напишите ") }
			.append(tr::link(u"@casekind"_q, u"https://t.me/casekind"_q))
			.append(Ru(" и приложите скрин перевода — значок и доступ в чат будут выданы, как только прочитает."));
		AddDividerText(
			content,
			rpl::single(std::move(supportText)))->setLinksTrusted();

		VrgDetail::AddCaption(content, Ru("Платформа и связь"));
		AddButtonWithLabel(
			content,
			rpl::single(QString("Boosty")),
			rpl::single(QString("boosty.to/vrg")),
			st::settingsButton,
			{ &st::menuIconInfo }
		)->setClickedCallback([=] {
			File::OpenUrl(u"https://boosty.to/vrg"_q);
		});
		AddButtonWithLabel(
			content,
			rpl::single(Ru("Отправить скрин оплаты")),
			rpl::single(QString("@casekind")),
			st::settingsButton,
			{ &st::menuIconChannel }
		)->setClickedCallback([=] {
			UrlClickHandler::Open(u"https://t.me/casekind"_q);
		});

		VrgDetail::AddCaption(content, Ru("Криптовалюта (нажмите, чтобы скопировать)"));
		struct Wallet {
			const char *label;
			const char *address;
		};
		const Wallet wallets[] = {
			{ "USDT (TON)", "UQD7oMbSvYoazp-Q3pH8PkyEUesE-s0Qv_G0wfbXInRW_33l" },
			{ "USDT (TRC20)", "TGQXu9ir4wo4wRopSLuCoybiEvrMYBjtHu" },
			{ "USDT (BEP20)", "0x4B6Cd26bDe3f66e7708c2A1B66099D78fDb9e7C6" },
			{ "USDT (SOL)", "14GRXZf511FFLNTrwgtUEgxVTkmvajrYqw9NTKT6bS7C" },
			{ "BTC", "bc1qrkdqzlj2klcxpreg8g7yd5lw7wtxsl0vgfam8q" },
			{ "GRAM (TON)", "UQD7oMbSvYoazp-Q3pH8PkyEUesE-s0Qv_G0wfbXInRW_33l" },
		};
		for (const auto &w : wallets) {
			const auto label = QString::fromUtf8(w.label);
			const auto addr = QString::fromUtf8(w.address);
			const auto preview = (addr.size() > 16)
				? (addr.left(8) + u"..."_q + addr.right(6))
				: addr;
			AddButtonWithLabel(
				content,
				rpl::single(label),
				rpl::single(preview),
				st::settingsButtonNoIcon
			)->setClickedCallback([=] {
				QGuiApplication::clipboard()->setText(addr);
				controller->showToast(
					Ru("Адрес ") + label + Ru(" скопирован"));
			});
		}
		Ui::AddSkip(content);
		Ui::ResizeFitChild(this, content);
	}

	[[nodiscard]] rpl::producer<QString> title() override {
		return rpl::single(VrgDetail::Ru("Поддержать проект"));
	}
};

inline Type VrgId() { return VrgMain::Id(); }
inline Type VrgGhostId() { return VrgGhost::Id(); }
inline Type VrgSideMenuId() { return VrgSideMenu::Id(); }
inline Type VrgPlusId() { return VrgPlus::Id(); }
inline Type VrgDeletedId() { return VrgDeleted::Id(); }
inline Type VrgLookId() { return VrgLook::Id(); }
inline Type VrgPlayerId() { return VrgPlayer::Id(); }
inline Type VrgMiscId() { return VrgMisc::Id(); }
inline Type VrgSecurityId() { return VrgSecurity::Id(); }
inline Type VrgAutoReplyId() { return VrgAutoReply::Id(); }
inline Type VrgVoiceChangerId() { return VrgVoiceChanger::Id(); }
inline Type VrgFakeProfileId() { return VrgFakeProfile::Id(); }
inline Type VrgSupportId() { return VrgSupport::Id(); }

}
