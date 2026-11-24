/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "base/qt/qt_common_adapters.h"
#include "chat_helpers/compose/compose_show.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QSaveFile>
#include <QtGui/QTextCursor>
#include <memory>
#include <vector>

namespace Vrg::Templates {

struct Entry {
	QString title;
	QString text;
};

[[nodiscard]] inline QString FilePath() {
	return QDir::homePath() + u"/.config/vrg/templates.json"_q;
}

[[nodiscard]] inline std::vector<Entry> Load() {
	QFile f(FilePath());
	if (!f.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto doc = QJsonDocument::fromJson(f.readAll());
	if (!doc.isArray()) {
		return {};
	}
	auto result = std::vector<Entry>();
	for (const auto &v : doc.array()) {
		if (!v.isObject()) {
			continue;
		}
		const auto obj = v.toObject();
		const auto title = obj.value(u"title"_q).toString().trimmed();
		const auto text = obj.value(u"text"_q).toString();
		if (!text.isEmpty()) {
			result.push_back({
				.title = title.isEmpty() ? text.left(24) : title,
				.text = text,
			});
		}
	}
	return result;
}

inline void Save(const std::vector<Entry> &items) {
	const auto path = FilePath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QJsonArray arr;
	for (const auto &item : items) {
		QJsonObject obj;
		obj.insert(u"title"_q, item.title);
		obj.insert(u"text"_q, item.text);
		arr.append(obj);
	}
	QSaveFile f(path);
	if (f.open(QIODevice::WriteOnly)) {
		f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
		f.commit();
	}
}

inline void ShowManageBox(std::shared_ptr<Ui::Show> show) {
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8("Быстрые шаблоны (VRG)")));
		box->setWidth(st::boxWidth);
		const auto listWrap = box->verticalLayout()->add(
			object_ptr<Ui::VerticalLayout>(box));

		const auto rebuild = std::make_shared<Fn<void()>>();
		*rebuild = [=] {
			while (listWrap->count() > 0) {
				delete listWrap->widgetAt(0);
			}
			const auto items = Load();
			if (items.empty()) {
				listWrap->add(
					object_ptr<Ui::FlatLabel>(
						listWrap,
						QString::fromUtf8("Пока нет ни одного шаблона."),
						st::boxDividerLabel),
					st::boxRowPadding);
			} else {
				for (int i = 0; i < int(items.size()); ++i) {
					const auto &item = items[i];
					const auto preview = item.text.simplified().left(36);
					const auto btn = listWrap->add(
						object_ptr<Ui::SettingsButton>(
							listWrap,
							rpl::single(item.title + u" — "_q + preview),
							st::settingsButtonNoIcon));
					btn->setClickedCallback([=] {
						auto updated = Load();
						if (i >= 0 && i < int(updated.size())) {
							updated.erase(updated.begin() + i);
							Save(updated);
							(*rebuild)();
						}
					});
				}
			}
			listWrap->resizeToWidth(st::boxWidth);
		};
		(*rebuild)();

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				QString::fromUtf8(
					"Как пользоваться: правый клик в поле ввода → "
					"«Быстрые шаблоны».\nНажмите на шаблон выше, "
					"чтобы удалить его."),
				st::boxDividerLabel),
			st::boxRowPadding);

		const auto titleField = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(QString::fromUtf8("Название (необязательно)"))));
		const auto textField = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			rpl::single(QString::fromUtf8("Текст шаблона"))));

		box->addButton(rpl::single(QString::fromUtf8("Добавить")), [=] {
			const auto text = textField->getLastText();
			if (text.trimmed().isEmpty()) {
				textField->showError();
				return;
			}
			const auto title = titleField->getLastText().trimmed();
			auto items = Load();
			items.push_back({
				.title = title.isEmpty() ? text.trimmed().left(24) : title,
				.text = text,
			});
			Save(items);
			titleField->setText(QString());
			textField->setText(QString());
			(*rebuild)();
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

inline void AttachToField(
		std::shared_ptr<Ui::Show> show,
		not_null<Ui::InputField*> field) {
	const auto weak = QPointer<Ui::InputField>(field.get());
	field->addContextMenuHook([=](Ui::InputField::ContextMenuRequest req) {
		req.customizePopupMenu([=](not_null<Ui::PopupMenu*> menu) {
			const auto templates = Load();
			menu->addSeparator();
			auto sub = std::make_unique<Ui::PopupMenu>(menu, menu->st());
			for (const auto &entry : templates) {
				const auto text = entry.text;
				sub->addAction(entry.title, [=] {
					if (weak) {
						weak->textCursor().insertText(text);
						weak->setFocus();
					}
				});
			}
			if (!templates.empty()) {
				sub->addSeparator();
			}
			sub->addAction(
				QString::fromUtf8("Настроить шаблоны..."),
				[=] { ShowManageBox(show); });
			menu->addAction(
				QString::fromUtf8("Быстрые шаблоны"),
				std::move(sub));
		});
	});
}

}

