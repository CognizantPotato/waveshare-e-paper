/*****************************************************************************
* | File       :   DEV_Config.cpp
* | Author     :   Waveshare team (modified for ESP32-S3 Arduino)
* | Function   :   Hardware underlying interface
******************************************************************************/
#include <Arduino.h>
#include <SPI.h>

#include "DEV_Config.h"

// If Waveshare-style logic-level constants are not defined, map them to Arduino.
#ifndef GPIO_PIN_RESET
  #define GPIO_PIN_RESET LOW
#endif
#ifndef GPIO_PIN_SET
  #define GPIO_PIN_SET HIGH
#endif

static const SPISettings EPD_SPI_SETTINGS(4000000, MSBFIRST, SPI_MODE0);

void GPIO_Config(void)
{
    pinMode(EPD_BUSY_PIN, INPUT);
    pinMode(EPD_RST_PIN,  OUTPUT);
    pinMode(EPD_DC_PIN,   OUTPUT);
    // pinMode(EPD_PWR_PIN, OUTPUT);   // optional if you actually wired power control
    pinMode(EPD_CS_PIN,   OUTPUT);

    // Put pins into a safe idle state
    digitalWrite(EPD_CS_PIN,  GPIO_PIN_SET);   // deselect e-paper
    digitalWrite(EPD_DC_PIN,  GPIO_PIN_SET);   // default to data (optional)
    digitalWrite(EPD_RST_PIN, GPIO_PIN_SET);   // keep out of reset

    // If you have EPD_PWR_PIN wired, you can enable it here:
    // digitalWrite(EPD_PWR_PIN, GPIO_PIN_SET);
}

void GPIO_Mode(UWORD GPIO_Pin, UWORD Mode)
{
    if (Mode == 0) {
        pinMode(GPIO_Pin, INPUT);
    } else {
        pinMode(GPIO_Pin, OUTPUT);
    }
}

/******************************************************************************
function:   SPI init (ESP32 requires explicit pin mapping if you didn't use defaults)
******************************************************************************/
void DEV_SPI_Init(void)
{
    // Note: e-paper is write-only in this mode, so MISO is unused -> pass -1
    SPI.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN, EPD_CS_PIN);
    SPI.beginTransaction(EPD_SPI_SETTINGS);
}

/******************************************************************************
function:   Module Initialize
******************************************************************************/
UBYTE DEV_Module_Init(void)
{
    GPIO_Config();

    DEV_SPI_Init();
    return 0;
}

/******************************************************************************
function:   Switch to GPIO mode (bit-bang support)
******************************************************************************/
void DEV_GPIO_Init(void)
{
#ifdef SPI_HAS_TRANSACTION
    SPI.endTransaction();
#endif
    SPI.end();

    pinMode(EPD_SCK_PIN,  OUTPUT);
    pinMode(EPD_MOSI_PIN, OUTPUT);
    pinMode(EPD_CS_PIN,   OUTPUT);

    digitalWrite(EPD_CS_PIN, GPIO_PIN_SET);
}

/******************************************************************************
function:   SPI write (hardware)
******************************************************************************/
void DEV_SPI_WriteByte(UBYTE data)
{
    SPI.transfer(data);
}

void DEV_SPI_Write_nByte(UBYTE *pData, UDOUBLE len)
{
    for (UDOUBLE i = 0; i < len; i++) {
        SPI.transfer(pData[i]);
    }
}

/******************************************************************************
function:   Bit-banged SPI send (fallback; slower)
******************************************************************************/
void DEV_SPI_SendByte(UBYTE data)
{
    GPIO_Mode(EPD_MOSI_PIN, OUTPUT);
    pinMode(EPD_SCK_PIN, OUTPUT);

    digitalWrite(EPD_CS_PIN, GPIO_PIN_RESET);

    for (int i = 0; i < 8; i++)
    {
        digitalWrite(EPD_MOSI_PIN, (data & 0x80) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        data <<= 1;

        digitalWrite(EPD_SCK_PIN, GPIO_PIN_SET);
        digitalWrite(EPD_SCK_PIN, GPIO_PIN_RESET);
    }

    digitalWrite(EPD_CS_PIN, GPIO_PIN_SET);
}

/******************************************************************************
function:   SPI read (not supported for this panel in serial mode)
******************************************************************************/
UBYTE DEV_SPI_ReadByte()
{
    // The 7.3"(E) panel serial interface is write-only in serial mode.
    // Return a safe dummy value in case legacy code calls this.
    return 0xFF;
}

void DEV_Module_Exit(void)
{
    // If you have power control hardware, you could power down here:
    // digitalWrite(EPD_PWR_PIN, GPIO_PIN_RESET);

#ifdef SPI_HAS_TRANSACTION
    SPI.endTransaction();
#endif
    SPI.end();

    // Put CS high so the display is not selected
    digitalWrite(EPD_CS_PIN, GPIO_PIN_SET);
}
