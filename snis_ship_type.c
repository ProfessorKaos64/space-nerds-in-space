#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>

#include "snis_ship_type.h"

struct ship_type_entry *snis_read_ship_types(char *filename, int *count)
{
	FILE *f;
	char line[255], class[255], model_file[PATH_MAX], thrust_attach[PATH_MAX];
	double toughness, max_speed;
	int warpchance;
	int crew_max;
	int ncargo_bays;
	char *x;
	int i, scancount;
	int integer;
	int linecount = 0, n = 0;
	int nalloced = 0;
	struct ship_type_entry *st;
	int nrots;
	char axis[4];
	float rot[4];
	int expected_count;

	nalloced = 30;
	st = malloc(sizeof(*st) * nalloced);
	if (!st)
		return NULL;

	f = fopen(filename, "r");
	if (!f)
		return NULL;

	while (!feof(f)) {
		x = fgets(line, sizeof(line) - 1, f);
		line[strlen(line) - 1] = '\0'; /* cut off trailing \n */
		if (!x) {
			if (feof(f))
				break;
		}
		linecount++;

		if (line[0] == '#') /* skip comment lines */
			continue;

		scancount = sscanf(line, "%s %s %s %lf %d %d %d %d %[xyzs] %f %[xyzs] %f %[xyzs] %f %[xyzs] %f\n",
				class, model_file, thrust_attach,
				&toughness, &integer, &warpchance, &crew_max, &ncargo_bays,
				&axis[0], &rot[0],
				&axis[1], &rot[1],
				&axis[2], &rot[2],
				&axis[3], &rot[3]);
		expected_count = 16;
		if (scancount == expected_count) {
			nrots = 4;
			goto done_scanfing_line;
		}
		expected_count -= 2;
		scancount = sscanf(line, "%s %s %s %lf %d %d %d %d %[xyzs] %f %[xyzs] %f %[xyzs] %f\n",
				class, model_file, thrust_attach,
				&toughness, &integer, &warpchance, &crew_max, &ncargo_bays,
				&axis[0], &rot[0],
				&axis[1], &rot[1],
				&axis[2], &rot[2]);
		if (scancount == expected_count) {
			nrots = 3;
			goto done_scanfing_line;
		}
		expected_count -= 2;
		scancount = sscanf(line, "%s %s %s %lf %d %d %d %d %[xyzs] %f %[xyzs] %f\n",
				class, model_file, thrust_attach,
				&toughness, &integer, &warpchance, &crew_max, &ncargo_bays,
				&axis[0], &rot[0], &axis[1], &rot[1]);
		if (scancount == expected_count) {
			nrots = 2;
			goto done_scanfing_line;
		}
		expected_count -= 2;
		scancount = sscanf(line, "%s %s %s %lf %d %d %d %d %[xyzs] %f\n",
				class, model_file, thrust_attach,
				&toughness, &integer, &warpchance, &crew_max, &ncargo_bays,
				&axis[0], &rot[0]);
		if (scancount == expected_count) {
			nrots = 1;
			goto done_scanfing_line;
		}
		expected_count -= 2;
		scancount = sscanf(line, "%s %s %s %lf %d %d %d %d\n", class, model_file,
					thrust_attach, &toughness, &integer, &warpchance,
					&crew_max, &ncargo_bays);
		if (scancount != expected_count) {
			fprintf(stderr, "Error at line %d in %s: '%s'\n",
				linecount, filename, line);
			if (scancount > 0)
				fprintf(stderr, "converted %d items\n", scancount);
			continue;
		}
		nrots = 0;

done_scanfing_line:

		for (i = 0; i < nrots; i++) {
			if (axis[i] != 'x' && axis[i] != 'y' && axis[i] != 'z' && axis[i] != 's') {
				fprintf(stderr, "Bad axis '%c' at line %d in %s: '%s'\n",
					axis[i], linecount, filename, line);
				axis[i] = 'x';
				rot[i] = 0.0;
			}
			if (axis[i] != 's')
				rot[i] = rot[i] * M_PI / 180.0;
		}

		max_speed = (double) integer / 100.0;
		if (toughness < 0.05) {
			fprintf(stderr, "%s:%d: toughness %lf for class %s out of range\n",
				filename, linecount, toughness, class);
			toughness = 0.05;
		}
		if (toughness > 1.0) {
			fprintf(stderr, "%s:%d: toughness %lf for class %s out of range\n",
				filename, linecount, toughness, class);
			toughness = 1.0;
		}

		/* exceeded allocated capacity */
		if (n >= nalloced) {
			struct ship_type_entry *newst;
			nalloced += 100;
			newst = realloc(st, nalloced * sizeof(*st));
			if (!newst) {
				fprintf(stderr, "out of memory at %s:%d\n", __FILE__, __LINE__);
				*count = n;
				return st;
			}
		}
		st[n].thrust_attachment_file = strdup(thrust_attach);
		st[n].class = strdup(class);
		if (!st[n].class) {
			fprintf(stderr, "out of memory at %s:%d\n", __FILE__, __LINE__);
			*count = n;
			return st;
		}
		st[n].model_file = strdup(model_file);
		if (!st[n].class) {
			fprintf(stderr, "out of memory at %s:%d\n", __FILE__, __LINE__);
			*count = n;
			return st;
		}
		st[n].toughness = toughness;
		st[n].max_shield_strength =
			(uint8_t) (255.0f * ((st[n].toughness * 0.8f) + 0.2f));
		st[n].max_speed = max_speed;
		st[n].warpchance = warpchance;
		st[n].crew_max = crew_max;
		st[n].ncargo_bays = ncargo_bays;

		st[n].nrotations = nrots;
		for (i = 0; i < nrots; i++) {
			st[n].axis[i] = axis[i];
			st[n].angle[i] = rot[i];
		}
		n++;
	}
	*count = n;
	return st;
}

void snis_free_ship_type(struct ship_type_entry *st, int count)
{
	int i;

	for (i = 0; i < count; i++)
		free(st[i].class);
	free(st);
}

