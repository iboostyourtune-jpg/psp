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

#define BUF_WIDTH   512
#define SCR_WIDTH   480
#define SCR_HEIGHT  272
#define PIXEL_SIZE  4
#define FRAME_SIZE  (BUF_WIDTH*SCR_HEIGHT*PIXEL_SIZE)

static unsigned int __attribute__((aligned(16))) list[262144];

/* -------- калькулятор -------- */
static char display[64] = "0";
static double acc = 0.0;
static char op = 0;
static int entering_second = 0;
static int has_dot = 0;

static void set_display_double(double v){
    char buf[64]; snprintf(buf,sizeof(buf),"%.10g",v);
    strncpy(display,buf,sizeof(display));
    display[sizeof(display)-1]=0;
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
    if(display[0]=='-'){ memmove(display,display+1,strlen(display)); }
    else{ size_t len=strlen(display); if(len>=31) return; memmove(display+1,display,len+1); display[0]='-'; }
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

/* -------- цвета ABGR -------- */
static const unsigned int COL_BG      = 0xFF000000; /* фон */
static const unsigned int COL_DISPLAY = 0xFFDDDDDD; /* дисплей */
static const unsigned int COL_NUM     = 0xFF333333; /* цифры */
static const unsigned int COL_SPEC    = 0xFFA5A5A5; /* AC, +/-, % */
static const unsigned int COL_OP      = 0xFF0095FF; /* оранжевые (ABGR: #FF9500) */
static const unsigned int COL_SEL     = 0xFFFFFF55; /* выделение */
static const unsigned int COL_FRAME   = 0xFF000000; /* рамка */
static const unsigned int COL_TEXT    = 0xFFFFFFFF; /* белый текст */
static const unsigned int COL_TEXT_BLACK=0xFF000000; /* чёрный текст */

/* -------- простой 8x8 шрифт -------- */
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
    {'C',{0x3C,0x42,0x40,0x40,0x40,0x40,0x42,0x3C}},
    {'L',{0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x7E}},
    {'O',{0x3C,0x42,0x42,0x42,0x42,0x42,0x42,0x3C}},
    {'R',{0x7C,0x42,0x42,0x7C,0x48,0x44,0x42,0x41}},
    {'S',{0x3C,0x42,0x40,0x3C,0x02,0x02,0x42,0x3C}},
    {'E',{0x7E,0x40,0x40,0x7C,0x40,0x40,0x40,0x7E}},
    {'G',{0x3C,0x42,0x40,0x4E,0x42,0x42,0x42,0x3C}},
    {'N',{0x42,0x62,0x52,0x4A,0x46,0x42,0x42,0x42}},
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

typedef struct { float x,y,z; } Vertex;
static inline void drawPixel(int x,int y,int s,unsigned int color){
    sceGuColor(color);
    Vertex* v=(Vertex*)sceGuGetMemory(6*sizeof(Vertex));
    v[0].x=x; v[0].y=y; v[0].z=0;
    v[1].x=x+s; v[1].y=y; v[1].z=0;
    v[2].x=x; v[2].y=y+s; v[2].z=0;
    v[3].x=x+s; v[3].y=y; v[3].z=0;
    v[4].x=x+s; v[4].y=y+s; v[4].z=0;
    v[5].x=x; v[5].y=y+s; v[5].z=0;
    sceGuDrawArray(GU_TRIANGLES,GU_VERTEX_32BITF|GU_TRANSFORM_2D,6,0,v);
}
static void drawChar(int x,int y,char c,int s,unsigned int color){
    const u8* g=glyphFor(c); if(!g) return;
    for(int r=0;r<8;r++){ u8 bits=g[r]; for(int col=0;col<8;col++) if(bits&(0x80>>col)) drawPixel(x+col*s,y+r*s,s,color); }
}
static void drawText(int x,int y,const char* txt,int s,unsigned int color){
    int cx=x; for(const char* p=txt;*p;p++){ if(*p==' '){ cx+=8*s; continue;} drawChar(cx,y,*p,s,color); cx+=8*s; }
}

/* -------- кнопки -------- */
typedef struct { int x,y,w,h; const char* label; int type; } Btn;
enum {BTN_NUM,BTN_SPEC,BTN_OP};

static Btn buttons[]={
    {20,110,104,30,"AC",BTN_SPEC}, {134,110,104,30,"+/-",BTN_SPEC}, {248,110,104,30,"%",BTN_SPEC}, {362,110,104,30,"/",BTN_OP},
    {20,145,104,30,"7",BTN_NUM}, {134,145,104,30,"8",BTN_NUM}, {248,145,104,30,"9",BTN_NUM}, {362,145,104,30,"*",BTN_OP},
    {20,180,104,30,"4",BTN_NUM}, {134,180,104,30,"5",BTN_NUM}, {248,180,104,30,"6",BTN_NUM}, {362,180,104,30,"-",BTN_OP},
    {20,215,104,30,"1",BTN_NUM}, {134,215,104,30,"2",BTN_NUM}, {248,215,104,30,"3",BTN_NUM}, {362,215,104,30,"+",BTN_OP},
    {20,250,218,30,"0",BTN_NUM}, {248,250,104,30,".",BTN_NUM}, {362,250,104,30,"=",BTN_OP}
};
static const int BTN_COUNT=sizeof(buttons)/sizeof(buttons[0]);
static int sel=0;

/* -------- отрисовка -------- */
static void drawQuad(int x,int y,int w,int h,unsigned int color){
    sceGuColor(color);
    Vertex* v=(Vertex*)sceGuGetMemory(6*sizeof(Vertex));
    v[0].x=x; v[0].y=y; v[0].z=0;
    v[1].x=x+w; v[1].y=y; v[1].z=0;
    v[2].x=x; v[2].y=y+h; v[2].z=0;
    v[3].x=x+w; v[3].y=y; v[3].z=0;
    v[4].x=x+w; v[4].y=y+h; v[4].z=0;
    v[5].x=x; v[5].y=y+h; v[5].z=0;
    sceGuDrawArray(GU_TRIANGLES,GU_VERTEX_32BITF|GU_TRANSFORM_2D,6,0,v);
}
static void render(void){
    sceGuStart(GU_DIRECT,list);
    sceGuClearColor(COL_BG);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_FLAT);
    sceGumMatrixMode(GU_PROJECTION); sceGumLoadIdentity();
    sceGumOrtho(0,SCR_WIDTH,SCR_HEIGHT,0,-1,1);
    sceGumMatrixMode(GU_VIEW); sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();

    /* дисплей */
    drawQuad(20,40,440,60,COL_DISPLAY);
    drawText(30,10,"iOS Calculator by Serge Legran",2,COL_TEXT);

    /* число справа */
    { int s=3; char buf[48]; snprintf(buf,sizeof(buf),"%s",display);
      int w=strlen(buf)*8*s; int tx=20+440-10-w; if(tx<24) tx=24;
      int ty=40+(60-8*s)/2; drawText(tx,ty,buf,s,COL_TEXT_BLACK); }

    /* кнопки */
    for(int i=0;i<BTN_COUNT;i++){
        Btn b=buttons[i];
        unsigned int col=(b.type==BTN_OP)?COL_OP:(b.type==BTN_SPEC?COL_SPEC:COL_NUM);
        if(i==sel) col=COL_SEL;
        drawQuad(b.x,b.y,b.w,b.h,col);
        int s=2; int tw=strlen(b.label)*8*s;
        int tx=b.x+(b.w-tw)/2; int ty=b.y+(b.h-8*s)/2;
        drawText(tx,ty,b.label,s,COL_TEXT);
    }
    sceGuFinish(); sceGuSync(0,0);
}

/* -------- действия -------- */
static void press_label(const char* label){
    if(strcmp(label,"AC")==0){ clear_all(); return; }
    if(strcmp(label,"+/-")==0){ toggle_sign(); return; }
    if(strcmp(label,"=")==0){ press_equal(); return; }
    if(strcmp(label,"%")==0){ double v=display_to_double(); v/=100.0; set_display_double(v); return; }
    if(strcmp(label,".")==0){ input_digit('.'); return; }
    if(strcmp(label,"+")==0||strcmp(label,"-")==0||strcmp(label,"*")==0||strcmp(label,"/")==0){ do_op(label[0]); clear_entry(); return; }
    if(label[0]>='0'&&label[0]<='9'&&label[1]=='\0'){ input_digit(label[0]); return; }
}

/* -------- main -------- */
int main(){
    sceGuInit();
    sceGuStart(GU_DIRECT,list);
    sceGuDrawBuffer(GU_PSM_8888,(void*)0,BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH,SCR_HEIGHT,(void*)0,BUF_WIDTH);
    sceGuDepthBuffer((void*)FRAME_SIZE,BUF_WIDTH);
    sceGuOffset(2048-(SCR_WIDTH/2),2048-(SCR_HEIGHT/2));
    sceGuViewport(2048,2048,SCR_WIDTH,SCR_HEIGHT);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuFinish(); sceGuSync(0,0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    SceCtrlData pad,old={0};
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while(1){
        render();
        sceCtrlReadBufferPositive(&pad,1);
        if((pad.Buttons&PSP_CTRL_START)&&!(old.Buttons&PSP_CTRL_START)) break;
        if((pad.Buttons&PSP_CTRL_LEFT)&&!(old.Buttons&PSP_CTRL_LEFT))  { if(sel>0) sel--; }
        if((pad.Buttons&PSP_CTRL_RIGHT)&&!(old.Buttons&PSP_CTRL_RIGHT)){ if(sel<BTN_COUNT-1) sel++; }
        if((pad.Buttons&PSP_CTRL_UP)&&!(old.Buttons&PSP_CTRL_UP))      { if(sel-4>=0) sel-=4; }
        if((pad.Buttons&PSP_CTRL_DOWN)&&!(old.Buttons&PSP_CTRL_DOWN))  { if(sel+4<BTN_COUNT) sel+=4; }
        if((pad.Buttons&PSP_CTRL_CROSS)&&!(old.Buttons&PSP_CTRL_CROSS)) press_label(buttons[sel].label);
        old=pad; sceDisplayWaitVblankStart();
    }
    sceGuTerm(); sceKernelExitGame(); return 0;
}
