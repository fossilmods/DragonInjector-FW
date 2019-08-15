#include "fuseegelee.h"
#include "dragonboot.h"
#include <Arduino.h>

int bootloader_mode_timer;
int bootloader_delay = 5; //BOOTLOADER MODE DELAY IN SECONDS
uint32_t currentSlot = 0xFF;

#define DBL_TAP_MAGIC 0xf01669ef
#define DBL_TAP_PTR ((volatile uint32_t *)(HMCRAMC0_ADDR + HMCRAMC0_SIZE - 4))

void setup()
{
  volatile char DI_VERSION[] = "DI_FW_0.10"; // FIRMWARE VERSION
  pinMode(3 , OUTPUT);
  pinMode(4 , OUTPUT);
  digitalWrite(3, LOW); // BLUE OFF
  digitalWrite(4, LOW); // RED OFF
}

void loop()
{   
  while (!searchTegraDevice())
  {
    digitalWrite(4, HIGH); // FLASH BLUE LED WHILE SEARCHING FOR RCM
    delay(20);
    digitalWrite(4, LOW);
    delay(180);
    bootloader_mode_timer++;
    if (bootloader_mode_timer > (bootloader_delay * 5)) resetToBootloader(); // REBOOT TO BOOTLOADER AFTER DELAY IF NO RCM FOUND
  }

  digitalWrite(4, LOW); // BLUE OFF
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
  digitalWrite(4, HIGH);
  delay(20);
  digitalWrite(4, LOW);
  delay(80);
  digitalWrite(4, HIGH);
  delay(20);
  digitalWrite(4, LOW);
  delay(80);
  digitalWrite(4, HIGH);
  delay(20);
  digitalWrite(4, LOW);
}
