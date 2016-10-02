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


static void usage(int argc, char **argv)
{
	printf("Multimeter measurement\n\n");
	printf("\nUsage: %s [OPTIONS]\n", argv[0]);
	printf("\n\n"
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
			usage(argc, argv);
			return 1;
		default:
			return -1;
		}
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct es51984 *es51984 = NULL;
	struct es51984_sample sample;
	int ret = 1;
	int err;

	err = parse_args(argc, argv);
	if (err > 0)
		ret = 0;
	if (err)
		goto out;

	es51984 = es51984_init(ES51984_BOARD_AMPROBE_35XPA, "/dev/ttyUSB0");
	if (!es51984)
		goto out;
	err = es51984_sync(es51984);
	if (err) {
		fprintf(stderr, "Failed to sync to data stream.\n");
		goto out;
	}
	while (1) {
		err = es51984_get_sample(es51984, &sample, 1);
		if (err) {
			fprintf(stderr, "Failed to read sample.\n");
			goto out;
		}
	}

	ret = 0;
out:
	es51984_exit(es51984);

	return ret;
}
