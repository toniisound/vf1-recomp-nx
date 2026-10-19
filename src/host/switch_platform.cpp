#ifdef __SWITCH__
#include "switch_platform.h"
#include <switch.h>
#include <SDL.h>
#include <cstdio>
#include <sys/stat.h>

void switch_platform_init()
{
	romfsInit();
	mkdir("sdmc:/switch", 0777);
	mkdir(VF_SWITCH_DIR, 0777);
	mkdir(VF_SWITCH_DIR "/roms", 0777);
#ifdef VF_NXLINK
	// development: stderr to the PC (nxlink -s)
	socketInitializeDefault();
	nxlinkStdio();
#endif
}

bool switch_full_memory()
{
	const AppletType t = appletGetAppletType();
	return t == AppletType_Application || t == AppletType_SystemApplication;
}

void switch_cpu_boost(bool on)
{
	static int now = -1;
	if (now == int(on)) return;
	if (appletGetAppletType() != AppletType_Application && appletGetAppletType() != AppletType_SystemApplication) {
		if (on) fprintf(stderr, "CPU boost: not in applet mode (start the game with title takeover)\n");
		now = int(on);
		return;
	}
	const Result rc = appletSetCpuBoostMode(on ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
	fprintf(stderr, "CPU boost %s%s\n", on ? "on (CPU 1785 MHz)" : "off", R_SUCCEEDED(rc) ? "" : " (refused by the system)");
	now = int(on);
}

bool switch_message(const char *title, const std::string &msg)
{
	if (SDL_WasInit(SDL_INIT_VIDEO)) return false;
	PrintConsole *con = consoleInit(nullptr);
	if (!con) return false;
	padConfigureInput(8, HidNpadStyleSet_NpadStandard);
	PadState pad;
	padInitializeAny(&pad);
	printf("\x1b[2J\x1b[1;1H\x1b[33;1m%s\x1b[0m\n\n", title);
	printf("%s\n\n", msg.c_str());
	printf("\x1b[36mPress A or + to continue.\x1b[0m\n");
	consoleUpdate(con);
	bool pressed = false;
	while (appletMainLoop()) {
		padUpdate(&pad);
		if (padGetButtonsDown(&pad) & (HidNpadButton_A | HidNpadButton_Plus)) { pressed = true; break; }
		consoleUpdate(con);
		svcSleepThread(16000000);
	}
	consoleExit(con);
	return pressed;
}
#endif
