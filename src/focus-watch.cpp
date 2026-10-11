// OBS Privacy Guard - реализация проверки фокуса игры.
//
// Используются только Unicode (W) варианты Win32 API во избежание
// конфликтов с UNICODE/_UNICODE в флагах сборки OBS.
#include "focus-watch.h"

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <string>
#include <vector>

#pragma comment(lib, "psapi.lib")

namespace privacy_guard {
namespace {

std::wstring widen_lower(const char *s)
{
	std::wstring out;
	if (!s || !*s)
		return out;
	int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
	if (len <= 1)
		return out;
	std::vector<wchar_t> buf((size_t)len);
	MultiByteToWideChar(CP_UTF8, 0, s, -1, buf.data(), len);
	out.assign(buf.data());
	for (wchar_t &c : out)
		c = static_cast<wchar_t>(towlower(c));
	return out;
}

std::wstring base_name(const std::wstring &path)
{
	size_t sep = path.find_last_of(L"\\/:");
	return (sep == std::wstring::npos) ? path : path.substr(sep + 1);
}

// Имя образа процесса по PID (только имя файла, нижний регистр).
// Пустая строка = не удалось получить (нет прав, процесс завершен и т.п.).
// Используется QueryFullProcessImageNameW: ему достаточно
// PROCESS_QUERY_LIMITED_INFORMATION (без повышенных прав). GetModuleBaseName
// здесь не подходит: он требует PROCESS_QUERY_INFORMATION | PROCESS_VM_READ
// и молча возвращает пустую строку для чужих процессов.
std::wstring process_image_name(DWORD pid)
{
	std::wstring result;
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!h)
		return result;
	wchar_t buf[MAX_PATH] = {0};
	DWORD size = MAX_PATH;
	if (QueryFullProcessImageNameW(h, 0, buf, &size) && size > 0)
		result.assign(buf, size);
	CloseHandle(h);
	if (!result.empty())
		result = base_name(result);
	for (wchar_t &c : result)
		c = static_cast<wchar_t>(towlower(c));
	return result;
}

void collect_process_names(std::vector<std::wstring> &out)
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return;
	PROCESSENTRY32W pe = {};
	pe.dwSize = sizeof(pe);
	if (Process32FirstW(snap, &pe)) {
		do {
			std::wstring name = pe.szExeFile;
			for (wchar_t &c : name)
				c = static_cast<wchar_t>(towlower(c));
			out.push_back(name);
		} while (Process32NextW(snap, &pe));
	}
	CloseHandle(snap);
}

} // namespace



// Занимает ли окно ровно один монитор целиком (эксклюзивный или
// безрамочный полный экран). Свернутые и невидимые окна не считаются.
// Невидимые рамки DWM делают rect окна чуть больше монитора, поэтому
// допускается превышение площади до 5%. Окно на два монитора (200%)
// отбрасывается: иначе многомониторная раскладка давала бы ложное
// открытие шторки.
static bool covers_monitor(HWND hwnd)
{
	if (!hwnd || IsIconic(hwnd) || !IsWindowVisible(hwnd))
		return false;
	RECT wr = {};
	if (!GetWindowRect(hwnd, &wr))
		return false;
	if (wr.right <= wr.left || wr.bottom <= wr.top)
		return false;
	HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
	if (!mon)
		return false;
	MONITORINFO mi = {};
	mi.cbSize = sizeof(mi);
	if (!GetMonitorInfoW(mon, &mi))
		return false;
	if (!(wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top &&
	      wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom))
		return false;
	long long win_area = (long long)(wr.right - wr.left) * (long long)(wr.bottom - wr.top);
	long long mon_area = (long long)(mi.rcMonitor.right - mi.rcMonitor.left) *
			     (long long)(mi.rcMonitor.bottom - mi.rcMonitor.top);
	if (mon_area <= 0)
		return false;
	return win_area <= mon_area + mon_area / 20;
}

FocusQuery query_game_focus(const std::vector<std::string> &targets, bool allow_fullscreen)
{
	FocusQuery q;

	std::vector<std::wstring> want;
	want.reserve(targets.size());
	for (const std::string &t : targets) {
		std::wstring w = base_name(widen_lower(t.c_str()));
		if (!w.empty())
			want.push_back(w);
	}
	if (want.empty() && !allow_fullscreen)
		return q; // api_ok=false: не за чем следить = закрытое состояние

	// Активное окно может отсутствовать (UAC, secure desktop) - это
	// неизвестное состояние, его нельзя трактовать как "игра активна".
	HWND fg = GetForegroundWindow();
	if (!fg)
		return q; // api_ok=false

	DWORD fg_pid = 0;
	if (!GetWindowThreadProcessId(fg, &fg_pid) || fg_pid == 0)
		return q; // api_ok=false

	q.api_ok = true;

	std::vector<std::wstring> running;
	collect_process_names(running);
	std::wstring fg_exe = process_image_name(fg_pid);

	for (const std::wstring &w : want) {
		bool exists = false;
		for (const std::wstring &r : running) {
			if (r == w) {
				exists = true;
				break;
			}
		}
		if (exists)
			q.process_exists = true;
		if (!fg_exe.empty() && fg_exe == w) {
			q.focused = true;
			q.process_exists = true;
		}
	}
	if (!q.focused && allow_fullscreen && covers_monitor(fg)) {
		// Чужое полноэкранное окно (игра без вписывания EXE).
		// Процесс из списка может отсутствовать, это нормально.
		q.fullscreen_other = true;
		q.focused = true;
		q.process_exists = true;
	}
	return q;
}



struct VisibleProbe {
	const std::vector<std::wstring> *want;
	bool allow_any;
	RECT fg_mon;
	bool fg_mon_ok;
	bool found;
};

static BOOL CALLBACK visible_enum_cb(HWND hwnd, LPARAM lp)
{
	VisibleProbe *probe = reinterpret_cast<VisibleProbe *>(lp);
	if (!hwnd || !IsWindowVisible(hwnd) || IsIconic(hwnd))
		return TRUE;
	if (!covers_monitor(hwnd))
		return TRUE; // не полный экран: дальше не интересно
	HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
	if (!mon)
		return TRUE;
	MONITORINFO mi = {};
	mi.cbSize = sizeof(mi);
	if (!GetMonitorInfoW(mon, &mi))
		return TRUE;
	// Игра должна быть на ДРУГОМ мониторе, чем фокус: иначе окно
	// поверх игры на том же экране ушло бы в эфир.
	if (probe->fg_mon_ok && mi.rcMonitor.left == probe->fg_mon.left &&
	    mi.rcMonitor.top == probe->fg_mon.top && mi.rcMonitor.right == probe->fg_mon.right &&
	    mi.rcMonitor.bottom == probe->fg_mon.bottom)
		return TRUE;
	if (probe->allow_any) {
		probe->found = true;
		return FALSE;
	}
	DWORD pid = 0;
	if (!GetWindowThreadProcessId(hwnd, &pid) || pid == 0)
		return TRUE;
	std::wstring exe = process_image_name(pid);
	if (exe.empty())
		return TRUE;
	for (const std::wstring &w : *probe->want) {
		if (exe == w) {
			probe->found = true;
			return FALSE;
		}
	}
	return TRUE;
}

bool game_visible_elsewhere(const std::vector<std::string> &targets, bool allow_fullscreen)
{
	std::vector<std::wstring> want;
	want.reserve(targets.size());
	for (const std::string &t : targets) {
		std::wstring w = base_name(widen_lower(t.c_str()));
		if (!w.empty())
			want.push_back(w);
	}
	if (want.empty() && !allow_fullscreen)
		return false;
	HWND fg = GetForegroundWindow();
	VisibleProbe probe{&want, allow_fullscreen, {}, false, false};
	if (fg) {
		HMONITOR fmon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
		if (fmon) {
			MONITORINFO mi = {};
			mi.cbSize = sizeof(mi);
			if (GetMonitorInfoW(fmon, &mi)) {
				probe.fg_mon = mi.rcMonitor;
				probe.fg_mon_ok = true;
			}
		}
	}
	EnumWindows(visible_enum_cb, reinterpret_cast<LPARAM>(&probe));
	return probe.found;
}



} // namespace privacy_guard
