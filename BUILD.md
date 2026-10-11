# Сборка (воспроизводимо)

Проверено на: Windows 11 Pro x64, OBS Studio 32.2.2, Visual Studio BuildTools 18
(MSVC 19.51), Windows SDK 10.0.26100.0, CMake 4.3.1 из состава BuildTools.

## Что нужно установить

1. Visual Studio 2022/2026 или BuildTools с workload "Desktop development
   with C++" (нужны MSVC x64 и Windows SDK 10.0.20348+).
2. Git.
3. Qt и OBS SDK скачивать вручную не нужно: CMake скачает официальные
   `obs-deps`, `Qt6` и исходники OBS Studio из релизов `obsproject` на GitHub
   в каталог `.deps` (только официальные источники, без сторонних сайтов).

## Шаги

```powershell
cd D:\PROJECT\OpenCode\OBS_Blur
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
# Либо cmake из PATH, если установлен отдельно (нужен 3.28+).

# 1. Конфигурация. Первый запуск долгий: скачивание зависимостей (~1 ГБ),
#    сборка libobs Debug+Release. Повторные запуски быстрые.
& $cmake --preset windows-x64-local

# Если установлен только VS 2022 (17), используйте пресет windows-x64:
# & $cmake --preset windows-x64

# 2. Сборка.
& $cmake --build --preset windows-x64-local --config RelWithDebInfo

# 3. Пакет.
& $cmake --install build_x64 --prefix release/RelWithDebInfo --config RelWithDebInfo
```

Результат: `release/RelWithDebInfo/obs-privacy-guard/bin/64bit/obs-privacy-guard.dll`
и `release/RelWithDebInfo/obs-privacy-guard/data/locale/*.ini`.

## Примечания

- В `buildspec.json` зафиксирован SHA256 архива исходников OBS Studio 32.2.2
  (`f15f001f...8b831aa`): это фактический хеш официального архива
  `github.com/obsproject/obs-studio/archive/refs/tags/32.2.2.zip`, полученный
  при сборке. Хеши `obs-deps` и `Qt6` взяты из актуального obs-plugintemplate.
- Пресет `windows-x64-local` отличается от шаблонного только генератором
  `Visual Studio 18 2026` и версией SDK `10.0.26100.0` под установленное окружение.
- В OBS 32 API отличаются от шаблонных: используется `blog()` вместо `obs_log()`
  и `obs_properties_add_button2()` вместо deprecated `obs_properties_add_button()`.
- Имя процесса определяется через `QueryFullProcessImageNameW` (kernel32):
  `GetModuleBaseName` требует `PROCESS_VM_READ` и молча не работает для чужих
  процессов, а `QueryFullProcessImageFileName*` отсутствует в актуальных
  заголовках SDK (переименован в `QueryFullProcessImageName*`).
