/*
 *   Multimeter measurement
 *
 *   Copyright (C) 2016-2018 Michael Buesch <m@bues.ch>
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
#include <stdbool.h>
#include <getopt.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>


static struct {
	const char *dev;
	bool csv;
	bool timestamp;
	double sleep;
} cmdline;


static int timeval_msec_diff(const struct timeval *a, const struct timeval *b)
{
	int64_t usec_a, usec_b, usec_diff;

	usec_a = (int64_t)a->tv_sec * 1000000;
	usec_a += (int64_t)a->tv_usec;

	usec_b = (int64_t)b->tv_sec * 1000000;
	usec_b += (int64_t)b->tv_usec;

	usec_diff = usec_a - usec_b;

	return usec_diff / 1000;
}

static int dump_es51984(enum es51984_board_type board,
			const char *dev,
			bool csv,
			bool timestamp,
			double sleep)
{
	struct es51984 *es = NULL;
	struct es51984_sample sample;
	int ret = -ENODEV;
	int err;
	const char *units;
	double value;
	time_t t;
	struct tm tm;
	char tbuf[256];
	struct timeval tv, prev_tv = { 0 };
	int sleep_ms;

	sleep_ms = (int)round(sleep * 1000.0);

	es = es51984_init(board, dev);
	if (!es)
		goto out;
	err = es51984_sync(es);
	if (err) {
		fprintf(stderr, "Failed to sync to data stream.\n");
		goto out;
	}
	/* Discard first sample */
	err = es51984_get_sample(es, &sample, 1, 0);
	if (err) {
		fprintf(stderr, "ERROR: Failed to read sample.\n");
		goto out;
	}

	if (gettimeofday(&prev_tv, NULL)) {
		fprintf(stderr, "ERROR: gettimeofday() failed.\n");
		goto out;
	}
	while (1) {
		err = es51984_get_sample(es, &sample, 1, 0);
		if (err) {
			fprintf(stderr, "ERROR: Failed to read sample.\n");
			continue;
		}

		t = time(NULL);
		if (t == ((time_t)-1)) {
			fprintf(stderr, "ERROR: Failed to get time.\n");
			continue;
		}
		if (sleep_ms > 0) {
			if (gettimeofday(&tv, NULL)) {
				fprintf(stderr, "ERROR: gettimeofday() failed.\n");
				continue;
			}
			if (timeval_msec_diff(&tv, &prev_tv) < sleep_ms)
				continue;
			prev_tv = tv;
		}

		localtime_r(&t, &tm);
		strftime(tbuf, sizeof(tbuf), "%F;%T", &tm);

		if (sample.function == ES51984_FUNC_TEMP)
			units = sample.degree ? "*C" : "F";
		else
			units = es51984_get_units(&sample);
		value = sample.overflow ? 0.0 : sample.value;
		if (csv) {
			printf("%s%s%lf\n",
			       timestamp ? tbuf : "",
			       timestamp ? ";" : "",
			       value);
		} else {
			printf("%s%s%s%.3lf %s%s  (%s, %s, %s)%s\n",
			       timestamp ? "[" : "",
			       timestamp ? tbuf : "",
			       timestamp ? "] " : "",
			       value,
			       sample.overflow ? "OVERFLOW " : "",
			       units,
			       sample.dc_mode ? "DC" : "AC",
			       sample.auto_mode ? "auto" : "man",
			       sample.hold ? "hold" : "no-hold",
			       sample.batt_low ? " BATTERY LOW" : "");
		}
		fflush(stdout);
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
	       "  -c|--csv             Use CSV output\n"
	       "  -t|--timestamp       Print time stamps in output\n"
	       "  -s|--sleep SECONDS   Sleep and discard values between prints\n"
	       "  -h|--help            Print this help text\n"
	);
}

static int parse_args(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "csv", no_argument, NULL, 'c', },
		{ "timestamp", no_argument, NULL, 't', },
		{ "sleep", required_argument, NULL, 's', },
		{ "help", no_argument, NULL, 'h', },
		{ NULL, },
	};
	int c, idx;

	cmdline.dev = NULL;
	cmdline.csv = false;
	cmdline.timestamp = false;
	cmdline.sleep = 0.0;

	while (1) {
		c = getopt_long(argc, argv, "cts:h",
				long_options, &idx);
		if (c == -1)
			break;
		switch (c) {
		case 'c':
			cmdline.csv = true;
			break;
		case 't':
			cmdline.timestamp = true;
			break;
		case 's':
			if (sscanf(optarg, "%lf", &cmdline.sleep) != 1) {
				fprintf(stderr, "ERROR: Invalid --sleep value\n");
				return -1;
			}
			break;
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
			   cmdline.dev,
			   cmdline.csv,
			   cmdline.timestamp,
			   cmdline.sleep);
	if (err)
		goto out;

	ret = 0;
out:

	return ret;
}
