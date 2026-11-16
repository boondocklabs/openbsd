/*	$OpenBSD: octrtc.c,v 1.15 2022/10/12 13:39:50 kettenis Exp $	*/

/*
 * Copyright (c) 2013, 2014 Paul Irofti.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "octiic.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/proc.h>

#include <dev/clock_subr.h>
#include <dev/i2c/i2cvar.h>

#if NOCTIIC > 0
extern struct i2c_controller *octiic_rtc_tag;
#endif
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/octeonvar.h>

#ifdef OCTRTC_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

#define MIO_TWS_SW_TWSI		0x0001180000001000ULL
#define MIO_TWS_SW_TWSI_EXT	0x0001180000001018ULL
#define OCTRTC_REG	0x68
#define DS1338_CH	0x80	/* Seconds register */
#define DS1338_CTRL	7
#define DS1338_OSF	0x20

struct octrtc_softc {
	struct device			sc_dev;
	struct todr_chip_handle		sc_todr;
	int				sc_ds1338;
};

struct cfdriver octrtc_cd = {
	NULL, "octrtc", DV_DULL
};

int	octrtc_match(struct device *, void *, void *);
void	octrtc_attach(struct device *, struct device *, void *);

int	octrtc_gettime(struct todr_chip_handle *, struct timeval *);
int	octrtc_read(uint8_t *, int);

int	octrtc_settime(struct todr_chip_handle *, struct timeval *);
int	octrtc_write(int, uint8_t);


const struct cfattach octrtc_ca = {
	sizeof(struct octrtc_softc), octrtc_match, octrtc_attach,
};


union mio_tws_sw_twsi_reg {
	uint64_t reg;
	struct cvmx_mio_twsx_sw_twsi_s {
		uint64_t v:1;		/* Valid bit */
		uint64_t slonly:1;	/* Slave Only Mode */
		uint64_t eia:1;		/* Extended Internal Address */
		uint64_t op:4;		/* Opcode field */
		uint64_t r:1;		/* Read bit or result */
		uint64_t sovr:1;	/* Size Override */
		uint64_t size:3;	/* Size in bytes */
		uint64_t scr:2;		/* Scratch, unused */
		uint64_t a:10;		/* Address field */
		uint64_t ia:5;		/* Internal Address */
		uint64_t eop_ia:3;	/* Extra opcode */
		uint64_t d:32;		/* Data Field */
	} field;
};


static const enum octeon_board no_rtc_boards[] = {
	BOARD_UBIQUITI_E100,
	BOARD_UBIQUITI_E120,
	BOARD_UBIQUITI_E200,
	BOARD_UBIQUITI_E220,
	BOARD_UBIQUITI_E300,
	BOARD_UBIQUITI_E1000,
	BOARD_RHINOLABS_UTM8,
};

int
octrtc_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *maa = aux;
	struct cfdata *cf = match;
	int i;

	if (strcmp(maa->maa_name, cf->cf_driver->cd_name) != 0)
		return 0;
	for (i = 0; i < nitems(no_rtc_boards); i++)
		if (octeon_board == no_rtc_boards[i])
			return 0;
	return 1;
}

void
octrtc_attach(struct device *parent, struct device *self, void *aux)
{
	struct octrtc_softc *sc = (struct octrtc_softc *)self;

	int root = OF_finddevice("/");

	sc->sc_ds1338 = root != -1 && OF_is_compatible(root, "pan,pa-220");
	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = octrtc_gettime;
	sc->sc_todr.todr_settime = octrtc_settime;
	sc->sc_todr.todr_quality = 0;
	todr_attach(&sc->sc_todr);

	printf(": %s\n", sc->sc_ds1338 ? "DS1338" : "DS1337");
}

int
octrtc_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct octrtc_softc *sc = handle->cookie;
	struct clock_ymdhms dt;
	uint8_t tod[8];
	uint8_t check;
	int i, rc;

	int nretries = 2;

	DPRINTF(("\nTOD: "));
	while (nretries--) {
		rc = octrtc_read(&tod[0], 0);	/* ia read */
		if (rc) {
			DPRINTF(("octrtc_read(0) failed %d", rc));
			return EIO;
		}

		for (i = 1; i < 8; i++) {
			rc = octrtc_read(&tod[i], sc->sc_ds1338 ? i : -1);
			if (rc) {
				DPRINTF(("octrtc_read(%d) failed %d", i, rc));
				return EIO;
			}
			DPRINTF(("%#X ", tod[i]));
		}

		/* Check against time-wrap */
		rc = octrtc_read(&check, 0);	/* ia read */
		if (rc) {
			DPRINTF(("octrtc_read(check) failed %d", rc));
			return EIO;
		}
		if (check == tod[0])
			break;
	}
	if (nretries < 0)
		return EIO;
	if (sc->sc_ds1338 && ((tod[0] & DS1338_CH) ||
	    (tod[DS1338_CTRL] & DS1338_OSF)))
		return EINVAL;
	DPRINTF(("\n"));

	DPRINTF(("Time: %d %d %d (%d) %02d:%02d:%02d\n",
	    (sc->sc_ds1338 || (tod[5] & 0x80) ? 2000 : 1900) + FROMBCD(tod[6]),	/* year */
	    FROMBCD(tod[5] & 0x1f),	/* month */
	    FROMBCD(tod[4] & 0x3f),	/* day */
	    (tod[3] & 0x7),		/* day of the week */
	    FROMBCD(tod[2] & 0x3f),	/* hour */
	    FROMBCD(tod[1] & 0x7f),	/* minute */
	    FROMBCD(tod[0] & 0x7f)));	/* second */

	dt.dt_year = (sc->sc_ds1338 || (tod[5] & 0x80) ? 2000 : 1900) + FROMBCD(tod[6]);
	dt.dt_mon = FROMBCD(tod[5] & 0x1f);
	dt.dt_day = FROMBCD(tod[4] & 0x3f);
	if (tod[2] & 0x40)
		dt.dt_hour = FROMBCD(tod[2] & 0x1f) % 12 +
		    ((tod[2] & 0x20) ? 12 : 0);
	else
		dt.dt_hour = FROMBCD(tod[2] & 0x3f);
	dt.dt_min = FROMBCD(tod[1] & 0x7f);
	dt.dt_sec = FROMBCD(tod[0] & 0x7f);

	if (dt.dt_sec > 59 || dt.dt_min > 59 || dt.dt_hour > 23 ||
	    dt.dt_day > 31 || dt.dt_day == 0 ||
	    dt.dt_mon > 12 || dt.dt_mon == 0 ||
	    dt.dt_year < POSIX_BASE_YEAR)
		return EINVAL;

	tv->tv_sec = clock_ymdhms_to_secs(&dt);
	tv->tv_usec = 0;
	return 0;
}

static int
octrtc_read_raw(uint8_t *data, int reg);

int
octrtc_read(uint8_t *data, int reg)
{
#if NOCTIIC > 0
	int error;

	if (octiic_rtc_tag != NULL) {
		error = iic_acquire_bus(octiic_rtc_tag, 0);
		if (error)
			return error;
		error = octrtc_read_raw(data, reg);
		iic_release_bus(octiic_rtc_tag, 0);
		return error;
	}
#endif
	return octrtc_read_raw(data, reg);
}

static int
octrtc_read_raw(uint8_t *data, int reg)
{
	int nretries = 5, npoll;
	union mio_tws_sw_twsi_reg twsi;

again:
	twsi.reg = 0;
	twsi.field.v = 1;
	twsi.field.r = 1;
	twsi.field.sovr = 1;
	twsi.field.a = OCTRTC_REG;
	if (reg >= 0) {
		twsi.field.op = 1;
		twsi.field.ia = reg >> 3;
		twsi.field.eop_ia = reg & 7;
	}

	octeon_xkphys_write_8(MIO_TWS_SW_TWSI, twsi.reg);
	/* The 1st bit is cleared when the operation is complete */
	for (npoll = 0; npoll < 128; npoll++) {
		delay(1000);
		twsi.reg = octeon_xkphys_read_8(MIO_TWS_SW_TWSI);
		if (!twsi.field.v)
			break;
	}
	if (twsi.field.v)
		return ETIMEDOUT;
	DPRINTF(("%#llX ", twsi.reg));

	/*
	 * The data field is in the upper 32 bits and we're only
	 * interested in the first byte.
	 */
	*data = twsi.field.d & 0xff;

	/* 8th bit is the read result: 1 = success, 0 = failure */
	if (twsi.field.r == 0) {
		/*
		 * Lost arbitration : 0x38, 0x68, 0xB0, 0x78
		 * Core busy as slave: 0x80, 0x88, 0xA0, 0xA8, 0xB8, 0xC0, 0xC8
		 */
		if (*data == 0x38 || *data == 0x68 || *data == 0xB0 ||
		    *data == 0x78 || *data == 0x80 || *data == 0x88 ||
		    *data == 0xA0 || *data == 0xA8 || *data == 0xB8 ||
		    *data == 0xC0 || *data == 0xC8)
			if (nretries--)
				goto again;
		return EIO;
	}

	return 0;
}

int
octrtc_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct octrtc_softc *sc = handle->cookie;
	struct clock_ymdhms dt;
	int nretries = 2;
	int rc, i;
	uint8_t tod[8];
	uint8_t check, control;

	clock_secs_to_ymdhms(tv->tv_sec, &dt);

	DPRINTF(("settime: %d %d %d (%d) %02d:%02d:%02d\n",
	    dt.dt_year, dt.dt_mon, dt.dt_day, dt.dt_wday,
	    dt.dt_hour, dt.dt_min, dt.dt_sec));

	tod[0] = TOBCD(dt.dt_sec);
	tod[1] = TOBCD(dt.dt_min);
	tod[2] = TOBCD(dt.dt_hour);
	tod[3] = TOBCD(dt.dt_wday + 1);
	tod[4] = TOBCD(dt.dt_day);
	tod[5] = TOBCD(dt.dt_mon);
	if (sc->sc_ds1338 && (dt.dt_year < 2000 || dt.dt_year >= 2100))
		return EINVAL;
	if (!sc->sc_ds1338 && dt.dt_year >= 2000)
		tod[5] |= 0x80;
	tod[6] = TOBCD(dt.dt_year % 100);

	while (nretries--) {
		for (i = 0; i < 7; i++) {
			rc = octrtc_write(i, tod[i]);
			if (rc) {
				DPRINTF(("octrtc_write(%d) failed %d", i, rc));
				return EIO;
			}
		}

		rc = octrtc_read(&check, 0);
		if (rc) {
			DPRINTF(("octrtc_read(check) failed %d", rc));
			return EIO;
		}

		if (check == tod[0]) {
			if (!sc->sc_ds1338)
				return 0;
			rc = octrtc_read(&control, DS1338_CTRL);
			if (rc)
				return rc;
			if (!(control & DS1338_OSF))
				return 0;
			return octrtc_write(DS1338_CTRL, control & ~DS1338_OSF);
		}
	}

	return EIO;
}

static int
octrtc_write_raw(int reg, uint8_t data);

int
octrtc_write(int reg, uint8_t data)
{
#if NOCTIIC > 0
	int error;

	if (octiic_rtc_tag != NULL) {
		error = iic_acquire_bus(octiic_rtc_tag, 0);
		if (error)
			return error;
		error = octrtc_write_raw(reg, data);
		iic_release_bus(octiic_rtc_tag, 0);
		return error;
	}
#endif
	return octrtc_write_raw(reg, data);
}

static int
octrtc_write_raw(int reg, uint8_t data)
{
	union mio_tws_sw_twsi_reg twsi;
	int i;

	twsi.reg = 0;
	twsi.field.v = 1;
	twsi.field.sovr = 1;
	twsi.field.op = 1;
	twsi.field.a = OCTRTC_REG;
	twsi.field.ia = reg >> 3;
	twsi.field.eop_ia = reg & 7;
	twsi.field.d = data;

	octeon_xkphys_write_8(MIO_TWS_SW_TWSI_EXT, 0);
	octeon_xkphys_write_8(MIO_TWS_SW_TWSI, twsi.reg);
	for (i = 0; i < 128; i++) {
		delay(1000);
		twsi.reg = octeon_xkphys_read_8(MIO_TWS_SW_TWSI);
		if (!twsi.field.v)
			return twsi.field.r ? 0 : EIO;
	}
	return ETIMEDOUT;
}
