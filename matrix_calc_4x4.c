#include <reg52.h>

typedef unsigned char u8;
typedef signed int s16;

/* ====== 74HC595 + 8位数码管（共阴）接口定义 ====== */
sbit SER   = P3^4;   /* DS */
sbit SRCLK = P3^5;   /* SHCP */
sbit RCLK  = P3^6;   /* STCP */

/* 位选（低有效，接三极管/驱动） */
const u8 DIG_SEL[8] = {
    0xFE, 0xFD, 0xFB, 0xF7,
    0xEF, 0xDF, 0xBF, 0x7F
};

/* 共阴段码：0~9、空白、负号 */
const u8 SEG_CODE[] = {
    0x3F, /*0*/ 0x06, /*1*/ 0x5B, /*2*/ 0x4F, /*3*/
    0x66, /*4*/ 0x6D, /*5*/ 0x7D, /*6*/ 0x07, /*7*/
    0x7F, /*8*/ 0x6F, /*9*/
    0x00, /*blank*/
    0x40  /*-*/
};

#define BLANK 10
#define MINUS 11

/* ====== 4x4矩阵 ====== */
static s16 A[4][4] = {
    {  1,  2,  3,  4 },
    {  5,  6,  7,  8 },
    {  9, 10, 11, 12 },
    { 13, 14, 15, 16 }
};

static s16 B[4][4] = {
    { 16, 15, 14, 13 },
    { 12, 11, 10,  9 },
    {  8,  7,  6,  5 },
    {  4,  3,  2,  1 }
};

static s16 C[4][4]; /* 结果矩阵 */

/* 显示缓冲：每位存 0~11 */
volatile u8 g_disp[8] = {BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK, BLANK};
volatile u8 g_scan_idx = 0;

/* ====== 基础延时 ====== */
void delay_ms(u16 ms)
{
    u16 i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 110; j++);
}

/* ====== 74HC595 串行移位输出 ====== */
void hc595_send_byte(u8 dat)
{
    u8 i;
    for (i = 0; i < 8; i++) {
        SRCLK = 0;
        SER = (dat & 0x80) ? 1 : 0;
        dat <<= 1;
        SRCLK = 1;
    }
}

/* 一次刷新一位 */
void seg_scan_once(void)
{
    u8 code;

    /* 熄屏，防止重影 */
    P2 = 0xFF;

    code = SEG_CODE[g_disp[g_scan_idx]];
    hc595_send_byte(code);
    RCLK = 0;
    RCLK = 1;

    P2 = DIG_SEL[g_scan_idx];

    g_scan_idx++;
    if (g_scan_idx >= 8) g_scan_idx = 0;
}

/* Timer0：约1ms中断，用于动态扫描 */
void timer0_init(void)
{
    TMOD &= 0xF0;
    TMOD |= 0x01;       /* 16位定时 */
    TH0 = 0xFC;         /* 11.0592MHz -> 约1ms */
    TL0 = 0x18;
    ET0 = 1;
    EA  = 1;
    TR0 = 1;
}

void timer0_isr(void) interrupt 1
{
    TH0 = 0xFC;
    TL0 = 0x18;
    seg_scan_once();
}

/* ====== 矩阵运算 ====== */
void mat_add_4x4(void)
{
    u8 i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            C[i][j] = A[i][j] + B[i][j];
}

void mat_sub_4x4(void)
{
    u8 i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            C[i][j] = A[i][j] - B[i][j];
}

void mat_mul_4x4(void)
{
    u8 i, j, k;
    s16 sum;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            sum = 0;
            for (k = 0; k < 4; k++)
                sum += A[i][k] * B[k][j];
            C[i][j] = sum;
        }
    }
}

/* ====== 显示格式：r c vvvv（8位）======
   [0]='r'索引(1~4), [1]='c'索引(1~4), [2]空白,
   [3]符号或空白, [4..7]数值(4位右对齐)
*/
void show_cell(u8 r, u8 c, s16 v)
{
    u16 t;

    g_disp[0] = r + 1;
    g_disp[1] = c + 1;
    g_disp[2] = BLANK;

    if (v < 0) {
        g_disp[3] = MINUS;
        t = (u16)(-v);
    } else {
        g_disp[3] = BLANK;
        t = (u16)v;
    }

    g_disp[7] = t % 10; t /= 10;
    g_disp[6] = t % 10; t /= 10;
    g_disp[5] = t % 10; t /= 10;
    g_disp[4] = t % 10;
}

void show_matrix_result(u16 stay_ms)
{
    u8 r, c;
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            show_cell(r, c, C[r][c]);
            delay_ms(stay_ms);
        }
    }
}

void main(void)
{
    P2 = 0xFF;
    timer0_init();

    while (1) {
        /* 1) A + B */
        mat_add_4x4();
        show_matrix_result(500);

        /* 2) A - B */
        mat_sub_4x4();
        show_matrix_result(500);

        /* 3) A x B */
        mat_mul_4x4();
        show_matrix_result(700);
    }
}
