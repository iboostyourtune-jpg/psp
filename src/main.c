#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

PSP_MODULE_INFO("CalcGU", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

/* --------------------- GU frame config --------------------- */
#define BUF_WIDTH   512
#define SCR_WIDTH   480
#define SCR_HEIGHT  272
#define PIXEL_SIZE  4
#define FRAME_SIZE  (BUF_WIDTH*SCR_HEIGHT*PIXEL_SIZE)

static unsigned int __attribute__((aligned(16))) list[262144];

/* --------------------- Calculator state -------------------- */
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

/* ----------------------- GUI colors ABGR ------------------- */
static const unsigned int COL_BG      = 0xFF202020; /* фон */
static const unsigned int COL_DISPLAY = 0xFFFFFFFF; /* белый дисплей */
static const unsigned int COL_NUM     = 0xFF3C3C3C; /* серые кнопки */
static const unsigned int COL_OP      = 0xFFFF9900; /* оранжевые операции */
static const unsigned int COL_SEL     = 0xFFFFCC55; /* выделение */
static const unsigned int COL_FRAME   = 0xFF000000; /* рамка */
static const unsigned int COL_TEXT    = 0xFF000000; /* чёрный текст */
static const unsigned int COL_TEXT_INV= 0xFFFFFFFF; /* белый текст */

/* ----------------------- Minimal 8x8 font ------------------ */
/* Битовая маска по 8 строк на символ; покрывает 0-9, A,C,B,S, + - * / % . = */
typedef unsigned char u8;
typedef struct { char ch; u8 row[8]; } Glyph;
static const Glyph FONT[] = {
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
    {'A',{0x18,0x24,0x42,0x42,0x7E,0x42,0x42,0x42}},
    {'B',{0x7C,0x42,0x42,0x7C,0x42,0x42,0x42,0x7C}},
    {'C',{0x3C,0x42,0x40,0x40,0x40,0x40,0x42,0x3C}},
    {'S',{0x3C,0x42,0x40,0x3C,0x02,0x02,0x42,0x3C}},
    {'+',{0x00,0x08,0x08,0x7F,0x08,0x08,0x00,0x00}},
    {'-',{0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}},
    {'*',{0x00,0x24,0x18,0x7E,0x18,0x24,0x00,0x00}},
    {'/',{0x02,0x04,0x08,0x10,0x20,0x40,0x00,0x00}},
    {'%',{0x62,0x64,0x08,0x10,0x20,0x4C,0x8C,0x00}},
    {'.',{0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}},
    {'=',{0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}},
};
static const int FONT_COUNT = sizeof(FONT)/sizeof(FONT[0]);

static const u8* glyphFor(char c){
    for(int i=0;i<FONT_COUNT;i++) if(FONT[i].ch==c) return FONT[i].row;
    return NULL;
}

/* Рисуем «пиксель» как маленький прямоугольник через GU */
typedef struct { float x,y,z; } Vertex;
static inline void drawPixelRect(int x,int y,int s,unsigned int color){
    sceGuColor(color);
    Vertex* v = (Vertex*)sceGuGetMemory(6 * sizeof(Vertex));
    v[0].x=x;     v[0].y=y;     v[0].z=0;
    v[1].x=x+s;   v[1].y=y;     v[1].z=0;
    v[2].x=x;     v[2].y=y+s;   v[2].z=0;
    v[3].x=x+s;   v[3].y=y;     v[3].z=0;
    v[4].x=x+s;   v[4].y=y+s;   v[4].z=0;
    v[5].x=x;     v[5].y=y+s;   v[5].z=0;
    sceGuDrawArray(GU_TRIANGLES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, 6, 0, v);
}

/* Рендер символа 8x8 бит, масштаб s (например, 2) */
static void drawChar(int x,int y,char c,int s,unsigned int color){
    const u8* g = glyphFor(c);
    if(!g) return;
    for(int r=0;r<8;r++){
        u8 bits = g[r];
        for(int col=0; col<8; col++){
            if(bits & (0x80 >> col)){
                drawPixelRect(x + col*s, y + r*s, s, color);
            }
        }
    }
}

/* Рендер строки (ASCII), моноширинный, ширина символа = 8*s */
static void drawText(int x,int y,const char* txt,int s,unsigned int color){
    int cx=x;
    for(const char* p=txt; *p; ++p){
        if(*p==' '){ cx += 8*s; continue; }
        drawChar(cx,y,*p,s,color);
        cx += 8*s;
    }
}

/* ----------------------- UI layout ------------------------- */
typedef struct { int x,y,w,h; const char* label; int isOp; } Btn;
static Btn buttons[] = {
    {  20,110,104,36,"AC",0 }, { 134,110,104,36,"+/-",0 }, { 248,110,104,36,"%",0 }, { 362,110,104,36,"/",1 },
    {  20,156,104,36,"7",0  }, { 134,156,104,36,"8",0  }, { 248,156,104,36,"9",0  }, { 362,156,104,36,"*",1 },
    {  20,202,104,36,"4",0  }, { 134,202,104,36,"5",0  }, { 248,202,104,36,"6",0  }, { 362,202,104,36,"-",1 },
    {  20,248,104,36,"1",0  }, { 134,248,104,36,"2",0  }, { 248,248,104,36,"3",0  }, { 362,248,104,36,"+",1 },
    /* влезаем по высоте — ряд 0 уже наверху, поэтому «0 . =» нарисуем поверх первых трёх: */
    /* На PSP 272px, поэтому оставим 4 ряда как сейчас. Кнопку 0 сделаем двойной ширины, заменив первые две позиции: */
};
static int sel_r=0, sel_c=0; /* 4x4 сетка */
static int selIndex(){ return sel_r*4 + sel_c; }

/* Прямоугольники */
static void drawQuad(int x,int y,int w,int h,unsigned int color){
    sceGuColor(color);
    Vertex* v = (Vertex*)sceGuGetMemory(6 * sizeof(Vertex));
    v[0].x=x;     v[0].y=y;     v[0].z=0;
    v[1].x=x+w;   v[1].y=y;     v[1].z=0;
    v[2].x=x;     v[2].y=y+h;   v[2].z=0;
    v[3].x=x+w;   v[3].y=y;     v[3].z=0;
    v[4].x=x+w;   v[4].y=y+h;   v[4].z=0;
    v[5].x=x;     v[5].y=y+h;   v[5].z=0;
    sceGuDrawArray(GU_TRIANGLES, GU_VERTEX_32BITF | GU_TRANSFORM_2D, 6, 0, v);
}
static void frameRect(int x,int y,int w,int h,unsigned int color){
    drawQuad(x, y, w, 1, color);
    drawQuad(x, y+h-1, w, 1, color);
    drawQuad(x, y, 1, h, color);
    drawQuad(x+w-1, y, 1, h, color);
}

/* ----------------------- Rendering ------------------------- */
static void begin2D(void){
    sceGuStart(GU_DIRECT, list);
    sceGuClearColor(COL_BG);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_FLAT);
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
    drawQuad(20, 24, 440, 60, COL_DISPLAY);
    frameRect(20, 24, 440, 60, COL_FRAME);

    /* четыре ряда по 4 кнопки */
    for(int i=0;i<16;i++){
        int r=i/4, c=i%4;
        Btn b = buttons[i];
        unsigned int base = b.isOp ? COL_OP : COL_NUM;
        unsigned int col  = (r==sel_r && c==sel_c) ? COL_SEL : base;
        drawQuad(b.x, b.y, b.w, b.h, col);
        frameRect(b.x, b.y, b.w, b.h, COL_FRAME);
        /* метка */
        int s=2; // масштаб шрифта
        int tw = (int)strlen(b.label) * 8 * s;
        int tx = b.x + (b.w - tw)/2;
        int ty = b.y + (b.h - 8*s)/2;
        drawText(tx, ty, b.label, s, COL_TEXT_INV);
    }

    /* нижняя строка 0 . = — поверх (сдвиг вниз на 44px), заменяя первые три ячейки */
    {
        int y = 248 + 44; /* за пределами — не влезет на 272px, значит оставим четыре ряда как есть */
        /* Ничего не рисуем — 4 ряда уже есть. Можно держать 0 . = на последнем ряду, как сейчас. */
    }

    /* число на дисплее, выравнивание вправо */
    {
        int s = 3; /* крупнее */
        char buf[48]; snprintf(buf,sizeof(buf),"%s", display);
        int w = (int)strlen(buf) * 8 * s;
        int tx = 20 + 440 - 10 - w; if(tx<24) tx=24;
        int ty = 24 + (60 - 8*s)/2;
        drawText(tx, ty, buf, s, COL_TEXT);
    }

    /* заголовок */
    drawText(24, 8, "iOS-style Calculator", 2, 0xFFFFFFFF);

    end2D();
}

/* ----------------------- Input->Actions -------------------- */
static void press_label(const char* label){
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

/* ----------------------- Main ------------------------------ */
int main(int argc, char* argv[]){
    /* GU init */
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

    /* Controls */
    SceCtrlData pad, old = {0};
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while(1){
        render();

        sceCtrlReadBufferPositive(&pad,1);
        if((pad.Buttons & PSP_CTRL_START) && !(old.Buttons & PSP_CTRL_START)) break;

        /* навигация по 4x4 */
        if((pad.Buttons & PSP_CTRL_LEFT)  && !(old.Buttons & PSP_CTRL_LEFT))  { if(sel_c>0) sel_c--; }
        if((pad.Buttons & PSP_CTRL_RIGHT) && !(old.Buttons & PSP_CTRL_RIGHT)) { if(sel_c<3) sel_c++; }
        if((pad.Buttons & PSP_CTRL_UP)    && !(old.Buttons & PSP_CTRL_UP))    { if(sel_r>0) sel_r--; }
        if((pad.Buttons & PSP_CTRL_DOWN)  && !(old.Buttons & PSP_CTRL_DOWN))  { if(sel_r<3) sel_r++; }

        int idx = selIndex();
        if(idx<0) idx=0; if(idx>15) idx=15;

        if((pad.Buttons & PSP_CTRL_CROSS)   && !(old.Buttons & PSP_CTRL_CROSS))   press_label(buttons[idx].label);
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
