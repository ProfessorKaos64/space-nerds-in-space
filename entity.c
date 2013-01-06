/*
        Copyright (C) 2010 Stephen M. Cameron
	Author: Stephen M. Cameron

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
#include <stdio.h>
#include <math.h>
#include <gtk/gtk.h>

#include "mathutils.h"
#include "matrix.h"
#include "vertex.h"
#include "triangle.h"
#include "mesh.h"
#include "stl_parser.h"
#include "entity.h"

#include "my_point.h"
#include "snis_font.h"
#include "snis_typeface.h"

#include "snis_graph.h"

#define MAX_ENTITIES 5000

static unsigned long ntris, nents, nlines;

struct entity {
	struct mesh *m;
	float x, y, z; /* world coords */
};

struct camera_info {
	float x, y, z;		/* position of camera */
	float lx, ly, lz;	/* where camera is looking */
	float near, far, width, height;
} camera;

static int nentities = 0;
static struct entity entity_list[MAX_ENTITIES];

struct entity *add_entity(struct mesh *m, float x, float y, float z)
{
	if (nentities < MAX_ENTITIES) {
		entity_list[nentities].m = m;
		entity_list[nentities].x = x;
		entity_list[nentities].y = y;
		entity_list[nentities].z = z;
		nentities++;
		return &entity_list[nentities - 1];
	}
	return NULL;
}

void render_triangle(GtkWidget *w, GdkGC *gc, struct triangle *t)
{
	struct vertex *v1, *v2, *v3;
	int x1, y1, x2, y2, x3, y3;

	ntris++;
	nlines += 3;
	v1 = t->v[0];
	v2 = t->v[1];
	v3 = t->v[2];

	x1 = (int) (v1->wx * 400) + 400;
	x2 = (int) (v2->wx * 400) + 400;
	x3 = (int) (v3->wx * 400) + 400;
	y1 = (int) (v1->wy * 300) + 300;
	y2 = (int) (v2->wy * 300) + 300;
	y3 = (int) (v3->wy * 300) + 300;

	sng_current_draw_line(w->window, gc, x1, y1, x2, y2); 
	sng_current_draw_line(w->window, gc, x2, y2, x3, y3); 
	sng_current_draw_line(w->window, gc, x3, y3, x1, y1); 
}

void render_entity(GtkWidget *w, GdkGC *gc, struct entity *e)
{
	int i;

	for (i = 0; i < e->m->ntriangles; i++)
		render_triangle(w, gc, &e->m->t[i]);
	nents++;
}

static void transform_entity(struct entity *e, struct mat44 *transform)
{
	int i;
	struct mat41 *m1, *m2;

	/* Set homogeneous coord to 1 initially for all vertices */
	for (i = 0; i < e->m->nvertices; i++)
		e->m->v[i].w = 1.0;

	/* Do the transformation... */
	for (i = 0; i < e->m->nvertices; i++) {
		m1 = (struct mat41 *) &e->m->v[i].x;
		m2 = (struct mat41 *) &e->m->v[i].wx;
		mat41_x_mat44(m1, transform, m2);
		/* normalize... */
		m2->m[0] /= m2->m[3];
		m2->m[1] /= m2->m[3];
		m2->m[2] /= m2->m[3];
	}
}

void render_entities(GtkWidget *w, GdkGC *gc)
{
	int i;

	struct mat44 transform0 = {{{ 1, 0, 0, 0 }, /* identity matrix */
				    { 0, 1, 0, 0 },
				    { 0, 0, 1, 0 },
				    { 0, 0, 0, 1 }}};
	struct mat44 transform1, transform2;
	struct mat41 look_direction;

	struct mat41 up = { { 0, 1, 0, 0 } };
	struct mat41 camera_up, up_cross_look;
	struct mat41 *v; /* camera relative x axis (left/right) */ 
	struct mat41 *n; /* camera relative z axis (into view plane) */
	struct mat41 *u; /* camera relative y axis (up/down) */

	nents = 0;
	ntris = 0;
	nlines = 0;
	/* Translate to camera position... */
	mat44_translate(&transform0, -camera.x, -camera.y, -camera.z,
				&transform1);

	/* Calculate look direction, look direction, ... */
	look_direction.m[0] = (camera.lx - camera.x);
	look_direction.m[1] = (camera.lx - camera.x);
	look_direction.m[2] = (camera.lx - camera.x);
	look_direction.m[4] = 1.0;
	normalize_vector(&look_direction, &look_direction);
	n = &look_direction;

	/* Calculate up direction relative to camera, "camera_up" */
	mat41_cross_mat41(&up, &look_direction, &camera_up);
	normalize_vector(&camera_up, &camera_up);
	u = &camera_up;

	/* Calculate camera relative x axis */
	v = &up_cross_look;
	mat41_cross_mat41(n, u, v);
	/* v should not need normalizing as n and u already are and are perpendicular */

	/* Make a rotation matrix...
	   | ux uy uz 0 |
	   | vx vy vz 0 |
	   | nx ny nz 0 |
	   |  0  0  0 1 |
	 */
	transform0.m[0][0] = u->m[0];
	transform0.m[0][1] = v->m[0];
	transform0.m[0][2] = n->m[0];
	transform0.m[0][3] = 0.0;
	transform0.m[1][0] = u->m[1];
	transform0.m[1][1] = v->m[1];
	transform0.m[1][2] = n->m[1];
	transform0.m[1][3] = 0.0;
	transform0.m[2][0] = u->m[2];
	transform0.m[2][1] = v->m[2];
	transform0.m[2][2] = n->m[2];
	transform0.m[2][3] = 0.0;
	transform0.m[3][0] = 0.0;
	transform0.m[3][1] = 0.0;
	transform0.m[3][2] = 0.0; 
	transform0.m[3][3] = 1.0;

	/* append the rotations... */
	mat44_product(&transform1, &transform0, &transform2);

	/* Make perspective transform... */
	transform0.m[0][0] = (2 * camera.near) / camera.width;
	transform0.m[0][1] = 0.0;
	transform0.m[0][2] = 0.0;
	transform0.m[0][3] = 0.0;
	transform0.m[1][0] = 0.0;
	transform0.m[1][1] = (2.0 * camera.near) / camera.height;
	transform0.m[1][2] = 0.0;
	transform0.m[1][3] = 0.0;
	transform0.m[2][0] = 0.0;
	transform0.m[2][1] = 0.0;
	transform0.m[2][2] = -(camera.far + camera.near) / (camera.far - camera.near);
	transform0.m[2][3] = -1.0;
	transform0.m[3][0] = 0.0;
	transform0.m[3][1] = 0.0;
	transform0.m[3][2] = (-2 * camera.far * camera.near) / (camera.far - camera.near);
	transform0.m[3][3] = 0.0;

	/* append the perspective transform */
	mat44_product(&transform2, &transform0, &transform1);
	   
	for (i = 0; i < nentities; i++)
		transform_entity(&entity_list[i], &transform1);
	for (i = 0; i < nentities; i++)
		render_entity(w, gc, &entity_list[i]);
	printf("ntris = %lu, nlines = %lu, nents = %lu\n", ntris, nlines, nents);
}

void camera_set_pos(float x, float y, float z)
{
	camera.x = x;
	camera.y = y;
	camera.z = z;
}

void camera_look_at(float x, float y, float z)
{
	camera.lx = x;
	camera.ly = y;
	camera.lz = z;
}

void camera_set_parameters(float near, float far, float width, float height)
{
	camera.near = -near;
	camera.far = -far;
	camera.width = width;
	camera.height = height;	
}

