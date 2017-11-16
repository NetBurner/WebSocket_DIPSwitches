/* Revision: 2.8.5 */

/******************************************************************************
* Copyright 1998-2017 NetBurner, Inc.  ALL RIGHTS RESERVED
*
*    Permission is hereby granted to purchasers of NetBurner Hardware to use or
*    modify this computer program for any use as long as the resultant program
*    is only executed on NetBurner provided hardware.
*
*    No other rights to use this program or its derivatives in part or in
*    whole are granted.
*
*    It may be possible to license this or other NetBurner software for use on
*    non-NetBurner Hardware. Contact sales@Netburner.com for more information.
*
*    NetBurner makes no representation or warranties with respect to the
*    performance of this computer program, and specifically disclaims any
*    responsibility for any damages, special or consequential, connected with
*    the use of this program.
*
* NetBurner
* 5405 Morehouse Dr.
* San Diego, CA 92121
* www.netburner.com
******************************************************************************/

#include <basictypes.h>
#include <sim.h>


void InitSingleEndAD()
{
#if (defined MCF5441X)
   volatile WORD vw;

   //See MCF5441X RM Chapter 29
   sim2.adc.cr1 = 0;
   sim2.adc.cr2 = 0;
   sim2.adc.zccr = 0;
   sim2.adc.lst1 = 0x3210; //Ch 0....
   sim2.adc.lst2 = 0x7654; //ch 7 in result 0..7
   sim2.adc.sdis = 0; //All channels enabled
   sim2.adc.sr = 0xFFFF;
   for (int i = 0; i < 8; i++)
   {
      vw = sim2.adc.rslt[i];
      (void)vw;
      sim2.adc.ofs[i] = 0;
   }

   sim2.adc.lsr = 0xFFFF;
   sim2.adc.zcsr = 0xFFFF;

   sim2.adc.pwr = 0; //Everything is turned on
   sim2.adc.cal = 0x0000;
   sim2.adc.pwr2 = 0x0005;
   sim2.adc.div = 0x505;
   sim2.adc.asdiv = 0x13;
#endif
}

void StartAD()
{
#if (defined MCF5441X)
   sim2.adc.sr = 0xffff;
   sim2.adc.cr1 = 0x2000;
#endif
}

bool ADDone()
{
#if (defined MCF5441X)
   if (sim2.adc.sr & 0x0800)
      return true;
   else
      return false;
#endif
}

WORD GetADResult(int ch) //Get the AD Result
{
#if (defined MCF5441X)
   return sim2.adc.rslt[ch];
#endif
}

