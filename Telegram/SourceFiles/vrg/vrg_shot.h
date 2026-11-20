/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "crl/crl_async.h"
#include "crl/crl_on_main.h"

#include <QtCore/QBuffer>
#include <QtCore/QByteArray>
#include <QtCore/QMimeData>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>

namespace Vrg {

class Shot final {
public:
	static void SetHideTime(bool hide) {
		HidingTime() = hide;
	}
	[[nodiscard]] static bool HideTime() {
		return HidingTime();
	}

	static void CopyToClipboard(QImage image) {
		if (image.isNull()) {
			return;
		}
		crl::async([image = std::move(image)] {
			auto bytes = QByteArray();
			bytes.reserve(image.sizeInBytes() / 8);
			auto buffer = QBuffer(&bytes);
			buffer.open(QIODevice::WriteOnly);
			image.save(&buffer, "PNG", 90);
			crl::on_main([bytes = std::move(bytes)]() mutable {
				const auto data = new QMimeData();
				data->setData(u"image/png"_q, std::move(bytes));
				QGuiApplication::clipboard()->setMimeData(data);
			});
		});
	}

private:
	static bool &HidingTime() {
		static bool hiding = false;
		return hiding;
	}
};

}
