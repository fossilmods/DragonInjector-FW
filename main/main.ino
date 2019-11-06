  //Includes
  #include "fuseegelee.h"
  #include "dragonboot_lz4.h"
  #include <Arduino.h>
  #include "Adafruit_FreeTouch.h"
  #define DBL_TAP_MAGIC 0xf01669ef
  #define DBL_TAP_PTR ((volatile uint32_t *)(HMCRAMC0_ADDR + HMCRAMC0_SIZE - 4))

  //User variables
  uint8_t bdelay = 5;          //Reboot to bootloader delay in seconds. (1-9) (Default: 5)
  uint8_t slots = 4;           //Multi-payload slot limit. (1-9) (Default: 4)
  uint8_t mdelay = 3;          //Button hold delay for mode change (single/multi payload) in seconds. (1-9) (Default: 3)
  uint8_t cslot = 0;           //Selected payload slot. 0 = Single, 1-9 = Multi. (0-9) (Default: 0)
  uint8_t rdelay = 1;          //Increment delay for pre-RCM payload switching in seconds. (1-3) (Default: 1)
  uint8_t dmode = 0;           //Alternate dual payload mode. 0 = Off. 1 = On. (0-1) (Default: 0)

  // NOTE: Alternate dual payload mode boots dragonboot/01/ normally, and dragonboot/02/ if button held during injection.

  //Working variables
  uint8_t pagedata = 0;
  uint8_t modeswitched = 0;
  uint8_t buttonstate = 0;
  uint8_t rcmbuttonstate = 0;
  uint8_t ledtimer = 0;  
  int bootloader_mode_timer = 0;            
  int capvalue = 0;
  int buttontimer = 0;
  int rcmbuttontimer = 0;

  //Defines
  #define PAGE_00 0xFC00
  #define PAGE_01 0xFC40
  #define PAGE_02 0xFC80
  #define PAGE_03 0xFCC0
  #define PAGE_04 0xFD00
  #define PAGE_05 0xFD40
  #define PAGE_06 0xFD80
  #define PAGE_07 0xFDC0
  #define PAGE_08 0xFE00
  #define PAGE_09 0xFE40
  #define PAGE_10 0xFE80
  #define PAGE_11 0xFEC0
  #define PAGE_12 0xFF00
  #define PAGE_13 0xFF40
  #define PAGE_14 0xFF80
  #define PAGE_15 0xFFC0
  #define LAST_PAGE ((usersettings_t*)PAGE_15)
  #define PAGE_SIZE 0x40

  //Make a struct to hold our settings.
  typedef struct __attribute__((__packed__)) usersettings
  {
    uint8_t a;       //Location for pagedata
    uint8_t b;       //Location for bdelay
    uint8_t c;       //Location for slots
    uint8_t d;       //Location for mdelay
    uint8_t e;       //Location for cslot
    uint8_t f;       //Location for rdelay
    uint8_t g;       //Location for dmode
  } usersettings_t;

  //Configure capacitive button.
  Adafruit_FreeTouch qt_1 = Adafruit_FreeTouch(A3, OVERSAMPLE_4, RESISTOR_0, FREQ_MODE_NONE);

void setup()
{
  read_settings();
  volatile char DI_VERSION[] = "DI_FW_0.98"; // FIRMWARE VERSION
  pinMode(0 , OUTPUT);
  pinMode(2 , OUTPUT);
  digitalWrite(0, LOW); // RED OFF
  digitalWrite(2, LOW); // BLUE OFF  
  qt_1.begin();
  capvalue = qt_1.measure();
  safesettings();
}

void loop()
{   
  if(!dmode)
  {
    if(cslot)
    {
      while(capvalue > 350)
      {
        rcmselect();
        rcmbuttonstate = 1;
      }
      if(rcmbuttonstate) delay(750);
    }
  }
  while (!searchTegraDevice())
  {
    //Blink red LED while searching for RCM.
    if(ledtimer > 0 && ledtimer < 20)
    {
      digitalWrite(0, HIGH);
    }
    else
    {
      digitalWrite(0, LOW);
    }
    if(ledtimer > 200)
    {
      ledtimer = 0;
    }
    else
    {
      ledtimer++;
    }

    // Reset to bootloader after delay for easy firmware updates.
    if (bootloader_mode_timer > (bdelay * 1000))
    {
      resetToBootloader();
    }
    else
    {
      bootloader_mode_timer++;
    }

    //Dual payload SOP check.
    if(!dmode)
    {
      capButton();
    }
    else
    {
      capvalue = qt_1.measure();
      if(capvalue > 350)
      {
        cslot = 2;
      }
      else
      {
        cslot = 1;
      }
    }
    
    delay(1);
  }

  digitalWrite(0, LOW);
  setupTegraDevice();
  sendPayload(fuseeBin, FUSEE_BIN_SIZE, cslot);
  launchPayload();
  successConfirmation();
  sleepDeep();
}

void sleepDeep()
{
  //Put DragonInjector to sleep.
  SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk; /* Enable deepsleep */
  __DSB(); /* Ensure effect of last store takes effect */
  __WFI(); /* Enter sleep mode */
}

void resetToBootloader()
{
  //Reset into bootloader.
  *DBL_TAP_PTR = DBL_TAP_MAGIC;
  NVIC_SystemReset();
}

void successConfirmation()
{
  //Rapidly blink red LED to indicate successful payload injection.
  for (int x = 0; x < 4; x++)
  {
    digitalWrite(0, HIGH);
    delay(20);
    digitalWrite(0, LOW);
    delay(80);
  }
}

void rcmselect()
{
  capvalue = qt_1.measure();
  if(rcmbuttontimer > (rdelay * 1000))
    {
      if(cslot < slots)
      {
        cslot++;
        payloadBlink();
      }
      else
      {
        cslot = 1;
        payloadBlink();
      } 
      rcmbuttontimer = 0;
    }
    else
    {
      rcmbuttontimer++;
    } 
  delay(1);
}


void capButton()
{
  // Get button capacitance.
  capvalue = qt_1.measure();    
  
  // Detect capacitive button press.
  if(capvalue > 350 && !buttonstate)
  {        
    buttonstate = 1;
  }

  // Detect capacitive button hold.
  if(capvalue > 350)  
  {
    bootloader_mode_timer = 0;
    if(buttontimer > (mdelay * 1000))
    {
      modeSwitch();
      buttontimer = 0;
    }
    else
    {
      buttontimer++;
    }    
  }
  
  // Detect capacitive button release.
  if(capvalue < 250 && buttonstate)
  {
    if(cslot && !modeswitched)
    {
      if(cslot < slots)
      {
        cslot++;
        payloadBlink();
      }
      else
      {
        cslot = 1;
        payloadBlink();
      } 
    }  
    buttonstate = 0;
    buttontimer = 0;
    modeswitched = 0;
  }
}

void payloadBlink()
{
  //Button was pressed, reset bootloader timer
  //Indicate selected payload with long red led blinks.  
  write_settings();
  digitalWrite(0, LOW);
  delay(160);
  for (int x = 0; x < cslot; x++)
  {
    digitalWrite(0, HIGH);
    delay(160);
    digitalWrite(0, LOW);
    delay(160);
  }
}

void modeSwitch()
{
  // Switch between single and multi-payload modes.  
  modeswitched = 1;
  if(cslot == 0)
  {
    cslot = 1;   
    payloadBlink();
  }
  else
  {
    cslot = 0;   
    write_settings();
    delay(160);
    digitalWrite(0, HIGH);
    delay(160);
    digitalWrite(0, LOW);
    delay(160);
  }
}

void read_settings()
{
  //Read settings stored in flash. 16 pages used for wear-levelling, starts reading from last page.
  for (int y = PAGE_15; y >= (PAGE_00); y -= PAGE_SIZE)
  {
    usersettings_t *config = (usersettings_t *)y;
    if(config->a == 0)
    {
      //If we find a page with user settings, read them and stop looking.
      bdelay = config->b;
      slots = config->c;
      mdelay = config->d;
      cslot = config->e;
      rdelay = config->f;
      dmode = config->g;
      break;
    }
  }  
}

void write_settings()
{
   //Copy settings into an array for easier page writing.   
   usersettings_t config;
   uint32_t usersettingarray[16];
   config.a = 0;
   config.b = bdelay;
   config.c = slots;
   config.d = mdelay;
   config.e = cslot;
   config.f = rdelay;
   config.g = dmode;
   memcpy(usersettingarray, &config, sizeof(usersettings_t));

   //Check if all pages used, erase all if true.
   if(LAST_PAGE->a == 0)
   {
     flash_erase_row((uint32_t *)PAGE_00);
     flash_erase_row((uint32_t *)PAGE_04);
     flash_erase_row((uint32_t *)PAGE_08);
     flash_erase_row((uint32_t *)PAGE_12);
   }

   //Store settings in flash. 16 pages used for wear-levelling, starts reading from first page.
   for (int y = PAGE_00; y <= (PAGE_15); y += PAGE_SIZE)
   {
     usersettings_t *flash_config = (usersettings_t *)y;
     if(flash_config->a == 0xFF)
     {
       //If we find a page with no user settings, write them and stop looking.
       flash_write_words((uint32_t *)y, usersettingarray, 16);
       break;
     }
   }   
}

void flash_erase_row(uint32_t *dst)
{
    wait_ready();
    NVMCTRL->STATUS.reg = NVMCTRL_STATUS_MASK;

    // Execute "ER" Erase Row
    NVMCTRL->ADDR.reg = (uint32_t)dst / 2;
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_ER;
    wait_ready();
}

void flash_write_words(uint32_t *dst, uint32_t *src, uint32_t n_words)
{
    // Set automatic page write
    NVMCTRL->CTRLB.bit.MANW = 0;

    while (n_words > 0) {
        uint32_t len = (FLASH_PAGE_SIZE >> 2) < n_words ? (FLASH_PAGE_SIZE >> 2) : n_words;
        n_words -= len;

        // Execute "PBC" Page Buffer Clear
        NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_PBC;
        wait_ready();

        // make sure there are no other memory writes here
        // otherwise we get lock-ups

        while (len--)
            *dst++ = *src++;

        // Execute "WP" Write Page
        NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_WP;
        wait_ready();
    }
}

static inline void wait_ready(void)
{
  while (NVMCTRL->INTFLAG.bit.READY == 0);
}

void safesettings()
{
  //Use known good settings if saved settings somehow out of range.
  int f = 0;
  if(bdelay < 1 || bdelay > 9)
  {
    bdelay = 5;
    f++;
  }
  if(slots < 1 || slots > 9)
  {
    slots = 4; 
    f++;
  }
  if(mdelay < 1 || mdelay > 9)
  {
    mdelay = 3;
    f++;
  }
  if(cslot < 0 || cslot > 9)
  {
    cslot = 0;
    f++;
  }
  if(rdelay < 1 || rdelay > 3)
  {
    rdelay = 1;
    f++;
  }
  if(dmode < 0 || dmode > 1)
  {
    dmode = 0;
    f++;
  }
  if(f)
  {
    write_settings();
  }
}
