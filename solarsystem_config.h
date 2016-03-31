#ifndef SOLARSYSTEM_CONFIG__
#define SOLARSYSTEM_CONFIG__
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

struct solarsystem_asset_spec {
	char *sun_texture;
	char *skybox_prefix;
	int nplanet_textures;
	char **planet_texture;
	char **planet_normalmap;
	char **planet_type;
};

struct solarsystem_asset_spec *solarsystem_asset_spec_read(char *filename);
void solarsystem_asset_spec_free(struct solarsystem_asset_spec *s);

#endif
