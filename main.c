/*
 *   Multimeter measurement
 *
 *   Copyright (C) 2016 Michael Buesch <m@bues.ch>
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either version 2
 *   of the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include "es51984.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <getopt.h>


static struct {
	const char *dev;
} cmdline;


static int dump_es51984(enum es51984_board_type board, const char *dev)
{
	struct es51984 *es = NULL;
	struct es51984_sample sample;
	int ret = -ENODEV;
	int err;
	const char *units;

	es = es51984_init(board, dev);
	if (!es)
		goto out;
	err = es51984_sync(es);
	if (err) {
		fprintf(stderr, "Failed to sync to data stream.\n");
		goto out;
	}
	while (1) {
		err = es51984_get_sample(es, &sample, 1, 0);
		if (err) {
			fprintf(stderr, "ERROR: Failed to read sample.\n");
			continue;
		}
		if (sample.function == ES51984_FUNC_TEMP)
			units = sample.degree ? "*C" : "F";
		else
			units = es51984_get_units(&sample);
		printf("%.3lf %s%s  (%s, %s, %s)%s\n",
		       sample.overflow ? 0.0 : sample.value,
		       sample.overflow ? "OVERFLOW " : "",
		       units,
		       sample.dc_mode ? "DC" : "AC",
		       sample.auto_mode ? "auto" : "man",
		       sample.hold ? "hold" : "no-hold",
		       sample.batt_low ? " BATTERY LOW" : "");
	}

	ret = 0;
out:
	es51984_exit(es);

	return ret;
}

static void usage(void)
{
	printf("Multimeter measurement\n\n"
	       "  Usage: mmmeas [OPTIONS] DEVICE\n"
	       "\n"
	       "  DEVICE is the serial device node.\n"
	       "\n"
	       "Options:\n"
	       "  -h|--help            Print this help text\n"
	);
}

static int parse_args(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "help", no_argument, NULL, 'h', },
		{ NULL, },
	};

	int c, idx;

	while (1) {
		c = getopt_long(argc, argv, "h",
				long_options, &idx);
		if (c == -1)
			break;
		switch (c) {
		case 'h':
			usage();
			return 1;
		default:
			return -1;
		}
	}
	if (optind < argc)
		cmdline.dev = argv[optind++];
	if (optind < argc) {
		fprintf(stderr, "ERROR: Too many arguments\n\n");
		usage();
		return -1;
	}

	if (!cmdline.dev) {
		fprintf(stderr, "ERROR: DEVICE node missing\n\n");
		usage();
		return -1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	int ret = 1;
	int err;

	err = parse_args(argc, argv);
	if (err > 0)
		ret = 0;
	if (err)
		goto out;

	err = dump_es51984(ES51984_BOARD_AMPROBE_35XPA,
			   cmdline.dev);
	if (err)
		goto out;

	ret = 0;
out:

	return ret;
}
