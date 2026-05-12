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
#include "eeprom.h"

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

 static void play_sound(uint32_t freq, uint32_t duration_ms) {
    if (freq <= 0) return;

    uint32_t cycles = (freq * duration_ms) / 1000;

    uint32_t delay_val = 300000 / freq;

    for (uint32_t i = 0; i < cycles; i++) {
        DAC_UpdateValue(LPC_DAC, 0);
        for(volatile int d = 0; d < delay_val; d++) {}

        DAC_UpdateValue(LPC_DAC, 1023);
        for(volatile int d = 0; d < delay_val; d++) {}
    }

    DAC_UpdateValue(LPC_DAC, 0);
}

static void music_hit(void) {
    (void)play_sound(NOTE_A3, 200);
}

static void music_miss(void) {
    (void)play_sound(50, 200);
}

static void music_game_over(void) {
    (void)play_sound(NOTE_E4, 300);
    (void)play_sound(NOTE_C4, 300);
    (void)play_sound(NOTE_A3, 600);
}

int main (void)
{
    int8_t x = 0;
    int8_t y = 0;
    int8_t z = 0;

    int32_t high_score = 0;
    uint8_t high_score_buf[10];
    uint8_t score_buf[10];
    static uint8_t buf[10];

    uint8_t arrow_state = 0;
    uint8_t joy_val = 0;
    uint8_t hit = 0;
    int32_t total_score = 0;
    uint32_t game_start_time = 0;
    int32_t reaction_bonus = 0;
    uint8_t mistakes = 0;
    int32_t last_sec = -1; // Dodaj to dla naprawy migania czasu

    init_i2c();
    init_ssp();
    init_adc();
    oled_init();
    joystick_init();
    pca9532_init();
    init_dac();
    acc_init();

    // Inicjalizacja PRNG szumem ADC
    ADC_StartCmd(LPC_ADC, ADC_START_NOW);
    while (!(ADC_ChannelGetStatus(LPC_ADC, ADC_CHANNEL_0, ADC_DATA_DONE)))
    {
        /*Wait*/
    }
    uint32_t adc_noise = ADC_ChannelGetData(LPC_ADC, ADC_CHANNEL_0);
    (void)srand(adc_noise);

    (void)eeprom_read((uint8_t*)&high_score, 0, 4);
    if (high_score < 0 || high_score > 9999) high_score = 0;

    if (SysTick_Config(SystemCoreClock / 1000)) {
        while (1); 
    }

    oled_clearScreen(OLED_COLOR_WHITE);
    game_start_time = getTicks();

    while(1) {
        // NAPRAWIONO: Skasowane ukryte znaki Unicode
        int32_t elapsed = getTicks() - game_start_time;
        int32_t time_left = 30 - (elapsed / 1000);

        if (time_left <= 0) {
            time_left = 0;
            mistakes = 4;
        }

        // Naprawa migania i wyswietlanie czasu
        if (time_left != last_sec) {
            oled_fillRect(1, 30, 25, 40, OLED_COLOR_WHITE);
            intToString(time_left, buf, 10, 10);
            oled_putString(1, 30, buf, 0, 1);
            last_sec = time_left;
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
            case 0: // GORA
                oled_putString(36, 20, (uint8_t*)" /\\ ", 0, 1);
                oled_putString(40, 28, (uint8_t*)" | ", 0, 1);
                oled_putString(40, 36, (uint8_t*)" | ", 0, 1);
                break;
            case 1: // PRAWO
                oled_putString(40, 28, (uint8_t*)"--->", 0, 1);
                break;
            case 2: // DOL
                oled_putString(40, 28, (uint8_t*)" | ", 0, 1);
                oled_putString(40, 36, (uint8_t*)" | ", 0, 1);
                oled_putString(36, 44, (uint8_t*)" \\/ ", 0, 1);
                break;
            case 3: // LEWO
                oled_putString(40, 28, (uint8_t*)"<---", 0, 1);
                break;
            default: break;    
        }

        for (int i = 0; i < 100; i++) {
            joy_val = joystick_read();
            if (((arrow_state == 0U) && ((joy_val & JOYSTICK_UP) != 0U)) ||
            ((arrow_state == 1U) && ((joy_val & JOYSTICK_RIGHT) != 0U)) ||
            ((arrow_state == 2U) && ((joy_val & JOYSTICK_DOWN) != 0U)) ||
            ((arrow_state == 3U) && ((joy_val & JOYSTICK_LEFT) != 0U))) {
                hit = 1;
                reaction_bonus = (100 - i) / 10;
                break;
            }
            else if ((joy_val != 0U) && ((joy_val & JOYSTICK_CENTER) == 0U)) {
                hit = 0;
                break;
            }
            Timer0_Wait(10);
        }

        // NAPRAWIONO: Zakomentowane bloki peryferiow
        /* acc_read(&x, &y, &z);
        t = temp_read();
        lux = light_read();
        */

        if (hit) {
            music_hit();
            total_score += (5 + reaction_bonus);
            oled_putString(1, 1, (uint8_t*)"TRAFIONY!", 0, 1);
        } else {
            mistakes++;
            music_miss();
            total_score -= 1;
            oled_putString(1, 1, (uint8_t*)"PUDLO...", 0, 1);
        }

        switch(mistakes) {
            case 0: pca9532_setLeds(0xFFFF, 0xFFFF); break;
            case 1: pca9532_setLeds(0x3F3F, 0xFFFF); break;
            case 2: pca9532_setLeds(0x0F0F, 0xFFFF); break;
            case 3: pca9532_setLeds(0x0303, 0xFFFF); break;
            case 4:
                pca9532_setLeds(0x0000, 0xFFFF);
                music_game_over();
                if (total_score > high_score) {
                    high_score = total_score;
                    (void)eeprom_write((uint8_t*)&high_score, 0, 4);
                }
                oled_clearScreen(OLED_COLOR_WHITE);
                while(1) {
                    oled_putString(22, 15, (uint8_t*)"GAME OVER!", 0, 1);
                    intToString(total_score, score_buf, 10, 10);
                    oled_putString(22, 30, (uint8_t*)"SCORE:", 0, 1);
                    oled_putString(62, 30, score_buf, 0, 1);
                    
                    if (joystick_read() & JOYSTICK_CENTER) {
                        total_score = 0; mistakes = 0;
                        game_start_time = getTicks();
                        last_sec = -1;
                        pca9532_setLeds(0xFFFF, 0xFFFF);
                        break;
                    }
                    Timer0_Wait(100);
                }
                continue;
            default: break;    
        }
        Timer0_Wait(500);
    }
}