/*
 * Copyright (C) 2004-2005 by Objective Systems, Inc.
 *
 * This software is furnished under an open source license and may be
 * used and copied only in accordance with the terms of this license.
 * The text of the license may generally be found in the root
 * directory of this installation in the COPYING file.  It
 * can also be viewed online at the following URL:
 *
 *   http://www.obj-sys.com/open/license.html
 *
 * Any redistributions of this file including modified versions must
 * maintain this copyright notice.
 *
 *****************************************************************************/

#include "ootypes.h"
#include "ootrace.h"
#include "oochannels.h"
#include "ooh323ep.h"
#include "ooCalls.h"
#include "ooCapability.h"
#include "ooStackCmds.h"
#include "oosndrtp.h"
#include "ooGkClient.h"
#ifndef _WIN32
#include <pthread.h>
#endif

int osEpStartReceiveChannel(ooCallData *call, ooLogicalChannel *pChannel);
int osEpStartTransmitChannel(ooCallData *call, ooLogicalChannel *pChannel);
int osEpStopReceiveChannel(ooCallData *call, ooLogicalChannel *pChannel);
int osEpStopTransmitChannel(ooCallData *call, ooLogicalChannel *pChannel);
int osEpOnIncomingCall(ooCallData* call );
int osEpOnOutgoingCallAdmitted(ooCallData* call );
int osEpOnCallCleared(ooCallData* call );
int osEpOnAlerting(ooCallData* call);
int osEpOpenLogicalChannels(ooCallData *call);
void * osEpHandleCommand(void*);

static int isActive;
static char callToken[20];
static char ourip[20];
static int ourport;
static char dest[256];
static OOBOOL gCmdThrd;
static char USAGE[]={
   "Listen to incoming calls\n"
   "\tsimple [options] --listen\n"
   "Make a call and then go in listen mode\n"
   "\tsimple [options] <remote>\n"
   "\t                 remote => Remote endpoint to call\n"
   "\t                           Can be ip:[port] or aliases(needs gk)\n"

   "options:\n"
   "--user <username> - Name of the user. This will be used as\n"
   "                  - display name for outbound calls\n"
   "--h323id <h323id> - H323ID to be used for this endpoint\n"
   "--e164 <number>   - E164 number used as callerid for this endpoint\n"
   "--use-ip <ip>     - Ip address for the endpoint\n"
   "                    (default - uses gethostbyname)\n"
   "--use-port <port> - Port number to use for listening to incoming\n"
   "                    calls.(default-1720)\n"
   "--no-faststart    - Disable fast start(default - enabled)\n"
   "--no-tunneling    - Disable H245 Tunneling\n" 
   "--help            -  Prints this usage message\n"
};

int main(int argc, char ** argv)
{
   int ret=0, x;
   char h323id[50];
   char e164[50];
   char user[50];
   OOBOOL bListen=FALSE, bDestFound=FALSE, bFastStart=TRUE, bTunneling=TRUE;

   OOH323CALLBACKS h323Callbacks ={
     .onNewCallCreated = NULL,
     .onAlerting = osEpOnAlerting,
     .onIncomingCall = osEpOnIncomingCall,
     .onOutgoingCall = NULL,
     .onCallAnswered = NULL,
     .onCallEstablished = NULL,
     .onOutgoingCallAdmitted = osEpOnOutgoingCallAdmitted,
     .onCallCleared = osEpOnCallCleared,
     .openLogicalChannels=NULL
  };

#ifdef _WIN32
   HANDLE threadHdl;
#else
   pthread_t threadHdl;
#endif

#ifdef _WIN32
   ooSocketsInit (); /*Initialize the windows socket api  */
#endif
    
   if(argc>14 || argc==1)
   {
      printf("USAGE:\n%s", USAGE);
      return -1;
   }
   h323id[0]='\0';
   e164[0]='\0';
   ourip[0]='\0';
   ourport=0;
   dest[0]='\0';
   user[0]='\0';
   gCmdThrd = FALSE;
   /* parse args */
   for(x=1; x<argc; x++)
   {
      if(!strcmp(argv[x], "--listen"))
      {
         if(!bDestFound)
            bListen=TRUE;
         else{
            printf("USAGE:\n%s",USAGE);
            return -1;
         }
      }else if(!strcmp(argv[x], "--no-faststart"))
      {
         bFastStart = FALSE;
      }else if(!strcmp(argv[x], "--no-tunneling"))
      {
         bTunneling = FALSE;
      }else if(!strcmp(argv[x], "--help"))
      {
         printf("USAGE:\n%s",USAGE);
         return 0;
      }else if(!strcmp(argv[x], "--user"))
      {
         x++;
         strncpy(user, argv[x], sizeof(user)-1);
      }else if(!strcmp(argv[x], "--h323id"))
      {
         x++;
         strncpy(h323id, argv[x], sizeof(h323id)-1);
      }else if(!strcmp(argv[x], "--e164"))
      {
         x++;
         strncpy(e164, argv[x], sizeof(e164)-1);
      }else if(!strcmp(argv[x], "--use-ip"))
      {
         x++;
         strncpy(ourip, argv[x], sizeof(ourip)-1);
      }else if(!strcmp(argv[x],"--use-port"))
      {
         x++;
         ourport = atoi(argv[x]);
      }else if(!bDestFound && !bListen)
      {
         strncpy(dest, argv[x], sizeof(dest)-1);
         bDestFound=TRUE;
      }else {
         printf("USAGE:\n%s",USAGE);
         return -1;
      }
   }
  
   if(!bListen && !bDestFound)
   {
      printf("USAGE:\n%s",USAGE);
      return -1;
   }
   /* Determine ourip if not specified by user */
   if(!strlen(ourip))
   {
      ooGetLocalIPAddress(ourip);
   }

   if(!strcmp(ourip, "127.0.0.1"))
   {
      printf("Failed to determine local ip, please specify as command line"
             " option\n");
      printf("USAGE:\n%s", USAGE);
      return -1;
   }
      
          
   /* Initialize the H323 endpoint - faststart and tunneling enabled*/
   ret = ooH323EpInitialize("objsyscall", OO_CALLMODE_AUDIOCALL, "simple.log");
   if(ret != OO_OK)
   {
      printf("Failed to initialize H.323 Endpoint\n");
      return -1;
   }


   if(!bFastStart)
      ooH323EpDisableFastStart();

   if(!bTunneling)
      ooH323EpDisableH245Tunneling();

   ooH323EpSetTraceLevel(OOTRCLVLDBGC);
  
   ooH323EpSetLocalAddress(ourip, ourport);
  
   if(strlen(user))
      ooH323EpSetCallerID(user);

   if(strlen(h323id))
      ooH323EpAddAliasH323ID(h323id);

   if(strlen(e164)){
      ooH323EpSetCallingPartyNumber(e164);
   }
  
  
   ooH323EpAddAliasURLID("http://www.obj-sys.com");

   /* Register callbacks */
   ooH323EpSetH323Callbacks(h323Callbacks);

   /* Add audio capability */
   ooH323EpAddG711Capability(OO_G711ULAW64K, 30, 240, OORXANDTX,
                       &osEpStartReceiveChannel, &osEpStartTransmitChannel,
                       &osEpStopReceiveChannel, &osEpStopTransmitChannel);

   /* To use a gatekeeper */

   /*ooGkClientInit(RasUseSpecificGatekeeper, "10.0.0.82", 0);*/

  
   /* Load media plug-in*/
#ifdef _WIN32
   ret = ooLoadSndRTPPlugin("oomedia.dll");
   if(ret != OO_OK)
   {
      printf("\n Failed to load the media plug-in library - oomedia.dll\n");
      exit(0);
   }
#else
   ret = ooLoadSndRTPPlugin("liboomedia.so");
   if(ret != OO_OK)
   {
      printf("\n Failed to load the media plug-in library - liboomedia.so\n");
      exit(0);
   }
#endif
   /* Create H.323 Listener */
   ret = ooCreateH323Listener();
   if(ret != OO_OK)
   {
      OOTRACEERR1("Failed to Create H.323 Listener");
      return -1;
   }
   if(bDestFound)
   {
      printf("Calling %s\n", dest);
      memset(callToken, 0, 20);
      ooMakeCall(dest, callToken, sizeof(callToken), NULL); /* Make call */
   }
#ifdef _WIN32
   threadHdl = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)osEpHandleCommand,
                            0, 0, 0);
#else
   pthread_create(&threadHdl, NULL, osEpHandleCommand, NULL);
#endif
   ooMonitorChannels();

   if(gCmdThrd)
#ifdef _WIN32
      TerminateThread(threadHdl, 0);
#else
      pthread_cancel(threadHdl);
      pthread_join(threadHdl, NULL);
#endif
   ooH323EpDestroy();
   return 0;
}

/*
int osEpOpenLogicalChannels(ooCallData *call)
{
  ooH323EpCapability *epCap=NULL;
   printf("in OpenLogicalChannels\n");
   epCap = call->jointCaps;
   while(epCap)
   {
     printf("Joint cap: %d\n", epCap->cap);
     epCap = epCap->next;
   }
   return OO_OK;

}*/

/* Callback to start receive media channel */
int osEpStartReceiveChannel(ooCallData *call, ooLogicalChannel *pChannel)
{
   printf("Starting Receive channel at %s:%d\n", call->localIP,
                                              pChannel->localRtpPort);
   isActive = 1;
   ooCreateReceiveRTPChannel(call->localIP,
                             pChannel->localRtpPort);
   ooStartReceiveAudioAndPlayback();
   return OO_OK;
}

/* Callback to start transmit media channel */
int osEpStartTransmitChannel(ooCallData *call, ooLogicalChannel *pChannel)
{
   printf("Starting transmit channel to %s:%d\n",
          call->remoteIP, pChannel->mediaPort);
   isActive = 1;
   ooCreateTransmitRTPChannel (call->remoteIP, pChannel->mediaPort);
   ooStartTransmitMic();
   return OO_OK;
}

/* Callback to stop receive media channel */
int osEpStopReceiveChannel(ooCallData *call, ooLogicalChannel *pChannel)
{
   printf("Stopping Receive Channel\n");
   ooStopReceiveAudioAndPlayback();
   return OO_OK;
}

/* Callback to stop transmit media channel */
int osEpStopTransmitChannel(ooCallData *call, ooLogicalChannel *pChannel)
{
   printf("Stopping Transmit Channel\n");
   ooStopTransmitMic();
   return OO_OK;
}

int osEpOnAlerting(ooCallData* call)
{
 
   return OO_OK;
}
/* on incoming call callback */
int osEpOnIncomingCall(ooCallData* call )
{
   ooMediaInfo mediaInfo1, mediaInfo2;
   char localip[20];
   memset(&mediaInfo1, 0, sizeof(ooMediaInfo));
   memset(&mediaInfo2, 0, sizeof(ooMediaInfo));

   /* Configure mediainfo for transmit media channel of type G711 */
   memset(localip, 0, 20);
   ooGetLocalIPAddress(localip);
   mediaInfo1.lMediaCntrlPort = 5001;
   mediaInfo1.lMediaPort = 5000;
   mediaInfo1.cap = OO_G711ULAW64K;
   strcpy(mediaInfo1.lMediaIP, localip);
   strcpy(mediaInfo1.dir, "transmit");
   ooAddMediaInfo(call, mediaInfo1);
  
   /* Configure mediainfo for receive media channel of type G711 */
   mediaInfo2.lMediaCntrlPort = 5001;
   mediaInfo2.lMediaPort = 5000;
   mediaInfo2.cap =  OO_G711ULAW64K;
   strcpy(mediaInfo2.lMediaIP, localip);
   strcpy(mediaInfo2.dir, "receive");
   ooAddMediaInfo(call, mediaInfo2);
    
   strcpy(callToken, call->callToken);
   isActive = 1;
  
   return OO_OK;
}

int osEpOnOutgoingCallAdmitted(ooCallData* call )
{
   ooMediaInfo mediaInfo1, mediaInfo2;
   char localip[20];
   memset(&mediaInfo1, 0, sizeof(ooMediaInfo));
   memset(&mediaInfo2, 0, sizeof(ooMediaInfo));

   /* Configure mediainfo for transmit media channel of type G711 */
   memset(localip, 0, 20);
   ooGetLocalIPAddress(localip);
   mediaInfo1.lMediaCntrlPort = 5001;
   mediaInfo1.lMediaPort = 5000;
   strcpy(mediaInfo1.lMediaIP, localip);
   strcpy(mediaInfo1.dir, "transmit");
   mediaInfo1.cap = OO_G711ULAW64K;
   ooAddMediaInfo(call, mediaInfo1);
  
   /* Configure mediainfo for receive media channel of type G711 */
   mediaInfo2.lMediaCntrlPort = 5001;
   mediaInfo2.lMediaPort = 5000;
   strcpy(mediaInfo2.lMediaIP, localip);
   strcpy(mediaInfo2.dir, "receive");
   mediaInfo2.cap = OO_G711ULAW64K;
   ooAddMediaInfo(call, mediaInfo2);
    
   strcpy(callToken, call->callToken);

  
   return OO_OK;
}

/* CallCleared callback */
int osEpOnCallCleared(ooCallData* call )
{
   printf("Call Ended\n");
   isActive = 0;
   return OO_OK;
}

void* osEpHandleCommand(void* dummy)
{
   int ret;
   char command[20];
   gCmdThrd = TRUE;
#ifndef _WIN32
   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
#endif
   memset(command, 0, sizeof(command));
   printf("Hit <ENTER> to Hang call\n");
   fgets(command, 20, stdin);
   if(isActive)
   {
      printf("Hanging up call\n");
      ret = ooHangCall(callToken);
   }
   else {
      printf("No active call\n");
   }
   printf("Hit <ENTER> to close stack\n");
   fgets(command, 20, stdin);
   printf("Closing down stack\n");
   ooStopMonitor();
   gCmdThrd=FALSE;
   return dummy;
}

