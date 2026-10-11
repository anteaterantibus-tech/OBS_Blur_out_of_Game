// OBS Privacy Guard - фильтр-автошторка, этап 1.
#pragma once

#include <obs-module.h>
#include <pthread.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Состояние защиты. Любое состояние кроме Open рисует заглушку.
enum class GuardState : int {
	Open = 0,
	AutoClosed = 1,
	ManualClosed = 2,
	WaitingRestore = 3,
	ErrorClosed = 4,
};

enum class ShutterMode : int {
	Opaque = 0,
	Blur = 1,
};

#define GUARD_MAX_MASKS 8

// Приватная область в процентах кадра источника (0..100).
struct GuardMask {
	bool enabled = false;
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
	int mode = 0; // 0 = плашка, 1 = размытие
	uint32_t color = 0xFF000000;
};

struct guard_filter {
	obs_source_t *source = nullptr;
	pthread_mutex_t mutex;
	gs_effect_t *blur_effect = nullptr; // создается под graphics-мьютексом

	// Настройки (под мьютексом).
	std::vector<std::string> process_list; // имена EXE, по одному на игру
	std::vector<std::string> deny_list; // никогда не считать игрой (браузер)
	std::string game_dir; // закэшированный путь папки сканирования
	std::vector<std::string> scanned_games; // результат последнего скана
	bool auto_enabled = true;
	bool allow_fullscreen = true; // чужое полноэкранное окно считается игрой
	uint32_t restore_delay_ms = 500;
	struct vec4 shutter_color = {};
	ShutterMode shutter_mode = ShutterMode::Opaque;
	float blur_radius = 6.0f; // множитель шага выборки шейдера
	float blur_dark = 0.30f; // затемнение размытой шторки
	int blur_kind = 0; // 0 = гаусс, 1 = пикселизация

	// Состояние (под мьютексом).
	bool manual_closed = false;
	GuardState state = GuardState::ErrorClosed; // fail-closed со старта
	GuardState logged_state = GuardState::ErrorClosed; // последнее залогированное
	uint64_t wait_deadline_ns = 0;
	uint64_t last_poll_ns = 0;
	uint64_t last_visible_poll_ns = 0;
	bool visible_now = false;
	uint32_t input_w = 0; // размеры входного кадра родителя
	uint32_t input_h = 0;
	std::array<GuardMask, GUARD_MAX_MASKS> masks;
	char status_text[256] = {0};
};

const char *guard_filter_get_name(void *type_data);
void *guard_filter_create(obs_data_t *settings, obs_source_t *source);
void guard_filter_destroy(void *data);
void guard_filter_update(void *data, obs_data_t *settings);
void guard_filter_defaults(obs_data_t *settings);
obs_properties_t *guard_filter_properties(void *data);
void guard_filter_tick(void *data, float seconds);
void guard_filter_render(void *data, gs_effect_t *effect);
void guard_filter_save(void *data, obs_data_t *settings);
void guard_filter_load(void *data, obs_data_t *settings);

// Глобальные действия для фронтенд-хоткеев (все инстансы сразу).
void guard_toggle_all();
void guard_fg_all(bool add);
