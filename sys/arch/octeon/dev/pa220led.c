/* Palo Alto Networks PA-220 front-panel LEDs. */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sensors.h>
#include <sys/sysctl.h>
#include <sys/timeout.h>

#include <machine/autoconf.h>
#include <machine/octeonvar.h>

#define PA220_CPLD_FRONT	PHYS_TO_XKPHYS(0x1b040007ULL, CCA_NC)
#define PA220_LED_HA		0
#define PA220_LED_ALM		1
#define PA220_LED_STAT		2
#define PA220_LED_HEARTBEAT	4
#define PA220_LED_MASK		0x03

/* Two short green flashes per second. */
static const int pa220led_delays[] = { 100, 100, 100, 700 };

#define PA220_CPLD_TEMP		0x800000001b040006ULL
#define PA220_TEMP_GREEN	0x01
#define PA220_TEMP_ORANGE	0x02
#define PA220_TEMP_MASK		(PA220_TEMP_GREEN | PA220_TEMP_ORANGE)

/* Match the OpenWrt defaults. OpenBSD temperature sensors use microkelvin. */
#define PA220_TEMP_HOT		358150000LL	/* 85 C */
#define PA220_TEMP_COOL		353150000LL	/* 80 C */

struct pa220led_softc {
	struct device		sc_dev;
	struct ksensordev	sc_sensordev;
	struct ksensor		sc_green;
	struct ksensor		sc_orange;
	uint8_t			sc_color;
	struct timeout		sc_heartbeat;
	unsigned int		sc_phase;
	int			sc_stat_automatic;
};

int	pa220led_match(struct device *, void *, void *);
void	pa220led_attach(struct device *, struct device *, void *);
void	pa220led_refresh(void *);
int	pa220led_sysctl(int, void *, size_t *, void *, size_t);
static void pa220led_heartbeat(void *);
static void pa220led_start(struct device *);

static struct pa220led_softc *pa220led_controller;

const struct cfattach pa220led_ca = {
	sizeof(struct pa220led_softc), pa220led_match, pa220led_attach
};

struct cfdriver pa220led_cd = {
	NULL, "pa220led", DV_DULL
};

int
pa220led_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *maa = aux;

	return strcmp(maa->maa_name, "pa220led") == 0;
}

void
pa220led_attach(struct device *parent, struct device *self, void *aux)
{
	struct pa220led_softc *sc = (void *)self;

	pa220led_controller = sc;
	timeout_set_proc(&sc->sc_heartbeat, pa220led_heartbeat, sc);
	sc->sc_stat_automatic = 1;
	config_mountroot(self, pa220led_start);

	sc->sc_color = PA220_TEMP_GREEN;
	sc->sc_green.type = SENSOR_INDICATOR;
	sc->sc_orange.type = SENSOR_INDICATOR;
	strlcpy(sc->sc_green.desc, "TEMP green", sizeof(sc->sc_green.desc));
	strlcpy(sc->sc_orange.desc, "TEMP orange", sizeof(sc->sc_orange.desc));
	strlcpy(sc->sc_sensordev.xname, self->dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	if (sensor_task_register(sc, pa220led_refresh, 15) == NULL) {
		printf(": can't register sensor task\n");
		return;
	}
	sensor_attach(&sc->sc_sensordev, &sc->sc_green);
	sensor_attach(&sc->sc_sensordev, &sc->sc_orange);
	sensordev_install(&sc->sc_sensordev);
	printf("\n");
}

static uint8_t
pa220led_color(uint8_t previous, int64_t temperature)
{
	if (temperature >= PA220_TEMP_HOT)
		return PA220_TEMP_ORANGE;
	if (temperature < PA220_TEMP_COOL)
		return PA220_TEMP_GREEN;
	return previous;
}

void
pa220led_refresh(void *arg)
{
	struct pa220led_softc *sc = arg;
	volatile uint8_t *reg = (volatile uint8_t *)PA220_CPLD_TEMP;
	struct ksensordev *device;
	struct ksensor *sensor;
	int error;
	uint8_t val;

	for (int i = 0; ; i++) {
		error = sensordev_get(i, &device);
		if (error == ENOENT)
			break;
		if (error != 0 || strcmp(device->xname, "tmptemp0") != 0)
			continue;
		if (sensor_find(i, SENSOR_TEMP, 1, &sensor) == 0 &&
		    !ISSET(sensor->flags, SENSOR_FINVALID)) {
			sc->sc_color = pa220led_color(sc->sc_color, sensor->value);
			val = *reg;
			if ((val & PA220_TEMP_MASK) != sc->sc_color)
				*reg = (val & ~PA220_TEMP_MASK) | sc->sc_color;
		}
		break;
	}

	val = *reg;
	sc->sc_green.value = !!ISSET(val, PA220_TEMP_GREEN);
	sc->sc_orange.value = !!ISSET(val, PA220_TEMP_ORANGE);
}


static void
pa220led_start(struct device *self)
{
	struct pa220led_softc *sc = (void *)self;

	timeout_add_sec(&sc->sc_heartbeat, 1);
}

static int
pa220led_set(int led, int color)
{
	uint8_t *reg = (uint8_t *)PA220_CPLD_FRONT;
	unsigned int shift = led * 2;
	uint8_t value;

	if (kcopy(reg, &value, sizeof(value)) != 0)
		return EIO;
	value = (value & ~(PA220_LED_MASK << shift)) | (color << shift);
	if (kcopy(&value, reg, sizeof(value)) != 0)
		return EIO;
	return 0;
}

static void
pa220led_heartbeat(void *arg)
{
	struct pa220led_softc *sc = arg;
	unsigned int phase = sc->sc_phase;

	if (!sc->sc_stat_automatic)
		return;

	if (pa220led_set(PA220_LED_STAT, (phase & 1) == 0) != 0) {
		timeout_add_sec(&sc->sc_heartbeat, 1);
		return;
	}
	sc->sc_phase = (phase + 1) % nitems(pa220led_delays);
	timeout_add_msec(&sc->sc_heartbeat, pa220led_delays[phase]);
}

/* The machdep sysctl and the non-MPSAFE timeout share the kernel lock. */
int
pa220led_sysctl(int led, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	struct pa220led_softc *sc = pa220led_controller;
	const uint8_t *reg = (const uint8_t *)PA220_CPLD_FRONT;
	uint8_t value;
	int mode, error;

	if (sc == NULL)
		return ENODEV;
	if (led < PA220_LED_HA || led > PA220_LED_STAT)
		return EINVAL;

	if (kcopy(reg, &value, sizeof(value)) != 0)
		return EIO;
	mode = (value >> (led * 2)) & PA220_LED_MASK;
	if (led == PA220_LED_STAT && sc->sc_stat_automatic)
		mode = PA220_LED_HEARTBEAT;

	error = sysctl_int_bounded(oldp, oldlenp, newp, newlen, &mode, 0,
	    led == PA220_LED_STAT ? PA220_LED_HEARTBEAT : 2);
	if (error || newp == NULL)
		return error;
	if (mode == PA220_LED_MASK)
		return EINVAL;

	if (led == PA220_LED_STAT) {
		timeout_del(&sc->sc_heartbeat);
		sc->sc_stat_automatic = mode == PA220_LED_HEARTBEAT;
		if (sc->sc_stat_automatic) {
			sc->sc_phase = 0;
			pa220led_heartbeat(sc);
			return 0;
		}
	}
	return pa220led_set(led, mode);
}
