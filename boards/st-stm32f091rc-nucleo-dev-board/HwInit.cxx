/** \copyright
 * Copyright (c) 2018, Balazs Racz
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are  permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \file HwInit.cxx
 *
 * This file represents the hardware initialization for the STM32F091RC Nucelo
 * board with the DevKit IO board plugged in.
 *
 * @author Balazs Racz
 * @date April 18, 2018
 */

#include <new>
#include <cstdint>

#include "stm32f0xx_hal_rcc.h"
#include "stm32f0xx_hal_flash.h"
#include "stm32f0xx_hal_gpio.h"
#include "stm32f0xx_hal_gpio_ex.h"
#include "stm32f0xx_hal_dma.h"
#include "stm32f0xx_hal_tim.h"

#include "os/OS.hxx"
#include "Stm32Uart.hxx"
#include "Stm32Can.hxx"
#include "Stm32SPI.hxx"
#include "Stm32EEPROMEmulation.hxx"
#include "Stm32PWM.hxx"
#include "hardware.hxx"

/** override stdin */
const char *STDIN_DEVICE = "/dev/ser0";

/** override stdout */
const char *STDOUT_DEVICE = "/dev/ser0";

/** override stderr */
const char *STDERR_DEVICE = "/dev/ser0";

/** UART 0 serial driver instance */
static Stm32Uart uart0("/dev/ser0", USART2, USART2_IRQn);

/** CAN 0 CAN driver instance */
static Stm32Can can0("/dev/can0");

/** EEPROM emulation driver. The file size might be made bigger. */
static Stm32EEPROMEmulation eeprom0("/dev/eeprom", 1900);

/** How many bytes of flash should hold the entire dataset. Must be an integer
 * multiple of the minimum erase length (which is the flash page length, for
 * the STM32F0 it is 2 kbytes). The file size maximum is half this value. */
const size_t EEPROMEmulation::SECTOR_SIZE = 4096;

Stm32PWMGroup servo_timer(TIM3, (configCPU_CLOCK_HZ * 6 / 1000 + 65535) / 65536,
                          configCPU_CLOCK_HZ * 6 / 1000);

extern PWM* servo_channels[];
/// The order of these channels follows the schematic arrangement of MCU pins
/// to logical servo ports.
PWM *servo_channels[4] = { //
    servo_timer.get_channel(4), servo_timer.get_channel(2),
    servo_timer.get_channel(3), servo_timer.get_channel(1)};

/// Recursive mutex for SPI1 peripheral.
OSMutex spi1_lock(true);

static void noop_cs() {
}

/// SPI1 driver for io-board peripherals
static Stm32SPI spi1_0(
    "/dev/spi1.ioboard", SPI1, SPI1_IRQn, &noop_cs, &noop_cs, &spi1_lock);


static void spi1_ext_cs_assert() {
    EXT_CS_Pin::set(false);
}

static void spi1_ext_cs_deassert() {
    EXT_CS_Pin::set(true);
}

/// SPI1 driver for the expansion port.
static Stm32SPI spi1_1("/dev/spi1.ext", SPI1, SPI1_IRQn, &spi1_ext_cs_assert,
    &spi1_ext_cs_deassert, &spi1_lock);

/// SPI2 driver for the onboard input ports.
static Stm32SPI spi2("/dev/spi2", SPI2, SPI2_IRQn, &noop_cs, &noop_cs, nullptr);

extern "C" {

/** Blink LED */
uint32_t blinker_pattern = 0;
static uint32_t rest_pattern = 0;

void hw_set_to_safe(void)
{
}

void resetblink(uint32_t pattern)
{
    blinker_pattern = pattern;
    rest_pattern = pattern ? 1 : 0;
    BLINKER_RAW_Pin::set(pattern ? true : false);
    /* make a timer event trigger immediately */
}

void setblink(uint32_t pattern)
{
    resetblink(pattern);
}


const uint8_t AHBPrescTable[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
const uint8_t APBPrescTable[8]  = {0, 0, 0, 0, 1, 2, 3, 4};


void timer14_interrupt_handler(void)
{
    //
    // Clear the timer interrupt.
    //
    TIM14->SR = ~TIM_IT_UPDATE;

    // Set output LED.
    BLINKER_RAW_Pin::set(rest_pattern & 1);

    // Shift and maybe reset pattern.
    rest_pattern >>= 1;
    if (!rest_pattern)
    {
        rest_pattern = blinker_pattern;
    }
}

void diewith(uint32_t pattern)
{
    // vPortClearInterruptMask(0x20);
    asm("cpsie i\n");

    resetblink(pattern);
    while (1)
        ;
}

/** Setup the system clock */
static void clock_setup(void)
{
    /* reset clock configuration to default state */
    RCC->CR = RCC_CR_HSITRIM_4 | RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY))
        ;

#define USE_EXTERNAL_8_MHz_CLOCK_SOURCE 1
/* configure PLL:  8 MHz * 6 = 48 MHz */
#if USE_EXTERNAL_8_MHz_CLOCK_SOURCE
    RCC->CR |= RCC_CR_HSEON | RCC_CR_HSEBYP;
    while (!(RCC->CR & RCC_CR_HSERDY))
        ;
    RCC->CFGR = RCC_CFGR_PLLMUL6 | RCC_CFGR_PLLSRC_HSE_PREDIV | RCC_CFGR_SW_HSE;
    while (!((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_HSE))
        ;
#else
    RCC->CFGR = RCC_CFGR_PLLMUL6 | RCC_CFGR_PLLSRC_HSI_PREDIV | RCC_CFGR_SW_HSI;
    while (!((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_HSI))
        ;
#endif
    /* enable PLL */
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY))
        ;

    /* set PLL as system clock */
    RCC->CFGR = (RCC->CFGR & (~RCC_CFGR_SW)) | RCC_CFGR_SW_PLL;
    while (!((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL))
        ;
}

/** Initialize the processor hardware.
 */
void hw_preinit(void)
{
    /* Globally disables interrupts until the FreeRTOS scheduler is up. */
    asm("cpsid i\n");

    /* these FLASH settings enable opertion at 48 MHz */
    __HAL_FLASH_PREFETCH_BUFFER_ENABLE();
    __HAL_FLASH_SET_LATENCY(FLASH_LATENCY_1);

    /* setup the system clock */
    clock_setup();

    /* enable peripheral clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_CAN1_CLK_ENABLE();
    __HAL_RCC_TIM14_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();

    /* setup pinmux */
    GPIO_InitTypeDef gpio_init;

    /* USART2 pinmux on PA2 and PA3 */
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = GPIO_AF1_USART2;
    gpio_init.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOA, &gpio_init);
    gpio_init.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOA, &gpio_init);

    /* CAN pinmux on PB8 and PB9 */
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = GPIO_AF4_CAN;
    gpio_init.Pin = GPIO_PIN_8;
    HAL_GPIO_Init(GPIOB, &gpio_init);
    gpio_init.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOB, &gpio_init);

    /* SPI1 pinmux on PB3, PB4, and PB5 */
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = GPIO_AF0_SPI1;
    gpio_init.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOB, &gpio_init);
    gpio_init.Pin = GPIO_PIN_4;
    HAL_GPIO_Init(GPIOB, &gpio_init);
    gpio_init.Pin = GPIO_PIN_5;
    HAL_GPIO_Init(GPIOB, &gpio_init);

    /* SPI2 pinmux on PC2 (MISO2), and PB10 (SCK) */
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = GPIO_AF5_SPI2;
    gpio_init.Pin = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOB, &gpio_init);
    gpio_init.Alternate = GPIO_AF1_SPI2;
    gpio_init.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOC, &gpio_init);
    
    GpioInit::hw_init();


    // Switches over servo timer pins to timer mode.
    // PC6-7-8-9
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = GPIO_AF0_TIM3;
    gpio_init.Pin = GPIO_PIN_6;
    HAL_GPIO_Init(GPIOC, &gpio_init);
    gpio_init.Pin = GPIO_PIN_7;
    HAL_GPIO_Init(GPIOC, &gpio_init);
    gpio_init.Pin = GPIO_PIN_8;
    HAL_GPIO_Init(GPIOC, &gpio_init);
    gpio_init.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOC, &gpio_init);

    /* Initializes the blinker timer. */
    TIM_HandleTypeDef TimHandle;
    TimHandle.Instance = TIM14;
    TimHandle.Init.Period = configCPU_CLOCK_HZ / 10000 / 8;
    TimHandle.Init.Prescaler = 10000;
    TimHandle.Init.ClockDivision = 0;
    TimHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
    TimHandle.Init.RepetitionCounter = 0;
    if (HAL_TIM_Base_Init(&TimHandle) != HAL_OK)
    {
        /* Initialization Error */
        HASSERT(0);
    }
    if (HAL_TIM_Base_Start_IT(&TimHandle) != HAL_OK)
    {
        /* Starting Error */
        HASSERT(0);
    }
    NVIC_SetPriority(TIM14_IRQn, 0);
    NVIC_EnableIRQ(TIM14_IRQn);
}

}