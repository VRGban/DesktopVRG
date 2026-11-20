/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QMap>
#include <QtCore/QString>

namespace Vrg {

inline constexpr int kMaxAccounts = 99;

class Conf final {
public:
	[[nodiscard]] static bool Bool(const QString &key, bool fallback) {
		const auto &map = Map();
		const auto i = map.constFind(key);
		return (i == map.constEnd()) ? fallback : (i.value() == "1");
	}

	[[nodiscard]] static int Int(const QString &key, int fallback) {
		const auto &map = Map();
		const auto i = map.constFind(key);
		if (i == map.constEnd()) {
			return fallback;
		}
		auto ok = false;
		const auto value = i.value().toInt(&ok);
		return ok ? value : fallback;
	}

	[[nodiscard]] static QString IconPath() {
		const auto name = Map().value("app_icon");
		return (name == "white" || name == "clear")
			? (":/gui/art/vrg_icon_" + name + ".png")
			: QString(":/gui/art/vrg_icon_black.png");
	}

	[[nodiscard]] static QString TrayIconPath() {
		return (Map().value("tray_icon") == "dark")
			? QString(":/gui/art/vrg_icon_tray_black.png")
			: QString(":/gui/art/vrg_icon_tray_white.png");
	}

	[[nodiscard]] static bool Seconds() {
		return Bool("show_seconds", false);
	}

	[[nodiscard]] static QString Str(const QString &key) {
		return Map().value(key);
	}

	static void Set(const QString &key, const QString &value) {
		Write(key, value);
	}

	static void SetBool(const QString &key, bool value) {
		Set(key, value ? "1" : "0");
	}

	static void SetInt(const QString &key, int value) {
		Set(key, QString::number(value));
	}

private:
	[[nodiscard]] static QString Dir() {
		return QDir::homePath() + "/.config/vrg";
	}

	[[nodiscard]] static QElapsedTimer &Age() {
		static QElapsedTimer age;
		return age;
	}

	[[nodiscard]] static QMap<QString, QString> &Data() {
		static QMap<QString, QString> data;
		return data;
	}

	static void Load(const QString &path, QMap<QString, QString> &to) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
			return;
		}
		while (!file.atEnd()) {
			const auto line = QString::fromUtf8(file.readLine()).trimmed();
			const auto eq = line.indexOf('=');
			if (eq > 0) {
				to.insert(line.left(eq), line.mid(eq + 1));
			}
		}
	}

	[[nodiscard]] static const QMap<QString, QString> &Map() {
		auto &age = Age();
		if (age.isValid() && age.elapsed() < 2000) {
			return Data();
		}
		age.start();
		auto &data = Data();
		data.clear();
		if (QFile::exists(Dir() + "/vrg.conf")) {
			Load(Dir() + "/vrg.conf", data);
		} else {
			Load(Dir() + "/ghost.conf", data);
		}
		return data;
	}

	static void Write(const QString &key, const QString &value) {
		Map();
		auto &data = Data();
		data.insert(key, value);
		QDir().mkpath(Dir());
		QFile file(Dir() + "/vrg.conf");
		if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
			for (auto i = data.constBegin(); i != data.constEnd(); ++i) {
				file.write((i.key() + "=" + i.value() + "\n").toUtf8());
			}
		}
		Age().invalidate();
	}
};

}
