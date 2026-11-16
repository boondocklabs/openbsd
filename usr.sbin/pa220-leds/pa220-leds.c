#include <sys/types.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CPU_PA220_LEDS 6
#define CPU_PA220_FRONT_LEDS 7
#define PA220_PORTS 8

static const char * const front_leds[] = { "ha", "alm", "stat" };
static const char * const front_colors[] = {
	"off", "green", "orange", "both", "heartbeat"
};
#define FRONT_LED_COUNT (sizeof(front_leds) / sizeof(front_leds[0]))
#define FRONT_MODE_COUNT (sizeof(front_colors) / sizeof(front_colors[0]))

static void
usage(void)
{
	fprintf(stderr, "usage: pa220-leds [port [link-activity | link | "
		    "combined | off | 0xRL]]\n"
	    "       pa220-leds front\n"
	    "       pa220-leds stat [off | green | orange | heartbeat]\n"
	    "       pa220-leds ha [off | green | orange]\n"
	    "       pa220-leds alm [off | green | red]\n");
	exit(1);
}

static int
parse_mode(const char *arg)
{
	char *end;
	long mode;

	if (strcmp(arg, "link-activity") == 0)
		return 0x40;
	if (strcmp(arg, "link") == 0)
		return 0x80;
	if (strcmp(arg, "combined") == 0)
		return 0x81;
	if (strcmp(arg, "off") == 0)
		return 0x88;

	if (strncmp(arg, "0x", 2) != 0 || strlen(arg) != 4)
		usage();
	errno = 0;
	mode = strtol(arg + 2, &end, 16);
	if (errno != 0 || *end != '\0' || mode < 0 ||
	    (mode >> 4) > 11 || (mode & 15) > 11)
		usage();
	return mode;
}

static void
show_port(int port, int requested)
{
	int mib[] = { CTL_MACHDEP, CPU_PA220_LEDS, port };
	int mode;
	size_t len = sizeof(mode);

	if (requested >= 0 &&
	    sysctl(mib, 3, NULL, NULL, &requested, sizeof(requested)) == -1)
		err(1, "set port %d", port);
	if (sysctl(mib, 3, &mode, &len, NULL, 0) == -1)
		err(1, "read port %d", port);
	printf("port %d (cnmac%d): 0x%02x\n", port, port - 1, mode);
}

static void
show_front(size_t led, const char *requested)
{
	int mib[] = { CTL_MACHDEP, CPU_PA220_FRONT_LEDS, led };
	const char *color;
	int mode;
	size_t len = sizeof(mode);

	if (requested != NULL) {
		if (strcmp(requested, "off") == 0)
			mode = 0;
		else if (strcmp(requested, "green") == 0)
			mode = 1;
		else if (strcmp(requested, led == 1 ? "red" : "orange") == 0)
			mode = 2;
		else if (led == 2 && strcmp(requested, "heartbeat") == 0)
			mode = 4;
		else
			usage();
		if (sysctl(mib, 3, NULL, NULL, &mode, sizeof(mode)) == -1)
			err(1, "set %s", front_leds[led]);
	}
	if (sysctl(mib, 3, &mode, &len, NULL, 0) == -1)
		err(1, "read %s", front_leds[led]);
	if (mode < 0 || (size_t)mode >= FRONT_MODE_COUNT)
		errx(1, "%s returned an invalid mode", front_leds[led]);
	color = led == 1 && mode == 2 ? "red" : front_colors[mode];
	printf("%s: %s\n", front_leds[led], color);
}

int
main(int argc, char **argv)
{
	const char *error;
	int port, requested = -1;

	if (argc > 3)
		usage();
	if (argc == 1) {
		for (port = 1; port <= PA220_PORTS; port++)
			show_port(port, -1);
		return 0;
	}

	if (strcmp(argv[1], "front") == 0) {
		if (argc != 2)
			usage();
		for (size_t i = 0; i < FRONT_LED_COUNT; i++)
			show_front(i, NULL);
		return 0;
	}
	for (size_t i = 0; i < FRONT_LED_COUNT; i++) {
		if (strcmp(argv[1], front_leds[i]) == 0) {
			show_front(i, argc == 3 ? argv[2] : NULL);
			return 0;
		}
	}

	port = strtonum(argv[1], 1, PA220_PORTS, &error);
	if (error != NULL)
		errx(1, "port %s is %s", argv[1], error);
	if (argc == 3)
		requested = parse_mode(argv[2]);
	show_port(port, requested);
	return 0;
}
