// OBS Privacy Guard - фильтр-автошторка.
//
// Архитектура: OBS_SOURCE_TYPE_FILTER поверх источника захвата.
// Фильтр рисует в пространстве кадра родителя, поэтому покрытие всегда
// совпадает с геометрией источника. Рендер всегда fail-closed: любое
// сомнительное состояние рисует заглушку и никогда не пропускает
// необработанный кадр.

#include "privacy-guard-filter.h"
#include "focus-watch.h"
#include "game-scan.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#endif

#include <util/base.h>
#include <util/platform.h>

#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#define SETTING_PROCESS_EXE "process_exe" // устаревшее одиночное (миграция)
#define SETTING_PROCESS_LIST "process_list" // многострочный текст
#define SETTING_PROCESS_FILE "process_file" // выбор EXE файловым диалогом
#define SETTING_PROCESS_RUNNING "process_running"
#define SETTING_GAME_DIR "game_dir"
#define VISIBLE_POLL_NS 500000000ULL
#define GUARD_MAX_MASKS 8
#define SETTING_ALLOW_FULLSCREEN "allow_fullscreen"
#define SETTING_FULLSCREEN_DENY "fullscreen_deny" // выбор из запущенных
#define SETTING_AUTO_ENABLED "auto_enabled"
#define SETTING_RESTORE_DELAY_MS "restore_delay_ms"
#define SETTING_SHUTTER_MODE "shutter_mode"
#define SETTING_SHUTTER_COLOR "shutter_color"
#define SETTING_BLUR_STRENGTH "blur_strength"
#define SETTING_STATUS "status_text"
#define SETTING_TOGGLE_BUTTON "toggle_button"
#define SETTING_MASKS_INFO "masks_info"
#define SETTING_DIAG_INFO "diag_info"

#define POLL_INTERVAL_NS 100000000ULL // опрос фокуса не чаще 10 Гц
#define BLUR_EFFECT_FILE "privacy-guard-blur.effect"

static const char *status_key_for_state(GuardState st)
{
	switch (st) {
	case GuardState::Open:
		return "Status_Open";
	case GuardState::AutoClosed:
		return "Status_AutoClosed";
	case GuardState::ManualClosed:
		return "Status_ManualClosed";
	case GuardState::WaitingRestore:
		return "Status_Waiting";
	case GuardState::ErrorClosed:
	default:
		return "Status_Error";
	}
}

const char *guard_filter_get_name(void * /*type_data*/)
{
	return obs_module_text("FilterName");
}

// Разбор многострочного списка EXE: по одному имени на строку,
// запятые и точки с запятой тоже считаются разделителями.
static std::vector<std::string> parse_exe_list(const char *text)
{
	std::vector<std::string> out;
	if (!text)
		return out;
	std::string cur;
	for (const char *p = text; *p; ++p) {
		char c = *p;
		if (c == '\n' || c == '\r' || c == ';' || c == ',') {
			while (!cur.empty() && (cur.front() == ' ' || cur.front() == '\t'))
				cur.erase(cur.begin());
			while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t'))
				cur.pop_back();
			if (!cur.empty())
				out.push_back(cur);
			cur.clear();
		} else {
			cur += c;
		}
	}
	while (!cur.empty() && (cur.front() == ' ' || cur.front() == '\t'))
		cur.erase(cur.begin());
	while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t'))
		cur.pop_back();
	if (!cur.empty())
		out.push_back(cur);
	return out;
}

static void guard_refresh_status_locked(guard_filter *gf)
{
	std::string text = obs_module_text(status_key_for_state(gf->state));
	text += " | ";
	if (gf->input_w > 0 && gf->input_h > 0) {
		char buf[64];
		snprintf(buf, sizeof(buf), "%s %ux%u", obs_module_text("Status_Frame"),
			 (unsigned)gf->input_w, (unsigned)gf->input_h);
		text += buf;
	} else {
		text += obs_module_text("Status_NoFrame");
	}
	if (!gf->blur_effect) {
		text += " | ";
		text += obs_module_text("Status_NoEffect");
	}
	if (gf->visible_now) {
		text += " | ";
		text += obs_module_text("Status_VisibleGame");
	}
	{
		char buf[64];
		snprintf(buf, sizeof(buf), " | %s: %u", obs_module_text("Status_Games"),
			 (unsigned)gf->process_list.size());
		text += buf;
		std::string names;
		for (size_t i = 0; i < gf->process_list.size() && names.size() < 96; i++) {
			if (i > 0)
				names += ", ";
			names += gf->process_list[i];
		}
		if (!names.empty()) {
			text += " (";
			text += names;
			text += ")";
		}
	}
	snprintf(gf->status_text, sizeof(gf->status_text), "%s", text.c_str());
}

static std::string lower_ascii_str(const std::string &s)
{
	std::string out = s;
	for (char &c : out)
		c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return out;
}

// Добавить имя в список, если его там еще нет (сравнение без учета регистра).
static void push_unique(std::vector<std::string> &list, const std::string &name)
{
	if (name.empty())
		return;
	std::string low = lower_ascii_str(name);
	for (const std::string &e : list) {
		if (lower_ascii_str(e) == low)
			return;
	}
	list.push_back(name);
}

// Только имя файла из возможного полного пути.
static std::string exe_base_name(const char *path)
{
	if (!path || !*path)
		return std::string();
	std::string s = path;
	size_t sep = s.find_last_of("\\/:");
	if (sep != std::string::npos)
		s = s.substr(sep + 1);
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
		s.erase(s.begin());
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '"' || s.back() == '\''))
		s.pop_back();
	if (!s.empty() && s.front() == '"' || !s.empty() && s.front() == '\'')
		s.erase(s.begin());
	return s;
}

#ifdef _WIN32
// Снимок имен запущенных процессов для выпадающего списка (поток UI).
// Только имена EXE, без путей и заголовков окон.
static std::string wide_to_utf8_str(const wchar_t *ws)
{
	std::string out;
	if (!ws || !*ws)
		return out;
	int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1)
		return out;
	out.resize((size_t)n - 1);
	WideCharToMultiByte(CP_UTF8, 0, ws, -1, out.data(), n, nullptr, nullptr);
	return out;
}

// Дописать имена в живой список источника сразу (кнопка работает в потоке UI).
static void add_names_live(guard_filter *gf, const char *key, const char *sep, const std::vector<std::string> &names)
{
	if (!gf || !gf->source || !key || names.empty())
		return;
	obs_data_t *cur = obs_source_get_settings(gf->source);
	if (!cur)
		return;
	std::vector<std::string> list = parse_exe_list(obs_data_get_string(cur, key));
	for (size_t i = 0; i < names.size(); i++)
		push_unique(list, names[i]);
	std::string merged;
	for (size_t i = 0; i < list.size(); i++) {
		if (i > 0)
			merged += sep;
		merged += list[i];
	}
	obs_data_set_string(cur, key, merged.c_str());
	obs_source_update(gf->source, cur);
	obs_data_release(cur);
}

#define NL_SEP "\n"

static bool add_exe_button_cb(obs_properties_t * /*props*/, obs_property_t * /*prop*/, void *data)
{
	guard_filter *gf = static_cast<guard_filter *>(data);
	if (!gf)
		return true;
	static wchar_t buf[32768];
	memset(buf, 0, sizeof(buf));
	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFile = buf;
	ofn.nMaxFile = 32768;
	ofn.lpstrFilter = L"*.exe\0*.exe\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrTitle = L"";
	ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
	const char *dlgtitle = obs_module_text("Prop_AddExe_Title");
	if (dlgtitle && *dlgtitle) {
		static wchar_t wtitle[256];
		MultiByteToWideChar(CP_UTF8, 0, dlgtitle, -1, wtitle, 256);
		ofn.lpstrTitle = wtitle;
	}
	if (!GetOpenFileNameW(&ofn))
		return true; // отмена
	std::vector<std::string> names;
	const wchar_t *p = buf + wcslen(buf) + 1;
	if (*p == 0) {
		names.push_back(exe_base_name(wide_to_utf8_str(buf).c_str()));
	} else {
		while (*p) {
			names.push_back(exe_base_name(wide_to_utf8_str(p).c_str()));
			p += wcslen(p) + 1;
		}
	}
	add_names_live(gf, SETTING_PROCESS_LIST, NL_SEP, names);
	return true;
}

static bool pick_exe_files(std::vector<std::string> &names, const char *title_key)
{
	wchar_t buf[32768] = {0};
	memset(buf, 0, sizeof(buf));
	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFile = buf;
	ofn.nMaxFile = 32768;
	ofn.lpstrFilter = L"*.exe\0*.exe\0";
	ofn.nFilterIndex = 1;
	const char *dlgtitle = obs_module_text(title_key);
	if (dlgtitle && *dlgtitle) {
		static wchar_t wtitle[256];
		MultiByteToWideChar(CP_UTF8, 0, dlgtitle, -1, wtitle, 256);
		ofn.lpstrTitle = wtitle;
	}
	ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
	if (!GetOpenFileNameW(&ofn))
		return false;
	const wchar_t *p = buf + wcslen(buf) + 1;
	if (*p == 0) {
		names.push_back(exe_base_name(wide_to_utf8_str(buf).c_str()));
	} else {
		while (*p) {
			names.push_back(exe_base_name(wide_to_utf8_str(p).c_str()));
			p += wcslen(p) + 1;
		}
	}
	return true;
}

static bool add_deny_button_cb(obs_properties_t *, obs_property_t *, void *data)
{
	guard_filter *gf = static_cast<guard_filter *>(data);
	if (!gf)
		return true;
	std::vector<std::string> names;
	if (!pick_exe_files(names, "Prop_AddDeny_Title"))
		return true;
	add_names_live(gf, SETTING_FULLSCREEN_DENY, NL_SEP, names);
	return true;
}

static int CALLBACK browse_cb(HWND h, UINT msg, LPARAM, LPARAM lp)
{
	if (msg == BFFM_INITIALIZED && lp)
		SendMessageW(h, BFFM_SETSELECTIONW, TRUE, lp);
	return 0;
}

static bool add_dir_button_cb(obs_properties_t * /*props*/, obs_property_t * /*prop*/, void *data)
{
	guard_filter *gf = static_cast<guard_filter *>(data);
	if (!gf)
		return true;
	wchar_t wtitle[256] = {0};
	const char *dlgtitle = obs_module_text("Prop_AddDir_Title");
	if (dlgtitle && *dlgtitle)
		MultiByteToWideChar(CP_UTF8, 0, dlgtitle, -1, wtitle, 256);
	BROWSEINFOW bi = {};
	bi.lpszTitle = wtitle[0] ? wtitle : nullptr;
	bi.ulFlags = BIF_NEWDIALOGSTYLE;
	LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
	if (!pidl)
		return true; // отмена
	wchar_t path[32768] = {0};
	BOOL got = SHGetPathFromIDListW(pidl, path);
	CoTaskMemFree(pidl);
	if (!got || !path[0])
		return true;
	std::vector<std::string> found = privacy_guard::scan_games_folder(wide_to_utf8_str(path), 3);
	add_names_live(gf, SETTING_PROCESS_LIST, "\n", found);
	return true;
}
#endif

static const char *state_name(GuardState st)
{
	switch (st) {
	case GuardState::Open: return "open";
	case GuardState::AutoClosed: return "auto-closed";
	case GuardState::ManualClosed: return "manual-closed";
	case GuardState::WaitingRestore: return "waiting";
	case GuardState::ErrorClosed: return "error-closed";
	default: return "?";
	}
}

static void guard_apply_focus_locked(guard_filter *gf, bool focused, bool api_ok, bool exists, uint64_t now_ns)
{
	if (gf->manual_closed) {
		gf->state = GuardState::ManualClosed;
		gf->wait_deadline_ns = 0;
		guard_refresh_status_locked(gf);
		return;
	}
	if (!gf->auto_enabled) {
		// Автошторка выключена: изображение открыто, ручная шторка
		// обрабатывается выше.
		gf->state = GuardState::Open;
		gf->wait_deadline_ns = 0;
		guard_refresh_status_locked(gf);
		return;
	}
	if (!api_ok || (gf->process_list.empty() && !gf->allow_fullscreen)) {
		gf->state = GuardState::ErrorClosed;
		gf->wait_deadline_ns = 0;
		guard_refresh_status_locked(gf);
		return;
	}
	if (!exists || !focused) {
		// Закрытие без намеренной задержки. Сброс таймера восстановления.
		gf->state = GuardState::AutoClosed;
		gf->wait_deadline_ns = 0;
		guard_refresh_status_locked(gf);
		return;
	}
	// Одна из игр списка существует и в фокусе.
	if (gf->state == GuardState::Open) {
		return;
	}
	if (gf->state != GuardState::WaitingRestore) {
		gf->state = GuardState::WaitingRestore;
		gf->wait_deadline_ns = now_ns + (uint64_t)gf->restore_delay_ms * 1000000ULL;
		guard_refresh_status_locked(gf);
	}
	if (now_ns >= gf->wait_deadline_ns) {
		gf->state = GuardState::Open;
		gf->wait_deadline_ns = 0;
		guard_refresh_status_locked(gf);
	}
	// Диагностика залипаний: только переходы, без имен окон и процессов.
	if (gf->state != gf->logged_state) {
		blog(LOG_INFO, "[obs-privacy-guard] state %s -> %s"
		     " (focused=%d api=%d exists=%d visible=%d manual=%d)",
		     state_name(gf->logged_state), state_name(gf->state), (int)focused, (int)api_ok,
		     (int)exists, (int)gf->visible_now, (int)gf->manual_closed);
		gf->logged_state = gf->state;
	}
}

static void toggle_hotkey_cb(void *data, obs_hotkey_id /*id*/, obs_hotkey_t * /*hotkey*/, bool pressed)
{
	if (!pressed || !data)
		return;
	guard_filter *gf = static_cast<guard_filter *>(data);
	pthread_mutex_lock(&gf->mutex);
	gf->manual_closed = !gf->manual_closed;
	if (gf->manual_closed) {
		gf->state = GuardState::ManualClosed;
		gf->wait_deadline_ns = 0;
	} else {
		// Ручное открытие не показывает игру мгновенно: следующий tick
		// переведет защиту в ожидание с задержкой восстановления.
		gf->state = GuardState::AutoClosed;
		gf->wait_deadline_ns = 0;
	}
	guard_refresh_status_locked(gf);
	bool now_closed = gf->manual_closed;
	pthread_mutex_unlock(&gf->mutex);
	blog(LOG_INFO, "[obs-privacy-guard] manual shutter %s", now_closed ? "closed" : "opened");
}

static bool toggle_button_cb(obs_properties_t * /*props*/, obs_property_t * /*prop*/, void *data)
{
	if (data)
		toggle_hotkey_cb(data, OBS_INVALID_HOTKEY_ID, nullptr, true);
	return true;
}

static gs_effect_t *load_blur_effect()
{
	char *full = obs_module_file(BLUR_EFFECT_FILE);
	if (!full)
		return nullptr;

	obs_enter_graphics();
	char *err = nullptr;
	gs_effect_t *effect = gs_effect_create_from_file(full, &err);
	obs_leave_graphics();
	bfree(full);
	if (err) {
		blog(LOG_WARNING, "[obs-privacy-guard] blur effect: %s", err);
		bfree(err);
	}
	if (!effect) {
		// Ошибка шейдера обязана давать непрозрачную заглушку:
		// вызывающая сторона проверяет nullptr и рисует плашку.
		blog(LOG_WARNING, "[obs-privacy-guard] blur effect not loaded, opaque fallback");
		return nullptr;
	}
	return effect;
}

void *guard_filter_create(obs_data_t *settings, obs_source_t *source)
{
	guard_filter *gf = new (std::nothrow) guard_filter();
	if (!gf)
		return nullptr;
	gf->source = source;
	pthread_mutex_init(&gf->mutex, nullptr);
	vec4_from_rgba(&gf->shutter_color, 0xFF000000);

	gf->hotkey = obs_hotkey_register_source(source, "ObsPrivacyGuard.Toggle",
						obs_module_text("Hotkey_Toggle"), toggle_hotkey_cb, gf);

	guard_filter_update(gf, settings);
	gf->blur_effect = load_blur_effect();

	// Fail-closed со старта: защита закрыта, пока tick не подтвердит фокус.
	pthread_mutex_lock(&gf->mutex);
	gf->state = gf->manual_closed ? GuardState::ManualClosed : GuardState::ErrorClosed;
	gf->wait_deadline_ns = 0;
	gf->last_poll_ns = 0;
	guard_refresh_status_locked(gf);
	pthread_mutex_unlock(&gf->mutex);
	return gf;
}

void guard_filter_destroy(void *data)
{
	guard_filter *gf = static_cast<guard_filter *>(data);
	if (!gf)
		return;
	if (gf->hotkey != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(gf->hotkey);
		gf->hotkey = OBS_INVALID_HOTKEY_ID;
	}
	if (gf->blur_effect) {
		obs_enter_graphics();
		gs_effect_destroy(gf->blur_effect);
		obs_leave_graphics();
		gf->blur_effect = nullptr;
	}
	pthread_mutex_destroy(&gf->mutex);
	delete gf;
}

void guard_filter_update(void *data, obs_data_t *settings)
{
	guard_filter *gf = static_cast<guard_filter *>(data);
	if (!gf)
		return;
	pthread_mutex_lock(&gf->mutex);
	std::vector<std::string> list = parse_exe_list(obs_data_get_string(settings, SETTING_PROCESS_LIST));
	if (list.empty()) {
		// Миграция со старого одиночного поля.
		const char *old = obs_data_get_string(settings, SETTING_PROCESS_EXE);
		list = parse_exe_list(old);
	}
	// Объединение: ручной список + выбор из файла + выбор из запущенных.
	// Ничего не перезаписывается: файл и комбобокс только добавляют.
	push_unique(list, exe_base_name(obs_data_get_string(settings, SETTING_PROCESS_FILE)));
	push_unique(list, exe_base_name(obs_data_get_string(settings, SETTING_PROCESS_RUNNING)));
	const char *dir = obs_data_get_string(settings, SETTING_GAME_DIR);
	if (dir && *dir) {
		if (gf->game_dir != dir) {
			gf->game_dir = dir;
			gf->scanned_games = privacy_guard::scan_games_folder(dir, 3);
		}
		for (const std::string &g : gf->scanned_games)
			push_unique(list, g);
	} else {
		gf->game_dir.clear();
		gf->scanned_games.clear();
	}
	{
		std::string merged;
		for (size_t i = 0; i < list.size(); i++) {
			if (i > 0)
				merged += "\n";
			merged += list[i];
		}
		obs_data_set_string(settings, SETTING_PROCESS_LIST, merged.c_str());
		obs_data_set_string(settings, SETTING_PROCESS_FILE, "");
		obs_data_set_string(settings, SETTING_PROCESS_RUNNING, "");
	}
	gf->process_list = list;
	for (int i = 0; i < GUARD_MAX_MASKS; i++) {
		char key[64];
		GuardMask m;
		snprintf(key, sizeof(key), "mask%d_enabled", i);
		m.enabled = obs_data_get_bool(settings, key);
		snprintf(key, sizeof(key), "mask%d_x", i);
		m.x = (int)obs_data_get_int(settings, key);
		snprintf(key, sizeof(key), "mask%d_y", i);
		m.y = (int)obs_data_get_int(settings, key);
		snprintf(key, sizeof(key), "mask%d_w", i);
		m.w = (int)obs_data_get_int(settings, key);
		snprintf(key, sizeof(key), "mask%d_h", i);
		m.h = (int)obs_data_get_int(settings, key);
		snprintf(key, sizeof(key), "mask%d_mode", i);
		m.mode = (obs_data_get_int(settings, key) == 1) ? 1 : 0;
		snprintf(key, sizeof(key), "mask%d_color", i);
		m.color = (uint32_t)obs_data_get_int(settings, key);
		if (m.x < 0)
			m.x = 0;
		if (m.x > 100)
			m.x = 100;
		if (m.y < 0)
			m.y = 0;
		if (m.y > 100)
			m.y = 100;
		if (m.w < 0)
			m.w = 0;
		if (m.w > 100)
			m.w = 100;
		if (m.h < 0)
			m.h = 0;
		if (m.h > 100)
			m.h = 100;
		gf->masks[i] = m;
	}
	gf->auto_enabled = obs_data_get_bool(settings, SETTING_AUTO_ENABLED);
	gf->allow_fullscreen = obs_data_get_bool(settings, SETTING_ALLOW_FULLSCREEN);
	gf->deny_list = parse_exe_list(obs_data_get_string(settings, SETTING_FULLSCREEN_DENY));
	long long delay = obs_data_get_int(settings, SETTING_RESTORE_DELAY_MS);
	if (delay < 0)
		delay = 0;
	if (delay > 5000)
		delay = 5000;
	gf->restore_delay_ms = (uint32_t)delay;
	uint32_t color = (uint32_t)obs_data_get_int(settings, SETTING_SHUTTER_COLOR);
	vec4_from_rgba(&gf->shutter_color, color);
	long long mode = obs_data_get_int(settings, SETTING_SHUTTER_MODE);
	gf->shutter_mode = (mode == 1) ? ShutterMode::Blur : ShutterMode::Opaque;
	long long strength = obs_data_get_int(settings, SETTING_BLUR_STRENGTH);
	if (strength < 1)
		strength = 1;
	if (strength > 10)
		strength = 10;
	gf->blur_radius = (float)strength;
	guard_refresh_status_locked(gf);
	pthread_mutex_unlock(&gf->mutex);
}

void guard_filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, SETTING_PROCESS_LIST, "");
	obs_data_set_default_string(settings, SETTING_PROCESS_FILE, "");
	obs_data_set_default_string(settings, SETTING_PROCESS_RUNNING, "");
	obs_data_set_default_string(settings, SETTING_GAME_DIR, "");
	for (int i = 0; i < GUARD_MAX_MASKS; i++) {
		char key[64];
		snprintf(key, sizeof(key), "mask%d_enabled", i);
		obs_data_set_default_bool(settings, key, false);
		snprintf(key, sizeof(key), "mask%d_x", i);
		obs_data_set_default_int(settings, key, 0);
		snprintf(key, sizeof(key), "mask%d_y", i);
		obs_data_set_default_int(settings, key, 0);
		snprintf(key, sizeof(key), "mask%d_w", i);
		obs_data_set_default_int(settings, key, 0);
		snprintf(key, sizeof(key), "mask%d_h", i);
		obs_data_set_default_int(settings, key, 0);
		snprintf(key, sizeof(key), "mask%d_mode", i);
		obs_data_set_default_int(settings, key, 0);
		snprintf(key, sizeof(key), "mask%d_color", i);
		obs_data_set_default_int(settings, key, 0xFF000000);
	}
	obs_data_set_default_bool(settings, SETTING_AUTO_ENABLED, true);
	obs_data_set_default_bool(settings, SETTING_ALLOW_FULLSCREEN, true);
	obs_data_set_default_string(settings, SETTING_FULLSCREEN_DENY, "");
	obs_data_set_default_int(settings, SETTING_RESTORE_DELAY_MS, 500);
	obs_data_set_default_int(settings, SETTING_SHUTTER_MODE, 0);
	obs_data_set_default_int(settings, SETTING_SHUTTER_COLOR, 0xFF000000);
	obs_data_set_default_int(settings, SETTING_BLUR_STRENGTH, 6);
}

obs_properties_t *guard_filter_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	(void)obs_properties_add_text(props, "section_game", obs_module_text("Prop_SectionGame"),
				      OBS_TEXT_INFO);
	obs_property_t *list = obs_properties_add_text(props, SETTING_PROCESS_LIST,
						       obs_module_text("Prop_ProcessList"),
						       OBS_TEXT_MULTILINE);
	obs_property_set_long_description(list, obs_module_text("Prop_ProcessList_Desc"));
#ifdef _WIN32
	(void)obs_properties_add_button2(props, "add_exe_button", obs_module_text("Prop_AddExe"),
					 add_exe_button_cb, data);
	(void)obs_properties_add_button2(props, "add_dir_button", obs_module_text("Prop_AddDir"),
					 add_dir_button_cb, data);
#endif
	(void)obs_properties_add_bool(props, SETTING_AUTO_ENABLED, obs_module_text("Prop_AutoEnabled"));
	obs_property_t *fsprop = obs_properties_add_bool(props, SETTING_ALLOW_FULLSCREEN,
						      obs_module_text("Prop_AllowFullscreen"));
	obs_property_set_long_description(fsprop, obs_module_text("Prop_AllowFullscreen_Desc"));
	obs_property_t *fdeny = obs_properties_add_text(props, SETTING_FULLSCREEN_DENY,
						      obs_module_text("Prop_FullscreenDeny"),
						      OBS_TEXT_MULTILINE);
	obs_property_set_long_description(fdeny, obs_module_text("Prop_FullscreenDeny_Desc"));
#ifdef _WIN32
	(void)obs_properties_add_button2(props, "add_deny_button", obs_module_text("Prop_AddDeny"),
					 add_deny_button_cb, data);
#endif
	(void)obs_properties_add_int(props, SETTING_RESTORE_DELAY_MS,
				     obs_module_text("Prop_RestoreDelay"), 0, 5000, 50);
	(void)obs_properties_add_text(props, SETTING_STATUS, obs_module_text("Prop_Status"), OBS_TEXT_INFO);

	(void)obs_properties_add_text(props, "section_shutter", obs_module_text("Prop_SectionShutter"),
				      OBS_TEXT_INFO);
	obs_property_t *mode = obs_properties_add_list(props, SETTING_SHUTTER_MODE,
						       obs_module_text("Prop_ShutterMode"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mode, obs_module_text("Prop_ShutterMode_Opaque"), 0);
	obs_property_list_add_int(mode, obs_module_text("Prop_ShutterMode_Blur"), 1);
	(void)obs_properties_add_color(props, SETTING_SHUTTER_COLOR, obs_module_text("Prop_ShutterColor"));
	(void)obs_properties_add_int(props, SETTING_BLUR_STRENGTH, obs_module_text("Prop_BlurStrength"),
				     1, 10, 1);

	(void)obs_properties_add_text(props, "section_masks", obs_module_text("Prop_SectionMasks"),
				      OBS_TEXT_INFO);
	obs_property_t *masks_hint = obs_properties_add_text(props, SETTING_MASKS_INFO, "Info", OBS_TEXT_INFO);
	obs_property_set_long_description(masks_hint, obs_module_text("Prop_MasksInfo"));
	for (int i = 0; i < GUARD_MAX_MASKS; i++) {
		char gid[64], key[64], title[128];
		snprintf(gid, sizeof(gid), "mask_group_%d", i);
		snprintf(title, sizeof(title), "%s %d", obs_module_text("Prop_MaskTitle"), i + 1);
		obs_properties_t *sub = obs_properties_create();
		snprintf(key, sizeof(key), "mask%d_enabled", i);
		(void)obs_properties_add_bool(sub, key, obs_module_text("Prop_MaskEnabled"));
		snprintf(key, sizeof(key), "mask%d_x", i);
		(void)obs_properties_add_int(sub, key, obs_module_text("Prop_MaskX"), 0, 100, 1);
		snprintf(key, sizeof(key), "mask%d_y", i);
		(void)obs_properties_add_int(sub, key, obs_module_text("Prop_MaskY"), 0, 100, 1);
		snprintf(key, sizeof(key), "mask%d_w", i);
		(void)obs_properties_add_int(sub, key, obs_module_text("Prop_MaskW"), 0, 100, 1);
		snprintf(key, sizeof(key), "mask%d_h", i);
		(void)obs_properties_add_int(sub, key, obs_module_text("Prop_MaskH"), 0, 100, 1);
		snprintf(key, sizeof(key), "mask%d_mode", i);
		obs_property_t *mmode = obs_properties_add_list(
			sub, key, obs_module_text("Prop_MaskMode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(mmode, obs_module_text("Prop_MaskMode_Opaque"), 0);
		obs_property_list_add_int(mmode, obs_module_text("Prop_MaskMode_Blur"), 1);
		snprintf(key, sizeof(key), "mask%d_color", i);
		(void)obs_properties_add_color(sub, key, obs_module_text("Prop_MaskColor"));
		(void)obs_properties_add_group(props, gid, title, OBS_GROUP_NORMAL, sub);
	}

	(void)obs_properties_add_text(props, "section_hotkey", obs_module_text("Prop_SectionHotkey"),
				      OBS_TEXT_INFO);
	(void)obs_properties_add_button2(props, SETTING_TOGGLE_BUTTON, obs_module_text("Prop_ToggleButton"),
					 toggle_button_cb, data);
	obs_property_t *hint = obs_properties_add_text(props, "hotkey_hint", "Info", OBS_TEXT_INFO);
	obs_property_set_long_description(hint, obs_module_text("Prop_HotkeyHint"));

	(void)obs_properties_add_text(props, "section_diag", obs_module_text("Prop_SectionDiag"),
				      OBS_TEXT_INFO);
	obs_property_t *diag = obs_properties_add_text(props, SETTING_DIAG_INFO, "Info", OBS_TEXT_INFO);
	obs_property_set_long_description(diag, obs_module_text("Prop_DiagInfo"));

	return props;
}

void guard_filter_tick(void *data, float /*seconds*/)
{
	guard_filter *gf = static_cast<guard_filter *>(data);
	if (!gf)
		return;

	uint64_t now_ns = os_gettime_ns();

	// Копируем настройки под мьютексом, Win32-опрос делаем без мьютекса.
	pthread_mutex_lock(&gf->mutex);
	std::vector<std::string> targets = gf->process_list;
	std::vector<std::string> gf_deny = gf->deny_list;
	bool manual = gf->manual_closed;
	bool auto_en = gf->auto_enabled;
	bool allow_fs = gf->allow_fullscreen;
	uint64_t last_vis = gf->last_visible_poll_ns;
	uint64_t last_poll = gf->last_poll_ns;
	pthread_mutex_unlock(&gf->mutex);

	if (!auto_en || manual) {
		pthread_mutex_lock(&gf->mutex);
		guard_apply_focus_locked(gf, false, true, true, now_ns);
		pthread_mutex_unlock(&gf->mutex);
		return;
	}

	if (now_ns - last_poll < POLL_INTERVAL_NS) {
		// Между опросами только проверяем истечение задержки восстановления.
		pthread_mutex_lock(&gf->mutex);
		if (gf->state == GuardState::WaitingRestore && now_ns >= gf->wait_deadline_ns &&
		    !gf->manual_closed) {
			// Повторно проверяем фокус перед открытием, чтобы не открыть
			// по устаревшим данным. Быстрый повторный опрос допустим.
			targets = gf->process_list;
			allow_fs = gf->allow_fullscreen;
			gf_deny = gf->deny_list;
			pthread_mutex_unlock(&gf->mutex);
			privacy_guard::FocusQuery q = privacy_guard::query_game_focus(targets, allow_fs, gf_deny);
			pthread_mutex_lock(&gf->mutex);
			gf->last_poll_ns = now_ns;
			guard_apply_focus_locked(gf, q.focused, q.api_ok, q.process_exists, now_ns);
			pthread_mutex_unlock(&gf->mutex);
			return;
		}
		pthread_mutex_unlock(&gf->mutex);
		return;
	}

	privacy_guard::FocusQuery q = privacy_guard::query_game_focus(targets, allow_fs, gf_deny);

	bool vis_now = false;
	if (now_ns - last_vis >= VISIBLE_POLL_NS) {
		vis_now = privacy_guard::game_visible_elsewhere(targets, gf_deny);
		pthread_mutex_lock(&gf->mutex);
		gf->last_visible_poll_ns = now_ns;
		gf->visible_now = vis_now;
		pthread_mutex_unlock(&gf->mutex);
	} else {
		pthread_mutex_lock(&gf->mutex);
		vis_now = gf->visible_now;
		pthread_mutex_unlock(&gf->mutex);
	}
	// Игра видна на весь монитор, а фокус на другом экране (клик по
	// второму монитору): картинка не изменилась, закрывать не надо.
	if (vis_now) {
		q.focused = true;
		q.process_exists = true;
		q.game_visible = true;
	}

	pthread_mutex_lock(&gf->mutex);
	gf->last_poll_ns = now_ns;
	gf->visible_now = vis_now;
	obs_source_t *parent = obs_filter_get_parent(gf->source);
	gf->input_w = parent ? obs_source_get_width(parent) : 0;
	gf->input_h = parent ? obs_source_get_height(parent) : 0;
	guard_apply_focus_locked(gf, q.focused, q.api_ok, q.process_exists, now_ns);
	pthread_mutex_unlock(&gf->mutex);
}

static void render_opaque(uint32_t width, uint32_t height, const struct vec4 *color)
{
	if (width == 0 || height == 0) {
		// Нет размеров кадра: очищаем цель, чтобы не пропустить мусор.
		gs_clear(GS_CLEAR_COLOR, color, 1.0f, 0);
		return;
	}
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	if (!solid) {
		gs_clear(GS_CLEAR_COLOR, color, 1.0f, 0);
		return;
	}
	gs_eparam_t *param = gs_effect_get_param_by_name(solid, "color");
	if (param)
		gs_effect_set_vec4(param, color);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, width, height);
}

static void render_opaque(uint32_t width, uint32_t height, const struct vec4 *color); // ниже

static void render_blur_frame(guard_filter *gf, gs_effect_t *effect, uint32_t width, uint32_t height,
			      float radius, float dark, const struct vec4 *fallback_color)
{
	if (!effect || width == 0 || height == 0) {
		render_opaque(width, height, fallback_color);
		return;
	}
	// Размываем ТЕКУЩИЙ кадр родителя. Если входа нет, begin вернет false
	// и уйдем в непрозрачную заглушку: пропускать резкий кадр нельзя.
	if (!obs_source_process_filter_begin(gf->source, GS_RGBA, OBS_NO_DIRECT_RENDERING)) {
		render_opaque(width, height, fallback_color);
		return;
	}
	gs_eparam_t *texel = gs_effect_get_param_by_name(effect, "texel_size");
	gs_eparam_t *pradius = gs_effect_get_param_by_name(effect, "blur_radius");
	gs_eparam_t *pdark = gs_effect_get_param_by_name(effect, "darken");
	struct vec2 texel_size;
	vec2_set(&texel_size, 1.0f / (float)width, 1.0f / (float)height);
	if (texel)
		gs_effect_set_vec2(texel, &texel_size);
	if (pradius)
		gs_effect_set_float(pradius, radius);
	if (pdark)
		gs_effect_set_float(pdark, dark);
	obs_source_process_filter_end(gf->source, effect, width, height);
}

static bool masks_active_locked(guard_filter *gf)
{
	for (int i = 0; i < GUARD_MAX_MASKS; i++) {
		const GuardMask &m = gf->masks[i];
		if (m.enabled && m.w > 0 && m.h > 0)
			return true;
	}
	return false;
}

static void render_masks_frame(guard_filter *gf, gs_effect_t *effect, const GuardMask *masks,
			       uint32_t width, uint32_t height, float radius,
			       const struct vec4 *fallback_color)
{
	if (!effect || width == 0 || height == 0) {
		render_opaque(width, height, fallback_color);
		return;
	}
	// Маски поверх ОРИГИНАЛА: отсутствие входа обязано давать заглушку.
	if (!obs_source_process_filter_begin(gf->source, GS_RGBA, OBS_NO_DIRECT_RENDERING)) {
		render_opaque(width, height, fallback_color);
		return;
	}
	gs_eparam_t *texel = gs_effect_get_param_by_name(effect, "texel_size");
	gs_eparam_t *pradius = gs_effect_get_param_by_name(effect, "mask_blur_radius");
	struct vec2 texel_size;
	vec2_set(&texel_size, 1.0f / (float)width, 1.0f / (float)height);
	if (texel)
		gs_effect_set_vec2(texel, &texel_size);
	if (pradius)
		gs_effect_set_float(pradius, radius);
	// Парсер эффектов OBS не поддерживает массивы uniform: 24 отдельных
	// параметра mask_rN/mask_aN/mask_cN выставляются по именам.
	for (int i = 0; i < GUARD_MAX_MASKS; i++) {
		const GuardMask &m = masks[i];
		char name[32];
		struct vec4 v;
		snprintf(name, sizeof(name), "mask_r%d", i);
		gs_eparam_t *pr = gs_effect_get_param_by_name(effect, name);
		if (pr) {
			vec4_set(&v, (float)m.x / 100.0f, (float)m.y / 100.0f,
				 (float)m.w / 100.0f, (float)m.h / 100.0f);
			gs_effect_set_vec4(pr, &v);
		}
		snprintf(name, sizeof(name), "mask_a%d", i);
		gs_eparam_t *pa = gs_effect_get_param_by_name(effect, name);
		if (pa) {
			vec4_set(&v, (m.enabled && m.w > 0 && m.h > 0) ? 1.0f : 0.0f,
				 (m.mode == 1) ? 1.0f : 0.0f, 0.0f, 0.0f);
			gs_effect_set_vec4(pa, &v);
		}
		snprintf(name, sizeof(name), "mask_c%d", i);
		gs_eparam_t *pc = gs_effect_get_param_by_name(effect, name);
		if (pc) {
			vec4_from_rgba(&v, m.color);
			gs_effect_set_vec4(pc, &v);
		}
	}
	// Техника Mask: process_filter_end всегда берет Draw, поэтому явный вызов.
	obs_source_process_filter_tech_end(gf->source, effect, width, height, "Mask");
}

void guard_filter_render(void *data, gs_effect_t * /*effect*/)
{
	guard_filter *gf = static_cast<guard_filter *>(data);
	if (!gf)
		return;

	pthread_mutex_lock(&gf->mutex);
	GuardState st = gf->state;
	ShutterMode mode = gf->shutter_mode;
	struct vec4 color = gf->shutter_color;
	float radius = gf->blur_radius;
	float dark = gf->blur_dark;
	gs_effect_t *blur = gf->blur_effect;
	pthread_mutex_unlock(&gf->mutex);

	obs_source_t *parent = obs_filter_get_parent(gf->source);
	uint32_t width = parent ? obs_source_get_width(parent) : 0;
	uint32_t height = parent ? obs_source_get_height(parent) : 0;
	if (width == 0)
		width = obs_source_get_width(gf->source);
	if (height == 0)
		height = obs_source_get_height(gf->source);

	if (st == GuardState::Open) {
		pthread_mutex_lock(&gf->mutex);
		bool use_masks = masks_active_locked(gf);
		gs_effect_t *mask_fx = gf->blur_effect;
		std::array<GuardMask, GUARD_MAX_MASKS> snap = gf->masks;
		pthread_mutex_unlock(&gf->mutex);
		if (!use_masks) {
			// Прямой пропуск кадра родителя. Если входа нет (begin вернул
			// false), уходим в закрытое состояние: отсутствие входного кадра
			// обязано давать заглушку, а не пропуск.
			if (!obs_source_process_filter_begin(gf->source, GS_RGBA,
							     OBS_NO_DIRECT_RENDERING)) {
				render_opaque(width, height, &color);
				return;
			}
			obs_source_process_filter_end(gf->source, obs_get_base_effect(OBS_EFFECT_DEFAULT),
						      width, height);
			return;
		}
		render_masks_frame(gf, mask_fx, snap.data(), width, height, radius, &color);
		return;
	}

	if (mode == ShutterMode::Blur) {
		render_blur_frame(gf, blur, width, height, radius, dark, &color);
		return;
	}

	render_opaque(width, height, &color);
}

void guard_filter_save(void *data, obs_data_t *settings)
{
	guard_filter *gf = static_cast<guard_filter *>(data);
	if (!gf)
		return;
	if (gf->hotkey != OBS_INVALID_HOTKEY_ID) {
		obs_data_array_t *arr = obs_hotkey_save(gf->hotkey);
		if (arr) {
			obs_data_set_array(settings, "toggle_hotkey", arr);
			obs_data_array_release(arr);
		}
	}
}

void guard_filter_load(void *data, obs_data_t *settings)
{
	guard_filter *gf = static_cast<guard_filter *>(data);
	if (!gf)
		return;
	if (gf->hotkey != OBS_INVALID_HOTKEY_ID) {
		obs_data_array_t *arr = obs_data_get_array(settings, "toggle_hotkey");
		if (arr) {
			obs_hotkey_load(gf->hotkey, arr);
			obs_data_array_release(arr);
		}
	}
}
