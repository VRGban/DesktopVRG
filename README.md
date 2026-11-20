# VRG Desktop

**Модифицированный Telegram с запрещённым функционалом.**
*A modified Telegram client with forbidden features.*

Сайт / Website: https://vrgclient.app · Канал / Channel: [@VRGclient](https://t.me/VRGclient)

---

## Русский

VRG Desktop — неофициальный клиент на основе [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) (версия 7.4.4), чёрно-белый, с возможностями, которых нет в оригинале:

- режим призрака: не отмечать прочитанным, не показывать «печатает» и «в сети», смотреть истории незаметно;
- удалённые сообщения остаются у вас, с историей правок;
- скрытие рекламы, свой Premium-значок локально;
- настраиваемое боковое меню и трей;
- **VRG Music**: свой плеер с поиском, офлайн-кешем, плейлистами, текстами песен и эквалайзером;
- автоответчик, тихая отправка, пароли на чаты и аккаунты;
- значки VRG рядом с именами, фейк-профиль, проверка обновлений.

**И это ещё не всё.**

### Скачать

Актуальная версия: **https://vrgclient.app**

### Сборка

Следуйте оригинальным инструкциям из `docs/` (для Linux: `docs/building-linux.md`).

Нужны **свои** `api_id` и `api_hash` с https://my.telegram.org. Передайте их в CMake как `TDESKTOP_API_ID` и `TDESKTOP_API_HASH`. Никогда не публикуйте их.

Код VRG лежит в `Telegram/SourceFiles/vrg/`; остальное — небольшие вставки в оригинальные файлы.

## English

VRG Desktop is an unofficial client based on [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) (version 7.4.4), black and white, with extra features:

- ghost mode: no read receipts, no typing or online status, silent story viewing;
- deleted messages stay visible, with edit history;
- ads hidden, local Premium badge;
- customizable side menu and tray;
- **VRG Music**: built-in player with search, offline cache, playlists, lyrics and equalizer;
- auto-responder, silent sending, chat and account passwords;
- VRG badges next to names, fake profile, update check.

**And that is not all.**

### Download

Latest version: **https://vrgclient.app**

### Building

Follow the original build instructions in `docs/` (for Linux: `docs/building-linux.md`).

You need **your own** `api_id` and `api_hash` from https://my.telegram.org. Pass them to CMake as `TDESKTOP_API_ID` and `TDESKTOP_API_HASH`. Never publish them.

VRG code lives in `Telegram/SourceFiles/vrg/`; everything else is small hooks into the original sources.

## License / Лицензия

VRG Desktop is based on Telegram Desktop and is licensed under GPLv3 with the OpenSSL exception, the same as the original. See `LICENSE` and `LEGAL`. Telegram names and logos belong to Telegram FZ-LLC. VRG is not affiliated with Telegram. Use at your own risk.

VRG Desktop основан на Telegram Desktop и распространяется под GPLv3 с исключением для OpenSSL, как и оригинал. См. `LICENSE` и `LEGAL`. Названия и логотипы Telegram принадлежат Telegram FZ-LLC. VRG не связан с Telegram. Используйте на свой риск.
