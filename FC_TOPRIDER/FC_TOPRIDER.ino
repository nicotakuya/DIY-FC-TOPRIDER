// DIY FC TOPRIDER by takuya matsubara
// for Atmega168/328  5V/16MHz(Arduino Pro/Pro Mini)

#include <Wire.h>
#include"8x8font.h"

#define USE_OLED  1      // OLED(SSD1306) :0=not use / 1=use

// BUTTON
#define BUTTON_PORT PORTB     // port
#define BUTTON_PIN  PINB      // pin
#define BUTTON_DDR  DDRB      // direction
#define BUTTON_BITACCEL  (1<<0) // アクセル
#define BUTTON_BITBRAKE  (1<<1) // ブレーキ
#define BUTTON_BITSELECT (1<<2) // セレクト
#define BUTTON_BITSTART  (1<<5) // スタート
#define BUTTON_BITSHIFT  (1<<4) // シフト

// FC
#define FC_PORT PORTD       // Data/Clock port
#define FC_PIN  PIND        // Data/Clock pin
#define FC_DDR  DDRD        // Data/Clock direction
#define FC_BITD2B3 (1<<2)   // P2.bit3 data mask
#define FC_BITD2B4 (1<<3)   // P2.bit4 data mask
#define FC_BITLATCH2 (1<<6) // LATCH2(OUT1) mask
#define FC_BITCLK2 (1<<7)   // P2 clock mask
#define UNTIL_FCCLK2_H while((FC_PIN&FC_BITCLK2)==0)
#define UNTIL_FCCLK2_L while((FC_PIN&FC_BITCLK2)!=0)
#define FC_D2B3_L  FC_PORT&=~FC_BITD2B3
#define FC_D2B3_H  FC_PORT|=FC_BITD2B3
#define FC_D2B4_L  FC_PORT&=~FC_BITD2B4
#define FC_D2B4_H  FC_PORT|=FC_BITD2B4
#define UNTIL_LATCH2_H while((FC_PIN&FC_BITLATCH2)==0)
#define UNTIL_LATCH2_L while((FC_PIN&FC_BITLATCH2)!=0)

#define CPUHZ 16000000  // CPU frequency[Hz]
#define PRS1  1         // timer1 prescaler
#define T1HZ  (CPUHZ/PRS1)  // timer1 freq.[Hz]
#define TIMER_10USEC   (unsigned int)(0x10000-(T1HZ/100000))
#define TIMER_1MSEC    (unsigned int)(0x10000-(T1HZ/1000))

// timer initialize
void timer_init(void)
{
  // timer1 prescaler
  TCCR1A = 0;
  TCCR1B = 1;
  // 0: No clock source (Timer/Counter stopped).
  // 1: clock /1 (No prescaling)
  // 2: clock /8 (From prescaler)
  // 3: clock /64 (From prescaler)
  // 4: clock /256 (From prescaler)
  // 5: clock /1024 (From prescaler)
}

//----- wait micro second
void timer_uswait(unsigned int limitcnt)
{
  TCNT1 = limitcnt;
  TIFR1 |= (1 << TOV1);  // clear TOV1
  while(!(TIFR1 & (1 << TOV1)));  // TIFR1が1になるまで待つ
}

//---- ADC
//---- AD init
void adc_init(void)
{
//#define ADCLOCK  0 // clock 1/2 
//#define ADCLOCK  1 // clock 1/2 
#define ADCLOCK  2 // clock 1/4 
//#define ADCLOCK  3 // clock 1/8 
//#define ADCLOCK  4 // clock 1/16 
//#define ADCLOCK  5 // clock 1/32 
//#define ADCLOCK  6 // clock 1/64 
//#define ADCLOCK  7 // clock 1/128     
  DDRC &= ~((1<<1)|(1<<0));   // input
  PORTC &= ~((1<<1)|(1<<0));  // no pull up
  ADCSRA = (1<<ADEN)|(0<<ADIE)|(1<<ADIF) | ADCLOCK;
// ADEN: ADC enable
// ADIF: ADC interrupt flag
// ADIE: ADC interrupt enable 1=enable/0=disable
  ADCSRB = 0;
}

//---- AD start(チャンネル番号) 戻り値は0～255
unsigned char adc_get(char adchan)
{
  int tempcnt;
  ADMUX = (1<<REFS0) | adchan; // select AD channel
  timer_uswait(TIMER_10USEC);
  ADCSRA |= (1<<ADSC);  // AD start
  while((ADCSRA & (1<<ADIF))==0){
  }
  tempcnt = (int)ADCL; 
  tempcnt += ((int)ADCH) << 8;
  return((unsigned char)(tempcnt >> 2));
}

//----VRAM
#define VRAMW 128 // [pixel]
#define VRAMH 64  // [pixel]
#define VRAMSIZE (VRAMW*VRAMH/8)
unsigned char vram[VRAMSIZE];
#define TEXTZOOMX 2
#define TEXTZOOMY 2
#define FONTW (TEXTZOOMX*8)
#define FONTH (TEXTZOOMY*8)

//---- vram all clear
void vram_clear(void)
{
  int vsize;
  unsigned char *p;
  p = &vram[0];
  vsize = VRAMSIZE;
  while(vsize--){
    *p++ = 0;
  }
}

//---- vram get
char vram_pget(unsigned char x,unsigned char y){
  int adr;
  unsigned char mask;
  if((x>=VRAMW)||(y>=VRAMH))return(0);
  adr = x+(VRAMW*(y/8));
  mask = (1<<(y % 8));
  if(vram[adr] & mask){
    return(1);
  }else{
    return(0);
  }
}

//---- vram put 
void vram_pset(unsigned char x,unsigned char y,char color){
  int adr;
  unsigned char mask;
  if((x>=VRAMW)||(y>=VRAMH))return;
  adr = x+(VRAMW*(y/8));
  mask = (1<<(y % 8));
  if(color==1){
    vram[adr] |= mask;
  }else if(color==0){
    vram[adr] &= ~mask;
  }else{
    vram[adr] ^= mask;
  }
}

//---- put chara( x,y,ch)
void vram_putch(unsigned char textx,unsigned char texty, unsigned char ch)
{
  char color;
  unsigned char i,j,bitdata;
  unsigned char x,y,xd,yd;
  PGM_P p;

  if(ch < 0x20)return;
  ch -= 0x20;
  p = (PGM_P)font;
  p += ((int)ch * 8);

  for(i=0 ;i<8 ;i++) {
    bitdata = pgm_read_byte(p++);
    for(j=0; j<8; j++){
      if(bitdata & ((1<<7)>>j)){
        color=1;
      }else{
        color = 0;
      }
      x = textx+(j*TEXTZOOMX);
      y = texty+(i*TEXTZOOMY);
      for(xd=0;xd<TEXTZOOMX;xd++){
        for(yd=0;yd<TEXTZOOMY;yd++){
          vram_pset(x+xd,y+yd,color);
        }
      }
    }
  }
}

//---- print HEX
void vram_puthex(unsigned char x,unsigned char y,char num)
{
  char temp;
  temp = ((num >> 4) & 0x0f)+'0';
  if(temp > '9')temp += ('A'-('9'+1));
  vram_putch(x,y,temp);
  x += FONTW;
  
  temp = (num & 0x0f)+'0';
  if(temp > '9')temp += ('A'-('9'+1));
  vram_putch(x,y,temp);
}

//---- print string on PROGMEM
void vram_putstr_pgm(unsigned char x,unsigned char y,PGM_P p)
{
  char temp;
  while(1){
    temp = pgm_read_byte(p++);
    if(temp==0)break;    
    vram_putch(x,y,temp);
    x += FONTW;
    if(x+(FONTW-1) >= VRAMW){
      x=0;
      y+=FONTH;
    }
  }
}

//---- print string
void vram_putstr(unsigned char x,unsigned char y,char *p)
{
  while(*p!=0){
    vram_putch(x,y,*p++);
    x += FONTW;
    if(x+(FONTW-1) >= VRAMW){
      x=0;
      y+=FONTH;
    }
  }
}

//---- box fill(X1,Y1,X2,Y2,color)
void vram_fill(int x1 ,int y1 ,int x2 ,int y2 ,char color)
{
  int x,y;

  for(y=y1; y<=y2; y++){
    for(x=x1; x<=x2; x++){
      vram_pset(x, y ,color); //ドット描画
    }
  }
}

//---- draw line(X1,Y1,X2,Y2,color)
void vram_line(int x1 ,int y1 ,int x2 ,int y2 ,char color)
{
  int xd;    // X2-X1座標の距離
  int yd;    // Y2-Y1座標の距離
  int xs=1;  // X方向の1pixel移動量
  int ys=1;  // Y方向の1pixel移動量
  int e;

  xd = x2 - x1;  // X2-X1座標の距離
  if(xd < 0){
    xd = -xd;  // X2-X1座標の絶対値
    xs = -1;    // X方向の1pixel移動量
  }
  yd = y2 - y1;  // Y2-Y1座標の距離
  if(yd < 0){
    yd = -yd;  // Y2-Y1座標の絶対値
    ys = -1;    // Y方向の1pixel移動量
  }
  vram_pset(x1, y1 ,color); //ドット描画
  e = 0;
  if( yd < xd ) {
    while( x1 != x2) {
      x1 += xs;
      e += (2 * yd);
      if(e >= xd) {
        y1 += ys;
        e -= (2 * xd);
      }
      vram_pset(x1, y1 ,color); //ドット描画
    }
  }else{
    while( y1 != y2) {
      y1 += ys;
      e += (2 * xd);
      if(e >= yd) {
        x1 += xs;
        e -= (2 * yd);
      }
      vram_pset(x1, y1 ,color); //ドット描画
    }
  }
}

//---- scroll
void vram_scroll(char x1,char y1){
  unsigned char x,y;
  char color;
  for(y=0;y<VRAMH;y++){
    for(x=0;x<VRAMW;x++){
      color = vram_pget(x+x1, y+y1);
      vram_pset(x,y,color);
    }
  }
}

//----OLED(SD1306)
#if USE_OLED

#define OLEDADDR 0x78 // SSD1306 slave address

// OLED(SSD1306)
#define SET_CONTRAST_CONTROL  0x81
#define SET_CHARGE_PUMP       0x8D 
#define SET_ADDRESSING_MODE   0x20
#define SET_DISPLAY_STARTLINE 0x40
#define SET_SEGMENT_REMAP     0xA1
#define SET_ENTIRE_DISPLAY    0xA4  
#define SET_DISPLAY_NORMAL    0xA6
#define SET_MULTIPLEX_RATIO   0xA8
#define SET_DISPLAY_ON        0xAF
#define SET_COM_OUTPUT_SCAN   0xC8
#define SET_DISPLAY_OFFSET    0xD3
#define SET_OSCILLATOR_FREQ   0xD5
#define SET_COM_PINS_HARDWARE 0xDA
#define SET_COLUMN_ADDRESS    0x21   //start,end
#define SET_PAGE_ADDRESS      0x22   //start,end

// SSD1306:
void oled_command(unsigned char data)
{
  Wire.beginTransmission(OLEDADDR >> 1);
  Wire.write(0b10000000); // control(single + command)
  Wire.write(data);             
  Wire.endTransmission();
}

//SSD1306:
void oled_command2(unsigned char data1,unsigned char data2)
{
  Wire.beginTransmission(OLEDADDR >> 1);
  Wire.write(0b00000000); // control(Continuation + command)
  Wire.write(data1);             
  Wire.write(data2);             
  Wire.endTransmission();
}

// SSD1306: initialize
void oled_init(void)
{
  Wire.setClock(400000);  
  delay(50);
  oled_command2(SET_MULTIPLEX_RATIO , 0x3F);  // multiplex ratio
  oled_command2(SET_DISPLAY_OFFSET,0);
  oled_command(SET_DISPLAY_STARTLINE);  // starting address of display RAM
  oled_command(SET_COM_OUTPUT_SCAN);
  oled_command(SET_SEGMENT_REMAP);  // column address and the segment driver
  oled_command2(SET_COM_PINS_HARDWARE, 0x12);
  oled_command2(SET_CONTRAST_CONTROL , 0x80);
  oled_command(SET_ENTIRE_DISPLAY); // entire display “ON” stage
  oled_command(SET_DISPLAY_NORMAL);
  oled_command2(SET_OSCILLATOR_FREQ  , 0x80);  
  oled_command2(SET_ADDRESSING_MODE  ,0); 
  oled_command2(SET_CHARGE_PUMP , 0x14);  // Enable charge pump
  oled_command(SET_DISPLAY_ON);
  delay(1);
  vram_line(0,0,VRAMW-1,VRAMH-1,1);
  vram_line(VRAMW-1,0,0,VRAMH-1,1);
  oled_redraw();
}

// SSD1306:screen update
void oled_redraw(void){
  int i,addr;

  Wire.beginTransmission(OLEDADDR >> 1);
  Wire.write(0b00000000); // control(Continuation + command)
  Wire.write(SET_COLUMN_ADDRESS);
  Wire.write(0);       // start column
  Wire.write(VRAMW-1); // end column
  Wire.write(SET_PAGE_ADDRESS);
  Wire.write(0);           // start page
  Wire.write((VRAMH/8)-1); // end page
  Wire.endTransmission();

  addr =0;
  while(addr < VRAMSIZE){  
    Wire.beginTransmission(OLEDADDR >> 1);
    Wire.write(0b01000000); //control(Continuation + data)
    for(i=0; i<8; i++){  
     Wire.write(vram[addr++]);
    }
    Wire.endTransmission();
  }
}
#endif // USE_OLED

//----デバッグ用 
void shiftsw_test(void)
{
  unsigned char btn;
  char shift;

#if USE_OLED
  vram_clear();
  vram_putstr(0,FONTH*0,"SHIFT");
#endif

  while(1){
    btn = ~BUTTON_PIN;
    if(btn & BUTTON_BITSHIFT){
      shift = 1; 
    }else{
      shift = 0;
    } 
    
#if USE_OLED
    vram_putch(112,FONTH*0,'0'+shift);
    oled_redraw();
#endif
    delay(16);
  }
}

//----デバッグ用 
void button_test(void)
{
  unsigned char btn;
  char accel,brake,select,start,shift;

#if USE_OLED
  vram_clear();
  vram_putstr(0,FONTH*0,"ACCEL");
  vram_putstr(0,FONTH*1,"BRAKE");
  vram_putstr(0,FONTH*2,"SELECT");
  vram_putstr(0,FONTH*3,"START");
  oled_redraw();
#endif

  while(1){
    btn = ~BUTTON_PIN; // ボタン=ONでビットが1になる
    if(btn & BUTTON_BITACCEL){
      accel = 1;
    } else {
      accel = 0;
    }
    if(btn & BUTTON_BITBRAKE){
      brake = 1;
    }else{
      brake = 0;
    }
    if(btn & BUTTON_BITSELECT){
      select = 1;
    } else {
      select = 0;
    }
    if(btn & BUTTON_BITSTART){
      start = 1;
    } else {
      start = 0;
    }
    if(btn & BUTTON_BITSHIFT){
      shift = 1; 
    } else {
      shift = 0;
    } 
    
#if USE_OLED
    vram_fill(FONTH*7,0,FONTH*8,63,0);
    vram_putch(FONTH*7,FONTH*0,'0'+accel);
    vram_putch(FONTH*7,FONTH*1,'0'+brake);
    vram_putch(FONTH*7,FONTH*2,'0'+select);
    vram_putch(FONTH*7,FONTH*3,'0'+start);
    oled_redraw();
#endif
    delay(16);
  }
}

//----デバッグ用 
void joystick_test()
{
  unsigned char adc_x,adc_y;
  int x,y;

  while(1){
    adc_x = adc_get(1);  // X座標
    adc_y = adc_get(0);  // Y座標
    adc_x = 0xff-adc_x;   // X座標の向きを反転
//    adc_y = 0xff-adc_y;   // Y座標の向きを反転 

#if USE_OLED
    vram_clear();
    vram_puthex(64,FONTH*0,adc_y);
    vram_puthex(64,FONTH*1,adc_x);
    x = adc_x/(256/VRAMH);
    y = adc_y/(256/VRAMH);
    vram_line(x-16,y,x+16,y,1);
    vram_line(x,y-16,x,y+16,1);
    oled_redraw();
#endif
    delay(16);
  }
}

//----デバッグ用 パルスカウント
void pulse_test(void){
#define TARGET FC_BITLATCH2
  char edge = 1;
  unsigned char pulsecnt=0;
  unsigned int tempcnt=0;

#if USE_OLED
  vram_clear();
  vram_putstr(0,FONTH*0,"PULSE");
  vram_putstr(0,FONTH*1,"COUNT");
  oled_redraw();
#endif

  cli(); //割り込み禁止
  TCNT1 = TIMER_1MSEC;
  TIFR1 |= (1 << TOV1);  // clear TOV1

  while(1){
    if(edge==0){
      if(FC_PIN & TARGET){ // 立ち上がり検出
        edge=1;
        pulsecnt++;
      }
    }else{
      if((FC_PIN & TARGET)==0){ // 立ち下がり検出
        edge=0;
      }
    }
    if(TIFR1 & (1 << TOV1)){  // 1mSec経過
      TCNT1 = TIMER_1MSEC;
      TIFR1 |= (1 << TOV1);  // clear TOV1
      tempcnt++;
      if(tempcnt >= 1000){ // 1Sec経過
        tempcnt = 0;
#if USE_OLED
        sei(); //割り込み許可
        vram_fill(32,32,32+15,32+31,0);
        vram_puthex(32,32,pulsecnt);
        oled_redraw();
        cli(); //割り込み禁止
#endif
        pulsecnt=0;
      }
    }
  }
}

// 傾きテーブル
// Right.bit1/ Left.bit1/ Right.bit0/ Left.bit0
char angle_data[] ={
  0b0101,  // Left=3,Right=0
  0b0100,  // Left=2,Right=0
  0b0001,  // Left=1,Right=0
  0b0000,  // Left=0,Right=0
  0b0000,  // Left=0,Right=0
  0b0010,  // Left=0,Right=1
  0b1000,  // Left=0,Right=2
  0b1010   // Left=0,Right=3
};

//----
void pad_control(void)
{
  unsigned char adc_x,adc_y;
  unsigned char btn;
  int angle;
  unsigned char senddata3,senddata4;
  char loopcnt;

#if USE_OLED
  vram_clear();
  vram_putstr(0,FONTH*0,"TOP");
  vram_putstr(0,FONTH*1,"RIDER");
  oled_redraw();
#endif
  cli(); //割り込み禁止

  while(1){
    adc_x = adc_get(1);  // アナログスティック X方向
    adc_y = adc_get(0);  // アナログスティック Y方向
//    adc_x = 0xff-adc_x;   // X方向を反転
//    adc_y = 0xff-adc_y;   // Y方向を反転
    angle = adc_x / 32;   // X方向の角度(0～7) 3～4が水平

    // 送信データ
    senddata3 = 0xff;
    senddata4 = (~angle_data[angle] << 3) | 0x07; // 傾き

    btn = ~BUTTON_PIN; // ボタン=ONでビットが1になる
    if(btn & BUTTON_BITACCEL){
      senddata3 &= ~(1<<7);   // Turbo
//      senddata3 &= ~(1<<3);   // Fast
//      senddata3 &= ~(1<<2);   // Slow
    }
    if(btn & BUTTON_BITBRAKE){
      senddata4 &= ~(1<<2);
    }
    if(btn & BUTTON_BITSHIFT){
      senddata4 &= ~(1<<1);
    }
    if(btn & BUTTON_BITSTART){
      senddata3 &= ~(1<<1);
    }
    if(btn & BUTTON_BITSELECT){
      senddata3 &= ~(1<<0);
    }
    if(adc_y > (256*5/6)){
      senddata4 &= ~(1<<0); // ウイリー
    }

    UNTIL_LATCH2_H;  // ラッチ2=Hを待つ
    UNTIL_LATCH2_L;  // ラッチ2=Lを待つ

    // 上位から8bit送信
    loopcnt=8;
    while(loopcnt--){
      if(senddata3 & 0x80){
        FC_D2B3_H;
      }else{
        FC_D2B3_L; 
      }
      if(senddata4 & 0x80){
        FC_D2B4_H;  
      }else{
        FC_D2B4_L;  
      }
      senddata3 <<= 1;
      senddata4 <<= 1;
      UNTIL_FCCLK2_L; // CLK2=Lowを待つ
      UNTIL_FCCLK2_H; // CLK2=Highを待つ
    }
  } 
}

//----
void setup() {
  BUTTON_DDR &= ~(BUTTON_BITSTART + BUTTON_BITSELECT
    + BUTTON_BITACCEL + BUTTON_BITBRAKE + BUTTON_BITSHIFT );

  // PULL UP
  BUTTON_PORT |= (BUTTON_BITSTART + BUTTON_BITSELECT
    + BUTTON_BITACCEL + BUTTON_BITBRAKE + BUTTON_BITSHIFT );

  FC_DDR |= (FC_BITD2B3 + FC_BITD2B4);

  FC_DDR &= ~(FC_BITCLK2 + FC_BITLATCH2);
  FC_PORT |= (FC_BITCLK2 + FC_BITLATCH2 + FC_BITD2B3 + FC_BITD2B4);

#if USE_OLED
  oled_init();
#endif
  adc_init();

  // デバッグ用
  Serial.begin(115200);
  while (!Serial) {
  }
  delay(50);
  Serial.print("start");
  Serial.flush();
}

//----
void loop() {
//  shiftsw_test(); // デバッグ用　シフトスイッチのテスト
//  button_test();  // デバッグ用　ボタンのテスト
//  joystick_test(); // デバッグ用　アナログスティックのテスト
//  pulse_test(); // デバッグ用　パルスのカウント

  pad_control();
}
