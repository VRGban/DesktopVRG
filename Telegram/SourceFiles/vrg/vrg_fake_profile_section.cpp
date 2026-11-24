/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#include "vrg/vrg_fake_profile_section.h"

#include "vrg/vrg_fake_profile.h"
#include "api/api_premium.h"
#include "apiwrap.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

#include <QtCore/QRandomGenerator>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Vrg::FakeProfile {
namespace {

[[nodiscard]] QString Ru(const char *s) {
	return QString::fromUtf8(s);
}

[[nodiscard]] QString FormatRealPhone(not_null<UserData*> user) {
	const auto p = user->phone().trimmed();
	if (p.isEmpty()) {
		return QString();
	}
	return p.startsWith('+') ? p : ('+' + p);
}

[[nodiscard]] std::vector<FakeUsername> ResolveUsernames(not_null<UserData*> user, const Data &data) {
	if (!data.usernames.empty()) {
		return data.usernames;
	}
	std::vector<FakeUsername> out;
	const auto &realList = user->usernames();
	if (!realList.empty()) {
		for (const auto &u : realList) {
			if (!u.isEmpty()) {
				out.push_back({ u, false });
			}
		}
	} else if (!user->username().isEmpty()) {
		out.push_back({ user->username(), false });
	}
	return out;
}

}

void ResolveGiftSlug(
		not_null<Main::Session*> session,
		const QString &rawSlug,
		Fn<void(std::optional<::Data::StarGift>)> onDone) {
	const auto slug = ExtractSlug(rawSlug);
	if (slug.isEmpty()) {
		if (onDone) {
			onDone(std::nullopt);
		}
		return;
	}
	if (const auto cached = CachedGift(slug)) {
		if (onDone) {
			onDone(cached);
		}
		return;
	}
	const auto weak = base::make_weak(session.get());
	session->api().request(
		MTPpayments_GetUniqueStarGift(MTP_string(slug))
	).done([=](const MTPpayments_UniqueStarGift &result) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		const auto &data = result.data();
		strong->data().processUsers(data.vusers());
		auto gift = Api::FromTL(strong, data.vgift());
		if (gift && gift->unique) {
			CacheGift(slug, *gift);
			if (onDone) {
				onDone(gift);
			}
		} else if (onDone) {
			onDone(std::nullopt);
		}
	}).fail([=](const MTP::Error &) {
		if (onDone) {
			onDone(std::nullopt);
		}
	}).send();
}

void ShowEditBox(
		not_null<Window::SessionController*> controller,
		Fn<void()> onUpdated) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		box->setTitle(rpl::single(Ru("Настройка фейк-профиля")));

		const auto data = Load();
		const auto state = std::make_shared<Data>(data);

		Ui::AddSkip(box->verticalLayout(), 6);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(Ru("Номер телефона (пусто = как в основном аккаунте)")),
				st::defaultSubsectionTitle),
			st::defaultSubsectionTitlePadding);

		const auto phoneField = box->addRow(
			object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				rpl::single(Ru("Например: +888 0000 0000")),
				data.phone),
			QMargins(24, 6, 24, 10));

		const auto gen888 = box->addRow(
			object_ptr<Ui::SettingsButton>(
				box,
				rpl::single(Ru("Сгенерировать анонимный номер +888")),
				st::settingsButtonNoIcon),
			QMargins(0, 0, 0, 8));
		gen888->setClickedCallback([=] {
			const auto a = QRandomGenerator::global()->bounded(1000, 9999);
			const auto b = QRandomGenerator::global()->bounded(1000, 9999);
			phoneField->setText(QString("+888 0%1 %2").arg(a).arg(b));
		});

		Ui::AddDivider(box->verticalLayout());
		Ui::AddSkip(box->verticalLayout(), 8);

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(Ru("Юзернеймы (первый — основной, остальные — «а также»)")),
				st::defaultSubsectionTitle),
			st::defaultSubsectionTitlePadding);

		const auto userField = box->addRow(
			object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				rpl::single(Ru("Введите юз (например: @durov)")),
				QString()),
			QMargins(24, 6, 24, 10));

		const auto collectibleCheck = box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				Ru("Коллекционный юз (NFT / Fragment)"),
				true,
				st::defaultBoxCheckbox),
			QMargins(24, 4, 24, 10));

		const auto listContainer = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			QMargins(0, 4, 0, 4));

		const auto rebuildUsernames = std::make_shared<Fn<void()>>();
		*rebuildUsernames = [=] {
			while (listContainer->count() > 0) {
				delete listContainer->widgetAt(0);
			}
			for (int i = 0; i < int(state->usernames.size()); ++i) {
				const auto &u = state->usernames[i];
				const auto label = '@' + u.username
					+ (u.collectible ? Ru("  [NFT · нажми чтобы удалить]") : Ru("  [удалить]"));
				const auto btn = listContainer->add(
					object_ptr<Ui::SettingsButton>(
						listContainer,
						rpl::single(label),
						st::settingsButtonNoIcon));
				btn->setClickedCallback([=] {
					state->usernames.erase(state->usernames.begin() + i);
					(*rebuildUsernames)();
				});
			}
		};
		(*rebuildUsernames)();

		const auto pushFieldUsername = [=] {
			auto u = userField->getLastText().trimmed();
			if (u.startsWith('@')) {
				u = u.mid(1);
			}
			if (u.isEmpty()) {
				return false;
			}
			state->usernames.push_back({ u, collectibleCheck->checked() });
			userField->setText(QString());
			(*rebuildUsernames)();
			return true;
		};

		const auto addUserBtn = box->addRow(
			object_ptr<Ui::SettingsButton>(
				box,
				rpl::single(Ru("+ Добавить юзернейм в список")),
				st::settingsButtonNoIcon),
			QMargins(0, 0, 0, 10));
		addUserBtn->setClickedCallback([=] {
			pushFieldUsername();
		});

		Ui::AddDivider(box->verticalLayout());
		Ui::AddSkip(box->verticalLayout(), 8);

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(Ru("Настоящие NFT-гифты Telegram (по ссылке t.me/nft/...)")),
				st::defaultSubsectionTitle),
			st::defaultSubsectionTitlePadding);

		const auto giftSlugField = box->addRow(
			object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				rpl::single(Ru("Ссылка или slug (например: t.me/nft/PlushPepe-142)")),
				QString()),
			QMargins(24, 6, 24, 10));

		const auto giftsContainer = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			QMargins(0, 4, 0, 4));

		const auto rebuildGifts = std::make_shared<Fn<void()>>();
		*rebuildGifts = [=] {
			while (giftsContainer->count() > 0) {
				delete giftsContainer->widgetAt(0);
			}
			for (int i = 0; i < int(state->giftSlugs.size()); ++i) {
				const auto &slug = state->giftSlugs[i];
				const auto btn = giftsContainer->add(
					object_ptr<Ui::SettingsButton>(
						giftsContainer,
						rpl::single(u"🎁  "_q + slug + Ru("  [удалить]")),
						st::settingsButtonNoIcon));
				btn->setClickedCallback([=] {
					state->giftSlugs.erase(state->giftSlugs.begin() + i);
					(*rebuildGifts)();
				});
			}
		};
		(*rebuildGifts)();

		const auto addGiftSlug = [=](const QString &raw) {
			const auto slug = ExtractSlug(raw);
			if (slug.isEmpty()) {
				controller->showToast(Ru("Вставь ссылку вида t.me/nft/PlushPepe-1"));
				return;
			}
			controller->showToast(Ru("Загружаю NFT с серверов Telegram..."));
			ResolveGiftSlug(&controller->session(), slug, [=](std::optional<::Data::StarGift> gift) {
				if (!gift || !gift->unique) {
					controller->showToast(Ru("NFT не найден: ") + slug);
					return;
				}
				state->giftSlugs.push_back(slug);
				giftSlugField->setText(QString());
				(*rebuildGifts)();
				auto savedNow = Load();
				savedNow.giftSlugs = state->giftSlugs;
				Save(savedNow);
				controller->showToast(
					Ru("Добавлен NFT: ") + gift->unique->title
					+ u" #"_q + QString::number(gift->unique->number));
			});
		};

		const auto addGiftBtn = box->addRow(
			object_ptr<Ui::SettingsButton>(
				box,
				rpl::single(Ru("+ Выдать настоящий NFT по ссылке")),
				st::settingsButtonNoIcon),
			QMargins(0, 0, 0, 6));
		addGiftBtn->setClickedCallback([=] {
			addGiftSlug(giftSlugField->getLastText());
		});

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(Ru("Быстрые примеры (подтянутся с Telegram):")),
				st::defaultSubsectionTitle),
			st::defaultSubsectionTitlePadding);
		for (const auto &sample : {
				u"PlushPepe-1"_q,
				u"DurovsCap-1"_q,
				u"AstralShard-1"_q,
				u"PreciousPeach-1"_q }) {
			const auto b = box->addRow(
				object_ptr<Ui::SettingsButton>(
					box,
					rpl::single(u"+ t.me/nft/"_q + sample),
					st::settingsButtonNoIcon),
				QMargins(0, 0, 0, 0));
			b->setClickedCallback([=] { addGiftSlug(sample); });
		}

		Ui::AddSkip(box->verticalLayout(), 10);
		Ui::AddDivider(box->verticalLayout());
		Ui::AddSkip(box->verticalLayout(), 6);

		const auto resetBtn = box->addRow(
			object_ptr<Ui::SettingsButton>(
				box,
				rpl::single(Ru("Сбросить фейк-профиль до основного")),
				st::settingsButtonNoIcon),
			QMargins(0, 0, 0, 10));
		resetBtn->setClickedCallback([=] {
			Reset();
			box->closeBox();
			controller->showToast(Ru("Фейк-профиль сброшен до основного"));
			if (onUpdated) {
				onUpdated();
			}
		});

		box->addButton(rpl::single(Ru("Сохранить")), [=] {
			pushFieldUsername();
			state->phone = phoneField->getLastText().trimmed();
			Save(*state);
			box->closeBox();
			if (onUpdated) {
				onUpdated();
			}
		});
		box->addLeftButton(rpl::single(Ru("Отмена")), [=] {
			box->closeBox();
		});
	}));
}

object_ptr<Window::SectionWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	Q_UNUSED(column);
	auto result = object_ptr<SectionWidget>(parent, controller, _user);
	result->setGeometry(geometry);
	return result;
}

SectionWidget::SectionWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user)
: Window::SectionWidget(parent, controller)
, _user(user)
, _back(this, st::historyTopBarBack)
, _title(this, Ru("Мой фейк-профиль"), st::boxTitle)
, _editBtn(this, st::infoTopBarEdit)
, _shadow(this)
, _scroll(this, st::defaultSolidScroll) {
	_back->setClickedCallback([=] {
		controller->showBackFromStack();
	});
	_editBtn->setClickedCallback([=] {
		ShowEditBox(controller, [=] {
			rebuildContent();
		});
	});
	_inner = _scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(_scroll));
	_scroll->show();
	_shadow->raise();
	rebuildContent();
}

void SectionWidget::rebuildContent() {
	while (_inner->count() > 0) {
		delete _inner->widgetAt(0);
	}
	const auto data = Load();

	const auto hero = _inner->add(
		object_ptr<Ui::FixedHeightWidget>(_inner, 108),
		QMargins(16, 12, 16, 8));
	const auto pic = Ui::CreateChild<Ui::UserpicButton>(
		hero,
		_user,
		st::defaultUserpicButton);
	pic->setAttribute(Qt::WA_TransparentForMouseEvents);
	pic->moveToLeft(4, 16);
	pic->show();

	const auto nameLbl = Ui::CreateChild<Ui::FlatLabel>(
		hero,
		rpl::single(_user->name()),
		st::boxTitle);
	nameLbl->moveToLeft(92, 26);
	nameLbl->show();

	const auto statusLbl = Ui::CreateChild<Ui::FlatLabel>(
		hero,
		rpl::single(Ru("в сети")),
		st::defaultFlatLabel);
	statusLbl->setTextColorOverride(QColor(0x98, 0x98, 0xa0));
	statusLbl->moveToLeft(92, 56);
	statusLbl->show();

	_inner->add(object_ptr<Ui::PlainShadow>(_inner), QMargins(0, 4, 0, 8));

	const auto addInfoRow = [&](
			const QString &value,
			const QString &caption,
			const QString &copyText) {
		const auto btn = _inner->add(
			object_ptr<Ui::SettingsButton>(
				_inner,
				rpl::single(value + u"\n"_q + caption),
				st::settingsButtonNoIcon));
		btn->setClickedCallback([=] {
			if (!copyText.isEmpty()) {
				QGuiApplication::clipboard()->setText(copyText);
				controller()->showToast(Ru("Скопировано: ") + copyText);
			}
		});
	};

	const auto phone = data.phone.isEmpty() ? FormatRealPhone(_user) : data.phone;
	if (!phone.isEmpty()) {
		addInfoRow(phone, Ru("Телефон"), phone);
	}

	const auto usernames = ResolveUsernames(_user, data);
	if (!usernames.empty()) {
		QStringList all;
		for (const auto &u : usernames) {
			all.append('@' + u.username + (u.collectible ? u" · NFT"_q : QString()));
		}
		const auto primary = '@' + usernames.front().username;
		addInfoRow(
			all.join(u", "_q),
			(usernames.size() > 1)
				? Ru("Имя пользователя и коллекционные юзы")
				: Ru("Имя пользователя"),
			primary);
	}

	updateLayout();
}

void SectionWidget::updateLayout() {
	const auto topH = st::topBarHeight;
	_back->moveToLeft(0, 0);
	_editBtn->moveToRight(0, 0);
	_title->moveToLeft(
		_back->width() + 8,
		(topH - _title->height()) / 2);
	_shadow->setGeometry(0, topH, width(), st::lineWidth);
	_scroll->setGeometry(0, topH + st::lineWidth, width(), std::max(0, height() - topH - st::lineWidth));
	if (_inner) {
		_inner->resizeToWidth(_scroll->width());
	}
}

bool SectionWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	Q_UNUSED(params);
	return sameTypeAs(memento);
}

bool SectionWidget::sameTypeAs(not_null<Window::SectionMemento*> memento) {
	return dynamic_cast<Memento*>(memento.get()) != nullptr;
}

QRect SectionWidget::floatPlayerAvailableRect() {
	return mapToGlobal(rect());
}

bool SectionWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

void SectionWidget::doSetInnerFocus() {
	_scroll->setFocus();
}

void SectionWidget::resizeEvent(QResizeEvent *e) {
	Window::SectionWidget::resizeEvent(e);
	updateLayout();
}

void SectionWidget::paintEvent(QPaintEvent *) {
	QPainter p(this);
	p.fillRect(rect(), st::windowBg);
}

}

