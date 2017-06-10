/*
	Copyright (C) 2015 Stephen M. Cameron
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
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "string-utils.h"
#include "solarsystem_config.h"

static void free_string_ptr(char **x)
{
	if (*x) {
		free(*x);
		*x = NULL;
	}
}

static char *get_field(char *line)
{
	char *i;

	for (i = line; *i != '\0' && *i != ':';)
		i++;
	if (*i == ':') {
		i++;
		return skip_leading_whitespace(i);
	}
	return i;
}

struct solarsystem_asset_spec *solarsystem_asset_spec_read(char *filename)
{
	FILE *f;
	struct solarsystem_asset_spec *a;
	char *field, *l, line[1000];
	int rc, ln = 0;
	int planet_textures_read = 0;
	int planet_textures_expected = 0;
	int got_position = 0;

	f = fopen(filename, "r");
	if (!f) {
		fprintf(stderr, "fopen: %s: %s\n", filename, strerror(errno));
		return NULL;
	}
	a = malloc(sizeof(*a));
	memset(a, 0, sizeof(*a));

	while (!feof(f)) {
		l = fgets(line, 1000, f);
		if (!l)
			break;
		trim_whitespace(line);
		ln++;

		if (line[0] == '#') /* skip comments */
			continue;
		clean_spaces(line);
		if (strcmp(line, "") == 0) /* skip blank lines */
			continue;
		if (has_prefix("planet texture count:", line)) {
			int value;

			field = get_field(line);
			rc = sscanf(field, "%d", &value);
			if (rc != 1) {
				fprintf(stderr, "%s:%d: failed to parse '%s' as integer, rc = %d\n",
					filename, ln, field, rc);
				goto bad_line;
			}
			/* FIXME: what should this limit really be? */
			if (value > PLANET_TYPE_COUNT_SHALL_BE) {
				fprintf(stderr, "%s:line %d: planet texture count %d exceeds max %d, capping\n",
						filename, ln, value, PLANET_TYPE_COUNT_SHALL_BE);
				value = PLANET_TYPE_COUNT_SHALL_BE;
			}
			planet_textures_expected = value;
			if (a->planet_texture != NULL || a->nplanet_textures != 0) {
				fprintf(stderr, "%s:line %d: multiple planet texture counts encountered, ignoring\n",
						filename, ln);
				goto bad_line;
			}
			a->nplanet_textures = PLANET_TYPE_COUNT_SHALL_BE;
			a->planet_texture = malloc(sizeof(a->planet_texture[0]) * PLANET_TYPE_COUNT_SHALL_BE);
			memset(a->planet_texture, 0, sizeof(a->planet_texture[0]) * PLANET_TYPE_COUNT_SHALL_BE);
			a->planet_normalmap = malloc(sizeof(a->planet_normalmap[0]) * PLANET_TYPE_COUNT_SHALL_BE);
			memset(a->planet_normalmap, 0, sizeof(a->planet_normalmap[0]) * PLANET_TYPE_COUNT_SHALL_BE);
			a->planet_type = malloc(sizeof(a->planet_type[0]) * PLANET_TYPE_COUNT_SHALL_BE);
			memset(a->planet_type, 0, sizeof(a->planet_type[0]) * PLANET_TYPE_COUNT_SHALL_BE);
			a->atmosphere_color = malloc(sizeof(a->atmosphere_color[0]) * PLANET_TYPE_COUNT_SHALL_BE);
			continue;
		} else if (has_prefix("planet texture:", line)) {
			if (a->nplanet_textures == 0) {
				fprintf(stderr, "%s:line %d: encountered planet texture before planet texture count\n",
						filename, ln);
				goto bad_line;
			}
			if (planet_textures_read >= a->nplanet_textures) {
				fprintf(stderr, "%s:line %d: too many planet textures.\n", filename, ln);
				goto bad_line;
			}
			char word1[1000], word2[1000], word3[1000];
			unsigned char r, g, b;
			field = get_field(line);
			rc = sscanf(field, "%s %s %s %hhu %hhu %hhu", word1, word2, word3, &r, &g, &b);
			if (rc == 6) {
				a->planet_texture[planet_textures_read] = strdup(word1);
				a->planet_normalmap[planet_textures_read] = strdup(word2);
				a->planet_type[planet_textures_read] = strdup(word3);
				a->atmosphere_color[planet_textures_read].r = r;
				a->atmosphere_color[planet_textures_read].g = g;
				a->atmosphere_color[planet_textures_read].b = b;
				planet_textures_read++;
				continue;
			}
			rc = sscanf(field, "%s %s %hhu %hhu %hhu", word1, word2, &r, &g, &b);
			if (rc == 5) {
				a->planet_texture[planet_textures_read] = strdup(word1);
				a->planet_type[planet_textures_read] = strdup(word2);
				a->planet_normalmap[planet_textures_read] = strdup("no-normal-map");
				a->atmosphere_color[planet_textures_read].r = r;
				a->atmosphere_color[planet_textures_read].g = g;
				a->atmosphere_color[planet_textures_read].b = b;
				planet_textures_read++;
				continue;
			}
			rc = sscanf(field, "%s %s %s", word1, word2, word3);
			if (rc == 3) {
				a->planet_texture[planet_textures_read] = strdup(word1);
				a->planet_normalmap[planet_textures_read] = strdup(word2);
				a->planet_type[planet_textures_read] = strdup(word3);
				a->atmosphere_color[planet_textures_read].r = 153;
				a->atmosphere_color[planet_textures_read].g = 153;
				a->atmosphere_color[planet_textures_read].b = 255;
				planet_textures_read++;
				continue;
			}
			rc = sscanf(field, "%s %s", word1, word2);
			if (rc == 2) { /* old style, no normal map */
				a->planet_texture[planet_textures_read] = strdup(word1);
				a->planet_normalmap[planet_textures_read] = strdup("no-normal-map");
				a->planet_type[planet_textures_read] = strdup(word2);
				a->atmosphere_color[planet_textures_read].r = 153;
				a->atmosphere_color[planet_textures_read].g = 153;
				a->atmosphere_color[planet_textures_read].b = 255;
				planet_textures_read++;
				fprintf(stderr,
					"%s:line %d: expected planet texture prefix, planet normal map prefix, and planet type\n",
					filename, ln);
				fprintf(stderr,
					"%s:line %d: Assuming old style without normal map (use no-normal-map to suppress this message).\n",
					filename, ln);
				continue;
			}
			fprintf(stderr,
				"%s:line %d: expected planet texture prefix, [ planet normal map prefix ], and planet type\n",
				filename, ln);
			goto bad_line;
		} else if (has_prefix("sun texture:", line)) {
			if (a->sun_texture != NULL) {
				fprintf(stderr, "%s:line %d: too many sun textures.\n", filename, ln);
				goto bad_line;
			}
			a->sun_texture = strdup(get_field(line));
			continue;
		} else if (has_prefix("skybox texture:", line)) {
			if (a->skybox_prefix != NULL) {
				fprintf(stderr, "%s:line %d: too many skybox prefixes.\n", filename, ln);
				goto bad_line;
			}
			a->skybox_prefix = strdup(get_field(line));
			continue;
		} else if (has_prefix("star location:", line)) {
			/* On the client, this info will be overridden by info from the lobby,
			 * On the server, this info is authoritative.
			 */
			double x, y, z;
			field = get_field(line);
			rc = sscanf(field, "%lf %lf %lf", &x, &y, &z);
			if (rc == 3) {
				a->x = x;
				a->y = y;
				a->z = z;
				got_position = 1;
			}
			continue;
		}
bad_line:
		fprintf(stderr, "solar system asset file %s:ignoring line %d:%s\n", filename, ln, line);
	}

	if (!got_position) {
		fprintf(stderr, "Solar system '%s' had no position information, using default.\n", filename);
		a->x = 0.0;
		a->y = 0.0;
		a->z = 0.0;
	}
	fclose(f);

	if (planet_textures_read <= 0) {
		fprintf(stderr, "%s: failed to read any planet types\n", filename);
		solarsystem_asset_spec_free(a);
		return NULL;
	}

	if (planet_textures_read < planet_textures_expected) {
		fprintf(stderr, "%s: expected %d planet types, but only found %d.\n",
			filename, planet_textures_expected, planet_textures_read);
	}

	if (planet_textures_read < PLANET_TYPE_COUNT_SHALL_BE) {
		int i, n;
		fprintf(stderr, "solar system asset file is short %d planet types, padding with duplicates\n",
			PLANET_TYPE_COUNT_SHALL_BE - planet_textures_read);

		n = PLANET_TYPE_COUNT_SHALL_BE - planet_textures_read;
		for (i = 0; i < n; i++)  {
			a->planet_texture[i + planet_textures_read] =
					strdup(a->planet_texture[i % planet_textures_read]);
			a->planet_normalmap[i + planet_textures_read] =
					strdup(a->planet_normalmap[i % planet_textures_read]);
			a->planet_type[i + planet_textures_read] =
					strdup(a->planet_type[i % planet_textures_read]);
		}
	}

	return a;
}

void solarsystem_asset_spec_free(struct solarsystem_asset_spec *s)
{
	int i;

	if (!s)
		return;
	free_string_ptr(&s->sun_texture);
	free_string_ptr(&s->skybox_prefix);
	for (i = 0; i < PLANET_TYPE_COUNT_SHALL_BE; i++) {
		free_string_ptr(&s->planet_texture[i]);
		free_string_ptr(&s->planet_normalmap[i]);
		free_string_ptr(&s->planet_type[i]);
	}
	free(s->planet_texture);
	free(s->planet_normalmap);
	free(s->planet_type);
	if (s->atmosphere_color)
		free(s->atmosphere_color);
	s->planet_texture = NULL;
	s->planet_normalmap = NULL;
	s->planet_type = NULL;
	s->atmosphere_color = NULL;
	free(s);
}

#ifdef SOLARSYSTEM_CONFIG_TEST

static void print_solarsystem_config(char *name, struct solarsystem_asset_spec *ss)
{
	int i;

	printf("-----------------------------\n");
	printf("Solarsystem %s:\n", name);
	printf("  Sun texture: %s\n", ss->sun_texture);
	printf("  skybox prefix: %s\n", ss->skybox_prefix);
	printf("  nplanet textures: %d\n", ss->nplanet_textures);

	for (i = 0; i < ss->nplanet_textures; i++) {
		printf("    planet_texture[%d]   : %s\n", i, ss->planet_texture[i]);
		printf("    planet_normalmap[%d] : %s\n", i, ss->planet_normalmap[i]);
		printf("    planet_type[%d] : %s\n", i, ss->planet_type[i]);
		printf("    planet  rgb[%d] : %hhu, %hhu, %hhu\n", i,
			ss->atmosphere_color[i].r, ss->atmosphere_color[i].g, ss->atmosphere_color[i].b);
	}
}

int main(int argc, char *argv[])
{
	struct solarsystem_asset_spec *ss;
	int i;

	for (i = 1; i < argc; i++) {
		printf("Reading solarsystem config file %s\n", argv[i]);
		ss = solarsystem_asset_spec_read(argv[i]);
		if (!ss) {
			fprintf(stderr, "Failed to read solarsystem config '%s'\n", argv[i]);
			continue;
		}
		print_solarsystem_config(argv[i], ss);
		solarsystem_asset_spec_free(ss);
		ss = NULL;
	}
	exit(0);
}

#endif
