#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <psppower.h>

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

/* ---- UI geometry ---- */
#define DISPLAY_X   20
#define DISPLAY_Y   40
#define DISPLAY_W   440
#define DISPLAY_H   60
#define GAP_BELOW_DISPLAY  16   /* отступ снизу табло до 1-го ряда кнопок */

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
static const unsigned int COL_BG          = 0xFF000000; /* чёрный фон */
static const unsigned int COL_DISPLAY     = 0xFFDDDDDD; /* светло-серый дисплей */
static const unsigned int COL_NUM         = 0xFF333333; /* цифры (тёмно-графит) */
static const unsigned int COL_SPEC        = 0xFFA5A5A5; /* AC, +/-, % */
static const unsigned int COL_OP          = 0xFF0095FF; /* «оранжевые» у нас синеватые iOS-style */
static const unsigned int COL_TEXT_WHITE  = 0xFFFFFFFF;
static const unsigned int COL_TEXT_BLACK  = 0xFF000000;

/* --------- helpers: color blend (ABGR) ---------- */
static unsigned int lerpABGR(unsigned int a, unsigned int b, float t){
    if(t<0.f) t=0.f; if(t>1.f) t=1.f;
    unsigned int aa=(a>>24)&0xFF, ab=(a>>16)&0xFF, ag=(a>>8)&0xFF, ar=a&0xFF;
    unsigned int ba=(b>>24)&0xFF, bb=(b>>16)&0xFF, bg=(b>>8)&0xFF, br=b&0xFF;
    unsigned int ra=(unsigned int)(aa + (ba-aa)*t + 0.5f);
    unsigned int rb=(unsigned int)(ab + (bb-ab)*t + 0.5f);
    unsigned int rg=(unsigned int)(ag + (bg-ag)*t + 0.5f);
    unsigned int rr=(unsigned int)(ar + (br-ar)*t + 0.5f);
    return (ra<<24)|(rb<<16)|(rg<<8)|rr;
}

/* ---------------- 8x8 FONT (subset) ------------------ */
typedef unsigned char u8;
typedef struct { char ch; u8 row[8]; } Glyph;
static const Glyph FONT[] = {
    /* digits */
    {'0',{0x3C,0x42,0x46,0x4A,0x52,0x62,0x42,0x3C}},
    {'1',{0x08,0x18,0x28,0x08,0x08,0x08,0x08,0x3E}},
    {'2',{0x3C,0x42,0x02,0x0C,0x30,0x40,0x40,0x7E}},
    {'3',{0x3C,0x42,0x02,0x1C,0x02,0x02,0x42,0x3C}},
    {'4',{0x04,0x0C,0x14,0x24,0x44,0x7E,0x04,0x04}},
    {'5',{0x7E,0x40,0x40,0x7C,0x02,0x02,0x42,0x3C}},
    {'6',{0x1C,0x20,0x40,0x7C,0x42,0x42,0x42,0x3C}},
    {'7',{0x7E,0x02,0x04,0x08,0x10,0x20,0x20,0x20}},
    {'8',{0x3C,0x42,0x42,0x3C,0x42,0x42,0x42,0x3C}},
    {'9',{0x3C,0x42,0x42,0x3E,0x02,0x04,0x08,0x30}},
    {'.',{0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}},
    {'+',{0x00,0x08,0x08,0x7F,0x08,0x08,0x00,0x00}},
    {'-',{0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}},
    {'*',{0x00,0x24,0x18,0x7E,0x18,0x24,0x00,0x00}},
    {'/',{0x02,0x04,0x08,0x10,0x20,0x40,0x00,0x00}},
    {'%',{0x62,0x64,0x08,0x10,0x20,0x4C,0x8C,0x00}},
    {'=',{0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}},
    /* letters for title (UPPERCASE) */
    {'A',{0x18,0x24,0x42,0x42,0x7E,0x42,0x42,0x42}},
    {'B',{0x7C,0x42,0x42,0x7C,0x42,0x42,0x42,0x7C}},
    {'C',{0x3C,0x42,0x40,0x40,0x40,0x40,0x42,0x3C}},
    {'E',{0x7E,0x40,0x40,0x7C,0x40,0x40,0x40,0x7E}},
    {'G',{0x3C,0x42,0x40,0x4E,0x42,0x42,0x42,0x3C}},
    {'I',{0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x7E}},
    {'L',{0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x7E}},
    {'N',{0x42,0x62,0x52,0x4A,0x46,0x42,0x42,0x42}},
    {'O',{0x3C,0x42,0x42,0x42,0x42,0x42,0x42,0x3C}},
    {'R',{0x7C,0x42,0x42,0x7C,0x48,0x44,0x42,0x41}},
    {'S',{0x3C,0x42,0x40,0x3C,0x02,0x02,0x42,0x3C}},
    {'T',{0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x18}},
    {'U',{0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x3C}},
    {'Y',{0x42,0x42,0x24,0x18,0x18,0x18,0x18,0x18}},
    {' ',{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
};
static const int FONT_COUNT = sizeof(FONT)/sizeof(FONT[0]);

static const u8* glyphFor(char c){
    for(int i=0;i<FONT_COUNT;i++) if(FONT[i].ch==c) return FONT[i].row;
    return NULL;
}

/* -------- GU primitives -------- */
typedef struct { float x,y,z; } Vertex;

static void drawTriFan(Vertex* v, int n){
    sceGuDrawArray(GU_TRIANGLE_FAN, GU_VERTEX_32BITF|GU_TRANSFORM_2D, n, 0, v);
}

static void drawQuad(int x,int y,int w,int h,unsigned int color){
    sceGuColor(color);
    Vertex* v=(Vertex*)sceGuGetMemory(6*sizeof(Vertex));
    v[0].x=x;   v[0].y=y;   v[0].z=0;
    v[1].x=x+w; v[1].y=y;   v[1].z=0;
    v[2].x=x;   v[2].y=y+h; v[2].z=0;
    v[3].x=x+w; v[3].y=y;   v[3].z=0;
    v[4].x=x+w; v[4].y=y+h; v[4].z=0;
    v[5].x=x;   v[5].y=y+h; v[5].z=0;
    sceGuDrawArray(GU_TRIANGLES, GU_VERTEX_32BITF|GU_TRANSFORM_2D, 6, 0, v);
}

static void drawQuarter(int cx,int cy,int r,float a0,float a1,unsigned int color){
    const int SEG=14;
    sceGuColor(color);
    Vertex* v=(Vertex*)sceGuGetMemory((SEG+2)*sizeof(Vertex));
    v[0].x=(float)cx; v[0].y=(float)cy; v[0].z=0;
    for(int i=0;i<=SEG;i++){
        float a=a0 + (a1-a0)*(float)i/(float)SEG;
        v[i+1].x = cx + r*cosf(a);
        v[i+1].y = cy + r*sinf(a);
        v[i+1].z = 0;
    }
    drawTriFan(v, SEG+2);
}

static void drawRoundedRect(int x,int y,int w,int h,int r,unsigned int color){
    if(r<2) { drawQuad(x,y,w,h,color); return; }
    drawQuad(x+r, y,   w-2*r, h,     color);
    drawQuad(x,   y+r, r,     h-2*r, color);
    drawQuad(x+w-r, y+r, r,  h-2*r, color);
    int cx,cy;
    cx=x+r;     cy=y+r;     drawQuarter(cx,cy,r,(float)M_PI, (float)(1.5*M_PI), color);
    cx=x+w-r;   cy=y+r;     drawQuarter(cx,cy,r,(float)(1.5*M_PI), (float)(2.0*M_PI), color);
    cx=x+w-r;   cy=y+h-r;   drawQuarter(cx,cy,r,0.0f, (float)(0.5*M_PI), color);
    cx=x+r;     cy=y+h-r;   drawQuarter(cx,cy,r,(float)(0.5*M_PI), (float)M_PI, color);
}

/* Фонтовый рендер (через маленькие квадратики) */
static void drawGlyph(int x,int y,char c,int s,unsigned int color){
    const u8* g = glyphFor(c); if(!g) return;
    for(int r=0;r<8;r++){
        u8 bits = g[r];
        for(int col=0; col<8; col++){
            if(bits & (0x80>>col)){
                drawQuad(x + col*s, y + r*s, s, s, color);
            }
        }
    }
}
static void drawText(int x,int y,const char* txt,int s,unsigned int color){
    int cx=x;
    for(const char* p=txt; *p; ++p){
        if(*p=='\n'){ y += 9*s; cx = x; continue; }
        drawGlyph(cx,y,*p,s,color);
        cx += 8*s;
    }
}
static int textWidth(const char* txt,int s){
    int w=0; for(const char* p=txt; *p; ++p) w += 8*s; return w;
}

/* ---------------- Buttons (5 rows) + pulse ------------- */
typedef struct { int x,y,w,h; const char* label; int type; int r,c; float pulse; } Btn;
enum {BTN_NUM_T, BTN_SPEC_T, BTN_OP_T};

static const int LEFT=DISPLAY_X, BW=104, BH=30, GX=12, GY=8;
static const int TOP = DISPLAY_Y + DISPLAY_H + GAP_BELOW_DISPLAY;
static const int RADIUS = 8; /* скругление */

static Btn buttons[] = {
    /* r0 */
    {LEFT+0*(BW+GX), TOP+0*(BH+GY), BW,   BH,   "AC", BTN_SPEC_T, 0,0, 0.f},
    {LEFT+1*(BW+GX), TOP+0*(BH+GY), BW,   BH,   "+/-",BTN_SPEC_T, 0,1, 0.f},
    {LEFT+2*(BW+GX), TOP+0*(BH+GY), BW,   BH,   "%",  BTN_SPEC_T, 0,2, 0.f},
    {LEFT+3*(BW+GX), TOP+0*(BH+GY), BW,   BH,   "/",  BTN_OP_T,   0,3, 0.f},
    /* r1 */
    {LEFT+0*(BW+GX), TOP+1*(BH+GY), BW,   BH,   "7",  BTN_NUM_T,  1,0, 0.f},
    {LEFT+1*(BW+GX), TOP+1*(BH+GY), BW,   BH,   "8",  BTN_NUM_T,  1,1, 0.f},
    {LEFT+2*(BW+GX), TOP+1*(BH+GY), BW,   BH,   "9",  BTN_NUM_T,  1,2, 0.f},
    {LEFT+3*(BW+GX), TOP+1*(BH+GY), BW,   BH,   "*",  BTN_OP_T,   1,3, 0.f},
    /* r2 */
    {LEFT+0*(BW+GX), TOP+2*(BH+GY), BW,   BH,   "4",  BTN_NUM_T,  2,0, 0.f},
    {LEFT+1*(BW+GX), TOP+2*(BH+GY), BW,   BH,   "5",  BTN_NUM_T,  2,1, 0.f},
    {LEFT+2*(BW+GX), TOP+2*(BH+GY), BW,   BH,   "6",  BTN_NUM_T,  2,2, 0.f},
    {LEFT+3*(BW+GX), TOP+2*(BH+GY), BW,   BH,   "-",  BTN_OP_T,   2,3, 0.f},
    /* r3 */
    {LEFT+0*(BW+GX), TOP+3*(BH+GY), BW,   BH,   "1",  BTN_NUM_T,  3,0, 0.f},
    {LEFT+1*(BW+GX), TOP+3*(BH+GY), BW,   BH,   "2",  BTN_NUM_T,  3,1, 0.f},
    {LEFT+2*(BW+GX), TOP+3*(BH+GY), BW,   BH,   "3",  BTN_NUM_T,  3,2, 0.f},
    {LEFT+3*(BW+GX), TOP+3*(BH+GY), BW,   BH,   "+",  BTN_OP_T,   3,3, 0.f},
    /* r4 */
    {LEFT+0*(BW+GX), TOP+4*(BH+GY), BW*2+GX, BH,      "0",  BTN_NUM_T,  4,0, 0.f},
    {LEFT+2*(BW+GX), TOP+4*(BH+GY), BW,      BH,      ".",  BTN_NUM_T,  4,2, 0.f},
    {LEFT+3*(BW+GX), TOP+4*(BH+GY), BW,      BH,      "=",  BTN_OP_T,   4,3, 0.f},
};
static const int BTN_COUNT = sizeof(buttons)/sizeof(buttons[0]);
static int sel = 0;

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

/* ---------------- RENDER ------------------------------ */
static void render(){
    sceGuStart(GU_DIRECT, list);

    /* обязательные состояния каждый кадр */
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuShadeModel(GU_FLAT);
    sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);

    sceGumMatrixMode(GU_PROJECTION); sceGumLoadIdentity();
    sceGumOrtho(0, SCR_WIDTH, SCR_HEIGHT, 0, -1, 1);
    sceGumMatrixMode(GU_VIEW);  sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();

    sceGuClearColor(COL_BG);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    /* дисплей (rounded) */
    drawRoundedRect(DISPLAY_X, DISPLAY_Y, DISPLAY_W, DISPLAY_H, 10, COL_DISPLAY);

    /* заголовок (авто-масштаб) */
    const char* titleFull  = "IOS CALCULATOR BY SERGE LEGRAN";
    const char* titleShort = "IOS CALCULATOR";
    int ts = 2;                              /* базовый масштаб 2× */
    int maxW = SCR_WIDTH - 16;               /* поля по 8px */
    int tw = textWidth(titleFull, ts);
    if (tw > maxW) { ts = 1; tw = textWidth(titleFull, ts); }
    const char* titleToDraw = titleFull;
    if (tw > maxW) { titleToDraw = titleShort; tw = textWidth(titleToDraw, ts); }
    int tx = (SCR_WIDTH - tw) / 2;
    if (tx < 8) tx = 8;
    drawText(tx, 12, titleToDraw, ts, COL_TEXT_WHITE);

    /* число справа в дисплее */
    {
        int s = 3;
        int w = textWidth(display, s);
        int dx = DISPLAY_X + DISPLAY_W - 12 - w;             /* правый внутренний отступ */
        if(dx < DISPLAY_X + 4) dx = DISPLAY_X + 4;
        int dy = DISPLAY_Y + (DISPLAY_H - 8*s)/2;
        drawText(dx, dy, display, s, COL_TEXT_BLACK);
    }

    /* кнопки (rounded + лёгкая подсветка выбранной) */
    for(int i=0;i<BTN_COUNT;i++){
        Btn *b=&buttons[i];
        unsigned int base = (b->type==BTN_OP_T)? COL_OP : (b->type==BTN_SPEC_T? COL_SPEC : COL_NUM);
        float t = (i==sel ? 0.20f : 0.0f) + (b->pulse * 0.6f);
        if(t>0.9f) t=0.9f;
        unsigned int fill = lerpABGR(base, 0xFFFFFFFF, t);

        drawRoundedRect(b->x, b->y, b->w, b->h, RADIUS, fill);

        int s=2;
        int w=textWidth(b->label, s);
        int lx=b->x + (b->w - w)/2;
        int ly=b->y + (b->h - 8*s)/2;
        drawText(lx, ly, b->label, s, COL_TEXT_WHITE);

        b->pulse *= 0.86f;
        if(b->pulse < 0.01f) b->pulse = 0.f;
    }

    sceKernelDcacheWritebackAll();
    sceGuFinish();
    sceGuSync(0,0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

/* ----------------------------- MAIN ----------------------------- */
int main(void){
    /* GU init (double buffered, 16-bit) */
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_5650, (void*)0, BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, (void*)FRAME_SIZE, BUF_WIDTH);
    sceGuDepthBuffer((void*)(FRAME_SIZE*2), BUF_WIDTH);
    sceGuOffset(2048 - (SCR_WIDTH/2), 2048 - (SCR_HEIGHT/2));
    sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuFinish(); sceGuSync(0,0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    /* Controller */
    SceCtrlData pad, old = {0};
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while(1){
        render();

        /* input */
        sceCtrlReadBufferPositive(&pad,1);
        if((pad.Buttons & PSP_CTRL_START) && !(old.Buttons & PSP_CTRL_START)) break;

        if((pad.Buttons & PSP_CTRL_LEFT)  && !(old.Buttons & PSP_CTRL_LEFT))  move_lr(-1);
        if((pad.Buttons & PSP_CTRL_RIGHT) && !(old.Buttons & PSP_CTRL_RIGHT)) move_lr(+1);
        if((pad.Buttons & PSP_CTRL_UP)    && !(old.Buttons & PSP_CTRL_UP))    move_ud(-1);
        if((pad.Buttons & PSP_CTRL_DOWN)  && !(old.Buttons & PSP_CTRL_DOWN))  move_ud(+1);

        if((pad.Buttons & PSP_CTRL_CROSS) && !(old.Buttons & PSP_CTRL_CROSS)){
            press_label(buttons[sel].label);
            buttons[sel].pulse = 1.0f; /* вспышка */
        }
        if((pad.Buttons & PSP_CTRL_SQUARE)&& !(old.Buttons & PSP_CTRL_SQUARE)) backspace();
        if((pad.Buttons & PSP_CTRL_CIRCLE)&& !(old.Buttons & PSP_CTRL_CIRCLE)) clear_all();
        if((pad.Buttons & PSP_CTRL_TRIANGLE)&&!(old.Buttons & PSP_CTRL_TRIANGLE)) press_equal();
        if((pad.Buttons & PSP_CTRL_SELECT)&& !(old.Buttons & PSP_CTRL_SELECT)) clear_entry();
        if((pad.Buttons & PSP_CTRL_LTRIGGER)&&!(old.Buttons & PSP_CTRL_LTRIGGER)) toggle_sign();
        if((pad.Buttons & PSP_CTRL_RTRIGGER)&&!(old.Buttons & PSP_CTRL_RTRIGGER)) toggle_sign();

        old = pad;

        scePowerTick(0);
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
