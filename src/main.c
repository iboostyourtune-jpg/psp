#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>   // нужно для atof()
#include <math.h>

PSP_MODULE_INFO("CalcIOS", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define printf pspDebugScreenPrintf

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

/* только ASCII */
static const char* rows[5][4]={
    {"AC","+/-","%","/"},
    {"7","8","9","*"},
    {"4","5","6","-"},
    {"1","2","3","+"},
    {"0",".","<-","="}
};
static int sel_r=3, sel_c=0;

static void draw_ui(void){
    pspDebugScreenClear();

    pspDebugScreenSetXY(0,0);
    printf("iOS-style Calc  (X=press  O=AC  []=Back  /\\=Equal)\n");

    pspDebugScreenSetXY(1,2); printf("+-----------------------------+");
    pspDebugScreenSetXY(1,3);
    char buf[32]; snprintf(buf,sizeof(buf),"%27s",display);
    printf("|%s|", buf);
    pspDebugScreenSetXY(1,4); printf("+-----------------------------+");

    int y0=6;
    for(int r=0;r<5;++r){
        for(int c=0;c<4;++c){
            int x = 2 + c*8;
            int y = y0 + r*2;
            const char* label = rows[r][c];
            int sel = (r==sel_r && c==sel_c);
            pspDebugScreenSetXY(x,y);
            if(sel) printf("[%3s]", label);
            else    printf(" %3s ", label);
        }
    }

    pspDebugScreenSetXY(0,18);
    printf("D-Pad move  X press  O AC  [] Backspace\n");
    pspDebugScreenSetXY(0,19);
    printf("/\\ =   L/R +/-   SELECT CE   START quit\n");
}

static void press_button(const char* label){
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
    pspDebugScreenInit();
    SceCtrlData pad, old={0};
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while(1){
        draw_ui();
        sceCtrlReadBufferPositive(&pad,1);

        if((pad.Buttons&PSP_CTRL_START)&&!(old.Buttons&PSP_CTRL_START)) break;

        if((pad.Buttons&PSP_CTRL_UP)   && !(old.Buttons&PSP_CTRL_UP))   { if(sel_r>0) sel_r--; }
        if((pad.Buttons&PSP_CTRL_DOWN) && !(old.Buttons&PSP_CTRL_DOWN)) { if(sel_r<4) sel_r++; }
        if((pad.Buttons&PSP_CTRL_LEFT) && !(old.Buttons&PSP_CTRL_LEFT)) { if(sel_c>0) sel_c--; }
        if((pad.Buttons&PSP_CTRL_RIGHT)&& !(old.Buttons&PSP_CTRL_RIGHT)){ if(sel_c<3) sel_c++; }

        if((pad.Buttons&PSP_CTRL_CROSS)   && !(old.Buttons&PSP_CTRL_CROSS))   press_button(rows[sel_r][sel_c]);
        if((pad.Buttons&PSP_CTRL_CIRCLE)  && !(old.Buttons&PSP_CTRL_CIRCLE))  clear_all();
        if((pad.Buttons&PSP_CTRL_SQUARE)  && !(old.Buttons&PSP_CTRL_SQUARE))  backspace();
        if((pad.Buttons&PSP_CTRL_TRIANGLE)&& !(old.Buttons&PSP_CTRL_TRIANGLE))press_equal();
        if((pad.Buttons&PSP_CTRL_LTRIGGER)&& !(old.Buttons&PSP_CTRL_LTRIGGER))toggle_sign();
        if((pad.Buttons&PSP_CTRL_RTRIGGER)&& !(old.Buttons&PSP_CTRL_RTRIGGER))toggle_sign();
        if((pad.Buttons&PSP_CTRL_SELECT)  && !(old.Buttons&PSP_CTRL_SELECT))  clear_entry();

        old = pad;
        sceDisplayWaitVblankStart();
    }

    sceKernelExitGame();
    return 0;
}
