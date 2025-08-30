#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>

PSP_MODULE_INFO("HelloHomebrew", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define printf pspDebugScreenPrintf

static int exit_callback(int a, int b, void *c) {
    sceKernelExitGame();
    return 0;
}
static int callback_thread(SceSize args, void *argp) {
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}
static int setup_callbacks(void) {
    int thid = sceKernelCreateThread("update_thread", callback_thread,
                                     0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
    return thid;
}

int main(int argc, char *argv[]) {
    pspDebugScreenInit();
    setup_callbacks();

    // --- Splash screen ---
    pspDebugScreenSetXY(0,0);
    printf("==============================================\n");
    printf("              HELLO PSP — Splash              \n");
    printf("  Werkruimte Homebrew demo                    \n");
    printf("  D-Pad — движение '@'   START — выход        \n");
    printf("==============================================\n");
    for (int i=3; i>0; --i) {
        pspDebugScreenSetXY(0,7);
        printf("Начинаем через %d ...   ", i);
        sceDisplayWaitVblankStart();
        sceKernelDelayThread(1000*1000); // 1s
    }

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    int x = 10, y = 8;       // позиция курсора
    SceCtrlData pad;

    while (1) {
        sceCtrlReadBufferPositive(&pad, 1);

        if (pad.Buttons & PSP_CTRL_START) break;
        if (pad.Buttons & PSP_CTRL_UP)    y = (y > 0)  ? y - 1 : y;
        if (pad.Buttons & PSP_CTRL_DOWN)  y = (y < 31) ? y + 1 : y;
        if (pad.Buttons & PSP_CTRL_LEFT)  x = (x > 0)  ? x - 1 : x;
        if (pad.Buttons & PSP_CTRL_RIGHT) x = (x < 59) ? x + 1 : x;

        pspDebugScreenClear();
        pspDebugScreenSetXY(0,0);
        printf("Hello from PSP Homebrew!\n");
        printf("D-Pad двигает '@', START — выход.\n\n");

        pspDebugScreenSetXY(x, y);
        printf("@");

        sceDisplayWaitVblankStart();
    }

    sceKernelExitGame();
    return 0;
}
