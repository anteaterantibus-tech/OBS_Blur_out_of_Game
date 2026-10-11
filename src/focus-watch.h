// OBS Privacy Guard - проверка фокуса игры (этап 1).
// Только Win32 API, без обхода античита, без повышенных прав.
// Не записывает содержимое окон в логи, сравнивает только имя процесса.
#pragma once

#include <string>
#include <vector>

namespace privacy_guard {

// Результат одной проверки фокуса.
struct FocusQuery {
	bool api_ok = false; // false = сбой Win32 API (неизвестное состояние)
	bool process_exists = false; // найден ли ХОТЯ БЫ ОДИН процесс из списка
	bool focused = false; // принадлежит ли активное окно одному из списка
	bool fullscreen_other = false; // активно чужое полноэкранное окно
	bool game_visible = false; // игра видна на весь монитор, фокус в другом месте
};

// Проверяет, принадлежит ли активное окно одному из процессов targets
// (имена файлов без пути, сравнение без учета регистра).
// Если allow_fullscreen, отдельно помечается случай, когда активное окно
// не из списка, но занимает весь монитор (игра без вписывания EXE).
// Процессы из deny никогда не открывают через эвристику (например браузер
// с видео на весь экран): fail-closed важнее удобства.
// Пустой список при выключенной эвристике всегда дает api_ok=false,
// чтобы вызывающая сторона перешла в закрытое состояние.
FocusQuery query_game_focus(const std::vector<std::string> &targets, bool allow_fullscreen,
			    const std::vector<std::string> &deny = {});

// Игра из списка (или любая при allow_fullscreen) занимает весь монитор
// и видна, а фокус находится на ДРУГОМ мониторе (клик по второму экрану).
// Тогда картинка захваченного монитора не изменилась и закрывать не надо.
// Процессы из deny игрой не считаются даже на весь экран (видео в браузере).
// Только чтение, окон не создает.
bool game_visible_elsewhere(const std::vector<std::string> &targets,
			    const std::vector<std::string> &deny = {});

// Совместимость со старым одиночным значением настройки.
inline FocusQuery query_game_focus(const char *target_exe)
{
	std::vector<std::string> one;
	if (target_exe && *target_exe)
		one.emplace_back(target_exe);
	return query_game_focus(one, false);
}

// Имя EXE активного окна (только имя файла, нижний регистр).
// Пустая строка, если определить не удалось. Только чтение.
std::string foreground_exe();

} // namespace privacy_guard
