#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspdebug.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

PSP_MODULE_INFO("CalcGU", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

// --- GU frame config ---
#define BUF_WIDTH   512
#define SCR_WIDTH   480
#define SCR_HEIGHT  272
#define PIXEL_SIZE  4
#define FRAME_SIZE  (BUF_WIDTH*SCR_HEIGHT*PIXEL_SIZE)

static unsigned int __attribute__((aligned(16))) list[262144];

// --- калькулятор ---
static char display[64] = "0";
static double acc = 0.0;
static char op = 0;
static int entering_second = 0;
static int has_dot = 0;

static void set_display_double(double v){
    char buf[64];
    snprintf(buf, sizeof(buf), "%.10g", v);
    strncpy(display, buf, sizeof(display));
    display[sizeof(display)-1] = 0;
}
static double display_to_double(void){ return atof(display); }
static void clear_all(void){ strcpy(display,"0"); acc=0.0; op=0; entering_second=0; has_dot=0; }
static void clear_entry(void){ strcpy(display,"0"); has_dot=0; }
static void input_digit(char d){
    size_t len=strlen(display);
    if(len>=31) return;
    if(strcmp(display,"0")==0 && d!='.'){ display[0]=d; display[1]=0; return; }
    if(d=='.'){ if(has_dot) return; has_dot=1; }
    display[len]=d; display[len+1]=0;
}
static void toggle_sign(void){
    if(display[0]=='-'){ memmove(display, display+1, strlen(display)); }
    else{ size_t len=strlen(display); if(len>=31) return; memmove(display+1, display, len+1); display[0]='-'; }
}
static void do_op(char newop){
    double cur=display_to_double();
    if(!op) acc=cur;
    else{
        if(op=='+') acc+=cur;
        else if(op=='-') acc-=cur;
        else if(op=='*') acc*=cur;
        else if(op=='/') acc=(cur==0.0)?NAN:acc/cur;
    }
    set_display_double(acc);
    op=newop; has_dot=0; entering_second=1;
}
static void press_equal(void){
    if(!op) return;
    double cur=display_to_double(), res=acc;
    if(op=='+') res=acc+cur;
    else if(op=='-') res=acc-cur;
    else if(op=='*') res=acc*cur;
    else if(op=='/') res=(cur==0.0)?NAN:acc/cur;
    set_display_double(res);
    acc=res; op=0; entering_second=0; has_dot=(strchr(display,'.')!=NULL);
}
static void backspace(void){
    size_t len=strlen(display);
    if(len<=1 || (len==2 && display[0]=='-')){ strcpy(display,"0"); has_dot=0; return; }
    if(display[len-1]=='.') has_dot=0;
    display[len-1]=0;
}

// --- GUI: кнопки и геометрия ---
typedef struct { int x,y,w,h; const char* label; int isOp; } Btn;

static Btn buttons[] = {
    // r0
    {  20,120,102,44, "AC",0 }, { 130,120,102,44, "+/-",0 }, { 240,120,102,44, "%",0 }, { 350,120,102,44, "/",1 },
    // r1
    {  20,170,102,44, "7",0 },  { 130,170,102,44, "8",0 },   { 240,170,102,44, "9",0 },  { 350,170,102,44, "*",1 },
    // r2
    {  20,220,102,44, "4",0 },  { 130,220,102,44, "5",0 },   { 240,220,102,44, "6",0 },  { 350,220,102,44, "-",1 },
    // r3
    {  20,270,102,44, "1",0 },  { 130,270,102,44, "2",0 },   { 240,270,102,44, "3",0 },  { 350,270,102,44, "+",1 },
    // r4
    {  20,320,214,44, "0",0 },  { 240,320,102,44, ".",0 },   { 350,320,102,44, "=",1 },
};
static const int BTN_COUNT = sizeof(buttons)/sizeof(buttons[0]);
// чтобы вместилось по высоте 272, сдвинем сетку чуть вверх
static const int GRID_Y_OFFSET = -30;

static int selIndex = 0;

// цвета ABGR
static inline unsigned int ABGR(unsigned char a, unsigned char b, unsigned char g, unsigned char r){
    return (a<<24)|(b<<16)|(g<<8)|r;
}
static const unsigned int COL_BG      = 0xFF202020;                // тёмно-серый фон
static const unsigned int COL_DISPLAY = 0xFFFFFFFF;                // белый дисплей
static const unsigned int COL_NUM     = ABGR(0xFF,0x55,0x55,0x55); // серые кнопки
static const unsigned int COL_OP      = ABGR(0xFF,0x99,0x99,0xFF); // оранжевые (псевдо-ABGR)
static const unsigned int COL_SEL     = ABGR(0xFF,0xCC,0xCC,0xFF); // выделение (светлее)
static const unsigned int COL_FRAME   = 0xFF000000;                // чёрная рамка
static const unsigned int COL_TEXT    = 0xFF000000;                // чёрный текст

// упрощённый 2D-праймитив
static void begin2D(){
    sceGuStart(GU_DIRECT, list);
    sceGuClearColor(COL_BG);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_FLAT);

    // ортографическая проекция: экранные координаты
    sceGumMatrixMode(GU_PROJECTION); sceGumLoadIdentity();
    sceGumOrtho(0, SCR_WIDTH, SCR_HEIGHT, 0, -1.0f, 1.0f);
    sceGumMatrixMode(GU_VIEW);  sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuScissor(0,0,SCR_WIDTH,SCR_HEIGHT);
}

static void end2D(){
    sceGuFinish();
    sceGuSync(0,0);
}

// прямоугольник заливкой
static void fillRect(int x, int y, int w, int h, unsigned int color){
    sceGuColor(color);
    sceGuBegin(GU_TRIANGLES);
        sceGuVertex2i(0, x,     y);
        sceGuVertex2i(0, x+w,   y);
        sceGuVertex2i(0, x,     y+h);

        sceGuVertex2i(0, x+w,   y);
        sceGuVertex2i(0, x+w,   y+h);
        sceGuVertex2i(0, x,     y+h);
    sceGuEnd();
}

// рамка (1px)
static void frameRect(int x, int y, int w, int h, unsigned int color){
    fillRect(x,   y,   w, 1, color);
    fillRect(x,   y+h-1, w, 1, color);
    fillRect(x,   y,   1, h, color);
    fillRect(x+w-1, y, 1, h, color);
}

// отрисовка всей сцены
static void render(){
    begin2D();

    // экран калькулятора
    fillRect(20, 20, 440, 64, COL_DISPLAY);
    frameRect(20, 20, 440, 64, COL_FRAME);

    // сетка кнопок
    for(int i=0;i<BTN_COUNT;i++){
        int x=buttons[i].x, y=buttons[i].y+GRID_Y_OFFSET, w=buttons[i].w, h=buttons[i].h;
        unsigned int base = buttons[i].isOp ? COL_OP : COL_NUM;
        unsigned int col = (i==selIndex) ? COL_SEL : base;
        fillRect(x, y, w, h, col);
        frameRect(x, y, w, h, COL_FRAME);
    }

    end2D();

    // ПОДПИСИ — простым текстом поверх (debug screen пишет в тот же буфер)
    pspDebugScreenSetTextColor(COL_TEXT);
    pspDebugScreenSetXY(2, 2);
    pspDebugScreenPrintf("iOS-style Calc (X=press  O=AC  []=Back  /\\==)\n");

    // правое выравнивание числа в дисплее (примерно 52 колонки по 8px)
    char dispbuf[48];
    snprintf(dispbuf, sizeof(dispbuf), "%s", display);
    int len = (int)strlen(dispbuf);
    int maxCols = 52; // грубо: 440px / ~8px
    int startCol = (len < maxCols) ? (maxCols - len) : 0;
    pspDebugScreenSetXY(4 + startCol, 4);
    pspDebugScreenPrintf("%s", dispbuf);

    // подписи к кнопкам
    for(int i=0;i<BTN_COUNT;i++){
        int x=buttons[i].x, y=buttons[i].y+GRID_Y_OFFSET;
        int col = x/8;                    // 8px ~ 1 символ
        int row = y/8;
        if(col<0) col=0; if(row<0) row=0;
        pspDebugScreenSetXY(col+ (buttons[i].w>=214? 8:3), row+1);
        pspDebugScreenPrintf("%s", buttons[i].label);
    }
}

// применение нажатой кнопки
static void press(const char* label){
    if(strcmp(label,"AC")==0){ clear_all(); return; }
    if(strcmp(label,"+/-")==0){ toggle_sign(); return; }
    if(strcmp(label,"<-")==0){ backspace(); return; }
    if(strcmp(label,"=")==0){ press_equal(); return; }
    if(strcmp(label,"%")==0){ double v=display_to_double(); v/=100.0; set_display_double(v); return; }
    if(strcmp(label,".")==0){ input_digit('.'); return; }
    if(strcmp(label,"+")==0 || strcmp(label,"-")==0 || strcmp(label,"*")==0 || strcmp(label,"/")==0){
        do_op(label[0]); clear_entry(); return;
    }
    if(label[0]>='0' && label[0]<='9' && label[1]=='\0'){ input_digit(label[0]); return; }
}

int main(int argc, char* argv[]){
    // Инициализация GU (один буфер, чтобы просто печатать текст поверх)
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void*)0, BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, (void*)0, BUF_WIDTH);
    sceGuDepthBuffer((void*)FRAME_SIZE, BUF_WIDTH);
    sceGuOffset(2048 - (SCR_WIDTH/2), 2048 - (SCR_HEIGHT/2));
    sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuFinish();
    sceGuSync(0,0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    // Текстовый оверлей
    pspDebugScreenInit();
    pspDebugScreenSetBackColor(0);
    pspDebugScreenSetTextColor(0xFFFFFFFF);

    // Пульт
    SceCtrlData pad, old = {0};
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while(1){
        render();

        sceCtrlReadBufferPositive(&pad,1);

        if((pad.Buttons & PSP_CTRL_START) && !(old.Buttons & PSP_CTRL_START)) break;

        // навигация
        if((pad.Buttons & PSP_CTRL_LEFT)  && !(old.Buttons & PSP_CTRL_LEFT))  { if(selIndex>0) selIndex--; }
        if((pad.Buttons & PSP_CTRL_RIGHT) && !(old.Buttons & PSP_CTRL_RIGHT)) { if(selIndex<BTN_COUNT-1) selIndex++; }
        if((pad.Buttons & PSP_CTRL_UP)    && !(old.Buttons & PSP_CTRL_UP))    {
            // прыжок вверх по рядам (слегка «жёстко» по сетке)
            if(selIndex>=4) selIndex -= 4;
        }
        if((pad.Buttons & PSP_CTRL_DOWN)  && !(old.Buttons & PSP_CTRL_DOWN))  {
            if(selIndex+4 < BTN_COUNT) selIndex += 4;
        }

        // действия
        if((pad.Buttons & PSP_CTRL_CROSS)   && !(old.Buttons & PSP_CTRL_CROSS))   press(buttons[selIndex].label);
        if((pad.Buttons & PSP_CTRL_CIRCLE)  && !(old.Buttons & PSP_CTRL_CIRCLE))  clear_all();
        if((pad.Buttons & PSP_CTRL_SQUARE)  && !(old.Buttons & PSP_CTRL_SQUARE))  backspace();
        if((pad.Buttons & PSP_CTRL_TRIANGLE)&& !(old.Buttons & PSP_CTRL_TRIANGLE))press_equal();
        if((pad.Buttons & PSP_CTRL_LTRIGGER)&& !(old.Buttons & PSP_CTRL_LTRIGGER))toggle_sign();
        if((pad.Buttons & PSP_CTRL_RTRIGGER)&& !(old.Buttons & PSP_CTRL_RTRIGGER))toggle_sign();
        if((pad.Buttons & PSP_CTRL_SELECT)  && !(old.Buttons & PSP_CTRL_SELECT))  clear_entry();

        old = pad;
        sceDisplayWaitVblankStart();
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
