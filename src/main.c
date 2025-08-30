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

/* ----------- GU frame config ----------- */
#define BUF_WIDTH   512
#define SCR_WIDTH   480
#define SCR_HEIGHT  272
#define PIXEL_SIZE  4
#define FRAME_SIZE  (BUF_WIDTH*SCR_HEIGHT*PIXEL_SIZE)

static unsigned int __attribute__((aligned(16))) list[262144];

/* ----------- calculator state ----------- */
static char   display[64] = "0";
static double acc = 0.0;
static char   op  = 0;
static int    entering_second = 0;
static int    has_dot = 0;

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
    else{
        size_t len=strlen(display); if(len>=31) return;
        memmove(display+1, display, len+1); display[0]='-';
    }
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

/* ----------- GUI ------------- */

/* ABGR цвета как константы (без вычислений в инициализаторах) */
static const unsigned int COL_BG      = 0xFF202020; /* фон */
static const unsigned int COL_DISPLAY = 0xFFFFFFFF; /* белый дисплей */
static const unsigned int COL_NUM     = 0xFF555555; /* серые кнопки */
static const unsigned int COL_OP      = 0xFFFF9900; /* оранжевые операции */
static const unsigned int COL_SEL     = 0xFFFFCC55; /* выделение светлее */
static const unsigned int COL_FRAME   = 0xFF000000; /* чёрная рамка */
static const unsigned int COL_TEXT    = 0xFF000000; /* чёрный текст */

typedef struct { float x, y, z; } Vertex;

/* Рисуем прямоугольник через массив вершин (2 треугольника) */
static void drawQuad(int x, int y, int w, int h, unsigned int color){
    sceGuColor(color);
    sceGuDisable(GU_TEXTURE_2D);
    Vertex* v = (Vertex*)sceGuGetMemory(6 * sizeof(Vertex));
    v[0].x = x;     v[0].y = y;     v[0].z = 0;
    v[1].x = x+w;   v[1].y = y;     v[1].z = 0;
    v[2].x = x;     v[2].y = y+h;   v[2].z = 0;
    v[3].x = x+w;   v[3].y = y;     v[3].z = 0;
    v[4].x = x+w;   v[4].y = y+h;   v[4].z = 0;
    v[5].x = x;     v[5].y = y+h;   v[5].z = 0;

    sceGuDrawArray(GU_TRIANGLES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, 6, 0, v);
}

static void frameRect(int x,int y,int w,int h,unsigned int color){
    drawQuad(x, y, w, 1, color);
    drawQuad(x, y+h-1, w, 1, color);
    drawQuad(x, y, 1, h, color);
    drawQuad(x+w-1, y, 1, h, color);
}

typedef struct { int x,y,w,h; const char* label; int isOp; } Btn;

/* Сетка 5×4, высота/ширина подобраны, чтобы влезть в 272px */
static Btn buttons[20];
static int BTN_COUNT = 20;
static int selIndex = 0;

static void setupButtons(void){
    /* layout */
    const int left = 20;
    const int top  = 96;   /* первая строка кнопок */
    const int bw   = 104;  /* ширина кнопки */
    const int bh   = 32;   /* высота кнопки */
    const int gapX = 12;
    const int gapY = 8;

    const char* labels[5][4] = {
        {"AC","+/-","%","/"},
        {"7","8","9","*"},
        {"4","5","6","-"},
        {"1","2","3","+"},
        {"0",".","=","="} /* последний столбец продублируем "=" для навигации */
    };
    const int isOpCol[4] = {0,0,0,1}; /* правый столбец — операции */

    int idx=0;
    for(int r=0;r<5;r++){
        for(int c=0;c<4;c++){
            buttons[idx].x = left + c*(bw+gapX);
            buttons[idx].y = top  + r*(bh+gapY);
            buttons[idx].w = bw;
            buttons[idx].h = bh;
            buttons[idx].label = labels[r][c];
            /* '=' считаем операцией тоже */
            buttons[idx].isOp = isOpCol[c] || (labels[r][c][0]=='=');
            idx++;
        }
    }
}

/* Начало/конец 2D кадра */
static void begin2D(void){
    sceGuStart(GU_DIRECT, list);
    sceGuClearColor(COL_BG);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    sceGuDisable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_FLAT);

    /* Ортографическая проекция (экранные координаты) */
    sceGumMatrixMode(GU_PROJECTION); sceGumLoadIdentity();
    sceGumOrtho(0, SCR_WIDTH, SCR_HEIGHT, 0, -1.0f, 1.0f);
    sceGumMatrixMode(GU_VIEW);  sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();

    sceGuEnable(GU_SCISSOR_TEST);
    sceGuScissor(0,0,SCR_WIDTH,SCR_HEIGHT);
}
static void end2D(void){
    sceGuFinish();
    sceGuSync(0,0);
}

static void render(void){
    begin2D();

    /* дисплей */
    drawQuad(20, 20, 440, 60, COL_DISPLAY);
    frameRect(20, 20, 440, 60, COL_FRAME);

    /* кнопки */
    for(int i=0;i<BTN_COUNT;i++){
        unsigned int base = buttons[i].isOp ? COL_OP : COL_NUM;
        unsigned int col  = (i==selIndex) ? COL_SEL : base;
        drawQuad(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, col);
        frameRect(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, COL_FRAME);
    }

    end2D();

    /* Текст поверх (debug screen рендерит в тот же буфер) */
    pspDebugScreenSetTextColor(COL_TEXT);
    pspDebugScreenSetXY(2, 1);
    pspDebugScreenPrintf("iOS-style Calc  (X press | O AC | [] BS | /\\ =)\n");

    /* правое выравнивание числа (примерно по 52 символа по 8px) */
    char dispbuf[48]; snprintf(dispbuf, sizeof(dispbuf), "%s", display);
    int len = (int)strlen(dispbuf);
    int maxCols = 52;
    int startCol = (len < maxCols) ? (maxCols - len) : 0;
    pspDebugScreenSetXY(4 + startCol, 4);
    pspDebugScreenPrintf("%s", dispbuf);

    /* подписи кнопок: приблизительно по сетке */
    for(int i=0;i<BTN_COUNT;i++){
        int col = buttons[i].x / 8;
        int row = buttons[i].y / 8;
        if (col < 0) col = 0;
        if (row < 0) row = 0;
        /* смещение внутри кнопки: цифры по центру примерно */
        pspDebugScreenSetXY(col + 6, row + 2);
        pspDebugScreenPrintf("%s", buttons[i].label);
    }
}

static void press(const char* label){
    if(strcmp(label,"AC")==0){ clear_all(); return; }
    if(strcmp(label,"+/-")==0){ toggle_sign(); return; }
    if(strcmp(label,"=")==0){ press_equal(); return; }
    if(strcmp(label,"%")==0){ double v=display_to_double(); v/=100.0; set_display_double(v); return; }
    if(strcmp(label,".")==0){ input_digit('.'); return; }
    if(strcmp(label,"+")==0 || strcmp(label,"-")==0 || strcmp(label,"*")==0 || strcmp(label,"/")==0){
        do_op(label[0]); clear_entry(); return;
    }
    if(label[0]>='0' && label[0]<='9' && label[1]=='\0'){ input_digit(label[0]); return; }
}

int main(int argc, char* argv[]){
    setupButtons();

    /* Инициализация GU: один кадр + debug overlay */
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

    pspDebugScreenInit();
    pspDebugScreenSetBackColor(0);
    pspDebugScreenSetTextColor(0xFFFFFFFF);

    /* контроллер */
    SceCtrlData pad, old = {0};
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while(1){
        render();

        sceCtrlReadBufferPositive(&pad,1);
        if((pad.Buttons & PSP_CTRL_START) && !(old.Buttons & PSP_CTRL_START)) break;

        /* навигация по сетке 5x4 (20 кнопок) */
        if((pad.Buttons & PSP_CTRL_LEFT)  && !(old.Buttons & PSP_CTRL_LEFT))  { if(selIndex%4>0) selIndex--; }
        if((pad.Buttons & PSP_CTRL_RIGHT) && !(old.Buttons & PSP_CTRL_RIGHT)) { if(selIndex%4<3) selIndex++; }
        if((pad.Buttons & PSP_CTRL_UP)    && !(old.Buttons & PSP_CTRL_UP))    { if(selIndex>=4)   selIndex-=4; }
        if((pad.Buttons & PSP_CTRL_DOWN)  && !(old.Buttons & PSP_CTRL_DOWN))  { if(selIndex<16)   selIndex+=4; }

        /* действия */
        if((pad.Buttons & PSP_CTRL_CROSS)    && !(old.Buttons & PSP_CTRL_CROSS))    press(buttons[selIndex].label);
        if((pad.Buttons & PSP_CTRL_CIRCLE)   && !(old.Buttons & PSP_CTRL_CIRCLE))   clear_all();
        if((pad.Buttons & PSP_CTRL_SQUARE)   && !(old.Buttons & PSP_CTRL_SQUARE))   backspace();
        if((pad.Buttons & PSP_CTRL_TRIANGLE) && !(old.Buttons & PSP_CTRL_TRIANGLE)) press_equal();
        if((pad.Buttons & PSP_CTRL_LTRIGGER) && !(old.Buttons & PSP_CTRL_LTRIGGER)) toggle_sign();
        if((pad.Buttons & PSP_CTRL_RTRIGGER) && !(old.Buttons & PSP_CTRL_RTRIGGER)) toggle_sign();
        if((pad.Buttons & PSP_CTRL_SELECT)   && !(old.Buttons & PSP_CTRL_SELECT))   clear_entry();

        old = pad;
        sceDisplayWaitVblankStart();
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
