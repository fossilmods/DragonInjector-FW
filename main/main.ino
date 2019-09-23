#include "fuseegelee.h"
#include "dragonboot_lz4.h"
#include <Arduino.h>
//#include "Adafruit_FreeTouch.h"
#define DBL_TAP_MAGIC 0xf01669ef
#define DBL_TAP_PTR ((volatile uint32_t *)(HMCRAMC0_ADDR + HMCRAMC0_SIZE - 4))

//Adafruit_FreeTouch qt_1 = Adafruit_FreeTouch(A3, OVERSAMPLE_4, RESISTOR_0, FREQ_MODE_NONE);
int bootloader_mode_timer;
int bootloader_delay = 10; //BOOTLOADER MODE DELAY IN SECONDS
int32_t currentSlot = 0;  //0 = Single, 1-8 = Multi
//int capvalue;

void setup()
{
//  qt_1.begin();
  volatile char DI_VERSION[] = "DI_FW_0.20"; // FIRMWARE VERSION
  pinMode(0 , OUTPUT);
  pinMode(2 , OUTPUT);
  digitalWrite(0, LOW); // RED OFF
  digitalWrite(2, LOW); // BLUE OFF
}

void loop()
{   
  while (!searchTegraDevice())
  {
    //qt_1.measure();
    digitalWrite(0, HIGH);
    delay(20);
    digitalWrite(0, LOW);
    delay(180);
    bootloader_mode_timer++;
    if (bootloader_mode_timer > (bootloader_delay * 5)) resetToBootloader(); // REBOOT TO BOOTLOADER AFTER DELAY IF NO RCM FOUND
  }

  digitalWrite(0, LOW);
  setupTegraDevice();
  sendPayload(fuseeBin, FUSEE_BIN_SIZE, currentSlot);
  launchPayload();
  successConfirmation();
  sleepDeep();
}

void sleepDeep() {
  SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk; /* Enable deepsleep */
  __DSB(); /* Ensure effect of last store takes effect */
  __WFI(); /* Enter sleep mode */
}

void resetToBootloader() {
  *DBL_TAP_PTR = DBL_TAP_MAGIC;
  NVIC_SystemReset();
}

void successConfirmation() {
  delay(80);
  digitalWrite(0, HIGH);
  delay(20);
  digitalWrite(0, LOW);
  delay(80);
  digitalWrite(0, HIGH);
  delay(20);
  digitalWrite(0, LOW);
  delay(80);
  digitalWrite(0, HIGH);
  delay(20);
  digitalWrite(0, LOW);
}
