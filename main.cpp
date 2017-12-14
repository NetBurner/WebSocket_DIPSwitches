/*******************************************************************************
* Real-Time DIP Switch States via WebSocket
*
* This program illustrates how to use the NetBurner WebSocket class to display
* the state of the DIP swithes on the MOD-DEV-70CR in real-time on a webpage.
* That means as soon as you flip the DIP switches on the development board,
* the state will change on the webpage without having to refresh the page.
* This program continously polls the DIP switches and sends the state of the DIP
* switches via a websocket to the client. It's not the most effecient implementation
* when considering CPU utilization but this app demonstrates the capabilities of
* a WebSocket to allow the server(NetBurner device) to send the state of a variable
* to the client(webpage) with low latency and minimal packet size. This app
* also illustrates how to use the NetBurner JSON library to build and send JSON
* objects from the NetBurner device to the client. In this case, JSON objects are
* used to pass the state of the DIP switches to the webpage.
******************************************************************************/

#include <predef.h>
#include <stdio.h>
#include <ctype.h>
#include <startnet.h>
#include <autoupdate.h>
#include <dhcpclient.h>
#include <smarttrap.h>
#include <constants.h>
#include <websockets.h>
#include <string.h>
#include <webclient/json_lexer.h>
#include <pins.h>
#include "SimpleAD.h"

extern "C" {
  void UserMain(void * pd);
}

const char * AppName = "Real-Time DIP Switch State via WebSocket";

#define INCOMING_BUF_SIZE   8192
#define REPORT_BUF_SIZE     512
#define NUM_LEDS            8
#define NUM_SWITCHES        8
#define STATE_BUF_SIZE      8

extern http_wshandler *TheWSHandler;
int ws_fd = -1;
OS_SEM SockReadySem;
char ReportBuffer[REPORT_BUF_SIZE];
char dipStates[NUM_SWITCHES][STATE_BUF_SIZE];
char IncomingBuffer[INCOMING_BUF_SIZE];


/*-------------------------------------------------------------------
 * On the MOD-DEV-70, the LEDs are on J2 connector pins:
 * 15, 16, 31, 23, 37, 19, 20, 24 (in that order)
 * -----------------------------------------------------------------*/
void WriteLeds( int ledNum, bool ledValue )
{
   static BOOL bLedGpioInit = FALSE;
   const BYTE PinNumber[8] = { 15, 16, 31, 23, 37, 19, 20, 24 };
   static BYTE ledMask = 0x00;       // LED mask stores the state of all 8 LEDs
   BYTE BitMask = 0x01;

   if ( ! bLedGpioInit )
   {
      for ( int i = 0; i < 8; i++ )
      {
         J2[PinNumber[i]].function( PIN_GPIO );
      }
      bLedGpioInit = TRUE;
   }

   if (ledValue)
   {
       // LED on
       ledMask |= (0x01 << (ledNum));
   }
   else
   {
       // LED off
       ledMask &= ~(0x01 << (ledNum));
   }

   for ( int i = 0; i < 8; i++ )
   {
      if ( (ledMask & BitMask) == 0 )
      {
         J2[PinNumber[i]] = 1;  // LEDs tied to 3.3V, so 1 = off
      }
      else
      {
         J2[PinNumber[i]] = 0;
      }

      BitMask <<= 1;
   }
}

static void ParseInputForLedMask( char *buf, int & ledNum, bool & ledValue )
{
    ParsedJsonDataSet JsonInObject(buf);
    const char * pJsonElementName;
    int tempLedValue = 0;

    /* Print the buffer received to serial  */
//    JsonInObject.PrintObject(true);

    /* navigate to the first element name */
    JsonInObject.GetFirst();
    JsonInObject.GetNextNameInCurrentObject();

    /* Get a pointer to the first element's name */
    pJsonElementName = JsonInObject.CurrentName();

    /* Scan the element name for the LED number. Store the number value */
    sscanf( pJsonElementName, "ledcb%d\"", &ledNum );

    /* Get the boolean value of the JSON element */
    ledValue = JsonInObject.FindFullNamePermissiveBoolean(pJsonElementName);
}

static int ConsumeSocket( char c, bool &inStr, bool &strEscape )
{
    switch (c) {
    case '\\':
        if (!inStr) {
            return 0; // no change to openCount
        }
        strEscape = !strEscape;
        break;
    case '"':
        if (!strEscape) { inStr = !inStr; }
        else            { strEscape = false; }
        break;
    case '{':
      iprintf("");
        if (!strEscape) { return 1; }
        else            { strEscape = false; }
        break;
    case '}':
        if (!strEscape) { return -1; }
        else            { strEscape = false; }
        break;
    default:
        if (strEscape)  { strEscape = false; }
        break;
    }

    return 0;
}

void InputTask(void * pd)
{
  SMPoolPtr pp;
  fd_set read_fds;
  fd_set error_fds;
  int index = 0, openCount = 0;
  bool inString = false, strEscape = false;

  FD_ZERO( &read_fds );
  FD_ZERO( &error_fds );

  while (1) {
    if (ws_fd > 0) {
      FD_SET(ws_fd, &read_fds);
      FD_SET(ws_fd, &error_fds);
      if (select(1, &read_fds, NULL, &error_fds, 0)) {
        if (FD_ISSET(ws_fd, &error_fds)) {
          iprintf("Closing WebSocket\r\n");
          close(ws_fd);
          ws_fd = -1;
          iprintf("WebSocket Closed\r\n");
        }
        if (FD_ISSET(ws_fd, &read_fds)) {
          while (dataavail(ws_fd) && (index < INCOMING_BUF_SIZE)) {
            read(ws_fd, IncomingBuffer + index, 1);
            openCount += ConsumeSocket( IncomingBuffer[index], inString, strEscape );
            index++;
            if (openCount == 0) {
              break;
            }
          }
        }
        if (openCount == 0) {
            int ledNum;
            bool ledValue;
            IncomingBuffer[index] = '\0';
            iprintf("read: %s\r\n", IncomingBuffer);
            OSTimeDly(4);
            ParseInputForLedMask(IncomingBuffer, ledNum, ledValue);
            WriteLeds(ledNum, ledValue);
            index = 0;
        }
      }
    }
    else {
      OSSemPend( &SockReadySem, 0 );
    }
  }
}


void SendConfigReport(int ws_fd)
{
  SMPoolPtr pq;
  ParsedJsonDataSet JsonOutObject;

  // Assemble JSON object
  JsonOutObject.StartBuilding();
  JsonOutObject.AddObjectStart("dipSwitches");
  JsonOutObject.Add("dip1", dipStates[0]);
  JsonOutObject.Add("dip2", dipStates[1]);
  JsonOutObject.Add("dip3", dipStates[2]);
  JsonOutObject.Add("dip4", dipStates[3]);
  JsonOutObject.Add("dip5", dipStates[4]);
  JsonOutObject.Add("dip6", dipStates[5]);
  JsonOutObject.Add("dip7", dipStates[6]);
  JsonOutObject.Add("dip8", dipStates[7]);
  JsonOutObject.EndObject();
  JsonOutObject.DoneBuilding();

  // If you would like to print the JSON object to serial to see the format, uncomment the next line
  //JsonOutObject.PrintObject(true);

  // Print JSON object to a buffer and write the buffer to the WebSocket file descriptor
  int dataLen = JsonOutObject.PrintObjectToBuffer(ReportBuffer, REPORT_BUF_SIZE);
  writeall(ws_fd, ReportBuffer, dataLen);
}

int MyDoWSUpgrade( HTTP_Request *req, int sock, PSTR url, PSTR rxb )
{
  if (httpstricmp(url, "INDEX")) {
    if (ws_fd < 0) {
      int rv = WSUpgrade( req, sock );
      if (rv >= 0) {
        ws_fd = rv;
        NB::WebSocket::ws_setoption(ws_fd, WS_SO_TEXT);
        OSSemPost( &SockReadySem );
        return 2;
      }
      else {
        return 0;
      }
    }
  }

  NotFoundResponse( sock, url );
  return 0;
}

/*-------------------------------------------------------------------
 * On the MOD-DEV-70, the switches are on J2 connector pins:
 * 8, 6, 7, 10, 9, 11, 12, 13 (in that order). These signals are
 * Analog to Digital, not GPIO, so we read the analog value and
 * determine the switch position from it. This function is exclusive to
 * the MOD5441X and NANO54415.
 * ------------------------------------------------------------------*/
BYTE ReadSwitch()
{
  static BOOL bReadSwitchInit = FALSE;
  const BYTE PinNumber[8] = { 7, 6, 5, 3, 4, 1, 0, 2 };  // map J2 conn signals pins to A/D number 0-7

  BYTE BitMask = 0;

  if ( ! bReadSwitchInit )
  {
    InitSingleEndAD();
    bReadSwitchInit = TRUE;
  }

  StartAD();
  while ( !ADDone() )  asm("nop");

  for ( int BitPos = 0x01, i = 0; BitPos < 256; BitPos <<= 1, i++ )
  {
    // if greater than half the 16-bit range, consider it logic high
    if ( GetADResult(PinNumber[i]) > ( 0x7FFF / 2) )
      BitMask |= (BYTE)(0xFF & BitPos);
  }

  return BitMask;
}

/*-------------------------------------------------------------------
 This function gets the state of the DIP Switches on the MOD-DEV-70 carrier board.
 The state of each switch is represented by a bit in a 8-bit
 register. A bit value of 0 = on, and 1 = off.
  ------------------------------------------------------------------*/
void DoSwitches()
{
  // Get the value of the switches
#if (defined MCF5441X)
  BYTE sw = ReadSwitch();
#else
  BYTE sw = getdipsw();
#endif

  memset(dipStates, 0 , NUM_SWITCHES * STATE_BUF_SIZE);

  // Write out each row of the table
  for ( int i = 0; i < NUM_SWITCHES ; i++ )
  {
    if ( sw & (0x01 <<  i) )
    {
      // Switch is off
      strncpy(dipStates[i], "Off", STATE_BUF_SIZE);
    }
    else
    {
      // Switch is on
      strncpy(dipStates[i], "On", STATE_BUF_SIZE);
    }
  }
}

void UserMain(void * pd) {
  InitializeStack();
  if (EthernetIP == 0) GetDHCPAddress();
  OSChangePrio(MAIN_PRIO);
  EnableAutoUpdate();

  OSSemInit( &SockReadySem, 0 );
  StartHTTP();
  TheWSHandler = MyDoWSUpgrade;
  OSSimpleTaskCreate(InputTask, MAIN_PRIO - 1);

#ifndef _DEBUG
  EnableSmartTraps();
#endif

  iprintf("Application started\n");

  while (1)
  {
    if (ws_fd > 0)
    {
      // Get the state of the DIP switches
      DoSwitches();

      // Send the state of the DIP switches as a JSON blob via a WebSocket
      SendConfigReport(ws_fd);
    }
    else
    {
      OSTimeDly(TICKS_PER_SECOND);
    }
  }
}
