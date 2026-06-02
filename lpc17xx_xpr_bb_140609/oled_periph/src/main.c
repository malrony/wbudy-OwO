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

#include "light.h"
#include "oled.h"
#include "temp.h"
#include "acc.h"

#ifndef __GNUC__

typedef struct {
    uint8_t Portnum;
    uint8_t Pinnum;
    uint8_t Funcnum;
    uint8_t Pinmode;
    uint8_t OpenDrain;
} PINSEL_CFG_Type;

typedef struct {
    uint32_t dummy; 
} SSP_CFG_Type;

typedef uint32_t OLED_COLOR_Type;

extern void PINSEL_ConfigPin(PINSEL_CFG_Type *PinCfg);
extern void SSP_ConfigStructInit(SSP_CFG_Type *SSP_ConfigStruct);
extern void SSP_Init(void *LPC_SSPx, SSP_CFG_Type *SSP_ConfigStruct);
extern void SSP_Cmd(void *LPC_SSPx, uint32_t NewState);
extern void I2C_Init(void *LPC_I2Cx, uint32_t clockrate);
extern void I2C_Cmd(void *LPC_I2Cx, uint32_t NewState);
extern void ADC_Init(void *LPC_ADCx, uint32_t rate);
extern void ADC_IntConfig(void *LPC_ADCx, uint32_t ADCIntType, uint32_t NewState);
extern void ADC_ChannelCmd(void *LPC_ADCx, uint8_t ADCChannel, uint32_t NewState);
extern void DAC_Init(void *LPC_DACx);
extern void DAC_UpdateValue(void *LPC_DACx, uint32_t dac_value);
extern void GPIO_SetDir(uint8_t portNum, uint32_t bitValue, uint8_t dir);
extern void GPIO_ClearValue(uint8_t portNum, uint32_t bitValue);
extern void ADC_StartCmd(void *LPC_ADCx, uint8_t start_mode);
extern uint32_t ADC_ChannelGetStatus(void *LPC_ADCx, uint8_t ADCChannel, uint32_t StatusType);
extern uint32_t ADC_ChannelGetData(void *LPC_ADCx, uint8_t ADCChannel);
extern uint32_t SysTick_Config(uint32_t ticks);

extern void oled_init(void);
extern void oled_clearScreen(OLED_COLOR_Type color);
extern void oled_putString(uint8_t x, uint8_t y, const uint8_t *pStr, OLED_COLOR_Type fbColor, OLED_COLOR_Type bgColor);
extern void oled_fillRect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, OLED_COLOR_Type color);
extern void joystick_init(void);
extern uint8_t joystick_read(void);
extern void pca9532_init(void);
extern void pca9532_setLeds(uint16_t ledOn, uint16_t ledOff);
extern void acc_init(void);
extern void acc_read(int8_t *x, int8_t *y, int8_t *z);
extern void eeprom_read(uint8_t *pBuffer, uint16_t addr, uint32_t len);
extern void eeprom_write(const uint8_t *pBuffer, uint16_t addr, uint32_t len);
extern void Timer0_Wait(uint32_t time);

#endif

void play_sound(uint32_t duration_ms);
void music_miss(void);
void check_failed(uint8_t *file, uint32_t line);

static uint32_t msTicks = 0;

static void intToString(int value, uint8_t* pBuf, uint32_t len, uint32_t base)
{
    static const char* pAscii = "0123456789abcdefghijklmnopqrstuvwxyz";
    int tmpValue = value;
    int local_value = value;

    // the buffer must not be null and at least have a length of 2 to handle one
    // digit and null-terminator
    // a valid base cannot be less than 2 or larger than 36
    if ((pBuf != NULL) && (len >= 2U) && (base >= 2U) && (base <= 36U))
    {

        int pos = 0;

        // negative value
        if (value < 0)
        {
            tmpValue = -tmpValue;
            local_value = -local_value;
            pBuf[pos] = '-';
            pos++;
        }

        // calculate the required length of the buffer
        do {
            pos++;
            tmpValue /= (int)base;
        } while(tmpValue > 0);

        if ((uint32_t)pos <= len)
        {
            pBuf[pos] = '\0';

            do {
                pos--;
                pBuf[pos] = pAscii[local_value % (int)base];
                local_value /= (int)base;
            } while(local_value > 0);
        }
    }
}

extern void SysTick_Handler(void);
extern void SysTick_Handler(void) {
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

    #ifndef __GNUC__
    SSP_ConfigStruct.dummy = 0U;
    (void)SSP_ConfigStruct.dummy;
    #endif

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

void play_sound(uint32_t duration_ms) {

    static const uint8_t audio_sample[] = {
        128, 153, 177, 199, 218, 233, 244, 251, 254, 251, 244, 233, 218, 199, 177, 153,
        128, 103,  79,  57,  38,  23,  12,   5,   2,   5,  12,  23,  38,  57,  79, 103
    };

    uint32_t start_time = getTicks();

    while ((getTicks() - start_time) < duration_ms) {
        for (uint32_t x = 0; x < sizeof(audio_sample); x++) {
            uint32_t dac_value = ((uint32_t)audio_sample[x] << 2);
            DAC_UpdateValue(LPC_DAC, dac_value);

            for(volatile uint32_t delay = 0U; delay < 120U; delay++) {
               (void)delay;
            }
        }
    }
}

static void music_hit(void) {
    play_sound(150);
}

void music_miss(void) {
    play_sound(50);
    Timer0_Wait(50);
    play_sound(50);
}

static void music_game_over(void) {
    play_sound(600);
}

int main (void)
{

    int8_t x = 0;
    int8_t y = 0;
    int8_t z = 0;

    int32_t high_score = 0;
    uint8_t high_score_buf[10];
    static uint8_t buf[10];

    uint8_t arrow_state = 0;
    uint8_t hit = 0;
    int32_t total_score = 0;
    uint32_t game_start_time = 0;
    int32_t reaction_bonus = 0;
    uint8_t mistakes = 0;
    uint8_t score_buf[10];

    (void)arrow_state;
    (void)hit;
    (void)reaction_bonus;

    init_i2c();
    init_ssp();
    init_adc();

    oled_init();
    joystick_init();
    pca9532_init();

    GPIO_SetDir(2, 1U<<0, 1);
    GPIO_SetDir(2, 1U<<1, 1);

    GPIO_SetDir(0, 1UL<<27, 1);
    GPIO_SetDir(0, 1UL<<28, 1);
    GPIO_SetDir(2, 1UL<<13, 1);
    GPIO_SetDir(0, 1UL<<26, 1);

    GPIO_ClearValue(0, 1UL<<27);
    GPIO_ClearValue(0, 1UL<<28);
    GPIO_ClearValue(2, 1UL<<13);

    init_dac();

    acc_init();

    ADC_StartCmd(LPC_ADC, ADC_START_NOW);
    while (!(ADC_ChannelGetStatus(LPC_ADC, ADC_CHANNEL_0, ADC_DATA_DONE)))
    {

    }
    uint32_t adc_noise = ADC_ChannelGetData(LPC_ADC, ADC_CHANNEL_0);
    srand(adc_noise);

    eeprom_read((uint8_t*)&high_score, 0, 4);
    if ((high_score < 0) || (high_score > 9999)) 
    {
        high_score = 0;
    }
    if (SysTick_Config(SystemCoreClock / 1000) != 0U) {
        while (1) 
        {

        }
    }

    oled_clearScreen(OLED_COLOR_WHITE);

    game_start_time = getTicks();
    while(1) {

        if ((getTicks() - game_start_time) >= 30000U) {
            mistakes = 4;
        }

        arrow_state = rand() % 4;
        hit = 0;
        reaction_bonus = 0;

        oled_clearScreen(OLED_COLOR_WHITE);

        intToString(total_score, score_buf, 10, 10);
        oled_putString(1, 55, (const uint8_t*)"SCORE:", 0, 1);
        oled_putString(35, 55, score_buf, 0, 1);
        intToString(high_score, high_score_buf, 10, 10);
        oled_putString(60, 55, (const uint8_t*)"HS:", 0, 1);
        oled_putString(75, 55, high_score_buf, 0, 1);

        switch(arrow_state) {
            case 0: // GÓRA
                oled_putString(36, 20, (const uint8_t*)" /\\ ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
                oled_putString(40, 28, (const uint8_t*)" | ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
                oled_putString(40, 36, (const uint8_t*)" | ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
                break;
            case 1: // PRAWO
            oled_putString(40, 28, (const uint8_t*)"--->", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
            break;
            case 2: // DÓŁ
                oled_putString(40, 28, (const uint8_t*)" | ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
                oled_putString(40, 36, (const uint8_t*)" | ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
                oled_putString(36, 44, (const uint8_t*)" \\/ ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
                break;
            case 3: // LEWO
                oled_putString(40, 28, (const uint8_t*)"<---", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
            break;
            default: break;
        }

        uint8_t move_made = 0;
        for (int i = 0; (i < 100) && (hit == 0U) && (move_made == 0U); i++) {
            uint8_t joy_val = joystick_read();

            if (((arrow_state == 0U) && ((joy_val & JOYSTICK_UP) != 0U)) ||
                ((arrow_state == 1U) && ((joy_val & JOYSTICK_RIGHT) != 0U)) ||
                ((arrow_state == 2U) && ((joy_val & JOYSTICK_DOWN) != 0U)) ||
                ((arrow_state == 3U) && ((joy_val & JOYSTICK_LEFT) != 0U))) {

                hit = 1;
                reaction_bonus = (100 - i) / 10;
            }
            else if (joy_val != 0U) {
                hit = 0; // To jest pudło
                move_made = 1;
            }
            else {

            }
            Timer0_Wait(10);
        }

        if (hit != 0U) {
            music_hit();
            int32_t points_gained = 5 + reaction_bonus;
            total_score += points_gained;

            oled_putString(1, 1, (const uint8_t*)"TRAFIONY!", OLED_COLOR_BLACK , OLED_COLOR_WHITE);

            intToString(points_gained, buf, 10, 10);
            oled_putString(72, 1, (const uint8_t*)"+", 0, 1);
            oled_putString(80, 1, buf, 0, 1);
        }
        else {
            mistakes++;
            music_miss();
            total_score -= 1;

            oled_putString(1, 1, (const uint8_t*)"PUDLO...", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
            oled_putString(72, 1, (const uint8_t*)" -1", 0, 1);
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
                oled_putString(22, 15, (const uint8_t*)"GAME OVER!", 0, 1);
                oled_putString(22, 30, (const uint8_t*)"SCORE:", 0, 1);
                intToString(total_score, score_buf, 10, 10);
                oled_putString(62, 30, score_buf, 0, 1);
                intToString(high_score, high_score_buf, 10, 10);
                oled_putString(22, 45, (const uint8_t*)"HS:", 0, 1);
                oled_putString(62, 45, (const uint8_t*)"    ", 0, 1);
                oled_putString(62, 45, high_score_buf, 0, 1);

                acc_read(&x, &y, &z);
                if (((x > 50) || (x < -50)) || ((y > 50) || (y < -50))) {
                    high_score = 0;
                    eeprom_write((uint8_t*)&high_score, 0, 4);
                    music_miss();

                    oled_fillRect(62, 35, 90, 45, OLED_COLOR_WHITE);
                }

                if (joystick_read() & JOYSTICK_CENTER) {
                    total_score = 0;
                    mistakes = 0;
                    (void)hit;
                    game_start_time = getTicks();

                    // Przywracamy linijkę LED do stanu początkowego (wszystkie świecą)
                    pca9532_setLeds(0xFFFF, 0xFFFF);

                    oled_clearScreen(OLED_COLOR_WHITE);
                    oled_putString(30, 30, (const uint8_t*)"NOWA GRA!", 0, 1);
                    Timer0_Wait(1000);

                    break; // Wychodzi z tej pętli while(1)
                }

                Timer0_Wait(100);
            }

            break;
            default: break;
        }

        Timer0_Wait(500);
    }

}

static void check_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    while(1) 
    {
        
    }
}