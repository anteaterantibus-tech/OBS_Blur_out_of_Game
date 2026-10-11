# Установка и удаление

## Установка через установщик (рекомендуется)

1. Закройте OBS.
2. Запустите `package/obs-privacy-guard-0.1.0-windows-x64-Setup.exe`
   (спросит права администратора).
3. Путь к OBS подставится сам из реестра. Далее, далее, установить.
4. Запустите OBS. Проверка: в свежем логе
   (`%APPDATA%\obs-studio\logs`) есть строка
   `[obs-privacy-guard] loaded`.

Установщик копирует только DLL в `obs-plugins\64bit` и две локали в
`data\obs-plugins\obs-privacy-guard\locale`. Сцены, коллекции и профили
(`%APPDATA%\obs-studio`) он не трогает. Удаление: Панель управления ->
Программы -> OBS Privacy Guard -> Удалить.

## Установка (portable OBS, рекомендуется для проверки)

1. Возьмите официальный portable OBS с сайта obsproject.com и распакуйте.
2. ВАЖНО: запускайте `obs64.exe` с рабочим каталогом `bin/64bit`, иначе OBS 32
   показывает ошибку `Failed to find locale/en-US.ini`.
3. Из `package/obs-privacy-guard-0.1.0-windows-x64.zip` скопируйте:
   - `obs-privacy-guard/bin/64bit/obs-privacy-guard.dll` в `<OBS>\obs-plugins\64bit\`
   - `obs-privacy-guard/data/locale/*.ini` в `<OBS>\data\obs-plugins\obs-privacy-guard\locale\`
4. Запустите с флагом `--portable`. Проверка: в логе
   (`config\obs-studio\logs`) есть строка `[obs-privacy-guard] loaded`.

## Установка (стационарный OBS)

Требуется подтверждение пользователя: файлы копируются в
`C:\Program Files\obs-studio\...` (нужны права администратора).
Пути те же: DLL в `obs-plugins\64bit`, локали в
`data\obs-plugins\obs-privacy-guard\locale`. После этого перезапустите OBS.

## Удаление

Удалите скопированные файлы (`obs-privacy-guard.dll` и каталог
`data\obs-plugins\obs-privacy-guard`), перезапустите OBS. Настройки фильтра
хранятся в коллекциях сцен OBS; после удаления DLL они игнорируются.
Основная установка OBS не изменяется и не требуется.
