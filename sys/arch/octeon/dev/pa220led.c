/* Palo Alto Networks PA-220 front-panel TEMP LED. */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <machine/autoconf.h>

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
};

int	pa220led_match(struct device *, void *, void *);
void	pa220led_attach(struct device *, struct device *, void *);
void	pa220led_refresh(void *);

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
