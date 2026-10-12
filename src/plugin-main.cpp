// OBS Privacy Guard - точка входа плагина.
#include <obs-module.h>
#include <util/base.h>

#include "privacy-guard-filter.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-privacy-guard", "en-US")
OBS_MODULE_AUTHOR("Privacy Guard");

static struct obs_source_info guard_filter_info = {};

static obs_hotkey_id hotkey_toggle = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id hotkey_add = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id hotkey_remove = OBS_INVALID_HOTKEY_ID;

// Дублирующее сохранение привязок в настройки сцены: штатный механизм
// OBS иногда не возвращает их после рестарта, а здесь восстановление
// детерминировано чтением собственных данных при создании фильтра.
static void save_one(obs_data_t *settings, const char *key, obs_hotkey_id id)
{
	if (id == OBS_INVALID_HOTKEY_ID)
		return;
	obs_data_array_t *arr = obs_hotkey_save(id);
	if (!arr)
		return;
	obs_data_set_array(settings, key, arr);
	obs_data_array_release(arr);
}

static void load_one(obs_data_t *settings, const char *key, obs_hotkey_id id)
{
	if (id == OBS_INVALID_HOTKEY_ID)
		return;
	obs_data_array_t *arr = obs_data_get_array(settings, key);
	if (!arr)
		return;
	if (obs_data_array_count(arr) > 0)
		obs_hotkey_load(id, arr);
	obs_data_array_release(arr);
}

void guard_save_hotkeys(obs_data_t *settings)
{
	save_one(settings, "guard_toggle_hk", hotkey_toggle);
	save_one(settings, "guard_add_hk", hotkey_add);
	save_one(settings, "guard_remove_hk", hotkey_remove);
}

void guard_load_hotkeys(obs_data_t *settings)
{
	load_one(settings, "guard_toggle_hk", hotkey_toggle);
	load_one(settings, "guard_add_hk", hotkey_add);
	load_one(settings, "guard_remove_hk", hotkey_remove);
}

static void on_toggle_hotkey(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		guard_toggle_all();
}

static void on_add_hotkey(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		guard_fg_all(true);
}

static void on_remove_hotkey(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		guard_fg_all(false);
}

bool obs_module_load(void)
{
	memset(&guard_filter_info, 0, sizeof(guard_filter_info));
	guard_filter_info.id = "obs_privacy_guard_filter";
	guard_filter_info.type = OBS_SOURCE_TYPE_FILTER;
	guard_filter_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;
	guard_filter_info.get_name = guard_filter_get_name;
	guard_filter_info.create = guard_filter_create;
	guard_filter_info.destroy = guard_filter_destroy;
	guard_filter_info.update = guard_filter_update;
	guard_filter_info.get_defaults = guard_filter_defaults;
	guard_filter_info.get_properties = guard_filter_properties;
	guard_filter_info.video_tick = guard_filter_tick;
	guard_filter_info.video_render = guard_filter_render;
	guard_filter_info.save = guard_filter_save;
	guard_filter_info.load = guard_filter_load;
	guard_filter_info.icon_type = OBS_ICON_TYPE_UNKNOWN;

	obs_register_source(&guard_filter_info);

	// Фронтенд-клавиши (видны в настройках всегда): фильтрам свои запрещены.
	hotkey_toggle = obs_hotkey_register_frontend("ObsPrivacyGuard.Toggle",
						     obs_module_text("Hotkey_Toggle"), on_toggle_hotkey, nullptr);
	hotkey_add = obs_hotkey_register_frontend("ObsPrivacyGuard.AddFg", obs_module_text("Hotkey_AddFg"),
						  on_add_hotkey, nullptr);
	hotkey_remove = obs_hotkey_register_frontend("ObsPrivacyGuard.RemoveFg",
						     obs_module_text("Hotkey_RemoveFg"), on_remove_hotkey,
						     nullptr);
	blog(LOG_INFO, "[obs-privacy-guard] hotkeys registered: toggle=%d add=%d remove=%d (invalid=%d)",
	     (int)hotkey_toggle, (int)hotkey_add, (int)hotkey_remove, (int)OBS_INVALID_HOTKEY_ID);

	blog(LOG_INFO, "[obs-privacy-guard] loaded (version %s)", "0.9.3");
	return true;
}

void obs_module_unload(void)
{
	if (hotkey_toggle != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(hotkey_toggle);
	if (hotkey_add != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(hotkey_add);
	if (hotkey_remove != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(hotkey_remove);
	hotkey_toggle = hotkey_add = hotkey_remove = OBS_INVALID_HOTKEY_ID;
	blog(LOG_INFO, "[obs-privacy-guard] unloaded");
}
