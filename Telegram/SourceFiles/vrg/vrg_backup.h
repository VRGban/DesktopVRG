/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "base/weak_ptr.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "ui/boxes/confirm_box.h"
#include "window/window_session_controller.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace Vrg::Backup {

[[nodiscard]] inline QStringList BackupFileNames() {
	return {
		u"vrg.conf"_q,
		u"templates.json"_q,
		u"music_library.json"_q,
		u"music_state.json"_q,
		u"music_stream_urls.json"_q,
	};
}

[[nodiscard]] inline QString ConfigDir() {
	return QDir::homePath() + u"/.config/vrg"_q;
}

inline void Export(not_null<Window::SessionController*> controller) {
	const auto weak = base::make_weak(controller.get());
	FileDialog::GetWritePath(
		controller->widget().get(),
		QString::fromUtf8("Экспорт настроек VRG и музыки"),
		u"VRG Backup (*.vrgbackup *.json)"_q,
		u"vrg_backup.vrgbackup"_q,
		[=](const QString &path) {
			if (path.isEmpty()) {
				return;
			}
			QJsonObject files;
			const auto dir = ConfigDir();
			for (const auto &name : BackupFileNames()) {
				QFile f(dir + u'/' + name);
				if (f.open(QIODevice::ReadOnly)) {
					files.insert(name, QString::fromUtf8(f.readAll()));
				}
			}
			QJsonObject root;
			root.insert(u"version"_q, 1);
			root.insert(u"files"_q, files);

			QSaveFile out(path);
			if (out.open(QIODevice::WriteOnly)) {
				out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
				if (out.commit() && weak) {
					weak->showToast(QString::fromUtf8(
						"Настройки и музыка экспортированы"));
					return;
				}
			}
			if (weak) {
				weak->showToast(QString::fromUtf8("Не удалось сохранить файл"));
			}
		});
}

inline void Import(not_null<Window::SessionController*> controller) {
	const auto weak = base::make_weak(controller.get());
	FileDialog::GetOpenPath(
		controller->widget().get(),
		QString::fromUtf8("Импорт настроек VRG и музыки"),
		u"VRG Backup (*.vrgbackup *.json)"_q,
		[=](const FileDialog::OpenResult &result) {
			if (result.paths.isEmpty()) {
				return;
			}
			QFile f(result.paths.front());
			if (!f.open(QIODevice::ReadOnly)) {
				if (weak) {
					weak->showToast(QString::fromUtf8("Не удалось открыть файл"));
				}
				return;
			}
			const auto doc = QJsonDocument::fromJson(f.readAll());
			const auto files = doc.object().value(u"files"_q).toObject();
			if (!doc.isObject() || files.isEmpty()) {
				if (weak) {
					weak->showToast(QString::fromUtf8("Неверный формат файла бэкапа"));
				}
				return;
			}
			const auto dir = ConfigDir();
			QDir().mkpath(dir);
			int restored = 0;
			for (const auto &name : BackupFileNames()) {
				if (!files.contains(name)) {
					continue;
				}
				QSaveFile out(dir + u'/' + name);
				if (out.open(QIODevice::WriteOnly)) {
					out.write(files.value(name).toString().toUtf8());
					if (out.commit()) {
						++restored;
					}
				}
			}
			if (!weak) {
				return;
			}
			if (restored == 0) {
				weak->showToast(QString::fromUtf8("В файле не найдено данных VRG"));
				return;
			}
			weak->show(Ui::MakeConfirmBox({
				.text = QString::fromUtf8(
					"Импортировано файлов: %1. Перезапустить VRG, "
					"чтобы применить все настройки и музыку?").arg(restored),
				.confirmed = [=](Fn<void()> close) {
					close();
					Core::Restart();
				},
				.confirmText = QString::fromUtf8("Перезапустить"),
			}));
		});
}

}
