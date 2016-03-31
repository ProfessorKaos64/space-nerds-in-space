#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <GL/glew.h>
#include <math.h>
#include <assert.h>
#include <sys/stat.h>

#include "shader.h"
#include "vertex.h"
#include "triangle.h"
#include "mtwist.h"
#include "mathutils.h"
#include "matrix.h"
#include "quat.h"
#include "mesh.h"
#include "vec4.h"
#include "snis_graph.h"
#include "graph_dev.h"
#include "material.h"
#include "entity.h"
#include "entity_private.h"
#include "snis_typeface.h"
#include "opengl_cap.h"

#define OPENGL_VERSION_STRING "#version 120\n"
#define AMBIENT_LIGHT_DEFINE "#define AMBIENT 0.05\n"
#define UNIVERSAL_SHADER_HEADER \
	OPENGL_VERSION_STRING \
	AMBIENT_LIGHT_DEFINE

#define TEX_RELOAD_DELAY 1.0
#define CUBEMAP_TEX_RELOAD_DELAY 10.0
#define MAX_LOADED_TEXTURES 40
struct loaded_texture {
	GLuint texture_id;
	char *filename;
	time_t mtime;
	double last_mtime_change;
};
static int nloaded_textures = 0;
static struct loaded_texture loaded_textures[MAX_LOADED_TEXTURES];

#define NCUBEMAP_TEXTURES 6
#define MAX_LOADED_CUBEMAP_TEXTURES 40
struct loaded_cubemap_texture {
	GLuint texture_id;
	int is_inside;
	char *filename[NCUBEMAP_TEXTURES];
	time_t mtime;
	double last_mtime_change;
};
static int nloaded_cubemap_textures = 0;
static struct loaded_cubemap_texture loaded_cubemap_textures[MAX_LOADED_CUBEMAP_TEXTURES];

static int draw_normal_lines = 0;
static int draw_billboard_wireframe = 0;
static int draw_polygon_as_lines = 0;
static int draw_msaa_samples = 0;
static int draw_render_to_texture = 0;
static int draw_smaa = 0;
static int draw_smaa_edge = 0;
static int draw_smaa_blend = 0;
static int draw_atmospheres = 1;
static const char *default_shader_directory = "share/snis/shader";
static char *shader_directory = NULL;

struct mesh_gl_info {
	/* common buffer to hold vertex positions */
	GLuint vertex_buffer;

	int ntriangles;
	/* uses vertex_buffer for data */
	GLuint triangle_vertex_buffer;

	GLuint triangle_normal_lines_buffer;
	GLuint triangle_tangent_lines_buffer;
	GLuint triangle_bitangent_lines_buffer;

	int nwireframe_lines;
	GLuint wireframe_lines_vertex_buffer;

	int npoints;
	/* uses vertex_buffer for data */

	int nlines;
	/* uses vertex_buffer for data */
	GLuint line_vertex_buffer;

	int nparticles;
	GLuint particle_vertex_buffer;
	GLuint particle_index_buffer;
};

struct vertex_buffer_data {
	union vec3 position;
};

struct vertex_triangle_buffer_data {
	union vec3 normal;
	union vec3 tvertex0;
	union vec3 tvertex1;
	union vec3 tvertex2;
	union vec3 wireframe_edge_mask;
	union vec2 texture_coord;
	union vec3 tangent;
	union vec3 bitangent;
};

struct vertex_wireframe_line_buffer_data {
	union vec3 position;
	union vec3 normal;
};

struct vertex_line_buffer_data {
	GLubyte multi_one[4];
	union vec3 line_vertex0;
	union vec3 line_vertex1;
};

struct vertex_color_buffer_data {
	GLfloat position[2];
	GLubyte color[4];
};

struct vertex_particle_buffer_data {
	GLubyte multi_one[4];
	union vec3 start_position;
	GLubyte start_tint_color[3];
	GLubyte start_apm[2];
	union vec3 end_position;
	GLubyte end_tint_color[3];
	GLubyte end_apm[2];
};

void mesh_graph_dev_cleanup(struct mesh *m)
{
	if (m->graph_ptr) {
		struct mesh_gl_info *ptr = m->graph_ptr;

		glDeleteBuffers(1, &ptr->vertex_buffer);
		glDeleteBuffers(1, &ptr->triangle_vertex_buffer);
		glDeleteBuffers(1, &ptr->wireframe_lines_vertex_buffer);
		glDeleteBuffers(1, &ptr->triangle_normal_lines_buffer);
		glDeleteBuffers(1, &ptr->triangle_tangent_lines_buffer);
		glDeleteBuffers(1, &ptr->triangle_bitangent_lines_buffer);
		glDeleteBuffers(1, &ptr->line_vertex_buffer);
		glDeleteBuffers(1, &ptr->particle_vertex_buffer);
		glDeleteBuffers(1, &ptr->particle_index_buffer);

		free(ptr);
		m->graph_ptr = 0;
	}
}

/* load/reload an array buffer using stream draw if it is being overwritten */
#define LOAD_BUFFER(buffer_type, buffer_id, buffer_size, buffer_data) \
	do { \
		GLenum usage; \
		if ((buffer_id) == 0) { \
			usage = GL_STATIC_DRAW; \
			glGenBuffers(1, &(buffer_id)); \
		} else \
			usage = GL_STREAM_DRAW; \
		glBindBuffer((buffer_type), (buffer_id)); \
		glBufferData((buffer_type), (buffer_size), (buffer_data), usage); \
	} while (0)

void mesh_graph_dev_init(struct mesh *m)
{
	struct mesh_gl_info *ptr = m->graph_ptr;
	if (!ptr) {
		ptr = malloc(sizeof(struct mesh_gl_info));
		memset(ptr, 0, sizeof(*ptr));
		m->graph_ptr = ptr;
	}

	if (m->geometry_mode == MESH_GEOMETRY_TRIANGLES) {
		/* setup the triangle mesh buffers */
		int i;
		size_t v_size = sizeof(struct vertex_buffer_data) * m->ntriangles * 3;
		size_t vt_size = sizeof(struct vertex_triangle_buffer_data) * m->ntriangles * 3;
		struct vertex_buffer_data *g_v_buffer_data = malloc(v_size);
		struct vertex_triangle_buffer_data *g_vt_buffer_data = malloc(vt_size);

		float normal_line_length = m->radius / 20.0;
		size_t nl_size = sizeof(struct vertex_buffer_data) * m->ntriangles * 3 * 2;
		struct vertex_buffer_data *g_nl_buffer_data = malloc(nl_size * 3);
		memset(g_nl_buffer_data, 0, nl_size * 3);
		struct vertex_buffer_data *g_tl_buffer_data = &g_nl_buffer_data[m->ntriangles * 3 * 2];
		struct vertex_buffer_data *g_bl_buffer_data = &g_nl_buffer_data[m->ntriangles * 3 * 2 * 2];

		ptr->ntriangles = m->ntriangles;
		ptr->npoints = m->ntriangles * 3; /* can be rendered as a point cloud too */

		for (i = 0; i < m->ntriangles; i++) {
			int j = 0;
			for (j = 0; j < 3; j++) {
				int v_index = i * 3 + j;
				g_v_buffer_data[v_index].position.v.x = m->t[i].v[j]->x;
				g_v_buffer_data[v_index].position.v.y = m->t[i].v[j]->y;
				g_v_buffer_data[v_index].position.v.z = m->t[i].v[j]->z;

				g_vt_buffer_data[v_index].normal.v.x = m->t[i].vnormal[j].x;
				g_vt_buffer_data[v_index].normal.v.y = m->t[i].vnormal[j].y;
				g_vt_buffer_data[v_index].normal.v.z = m->t[i].vnormal[j].z;

				g_vt_buffer_data[v_index].tvertex0.v.x = m->t[i].v[0]->x;
				g_vt_buffer_data[v_index].tvertex0.v.y = m->t[i].v[0]->y;
				g_vt_buffer_data[v_index].tvertex0.v.z = m->t[i].v[0]->z;

				g_vt_buffer_data[v_index].tvertex1.v.x = m->t[i].v[1]->x;
				g_vt_buffer_data[v_index].tvertex1.v.y = m->t[i].v[1]->y;
				g_vt_buffer_data[v_index].tvertex1.v.z = m->t[i].v[1]->z;

				g_vt_buffer_data[v_index].tvertex2.v.x = m->t[i].v[2]->x;
				g_vt_buffer_data[v_index].tvertex2.v.y = m->t[i].v[2]->y;
				g_vt_buffer_data[v_index].tvertex2.v.z = m->t[i].v[2]->z;

				g_vt_buffer_data[v_index].tangent.v.x = m->t[i].vtangent[j].x;
				g_vt_buffer_data[v_index].tangent.v.y = m->t[i].vtangent[j].y;
				g_vt_buffer_data[v_index].tangent.v.z = m->t[i].vtangent[j].z;

				g_vt_buffer_data[v_index].bitangent.v.x = m->t[i].vbitangent[j].x;
				g_vt_buffer_data[v_index].bitangent.v.y = m->t[i].vbitangent[j].y;
				g_vt_buffer_data[v_index].bitangent.v.z = m->t[i].vbitangent[j].z;

				/* bias the edge distance to make the coplanar edges not draw */
				if ((j == 1 || j == 2) && (m->t[i].flag & TRIANGLE_1_2_COPLANAR))
					g_vt_buffer_data[v_index].wireframe_edge_mask.v.x = 1000;
				else
					g_vt_buffer_data[v_index].wireframe_edge_mask.v.x = 0;

				if ((j == 0 || j == 2) && (m->t[i].flag & TRIANGLE_0_2_COPLANAR))
					g_vt_buffer_data[v_index].wireframe_edge_mask.v.y = 1000;
				else
					g_vt_buffer_data[v_index].wireframe_edge_mask.v.y = 0;

				if ((j == 0 || j == 1) && (m->t[i].flag & TRIANGLE_0_1_COPLANAR))
					g_vt_buffer_data[v_index].wireframe_edge_mask.v.z = 1000;
				else
					g_vt_buffer_data[v_index].wireframe_edge_mask.v.z = 0;

				if (m->tex) {
					g_vt_buffer_data[v_index].texture_coord.v.x = m->tex[v_index].u;
					g_vt_buffer_data[v_index].texture_coord.v.y = m->tex[v_index].v;
				} else {
					g_vt_buffer_data[v_index].texture_coord.v.x = 0;
					g_vt_buffer_data[v_index].texture_coord.v.y = 0;
				}

				/* draw a line for each vertex normal, tangent, and bitangent */
				int nl_index = i * 6 + j * 2;

				/* normal */
				g_nl_buffer_data[nl_index].position.v.x = m->t[i].v[j]->x;
				g_nl_buffer_data[nl_index].position.v.y = m->t[i].v[j]->y;
				g_nl_buffer_data[nl_index].position.v.z = m->t[i].v[j]->z;

				g_nl_buffer_data[nl_index + 1].position.v.x =
					m->t[i].v[j]->x + normal_line_length * m->t[i].vnormal[j].x;
				g_nl_buffer_data[nl_index + 1].position.v.y =
					m->t[i].v[j]->y + normal_line_length * m->t[i].vnormal[j].y;
				g_nl_buffer_data[nl_index + 1].position.v.z =
					m->t[i].v[j]->z + normal_line_length * m->t[i].vnormal[j].z;

				/* tangent */
				g_tl_buffer_data[nl_index].position.v.x = m->t[i].v[j]->x;
				g_tl_buffer_data[nl_index].position.v.y = m->t[i].v[j]->y;
				g_tl_buffer_data[nl_index].position.v.z = m->t[i].v[j]->z;
				g_tl_buffer_data[nl_index + 1].position.v.x =
					m->t[i].v[j]->x + normal_line_length * m->t[i].vtangent[j].x;
				g_tl_buffer_data[nl_index + 1].position.v.y =
					m->t[i].v[j]->y + normal_line_length * m->t[i].vtangent[j].y;
				g_tl_buffer_data[nl_index + 1].position.v.z =
					m->t[i].v[j]->z + normal_line_length * m->t[i].vtangent[j].z;

				/* bitangent */
				g_bl_buffer_data[nl_index].position.v.x = m->t[i].v[j]->x;
				g_bl_buffer_data[nl_index].position.v.y = m->t[i].v[j]->y;
				g_bl_buffer_data[nl_index].position.v.z = m->t[i].v[j]->z;
				g_bl_buffer_data[nl_index + 1].position.v.x =
					m->t[i].v[j]->x + normal_line_length * m->t[i].vbitangent[j].x;
				g_bl_buffer_data[nl_index + 1].position.v.y =
					m->t[i].v[j]->y + normal_line_length * m->t[i].vbitangent[j].y;
				g_bl_buffer_data[nl_index + 1].position.v.z =
					m->t[i].v[j]->z + normal_line_length * m->t[i].vbitangent[j].z;
			}
		}

		LOAD_BUFFER(GL_ARRAY_BUFFER, ptr->vertex_buffer, v_size, g_v_buffer_data);
		LOAD_BUFFER(GL_ARRAY_BUFFER, ptr->triangle_vertex_buffer, vt_size, g_vt_buffer_data);
		LOAD_BUFFER(GL_ARRAY_BUFFER, ptr->triangle_normal_lines_buffer, nl_size, g_nl_buffer_data);
		LOAD_BUFFER(GL_ARRAY_BUFFER, ptr->triangle_tangent_lines_buffer, nl_size, g_tl_buffer_data);
		LOAD_BUFFER(GL_ARRAY_BUFFER, ptr->triangle_bitangent_lines_buffer, nl_size, g_bl_buffer_data);

		free(g_v_buffer_data);
		free(g_vt_buffer_data);
		free(g_nl_buffer_data);

		/* setup the line buffers used for wireframe */
		size_t wfl_size = sizeof(struct vertex_wireframe_line_buffer_data) * m->ntriangles * 3 * 2;
		struct vertex_wireframe_line_buffer_data *g_wfl_buffer_data = malloc(wfl_size);

		/* map the edge combinatinos to the triangle coplanar flag */
		static const int tri_coplaner_flags[3][3] = {
			{0, TRIANGLE_0_1_COPLANAR, TRIANGLE_0_2_COPLANAR},
			{TRIANGLE_0_1_COPLANAR, 0, TRIANGLE_1_2_COPLANAR},
			{TRIANGLE_0_2_COPLANAR, TRIANGLE_1_2_COPLANAR, 0} };

		ptr->nwireframe_lines = 0;

		for (i = 0; i < m->ntriangles; i++) {
			int j0 = 0;
			for (j0 = 0; j0 < 3; j0++) {
				int j1 = (j0 + 1) % 3;

				if (!(m->t[i].flag & tri_coplaner_flags[j0][j1])) {
					int index = 2 * ptr->nwireframe_lines;

					/* add the line from vertex j0 to j1 */
					g_wfl_buffer_data[index].position.v.x = m->t[i].v[j0]->x;
					g_wfl_buffer_data[index].position.v.y = m->t[i].v[j0]->y;
					g_wfl_buffer_data[index].position.v.z = m->t[i].v[j0]->z;

					g_wfl_buffer_data[index + 1].position.v.x = m->t[i].v[j1]->x;
					g_wfl_buffer_data[index + 1].position.v.y = m->t[i].v[j1]->y;
					g_wfl_buffer_data[index + 1].position.v.z = m->t[i].v[j1]->z;

					/* the line normal is the same as the triangle */
					g_wfl_buffer_data[index].normal.v.x = m->t[i].n.x;
					g_wfl_buffer_data[index].normal.v.y = m->t[i].n.y;
					g_wfl_buffer_data[index].normal.v.z = m->t[i].n.z;
					g_wfl_buffer_data[index + 1].normal.v.x = m->t[i].n.x;
					g_wfl_buffer_data[index + 1].normal.v.y = m->t[i].n.y;
					g_wfl_buffer_data[index + 1].normal.v.z = m->t[i].n.z;

					ptr->nwireframe_lines++;
				}
			}
		}

		LOAD_BUFFER(GL_ARRAY_BUFFER, ptr->wireframe_lines_vertex_buffer,
			sizeof(struct vertex_wireframe_line_buffer_data) * ptr->nwireframe_lines * 2,
			g_wfl_buffer_data);

		free(g_wfl_buffer_data);
	}

	if (m->geometry_mode == MESH_GEOMETRY_LINES || m->geometry_mode == MESH_GEOMETRY_PARTICLE_ANIMATION) {
		/* setup the line buffers */
		int i;
		size_t v_size = sizeof(struct vertex_buffer_data) * m->nvertices * 2;
		struct vertex_buffer_data *g_v_buffer_data = malloc(v_size);

		size_t vl_size = sizeof(struct vertex_line_buffer_data) * m->nvertices * 2;
		struct vertex_line_buffer_data *g_vl_buffer_data = malloc(vl_size);

		ptr->nlines = 0;

		for (i = 0; i < m->nlines; i++) {
			struct vertex *vstart = m->l[i].start;
			struct vertex *vend = m->l[i].end;

			if (m->l[i].flag & MESH_LINE_STRIP) {
				struct vertex *vcurr = vstart;
				struct vertex *v1;

				while (vcurr <= vend) {
					struct vertex *v2 = vcurr;

					if (v2 != vstart) {
						int index = ptr->nlines * 2;
						g_v_buffer_data[index].position.v.x = v1->x;
						g_v_buffer_data[index].position.v.y = v1->y;
						g_v_buffer_data[index].position.v.z = v1->z;

						g_v_buffer_data[index + 1].position.v.x = v2->x;
						g_v_buffer_data[index + 1].position.v.y = v2->y;
						g_v_buffer_data[index + 1].position.v.z = v2->z;

						g_vl_buffer_data[index].multi_one[0] =
							g_vl_buffer_data[index + 1].multi_one[0] = 0; /* is dotted */

						g_vl_buffer_data[index].line_vertex0.v.x =
							g_vl_buffer_data[index + 1].line_vertex0.v.x = v1->x;
						g_vl_buffer_data[index].line_vertex0.v.y =
							g_vl_buffer_data[index + 1].line_vertex0.v.y = v1->y;
						g_vl_buffer_data[index].line_vertex0.v.z =
							g_vl_buffer_data[index + 1].line_vertex0.v.z = v1->z;

						g_vl_buffer_data[index].line_vertex1.v.x =
							g_vl_buffer_data[index + 1].line_vertex1.v.x = v2->x;
						g_vl_buffer_data[index].line_vertex1.v.y =
							g_vl_buffer_data[index + 1].line_vertex1.v.y = v2->y;
						g_vl_buffer_data[index].line_vertex1.v.z =
							g_vl_buffer_data[index + 1].line_vertex1.v.z = v2->z;

						ptr->nlines++;
					}
					v1 = v2;
					++vcurr;
				}
			} else {
				int is_dotted = m->l[i].flag & MESH_LINE_DOTTED;

				int index = ptr->nlines * 2;
				g_v_buffer_data[index].position.v.x = vstart->x;
				g_v_buffer_data[index].position.v.y = vstart->y;
				g_v_buffer_data[index].position.v.z = vstart->z;

				g_v_buffer_data[index + 1].position.v.x = vend->x;
				g_v_buffer_data[index + 1].position.v.y = vend->y;
				g_v_buffer_data[index + 1].position.v.z = vend->z;

				g_vl_buffer_data[index].multi_one[0] =
					g_vl_buffer_data[index + 1].multi_one[0] = is_dotted ? 255 : 0;

				g_vl_buffer_data[index].line_vertex0.v.x =
					g_vl_buffer_data[index + 1].line_vertex0.v.x = vstart->x;
				g_vl_buffer_data[index].line_vertex0.v.y =
					g_vl_buffer_data[index + 1].line_vertex0.v.y = vstart->y;
				g_vl_buffer_data[index].line_vertex0.v.z =
					g_vl_buffer_data[index + 1].line_vertex0.v.z = vstart->z;

				g_vl_buffer_data[index].line_vertex1.v.x =
					g_vl_buffer_data[index + 1].line_vertex1.v.x = vend->x;
				g_vl_buffer_data[index].line_vertex1.v.y =
					g_vl_buffer_data[index + 1].line_vertex1.v.y = vend->y;
				g_vl_buffer_data[index].line_vertex1.v.z =
					g_vl_buffer_data[index + 1].line_vertex1.v.z = vend->z;

				ptr->nlines++;
			}
		}

		LOAD_BUFFER(GL_ARRAY_BUFFER, ptr->vertex_buffer, sizeof(struct vertex_buffer_data) * 2 * ptr->nlines,
			g_v_buffer_data);
		LOAD_BUFFER(GL_ARRAY_BUFFER, ptr->line_vertex_buffer,
			sizeof(struct vertex_line_buffer_data) * 2 * ptr->nlines, g_vl_buffer_data);

		ptr->npoints = ptr->nlines * 2; /* can be rendered as a point cloud too */

		free(g_v_buffer_data);
		free(g_vl_buffer_data);
	}

	if (m->geometry_mode == MESH_GEOMETRY_POINTS) {
		/* setup the point buffers */
		size_t v_size = sizeof(struct vertex_buffer_data) * m->nvertices;
		struct vertex_buffer_data *g_v_buffer_data = malloc(v_size);

		ptr->npoints = m->nvertices;

		int i;
		for (i = 0; i < m->nvertices; i++) {
			g_v_buffer_data[i].position.v.x = m->v[i].x;
			g_v_buffer_data[i].position.v.y = m->v[i].y;
			g_v_buffer_data[i].position.v.z = m->v[i].z;
		}

		LOAD_BUFFER(GL_ARRAY_BUFFER, ptr->vertex_buffer, v_size, g_v_buffer_data);

		free(g_v_buffer_data);
	}

	if (m->geometry_mode == MESH_GEOMETRY_PARTICLE_ANIMATION) {
		ptr->nparticles = m->nvertices / 2;

		size_t v_size = sizeof(struct vertex_particle_buffer_data) * ptr->nparticles * 4;
		struct vertex_particle_buffer_data *g_v_buffer_data = malloc(v_size);

		size_t i_size = sizeof(GLushort) * ptr->nparticles * 6;
		GLushort *g_i_buffer_data = malloc(i_size);

		/* two triangles from four vertices
		   V3 (0,1) +---+ V2 (1,1)
			    +\  +
			    + \ +
			    +  \+
		   V0 (0,0) +---+ V1 (1,0)
		*/
		int i;
		for (i = 0; i < m->nvertices; i += 2) {
			int v_index = i * 2;

			GLubyte tint_red = (int)(m->l[i / 2].tint_color.red * 255) & 255;
			GLubyte tint_green = (int)(m->l[i / 2].tint_color.green * 255) & 255;
			GLubyte tint_blue = (int)(m->l[i / 2].tint_color.blue * 255) & 255;
			GLubyte additivity = (int)(m->l[i / 2].additivity * 255) & 255;
			GLubyte opacity = (int)(m->l[i / 2].opacity * 255) & 255;
			GLubyte time_offset = (int)(m->l[i / 2].time_offset * 255) & 255;

			/* texture coord is different for all four vertices */
			g_v_buffer_data[v_index + 0].multi_one[0] = 0;
			g_v_buffer_data[v_index + 0].multi_one[1] = 0;

			g_v_buffer_data[v_index + 1].multi_one[0] = 255;
			g_v_buffer_data[v_index + 1].multi_one[1] = 0;

			g_v_buffer_data[v_index + 2].multi_one[0] = 255;
			g_v_buffer_data[v_index + 2].multi_one[1] = 255;

			g_v_buffer_data[v_index + 3].multi_one[0] = 0;
			g_v_buffer_data[v_index + 3].multi_one[1] = 255;

			g_v_buffer_data[v_index + 0].multi_one[2] =
				g_v_buffer_data[v_index + 1].multi_one[2] =
				g_v_buffer_data[v_index + 2].multi_one[2] =
				g_v_buffer_data[v_index + 3].multi_one[2] = time_offset;

			/* the rest of the attributes are the same for all four */
			g_v_buffer_data[v_index + 0].start_position.v.x =
				g_v_buffer_data[v_index + 1].start_position.v.x =
				g_v_buffer_data[v_index + 2].start_position.v.x =
				g_v_buffer_data[v_index + 3].start_position.v.x = m->v[i].x;
			g_v_buffer_data[v_index + 0].start_position.v.y =
				g_v_buffer_data[v_index + 1].start_position.v.y =
				g_v_buffer_data[v_index + 2].start_position.v.y =
				g_v_buffer_data[v_index + 3].start_position.v.y = m->v[i].y;
			g_v_buffer_data[v_index + 0].start_position.v.z =
				g_v_buffer_data[v_index + 1].start_position.v.z =
				g_v_buffer_data[v_index + 2].start_position.v.z =
				g_v_buffer_data[v_index + 3].start_position.v.z = m->v[i].z;

			g_v_buffer_data[v_index + 0].start_tint_color[0] =
				g_v_buffer_data[v_index + 1].start_tint_color[0] =
				g_v_buffer_data[v_index + 2].start_tint_color[0] =
				g_v_buffer_data[v_index + 3].start_tint_color[0] = tint_red;
			g_v_buffer_data[v_index + 0].start_tint_color[1] =
				g_v_buffer_data[v_index + 1].start_tint_color[1] =
				g_v_buffer_data[v_index + 2].start_tint_color[1] =
				g_v_buffer_data[v_index + 3].start_tint_color[1] = tint_green;
			g_v_buffer_data[v_index + 0].start_tint_color[2] =
				g_v_buffer_data[v_index + 1].start_tint_color[2] =
				g_v_buffer_data[v_index + 2].start_tint_color[2] =
				g_v_buffer_data[v_index + 3].start_tint_color[2] = tint_blue;

			g_v_buffer_data[v_index + 0].start_apm[0] =
				g_v_buffer_data[v_index + 1].start_apm[0] =
				g_v_buffer_data[v_index + 2].start_apm[0] =
				g_v_buffer_data[v_index + 3].start_apm[0] = additivity;
			g_v_buffer_data[v_index + 0].start_apm[1] =
				g_v_buffer_data[v_index + 1].start_apm[1] =
				g_v_buffer_data[v_index + 2].start_apm[1] =
				g_v_buffer_data[v_index + 3].start_apm[1] = opacity;

			g_v_buffer_data[v_index + 0].end_position.v.x =
				g_v_buffer_data[v_index + 1].end_position.v.x =
				g_v_buffer_data[v_index + 2].end_position.v.x =
				g_v_buffer_data[v_index + 3].end_position.v.x = m->v[i + 1].x;
			g_v_buffer_data[v_index + 0].end_position.v.y =
				g_v_buffer_data[v_index + 1].end_position.v.y =
				g_v_buffer_data[v_index + 2].end_position.v.y =
				g_v_buffer_data[v_index + 3].end_position.v.y = m->v[i + 1].y;
			g_v_buffer_data[v_index + 0].end_position.v.z =
				g_v_buffer_data[v_index + 1].end_position.v.z =
				g_v_buffer_data[v_index + 2].end_position.v.z =
				g_v_buffer_data[v_index + 3].end_position.v.z = m->v[i + 1].z;

			g_v_buffer_data[v_index + 0].end_tint_color[0] =
				g_v_buffer_data[v_index + 1].end_tint_color[0] =
				g_v_buffer_data[v_index + 2].end_tint_color[0] =
				g_v_buffer_data[v_index + 3].end_tint_color[0] = tint_red;
			g_v_buffer_data[v_index + 0].end_tint_color[1] =
				g_v_buffer_data[v_index + 1].end_tint_color[1] =
				g_v_buffer_data[v_index + 2].end_tint_color[1] =
				g_v_buffer_data[v_index + 3].end_tint_color[1] = tint_green;
			g_v_buffer_data[v_index + 0].end_tint_color[2] =
				g_v_buffer_data[v_index + 1].end_tint_color[2] =
				g_v_buffer_data[v_index + 2].end_tint_color[2] =
				g_v_buffer_data[v_index + 3].end_tint_color[2] = tint_blue;

			g_v_buffer_data[v_index + 0].end_apm[0] =
				g_v_buffer_data[v_index + 1].end_apm[0] =
				g_v_buffer_data[v_index + 2].end_apm[0] =
				g_v_buffer_data[v_index + 3].end_apm[0] = additivity;
			g_v_buffer_data[v_index + 0].end_apm[1] =
				g_v_buffer_data[v_index + 1].end_apm[1] =
				g_v_buffer_data[v_index + 2].end_apm[1] =
				g_v_buffer_data[v_index + 3].end_apm[1] = opacity;

			/* setup six indices for our two triangles */
			int i_index = i * 3;
			g_i_buffer_data[i_index + 0] = v_index + 0;
			g_i_buffer_data[i_index + 1] = v_index + 1;
			g_i_buffer_data[i_index + 2] = v_index + 3;
			g_i_buffer_data[i_index + 3] = v_index + 1;
			g_i_buffer_data[i_index + 4] = v_index + 2;
			g_i_buffer_data[i_index + 5] = v_index + 3;
		}

		LOAD_BUFFER(GL_ARRAY_BUFFER, ptr->particle_vertex_buffer, v_size, g_v_buffer_data);
		LOAD_BUFFER(GL_ELEMENT_ARRAY_BUFFER, ptr->particle_index_buffer, i_size, g_i_buffer_data);

		free(g_v_buffer_data);
		free(g_i_buffer_data);
	}
}

struct graph_dev_gl_vertex_color_shader {
	GLuint program_id;
	GLint mvp_matrix_id;
	GLint vertex_position_id;
	GLint vertex_color_id;
};

struct graph_dev_gl_single_color_lit_shader {
	GLuint program_id;
	GLint mvp_matrix_id;
	GLint mv_matrix_id;
	GLint normal_matrix_id;
	GLint vertex_position_id;
	GLint vertex_normal_id;
	GLint light_pos_id;
	GLint color_id;
};

struct graph_dev_gl_atmosphere_shader {
	GLuint program_id;
	GLint mvp_matrix_id;
	GLint mv_matrix_id;
	GLint normal_matrix_id;
	GLint vertex_position_id;
	GLint vertex_normal_id;
	GLint light_pos_id;
	GLint color_id;
};

struct graph_dev_gl_filled_wireframe_shader {
	GLuint program_id;
	GLint viewport_id;
	GLint mvp_matrix_id;
	GLint position_id;
	GLint tvertex0_id;
	GLint tvertex1_id;
	GLint tvertex2_id;
	GLint edge_mask_id;
	GLint line_color_id;
	GLint triangle_color_id;
};

struct graph_dev_gl_trans_wireframe_shader {
	GLuint program_id;
	GLint mvp_matrix_id;
	GLint mv_matrix_id;
	GLint normal_matrix_id;
	GLint vertex_position_id;
	GLint vertex_normal_id;
	GLint color_id;

	GLint clip_sphere_id;
	GLint clip_sphere_radius_fade_id;
};

struct graph_dev_gl_single_color_shader {
	GLuint program_id;
	GLint mvp_matrix_id;
	GLint vertex_position_id;
	GLint color_id;
};

struct graph_dev_gl_line_single_color_shader {
	GLuint program_id;
	GLint mvp_matrix_id;
	GLint viewport_id;
	GLint multi_one_id;
	GLint vertex_position_id;
	GLint line_vertex0_id;
	GLint line_vertex1_id;
	GLint dot_size_id;
	GLint dot_pitch_id;
	GLint line_color_id;
};

struct graph_dev_gl_point_cloud_shader {
	GLuint program_id;
	GLint mvp_matrix_id;
	GLint vertex_position_id;
	GLint point_size_id;
	GLint color_id;
	GLint time_id;
};

struct graph_dev_gl_skybox_shader {
	GLuint program_id;
	GLint mvp_id;
	GLint vertex_id;
	GLint texture_id;

	int texture_loaded;
	GLuint cube_texture_id;
};

struct graph_dev_gl_color_by_w_shader {
	GLuint program_id;
	GLint mvp_id;
	GLint position_id;
	GLint near_color_id;
	GLint near_w_id;
	GLint center_color_id;
	GLint center_w_id;
	GLint far_color_id;
	GLint far_w_id;
};

struct graph_dev_gl_textured_shader {
	GLuint program_id;
	GLint mvp_matrix_id;
	GLint mv_matrix_id;
	GLint normal_matrix_id;
	GLint vertex_position_id;
	GLint vertex_normal_id;
	GLint vertex_tangent_id;
	GLint vertex_bitangent_id;
	GLint tint_color_id;
	GLint texture_coord_id;
	GLint texture_2d_id;
	GLint emit_texture_2d_id;
	GLint texture_cubemap_id;
	GLint normalmap_cubemap_id;
	GLint light_pos_id;

	GLint shadow_sphere_id;

	GLint shadow_annulus_texture_id;
	GLint shadow_annulus_center_id;
	GLint shadow_annulus_normal_id;
	GLint shadow_annulus_radius_id;
	GLint shadow_annulus_tint_color_id;

	GLint ring_texture_v_id;
	GLint ring_inner_radius_id;
	GLint ring_outer_radius_id;
};

struct clip_sphere_data {
	union vec3 eye_pos;
	float r;
	float radius_fade;
};

struct shadow_sphere_data {
	union vec3 eye_pos;
	float r;
};

struct shadow_annulus_data {
	GLuint texture_id;
	union vec3 eye_pos;
	float r1, r2;
	struct sng_color tint_color;
	float alpha;
};

struct graph_dev_gl_textured_particle_shader {
	GLuint program_id;
	GLint mvp_matrix_id;
	GLint camera_up_vec_id;
	GLint camera_right_vec_id;
	GLint time_id;
	GLint radius_id;
	GLint multi_one_id;
	GLint start_position_id;
	GLint start_tint_color_id;
	GLint start_apm_id;
	GLint end_position_id;
	GLint end_tint_color_id;
	GLint end_apm_id;
	GLint texture_id; /* param to vertex shader */
};

struct graph_dev_gl_fs_effect_shader {
	GLuint program_id;
	GLint mvp_matrix_id;
	GLint vertex_position_id;
	GLint texture_coord_id;
	GLint tint_color_id;
	GLint viewport_id;
	GLint texture0_id;
	GLint texture1_id;
	GLint texture2_id;
};

struct fbo_target {
	GLuint fbo;
	GLuint color0_texture;
	GLuint color0_buffer;
	GLuint depth_buffer;
	int samples;
	int width;
	int height;
};

struct graph_dev_smaa_effect {
	struct fbo_target edge_target;
	struct fbo_target blend_target;

	struct graph_dev_gl_fs_effect_shader edge_shader;
	struct graph_dev_gl_fs_effect_shader blend_shader;
	struct graph_dev_gl_fs_effect_shader neighborhood_shader;

	GLuint area_tex;
	GLuint search_tex;
};

/* store all the shader parameters */
static struct graph_dev_gl_single_color_lit_shader single_color_lit_shader;
static struct graph_dev_gl_atmosphere_shader atmosphere_shader;
static struct graph_dev_gl_trans_wireframe_shader trans_wireframe_shader;
static struct graph_dev_gl_trans_wireframe_shader trans_wireframe_with_clip_sphere_shader;
static struct graph_dev_gl_filled_wireframe_shader filled_wireframe_shader;
static struct graph_dev_gl_single_color_shader single_color_shader;
static struct graph_dev_gl_line_single_color_shader line_single_color_shader;
static struct graph_dev_gl_vertex_color_shader vertex_color_shader;
static struct graph_dev_gl_point_cloud_shader point_cloud_shader;
static struct graph_dev_gl_point_cloud_shader point_cloud_intensity_noise_shader;
static struct graph_dev_gl_skybox_shader skybox_shader;
static struct graph_dev_gl_color_by_w_shader color_by_w_shader;
static struct graph_dev_gl_textured_shader textured_shader;
static struct graph_dev_gl_textured_shader textured_with_sphere_shadow_shader;
static struct graph_dev_gl_textured_shader textured_lit_shader;
static struct graph_dev_gl_textured_shader textured_lit_emit_shader;
static struct graph_dev_gl_textured_shader textured_cubemap_lit_shader;
static struct graph_dev_gl_textured_shader textured_cubemap_lit_normal_map_shader;
static struct graph_dev_gl_textured_shader textured_cubemap_shield_shader;
static struct graph_dev_gl_textured_shader textured_cubemap_lit_with_annulus_shadow_shader;
static struct graph_dev_gl_textured_shader textured_cubemap_normal_mapped_lit_with_annulus_shadow_shader;
static struct graph_dev_gl_textured_particle_shader textured_particle_shader;
static struct graph_dev_gl_fs_effect_shader fs_copy_shader;
static struct graph_dev_smaa_effect smaa_effect;

static struct fbo_target msaa = { 0 };
static struct fbo_target post_target0 = { 0 };
static struct fbo_target post_target1 = { 0 };
static struct fbo_target render_target_2d = { 0 };

struct graph_dev_primitive {
	int nvertices;
	GLuint vertex_buffer;
	GLuint triangle_vertex_buffer;
};

static struct graph_dev_primitive cubemap_cube;
static struct graph_dev_primitive textured_unit_quad;

#define BUFFERED_VERTICES_2D 2000
#define VERTEX_BUFFER_2D_SIZE (BUFFERED_VERTICES_2D*sizeof(struct vertex_color_buffer_data))

static struct graph_dev_gl_context {
	int screen_x, screen_y;
	float x_scale, y_scale;
	GdkColor *hue; /* current color */
	int alpha_blend;
	float alpha;
	GLuint fbo_current;

	int active_vp; /* 0=none, 1=2d, 2=3d */
	int vp_x_3d, vp_y_3d, vp_width_3d, vp_height_3d;
	GLuint fbo_2d;
	struct mat44 ortho_2d_mvp;

	int nvertex_2d;
	GLbyte vertex_type_2d[BUFFERED_VERTICES_2D];
	struct vertex_color_buffer_data vertex_data_2d[BUFFERED_VERTICES_2D];
	GLuint vertex_buffer_2d;

	struct mesh_gl_info gl_info_3d_line;
	GLuint fbo_3d;
	int texture_unit_active;
	GLuint texture_unit_bind[4];
	GLenum src_blend_func;
	GLenum dest_blend_func;
	GLint vp_x, vp_y;
	GLsizei vp_width, vp_height;
} sgc;

#define BIND_TEXTURE(tex_unit, tex_type, tex_id) \
	do { \
		int tex_offset = tex_unit - GL_TEXTURE0; \
		if (sgc.texture_unit_active != tex_offset) { \
			glActiveTexture(tex_unit); \
			sgc.texture_unit_active = tex_offset; \
		} \
		if (sgc.texture_unit_bind[tex_offset] != tex_id) { \
			glBindTexture(tex_type, tex_id); \
			sgc.texture_unit_bind[tex_offset] = tex_id; \
		} \
	} while (0)

#define BLEND_FUNC(src_blend, dest_blend) \
	do { \
		if (sgc.src_blend_func != src_blend || sgc.dest_blend_func != dest_blend) { \
			glBlendFunc(src_blend, dest_blend); \
			sgc.src_blend_func = src_blend; \
			sgc.dest_blend_func = dest_blend; \
		} \
	} while (0)

#define VIEWPORT(x, y, width, height) \
	do { \
		if (sgc.vp_x != x || sgc.vp_y != y || sgc.vp_width != width || sgc.vp_height != height) { \
			glViewport(x, y, width, height); \
			sgc.vp_x = x; \
			sgc.vp_y = y; \
			sgc.vp_width = width; \
			sgc.vp_height = height; \
		} \
	} while (0)

static void print_framebuffer_error()
{
	switch (glCheckFramebufferStatus(GL_FRAMEBUFFER)) {
	case GL_FRAMEBUFFER_COMPLETE:
		break;

	case GL_FRAMEBUFFER_UNSUPPORTED:
		printf("FBO Unsupported framebuffer format.\n");
		break;

	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
		printf("FBO Missing attachment.\n");
		break;

	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
		printf("FBO Duplicate attachment.\n");
		break;

	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
		printf("FBO Missing draw buffer.\n");
		break;

	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
		printf("FBO Missing read buffer.\n");
		break;

	case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
		printf("FBO Attached images must have the same number of samples.\n");
		break;

	default:
		printf("FBO Fatal error.\n");
	}
}

static void resize_fbo_if_needed(struct fbo_target *target)
{
	if (target->width != sgc.screen_x || target->height != sgc.screen_y) {
		/* need to resize the fbo attachments */
		if (target->color0_texture > 0) {
			glBindTexture(GL_TEXTURE_2D, target->color0_texture);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
				sgc.screen_x, sgc.screen_y, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);
		}

		if (target->depth_buffer > 0) {
			glBindRenderbuffer(GL_RENDERBUFFER, target->depth_buffer);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, sgc.screen_x, sgc.screen_y);
		}

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target->fbo);

		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			print_framebuffer_error();
		}

		target->width = sgc.screen_x;
		target->height = sgc.screen_y;
	}
}

void graph_dev_set_screen_size(int width, int height)
{
	sgc.active_vp = 0;
	sgc.screen_x = width;
	sgc.screen_y = height;
}

void graph_dev_set_extent_scale(float x_scale, float y_scale)
{
	sgc.x_scale = x_scale;
	sgc.y_scale = y_scale;
}

void graph_dev_set_3d_viewport(int x_offset, int y_offset, int width, int height)
{
	sgc.active_vp = 0;
	sgc.vp_x_3d = x_offset;
	sgc.vp_y_3d = sgc.screen_y - height - y_offset;
	sgc.vp_width_3d = width;
	sgc.vp_height_3d = height;
}

static void enable_2d_viewport()
{
	if (sgc.active_vp != 1) {
		/* 2d viewport is entire screen */
		VIEWPORT(0, 0, sgc.screen_x, sgc.screen_y);

		float left = 0, right = sgc.screen_x, bottom = 0, top = sgc.screen_y;
		float near = -1, far = 1;

		sgc.ortho_2d_mvp.m[0][0] = 2.0 / (right - left);
		sgc.ortho_2d_mvp.m[0][1] = 0;
		sgc.ortho_2d_mvp.m[0][2] = 0;
		sgc.ortho_2d_mvp.m[0][3] = 0;
		sgc.ortho_2d_mvp.m[1][0] = 0;
		sgc.ortho_2d_mvp.m[1][1] = 2.0 / (top - bottom);
		sgc.ortho_2d_mvp.m[1][2] = 0;
		sgc.ortho_2d_mvp.m[1][3] = 0;
		sgc.ortho_2d_mvp.m[2][0] = 0;
		sgc.ortho_2d_mvp.m[2][1] = 0;
		sgc.ortho_2d_mvp.m[2][2] = -2.0 / (far - near);
		sgc.ortho_2d_mvp.m[2][3] = 0;
		sgc.ortho_2d_mvp.m[3][0] = -(right + left) / (right - left);
		sgc.ortho_2d_mvp.m[3][1] = -(top + bottom) / (top - bottom);
		sgc.ortho_2d_mvp.m[3][2] = -(far + near) / (far - near);
		sgc.ortho_2d_mvp.m[3][3] = 1;

		if (sgc.fbo_2d > 0) {
			if (sgc.fbo_current != sgc.fbo_2d) {
				glBindFramebuffer(GL_FRAMEBUFFER, sgc.fbo_2d);
				sgc.fbo_current = sgc.fbo_2d;
			}
		} else if (sgc.fbo_current != 0) {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glDrawBuffer(GL_BACK);
			sgc.fbo_current = 0;
		}

		sgc.active_vp = 1;
	}
}

static void enable_3d_viewport()
{
	if (sgc.active_vp != 2) {
		VIEWPORT(sgc.vp_x_3d, sgc.vp_y_3d, sgc.vp_width_3d, sgc.vp_height_3d);

		if (sgc.fbo_3d > 0) {
			if (sgc.fbo_current != sgc.fbo_3d) {
				glBindFramebuffer(GL_FRAMEBUFFER, sgc.fbo_3d);
				sgc.fbo_current = sgc.fbo_3d;
			}
		} else if (sgc.fbo_current != 0) {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glDrawBuffer(GL_BACK);
			sgc.fbo_current = 0;
		}

		sgc.active_vp = 2;
	}
}


void graph_dev_set_color(void *gdk_color, float a)
{
	sgc.hue = gdk_color;

	if (a >= 0) {
		sgc.alpha_blend = 1;
		sgc.alpha = a;
	} else {
		sgc.alpha_blend = 0;
	}
}

void graph_dev_set_context(void *gdk_drawable, void *gdk_gc)
{
	/* noop */
}

void graph_dev_setup_colors(void *gtk_widget, void *gdk_color_huex, int nhuex)
{
	/* noop */
}

static void draw_vertex_buffer_2d()
{
	if (sgc.nvertex_2d > 0) {
		/* printf("start draw_vertex_buffer_2d %d\n", sgc.nvertex_2d); */
		enable_2d_viewport();

		/* transfer into opengl buffer */
		glBindBuffer(GL_ARRAY_BUFFER, sgc.vertex_buffer_2d);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sgc.nvertex_2d * sizeof(struct vertex_color_buffer_data),
			sgc.vertex_data_2d);

		glUseProgram(vertex_color_shader.program_id);

		glUniformMatrix4fv(vertex_color_shader.mvp_matrix_id, 1, GL_FALSE, &sgc.ortho_2d_mvp.m[0][0]);

		/* load x,y vertex position */
		glEnableVertexAttribArray(vertex_color_shader.vertex_position_id);
		glBindBuffer(GL_ARRAY_BUFFER, sgc.vertex_buffer_2d);
		glVertexAttribPointer(
			vertex_color_shader.vertex_position_id,
			2,
			GL_FLOAT,
			GL_FALSE,
			sizeof(struct vertex_color_buffer_data),
			(void *)offsetof(struct vertex_color_buffer_data, position[0])
		);

		/* load the color as as 4 bytes and let opengl normalize them to 0-1 floats */
		glEnableVertexAttribArray(vertex_color_shader.vertex_color_id);
		glBindBuffer(GL_ARRAY_BUFFER, sgc.vertex_buffer_2d);
		glVertexAttribPointer(
			vertex_color_shader.vertex_color_id,
			4,
			GL_UNSIGNED_BYTE,
			GL_TRUE,
			sizeof(struct vertex_color_buffer_data),
			(void *)offsetof(struct vertex_color_buffer_data, color[0])
		);

		int i;
		GLint start = 0;
		GLbyte mode = sgc.vertex_type_2d[0];

		for (i = 0; i < sgc.nvertex_2d; i++) {
			if (mode != sgc.vertex_type_2d[i]) {
				GLsizei count;

				/* primitive terminate */
				if (sgc.vertex_type_2d[i] == -1)
					count = i - start + 1; /* we include this vertex in draw */
				else
					count = i - start;

				assert(mode != GL_LINES || count % 2 == 0);
				assert(mode != GL_TRIANGLES || count % 3 == 0);

				glDrawArrays(mode, start, count);
				/* printf("glDrawArrays 1 mode=%d start=%d count=%d\n", mode, start, count); */

				start = start + count;
				if (start < sgc.nvertex_2d) {
					mode = sgc.vertex_type_2d[start];
				}
			}
		}
		if (start < sgc.nvertex_2d) {
			GLsizei count = sgc.nvertex_2d - start;

			assert(mode != GL_LINES || count % 2 == 0);
			assert(mode != GL_TRIANGLES || count % 3 == 0);

			glDrawArrays(mode, start, count);
			/* printf("glDrawArrays 2 mode=%d start=%d count=%d\n", mode, start, i - start); */
		}

		sgc.nvertex_2d = 0;

		glDisableVertexAttribArray(vertex_color_shader.vertex_position_id);
		glDisableVertexAttribArray(vertex_color_shader.vertex_color_id);
		glUseProgram(0);

		/* orphan this buffer so we don't get blocked on these draw commands */
		glBindBuffer(GL_ARRAY_BUFFER, sgc.vertex_buffer_2d);
		glBufferData(GL_ARRAY_BUFFER, VERTEX_BUFFER_2D_SIZE, 0, GL_STREAM_DRAW);
	}
}

static void make_room_in_vertex_buffer_2d(int nvertices)
{
	if (sgc.nvertex_2d + nvertices > BUFFERED_VERTICES_2D) {
		/* buffer needs to be emptied to fit next batch */
		draw_vertex_buffer_2d();
	}
}

static void add_vertex_2d(float x, float y, GdkColor* color, GLubyte alpha, GLenum mode)
{
	struct vertex_color_buffer_data *vertex = &sgc.vertex_data_2d[sgc.nvertex_2d];

	/* setup the vertex and color */
	vertex->position[0] = x;
	vertex->position[1] = sgc.screen_y - y;

	vertex->color[0] = color->red >> 8;
	vertex->color[1] = color->green >> 8;
	vertex->color[2] = color->blue >> 8;
	vertex->color[3] = alpha;

	sgc.vertex_type_2d[sgc.nvertex_2d] = mode;

	sgc.nvertex_2d += 1;
}

static void graph_dev_draw_normal_lines(const struct mat44 *mat_mvp, struct mesh *m, struct mesh_gl_info *ptr)
{
	glEnable(GL_DEPTH_TEST);

	glUseProgram(single_color_shader.program_id);

	glUniformMatrix4fv(single_color_shader.mvp_matrix_id, 1, GL_FALSE, &mat_mvp->m[0][0]);

	/* normal lines */
	glUniform4f(single_color_shader.color_id, 1, 0, 0, 1);
	glEnableVertexAttribArray(single_color_shader.vertex_position_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->triangle_normal_lines_buffer);
	glVertexAttribPointer(
		single_color_shader.vertex_position_id, /* The attribute we want to configure */
		3,                           /* size */
		GL_FLOAT,                    /* type */
		GL_FALSE,                    /* normalized? */
		sizeof(struct vertex_buffer_data), /* stride */
		(void *)offsetof(struct vertex_buffer_data, position.v.x) /* array buffer offset */
	);
	glDrawArrays(GL_LINES, 0, m->ntriangles * 3 * 2);

	/* tangent lines */
	glUniform4f(single_color_shader.color_id, 0, 1, 0, 1);
	glEnableVertexAttribArray(single_color_shader.vertex_position_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->triangle_tangent_lines_buffer);
	glVertexAttribPointer(
		single_color_shader.vertex_position_id, /* The attribute we want to configure */
		3,                           /* size */
		GL_FLOAT,                    /* type */
		GL_FALSE,                    /* normalized? */
		sizeof(struct vertex_buffer_data), /* stride */
		(void *)offsetof(struct vertex_buffer_data, position.v.x) /* array buffer offset */
	);
	glDrawArrays(GL_LINES, 0, m->ntriangles * 3 * 2);

	/* bitangent lines */
	glUniform4f(single_color_shader.color_id, 0, 0, 1, 1);
	glEnableVertexAttribArray(single_color_shader.vertex_position_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->triangle_bitangent_lines_buffer);
	glVertexAttribPointer(
		single_color_shader.vertex_position_id, /* The attribute we want to configure */
		3,                           /* size */
		GL_FLOAT,                    /* type */
		GL_FALSE,                    /* normalized? */
		sizeof(struct vertex_buffer_data), /* stride */
		(void *)offsetof(struct vertex_buffer_data, position.v.x) /* array buffer offset */
	);
	glDrawArrays(GL_LINES, 0, m->ntriangles * 3 * 2);

	glDisableVertexAttribArray(single_color_shader.vertex_position_id);
	glUseProgram(0);

	glDisable(GL_DEPTH_TEST);
}

static void graph_dev_raster_texture(struct graph_dev_gl_textured_shader *shader, const struct mat44 *mat_mvp,
	const struct mat44 *mat_mv, const struct mat33 *mat_normal, struct mesh *m,
	struct sng_color *triangle_color, float alpha, union vec3 *eye_light_pos, GLuint texture_number,
	GLuint emit_texture_number, GLuint normalmap_texture_number, struct shadow_sphere_data *shadow_sphere,
	struct shadow_annulus_data *shadow_annulus, int do_cullface, int do_blend,
	float ring_texture_v, float ring_inner_radius, float ring_outer_radius)
{
	enable_3d_viewport();

	if (!m->graph_ptr)
		return;

	struct mesh_gl_info *ptr = m->graph_ptr;

	glEnable(GL_DEPTH_TEST);

	if (do_cullface)
		glEnable(GL_CULL_FACE);
	if (draw_polygon_as_lines)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	if (do_blend) {
		/* enable depth test but don't write to depth buffer */
		glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);
		BLEND_FUNC(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	}

	glUseProgram(shader->program_id);

	if (shader->texture_2d_id >= 0)
		BIND_TEXTURE(GL_TEXTURE0, GL_TEXTURE_2D, texture_number);
	else if (shader->texture_cubemap_id >= 0)
		BIND_TEXTURE(GL_TEXTURE0, GL_TEXTURE_CUBE_MAP, texture_number);

	if (shader->normalmap_cubemap_id >= 0)
		BIND_TEXTURE(GL_TEXTURE3, GL_TEXTURE_CUBE_MAP, normalmap_texture_number);

	if (shader->emit_texture_2d_id >= 0)
		BIND_TEXTURE(GL_TEXTURE1, GL_TEXTURE_2D, emit_texture_number);

	if (shader->light_pos_id >= 0)
		glUniform3f(shader->light_pos_id, eye_light_pos->v.x, eye_light_pos->v.y, eye_light_pos->v.z);

	glUniformMatrix4fv(shader->mvp_matrix_id, 1, GL_FALSE, &mat_mvp->m[0][0]);
	if (shader->mv_matrix_id >= 0)
		glUniformMatrix4fv(shader->mv_matrix_id, 1, GL_FALSE, &mat_mv->m[0][0]);
	if (shader->normal_matrix_id >= 0)
		glUniformMatrix3fv(shader->normal_matrix_id, 1, GL_FALSE, &mat_normal->m[0][0]);

	glUniform4f(shader->tint_color_id, triangle_color->red,
		triangle_color->green, triangle_color->blue, alpha);

	/* Ring texture v value */
	if (shader->ring_texture_v_id >= 0)
		glUniform1f(shader->ring_texture_v_id, ring_texture_v);
	if (shader->ring_inner_radius_id >= 0)
		glUniform1f(shader->ring_inner_radius_id, ring_inner_radius);
	if (shader->ring_outer_radius_id >= 0)
		glUniform1f(shader->ring_outer_radius_id, ring_outer_radius);

	/* shadow sphere */
	if (shader->shadow_sphere_id >= 0)
		glUniform4f(shader->shadow_sphere_id, shadow_sphere->eye_pos.v.x, shadow_sphere->eye_pos.v.y,
			shadow_sphere->eye_pos.v.z, shadow_sphere->r * shadow_sphere->r);

	/* shadow annulus */
	if (shader->shadow_annulus_texture_id >= 0) {
		BIND_TEXTURE(GL_TEXTURE1, GL_TEXTURE_2D, shadow_annulus->texture_id);

		glUniform4f(shader->shadow_annulus_tint_color_id, shadow_annulus->tint_color.red,
			shadow_annulus->tint_color.green, shadow_annulus->tint_color.blue, shadow_annulus->alpha);
		glUniform3f(shader->shadow_annulus_center_id, shadow_annulus->eye_pos.v.x,
			shadow_annulus->eye_pos.v.y, shadow_annulus->eye_pos.v.z);

		/* this only works if the ring has an identity quat for its child orientation */
		/* ring disc is in x/y plane, so z is normal */
		union vec3 annulus_normal = { { 0, 0, 1 } };
		union vec3 eye_annulus_normal;
		mat33_x_vec3(mat_normal, &annulus_normal, &eye_annulus_normal);
		vec3_normalize_self(&eye_annulus_normal);

		glUniform3f(shader->shadow_annulus_normal_id, eye_annulus_normal.v.x,
			eye_annulus_normal.v.y, eye_annulus_normal.v.z);

		glUniform4f(shader->shadow_annulus_radius_id,
			shadow_annulus->r1, shadow_annulus->r1 * shadow_annulus->r1,
			shadow_annulus->r2, shadow_annulus->r2 * shadow_annulus->r2);
	}

	glEnableVertexAttribArray(shader->vertex_position_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->vertex_buffer);
	glVertexAttribPointer(
		shader->vertex_position_id, /* The attribute we want to configure */
		3,                           /* size */
		GL_FLOAT,                    /* type */
		GL_FALSE,                    /* normalized? */
		sizeof(struct vertex_buffer_data), /* stride */
		(void *)offsetof(struct vertex_buffer_data, position.v.x) /* array buffer offset */
	);

	if (shader->vertex_normal_id >= 0) {
		glEnableVertexAttribArray(shader->vertex_normal_id);
		glBindBuffer(GL_ARRAY_BUFFER, ptr->triangle_vertex_buffer);
		glVertexAttribPointer(
			shader->vertex_normal_id,  /* The attribute we want to configure */
			3,                            /* size */
			GL_FLOAT,                     /* type */
			GL_FALSE,                     /* normalized? */
			sizeof(struct vertex_triangle_buffer_data), /* stride */
			(void *)offsetof(struct vertex_triangle_buffer_data, normal.v.x) /* array buffer offset */
		);
	}

	if (shader->vertex_tangent_id >= 0) {
		glEnableVertexAttribArray(shader->vertex_tangent_id);
		glBindBuffer(GL_ARRAY_BUFFER, ptr->triangle_vertex_buffer);
		glVertexAttribPointer(
			shader->vertex_tangent_id,    /* The attribute we want to configure */
			3,                            /* size */
			GL_FLOAT,                     /* type */
			GL_FALSE,                     /* normalized? */
			sizeof(struct vertex_triangle_buffer_data), /* stride */
			(void *)offsetof(struct vertex_triangle_buffer_data, tangent.v.x) /* array buffer offset */
		);
	}

	if (shader->vertex_bitangent_id >= 0) {
		glEnableVertexAttribArray(shader->vertex_bitangent_id);
		glBindBuffer(GL_ARRAY_BUFFER, ptr->triangle_vertex_buffer);
		glVertexAttribPointer(
			shader->vertex_bitangent_id,  /* The attribute we want to configure */
			3,                            /* size */
			GL_FLOAT,                     /* type */
			GL_FALSE,                     /* normalized? */
			sizeof(struct vertex_triangle_buffer_data), /* stride */
			(void *)offsetof(struct vertex_triangle_buffer_data, bitangent.v.x) /* array buffer offset */
		);
	}

	if (shader->texture_coord_id >= 0) {
		glEnableVertexAttribArray(shader->texture_coord_id);
		glBindBuffer(GL_ARRAY_BUFFER, ptr->triangle_vertex_buffer);
		glVertexAttribPointer(shader->texture_coord_id, 2, GL_FLOAT, GL_TRUE,
			sizeof(struct vertex_triangle_buffer_data),
			(void *)offsetof(struct vertex_triangle_buffer_data, texture_coord.v.x));
	}

	glDrawArrays(GL_TRIANGLES, 0, m->ntriangles * 3);

	glDisableVertexAttribArray(shader->vertex_position_id);
	if (shader->vertex_normal_id >= 0)
		glDisableVertexAttribArray(shader->vertex_normal_id);
	if (shader->vertex_tangent_id >= 0)
		glDisableVertexAttribArray(shader->vertex_tangent_id);
	if (shader->vertex_bitangent_id >= 0)
		glDisableVertexAttribArray(shader->vertex_bitangent_id);
	if (shader->texture_coord_id >= 0)
		glDisableVertexAttribArray(shader->texture_coord_id);
	glUseProgram(0);

	glDisable(GL_DEPTH_TEST);
	if (do_cullface)
		glDisable(GL_CULL_FACE);
	if (draw_polygon_as_lines)
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	if (do_blend) {
		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
	}

	if (draw_normal_lines) {
		graph_dev_draw_normal_lines(mat_mvp, m, ptr);
	}
}

static void graph_dev_raster_single_color_lit(const struct mat44 *mat_mvp, const struct mat44 *mat_mv,
	const struct mat33 *mat_normal, struct mesh *m, struct sng_color *triangle_color, union vec3 *eye_light_pos)
{
	enable_3d_viewport();

	if (!m->graph_ptr)
		return;

	struct mesh_gl_info *ptr = m->graph_ptr;

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	if (draw_polygon_as_lines)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	glUseProgram(single_color_lit_shader.program_id);

	glUniformMatrix4fv(single_color_lit_shader.mv_matrix_id, 1, GL_FALSE, &mat_mv->m[0][0]);
	glUniformMatrix4fv(single_color_lit_shader.mvp_matrix_id, 1, GL_FALSE, &mat_mvp->m[0][0]);
	glUniformMatrix3fv(single_color_lit_shader.normal_matrix_id, 1, GL_FALSE, &mat_normal->m[0][0]);

	glUniform3f(single_color_lit_shader.color_id, triangle_color->red,
		triangle_color->green, triangle_color->blue);
	glUniform3f(single_color_lit_shader.light_pos_id, eye_light_pos->v.x, eye_light_pos->v.y, eye_light_pos->v.z);

	glEnableVertexAttribArray(single_color_lit_shader.vertex_position_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->vertex_buffer);
	glVertexAttribPointer(
		single_color_lit_shader.vertex_position_id, /* The attribute we want to configure */
		3,                           /* size */
		GL_FLOAT,                    /* type */
		GL_FALSE,                    /* normalized? */
		sizeof(struct vertex_buffer_data), /* stride */
		(void *)offsetof(struct vertex_buffer_data, position.v.x) /* array buffer offset */
	);

	glEnableVertexAttribArray(single_color_lit_shader.vertex_normal_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->triangle_vertex_buffer);
	glVertexAttribPointer(
		single_color_lit_shader.vertex_normal_id,  /* The attribute we want to configure */
		3,                            /* size */
		GL_FLOAT,                     /* type */
		GL_FALSE,                     /* normalized? */
		sizeof(struct vertex_triangle_buffer_data), /* stride */
		(void *)offsetof(struct vertex_triangle_buffer_data, normal.v.x) /* array buffer offset */
	);

	glDrawArrays(GL_TRIANGLES, 0, m->ntriangles * 3);

	glDisableVertexAttribArray(single_color_lit_shader.vertex_position_id);
	glDisableVertexAttribArray(single_color_lit_shader.vertex_normal_id);
	glUseProgram(0);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	if (draw_polygon_as_lines)
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	if (draw_normal_lines) {
		graph_dev_draw_normal_lines(mat_mvp, m, ptr);
	}
}

static void graph_dev_raster_atmosphere(const struct mat44 *mat_mvp, const struct mat44 *mat_mv,
	const struct mat33 *mat_normal,
	struct mesh *m, struct sng_color *triangle_color, union vec3 *eye_light_pos)
{
	enable_3d_viewport();

	if (!draw_atmospheres)
		return;

	if (!m->graph_ptr)
		return;

	struct mesh_gl_info *ptr = m->graph_ptr;

	/* enable depth test but don't write to depth buffer */
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	BLEND_FUNC(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	if (draw_polygon_as_lines)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	glUseProgram(atmosphere_shader.program_id);

	glUniformMatrix4fv(atmosphere_shader.mv_matrix_id, 1, GL_FALSE, &mat_mv->m[0][0]);
	glUniformMatrix4fv(atmosphere_shader.mvp_matrix_id, 1, GL_FALSE, &mat_mvp->m[0][0]);
	glUniformMatrix3fv(atmosphere_shader.normal_matrix_id, 1, GL_FALSE, &mat_normal->m[0][0]);

	glUniform3f(atmosphere_shader.color_id, triangle_color->red,
		triangle_color->green, triangle_color->blue);
	glUniform3f(atmosphere_shader.light_pos_id, eye_light_pos->v.x, eye_light_pos->v.y, eye_light_pos->v.z);

	glEnableVertexAttribArray(atmosphere_shader.vertex_position_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->vertex_buffer);
	glVertexAttribPointer(
		atmosphere_shader.vertex_position_id, /* The attribute we want to configure */
		3,                           /* size */
		GL_FLOAT,                    /* type */
		GL_FALSE,                    /* normalized? */
		sizeof(struct vertex_buffer_data), /* stride */
		(void *)offsetof(struct vertex_buffer_data, position.v.x) /* array buffer offset */
	);

	glEnableVertexAttribArray(atmosphere_shader.vertex_normal_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->triangle_vertex_buffer);
	glVertexAttribPointer(
		atmosphere_shader.vertex_normal_id,  /* The attribute we want to configure */
		3,                            /* size */
		GL_FLOAT,                     /* type */
		GL_FALSE,                     /* normalized? */
		sizeof(struct vertex_triangle_buffer_data), /* stride */
		(void *)offsetof(struct vertex_triangle_buffer_data, normal.v.x) /* array buffer offset */
	);

	glDrawArrays(GL_TRIANGLES, 0, m->ntriangles * 3);

	glDisableVertexAttribArray(atmosphere_shader.vertex_position_id);
	glDisableVertexAttribArray(atmosphere_shader.vertex_normal_id);
	glUseProgram(0);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDepthMask(GL_TRUE);
	if (draw_polygon_as_lines)
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	if (draw_normal_lines) {
		graph_dev_draw_normal_lines(mat_mvp, m, ptr);
	}
}

static void graph_dev_raster_filled_wireframe_mesh(const struct mat44 *mat_mvp, struct mesh *m,
	struct sng_color *line_color, struct sng_color *triangle_color)
{
	enable_3d_viewport();

	if (!m->graph_ptr)
		return;

	struct mesh_gl_info *ptr = m->graph_ptr;

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	glUseProgram(filled_wireframe_shader.program_id);

	glUniform2f(filled_wireframe_shader.viewport_id, sgc.vp_width_3d, sgc.vp_height_3d);
	glUniformMatrix4fv(filled_wireframe_shader.mvp_matrix_id, 1, GL_FALSE, &mat_mvp->m[0][0]);

	glUniform3f(filled_wireframe_shader.line_color_id, line_color->red,
		line_color->green, line_color->blue);
	glUniform3f(filled_wireframe_shader.triangle_color_id, triangle_color->red,
		triangle_color->green, triangle_color->blue);

	glEnableVertexAttribArray(filled_wireframe_shader.position_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->vertex_buffer);
	glVertexAttribPointer(
		filled_wireframe_shader.position_id,
		3,
		GL_FLOAT,
		GL_FALSE,
		sizeof(struct vertex_buffer_data),
		(void *)offsetof(struct vertex_buffer_data, position.v.x)
	);

	glEnableVertexAttribArray(filled_wireframe_shader.tvertex0_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->triangle_vertex_buffer);
	glVertexAttribPointer(
		filled_wireframe_shader.tvertex0_id,
		3,
		GL_FLOAT,
		GL_FALSE,
		sizeof(struct vertex_triangle_buffer_data),
		(void *)offsetof(struct vertex_triangle_buffer_data, tvertex0.v.x)
	);

	glEnableVertexAttribArray(filled_wireframe_shader.tvertex1_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->triangle_vertex_buffer);
	glVertexAttribPointer(
		filled_wireframe_shader.tvertex1_id,
		3,
		GL_FLOAT,
		GL_FALSE,
		sizeof(struct vertex_triangle_buffer_data),
		(void *)offsetof(struct vertex_triangle_buffer_data, tvertex1.v.x)
	);

	glEnableVertexAttribArray(filled_wireframe_shader.tvertex2_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->triangle_vertex_buffer);
	glVertexAttribPointer(
		filled_wireframe_shader.tvertex2_id,
		3,
		GL_FLOAT,
		GL_FALSE,
		sizeof(struct vertex_triangle_buffer_data),
		(void *)offsetof(struct vertex_triangle_buffer_data, tvertex2.v.x)
	);

	glEnableVertexAttribArray(filled_wireframe_shader.edge_mask_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->triangle_vertex_buffer);
	glVertexAttribPointer(
		filled_wireframe_shader.edge_mask_id,
		3,
		GL_FLOAT,
		GL_FALSE,
		sizeof(struct vertex_triangle_buffer_data),
		(void *)offsetof(struct vertex_triangle_buffer_data, wireframe_edge_mask.v.x)
	);

	glDrawArrays(GL_TRIANGLES, 0, ptr->ntriangles*3);

	glDisableVertexAttribArray(filled_wireframe_shader.position_id);
	glUseProgram(0);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	if (draw_normal_lines) {
		graph_dev_draw_normal_lines(mat_mvp, m, ptr);
	}
}

static void graph_dev_raster_trans_wireframe_mesh(struct graph_dev_gl_trans_wireframe_shader *shader,
		const struct mat44 *mat_mvp, const struct mat44 *mat_mv,
		const struct mat33 *mat_normal, struct mesh *m, struct sng_color *line_color,
		struct clip_sphere_data *clip_sphere, int do_cullface)
{
	enable_3d_viewport();

	if (!m->graph_ptr)
		return;

	struct mesh_gl_info *ptr = m->graph_ptr;

	glEnable(GL_DEPTH_TEST);

	if (do_cullface) {
		glUseProgram(shader->program_id);

		glUniformMatrix4fv(shader->mvp_matrix_id, 1, GL_FALSE, &mat_mvp->m[0][0]);
		glUniformMatrix4fv(shader->mv_matrix_id, 1, GL_FALSE, &mat_mv->m[0][0]);
		glUniformMatrix3fv(shader->normal_matrix_id, 1, GL_FALSE, &mat_normal->m[0][0]);
		glUniform3f(shader->color_id, line_color->red,
			line_color->green, line_color->blue);

		if (shader->clip_sphere_id >= 0) {
			glUniform4f(shader->clip_sphere_id, clip_sphere->eye_pos.v.x, clip_sphere->eye_pos.v.y,
				clip_sphere->eye_pos.v.z, clip_sphere->r);

			glUniform1f(shader->clip_sphere_radius_fade_id, clip_sphere->radius_fade);
		}

		glEnableVertexAttribArray(shader->vertex_position_id);
		glBindBuffer(GL_ARRAY_BUFFER, ptr->wireframe_lines_vertex_buffer);
		glVertexAttribPointer(
			shader->vertex_position_id, /* The attribute we want to configure */
			3,                           /* size */
			GL_FLOAT,                    /* type */
			GL_FALSE,                    /* normalized? */
			sizeof(struct vertex_wireframe_line_buffer_data), /* stride */
			(void *)offsetof(struct vertex_wireframe_line_buffer_data, position.v.x) /* array buffer offset */
		);

		glEnableVertexAttribArray(shader->vertex_normal_id);
		glBindBuffer(GL_ARRAY_BUFFER, ptr->wireframe_lines_vertex_buffer);
		glVertexAttribPointer(
			shader->vertex_normal_id,    /* The attribute we want to configure */
			3,                            /* size */
			GL_FLOAT,                     /* type */
			GL_FALSE,                     /* normalized? */
			sizeof(struct vertex_wireframe_line_buffer_data), /* stride */
			(void *)offsetof(struct vertex_wireframe_line_buffer_data, normal.v.x) /* array buffer offset */
		);

	} else {
		/* don't cullface so just render with single color shader */
		glUseProgram(single_color_shader.program_id);

		glUniformMatrix4fv(single_color_shader.mvp_matrix_id, 1, GL_FALSE, &mat_mvp->m[0][0]);
		glUniform4f(single_color_shader.color_id, line_color->red,
			line_color->green, line_color->blue, 1.0);

		glEnableVertexAttribArray(single_color_shader.vertex_position_id);
		glBindBuffer(GL_ARRAY_BUFFER, ptr->wireframe_lines_vertex_buffer);
		glVertexAttribPointer(
			single_color_shader.vertex_position_id, /* The attribute we want to configure */
			3,                           /* size */
			GL_FLOAT,                    /* type */
			GL_FALSE,                    /* normalized? */
			sizeof(struct vertex_wireframe_line_buffer_data), /* stride */
			(void *)offsetof(struct vertex_wireframe_line_buffer_data, position.v.x) /* array buffer offset */
		);
	}

	glDrawArrays(GL_LINES, 0, ptr->nwireframe_lines * 2);

	if (do_cullface) {
		glDisableVertexAttribArray(shader->vertex_position_id);
		glDisableVertexAttribArray(shader->vertex_normal_id);
	} else {
		glDisableVertexAttribArray(single_color_shader.vertex_position_id);
	}

	glUseProgram(0);

	glDisable(GL_DEPTH_TEST);

	if (draw_normal_lines) {
		graph_dev_draw_normal_lines(mat_mvp, m, ptr);
	}
}

static void graph_dev_raster_line_mesh(struct entity *e, const struct mat44 *mat_mvp, struct mesh *m,
					struct sng_color *line_color)
{
	enable_3d_viewport();

	if (!m->graph_ptr)
		return;

	struct mesh_gl_info *ptr = m->graph_ptr;

	glEnable(GL_DEPTH_TEST);

	GLuint vertex_position_id;

	if (e->material_ptr && e->material_ptr->type == MATERIAL_COLOR_BY_W) {
		struct material_color_by_w *m = &e->material_ptr->color_by_w;

		glUseProgram(color_by_w_shader.program_id);

		glUniformMatrix4fv(color_by_w_shader.mvp_id, 1, GL_FALSE, &mat_mvp->m[0][0]);

		struct sng_color near_color = sng_get_color(m->near_color);
		glUniform3f(color_by_w_shader.near_color_id, near_color.red,
			near_color.green, near_color.blue);
		glUniform1f(color_by_w_shader.near_w_id, m->near_w);

		struct sng_color center_color = sng_get_color(m->center_color);
		glUniform3f(color_by_w_shader.center_color_id, center_color.red,
			center_color.green, center_color.blue);
		glUniform1f(color_by_w_shader.center_w_id, m->center_w);

		struct sng_color far_color = sng_get_color(m->far_color);
		glUniform3f(color_by_w_shader.far_color_id, far_color.red,
			far_color.green, far_color.blue);
		glUniform1f(color_by_w_shader.far_w_id, m->far_w);

		vertex_position_id = color_by_w_shader.position_id;
	} else {
		glUseProgram(line_single_color_shader.program_id);

		glUniformMatrix4fv(line_single_color_shader.mvp_matrix_id, 1, GL_FALSE, &mat_mvp->m[0][0]);
		glUniform2f(line_single_color_shader.viewport_id, sgc.vp_width_3d, sgc.vp_height_3d);

		glUniform1f(line_single_color_shader.dot_size_id, 2.0);
		glUniform1f(line_single_color_shader.dot_pitch_id, 5.0);
		glUniform4f(line_single_color_shader.line_color_id, line_color->red, line_color->green,
			line_color->blue, 1.0);

		vertex_position_id = line_single_color_shader.vertex_position_id;

		glEnableVertexAttribArray(line_single_color_shader.multi_one_id);
		glBindBuffer(GL_ARRAY_BUFFER, ptr->line_vertex_buffer);
		glVertexAttribPointer(
			line_single_color_shader.multi_one_id, /* The attribute we want to configure */
			4,                           /* size */
			GL_UNSIGNED_BYTE,            /* type */
			GL_TRUE,                     /* normalized? */
			sizeof(struct vertex_line_buffer_data), /* stride */
			(void *)offsetof(struct vertex_line_buffer_data, multi_one) /* array buffer offset */
		);

		glEnableVertexAttribArray(line_single_color_shader.line_vertex0_id);
		glBindBuffer(GL_ARRAY_BUFFER, ptr->line_vertex_buffer);
		glVertexAttribPointer(
			line_single_color_shader.line_vertex0_id,
			3,
			GL_FLOAT,
			GL_FALSE,
			sizeof(struct vertex_line_buffer_data),
			(void *)offsetof(struct vertex_line_buffer_data, line_vertex0.v.x)
		);

		glEnableVertexAttribArray(line_single_color_shader.line_vertex1_id);
		glBindBuffer(GL_ARRAY_BUFFER, ptr->line_vertex_buffer);
		glVertexAttribPointer(
			line_single_color_shader.line_vertex1_id,
			3,
			GL_FLOAT,
			GL_FALSE,
			sizeof(struct vertex_line_buffer_data),
			(void *)offsetof(struct vertex_line_buffer_data, line_vertex1.v.x)
	);

	}

	glEnableVertexAttribArray(vertex_position_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->vertex_buffer);
	glVertexAttribPointer(
		vertex_position_id, /* The attribute we want to configure */
		3,                           /* size */
		GL_FLOAT,                    /* type */
		GL_FALSE,                    /* normalized? */
		sizeof(struct vertex_buffer_data), /* stride */
		(void *)offsetof(struct vertex_buffer_data, position.v.x) /* array buffer offset */
	);

	glDrawArrays(GL_LINES, 0, ptr->nlines * 2);

	glDisableVertexAttribArray(vertex_position_id);
	if (e->material_ptr && e->material_ptr->type != MATERIAL_COLOR_BY_W) {
		glDisableVertexAttribArray(line_single_color_shader.multi_one_id);
		glDisableVertexAttribArray(line_single_color_shader.line_vertex0_id);
		glDisableVertexAttribArray(line_single_color_shader.line_vertex1_id);
	}
	glUseProgram(0);

	glDisable(GL_DEPTH_TEST);
}

void graph_dev_raster_point_cloud_mesh(struct graph_dev_gl_point_cloud_shader *shader,
	const struct mat44 *mat_mvp, struct mesh *m, struct sng_color *point_color, float alpha, float pointSize,
	int do_blend)
{
	enable_3d_viewport();

	if (!m->graph_ptr)
		return;

	struct mesh_gl_info *ptr = m->graph_ptr;

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);

	if (do_blend) {
		/* enable depth test but don't write to depth buffer */
		glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);
		BLEND_FUNC(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	}

	glUseProgram(shader->program_id);

	glUniformMatrix4fv(shader->mvp_matrix_id, 1, GL_FALSE, &mat_mvp->m[0][0]);
	glUniform1f(shader->point_size_id, pointSize);
	glUniform4f(shader->color_id, point_color->red,
		point_color->green, point_color->blue, alpha);

	if (shader->time_id >= 0) {
		float time = fmod(time_now_double(), 1.0);
		glUniform1f(shader->time_id, time);
	}

	glEnableVertexAttribArray(shader->vertex_position_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->vertex_buffer);
	glVertexAttribPointer(
		shader->vertex_position_id, /* The attribute we want to configure */
		3,                           /* size */
		GL_FLOAT,                    /* type */
		GL_FALSE,                    /* normalized? */
		sizeof(struct vertex_buffer_data), /* stride */
		(void *)offsetof(struct vertex_buffer_data, position.v.x) /* array buffer offset */
	);

	glDrawArrays(GL_POINTS, 0, ptr->npoints);

	glDisableVertexAttribArray(shader->vertex_position_id);
	glUseProgram(0);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_VERTEX_PROGRAM_POINT_SIZE);
	if (do_blend) {
		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
	}
}

static void graph_dev_draw_nebula(const struct mat44 *mat_mvp, const struct mat44 *mat_mv, const struct mat33 *mat_normal,
	struct entity *e, union vec3 *eye_light_pos)
{
	struct material_nebula *mt = &e->material_ptr->nebula;

	/* transform model origin into camera space */
	union vec4 ent_pos = { { 0.0, 0.0, 0.0, 1.0 } };
	union vec4 camera_ent_pos_4;
	mat44_x_vec4(mat_mv, &ent_pos, &camera_ent_pos_4);

	union vec3 camera_pos = { { 0, 0, 0 } };
	union vec3 camera_ent_pos;
	vec4_to_vec3(&camera_ent_pos_4, &camera_ent_pos);

	union vec3 camera_ent_vector;
	vec3_sub(&camera_ent_vector, &camera_pos, &camera_ent_pos);
	vec3_normalize_self(&camera_ent_vector);

	int i;
	for (i = 0; i < MATERIAL_NEBULA_NPLANES; i++) {
		struct mat44 mat_local_r;
		quat_to_rh_rot_matrix(&mt->orientation[i], &mat_local_r.m[0][0]);

		struct mat44 mat_mvp_local_r;
		mat44_product(mat_mvp, &mat_local_r, &mat_mvp_local_r);

		struct mat44 mat_mv_local_r;
		mat44_product(mat_mv, &mat_local_r, &mat_mv_local_r);

		struct mat33 mat_normal_local_r;
		struct mat33 mat_tmp33;
		mat33_inverse_transpose_ff(mat44_to_mat33_ff(&mat_mv_local_r, &mat_tmp33), &mat_normal_local_r);

		/* rotate the triangle normal into camera space */
		union vec3 *ent_normal = (union vec3 *)&e->m->t[0].n.x;
		union vec3 camera_normal;
		mat33_x_vec3(&mat_normal_local_r, ent_normal, &camera_normal);
		vec3_normalize_self(&camera_normal);

		float alpha = fabs(vec3_dot(&camera_normal, &camera_ent_vector)) * mt->alpha;

		graph_dev_raster_texture(&textured_shader, &mat_mvp_local_r, &mat_mv_local_r, &mat_normal_local_r,
			e->m, &mt->tint, alpha, eye_light_pos, mt->texture_id[i], 0, -1, 0, 0, 0, 1, 0.0f,
				2.0f, 4.0f);

		if (draw_billboard_wireframe) {
			struct sng_color line_color = sng_get_color(WHITE);
			graph_dev_raster_trans_wireframe_mesh(0, &mat_mvp_local_r, &mat_mv_local_r,
				&mat_normal_local_r, e->m, &line_color, 0, 0);
		}
	}

}

static void graph_dev_raster_particle_animation(const struct entity_context *cx, struct entity *e,
	const struct entity_transform *transform, GLuint texture_number,
	float particle_radius, float time_base)
{
	enable_3d_viewport();

	if (!e->m->graph_ptr)
		return;

	struct mesh_gl_info *ptr = e->m->graph_ptr;

	/* enable depth test but don't write to depth buffer */
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	if (draw_polygon_as_lines)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	glEnable(GL_BLEND);
	BLEND_FUNC(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	glUseProgram(textured_particle_shader.program_id);

	glUniformMatrix4fv(textured_particle_shader.mvp_matrix_id, 1, GL_FALSE, &transform->mvp.m[0][0]);

	/* need the transpose of model and view rotation */
	struct mat33d v_rotation, m_rotation;
	mat44_to_mat33_dd(transform->v, &v_rotation);
	mat44_to_mat33_dd(&transform->m_no_scale, &m_rotation);

	struct mat33 mv_rotation;
	mat33_product_ddf(&v_rotation, &m_rotation, &mv_rotation);

	struct mat33 mat_view_to_model;
	mat33_transpose(&mv_rotation, &mat_view_to_model);

	union vec3 camera_up_view = { { 0, 1, 0 } };
	union vec3 camera_up_model;
	mat33_x_vec3(&mat_view_to_model, &camera_up_view, &camera_up_model);
	vec3_normalize_self(&camera_up_model);

	union vec3 camera_right_view = { { 1, 0, 0 } };
	union vec3 camera_right_model;
	mat33_x_vec3(&mat_view_to_model, &camera_right_view, &camera_right_model);
	vec3_normalize_self(&camera_right_model);

	glUniform3f(textured_particle_shader.camera_up_vec_id, camera_up_model.v.x, camera_up_model.v.y,
		camera_up_model.v.z);
	glUniform3f(textured_particle_shader.camera_right_vec_id, camera_right_model.v.x, camera_right_model.v.y,
		camera_right_model.v.z);

	double time_now = time_now_double();
	double fmoded_time = fmod(time_now, time_base);
	float anim_time = fmoded_time / time_base;
	glUniform1f(textured_particle_shader.time_id, anim_time);

	glUniform1f(textured_particle_shader.radius_id, particle_radius);

	BIND_TEXTURE(GL_TEXTURE0, GL_TEXTURE_2D, texture_number);

	glEnableVertexAttribArray(textured_particle_shader.multi_one_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->particle_vertex_buffer);
	glVertexAttribPointer(
		textured_particle_shader.multi_one_id, /* The attribute we want to configure */
		4,                           /* size */
		GL_UNSIGNED_BYTE,            /* type */
		GL_TRUE,                     /* normalized? */
		sizeof(struct vertex_particle_buffer_data), /* stride */
		(void *)offsetof(struct vertex_particle_buffer_data, multi_one) /* array buffer offset */
	);

	glEnableVertexAttribArray(textured_particle_shader.start_position_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->particle_vertex_buffer);
	glVertexAttribPointer(
		textured_particle_shader.start_position_id, /* The attribute we want to configure */
		3,                           /* size */
		GL_FLOAT,                    /* type */
		GL_FALSE,                    /* normalized? */
		sizeof(struct vertex_particle_buffer_data), /* stride */
		(void *)offsetof(struct vertex_particle_buffer_data, start_position.v.x) /* array buffer offset */
	);

	glEnableVertexAttribArray(textured_particle_shader.start_tint_color_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->particle_vertex_buffer);
	glVertexAttribPointer(
		textured_particle_shader.start_tint_color_id, /* The attribute we want to configure */
		3,                           /* size */
		GL_UNSIGNED_BYTE,            /* type */
		GL_TRUE,                     /* normalized? */
		sizeof(struct vertex_particle_buffer_data), /* stride */
		(void *)offsetof(struct vertex_particle_buffer_data, start_tint_color) /* array buffer offset */
	);

	glEnableVertexAttribArray(textured_particle_shader.start_apm_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->particle_vertex_buffer);
	glVertexAttribPointer(
		textured_particle_shader.start_apm_id, /* The attribute we want to configure */
		2,                           /* size */
		GL_UNSIGNED_BYTE,            /* type */
		GL_TRUE,                     /* normalized? */
		sizeof(struct vertex_particle_buffer_data), /* stride */
		(void *)offsetof(struct vertex_particle_buffer_data, start_apm) /* array buffer offset */
	);

	glEnableVertexAttribArray(textured_particle_shader.end_position_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->particle_vertex_buffer);
	glVertexAttribPointer(
		textured_particle_shader.end_position_id, /* The attribute we want to configure */
		3,                           /* size */
		GL_FLOAT,                    /* type */
		GL_FALSE,                    /* normalized? */
		sizeof(struct vertex_particle_buffer_data), /* stride */
		(void *)offsetof(struct vertex_particle_buffer_data, end_position.v.x) /* array buffer offset */
	);

	glEnableVertexAttribArray(textured_particle_shader.end_tint_color_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->particle_vertex_buffer);
	glVertexAttribPointer(
		textured_particle_shader.end_tint_color_id, /* The attribute we want to configure */
		3,                           /* size */
		GL_UNSIGNED_BYTE,            /* type */
		GL_TRUE,                     /* normalized? */
		sizeof(struct vertex_particle_buffer_data), /* stride */
		(void *)offsetof(struct vertex_particle_buffer_data, end_tint_color) /* array buffer offset */
	);

	glEnableVertexAttribArray(textured_particle_shader.end_apm_id);
	glBindBuffer(GL_ARRAY_BUFFER, ptr->particle_vertex_buffer);
	glVertexAttribPointer(
		textured_particle_shader.end_apm_id, /* The attribute we want to configure */
		2,                           /* size */
		GL_UNSIGNED_BYTE,            /* type */
		GL_TRUE,                     /* normalized? */
		sizeof(struct vertex_particle_buffer_data), /* stride */
		(void *)offsetof(struct vertex_particle_buffer_data, end_apm) /* array buffer offset */
	);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ptr->particle_index_buffer);
	glDrawElements(GL_TRIANGLES, ptr->nparticles * 6, GL_UNSIGNED_SHORT, NULL);

	glDisableVertexAttribArray(textured_particle_shader.multi_one_id);
	glDisableVertexAttribArray(textured_particle_shader.start_position_id);
	glDisableVertexAttribArray(textured_particle_shader.start_tint_color_id);
	glDisableVertexAttribArray(textured_particle_shader.start_apm_id);
	glDisableVertexAttribArray(textured_particle_shader.end_position_id);
	glDisableVertexAttribArray(textured_particle_shader.end_tint_color_id);
	glDisableVertexAttribArray(textured_particle_shader.end_apm_id);
	glUseProgram(0);

	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	if (draw_polygon_as_lines)
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDisable(GL_BLEND);

	if (draw_billboard_wireframe) {
		struct sng_color white = sng_get_color(WHITE);
		graph_dev_raster_line_mesh(e, &transform->mvp, e->m, &white);

		struct sng_color red = sng_get_color(RED);
		graph_dev_raster_point_cloud_mesh(&point_cloud_shader, &transform->mvp, e->m, &red, 1.0, 3.0, 0);
	}
}

extern int graph_dev_entity_render_order(struct entity_context *cx, struct entity *e)
{
	int does_blending = 0;

	if (!e->material_ptr)
		return GRAPH_DEV_RENDER_NEAR_TO_FAR;

	switch (e->material_ptr->type) {
	case MATERIAL_NEBULA:
	case MATERIAL_TEXTURED_PARTICLE:
	case MATERIAL_TEXTURED_PLANET_RING:
	case MATERIAL_TEXTURED_SHIELD:
		does_blending = 1;
		break;
	case MATERIAL_TEXTURE_MAPPED_UNLIT:
		does_blending = e->material_ptr->texture_mapped_unlit.do_blend;
		break;
	case MATERIAL_TEXTURE_CUBEMAP:
		does_blending = e->material_ptr->texture_cubemap.do_blend;
		break;
	}

	if (does_blending)
		return GRAPH_DEV_RENDER_FAR_TO_NEAR;
	else
		return GRAPH_DEV_RENDER_NEAR_TO_FAR;
}

void graph_dev_draw_entity(struct entity_context *cx, struct entity *e, union vec3 *eye_light_pos,
	const struct entity_transform *transform)
{
	const struct mat44 *mat_mvp = &transform->mvp;
	const struct mat44 *mat_mv = &transform->mv;
	const struct mat33 *mat_normal = &transform->normal;
	float ring_texture_v = 0.0f;
	float ring_inner_radius = 1.0f;
	float ring_outer_radius = 4.0f;
	struct sng_color atmosphere_color = { 0 };

	draw_vertex_buffer_2d();

	struct sng_color line_color = sng_get_color(e->color);

	if (e->material_ptr && e->material_ptr->type == MATERIAL_NEBULA) {
		graph_dev_draw_nebula(mat_mvp, mat_mv, mat_normal, e, eye_light_pos);
		return;
	}

	switch (e->m->geometry_mode) {
	case MESH_GEOMETRY_TRIANGLES:
	{
		struct camera_info *c = &cx->camera;

		int filled_triangle = ((c->renderer & FLATSHADING_RENDERER) || (c->renderer & BLACK_TRIS))
					&& !(e->render_style & RENDER_NO_FILL);
		int outline_triangle = (c->renderer & WIREFRAME_RENDERER)
					|| (e->render_style & RENDER_WIREFRAME);

		struct graph_dev_gl_textured_shader *tex_shader = 0;

		int atmosphere = 0;
		int do_cullface = 1;
		int do_blend = 0;
		struct sng_color texture_tint = { 1.0, 1.0, 1.0 };
		float texture_alpha = 1.0;
		GLuint texture_id = 0;
		GLuint emit_texture_id = 0;
		GLuint normalmap_id = -1;

		/* for sphere shadows */
		struct shadow_sphere_data shadow_sphere;
		vec3_init(&shadow_sphere.eye_pos, 0, 0, 0);
		shadow_sphere.r = 0;

		/* for annulus shadows */
		struct shadow_annulus_data shadow_annulus;
		shadow_annulus.texture_id = 0;
		vec3_init(&shadow_annulus.eye_pos, 0, 0, 0);
		shadow_annulus.r1 = 0;
		shadow_annulus.r2 = 0;
		shadow_annulus.tint_color = sng_get_color(WHITE);
		shadow_annulus.alpha = 1.0;

		struct graph_dev_gl_trans_wireframe_shader *wireframe_trans_shader = &trans_wireframe_shader;

		/* for clipping sphere */
		struct clip_sphere_data clip_sphere;
		vec3_init(&clip_sphere.eye_pos, 0, 0, 0);
		clip_sphere.r = 0;
		clip_sphere.radius_fade = 0;

		if (e->material_ptr) {
			switch (e->material_ptr->type) {
			case MATERIAL_TEXTURE_MAPPED: {
				tex_shader = &textured_lit_shader;

				struct material_texture_mapped *mt = &e->material_ptr->texture_mapped;
				texture_id = mt->texture_id;
				emit_texture_id = mt->emit_texture_id;

				if (emit_texture_id > 0)
					tex_shader = &textured_lit_emit_shader;
				else
					tex_shader = &textured_lit_shader;
				}
				break;
			case MATERIAL_TEXTURE_MAPPED_UNLIT: {
				tex_shader = &textured_shader;

				struct material_texture_mapped_unlit *mt =
						&e->material_ptr->texture_mapped_unlit;
				texture_id = mt->texture_id;
				do_cullface = mt->do_cullface;
				do_blend = mt->do_blend;
				texture_alpha = mt->alpha;
				texture_tint = mt->tint;
				}
				break;
			case MATERIAL_ATMOSPHERE: {
				do_blend = 1;
				texture_alpha = 0.5;
				atmosphere = 1;
				atmosphere_color.red = e->material_ptr->atmosphere.r;
				atmosphere_color.green = e->material_ptr->atmosphere.g;
				atmosphere_color.blue = e->material_ptr->atmosphere.b;
				break;
				}
			case MATERIAL_TEXTURE_CUBEMAP: {
				tex_shader = &textured_cubemap_lit_shader;

				struct material_texture_cubemap *mt = &e->material_ptr->texture_cubemap;
				texture_id = mt->texture_id;
				do_cullface = mt->do_cullface;
				do_blend = mt->do_blend;
				texture_alpha = mt->alpha;
				texture_tint = mt->tint;
				}
				break;
			case MATERIAL_TEXTURED_SHIELD: {
				struct material_textured_shield *mt = &e->material_ptr->textured_shield;
				texture_id = mt->texture_id;
				tex_shader = &textured_cubemap_shield_shader;
				do_blend = 1;
				texture_alpha = entity_get_alpha(e);
				do_cullface = 0;
				}
				break;
			case MATERIAL_TEXTURED_PLANET: {
				struct material_textured_planet *mt = &e->material_ptr->textured_planet;
				texture_id = mt->texture_id;
				normalmap_id = mt->normalmap_id;

				if (mt->ring_material && mt->ring_material->type == MATERIAL_TEXTURED_PLANET_RING) {
					if (normalmap_id == (GLuint) -1)
						tex_shader = &textured_cubemap_lit_with_annulus_shadow_shader;
					else
						tex_shader =
							&textured_cubemap_normal_mapped_lit_with_annulus_shadow_shader;

					struct material_textured_planet_ring *ring_mt =
						&mt->ring_material->textured_planet_ring;
					ring_texture_v = ring_mt->texture_v;
					ring_inner_radius = ring_mt->inner_radius;
					ring_outer_radius = ring_mt->outer_radius;

					shadow_annulus.texture_id = ring_mt->texture_id;
					shadow_annulus.tint_color = ring_mt->tint;
					shadow_annulus.alpha = ring_mt->alpha;

					/* ring is at the center of our mesh */
					union vec4 sphere_pos = { { 0, 0, 0, 1 } };
					mat44_x_vec4_into_vec3(mat_mv, &sphere_pos, &shadow_annulus.eye_pos);

					/* ring is the 2x to 3x of the planet scale, world space distance
					   is the same in eye space as the view matrix does not scale */
					shadow_annulus.r1 = vec3_cwise_max(&e->scale) *
									ring_inner_radius;
					shadow_annulus.r2 = vec3_cwise_max(&e->scale) *
									ring_outer_radius;
				} else {
					if (normalmap_id != (GLuint) -1)
						tex_shader = &textured_cubemap_lit_normal_map_shader;
					else
						tex_shader = &textured_cubemap_lit_shader;
				}
				}
				break;
			case MATERIAL_TEXTURED_PLANET_RING: {
				tex_shader = &textured_with_sphere_shadow_shader;

				struct material_textured_planet_ring *mt =
							&e->material_ptr->textured_planet_ring;
				texture_id = mt->texture_id;
				texture_alpha = mt->alpha;
				texture_tint = mt->tint;
				do_blend = 1;
				do_cullface = 0;
				ring_texture_v = mt->texture_v;
				ring_inner_radius = mt->inner_radius;
				ring_outer_radius = mt->outer_radius;

				/* planet is at the center of our mesh */
				union vec4 sphere_pos = { { 0, 0, 0, 1 } };
				mat44_x_vec4_into_vec3(mat_mv, &sphere_pos, &shadow_sphere.eye_pos);

				/* planet is the size of the ring scale, world space distance
				   is the same in eye space as the view matrix does not scale */
				shadow_sphere.r = vec3_cwise_max(&e->scale);
				}
				break;
			case MATERIAL_WIREFRAME_SPHERE_CLIP: {
				wireframe_trans_shader = &trans_wireframe_with_clip_sphere_shader;

				struct material_wireframe_sphere_clip *mt =
					&e->material_ptr->wireframe_sphere_clip;

				union vec4 clip_sphere_pos;
				vec4_init_vec3(&clip_sphere_pos, &mt->center->e_pos, 1);
				mat44_x_vec4_into_vec3_dff(transform->v, &clip_sphere_pos, &clip_sphere.eye_pos);

				clip_sphere.r = e->material_ptr->wireframe_sphere_clip.radius;
				clip_sphere.radius_fade = e->material_ptr->wireframe_sphere_clip.radius_fade;
				}
				break;
			}
		}

		if (filled_triangle) {
			struct sng_color triangle_color;
			if (cx->camera.renderer & BLACK_TRIS)
				triangle_color = sng_get_color(BLACK);
			else
				triangle_color = sng_get_color(240 + GRAY + (NSHADESOFGRAY * e->shadecolor) + 10);

			/* outline and filled */
			if (outline_triangle) {
				graph_dev_raster_filled_wireframe_mesh(mat_mvp, e->m, &line_color, &triangle_color);
			} else {
				if (tex_shader)
					graph_dev_raster_texture(tex_shader, mat_mvp, mat_mv, mat_normal, e->m,
						&texture_tint, texture_alpha, eye_light_pos, texture_id,
						emit_texture_id, normalmap_id, &shadow_sphere, &shadow_annulus,
						do_cullface, do_blend, ring_texture_v,
						ring_inner_radius, ring_outer_radius);
				else if (atmosphere)
					graph_dev_raster_atmosphere(mat_mvp, mat_mv, mat_normal,
						e->m, &atmosphere_color, eye_light_pos);
				else
					graph_dev_raster_single_color_lit(mat_mvp, mat_mv, mat_normal,
						e->m, &triangle_color, eye_light_pos);
			}
		} else if (outline_triangle) {
			graph_dev_raster_trans_wireframe_mesh(wireframe_trans_shader, mat_mvp, mat_mv,
				mat_normal, e->m, &line_color, &clip_sphere, 1);
		}

		if (draw_billboard_wireframe && e->material_ptr &&
				e->material_ptr->billboard_type != MATERIAL_BILLBOARD_TYPE_NONE) {
			struct sng_color white_color = sng_get_color(WHITE);
			graph_dev_raster_trans_wireframe_mesh(0, mat_mvp, mat_mv,
				mat_normal, e->m, &white_color, 0, 0);
		}
		break;
	}
	case MESH_GEOMETRY_LINES:
		graph_dev_raster_line_mesh(e, mat_mvp, e->m, &line_color);
		break;

	case MESH_GEOMETRY_POINTS: {
			int do_blend = 0;
			float alpha = 1.0;
			float point_size = 1.0;
			struct graph_dev_gl_point_cloud_shader *shader;

			if (e->material_ptr && e->material_ptr->type == MATERIAL_POINT_CLOUD_INTENSITY_NOISE)
				shader = &point_cloud_intensity_noise_shader;
			else
				shader = &point_cloud_shader;

			graph_dev_raster_point_cloud_mesh(shader, mat_mvp, e->m, &line_color, alpha, point_size,
				do_blend);
		}
		break;
	case MESH_GEOMETRY_PARTICLE_ANIMATION:
		if (e->material_ptr && e->material_ptr->type == MATERIAL_TEXTURED_PARTICLE) {
			struct material_textured_particle *mt = &e->material_ptr->textured_particle;

			graph_dev_raster_particle_animation(cx, e, transform, mt->texture_id,
				mt->radius * vec3_cwise_min(&e->scale), mt->time_base);
		}
		break;
	}
}

/* This implementation is ok for drawing a few times, but the performance
   will really suck as lines per frame goes up */
void graph_dev_draw_3d_line(struct entity_context *cx, const struct mat44 *mat_vp, const struct mat44 *mat_v,
	float x1, float y1, float z1, float x2, float y2, float z2)
{
	draw_vertex_buffer_2d();

	enable_3d_viewport();

	/* setup fake line entity to render this */
	struct vertex_buffer_data g_v_buffer_data[2];
	g_v_buffer_data[0].position.v.x = x1;
	g_v_buffer_data[0].position.v.y = y1;
	g_v_buffer_data[0].position.v.z = z1;
	g_v_buffer_data[1].position.v.x = x2;
	g_v_buffer_data[1].position.v.y = y2;
	g_v_buffer_data[1].position.v.z = z2;

	struct vertex_line_buffer_data g_vl_buffer_data[2];

	int is_dotted = 0;

	g_vl_buffer_data[0].multi_one[0] =
		g_vl_buffer_data[1].multi_one[0] = is_dotted ? 255 : 0;

	g_vl_buffer_data[0].line_vertex0.v.x =
		g_vl_buffer_data[1].line_vertex0.v.x = x1;
	g_vl_buffer_data[0].line_vertex0.v.y =
		g_vl_buffer_data[1].line_vertex0.v.y = y1;
	g_vl_buffer_data[0].line_vertex0.v.z =
		g_vl_buffer_data[1].line_vertex0.v.z = z1;

	g_vl_buffer_data[0].line_vertex1.v.x =
		g_vl_buffer_data[1].line_vertex1.v.x = x2;
	g_vl_buffer_data[0].line_vertex1.v.y =
		g_vl_buffer_data[1].line_vertex1.v.y = y2;
	g_vl_buffer_data[0].line_vertex1.v.z =
		g_vl_buffer_data[1].line_vertex1.v.z = z2;

	sgc.gl_info_3d_line.nlines = 1;

	glBindBuffer(GL_ARRAY_BUFFER, sgc.gl_info_3d_line.vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(g_v_buffer_data), g_v_buffer_data, GL_STREAM_DRAW);

	glBindBuffer(GL_ARRAY_BUFFER, sgc.gl_info_3d_line.line_vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(g_vl_buffer_data), g_vl_buffer_data, GL_STREAM_DRAW);

	struct mesh m;
	m.graph_ptr = &sgc.gl_info_3d_line;

	struct entity e;
	e.material_ptr = 0;
	e.m = &m;

	struct sng_color line_color = sng_get_foreground();
	graph_dev_raster_line_mesh(&e, mat_vp, &m, &line_color);
}

static void graph_dev_raster_fs_effect(struct graph_dev_gl_fs_effect_shader *shader, GLuint texture0_id,
	GLuint texture1_id, GLuint texture2_id, const struct sng_color *tint_color, float alpha)
{
	static const struct mat44 mat_identity = { { { 1, 0, 0, 0}, { 0, 1, 0, 0 }, { 0, 0, 1, 0}, { 0, 0, 0, 1} } };

	glUseProgram(shader->program_id);

	if (texture0_id > 0 && shader->texture0_id >= 0) {
		BIND_TEXTURE(GL_TEXTURE0, GL_TEXTURE_2D, texture0_id);
	}

	if (texture1_id > 0 && shader->texture1_id >= 0) {
		BIND_TEXTURE(GL_TEXTURE1, GL_TEXTURE_2D, texture1_id);
	}

	if (texture2_id > 0 && shader->texture2_id >= 0) {
		BIND_TEXTURE(GL_TEXTURE2, GL_TEXTURE_2D, texture2_id);
	}

	glUniformMatrix4fv(shader->mvp_matrix_id, 1, GL_FALSE, &mat_identity.m[0][0]);
	glUniform4f(shader->viewport_id, 1.0 / sgc.screen_x, 1.0 / sgc.screen_y,
		sgc.screen_x, sgc.screen_y);

	if (shader->tint_color_id > 0) {
		if (tint_color)
			glUniform4f(shader->tint_color_id, tint_color->red,
				tint_color->green, tint_color->blue, alpha);
		else
			glUniform4f(shader->tint_color_id, 1, 1, 1, 1);
	}

	glEnableVertexAttribArray(shader->vertex_position_id);
	glBindBuffer(GL_ARRAY_BUFFER, textured_unit_quad.vertex_buffer);
	glVertexAttribPointer(
		shader->vertex_position_id, /* The attribute we want to configure */
		3,                           /* size */
		GL_FLOAT,                    /* type */
		GL_FALSE,                    /* normalized? */
		sizeof(struct vertex_buffer_data), /* stride */
		(void *)offsetof(struct vertex_buffer_data, position.v.x) /* array buffer offset */
	);

	glEnableVertexAttribArray(shader->texture_coord_id);
	glBindBuffer(GL_ARRAY_BUFFER, textured_unit_quad.triangle_vertex_buffer);
	glVertexAttribPointer(
		shader->texture_coord_id,/* The attribute we want to configure */
		2,                            /* size */
		GL_FLOAT,                     /* type */
		GL_TRUE,                     /* normalized? */
		sizeof(struct vertex_triangle_buffer_data), /* stride */
		(void *)offsetof(struct vertex_triangle_buffer_data, texture_coord.v.x) /* array buffer offset */
	);

	glDrawArrays(GL_TRIANGLES, 0, textured_unit_quad.nvertices);

	glDisableVertexAttribArray(shader->vertex_position_id);
	glDisableVertexAttribArray(shader->texture_coord_id);
	glUseProgram(0);
}

void graph_dev_start_frame()
{
	/* reset viewport to whole screen */
	sgc.active_vp = 0;
	VIEWPORT(0, 0, sgc.screen_x, sgc.screen_y);

	if (draw_render_to_texture && render_target_2d.fbo > 0) {
		resize_fbo_if_needed(&render_target_2d);
		sgc.fbo_2d = render_target_2d.fbo;
		glBindFramebuffer(GL_FRAMEBUFFER, render_target_2d.fbo);
		glClear(GL_COLOR_BUFFER_BIT);
	} else
		sgc.fbo_2d = 0;

	if (draw_msaa_samples > 0 && msaa.fbo > 0) {

		glEnable(GL_MULTISAMPLE);

		glBindFramebuffer(GL_FRAMEBUFFER, msaa.fbo);
		sgc.fbo_3d = msaa.fbo;

		if (msaa.width != sgc.screen_x || msaa.height != sgc.screen_y || msaa.samples != draw_msaa_samples) {
			/* need to rebuild the fbo attachments */
			glBindRenderbuffer(GL_RENDERBUFFER, msaa.color0_buffer);
			glRenderbufferStorageMultisample(GL_RENDERBUFFER, draw_msaa_samples, GL_RGBA8,
				sgc.screen_x, sgc.screen_y);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
				msaa.color0_buffer);

			glBindRenderbuffer(GL_RENDERBUFFER, msaa.depth_buffer);
			glRenderbufferStorageMultisample(GL_RENDERBUFFER, draw_msaa_samples, GL_DEPTH_COMPONENT,
				sgc.screen_x, sgc.screen_y);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
				msaa.depth_buffer);

			msaa.width = sgc.screen_x;
			msaa.height = sgc.screen_y;
			msaa.samples = draw_msaa_samples;

			GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if (status != GL_FRAMEBUFFER_COMPLETE) {
				print_framebuffer_error();
			}
		}

	} else if (draw_render_to_texture && post_target0.fbo > 0) {

		resize_fbo_if_needed(&post_target0);

		glBindFramebuffer(GL_FRAMEBUFFER, post_target0.fbo);
		sgc.fbo_3d = post_target0.fbo;
	} else {
		/* render direct to back buffer */
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		sgc.fbo_3d = 0;
	}

	/* clear the bound 3d buffer */
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	sgc.fbo_current = sgc.fbo_3d;
}

void graph_dev_end_frame()
{
	/* printf("end frame\n"); */
	draw_vertex_buffer_2d();

	/* reset viewport to whole screen for final effects */
	VIEWPORT(0, 0, sgc.screen_x, sgc.screen_y);

	if (msaa.fbo != 0 && sgc.fbo_3d == msaa.fbo) {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa.fbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glBlitFramebuffer(0, 0, msaa.width, msaa.height, 0, 0,
			sgc.screen_x, sgc.screen_y, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		glDisable(GL_MULTISAMPLE);

	} else if (post_target0.fbo != 0 && sgc.fbo_3d == post_target0.fbo) {
		GLuint result_texture;

		if (draw_smaa) {
			/* do the multi stage smaa process:
				render to texture -> edge_fbo -> blend_fbo -> screen back buffer */
			resize_fbo_if_needed(&smaa_effect.edge_target);
			resize_fbo_if_needed(&smaa_effect.blend_target);
			resize_fbo_if_needed(&post_target1);

			/* edge detect pass - render into edge_fbo */
			glBindFramebuffer(GL_FRAMEBUFFER, smaa_effect.edge_target.fbo);
			glClear(GL_COLOR_BUFFER_BIT);
			graph_dev_raster_fs_effect(&smaa_effect.edge_shader, post_target0.color0_texture,
				0, 0, 0, 1);

			/* blend pass - render into blend_fbo */
			glBindFramebuffer(GL_FRAMEBUFFER, smaa_effect.blend_target.fbo);
			glClear(GL_COLOR_BUFFER_BIT);
			graph_dev_raster_fs_effect(&smaa_effect.blend_shader, smaa_effect.edge_target.color0_texture,
				smaa_effect.area_tex, smaa_effect.search_tex, 0, 1);

			/* eighborhood pass - render to back buffer */
			glBindFramebuffer(GL_FRAMEBUFFER, post_target1.fbo);
			glClear(GL_COLOR_BUFFER_BIT);
			graph_dev_raster_fs_effect(&smaa_effect.neighborhood_shader, post_target0.color0_texture,
				smaa_effect.blend_target.color0_texture, 0, 0, 1);

			if (draw_smaa_edge)
				result_texture = smaa_effect.edge_target.color0_texture;
			else if (draw_smaa_blend)
				result_texture = smaa_effect.blend_target.color0_texture;
			else
				result_texture = post_target1.color0_texture;
		} else {
			result_texture = post_target0.color0_texture;
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		graph_dev_raster_fs_effect(&fs_copy_shader, result_texture, 0, 0, 0, 1);
	}

	if (render_target_2d.fbo != 0 && sgc.fbo_2d == render_target_2d.fbo) {
		/* alpha blend copy 2d fbo onto back buffer */
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		glEnable(GL_BLEND);
		BLEND_FUNC(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		graph_dev_raster_fs_effect(&fs_copy_shader, render_target_2d.color0_texture, 0, 0, 0, 1);
		glDisable(GL_BLEND);
	}
}

void graph_dev_clear_depth_bit()
{
	glClear(GL_DEPTH_BUFFER_BIT);
}

void graph_dev_draw_line(float x1, float y1, float x2, float y2)
{
	make_room_in_vertex_buffer_2d(2);

	add_vertex_2d(x1, y1, sgc.hue, 255, GL_LINES);
	add_vertex_2d(x2, y2, sgc.hue, 255, GL_LINES);
}

void graph_dev_draw_rectangle(int filled, float x, float y, float width, float height)
{
	int x2, y2;

	x2 = x + width;
	y2 = y + height;

	if (filled ) {
		/* filled rectangle with two triangles
		  0 ------- 1
		    |\  1 |
		    | \   |
		    |  \  |
		    |   \ |
		    | 2  \|
		  2 ------- 3
		*/

		make_room_in_vertex_buffer_2d(6);

		/* triangle 1 = 0, 3, 1 */
		add_vertex_2d(x, y, sgc.hue, 255, GL_TRIANGLES);
		add_vertex_2d(x2, y2, sgc.hue, 255, GL_TRIANGLES);
		add_vertex_2d(x2, y, sgc.hue, 255, GL_TRIANGLES);

		/* triangle 2 = 0, 3, 2 */
		add_vertex_2d(x, y, sgc.hue, 255, GL_TRIANGLES);
		add_vertex_2d(x2, y2, sgc.hue, 255, GL_TRIANGLES);
		add_vertex_2d(x, y2, sgc.hue, 255, GL_TRIANGLES);
	} else {
		/* not filled */
		make_room_in_vertex_buffer_2d(5);

		add_vertex_2d(x, y, sgc.hue, 255, GL_LINE_STRIP);
		add_vertex_2d(x2, y, sgc.hue, 255, GL_LINE_STRIP);
		add_vertex_2d(x2, y2, sgc.hue, 255, GL_LINE_STRIP);
		add_vertex_2d(x, y2, sgc.hue, 255, GL_LINE_STRIP);
		add_vertex_2d(x, y, sgc.hue, 255, -1 /* primitive end */);
	}
}

void graph_dev_draw_point(float x, float y)
{
	make_room_in_vertex_buffer_2d(1);

	add_vertex_2d(x, y, sgc.hue, 255, GL_POINTS);
}

void graph_dev_draw_arc(int filled, float x, float y, float width, float height, float angle1, float angle2)
{
	float max_angle_delta = 2.0 * M_PI / 180.0; /*some ratio to height and width? */
	float rx = width/2.0;
	float ry = height/2.0;
	float cx = x + rx;
	float cy = y + ry;

	int i;

	int segments = (int)((angle2 - angle1) / max_angle_delta) + 1;
	float delta = (angle2 - angle1) / segments;

	GLubyte alpha = 255;

	if (sgc.alpha_blend) {
		/* must empty the vertex buffer to draw this primitive with blending */
		draw_vertex_buffer_2d();

		glEnable(GL_BLEND);
		BLEND_FUNC(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		alpha = 255 * sgc.alpha;
	}

	if (filled)
		make_room_in_vertex_buffer_2d(segments * 3);
	else
		make_room_in_vertex_buffer_2d(segments + 1);

	float x1 = 0, y1 = 0;
	for (i = 0; i <= segments; i++) {
		float a = angle1 + delta * (float)i;
		float x2 = cx + cos(a) * rx;
		float y2 = cy + sin(a) * ry;

		if (!filled || i > 0) {
			if (filled) {
				add_vertex_2d(x2, y2, sgc.hue, alpha, GL_TRIANGLES);
				add_vertex_2d(x1, y1, sgc.hue, alpha, GL_TRIANGLES);
				add_vertex_2d(cx, cy, sgc.hue, alpha, GL_TRIANGLES);
			} else {
				add_vertex_2d(x2, y2, sgc.hue, alpha, (i != segments ? GL_LINE_STRIP : -1));
			}
		}
		x1 = x2;
		y1 = y2;
	}

	if (sgc.alpha_blend) {
		/* must draw the vertex buffer to complete the blending */
		draw_vertex_buffer_2d();

		glDisable(GL_BLEND);
	}
}

static void setup_single_color_lit_shader(struct graph_dev_gl_single_color_lit_shader *shader)
{
	/* Create and compile our GLSL program from the shaders */
	shader->program_id = load_shaders(shader_directory,
				"single-color-lit-per-vertex.vert",
				"single-color-lit-per-vertex.frag",
				UNIVERSAL_SHADER_HEADER);

	/* Get a handle for our "MVP" uniform */
	shader->mvp_matrix_id = glGetUniformLocation(shader->program_id, "u_MVPMatrix");
	shader->mv_matrix_id = glGetUniformLocation(shader->program_id, "u_MVMatrix");
	shader->normal_matrix_id = glGetUniformLocation(shader->program_id, "u_NormalMatrix");
	shader->light_pos_id = glGetUniformLocation(shader->program_id, "u_LightPos");

	/* Get a handle for our buffers */
	shader->vertex_position_id = glGetAttribLocation(shader->program_id, "a_Position");
	shader->vertex_normal_id = glGetAttribLocation(shader->program_id, "a_Normal");
	shader->color_id = glGetUniformLocation(shader->program_id, "u_Color");
}

static void setup_atmosphere_shader(struct graph_dev_gl_atmosphere_shader *shader)
{
	/* Create and compile our GLSL program from the shaders */
	shader->program_id = load_shaders(shader_directory,
				"atmosphere.vert", "atmosphere.frag",
				UNIVERSAL_SHADER_HEADER);

	/* Get a handle for our "MVP" uniform */
	shader->mvp_matrix_id = glGetUniformLocation(shader->program_id, "u_MVPMatrix");
	shader->mv_matrix_id = glGetUniformLocation(shader->program_id, "u_MVMatrix");
	shader->normal_matrix_id = glGetUniformLocation(shader->program_id, "u_NormalMatrix");
	shader->light_pos_id = glGetUniformLocation(shader->program_id, "u_LightPos");

	/* Get a handle for our buffers */
	shader->vertex_position_id = glGetAttribLocation(shader->program_id, "a_Position");
	shader->vertex_normal_id = glGetAttribLocation(shader->program_id, "a_Normal");
	shader->color_id = glGetUniformLocation(shader->program_id, "u_Color");
}

static void setup_textured_shader(const char *basename, const char *defines,
	struct graph_dev_gl_textured_shader *shader)
{
	/* set all attributes to -1 */
	memset(shader, 0xff, sizeof(*shader));

	char vert_header[512];
	snprintf(vert_header, sizeof(vert_header), "%s\n#define INCLUDE_VS 1\n", defines);
	char frag_header[512];
	snprintf(frag_header, sizeof(frag_header), "%s\n#define INCLUDE_FS 1\n", defines);

	char shader_filename[PATH_MAX];
	snprintf(shader_filename, sizeof(shader_filename), "%s.shader", basename);

	const char *filenames[] = { shader_filename };

	shader->program_id = load_concat_shaders(shader_directory,
				vert_header, 1, filenames, frag_header, 1, filenames);
	glUseProgram(shader->program_id);

	shader->mvp_matrix_id = glGetUniformLocation(shader->program_id, "u_MVPMatrix");
	shader->mv_matrix_id = glGetUniformLocation(shader->program_id, "u_MVMatrix");
	shader->normal_matrix_id = glGetUniformLocation(shader->program_id, "u_NormalMatrix");
	shader->tint_color_id = glGetUniformLocation(shader->program_id, "u_TintColor");
	shader->light_pos_id = glGetUniformLocation(shader->program_id, "u_LightPos");
	shader->texture_2d_id = glGetUniformLocation(shader->program_id, "u_AlbedoTex");
	glUniform1i(shader->texture_2d_id, 0);
	shader->emit_texture_2d_id = glGetUniformLocation(shader->program_id, "u_EmitTex");
	if (shader->emit_texture_2d_id >= 0)
		glUniform1i(shader->emit_texture_2d_id, 1);
	shader->ring_texture_v_id = glGetUniformLocation(shader->program_id, "u_ring_texture_v");
	shader->ring_inner_radius_id = glGetUniformLocation(shader->program_id, "u_ring_inner_radius");
	if (shader->ring_inner_radius_id >= 0)
		glUniform1f(shader->ring_inner_radius_id, 2.0);
	shader->ring_outer_radius_id = glGetUniformLocation(shader->program_id, "u_ring_outer_radius");
	if (shader->ring_outer_radius_id >= 0)
		glUniform1f(shader->ring_outer_radius_id, 2.0);

	shader->vertex_position_id = glGetAttribLocation(shader->program_id, "a_Position");
	shader->vertex_normal_id = glGetAttribLocation(shader->program_id, "a_Normal");
	shader->texture_coord_id = glGetAttribLocation(shader->program_id, "a_TexCoord");

	shader->shadow_sphere_id = glGetUniformLocation(shader->program_id, "u_Sphere");
}

static void setup_textured_cubemap_shader(const char *basename, int use_normal_map,
					struct graph_dev_gl_textured_shader *shader)
{
	/* set all attributes to -1 */
	memset(shader, 0xff, sizeof(*shader));

	char vert_header[1024];
	char frag_header[1024];

	sprintf(vert_header, "%s\n%s\n%s\n",
		UNIVERSAL_SHADER_HEADER, "#define INCLUDE_VS 1\n",
			use_normal_map ? "#define USE_NORMAL_MAP 1\n" : "\n");
	sprintf(frag_header, "%s\n%s\n%s\n",
		UNIVERSAL_SHADER_HEADER, "#define INCLUDE_FS 1\n",
			use_normal_map ? "#define USE_NORMAL_MAP 1\n" : "\n");

	char shader_filename[PATH_MAX];
	snprintf(shader_filename, sizeof(shader_filename), "%s.shader", basename);

	const char *filenames[] = { shader_filename };

	shader->program_id = load_concat_shaders(shader_directory,
				vert_header, 1, filenames, frag_header, 1, filenames);
	glUseProgram(shader->program_id);

	/* Get a handle for our "MVP" uniform */
	shader->mvp_matrix_id = glGetUniformLocation(shader->program_id, "u_MVPMatrix");
	shader->mv_matrix_id = glGetUniformLocation(shader->program_id, "u_MVMatrix");
	shader->normal_matrix_id = glGetUniformLocation(shader->program_id, "u_NormalMatrix");
	shader->light_pos_id = glGetUniformLocation(shader->program_id, "u_LightPos");
	shader->texture_cubemap_id = glGetUniformLocation(shader->program_id, "u_AlbedoTex");
	glUniform1i(shader->texture_cubemap_id, 0);
	if (use_normal_map) {
		shader->normalmap_cubemap_id = glGetUniformLocation(shader->program_id, "u_NormalMapTex");
		glUniform1i(shader->normalmap_cubemap_id, 3); /* GL_TEXTURE3 */
	}
	shader->tint_color_id = glGetUniformLocation(shader->program_id, "u_TintColor");
	shader->ring_texture_v_id = glGetUniformLocation(shader->program_id, "u_ring_texture_v");
	if (shader->ring_texture_v_id >= 0)
		glUniform1f(shader->ring_texture_v_id, 0.25);
	shader->ring_inner_radius_id = glGetUniformLocation(shader->program_id, "u_ring_inner_radius");
	if (shader->ring_inner_radius_id >= 0)
		glUniform1f(shader->ring_inner_radius_id, 2.0);
	shader->ring_outer_radius_id = glGetUniformLocation(shader->program_id, "u_ring_outer_radius");
	if (shader->ring_outer_radius_id >= 0)
		glUniform1f(shader->ring_outer_radius_id, 2.0);

	/* Get a handle for our buffers */
	shader->vertex_position_id = glGetAttribLocation(shader->program_id, "a_Position");
	shader->vertex_normal_id = glGetAttribLocation(shader->program_id, "a_Normal");
	shader->vertex_tangent_id = glGetAttribLocation(shader->program_id, "a_Tangent");
	shader->vertex_bitangent_id = glGetAttribLocation(shader->program_id, "a_BiTangent");

	shader->shadow_annulus_texture_id = glGetUniformLocation(shader->program_id, "u_AnnulusAlbedoTex");
	glUniform1i(shader->shadow_annulus_texture_id, 1);
	shader->shadow_annulus_center_id = glGetUniformLocation(shader->program_id, "u_AnnulusCenter");
	shader->shadow_annulus_normal_id = glGetUniformLocation(shader->program_id, "u_AnnulusNormal");
	shader->shadow_annulus_radius_id = glGetUniformLocation(shader->program_id, "u_AnnulusRadius");
	shader->shadow_annulus_tint_color_id = glGetUniformLocation(shader->program_id, "u_AnnulusTintColor");
}

static void setup_filled_wireframe_shader(struct graph_dev_gl_filled_wireframe_shader *shader)
{
	/* Create and compile our GLSL program from the shaders */
	shader->program_id = load_shaders(shader_directory,
					"wireframe_filled.vert", "wireframe_filled.frag",
					UNIVERSAL_SHADER_HEADER);

	shader->viewport_id = glGetUniformLocation(shader->program_id, "Viewport");
	shader->mvp_matrix_id = glGetUniformLocation(shader->program_id, "ModelViewProjectionMatrix");

	shader->position_id = glGetAttribLocation(shader->program_id, "position");
	shader->tvertex0_id = glGetAttribLocation(shader->program_id, "tvertex0");
	shader->tvertex1_id = glGetAttribLocation(shader->program_id, "tvertex1");
	shader->tvertex2_id = glGetAttribLocation(shader->program_id, "tvertex2");
	shader->edge_mask_id = glGetAttribLocation(shader->program_id, "edge_mask");

	shader->line_color_id = glGetUniformLocation(shader->program_id, "line_color");
	shader->triangle_color_id = glGetUniformLocation(shader->program_id, "triangle_color");
}

static void setup_trans_wireframe_shader(const char *basename, struct graph_dev_gl_trans_wireframe_shader *shader)
{
	char vert_filename[PATH_MAX];
	char frag_filename[PATH_MAX];
	snprintf(vert_filename, sizeof(vert_filename), "%s.vert", basename);
	snprintf(frag_filename, sizeof(frag_filename), "%s.frag", basename);

	/* Create and compile our GLSL program from the shaders */
	shader->program_id = load_shaders(shader_directory, vert_filename, frag_filename,
						UNIVERSAL_SHADER_HEADER);

	shader->mvp_matrix_id = glGetUniformLocation(shader->program_id, "u_MVPMatrix");
	shader->mv_matrix_id = glGetUniformLocation(shader->program_id, "u_MVMatrix");
	shader->normal_matrix_id = glGetUniformLocation(shader->program_id, "u_NormalMatrix");
	shader->vertex_position_id = glGetAttribLocation(shader->program_id, "a_Position");
	shader->vertex_normal_id = glGetAttribLocation(shader->program_id, "a_Normal");
	shader->color_id = glGetUniformLocation(shader->program_id, "u_Color");
	shader->clip_sphere_id = glGetUniformLocation(shader->program_id, "u_ClipSphere");
	shader->clip_sphere_radius_fade_id = glGetUniformLocation(shader->program_id, "u_ClipSphereRadiusFade");
}

static void setup_single_color_shader(struct graph_dev_gl_single_color_shader *shader)
{
	/* Create and compile our GLSL program from the shaders */
	shader->program_id = load_shaders(shader_directory,
				"single_color.vert", "single_color.frag",
				UNIVERSAL_SHADER_HEADER);

	/* Get a handle for our "MVP" uniform */
	shader->mvp_matrix_id = glGetUniformLocation(shader->program_id, "u_MVPMatrix");

	/* Get a handle for our buffers */
	shader->vertex_position_id = glGetAttribLocation(shader->program_id, "a_Position");
	shader->color_id = glGetUniformLocation(shader->program_id, "u_Color");
}

static void setup_vertex_color_shader(struct graph_dev_gl_vertex_color_shader *shader)
{
	shader->program_id = load_shaders(shader_directory,
				"per_vertex_color.vert", "per_vertex_color.frag",
				UNIVERSAL_SHADER_HEADER);

	shader->mvp_matrix_id = glGetUniformLocation(shader->program_id, "u_MVPMatrix");

	shader->vertex_position_id = glGetAttribLocation(shader->program_id, "a_Position");
	shader->vertex_color_id = glGetAttribLocation(shader->program_id, "a_Color");
}

static void setup_line_single_color_shader(struct graph_dev_gl_line_single_color_shader *shader)
{
	/* Create and compile our GLSL program from the shaders */
	shader->program_id = load_shaders(shader_directory,
				"line-single-color.vert", "line-single-color.frag",
				UNIVERSAL_SHADER_HEADER);

	shader->mvp_matrix_id = glGetUniformLocation(shader->program_id, "u_MVPMatrix");
	shader->viewport_id = glGetUniformLocation(shader->program_id, "u_Viewport");
	shader->dot_size_id = glGetUniformLocation(shader->program_id, "u_DotSize");
	shader->dot_pitch_id = glGetUniformLocation(shader->program_id, "u_DotPitch");
	shader->line_color_id = glGetUniformLocation(shader->program_id, "u_LineColor");

	shader->multi_one_id = glGetAttribLocation(shader->program_id, "a_MultiOne");
	shader->vertex_position_id = glGetAttribLocation(shader->program_id, "a_Position");
	shader->line_vertex0_id = glGetAttribLocation(shader->program_id, "a_LineVertex0");
	shader->line_vertex1_id = glGetAttribLocation(shader->program_id, "a_LineVertex1");
}

static void setup_point_cloud_shader(const char *basename, struct graph_dev_gl_point_cloud_shader *shader)
{
	char vert_filename[PATH_MAX];
	char frag_filename[PATH_MAX];
	snprintf(vert_filename, sizeof(vert_filename), "%s.vert", basename);
	snprintf(frag_filename, sizeof(frag_filename), "%s.frag", basename);

	/* Create and compile our GLSL program from the shaders */
	shader->program_id = load_shaders(shader_directory, vert_filename, frag_filename,
				UNIVERSAL_SHADER_HEADER);

	/* Get a handle for our "MVP" uniform */
	shader->mvp_matrix_id = glGetUniformLocation(shader->program_id, "u_MVPMatrix");

	/* Get a handle for our buffers */
	shader->vertex_position_id = glGetAttribLocation(shader->program_id, "a_Position");
	shader->point_size_id = glGetUniformLocation(shader->program_id, "u_PointSize");
	shader->color_id = glGetUniformLocation(shader->program_id, "u_Color");
	shader->time_id = glGetUniformLocation(shader->program_id, "u_Time");
}

static void setup_color_by_w_shader(struct graph_dev_gl_color_by_w_shader *shader)
{
	/* Create and compile our GLSL program from the shaders */
	shader->program_id = load_shaders(shader_directory, "color_by_w.vert", "color_by_w.frag",
					UNIVERSAL_SHADER_HEADER);

	/* Get a handle for our "MVP" uniform */
	shader->mvp_id = glGetUniformLocation(shader->program_id, "u_MVPMatrix");

	/* Get a handle for our buffers */
	shader->position_id = glGetAttribLocation(shader->program_id, "a_Position");
	shader->near_color_id = glGetUniformLocation(shader->program_id, "u_NearColor");
	shader->near_w_id = glGetUniformLocation(shader->program_id, "u_NearW");
	shader->center_color_id = glGetUniformLocation(shader->program_id, "u_CenterColor");
	shader->center_w_id = glGetUniformLocation(shader->program_id, "u_CenterW");
	shader->far_color_id = glGetUniformLocation(shader->program_id, "u_FarColor");
	shader->far_w_id = glGetUniformLocation(shader->program_id, "u_FarW");
}


static void setup_skybox_shader(struct graph_dev_gl_skybox_shader *shader)
{
	/* Create and compile our GLSL program from the shaders */
	shader->program_id = load_shaders(shader_directory, "skybox.vert", "skybox.frag",
						UNIVERSAL_SHADER_HEADER);
	glUseProgram(shader->program_id);

	/* Get a handle for our "MVP" uniform */
	shader->mvp_id = glGetUniformLocation(shader->program_id, "MVP");
	shader->texture_id = glGetUniformLocation(shader->program_id, "texture");
	glUniform1i(shader->texture_id, 0);

	/* Get a handle for our buffers */
	shader->vertex_id = glGetAttribLocation(shader->program_id, "vertex");

	shader->texture_loaded = 0;
}

static void setup_cubemap_cube(struct graph_dev_primitive *obj)
{
	/* cube vertices in triangle strip for vertex buffer object */
	static const struct vertex_buffer_data cube_v_data[] = {
		{ .position = { { -10.0f,  10.0f, -10.0f } } },
		{ .position = { { -10.0f, -10.0f, -10.0f } } },
		{ .position = { { 10.0f, -10.0f, -10.0f } } },
		{ .position = { { 10.0f, -10.0f, -10.0f } } },
		{ .position = { { 10.0f,  10.0f, -10.0f } } },
		{ .position = { { -10.0f,  10.0f, -10.0f } } },

		{ .position = { { -10.0f, -10.0f,  10.0f } } },
		{ .position = { { -10.0f, -10.0f, -10.0f } } },
		{ .position = { { -10.0f,  10.0f, -10.0f } } },
		{ .position = { { -10.0f,  10.0f, -10.0f } } },
		{ .position = { { -10.0f,  10.0f,  10.0f } } },
		{ .position = { { -10.0f, -10.0f,  10.0f } } },

		{ .position = { { 10.0f, -10.0f, -10.0f } } },
		{ .position = { { 10.0f, -10.0f,  10.0f } } },
		{ .position = { { 10.0f,  10.0f,  10.0f } } },
		{ .position = { { 10.0f,  10.0f,  10.0f } } },
		{ .position = { { 10.0f,  10.0f, -10.0f } } },
		{ .position = { { 10.0f, -10.0f, -10.0f } } },

		{ .position = { { -10.0f, -10.0f,  10.0f } } },
		{ .position = { { -10.0f,  10.0f,  10.0f } } },
		{ .position = { { 10.0f,  10.0f,  10.0f } } },
		{ .position = { { 10.0f,  10.0f,  10.0f } } },
		{ .position = { { 10.0f, -10.0f,  10.0f } } },
		{ .position = { { -10.0f, -10.0f,  10.0f } } },

		{ .position = { { -10.0f,  10.0f, -10.0f } } },
		{ .position = { { 10.0f,  10.0f, -10.0f } } },
		{ .position = { { 10.0f,  10.0f,  10.0f } } },
		{ .position = { { 10.0f,  10.0f,  10.0f } } },
		{ .position = { { -10.0f,  10.0f,  10.0f } } },
		{ .position = { { -10.0f,  10.0f, -10.0f } } },

		{ .position = { { -10.0f, -10.0f, -10.0f } } },
		{ .position = { { -10.0f, -10.0f,  10.0f } } },
		{ .position = { { 10.0f, -10.0f, -10.0f } } },
		{ .position = { { 10.0f, -10.0f, -10.0f } } },
		{ .position = { { -10.0f, -10.0f,  10.0f } } },
		{ .position = { { 10.0f, -10.0f,  10.0f } } } };

	glGenBuffers(1, &obj->vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, obj->vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(cube_v_data), cube_v_data, GL_STATIC_DRAW);

	obj->nvertices = sizeof(cube_v_data)/sizeof(struct vertex_buffer_data);
}

static void setup_textured_unit_quad(struct graph_dev_primitive *obj)
{
	static const struct vertex_buffer_data quad_v_data[] = {
		{ { { -1.0f, -1.0f, 0.0f } } },
		{ { { 1.0f, 1.0f, 0.0f } } },
		{ { { -1.0f, 1.0f, 0.0f } } },

		{ { { -1.0f, -1.0f, 0.0f } } },
		{ { { 1.0f, -1.0f, 0.0f } } },
		{ { { 1.0f, 1.0f, 0.0f } } } };

	static const struct vertex_triangle_buffer_data quat_vt_data[] = {
		{ .texture_coord = { { 0.0f, 0.0f } } },
		{ .texture_coord = { { 1.0f, 1.0f } } },
		{ .texture_coord = { { 0.0f, 1.0f } } },

		{ .texture_coord = { { 0.0f, 0.0f } } },
		{ .texture_coord = { { 1.0f, 0.0f } } },
		{ .texture_coord = { { 1.0f, 1.0f } } } };

	glGenBuffers(1, &obj->vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, obj->vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad_v_data), quad_v_data, GL_STATIC_DRAW);

	glGenBuffers(1, &obj->triangle_vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, obj->triangle_vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quat_vt_data), quat_vt_data, GL_STATIC_DRAW);

	obj->nvertices = sizeof(quad_v_data)/sizeof(struct vertex_buffer_data);
}

static void setup_textured_particle_shader(struct graph_dev_gl_textured_particle_shader *shader)
{
	/* Create and compile our GLSL program from the shaders */
	shader->program_id = load_shaders(shader_directory,
				"textured-particle.vert", "textured-particle.frag",
				UNIVERSAL_SHADER_HEADER);
	glUseProgram(shader->program_id);

	shader->mvp_matrix_id = glGetUniformLocation(shader->program_id, "u_MVPMatrix");
	shader->camera_up_vec_id = glGetUniformLocation(shader->program_id, "u_CameraUpVec");
	shader->camera_right_vec_id = glGetUniformLocation(shader->program_id, "u_CameraRightVec");
	shader->time_id = glGetUniformLocation(shader->program_id, "u_Time");
	shader->radius_id = glGetUniformLocation(shader->program_id, "u_Radius");
	shader->texture_id = glGetUniformLocation(shader->program_id, "u_AlbedoTex");
	glUniform1i(shader->texture_id, 0);

	shader->multi_one_id = glGetAttribLocation(shader->program_id, "a_MultiOne");
	shader->start_position_id = glGetAttribLocation(shader->program_id, "a_StartPosition");
	shader->start_tint_color_id = glGetAttribLocation(shader->program_id, "a_StartTintColor");
	shader->start_apm_id = glGetAttribLocation(shader->program_id, "a_StartAPM");
	shader->end_position_id = glGetAttribLocation(shader->program_id, "a_EndPosition");
	shader->end_tint_color_id = glGetAttribLocation(shader->program_id, "a_EndTintColor");
	shader->end_apm_id = glGetAttribLocation(shader->program_id, "a_StartAPM");
}

static void setup_fs_effect_shader(const char *basename,
	struct graph_dev_gl_fs_effect_shader *shader)
{
	const char *vert_header =
		"#version 120\n"
		"#define INCLUDE_VS 1\n";
	const char *frag_header =
		"#version 120\n"
		"#define INCLUDE_FS 1\n";

	/* Create and compile our GLSL program from the shaders */
	char shader_filename[255];
	snprintf(shader_filename, sizeof(shader_filename), "%s.shader", basename);

	const char *filenames[] = { shader_filename };

	shader->program_id = load_concat_shaders(shader_directory, vert_header, 1, filenames,
		frag_header, 1, filenames);
	glUseProgram(shader->program_id);

	shader->mvp_matrix_id = glGetUniformLocation(shader->program_id, "u_MVPMatrix");
	shader->vertex_position_id = glGetAttribLocation(shader->program_id, "a_Position");
	shader->texture_coord_id = glGetAttribLocation(shader->program_id, "a_TexCoord");
	shader->tint_color_id = glGetUniformLocation(shader->program_id, "u_TintColor");
	shader->viewport_id = glGetUniformLocation(shader->program_id, "u_Viewport");
	shader->texture0_id = glGetUniformLocation(shader->program_id, "texture0Sampler");
	if (shader->texture0_id >= 0)
		glUniform1i(shader->texture0_id, 0);
	shader->texture1_id = glGetUniformLocation(shader->program_id, "texture1Sampler");
	if (shader->texture1_id >= 0)
		glUniform1i(shader->texture1_id, 1);
	shader->texture2_id = glGetUniformLocation(shader->program_id, "texture2Sampler");
	if (shader->texture2_id >= 0)
		glUniform1i(shader->texture2_id, 2);
}

static void setup_smaa_effect_shader(const char *basename, struct graph_dev_gl_fs_effect_shader *shader)
{
	char shader_filename[255];
	snprintf(shader_filename, sizeof(shader_filename), "%s.shader", basename);

	const char *vert_header;
	const char *frag_header;
	if (GLEW_VERSION_3_0) {
		vert_header =
			"#version 130\n"
			"#define INCLUDE_VS 1\n"
			"#define SMAA_GLSL_3\n";
		frag_header =
			"#version 130\n"
			"#define INCLUDE_FS 1\n"
			"#define SMAA_GLSL_3\n";
	} else {
		/* fall back to OGL 2.1 */
		vert_header =
			"#version 120\n"
			"#define INCLUDE_VS 1\n"
			"#define SMAA_GLSL_2\n";
		frag_header =
			"#version 120\n"
			"#define INCLUDE_FS 1\n"
			"#define SMAA_GLSL_2\n";
	}

	const char *filenames[] = { "smaa-high.shader", "SMAA.hlsl", shader_filename };

	shader->program_id = load_concat_shaders(shader_directory,
				vert_header, 3, filenames, frag_header, 3, filenames);

	shader->mvp_matrix_id = glGetUniformLocation(shader->program_id, "u_MVPMatrix");
	shader->vertex_position_id = glGetAttribLocation(shader->program_id, "a_Position");
	shader->texture_coord_id = glGetAttribLocation(shader->program_id, "a_TexCoord");
	shader->tint_color_id = -1;
	shader->viewport_id = glGetUniformLocation(shader->program_id, "u_Viewport");
	shader->texture0_id = -1;
	shader->texture1_id = -1;
	shader->texture2_id = -1;
}

static void setup_smaa_effect(struct graph_dev_smaa_effect *effect)
{
	struct graph_dev_gl_fs_effect_shader *shader;

	shader = &effect->edge_shader;
	setup_smaa_effect_shader("smaa-edge", shader);

	glUseProgram(shader->program_id);
	shader->texture0_id = glGetUniformLocation(shader->program_id, "u_AlbedoTex");
	glUniform1i(shader->texture0_id, 0);

	shader = &effect->blend_shader;
	setup_smaa_effect_shader("smaa-blend", shader);

	glUseProgram(shader->program_id);
	shader->texture0_id = glGetUniformLocation(shader->program_id, "u_EdgeTex");
	glUniform1i(shader->texture0_id, 0);
	shader->texture1_id = glGetUniformLocation(shader->program_id, "u_AreaTex");
	glUniform1i(shader->texture1_id, 1);
	shader->texture2_id = glGetUniformLocation(shader->program_id, "u_SearchTex");
	glUniform1i(shader->texture2_id, 2);

	shader = &effect->neighborhood_shader;
	setup_smaa_effect_shader("smaa-neighborhood", shader);

	glUseProgram(shader->program_id);
	shader->texture0_id = glGetUniformLocation(shader->program_id, "u_AlbedoTex");
	glUniform1i(shader->texture0_id, 0);
	shader->texture1_id = glGetUniformLocation(shader->program_id, "u_BlendTex");
	glUniform1i(shader->texture1_id, 1);

	glGenTextures(1, &effect->edge_target.color0_texture);
	glBindTexture(GL_TEXTURE_2D, effect->edge_target.color0_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glGenTextures(1, &effect->blend_target.color0_texture);
	glBindTexture(GL_TEXTURE_2D, effect->blend_target.color0_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	/* include file defines sizes and areaTexBytes of the area texture */
#include "share/snis/textures/AreaTex.h"

	glGenTextures(1, &effect->area_tex);
	glBindTexture(GL_TEXTURE_2D, effect->area_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE8_ALPHA8, (GLsizei)AREATEX_WIDTH, (GLsizei)AREATEX_HEIGHT, 0,
		GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, areaTexBytes);

	/* include file defines sizes and searchTexBytes of the search texture */
#include "share/snis/textures/SearchTex.h"

	glGenTextures(1, &effect->search_tex);
	glBindTexture(GL_TEXTURE_2D, effect->search_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, (GLsizei)SEARCHTEX_WIDTH, (GLsizei)SEARCHTEX_HEIGHT, 0,
		GL_RED, GL_UNSIGNED_BYTE, searchTexBytes);

	glGenFramebuffers(1, &effect->edge_target.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, effect->edge_target.fbo);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
		effect->edge_target.color0_texture, 0);

	glGenFramebuffers(1, &effect->blend_target.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, effect->blend_target.fbo);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
		effect->blend_target.color0_texture, 0);
}

static void setup_2d()
{
	memset(&render_target_2d, 0, sizeof(render_target_2d));

	glGenBuffers(1, &sgc.vertex_buffer_2d);
	glBindBuffer(GL_ARRAY_BUFFER, sgc.vertex_buffer_2d);
	glBufferData(GL_ARRAY_BUFFER, VERTEX_BUFFER_2D_SIZE, 0, GL_STREAM_DRAW);

	sgc.nvertex_2d = 0;

	/* render 2d to seperate fbo if supported */
	if (fbo_render_to_texture_supported()) {
		glGenTextures(1, &render_target_2d.color0_texture);
		glBindTexture(GL_TEXTURE_2D, render_target_2d.color0_texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glGenFramebuffers(1, &render_target_2d.fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, render_target_2d.fbo);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
			render_target_2d.color0_texture, 0);
	}
}

static void setup_3d()
{
	sgc.gl_info_3d_line.nlines = 0;

	glGenBuffers(1, &sgc.gl_info_3d_line.vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, sgc.gl_info_3d_line.vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, 0, 0, GL_STREAM_DRAW);

	glGenBuffers(1, &sgc.gl_info_3d_line.line_vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, sgc.gl_info_3d_line.line_vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, 0, 0, GL_STATIC_DRAW);

	sgc.texture_unit_active = 0;
	memset(sgc.texture_unit_bind, 0, sizeof(sgc.texture_unit_bind));
	sgc.src_blend_func = GL_ONE;
	sgc.dest_blend_func = GL_ZERO;
	sgc.vp_x = 0;
	sgc.vp_y = 0;
	sgc.vp_width = 0;
	sgc.vp_height = 0;
}

int graph_dev_setup(const char *shader_dir)
{
	glewExperimental = GL_TRUE; /* OSX apparently needs glewExperimental */

	if (glewInit() != GLEW_OK) {
		fprintf(stderr, "Failed to initialize GLEW\n");
		return -1;
	}
	if (!GLEW_VERSION_2_1) {
		fprintf(stderr, "Need atleast OpenGL 2.1\n");
		return -1;
	}
	printf("Initialized GLEW\n");

	if (GLEW_VERSION_3_0)
		printf("OpenGL 3.0 available\n");

	if (framebuffer_srgb_supported())
		printf("sRGB framebuffer supported\n");

	if (texture_srgb_supported())
		printf("sRGB texture supported\n");

	if (shader_dir) {
		if (shader_directory && shader_directory != default_shader_directory)
			free(shader_directory);
		shader_directory = strdup(shader_dir);
	} else {
		shader_directory = (char *) default_shader_directory;
	}

	glDepthFunc(GL_LESS);

	memset(&msaa, 0, sizeof(msaa));
	memset(&post_target0, 0, sizeof(post_target0));
	memset(&post_target1, 0, sizeof(post_target1));
	memset(&smaa_effect, 0, sizeof(smaa_effect));

	if (msaa_render_to_fbo_supported()) {
		glGenFramebuffers(1, &msaa.fbo);
		glGenRenderbuffers(1, &msaa.color0_buffer);
		glGenRenderbuffers(1, &msaa.depth_buffer);
		msaa.width = 0;
		msaa.height = 0;
		msaa.samples = 0;
	}

	if (fbo_render_to_texture_supported()) {
		glGenTextures(1, &post_target0.color0_texture);
		glBindTexture(GL_TEXTURE_2D, post_target0.color0_texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glGenRenderbuffers(1, &post_target0.depth_buffer);
		glBindRenderbuffer(GL_RENDERBUFFER, post_target0.depth_buffer);

		glGenFramebuffers(1, &post_target0.fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, post_target0.fbo);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
			post_target0.color0_texture, 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
			post_target0.depth_buffer);

		glGenTextures(1, &post_target1.color0_texture);
		glBindTexture(GL_TEXTURE_2D, post_target1.color0_texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glGenFramebuffers(1, &post_target1.fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, post_target1.fbo);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
			post_target1.color0_texture, 0);
	}

	setup_single_color_lit_shader(&single_color_lit_shader);
	setup_atmosphere_shader(&atmosphere_shader);
	setup_trans_wireframe_shader("wireframe_transparent", &trans_wireframe_shader);
	setup_trans_wireframe_shader("wireframe-transparent-sphere-clip", &trans_wireframe_with_clip_sphere_shader);
	setup_filled_wireframe_shader(&filled_wireframe_shader);
	setup_single_color_shader(&single_color_shader);
	setup_vertex_color_shader(&vertex_color_shader);
	setup_line_single_color_shader(&line_single_color_shader);
	setup_point_cloud_shader("point_cloud", &point_cloud_shader);
	setup_point_cloud_shader("point_cloud-intensity-noise", &point_cloud_intensity_noise_shader);
	setup_color_by_w_shader(&color_by_w_shader);
	setup_skybox_shader(&skybox_shader);
	setup_textured_shader("textured", UNIVERSAL_SHADER_HEADER, &textured_shader);
	setup_textured_shader("textured-with-sphere-shadow-per-pixel", UNIVERSAL_SHADER_HEADER,
				&textured_with_sphere_shadow_shader);
	setup_textured_shader("textured-and-lit-per-pixel", UNIVERSAL_SHADER_HEADER, &textured_lit_shader);
	setup_textured_shader("textured-and-lit-per-pixel", UNIVERSAL_SHADER_HEADER "#define USE_EMIT_MAP",
				&textured_lit_emit_shader);
	setup_textured_cubemap_shader("textured-cubemap-and-lit-per-pixel", 0, &textured_cubemap_lit_shader);
	setup_textured_cubemap_shader("textured-cubemap-and-lit-per-pixel", 1, &textured_cubemap_lit_normal_map_shader);
	setup_textured_cubemap_shader("textured-cubemap-shield-per-pixel", 0, &textured_cubemap_shield_shader);
	setup_textured_cubemap_shader("textured-cubemap-and-lit-with-annulus-shadow-per-pixel", 0,
		&textured_cubemap_lit_with_annulus_shadow_shader);
	setup_textured_cubemap_shader("textured-cubemap-and-lit-with-annulus-shadow-per-pixel", 1,
		&textured_cubemap_normal_mapped_lit_with_annulus_shadow_shader);
	setup_textured_particle_shader(&textured_particle_shader);
	setup_fs_effect_shader("fs-effect-copy", &fs_copy_shader);

	if (fbo_render_to_texture_supported())
		setup_smaa_effect(&smaa_effect);

	setup_cubemap_cube(&cubemap_cube);
	setup_textured_unit_quad(&textured_unit_quad);

	/* after all the shaders are loaded */
	setup_2d();
	setup_3d();

	return 0;
}

static void load_cubemap_texture_id(
	GLuint cube_texture_id,
	int is_inside,
	const char **tex_filenames)
{
	static const GLint tex_pos[] = {
		GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
		GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
		GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z };

	glBindTexture(GL_TEXTURE_CUBE_MAP, cube_texture_id);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	char whynotz[100];
	int whynotlen = 100;

	int i;
	for (i = 0; i < NCUBEMAP_TEXTURES; i++) {
		int tw, th, hasAlpha = 0;
		/* do horizontal invert if we are projecting on the inside */
		char *image_data = sng_load_png_texture(tex_filenames[i], 0, is_inside, 1, &tw, &th, &hasAlpha,
			whynotz, whynotlen);
		if (image_data) {
			glTexImage2D(tex_pos[i], 0, (hasAlpha ? GL_RGBA8 : GL_RGB8), tw, th, 0,
					(hasAlpha ? GL_RGBA : GL_RGB), GL_UNSIGNED_BYTE, image_data);
			free(image_data);
		} else {
			printf("Unable to load cubemap texture %d '%s': %s\n", i, tex_filenames[i], whynotz);
		}
	}
}

unsigned int graph_dev_load_cubemap_texture(
	int is_inside,
	const char *texture_filename_pos_x,
	const char *texture_filename_neg_x,
	const char *texture_filename_pos_y,
	const char *texture_filename_neg_y,
	const char *texture_filename_pos_z,
	const char *texture_filename_neg_z)
{
	const char *tex_filenames[] = {
		texture_filename_pos_x, texture_filename_neg_x,
		texture_filename_pos_y, texture_filename_neg_y,
		texture_filename_pos_z, texture_filename_neg_z };

	int i, j;
	for (i = 0; i < nloaded_cubemap_textures; i++) {
		if (loaded_cubemap_textures[i].is_inside == is_inside) {
			int match = 1;
			for (j = 0; j < NCUBEMAP_TEXTURES; j++) {
				if (strcmp(tex_filenames[j], loaded_cubemap_textures[i].filename[j]) != 0) {
					match = 0;
					break;
				}
			}
			if (match)
				return loaded_cubemap_textures[i].texture_id;
		}
	}

	if (nloaded_cubemap_textures >= MAX_LOADED_CUBEMAP_TEXTURES) {
		printf("Unable to load cubemap texture '%s': max of %d textures are already loaded\n",
			texture_filename_pos_x, nloaded_cubemap_textures);
		return 0;
	}

	GLuint cube_texture_id;
	glGenTextures(1, &cube_texture_id);

	load_cubemap_texture_id(cube_texture_id, is_inside, tex_filenames);

	loaded_cubemap_textures[nloaded_cubemap_textures].texture_id = cube_texture_id;
	loaded_cubemap_textures[nloaded_cubemap_textures].is_inside = is_inside;
	for (i = 0; i < NCUBEMAP_TEXTURES; i++)
		loaded_cubemap_textures[nloaded_cubemap_textures].filename[i] = strdup(tex_filenames[i]);
	nloaded_cubemap_textures++;

	return (unsigned int) cube_texture_id;
}

static time_t get_file_modify_time(const char *filename)
{
	struct stat s;
	if (stat(filename, &s) != 0)
		return 0;
	return s.st_mtime;
}

void graph_dev_reload_cubemap_textures()
{
	int i;
	for (i = 0; i < nloaded_cubemap_textures; i++) {
		load_cubemap_texture_id(loaded_cubemap_textures[i].texture_id,
			loaded_cubemap_textures[i].is_inside, (const char **)loaded_cubemap_textures[i].filename);
	}
}

static void load_texture_id(GLuint texture_number, const char *filename)
{
	char whynotz[100];
	int whynotlen = 100;
	int tw, th, hasAlpha = 1;

	glBindTexture(GL_TEXTURE_2D, texture_number);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	char *image_data = sng_load_png_texture(filename, 1, 0, 1, &tw, &th, &hasAlpha,
						whynotz, whynotlen);
	if (image_data) {
		glTexImage2D(GL_TEXTURE_2D, 0, (hasAlpha ? GL_RGBA8 : GL_RGB8), tw, th, 0,
				(hasAlpha ? GL_RGBA : GL_RGB), GL_UNSIGNED_BYTE, image_data);
		free(image_data);
	} else {
		printf("Unable to load texture '%s': %s\n", filename, whynotz);
	}
}


/* returning unsigned int instead of GLuint so as not to leak opengl types out
 * Kind of ugly, but should not be dangerous.
 */
unsigned int graph_dev_load_texture(const char *filename)
{
	int i;
	for (i = 0; i < nloaded_textures; i++) {
		if (strcmp(filename, loaded_textures[i].filename) == 0) {
			return loaded_textures[i].texture_id;
		}
	}

	if (nloaded_textures >= MAX_LOADED_TEXTURES) {
		printf("Unable to load texture '%s': max of %d textures are already loaded\n",
			filename, nloaded_textures);
		return 0;
	}

	GLuint texture_number;
	glGenTextures(1, &texture_number);

	load_texture_id(texture_number, filename);

	loaded_textures[nloaded_textures].texture_id = texture_number;
	loaded_textures[nloaded_textures].filename = strdup(filename);
	loaded_textures[nloaded_textures].mtime = get_file_modify_time(filename);
	loaded_textures[nloaded_textures].last_mtime_change = 0;

	nloaded_textures++;

	return (unsigned int) texture_number;
}

const char *graph_dev_get_texture_filename(unsigned int texture_id)
{
	int i;
	for (i = 0; i < nloaded_textures; i++) {
		if (texture_id == loaded_textures[i].texture_id) {
			return loaded_textures[i].filename;
		}
	}
	return "";
}

void graph_dev_reload_textures()
{
	int i;
	for (i = 0; i < nloaded_textures; i++) {
		load_texture_id(loaded_textures[i].texture_id, loaded_textures[i].filename);
	}
}

void graph_dev_reload_changed_textures()
{
	int i;
	for (i = 0; i < nloaded_textures; i++) {
		time_t mtime = get_file_modify_time(loaded_textures[i].filename);
		if (loaded_textures[i].mtime != mtime) {
			loaded_textures[i].mtime = mtime;
			loaded_textures[i].last_mtime_change = time_now_double();
		} else if (loaded_textures[i].last_mtime_change > 0 &&
			time_now_double() - loaded_textures[i].last_mtime_change >= TEX_RELOAD_DELAY) {
			printf("reloading texture '%s'\n", loaded_textures[i].filename);
			load_texture_id(loaded_textures[i].texture_id, loaded_textures[i].filename);
			loaded_textures[i].last_mtime_change = 0;
		}
	}
}

void graph_dev_reload_changed_cubemap_textures()
{
	int i;
	for (i = 0; i < nloaded_cubemap_textures; i++) {
		time_t mtime = get_file_modify_time(loaded_cubemap_textures[i].filename[5]);
		if (loaded_cubemap_textures[i].mtime != mtime) {
			loaded_cubemap_textures[i].mtime = mtime;
			loaded_cubemap_textures[i].last_mtime_change = time_now_double();
		} else if (loaded_cubemap_textures[i].last_mtime_change > 0 &&
			time_now_double() - loaded_cubemap_textures[i].last_mtime_change >=
					CUBEMAP_TEX_RELOAD_DELAY) {
			printf("reloading cubemap texture '%s'\n",
				loaded_cubemap_textures[i].filename[0]);
			load_cubemap_texture_id(loaded_cubemap_textures[i].texture_id,
					loaded_cubemap_textures[i].is_inside,
					(const char **)loaded_cubemap_textures[i].filename);
			loaded_cubemap_textures[i].last_mtime_change = 0;
		}
	}
}

void graph_dev_load_skybox_texture(
	const char *texture_filename_pos_x,
	const char *texture_filename_neg_x,
	const char *texture_filename_pos_y,
	const char *texture_filename_neg_y,
	const char *texture_filename_pos_z,
	const char *texture_filename_neg_z)
{
	if (skybox_shader.texture_loaded) {
		glDeleteTextures(1, &skybox_shader.cube_texture_id);
		skybox_shader.texture_loaded = 0;
	}

	skybox_shader.cube_texture_id = graph_dev_load_cubemap_texture(1, texture_filename_pos_x,
		texture_filename_neg_x, texture_filename_pos_y, texture_filename_neg_y, texture_filename_pos_z,
		texture_filename_neg_z);

	skybox_shader.texture_loaded = 1;
}

void graph_dev_draw_skybox(struct entity_context *cx, const struct mat44 *mat_vp)
{
	draw_vertex_buffer_2d();

	enable_3d_viewport();

	glDepthMask(GL_FALSE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	glUseProgram(skybox_shader.program_id);

	BIND_TEXTURE(GL_TEXTURE0, GL_TEXTURE_CUBE_MAP, skybox_shader.cube_texture_id);

	glUniformMatrix4fv(skybox_shader.mvp_id, 1, GL_FALSE, &mat_vp->m[0][0]);

	glEnableVertexAttribArray(skybox_shader.vertex_id);
	glBindBuffer(GL_ARRAY_BUFFER, cubemap_cube.vertex_buffer);
	glVertexAttribPointer(
		skybox_shader.vertex_id, /* The attribute we want to configure */
		3,                           /* size */
		GL_FLOAT,                    /* type */
		GL_FALSE,                    /* normalized? */
		sizeof(struct vertex_buffer_data), /* stride */
		(void *)offsetof(struct vertex_buffer_data, position.v.x) /* array buffer offset */
	);

	glDrawArrays(GL_TRIANGLES, 0, cubemap_cube.nvertices);

	glDisableVertexAttribArray(skybox_shader.vertex_id);
	glUseProgram(0);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
}

static void debug_menu_draw_item(char *item, int itemnumber, int grayed, int checked)
{
	int x = 15;
	int y = 35 + itemnumber * 20;

	if (grayed)
		sng_set_foreground(GRAY75);
	else
		sng_set_foreground(WHITE);

	graph_dev_draw_rectangle(0, x, y, 15, 15);
	if (checked)
		graph_dev_draw_rectangle(1, x + 2, y + 2, 11, 11);
	sng_abs_xy_draw_string(item, NANO_FONT, (x + 20) / sgc.x_scale, (y + 10) / sgc.y_scale);
}

void graph_dev_display_debug_menu_show()
{
	sng_set_foreground(BLACK);
	graph_dev_draw_rectangle(1, 10, 30, 250 * sgc.x_scale, 225);
	sng_set_foreground(WHITE);
	graph_dev_draw_rectangle(0, 10, 30, 250 * sgc.x_scale, 225);

	debug_menu_draw_item("VERTEX NORM/TAN/BITAN (RGB)", 0, 0, draw_normal_lines);
	debug_menu_draw_item("BILLBOARD WIREFRAME", 1, 0, draw_billboard_wireframe);
	debug_menu_draw_item("POLYGON AS LINE", 2, 0, draw_polygon_as_lines);
	debug_menu_draw_item("NO MSAA", 3, 0, draw_msaa_samples == 0);

	int max_samples = msaa_max_samples();
	debug_menu_draw_item("2x MSAA", 4, max_samples < 2, draw_msaa_samples == 2);
	debug_menu_draw_item("4x MSAA", 5, max_samples < 4, draw_msaa_samples == 4);
	debug_menu_draw_item("RENDER TO TEXTURE", 6, draw_msaa_samples > 0, draw_render_to_texture);
	debug_menu_draw_item("SMAA", 7, !draw_render_to_texture,
									draw_smaa);
	debug_menu_draw_item("SMAA DEBUG EDGE", 8, !draw_smaa, draw_smaa_edge);
	debug_menu_draw_item("SMAA DEBUG BLEND", 9, !draw_smaa, draw_smaa_blend);
	debug_menu_draw_item("PLANETARY ATMOSPHERES", 10, 0, draw_atmospheres);
}

static int selected_debug_item_checkbox(int n, int x, int y, int *toggle)
{
	if (x > 15 && x < 35 && y >= 35 + n * 20 && y <= 50 + n * 20) {
		if (toggle)
			*toggle = !*toggle;
		return 1;
	}
	return 0;
}

int graph_dev_graph_dev_debug_menu_click(int x, int y)
{
	if (selected_debug_item_checkbox(0, x, y, &draw_normal_lines))
		return 1;
	if (selected_debug_item_checkbox(1, x, y, &draw_billboard_wireframe))
		return 1;
	if (selected_debug_item_checkbox(2, x, y, &draw_polygon_as_lines))
		return 1;
	if (selected_debug_item_checkbox(3, x, y, NULL)) {
		draw_msaa_samples = 0;
		return 1;
	}
	if (selected_debug_item_checkbox(4, x, y, NULL)) {
		if (msaa_max_samples() >= 2)
			draw_msaa_samples = 2;
		return 1;
	}
	if (selected_debug_item_checkbox(5, x, y, NULL)) {
		if (msaa_max_samples() >= 4)
			draw_msaa_samples = 4;
		return 1;
	}
	if (selected_debug_item_checkbox(6, x, y, &draw_render_to_texture))
		return 1;
	if (selected_debug_item_checkbox(7, x, y, &draw_smaa))
		return 1;
	if (selected_debug_item_checkbox(8, x, y, &draw_smaa_edge)) {
		draw_smaa_blend = 0;
		return 1;
	}
	if (selected_debug_item_checkbox(9, x, y, &draw_smaa_blend)) {
		draw_smaa_edge = 0;
		return 1;
	}
	if (selected_debug_item_checkbox(10, x, y, &draw_atmospheres))
		return 1;
	return 0;
}

void graph_dev_grab_framebuffer(unsigned char **buffer, int *width, int *height)
{
	*buffer = malloc(3 * sgc.screen_x * sgc.screen_y);
	*width = sgc.screen_x;
	*height = sgc.screen_y;
	glReadPixels(0, 0, sgc.screen_x - 1, sgc.screen_y - 1,
			GL_RGB, GL_UNSIGNED_BYTE, *buffer);
}
