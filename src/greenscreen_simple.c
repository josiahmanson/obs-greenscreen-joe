#include <obs-module.h>
#include <graphics/matrix4.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>

/* clang-format off */

#define SETTING_KEY_COLOR              "key_color"
#define SETTING_SIMILARITY             "similarity"
#define SETTING_DESATURATION           "desaturation"
#define SETTING_DARKNESS               "darkness"
#define SETTING_RADIUS                 "radius"
#define SETTING_SAMPLE_THRESHOLD       "sample_threshold"

#define TEXT_KEY_COLOR                 obs_module_text("KeyColor")
#define TEXT_SIMILARITY                obs_module_text("Similarity")
#define TEXT_DESATURATION              obs_module_text("Desaturation")
#define TEXT_DARKNESS                  obs_module_text("Darkness")
#define TEXT_RADIUS                    obs_module_text("Radius")
#define TEXT_SAMPLE_THRESHOLD          obs_module_text("Sample Threshold")

/* clang-format on */

struct color_key_filter_data_v2 {
	obs_source_t *context;

	gs_effect_t *effect;

	gs_eparam_t *key_hue_param;
	gs_eparam_t *similarity_param;
	gs_eparam_t *desaturation_param;
	gs_eparam_t *darkness_param;
	gs_eparam_t *radius_param;
	gs_eparam_t *pixel_size_param;
	gs_eparam_t *sample_threshold_param;

	struct vec4 key_color;
	float similarity;
	float desaturation;
	float darkness;
	float radius;
	int sample_threshold;
};

float hue_from_color(struct vec4 c)
{
	float maxc = fmaxf(fmaxf(c.x, c.y), c.z);
	float minc = fminf(fminf(c.x, c.y), c.z);

	if (minc == maxc)
		return 0;

	float diff = maxc - minc;

	float h = 0;
	if (c.x == maxc)
		h = 1.0f + (c.y - c.z) / diff;
	else if (c.y == maxc)
		h = 3.0f + (c.z - c.x) / diff;
	else
		h = 5.0f + (c.x - c.y) / diff;

	return h / 6.0f;
}

static const char *simple_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Greenscreen Joe");
}

static void simple_update(void *data, obs_data_t *settings)
{
	struct color_key_filter_data_v2 *filter = data;

	uint32_t key_color = (uint32_t)obs_data_get_int(settings, SETTING_KEY_COLOR);
	vec4_from_rgba(&filter->key_color, key_color | 0xFF000000);

	filter->similarity = (float)obs_data_get_double(settings, SETTING_SIMILARITY);

	filter->desaturation = (float)obs_data_get_double(settings, SETTING_DESATURATION);

	filter->darkness = (float)obs_data_get_double(settings, SETTING_DARKNESS);

	filter->radius = (float)obs_data_get_double(settings, SETTING_RADIUS);

	filter->sample_threshold = (int)obs_data_get_int(settings, SETTING_SAMPLE_THRESHOLD);
}

static void simple_destroy(void *data)
{
	struct color_key_filter_data_v2 *filter = data;

	if (filter->effect) {
		obs_enter_graphics();
		gs_effect_destroy(filter->effect);
		obs_leave_graphics();
	}

	bfree(data);
}

static void *simple_create(obs_data_t *settings, obs_source_t *context)
{
	struct color_key_filter_data_v2 *filter = bzalloc(sizeof(struct color_key_filter_data_v2));
	char *effect_path = obs_module_file("greenscreen_simple.effect");

	filter->context = context;

	obs_enter_graphics();

	filter->effect = gs_effect_create_from_file(effect_path, NULL);
	if (filter->effect) {
		filter->key_hue_param = gs_effect_get_param_by_name(filter->effect, "key_hue");
		filter->similarity_param = gs_effect_get_param_by_name(filter->effect, "similarity");
		filter->desaturation_param = gs_effect_get_param_by_name(filter->effect, "desaturation");
		filter->darkness_param = gs_effect_get_param_by_name(filter->effect, "darkness");
		filter->radius_param = gs_effect_get_param_by_name(filter->effect, "radius");
		filter->sample_threshold_param = gs_effect_get_param_by_name(filter->effect, "sample_threshold");
		filter->pixel_size_param = gs_effect_get_param_by_name(filter->effect, "pixel_size");
	}

	obs_leave_graphics();

	bfree(effect_path);

	if (!filter->effect) {
		simple_destroy(filter);
		return NULL;
	}

	simple_update(filter, settings);
	return filter;
}

static void simple_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	struct color_key_filter_data_v2 *filter = data;
	obs_source_t *target = obs_filter_get_target(filter->context);

	uint32_t width = obs_source_get_base_width(target);
	uint32_t height = obs_source_get_base_height(target);
	struct vec2 pixel_size;
	vec2_set(&pixel_size, 1.0f / (float)width, 1.0f / (float)height);

	const enum gs_color_space preferred_spaces[] = {
		GS_CS_SRGB,
		GS_CS_SRGB_16F,
		GS_CS_709_EXTENDED,
	};

	const enum gs_color_space source_space =
		obs_source_get_color_space(target, OBS_COUNTOF(preferred_spaces), preferred_spaces);
	if (source_space == GS_CS_709_EXTENDED) {
		obs_source_skip_video_filter(filter->context);
	} else {
		const enum gs_color_format format = gs_get_format_from_space(source_space);
		if (obs_source_process_filter_begin_with_color_space(filter->context, format, source_space,
								     OBS_ALLOW_DIRECT_RENDERING)) {
			gs_effect_set_float(filter->key_hue_param, hue_from_color(filter->key_color));
			gs_effect_set_float(filter->similarity_param, filter->similarity);
			gs_effect_set_float(filter->desaturation_param, filter->desaturation);
			gs_effect_set_float(filter->darkness_param, filter->darkness);
			gs_effect_set_float(filter->radius_param, filter->radius);
			gs_effect_set_int(filter->sample_threshold_param, filter->sample_threshold);
			gs_effect_set_vec2(filter->pixel_size_param, &pixel_size);

			gs_blend_state_push();
			gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

			obs_source_process_filter_end(filter->context, filter->effect, 0, 0);

			gs_blend_state_pop();
		}
	}
}

static obs_properties_t *simple_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_color(props, SETTING_KEY_COLOR, TEXT_KEY_COLOR);
	obs_properties_add_float_slider(props, SETTING_SIMILARITY, TEXT_SIMILARITY, 0.0, 1.0, 0.0001);
	obs_properties_add_float_slider(props, SETTING_DESATURATION, TEXT_DESATURATION, 0.0, 1.0, 0.0001);
	obs_properties_add_float_slider(props, SETTING_DARKNESS, TEXT_DARKNESS, 0.0, 1.0, 0.0001);
	obs_properties_add_float_slider(props, SETTING_RADIUS, TEXT_RADIUS, 0.0, 20.0, 0.01);
	obs_properties_add_int_slider(props, SETTING_SAMPLE_THRESHOLD, TEXT_SAMPLE_THRESHOLD, 0, 8, 1);

	UNUSED_PARAMETER(data);
	return props;
}

static void simple_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, SETTING_KEY_COLOR, 0x00FF00);
	obs_data_set_default_double(settings, SETTING_SIMILARITY, .1);
	obs_data_set_default_double(settings, SETTING_DESATURATION, .1);
	obs_data_set_default_double(settings, SETTING_DARKNESS, .05);
	obs_data_set_default_double(settings, SETTING_RADIUS, 2);
	obs_data_set_default_double(settings, SETTING_SAMPLE_THRESHOLD, 2);
}

static enum gs_color_space simple_get_color_space(void *data, size_t count, const enum gs_color_space *preferred_spaces)
{
	UNUSED_PARAMETER(count);
	UNUSED_PARAMETER(preferred_spaces);

	const enum gs_color_space potential_spaces[] = {
		GS_CS_SRGB,
		GS_CS_SRGB_16F,
		GS_CS_709_EXTENDED,
	};

	struct color_key_filter_data_v2 *const filter = data;
	const enum gs_color_space source_space = obs_source_get_color_space(
		obs_filter_get_target(filter->context), OBS_COUNTOF(potential_spaces), potential_spaces);

	return source_space;
}

struct obs_source_info greenscreen_simple = {
	.id = "greenscreen_simple",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = simple_name,
	.create = simple_create,
	.destroy = simple_destroy,
	.video_render = simple_render,
	.update = simple_update,
	.get_properties = simple_properties,
	.get_defaults = simple_defaults,
	.video_get_color_space = simple_get_color_space,
};
