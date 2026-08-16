/*
 * Emissivity Tables - text string identifying the type and its emissivity in integer
 * percent.
 *
 * Copyright 2020 Dan Julio
 *
 * This file is part of tCam.
 *
 * tCam is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * tCam is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with tCam.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#ifndef EMISSIVITY_TABLE_H
#define EMISSIVITY_TABLE_H



typedef const struct {
	char* item;
	int e;
} emissivity_table_t;


#define NUM_EMISSIVITY_ENTRIES 36

// Last item in the table has an e value of 0
emissivity_table_t const emissivity_table[NUM_EMISSIVITY_ENTRIES] = {
	{"Aluminum, polished", 5},
	{"Aluminum, oxidized", 25},
	{"Brass, tarnished", 22},
	{"Brass, polished", 3},
	{"Brick, common", 85},
	{"Brick, plastered", 94},
	{"Carbon", 96},
	{"Chipboard, untreated", 90},
	{"Clay, fired", 91},
	{"Concrete", 95},
	{"Elec Tape, Black", 96},
	{"Enamel", 90},
	{"Formica", 93},
	{"Soil", 93},
	{"Glass Pane", 97},
	{"Granite", 86},
	{"Iron, hot rolled", 77},
	{"Iron sheet, galvanized", 28},
	{"Lacquer, black", 97},
	{"Lacquer, white", 87},
	{"Lead, oxidized", 63},
	{"Leather, tanned", 77},
	{"Oil, thick", 82},
	{"Paint, oil, avg", 94},
	{"Paper, white", 90},
	{"Plasterboard", 90},
	{"Plastic, PCB", 91},
	{"Plastic, PVC", 93},
	{"Porcelain, glazed", 92},
	{"Rubber", 94},
	{"Snow", 80},
	{"Steel, rolled", 50},
	{"Tar Paper", 92},
	{"Varnish, oak floor", 92},
	{"Water", 98},
	{"Wood, plywood", 82}
};

#endif /* EMISSIVITY_TABLE_H */