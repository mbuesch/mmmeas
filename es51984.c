/*
 *   Cyrustek ES 51984 digital multimeter
 *   RS232 signal interpreter
 *
 *   Copyright (C) 2009-2016 Michael Buesch <m@bues.ch>
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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <float.h>
#include <assert.h>


#define PFX	"es51984: "


struct es51984 {
	enum es51984_board_type board;
	const char *tty;
	int fd;
	int synced;
	unsigned char sample_buf[11];
	unsigned int sample_ptr;
};

enum es51984_voltage_range {
	ES51984_VOLTRANGE_4p000		= ES51984_PACK(0), /* 4.000 V */
	ES51984_VOLTRANGE_40p00		= ES51984_PACK(1), /* 40.00 V */
	ES51984_VOLTRANGE_400p0		= ES51984_PACK(2), /* 400.0 V */
	ES51984_VOLTRANGE_4000		= ES51984_PACK(3), /* 4000 V */
	ES51984_VOLTRANGE_400p0m	= ES51984_PACK(4), /* 400.0 mV */
};

enum es51984_ma_range {
	ES51984_MARANGE_40p00		= ES51984_PACK(0), /* 40.00 mA */
	ES51984_MARANGE_400p0		= ES51984_PACK(1), /* 400.0 mA */
};

enum es51984_ua_range {
	ES51984_UARANGE_400p0		= ES51984_PACK(0), /* 400.0 uA */
	ES51984_UARANGE_4000		= ES51984_PACK(1), /* 4000 uA */
};

enum es51984_autoa_range {
	ES51984_AARANGE_4p000		= ES51984_PACK(0), /* 4.000 A */
	ES51984_AARANGE_40p00		= ES51984_PACK(1), /* 40.00 A */
};

enum es51984_ohm_range {
	ES51984_OHMRANGE_400p0		= ES51984_PACK(0), /* 400.0 Ohms */
	ES51984_OHMRANGE_4p000k		= ES51984_PACK(1), /* 4.000 Kilo-Ohms */
	ES51984_OHMRANGE_40p00k		= ES51984_PACK(2), /* 40.00 Kilo-Ohms */
	ES51984_OHMRANGE_400p0k		= ES51984_PACK(3), /* 400.0 Kilo-Ohms */
	ES51984_OHMRANGE_4p000m		= ES51984_PACK(4), /* 4.000 Mega-Ohms */
	ES51984_OHMRANGE_40p00m		= ES51984_PACK(5), /* 40.00 Mega-Ohms */
};

enum es51984_freq_range {
	ES51984_FREQRANGE_4p000k	= ES51984_PACK(0), /* 4.000 KHz */
	ES51984_FREQRANGE_40p00k	= ES51984_PACK(1), /* 40.00 KHz */
	ES51984_FREQRANGE_400p0k	= ES51984_PACK(2), /* 400.0 KHz */
	ES51984_FREQRANGE_4p000m	= ES51984_PACK(3), /* 4.000 MHz */
	ES51984_FREQRANGE_40p00m	= ES51984_PACK(4), /* 40.00 MHz */
};

enum es51984_cap_range {
	ES51984_CAPRANGE_4p000n		= ES51984_PACK(0), /* 4.000 nF */
	ES51984_CAPRANGE_40p00n		= ES51984_PACK(1), /* 40.00 nF */
	ES51984_CAPRANGE_400p0n		= ES51984_PACK(2), /* 400.0 nF */
	ES51984_CAPRANGE_4p000u		= ES51984_PACK(3), /* 4.000 uF */
	ES51984_CAPRANGE_40p00u		= ES51984_PACK(4), /* 40.00 uF */
	ES51984_CAPRANGE_400p0u		= ES51984_PACK(5), /* 400.0 uF */
	ES51984_CAPRANGE_4p000m		= ES51984_PACK(6), /* 4.000 mF */
};

struct es51984_raw_sample {
	uint8_t range;
	uint8_t digit3;
	uint8_t digit2;
	uint8_t digit1;
	uint8_t digit0;
	uint8_t function;
	uint8_t status;
	uint8_t option1;
	uint8_t option2;
	uint8_t cr;
	uint8_t lf;
} __attribute__((__packed__));

#define ES51984_DIGIT_MASK	0x0F
#define ES51984_STATUS_JUDGE	0x08
#define ES51984_STATUS_SIGN	0x04
#define ES51984_STATUS_BATT	0x02
#define ES51984_STATUS_OL	0x01
#define ES51984_OPT1_HOLD	0x08
#define ES51984_OPT2_DC		0x08
#define ES51984_OPT2_AC		0x04
#define ES51984_OPT2_AUTO	0x02


static void dump_raw_sample(const char *description,
			    struct es51984_raw_sample *raw)
{
	int valid_termination = (raw->cr == '\r' && raw->lf == '\n');

	printf("%s (%svalid termination):\n", description,
	       valid_termination ? "" : "in");
	printf("Function: %02X\n", raw->function);
	printf("Status:   %02X\n", raw->status);
	printf("Option1:  %02X\n", raw->option1);
	printf("Option2:  %02X\n", raw->option2);
	printf("Digits:   %02X %02X %02X %02X\n",
	       raw->digit3, raw->digit2, raw->digit1, raw->digit0);
}

static int set_blocking(struct es51984 *es, unsigned int block_chars)
{
	struct termios ios;
	int err;

	err = tcgetattr(es->fd, &ios);
	if (err < 0) {
		fprintf(stderr, PFX "Failed to get tty attributes on %s: %s\n",
			es->tty, strerror(errno));
		return -EIO;
	}
	ios.c_cc[VMIN] = block_chars;
	err = tcsetattr(es->fd, TCSANOW, &ios);
	if (err < 0) {
		fprintf(stderr, PFX "Failed to set tty attributes on %s: %s\n",
			es->tty, strerror(errno));
		return -EIO;
	}

	return 0;
}

static int read_sample(struct es51984 *es,
		       struct es51984_raw_sample **sample,
		       int blocking)
{
	struct timespec ts;
	ssize_t res;
	int err;

	assert(sizeof(es->sample_buf) == sizeof(struct es51984_raw_sample));

	err = set_blocking(es, blocking ? sizeof(struct es51984_raw_sample) : 0);
	if (err)
		return err;
	while (1) {
		res = read(es->fd, es->sample_buf + es->sample_ptr,
			   sizeof(struct es51984_raw_sample) - es->sample_ptr);
		if (res < 0)
			return -EIO;
		if (res == 0) {
			if (!blocking)
				return -EAGAIN;
			ts.tv_sec = 0;
			ts.tv_nsec = 1000*1000;
			nanosleep(&ts, &ts);
			continue;
		}
		es->sample_ptr += res;
		if (es->sample_ptr >= sizeof(struct es51984_raw_sample)) {
			*sample = (void *)es->sample_buf;
			es->sample_ptr = 0;
			break;
		}
		if (!blocking)
			return -EAGAIN;
	}

	return 0;
}

static int digits_sanity_check(struct es51984_raw_sample *raw)
{
	if ((raw->digit3 & 0xF0) != 0x30 ||
	    (raw->digit2 & 0xF0) != 0x30 ||
	    (raw->digit1 & 0xF0) != 0x30 ||
	    (raw->digit0 & 0xF0) != 0x30)
		return 1;
	if ((raw->digit3 & ES51984_DIGIT_MASK) > 4 ||
	    (raw->digit2 & ES51984_DIGIT_MASK) > 9 ||
	    (raw->digit1 & ES51984_DIGIT_MASK) > 9 ||
	    (raw->digit0 & ES51984_DIGIT_MASK) > 9)
		return 1;
	return 0; /* Digits are OK */
}

static void parse_4p000(struct es51984_raw_sample *raw,
			struct es51984_sample *sample)
{
	sample->value += (double)(raw->digit3 & ES51984_DIGIT_MASK);
	sample->value += (double)(raw->digit2 & ES51984_DIGIT_MASK) / 10.0l;
	sample->value += (double)(raw->digit1 & ES51984_DIGIT_MASK) / 100.0l;
	sample->value += (double)(raw->digit0 & ES51984_DIGIT_MASK) / 1000.0l;
}

static void parse_40p00(struct es51984_raw_sample *raw,
			struct es51984_sample *sample)
{
	sample->value += (double)(raw->digit3 & ES51984_DIGIT_MASK) * 10.0l;
	sample->value += (double)(raw->digit2 & ES51984_DIGIT_MASK);
	sample->value += (double)(raw->digit1 & ES51984_DIGIT_MASK) / 10.0l;
	sample->value += (double)(raw->digit0 & ES51984_DIGIT_MASK) / 100.0l;
}

static void parse_400p0(struct es51984_raw_sample *raw,
			struct es51984_sample *sample)
{
	sample->value += (double)(raw->digit3 & ES51984_DIGIT_MASK) * 100.0l;
	sample->value += (double)(raw->digit2 & ES51984_DIGIT_MASK) * 10.0l;
	sample->value += (double)(raw->digit1 & ES51984_DIGIT_MASK);
	sample->value += (double)(raw->digit0 & ES51984_DIGIT_MASK) / 10.0l;
}

static void parse_4000(struct es51984_raw_sample *raw,
		       struct es51984_sample *sample)
{
	sample->value += (double)(raw->digit3 & ES51984_DIGIT_MASK) * 1000.0l;
	sample->value += (double)(raw->digit2 & ES51984_DIGIT_MASK) * 100.0l;
	sample->value += (double)(raw->digit1 & ES51984_DIGIT_MASK) * 10.0l;
	sample->value += (double)(raw->digit0 & ES51984_DIGIT_MASK);
}

static int parse_sample(struct es51984 *es,
			struct es51984_raw_sample *raw,
			struct es51984_sample *sample)
{
	if (digits_sanity_check(raw)) {
		fprintf(stderr, PFX "Got invalid digits %02X %02X %02X %02X (func %02X)\n",
			raw->digit3, raw->digit2, raw->digit1, raw->digit0,
			raw->function);
		goto error;
	}

	switch (raw->function) {
	case ES51984_FUNC_VOLTAGE:
		switch (raw->range) {
		case ES51984_VOLTRANGE_4p000:
			parse_4p000(raw, sample);
			break;
		case ES51984_VOLTRANGE_40p00:
			parse_40p00(raw, sample);
			break;
		case ES51984_VOLTRANGE_400p0:
			parse_400p0(raw, sample);
			break;
		case ES51984_VOLTRANGE_4000:
			parse_4000(raw, sample);
			break;
		case ES51984_VOLTRANGE_400p0m:
			parse_400p0(raw, sample);
			sample->value /= 1000.0l;
			break;
		default:
			goto invalid_range;
		}
		break;
	case ES51984_FUNC_UA_CURRENT:
		switch (raw->range) {
		case ES51984_UARANGE_400p0:
			parse_400p0(raw, sample);
			break;
		case ES51984_UARANGE_4000:
			parse_4000(raw, sample);
			break;
		default:
			goto invalid_range;
		}
		break;
	case ES51984_FUNC_MA_CURRENT:
		switch (raw->range) {
		case ES51984_MARANGE_40p00:
			parse_40p00(raw, sample);
			break;
		case ES51984_MARANGE_400p0:
			parse_400p0(raw, sample);
			break;
		default:
			goto invalid_range;
		}
		break;
	case ES51984_FUNC_AUTO_CURRENT:
		switch (raw->range) {
		case ES51984_AARANGE_4p000:
			parse_4p000(raw, sample);
			break;
		case ES51984_AARANGE_40p00:
			parse_40p00(raw, sample);
			break;
		default:
			goto invalid_range;
		}
		break;
	case ES51984_FUNC_MAN_CURRENT:
		/* TODO: What range do we have here? */
		break;
	case ES51984_FUNC_OHMS:
		switch (raw->range) {
		case ES51984_OHMRANGE_400p0:
			parse_400p0(raw, sample);
			break;
		case ES51984_OHMRANGE_4p000k:
			parse_4p000(raw, sample);
			sample->value *= 1000.0l;
			break;
		case ES51984_OHMRANGE_40p00k:
			parse_40p00(raw, sample);
			sample->value *= 1000.0l;
			break;
		case ES51984_OHMRANGE_400p0k:
			parse_400p0(raw, sample);
			sample->value *= 1000.0l;
			break;
		case ES51984_OHMRANGE_4p000m:
			parse_4p000(raw, sample);
			sample->value *= 1000000.0l;
			break;
		case ES51984_OHMRANGE_40p00m:
			parse_40p00(raw, sample);
			sample->value *= 1000000.0l;
			break;
		default:
			goto invalid_range;
		}
		break;
	case ES51984_FUNC_CONT:
		parse_4000(raw, sample);
		break;
	case ES51984_FUNC_DIODE:
		parse_4p000(raw, sample);
		break;
	case ES51984_FUNC_FREQUENCY:
		switch (raw->range) {
		case ES51984_FREQRANGE_4p000k:
			parse_4p000(raw, sample);
			sample->value *= 1000.0l;
			break;
		case ES51984_FREQRANGE_40p00k:
			parse_40p00(raw, sample);
			sample->value *= 1000.0l;
			break;
		case ES51984_FREQRANGE_400p0k:
			parse_400p0(raw, sample);
			sample->value *= 1000.0l;
			break;
		case ES51984_FREQRANGE_4p000m:
			parse_4p000(raw, sample);
			sample->value *= 1000000.0l;
			break;
		case ES51984_FREQRANGE_40p00m:
			parse_40p00(raw, sample);
			sample->value *= 1000000.0l;
			break;
		default:
			goto invalid_range;
		}
		break;
	case ES51984_FUNC_CAPACITOR:
		switch (raw->range) {
		case ES51984_CAPRANGE_4p000n:
			parse_4p000(raw, sample);
			sample->value /= 1000000000.0l;
			break;
		case ES51984_CAPRANGE_40p00n:
			parse_40p00(raw, sample);
			sample->value /= 1000000000.0l;
			break;
		case ES51984_CAPRANGE_400p0n:
			parse_400p0(raw, sample);
			sample->value /= 1000000000.0l;
			break;
		case ES51984_CAPRANGE_4p000u:
			parse_4p000(raw, sample);
			sample->value /= 1000000.0l;
			break;
		case ES51984_CAPRANGE_40p00u:
			parse_40p00(raw, sample);
			sample->value /= 1000000.0l;
			break;
		case ES51984_CAPRANGE_400p0u:
			parse_400p0(raw, sample);
			sample->value /= 1000000.0l;
			break;
		case ES51984_CAPRANGE_4p000m:
			parse_4p000(raw, sample);
			sample->value /= 1000.0l;
			break;
		default:
			goto invalid_range;
		}
		break;
	case ES51984_FUNC_TEMP:
		/* TODO: What range do we have here? */
		break;
	case ES51984_FUNC_ADP0:
		switch (sample->board) {
		case ES51984_BOARD_UNKNOWN:
			sample->overflow = 1;
			sample->value = DBL_MAX;
			break;
		case ES51984_BOARD_AMPROBE_35XPA:
			parse_4000(raw, sample);
			break;
		}
		break;
	case ES51984_FUNC_ADP1:
		switch (sample->board) {
		case ES51984_BOARD_UNKNOWN:
			sample->overflow = 1;
			sample->value = DBL_MAX;
			break;
		case ES51984_BOARD_AMPROBE_35XPA:
			parse_400p0(raw, sample);
			break;
		}
		break;
	case ES51984_FUNC_ADP2:
		sample->overflow = 1;
		sample->value = DBL_MAX;
		break;
	case ES51984_FUNC_ADP3:
		sample->overflow = 1;
		sample->value = DBL_MAX;
		break;
	default:
		fprintf(stderr, PFX "Got invalid function code %02X\n",
			raw->function);
		goto error;
	}
	sample->function = raw->function;

	/* Parse status code */
	if ((raw->status & 0xF0) != 0x30) {
		fprintf(stderr, PFX "Got invalid status code %02X (func %02X)\n",
			raw->status, raw->function);
		goto error;
	}
	if (raw->function == ES51984_FUNC_TEMP) {
		if (raw->status & ES51984_STATUS_JUDGE)
			sample->degree = 1;
		else
			sample->degree = 0;
	}
	if (raw->status & ES51984_STATUS_OL) {
		sample->overflow = 1;
		sample->value = DBL_MAX;
	}
	if (raw->status & ES51984_STATUS_SIGN)
		sample->value = -(sample->value);
	if (raw->status & ES51984_STATUS_BATT)
		sample->batt_low = 1;

	/* Parse option1 code */
	if ((raw->option1 & 0xF0) != 0x30) {
		fprintf(stderr, PFX "Got invalid option1 code %02X (func %02X)\n",
			raw->option1, raw->function);
		goto error;
	}
	if (raw->option1 & ES51984_OPT1_HOLD)
		sample->hold = 1;

	/* Parse option2 code */
	if ((raw->option2 & 0xF0) != 0x30) {
		fprintf(stderr, PFX "Got invalid option2 code %02X (func %02X)\n",
			raw->option2, raw->function);
		goto error;
	}
	if (raw->option2 & ES51984_OPT2_DC)
		sample->dc_mode = 1;
	if (raw->option2 & ES51984_OPT2_AC)
		sample->dc_mode = 0;
	if (raw->option2 & ES51984_OPT2_AUTO)
		sample->auto_mode = 1;

	/* Verify CR/LF */
	if (raw->cr != '\r' || raw->lf != '\n') {
		fprintf(stderr, PFX "Got invalid packet termination %02X %02X\n",
			raw->cr, raw->lf);
		goto error;
	}

	return 0;

invalid_range:
	fprintf(stderr, PFX "Got an invalid range code %02X (func %02X)\n",
		raw->range, raw->function);
error:
	es->synced = 0; /* We lost synchronization */
	return -EPIPE;
}

int es51984_get_sample(struct es51984 *es,
		       struct es51984_sample *sample,
		       int blocking)
{
	struct es51984_raw_sample *raw;
	int err;

	if (!es->synced)
		return -EPIPE; /* Must sync first! */

	memset(sample, 0, sizeof(*sample));
	sample->value = 0.0;
	sample->board = es->board;

	err = read_sample(es, &raw, blocking);
	if (err)
		return err;
	dump_raw_sample("es51984_get_sample", raw);
	err = parse_sample(es, raw, sample);
	if (err)
		return err;

	return 0;
}

const char * es51984_get_units(const struct es51984_sample *sample)
{
	switch (sample->function) {
	case ES51984_FUNC_VOLTAGE:
		return "V";
	case ES51984_FUNC_UA_CURRENT:
		return "uA";
	case ES51984_FUNC_MA_CURRENT:
		return "mA";
	case ES51984_FUNC_AUTO_CURRENT:
		return "A";
	case ES51984_FUNC_MAN_CURRENT:
		return "A";
	case ES51984_FUNC_OHMS:
		return "Ohms";
	case ES51984_FUNC_CONT:
		return "Ohms";
	case ES51984_FUNC_DIODE:
		return "V";
	case ES51984_FUNC_FREQUENCY:
		return "Hz";
	case ES51984_FUNC_CAPACITOR:
		return "F";
	case ES51984_FUNC_TEMP:
		if (sample->degree)
			return "C";
		return "F";
	case ES51984_FUNC_ADP0:
		switch (sample->board) {
		case ES51984_BOARD_UNKNOWN:
			break;
		case ES51984_BOARD_AMPROBE_35XPA:
			return "C/F";
		}
		return "ADP0";
	case ES51984_FUNC_ADP1:
		switch (sample->board) {
		case ES51984_BOARD_UNKNOWN:
			break;
		case ES51984_BOARD_AMPROBE_35XPA:
			return "C/F";
		}
		return "ADP1";
	case ES51984_FUNC_ADP2:
		return "ADP2";
	case ES51984_FUNC_ADP3:
		return "ADP3";
	}
	return "Unknown units";
}

int es51984_discard(struct es51984 *es)
{
	struct es51984_sample sample;
	int err;

	if (!es->synced)
		return -EPIPE; /* Must sync first! */

	/* Read samples until the buffer is empty */
	while (1) {
		err = es51984_get_sample(es, &sample, 0);
		if (err == -EAGAIN)
			break; /* No more samples */
		if (err)
			return err;
	}
	if (es->sample_ptr) {
		/* We have a partially received sample. Discard it.
		 * Wait for the end of the sample. */
		err = es51984_sync(es);
		if (err)
			return err;
	}

	return 0;
}

static void tv_add_msec(struct timeval *tv, unsigned int msec)
{
	tv->tv_usec += msec * 1000;
	while (tv->tv_usec > 1000000) {
		tv->tv_sec++;
		tv->tv_usec -= 1000000;
	}
}

static int tv_after(const struct timeval *a, const struct timeval *b)
{
	if (a->tv_sec > b->tv_sec)
		return 1;
	if (a->tv_sec == b->tv_sec && a->tv_usec > b->tv_usec)
		return 1;
	return 0;
}

int es51984_sync(struct es51984 *es)
{
	ssize_t res;
	int err;
	unsigned char prev = 0, c;
	struct timeval to, tv;
	struct timespec ts;

	/* We sync to the final CR/LF sequence of the data stream. */

	err = gettimeofday(&to, NULL);
	if (err) {
		fprintf(stderr, PFX "Failed to get time of day: %s\n",
			strerror(errno));
		return -EIO;
	}
	switch (es->board) {
	case ES51984_BOARD_UNKNOWN:
	case ES51984_BOARD_AMPROBE_35XPA:
		tv_add_msec(&to, 3000); /* 3 seconds */
		break;
	}

	err = set_blocking(es, 0);
	if (err)
		return err;
	tcflush(es->fd, TCIFLUSH);
	while (1) {
		err = gettimeofday(&tv, NULL);
		if (err) {
			fprintf(stderr, PFX "Failed to get time of day: %s\n",
				strerror(errno));
			return -EIO;
		}
		if (tv_after(&tv, &to)) {
			fprintf(stderr, PFX "Sync: Timeout. Is the device connected?\n");
			return -ETIME;
		}
		res = read(es->fd, &c, 1);
		if (res == 0) {
			ts.tv_sec = 0;
			ts.tv_nsec = 1000*1000;
			nanosleep(&ts, &ts);
			continue;
		}
		if (res != 1) {
			fprintf(stderr, PFX "Sync: Read failed: %s\n",
				strerror(errno));
			return -EIO;
		}
		if (prev == '\r' && c == '\n') {
			/* Got it! */
			break;
		}
		prev = c;
	}
	es->synced = 1;
	es->sample_ptr = 0;

	return 0;
}

struct es51984 * es51984_init(enum es51984_board_type board,
			      const char *tty)
{
	struct es51984 *es;
	struct termios ios;
	int err;

	es = malloc(sizeof(*es));
	if (!es) {
		fprintf(stderr, "Out of memory\n");
		return NULL;
	}
	memset(es, 0, sizeof(*es));

	es->board = board;
	es->tty = tty;
	es->fd = open(tty, O_RDONLY | O_NOCTTY);
	if (es->fd < 0) {
		fprintf(stderr, PFX "Failed to open %s: %s\n",
			tty, strerror(errno));
		goto err_free;
	}

	err = tcgetattr(es->fd, &ios);
	if (err < 0) {
		fprintf(stderr, PFX "Failed to get tty attributes on %s: %s\n",
			tty, strerror(errno));
		goto err_close;
	}
	cfsetispeed(&ios, B19200);
	cfmakeraw(&ios);
	ios.c_cflag &= ~(CSIZE | CLOCAL | CREAD | CSTOPB | PARENB | PARODD);
	ios.c_cflag |= CS7 | CLOCAL | CREAD | PARENB | PARODD;
	ios.c_iflag &= ~(INPCK | PARMRK | IXON | IXOFF | BRKINT | INLCR | IGNCR | ICRNL | IUCLC | IMAXBEL | ISTRIP | IGNBRK | IGNPAR);
	ios.c_iflag |= IGNBRK;
	ios.c_lflag &= ~(NOFLSH | ECHO | ECHOE | ECHOK | ECHONL | XCASE | ECHOCTL | ECHOPRT | ECHOKE | PENDIN | ICANON | ISIG);
	ios.c_lflag |= 0;
	ios.c_cc[VMIN] = 0; /* non-blocking */
	err = tcsetattr(es->fd, TCSANOW, &ios);
	if (err < 0) {
		fprintf(stderr, PFX "Failed to set tty attributes on %s: %s\n",
			tty, strerror(errno));
		goto err_close;
	}
	err = tcflow(es->fd, TCION);
	if (err) {
		fprintf(stderr, PFX "Failed to enable input on %s: %s\n",
			tty, strerror(errno));
		goto err_close;
	}

	return es;

err_close:
	close(es->fd);
err_free:
	free(es);
	return NULL;
}

void es51984_exit(struct es51984 *es)
{
	if (!es)
		return;
	close(es->fd);
	free(es);
}
