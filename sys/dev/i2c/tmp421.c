/* TI TMP421 local and remote temperature sensor. */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <dev/i2c/i2cvar.h>
#include <dev/ofw/openfirm.h>

#define TMP421_LOCAL		0x00
#define TMP421_REMOTE		0x01
#define TMP421_CONFIG		0x09
#define TMP421_CONFIG_RANGE	0x04
#define TMP421_INVALID		0x02
#define TMP421_REMOTE_OPEN	0x01
#define TMP421_MFR_ID		0xfe
#define TMP421_DEVICE_ID	0xff
#define TMP421_MFR_TI		0x55
#define TMP421_DEVICE		0x21

struct tmp421_softc {
	struct device		sc_dev;
	i2c_tag_t		sc_tag;
	i2c_addr_t		sc_addr;
	struct ksensordev	sc_sensordev;
	struct ksensor		sc_temp[2];
};

int	tmp421_match(struct device *, void *, void *);
void	tmp421_attach(struct device *, struct device *, void *);
void	tmp421_refresh(void *);

const struct cfattach tmptemp_ca = {
	sizeof(struct tmp421_softc), tmp421_match, tmp421_attach
};

struct cfdriver tmptemp_cd = {
	NULL, "tmptemp", DV_DULL
};

int
tmp421_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	return strcmp(ia->ia_name, "ti,tmp421") == 0;
}

void
tmp421_attach(struct device *parent, struct device *self, void *aux)
{
	struct tmp421_softc *sc = (void *)self;
	struct i2c_attach_args *ia = aux;
	uint8_t reg, mfr, id;
	int error, root;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	error = iic_acquire_bus(sc->sc_tag, 0);
	if (error) {
		printf(": can't acquire bus\n");
		return;
	}

	reg = TMP421_MFR_ID;
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &reg, 1, &mfr, 1, 0);
	if (error == 0) {
		reg = TMP421_DEVICE_ID;
		error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
		    &reg, 1, &id, 1, 0);
	}
	iic_release_bus(sc->sc_tag, 0);

	if (error || mfr != TMP421_MFR_TI || id != TMP421_DEVICE) {
		printf(": TMP421 identification failed\n");
		return;
	}

	strlcpy(sc->sc_sensordev.xname, self->dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	strlcpy(sc->sc_temp[TMP421_LOCAL].desc, "board",
	    sizeof(sc->sc_temp[TMP421_LOCAL].desc));
	root = OF_finddevice("/");
	strlcpy(sc->sc_temp[TMP421_REMOTE].desc,
	    root != -1 && OF_is_compatible(root, "pan,pa-220") ?
	    "CPU" : "remote diode",
	    sizeof(sc->sc_temp[TMP421_REMOTE].desc));
	for (size_t i = 0; i < nitems(sc->sc_temp); i++) {
		sc->sc_temp[i].type = SENSOR_TEMP;
		sc->sc_temp[i].flags = SENSOR_FINVALID;
	}
	if (sensor_task_register(sc, tmp421_refresh, 5) == NULL) {
		printf(": can't register sensor task\n");
		return;
	}
	for (size_t i = 0; i < nitems(sc->sc_temp); i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_temp[i]);
	sensordev_install(&sc->sc_sensordev);
	printf("\n");
}

void
tmp421_refresh(void *arg)
{
	struct tmp421_softc *sc = arg;
	uint8_t reg = TMP421_CONFIG, config, data[2];
	struct ksensor *sensor;
	int error, degrees;

	for (size_t i = 0; i < nitems(sc->sc_temp); i++)
		sc->sc_temp[i].flags |= SENSOR_FINVALID;

	if (iic_acquire_bus(sc->sc_tag, 0) != 0)
		return;
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &reg, 1, &config, 1, 0);
	if (error != 0)
		goto done;

	for (reg = TMP421_LOCAL; reg <= TMP421_REMOTE; reg++) {
		sensor = &sc->sc_temp[reg];
		error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
		    &reg, 1, data, sizeof(data), 0);
		if (error || (data[1] & TMP421_INVALID) ||
		    (reg == TMP421_REMOTE && (data[1] & TMP421_REMOTE_OPEN)))
			continue;

		if (config & TMP421_CONFIG_RANGE)
			degrees = (int)data[0] - 64;
		else
			degrees = (int8_t)data[0];

		sensor->value = 273150000LL + degrees * 1000000LL +
		    (data[1] >> 4) * 62500LL;
		sensor->flags &= ~SENSOR_FINVALID;
	}
done:
	iic_release_bus(sc->sc_tag, 0);
}
