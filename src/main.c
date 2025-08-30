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

/* ---------------- Frame config (16-bit) ---------------- */
#define BUF_WIDTH   512
#define SCR_WIDTH   480
#define SCR_HEIGHT  272
#define PIXEL_SIZE  2                              /* 16-bit */
#define FRAME_SIZE  (BUF_WIDTH * SCR_HEIGHT * PIXEL_SIZE)

static unsigned int __attribute__((aligned(16))) list[262144];

/* ---------------- Calculator state -------------------- */
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

/* ---------------- Colors (ABGR) ----------------------- */
/* debugScreen тоже ок с этими значениями */
static const unsigned int COL_BG          = 0xFF000000; /* чёрный фон */
static const unsigned int COL_DISPLAY     = 0xFFDDDDDD; /* светло-серый дисплей */
static const unsigned int COL_NUM         = 0xFF333333; /* цифры (тёмно-графит) */
static const unsigned int COL_SPEC        = 0xFFA5A5A5; /* AC, +/-, % */
static const unsigned int COL_OP          = 0xFF0095FF; /* оранжевые операции (#FF9500 → ABGR) */
static const unsigned int COL_SEL         = 0xFFFFFF55; /* подсветка выбранной */
static const unsigned int COL_TEXT_WHITE  = 0xFFFFFFFF;
static const unsigned int COL_TEXT_BLACK  = 0xFF000000;

/* ---------------- Buttons (5 rows) -------------------- */
typedef struct { int x,y,w,h; const char* label; int type; int r,c; } Btn;
enum {BTN_NUM_T, BTN_SPEC_T, BTN_OP_T};

static const int LEFT=20, TOP=90, BW=104, BH=30, GX=12, GY=8;

static Btn buttons[] = {
    /* r0 */
    {LEFT+0*(BW+GX), TOP+0*(BH+GY), BW,   BH,   "AC", BTN_SPEC_T, 0,0},
    {LEFT+1*(BW+GX), TOP+0*(BH+GY), BW,   BH,   "+/-",BTN_SPEC_T, 0,1},
    {LEFT+2*(BW+GX), TOP+0*(BH+GY), BW,   BH,   "%",  BTN_SPEC_T, 0,2},
    {LEFT+3*(BW+GX), TOP+0*(BH+GY), BW,   BH,   "/",  BTN_OP_T,   0,3},
    /* r1 */
    {LEFT+0*(BW+GX), TOP+1*(BH+GY), BW,   BH,   "7",  BTN_NUM_T,  1,0},
    {LEFT+1*(BW+GX), TOP+1*(BH+GY), BW,   BH,   "8",  BTN_NUM_T,  1,1},
    {LEFT+2*(BW+GX), TOP+1*(BH+GY), BW,   BH,   "9",  BTN_NUM_T,  1,2},
    {LEFT+3*(BW+GX), TOP+1*(BH+GY), BW,   BH,   "*",  BTN_OP_T,   1,3},
    /* r2 */
    {LEFT+0*(BW+GX), TOP+2*(BH+GY), BW,   BH,   "4",  BTN_NUM_T,  2,0},
    {LEFT+1*(BW+GX), TOP+2*(BH+GY), BW,   BH,   "5",  BTN_NUM_T,  2,1},
    {LEFT+2*(BW+GX), TOP+2*(BH+GY), BW,   BH,   "6",  BTN_NUM_T,  2,2},
    {LEFT+3*(BW+GX), TOP+2*(BH+GY), BW,   BH,   "-",  BTN_OP_T,   2,3},
    /* r3 */
    {LEFT+0*(BW+GX), TOP+3*(BH+GY), BW,   BH,   "1",  BTN_NUM_T,  3,0},
    {LEFT+1*(BW+GX), TOP+3*(BH+GY), BW,   BH,   "2",  BTN_NUM_T,  3,1},
    {LEFT+2*(BW+GX), TOP+3*(BH+GY), BW,   BH,   "3",  BTN_NUM_T,  3,2},
    {LEFT+3*(BW+GX), TOP+3*(BH+GY), BW,   BH,   "+",  BTN_OP_T,   3,3},
    /* r4 */
    {LEFT+0*(BW+GX), TOP+4*(BH+GY), BW*2+GX, BH,      "0",  BTN_NUM_T,  4,0}, /* двойная ширина */
    {LEFT+2*(BW+GX), TOP+4*(BH+GY), BW,      BH,      ".",  BTN_NUM_T,  4,2},
    {LEFT+3*(BW+GX), TOP+4*(BH+GY), BW,      BH,      "=",  BTN_OP_T,   4,3},
};
static const int BTN_COUNT = sizeof(buttons)/sizeof(buttons[0]);
static int sel = 0;

/* ---------------- GU rectangles ---------------------- */
typedef struct { float x,y,z; } Vertex;
static void drawQuad(int x,int y,int w,int h,unsigned int color){
    sceGuColor(color);
    Vertex* v=(Vertex*)sceGuGetMemory(6*sizeof(Vertex));
    v[0].x=x;   v[0].y=y;   v[0].z=0;
    v[1].x=x+w; v[1].y=y;   v[1].z=0;
    v[2].x=x;   v[2].y=y+h; v[2].z=0;
    v[3].x=x+w; v[3].y=y;   v[3].z=0;
    v[4].x=x+w; v[4].y=y+h; v[4].z=0;
    v[5].x=x;   v[5].y=y+h; v[5].z=0;
    sceGuDrawArray(GU_TRIANGLES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, 6, 0, v);
}

/* -------------- Navigation helpers ------------------- */
static void move_lr(int dir){ /* -1 left, +1 right within row */
    int r=buttons[sel].r, c=buttons[sel].c, best=-1;
    for(int i=0;i<BTN_COUNT;i++){
        if(buttons[i].r!=r) continue;
        if(dir<0 && buttons[i].c<c && (best==-1 || buttons[i].c>buttons[best].c)) best=i;
        if(dir>0 && buttons[i].c>c && (best==-1 || buttons[i].c<buttons[best].c)) best=i;
    }
    if(best!=-1) sel=best;
}
static void move_ud(int dir){ /* -1 up, +1 down (nearest column) */
    int r=buttons[sel].r, c=buttons[sel].c, target=r+dir;
    if(target<0 || target>4) return;
    int best=-1, diff=999;
    for(int i=0;i<BTN_COUNT;i++){
        if(buttons[i].r!=target) continue;
        int d=abs(buttons[i].c - c);
        if(d<diff){ diff=d; best=i; }
    }
    if(best!=-1) sel=best;
}

/* -------------- Actions by label --------------------- */
static void press_label(const char* L){
    if(strcmp(L,"AC")==0){ clear_all(); return; }
    if(strcmp(L,"+/-")==0){ toggle_sign(); return; }
    if(strcmp(L,"=")==0){ press_equal(); return; }
    if(strcmp(L,"%")==0){ double v=display_to_double(); set_display_double(v/100.0); return; }
    if(strcmp(L,".")==0){ input_digit('.'); return; }
    if(strcmp(L,"+")==0||strcmp(L,"-")==0||strcmp(L,"*")==0||strcmp(L,"/")==0){
        do_op(L[0]); clear_entry(); return;
    }
    if(L[0]>='0' && L[0]<='9' && L[1]=='\0'){ input_digit(L[0]); return; }
}

/* ----------------------------- MAIN ----------------------------- */
int main(void){
    /* GU init: double buffered, 16-bit (5650) */
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_5650, (void*)0, BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, (void*)FRAME_SIZE, BUF_WIDTH);
    sceGuDepthBuffer((void*)(FRAME_SIZE*2), BUF_WIDTH);
    sceGuOffset(2048 - (SCR_WIDTH/2), 2048 - (SCR_HEIGHT/2));
    sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuFinish(); sceGuSync(0,0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    /* One-time init for text */
    pspDebugScreenInit();

    /* Controller */
    SceCtrlData pad, old = {0};
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while(1){
        /* -------- GU pass -------- */
        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(COL_BG);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        sceGuDisable(GU_DEPTH_TEST);
        sceGuShadeModel(GU_FLAT);
        sceGumMatrixMode(GU_PROJECTION); sceGumLoadIdentity();
        sceGumOrtho(0, SCR_WIDTH, SCR_HEIGHT, 0, -1, 1);
        sceGumMatrixMode(GU_VIEW);  sceGumLoadIdentity();
        sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();

        /* display area */
        drawQuad(20, 40, 440, 60, COL_DISPLAY);

        /* buttons */
        for(int i=0;i<BTN_COUNT;i++){
            Btn *b=&buttons[i];
            unsigned int base = (b->type==BTN_OP_T)? COL_OP : (b->type==BTN_SPEC_T? COL_SPEC : COL_NUM);
            unsigned int col  = (i==sel)? COL_SEL : base;
            drawQuad(b->x, b->y, b->w, b->h, col);
        }

        sceGuFinish();
        sceGuSync(0,0);

        /* -------- swap & print text -------- */
        sceDisplayWaitVblankStart();
        void* fb = sceGuSwapBuffers();                 /* активный буфер показа */
        pspDebugScreenSetBase((u32*)fb);               /* направили печать в него */

        /* title */
        pspDebugScreenSetTextColor(COL_TEXT_WHITE);
        pspDebugScreenSetXY(2,1);
        pspDebugScreenPrintf("iOS Calculator by Serge Legran");

        /* number (right aligned) */
        {
            int cols = 55; /* ~440px / 8 */
            int startCol = 20/8 + (cols - (int)strlen(display) - 1);
            if(startCol < 3) startCol = 3;
            int row = 40/8 + 2;
            pspDebugScreenSetTextColor(COL_TEXT_BLACK);
            pspDebugScreenSetXY(startCol, row);
            pspDebugScreenPrintf("%s", display);
        }

        /* button labels */
        pspDebugScreenSetTextColor(COL_TEXT_WHITE);
        for(int i=0;i<BTN_COUNT;i++){
            Btn *b=&buttons[i];
            int len=(int)strlen(b->label);
            int col=(b->x + b->w/2)/8 - (len/2);
            int row=(b->y + b->h/2)/8;
            if(col<0) col=0; if(row<0) row=0;
            pspDebugScreenSetXY(col,row);
            pspDebugScreenPrintf("%s", b->label);
        }

        /* -------- input -------- */
        sceCtrlReadBufferPositive(&pad,1);
        if((pad.Buttons & PSP_CTRL_START) && !(old.Buttons & PSP_CTRL_START)) break;

        if((pad.Buttons & PSP_CTRL_LEFT)  && !(old.Buttons & PSP_CTRL_LEFT))  move_lr(-1);
        if((pad.Buttons & PSP_CTRL_RIGHT) && !(old.Buttons & PSP_CTRL_RIGHT)) move_lr(+1);
        if((pad.Buttons & PSP_CTRL_UP)    && !(old.Buttons & PSP_CTRL_UP))    move_ud(-1);
        if((pad.Buttons & PSP_CTRL_DOWN)  && !(old.Buttons & PSP_CTRL_DOWN))  move_ud(+1);

        if((pad.Buttons & PSP_CTRL_CROSS) && !(old.Buttons & PSP_CTRL_CROSS)) press_label(buttons[sel].label);
        if((pad.Buttons & PSP_CTRL_SQUARE)&& !(old.Buttons & PSP_CTRL_SQUARE)) backspace();
        if((pad.Buttons & PSP_CTRL_CIRCLE)&& !(old.Buttons & PSP_CTRL_CIRCLE)) clear_all();
        if((pad.Buttons & PSP_CTRL_TRIANGLE)&&!(old.Buttons & PSP_CTRL_TRIANGLE)) press_equal();
        if((pad.Buttons & PSP_CTRL_SELECT)&& !(old.Buttons & PSP_CTRL_SELECT)) clear_entry();
        if((pad.Buttons & PSP_CTRL_LTRIGGER)&&!(old.Buttons & PSP_CTRL_LTRIGGER)) toggle_sign();
        if((pad.Buttons & PSP_CTRL_RTRIGGER)&&!(old.Buttons & PSP_CTRL_RTRIGGER)) toggle_sign();

        old = pad;

        /* не даём устройству «уснуть» */
        sceKernelPowerTick(0);
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
