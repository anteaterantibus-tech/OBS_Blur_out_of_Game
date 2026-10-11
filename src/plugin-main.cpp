// OBS Privacy Guard - точка входа плагина.
#include <obs-module.h>
#include <util/base.h>

#include "privacy-guard-filter.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-privacy-guard", "en-US")
OBS_MODULE_AUTHOR("Privacy Guard");

static struct obs_source_info guard_filter_info = {};

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

	blog(LOG_INFO, "[obs-privacy-guard] loaded (version %s)", "0.7.6");
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[obs-privacy-guard] unloaded");
}
