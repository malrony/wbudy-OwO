/*****************************************************************************
* Peripherals such as temp sensor, light sensor, accelerometer,
* and trim potentiometer are monitored and values are written to
* the OLED display.
*
* Copyright(C) 2010, Embedded Artists AB
* All rights reserved.
*
******************************************************************************/


#include "lpc17xx_pinsel.h"
#include "lpc17xx_i2c.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_ssp.h"
#include "lpc17xx_adc.h"
#include "lpc17xx_timer.h"
#include "stdlib.h"
#include "joystick.h"
#include "pca9532.h"
#include "lpc17xx_dac.h"

#define NOTE_C4 261
#define NOTE_E4 329
#define NOTE_G4 392
#define NOTE_C5 523
#define NOTE_A3 220
#define NOTE_F3 174
#define NOTE_C6 1047
#define NOTE_E6 1319
#define NOTE_G6 1568
#define NOTE_C7 2093
#define NOTE_E5 659
#define NOTE_G5 784


#include "light.h"
#include "oled.h"
#include "temp.h"
#include "acc.h"


static uint32_t msTicks = 0;
static uint8_t buf[10];

static void intToString(int value, uint8_t* pBuf, uint32_t len, uint32_t base)
{
    static const char* pAscii = "0123456789abcdefghijklmnopqrstuvwxyz";
    int pos = 0;
    int tmpValue = value;

    // the buffer must not be null and at least have a length of 2 to handle one
    // digit and null-terminator
    if (pBuf == NULL || len < 2)
    {
        return;
    }

    // a valid base cannot be less than 2 or larger than 36
    // a base value of 2 means binary representation. A value of 1 would mean only zeros
    // a base larger than 36 can only be used if a larger alphabet were used.
    if (base < 2 || base > 36)
    {
        return;
    }

    // negative value
    if (value < 0)
    {
        tmpValue = -tmpValue;
        value = -value;
        pBuf[pos++] = '-';
    }

    // calculate the required length of the buffer
    do {
        pos++;
        tmpValue /= base;
    } while(tmpValue > 0);


    if (pos > len)
    {
        // the len parameter is invalid.
        return;
    }

    pBuf[pos] = '\0';

    do {
        pBuf[--pos] = pAscii[value % base];
        value /= base;
    } while(value > 0);

    return;

}

void SysTick_Handler(void) {
    msTicks++;
}

static uint32_t getTicks(void)
{
    return msTicks;
}

static void init_ssp(void)
{
    SSP_CFG_Type SSP_ConfigStruct;
    PINSEL_CFG_Type PinCfg;

    /*
    * Initialize SPI pin connect
    * P0.7 - SCK;
    * P0.8 - MISO
    * P0.9 - MOSI
    * P2.2 - SSEL - used as GPIO
    */
    PinCfg.Funcnum = 2;
    PinCfg.OpenDrain = 0;
    PinCfg.Pinmode = 0;
    PinCfg.Portnum = 0;
    PinCfg.Pinnum = 7;
    PINSEL_ConfigPin(&PinCfg);
    PinCfg.Pinnum = 8;
    PINSEL_ConfigPin(&PinCfg);
    PinCfg.Pinnum = 9;
    PINSEL_ConfigPin(&PinCfg);
    PinCfg.Funcnum = 0;
    PinCfg.Portnum = 2;
    PinCfg.Pinnum = 2;
    PINSEL_ConfigPin(&PinCfg);

    SSP_ConfigStructInit(&SSP_ConfigStruct);

    // Initialize SSP peripheral with parameter given in structure above
    SSP_Init(LPC_SSP1, &SSP_ConfigStruct);

    // Enable SSP peripheral
    SSP_Cmd(LPC_SSP1, ENABLE);

}

static void init_i2c(void)
{
    PINSEL_CFG_Type PinCfg;

    /* Initialize I2C2 pin connect */
    PinCfg.Funcnum = 2;
    PinCfg.Pinnum = 10;
    PinCfg.Portnum = 0;
    PINSEL_ConfigPin(&PinCfg);
    PinCfg.Pinnum = 11;
    PINSEL_ConfigPin(&PinCfg);

    // Initialize I2C peripheral
    I2C_Init(LPC_I2C2, 100000);

    /* Enable I2C1 operation */
    I2C_Cmd(LPC_I2C2, ENABLE);
}

static void init_adc(void)
{
    PINSEL_CFG_Type PinCfg;

    /*
    * Init ADC pin connect
    * AD0.0 on P0.23
    */
    PinCfg.Funcnum = 1;
    PinCfg.OpenDrain = 0;
    PinCfg.Pinmode = 0;
    PinCfg.Pinnum = 23;
    PinCfg.Portnum = 0;
    PINSEL_ConfigPin(&PinCfg);

    /* Configuration for ADC :
    * Frequency at 0.2Mhz
    * ADC channel 0, no Interrupt
    */
    ADC_Init(LPC_ADC, 200000);
    ADC_IntConfig(LPC_ADC,ADC_CHANNEL_0,DISABLE);
    ADC_ChannelCmd(LPC_ADC,ADC_CHANNEL_0,ENABLE);

}

static void init_dac(void) {
    PINSEL_CFG_Type PinCfg;
    PinCfg.Funcnum = 2;
    PinCfg.OpenDrain = 0;
    PinCfg.Pinmode = 0;
    PinCfg.Pinnum = 26;
    PinCfg.Portnum = 0;
    PINSEL_ConfigPin(&PinCfg);

    DAC_Init(LPC_DAC);
}

void play_sound(uint32_t freq, uint32_t duration_ms) {
    if (freq <= 0) return;

    uint32_t cycles = (freq * duration_ms) / 1000;

    uint32_t delay_val = 300000 / freq;

    for (uint32_t i = 0; i < cycles; i++) {
        DAC_UpdateValue(LPC_DAC, 0);
        for(volatile int d = 0; d < delay_val; d++);

        DAC_UpdateValue(LPC_DAC, 1023);
        for(volatile int d = 0; d < delay_val; d++);
    }

    DAC_UpdateValue(LPC_DAC, 0);
}

void music_hit(void) {
    play_sound(NOTE_A3, 200);
}

void music_miss(void) {
    play_sound(50, 200);
}

void music_game_over(void) {
    play_sound(NOTE_E4, 300);
    play_sound(NOTE_C4, 300);
    play_sound(NOTE_A3, 600);
}

int main (void)
{
    /*
    int32_t xoff = 0;
    int32_t yoff = 0;
    int32_t zoff = 0;

    int32_t t = 0;
    uint32_t lux = 0;
    uint32_t trim = 0;
    */

    int8_t x = 0;
    int8_t y = 0;
    int8_t z = 0;

    int32_t high_score = 0;
    uint8_t high_score_buf[10];

    uint8_t arrow_state = 0;
    uint8_t joy_val = 0;
    uint32_t start_time = 0;
    uint8_t hit = 0;
    int32_t total_score = 0;
    uint32_t game_start_time = 0;
    int32_t reaction_bonus = 0;
    uint8_t mistakes = 0;
    uint8_t score_buf[10];

    init_i2c();
    init_ssp();
    init_adc();

    oled_init();
    joystick_init();
    pca9532_init();

    GPIO_SetDir(2, 1<<0, 1);
    GPIO_SetDir(2, 1<<1, 1);

    GPIO_SetDir(0, 1<<27, 1);
    GPIO_SetDir(0, 1<<28, 1);
    GPIO_SetDir(2, 1<<13, 1);
    GPIO_SetDir(0, 1<<26, 1);

    GPIO_ClearValue(0, 1<<27);
    GPIO_ClearValue(0, 1<<28);
    GPIO_ClearValue(2, 1<<13);

    init_dac();

    acc_init();

    ADC_StartCmd(LPC_ADC, ADC_START_NOW);
    while (!(ADC_ChannelGetStatus(LPC_ADC, ADC_CHANNEL_0, ADC_DATA_DONE)));
    uint32_t adc_noise = ADC_ChannelGetData(LPC_ADC, ADC_CHANNEL_0);
    srand(adc_noise);

    eeprom_read((uint8_t*)&high_score, 0, 4);
    if (high_score < 0 || high_score > 9999) high_score = 0;
    /*
    light_init();

    temp_init (&getTicks);
    */


    if (SysTick_Config(SystemCoreClock / 1000)) {
    while (1); // Capture error
    }

    /*
    * Assume base board in zero-g position when reading first value.
    */
    /*
    xoff = 0-x;
    yoff = 0-y;
    zoff = 64-z;*/

    /*
    light_enable();
    light_setRange(LIGHT_RANGE_4000);
    */

    oled_clearScreen(OLED_COLOR_WHITE);

    /*
    oled_putString(1,1, (uint8_t*)"Temp : ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    oled_putString(1,9, (uint8_t*)"Light : ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    oled_putString(1,17, (uint8_t*)"Trimpot: ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    oled_putString(1,25, (uint8_t*)"Acc x : ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    oled_putString(1,33, (uint8_t*)"Acc y : ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    oled_putString(1,41, (uint8_t*)"Acc z : ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    */


    game_start_time = getTicks();
    while(1) {

      if (getTicks() - game_start_time >= 30000) {
              mistakes = 4;
          }

        arrow_state = rand() % 4;
        hit = 0;
        reaction_bonus = 0;

        oled_clearScreen(OLED_COLOR_WHITE);

        intToString(total_score, score_buf, 10, 10);
        oled_putString(1, 55, (uint8_t*)"SCORE:", 0, 1);
        oled_putString(35, 55, score_buf, 0, 1);
        intToString(high_score, high_score_buf, 10, 10);
        oled_putString(60, 55, (uint8_t*)"HS:", 0, 1);
        oled_putString(75, 55, high_score_buf, 0, 1);

        switch(arrow_state) {
            case 0: // GÓRA
                oled_putString(36, 20, (uint8_t*)" /\\ ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
                oled_putString(40, 28, (uint8_t*)" | ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
                oled_putString(40, 36, (uint8_t*)" | ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
                break;
            case 1: // PRAWO
            oled_putString(40, 28, (uint8_t*)"--->", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
            break;
            case 2: // DÓŁ
                oled_putString(40, 28, (uint8_t*)" | ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
                oled_putString(40, 36, (uint8_t*)" | ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
                oled_putString(36, 44, (uint8_t*)" \\/ ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
                break;
            case 3: // LEWO
                oled_putString(40, 28, (uint8_t*)"<---", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
            break;
        }

        uint8_t move_made = 0;
        for (int i = 0; i < 100; i++) {
            joy_val = joystick_read();

            if ((arrow_state == 0 && (joy_val & JOYSTICK_UP)) ||
                (arrow_state == 1 && (joy_val & JOYSTICK_RIGHT)) ||
                (arrow_state == 2 && (joy_val & JOYSTICK_DOWN)) ||
                (arrow_state == 3 && (joy_val & JOYSTICK_LEFT))) {

                hit = 1;
                reaction_bonus = (100 - i) / 10;
                break;
            }


            else if (joy_val != 0) {
                hit = 0; // To jest pudło
                move_made = 1;
                break; // Kończymy pętlę natychmiast po błędzie
            }
            Timer0_Wait(10);
        }

        /* Accelerometer */
        /*acc_read(&x, &y, &z);
        x = x+xoff;
        y = y+yoff;
        z = z+zoff; */

        /* Temperature */
        /* t = temp_read(); */

        /* light */
        /* lux = light_read(); */

        /* trimpot */
        /*ADC_StartCmd(LPC_ADC,ADC_START_NOW);
        //Wait conversion complete
        while (!(ADC_ChannelGetStatus(LPC_ADC,ADC_CHANNEL_0,ADC_DATA_DONE)));
        trim = ADC_ChannelGetData(LPC_ADC,ADC_CHANNEL_0);

        /* output values to OLED display

        intToString(t, buf, 10, 10);
        oled_fillRect((1+9*6),1, 80, 8, OLED_COLOR_WHITE);
        oled_putString((1+9*6),1, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

        intToString(lux, buf, 10, 10);
        oled_fillRect((1+9*6),9, 80, 16, OLED_COLOR_WHITE);
        oled_putString((1+9*6),9, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

        intToString(trim, buf, 10, 10);
        oled_fillRect((1+9*6),17, 80, 24, OLED_COLOR_WHITE);
        oled_putString((1+9*6),17, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

        intToString(x, buf, 10, 10);
        oled_fillRect((1+9*6),25, 80, 32, OLED_COLOR_WHITE);
        oled_putString((1+9*6),25, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

        intToString(y, buf, 10, 10);
        oled_fillRect((1+9*6),33, 80, 40, OLED_COLOR_WHITE);
        oled_putString((1+9*6),33, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

        intToString(z, buf, 10, 10);
        oled_fillRect((1+9*6),41, 80, 48, OLED_COLOR_WHITE);
        oled_putString((1+9*6),41, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE); */

        if (hit) {
            music_hit();
            int32_t points_gained = 5 + reaction_bonus;
            total_score += points_gained;

            oled_putString(1, 1, (uint8_t*)"TRAFIONY!", OLED_COLOR_BLACK , OLED_COLOR_WHITE);

            intToString(points_gained, buf, 10, 10);
            oled_putString(72, 1, (uint8_t*)"+", 0, 1);
            oled_putString(80, 1, buf, 0, 1);
        }
        else {
            mistakes++;
            music_miss();
            total_score -= 1;

            oled_putString(1, 1, (uint8_t*)"PUDLO...", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
            oled_putString(72, 1, (uint8_t*)" -1", 0, 1);
        }

        switch(mistakes) {
            case 0:
                pca9532_setLeds(0xFFFF, 0xFFFF);
                break;
            case 1:
                pca9532_setLeds(0x3F3F, 0xFFFF);
                break;
            case 2:
                pca9532_setLeds(0x0F0F, 0xFFFF);
                break;
            case 3:
                pca9532_setLeds(0x0303, 0xFFFF);
                break;
            case 4:
                pca9532_setLeds(0x0000, 0xFFFF);
                music_game_over();

            if (total_score > high_score) {
                high_score = total_score;
                eeprom_write((uint8_t*)&high_score, 0, 4);
            }

            oled_clearScreen(OLED_COLOR_WHITE);

            while(1) {
                oled_putString(22, 15, (uint8_t*)"GAME OVER!", 0, 1);
                oled_putString(22, 30, (uint8_t*)"SCORE:", 0, 1);
                intToString(total_score, score_buf, 10, 10);
                oled_putString(62, 30, score_buf, 0, 1);
                intToString(high_score, high_score_buf, 10, 10);
                oled_putString(22, 45, (uint8_t*)"HS:", 0, 1);
                oled_putString(62, 45, (uint8_t*)"    ", 0, 1);
                oled_putString(62, 45, high_score_buf, 0, 1);

                acc_read(&x, &y, &z);
                if (x > 50 || x < -50 || y > 50 || y < -50) {
                    high_score = 0;
                    eeprom_write((uint8_t*)&high_score, 0, 4);
                    music_miss();

                    oled_fillRect(62, 35, 90, 45, OLED_COLOR_WHITE);
                }

                if (joystick_read() & JOYSTICK_CENTER) {
                    total_score = 0;
                    mistakes = 0;
                    hit = 0;
                    game_start_time = getTicks();

                    // Przywracamy linijkę LED do stanu początkowego (wszystkie świecą)
                    pca9532_setLeds(0xFFFF, 0xFFFF);

                    oled_clearScreen(OLED_COLOR_WHITE);
                    oled_putString(30, 30, (uint8_t*)"NOWA GRA!", 0, 1);
                    Timer0_Wait(1000);

                    break; // Wychodzi z tej pętli while(1)
                }

                Timer0_Wait(100);
            }

            continue;
        }

        Timer0_Wait(500);
    }

}

void check_failed(uint8_t *file, uint32_t line)
{
    /* User can add his own implementation to report the file name and line number,
    ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

    /* Infinite loop */
    while(1);
}