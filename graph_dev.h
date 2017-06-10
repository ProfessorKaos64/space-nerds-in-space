/*
	Copyright (C) 2013 Jeremy Van Grinsven
	Author: Jeremy Van Grinsven

	This file is part of Spacenerds In Space.

	Spacenerds in Space is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Spacenerds in Space is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Spacenerds in Space; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef INCLUDE_graph_device_H
#define INCLUDE_graph_device_H

struct mesh;
struct entity_context;
struct entity;
struct entity_transform;
union vec3;
struct mat44;
struct mat33;

extern int graph_dev_setup(const char *shader_dir);
extern void graph_dev_start_frame();
extern void graph_dev_end_frame();
extern void graph_dev_set_context(void *gdk_drawable, void *gdk_gc);
extern void graph_dev_set_color(void *gdk_color, float a);
extern void graph_dev_set_screen_size(int width, int height);
extern void graph_dev_set_extent_scale(float x_scale, float y_scale);
extern void graph_dev_set_3d_viewport(int x_offset, int y_offset, int width, int height);
extern void graph_dev_clear_depth_bit();

#define GRAPH_DEV_RENDER_FAR_TO_NEAR 0
#define GRAPH_DEV_RENDER_NEAR_TO_FAR 1
extern int graph_dev_entity_render_order(struct entity_context *cx, struct entity *e);

extern void graph_dev_draw_entity(struct entity_context *cx, struct entity *e, union vec3 *eye_light_pos,
	const struct entity_transform *transform);
extern void graph_dev_draw_3d_line(struct entity_context *cx, const struct mat44 *mat_vp, const struct mat44 *mat_v,
	float x1, float y1, float z1, float x2, float y2, float z2);

extern void graph_dev_draw_line(float x1, float y1, float x2, float y2);
extern void graph_dev_draw_rectangle(int filled, float x, float y, float width, float height);
extern void graph_dev_draw_point(float x, float y);
extern void graph_dev_draw_arc(int filled, float x, float y, float width, float height,
	float angle1, float angle2);

extern int graph_dev_load_skybox_texture(
	const char *texture_filename_pos_x,
	const char *texture_filename_neg_x,
	const char *texture_filename_pos_y,
	const char *texture_filename_neg_y,
	const char *texture_filename_pos_z,
	const char *texture_filename_neg_z);

extern unsigned int graph_dev_load_cubemap_texture(
	int is_inside,
        const char *texture_filename_pos_x,
        const char *texture_filename_neg_x,
        const char *texture_filename_pos_y,
        const char *texture_filename_neg_y,
        const char *texture_filename_pos_z,
        const char *texture_filename_neg_z);

extern unsigned int graph_dev_load_texture(const char *filename);
extern unsigned int graph_dev_load_texture_no_mipmaps(const char *filename);
extern const char *graph_dev_get_texture_filename(unsigned int);
extern void graph_dev_draw_skybox(struct entity_context *cx, const struct mat44 *mat_vp);
extern int graph_dev_reload_changed_textures();
extern int graph_dev_reload_changed_cubemap_textures();
extern void graph_dev_expire_all_textures();
extern void graph_dev_expire_texture(char *filename);
extern void graph_dev_expire_cubemap_texture(int is_inside,
	const char *texture_filename_pos_x,
	const char *texture_filename_neg_x,
	const char *texture_filename_pos_y,
	const char *texture_filename_neg_y,
	const char *texture_filename_pos_z,
	const char *texture_filename_neg_z);

extern void graph_dev_display_debug_menu_show();
extern int graph_dev_graph_dev_debug_menu_click(int x, int y);

extern void graph_dev_setup_colors(void *gtk_widget, void *gdk_color_huex, int nhuex);

/* graph_dev_grab_framebuffer only implemented for opengl backend */
extern void graph_dev_grab_framebuffer(unsigned char **buffer, int *width, int *height);

#endif

