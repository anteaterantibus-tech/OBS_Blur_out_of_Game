# OBS Privacy Guard

Native OBS Studio filter plugin (Windows x64). It covers the source image
with an opaque cover or live blur when the game loses focus or when you close
it manually, and hides fixed screen areas with masks. Your monitor stays
untouched: processing applies only to the frame OBS sends to viewers
(stream, recording, virtual camera).

Supported OBS version: 32.2.2 (Windows x64).
Language: C++17. Build: CMake using the official obs-plugintemplate layout.
License: GPL-2.0 (same as OBS Studio and the plugin template).

---

# OBS Privacy Guard

Нативный фильтр для OBS Studio (Windows x64). Закрывает изображение источника
непрозрачной заглушкой или живым размытием, когда игра теряет фокус или
по ручному закрытию, и скрывает фиксированные области экрана масками.
На мониторе стримера ничего не меняется: обработка применяется только к кадру,
который OBS отдает зрителям (стрим, запись, виртуальная камера).

Поддерживаемая версия OBS: 32.2.2 (Windows x64).
Язык: C++17. Сборка: CMake по структуре официального obs-plugintemplate.
Лицензия: GPL-2.0 (как у OBS Studio и шаблона плагинов).

---

## Architecture / Архитектура

**EN.** An OBS filter (`OBS_SOURCE_TYPE_FILTER`) placed on the game/capture
source, not a separate source. Reasons:

- the filter draws in the parent frame coordinates, so masks automatically
  follow the source when it is moved, scaled or cropped;
- the filter's `video_render` runs even when the input frame is gone:
  with no input it draws the cover itself (fail-closed), no extra source needed;
- a separate scene/source would complicate setup with no privacy benefit.

Focus polling: Win32 API (`GetForegroundWindow` + process-name compare),
no input hooks, no anticheat bypass, no admin rights.
Cover rendering: OBS base effect `OBS_EFFECT_SOLID` (GPU) plus a custom
13-tap gaussian effect for blur and masks. Polling runs in `video_tick`
(10 Hz focus, 2 Hz overlay/visibility checks), state is mutex-protected,
manual shutter wins over automation.

**RU.** Фильтр OBS (`OBS_SOURCE_TYPE_FILTER`) поверх источника игры/захвата,
а не отдельный источник. Причины:

- фильтр рисует в координатах кадра родителя, поэтому маски автоматически
  двигаются вместе с источником при перемещении, масштабировании и обрезке;
- `video_render` фильтра вызывается даже при пропавшем входном кадре:
  без входа фильтр рисует заглушку сам (fail-closed), отдельный источник
  не нужен;
- отдельная сцена/источник усложнили бы настройку без выигрыша
  для приватности.

Опрос фокуса: Win32 API (`GetForegroundWindow` + сравнение имени процесса),
без перехвата ввода, без обхода античита, без прав администратора.
Рендер заглушки: базовый эффект OBS `OBS_EFFECT_SOLID` (GPU) плюс свой
13-точечный гауссов эффект для размытия и масок. Опрос в `video_tick`
(фокус 10 Гц, оверлей/видимость 2 Гц), состояние под мьютексом,
ручная шторка приоритетнее автоматики.

## Features / Возможности

**EN.**

- Auto-shutter on any focus loss (closes instantly, restores after
  a configurable delay, default 500 ms, timer resets on repeated loss).
- Fullscreen-window heuristic: any fullscreen game works without listing EXEs.
- Visibility rule: a fullscreen-visible game on one monitor stays open when
  you click on another monitor.
- Process list: manual text, "+ EXE files" multi-select button,
  "+ Games folder" button with recursive scan, all merged live.
- Shutter types: opaque cover (any color) or current-frame blur with strength.
- Manual shutter via OBS hotkey or button, overrides automation.
- Private areas: up to 8 rectangles in source-frame percent with opaque
  cover or blur, moving together with the source.
- Five Russian/English-localized settings sections, status line
  (state, frame size, game count and names), settings persist via OBS.
- Fail-closed everywhere: startup, missing process, unknown state,
  missing input frame, shader error. No private frames on disk,
  no window contents in logs.

**RU.**

- Автошторка при любой потере фокуса (закрытие мгновенно, восстановление
  после настраиваемой задержки, по умолчанию 500 мс, таймер сбрасывается
  при повторной потере).
- Эвристика полноэкранного окна: любая игра в полном экране работает
  без перечисления EXE.
- Правило видимости: игра на весь экран остается открытой при клике
  по другому монитору.
- Список процессов: ручной текст, кнопки "+ EXE-файлы" (мультивыбор)
  и "+ Папка с играми" (рекурсивный скан), все объединяется живьем.
- Типы заглушки: непрозрачная плашка (любой цвет) или размытие текущего
  кадра с регулировкой силы.
- Ручная шторка горячей клавишей OBS или кнопкой, приоритет над автоматикой.
- Приватные области: до 8 прямоугольников в процентах кадра источника
  с плашкой или размытием, двигаются вместе с источником.
- Пять разделов настроек с русской/английской локализацией, строка статуса
  (состояние, размер кадра, число и имена игр), настройки сохраняются в OBS.
- Fail-closed везде: старт, нет процесса, неизвестное состояние, нет
  входного кадра, ошибка шейдера. Приватные кадры на диск не пишутся,
  содержимое окон в логи не попадает.

## Honest limitations / Честные ограничения

**EN.** Focus events and captured frames can disagree in time: no absolute
protection from focus detection alone. A second screen-capture source in the
same scene exposes the desktop regardless of this plugin. Blur does not hide
passwords/codes: use opaque covers for secrets. Geometric masks do not track
moving windows (e.g. draggable Steam overlay: it has no Win32 window at all,
verified by measurement; use the manual shutter). A fullscreen window
spanning two monitors is not treated as a game.

**RU.** Событие фокуса и захваченный кадр могут расходиться во времени:
абсолютной защиты от одного детекта фокуса нет. Второй источник захвата
экрана в той же сцене раскроет рабочий стол независимо от плагина.
Размытие не прячет пароли и коды: для секретов только плашка.
Геометрические маски не следуют за двигающимися окнами (у оверлея Steam
вообще нет Win32-окна, доказано замером; закрывается ручной шторкой).
Окно на два монитора игрой не считается.

## Docs / Документация

- `BUILD.md`: reproducible build instructions.
- `INSTALL.md`: install/uninstall (installer + manual).
- `USER.md`: user manual (Russian).
- `TESTS.md`: test results and known limitations.
- `installer.iss`: Inno Setup script building the one-click installer.
- `package/`: not stored in git; build it via `BUILD.md`
  (installer EXE attached to GitHub Releases).

## Status / Статус

Current version: 0.8.0. Done: auto-shutter, opaque/blur shutter, manual
hotkey, process list with pickers and folder scan, fullscreen heuristic
with deny list, visibility rule, 8 privacy masks, transition logging,
installer.
Next: mouse-driven mask selection, custom shutter image.
See `TESTS.md` for what is verified vs NOT RUN. Planned features are never
presented as done.

Текущая версия: 0.8.0. Готово: автошторка, плашка/размытие, ручная клавиша,
список процессов с пикеры и сканом папки, эвристика полного экрана, правило
видимости, 8 приватных масок, журнал переходов, установщик.
Дальше: выделение масок мышью, своя картинка заглушки.
Что проверено, а что NOT RUN, смотри в `TESTS.md`. Запланированное
за готовое не выдаем.
