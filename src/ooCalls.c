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

#include "ootrace.h"
#include "ootypes.h"
#include "ooCalls.h"
#include "ooUtils.h"
#include "ooports.h"
#include "oochannels.h"
#include "ooh245.h"
#include "ooCapability.h"
#include "ooGkClient.h"
#include "ooh323ep.h"
#include "ooCalls.h"

/** Global endpoint structure */
extern OOH323EndPoint gH323ep;

OOH323CallData* ooCreateCall(char* type, char*callToken)
{
   OOH323CallData *call=NULL;
   OOCTXT *pctxt=NULL;

   pctxt = newContext();
   if(!pctxt)
   {
      OOTRACEERR1("ERROR:Failed to create OOCTXT for new call\n");
      return NULL;
   }
   call = (OOH323CallData*)memAlloc(pctxt, sizeof(OOH323CallData));
   if(!call)
   {
      OOTRACEERR1("ERROR:Memory - ooCreateCall - call\n");
      return NULL;
   }
   /*   memset(call, 0, sizeof(OOH323CallData));*/
   call->pctxt = pctxt;
  
   sprintf(call->callToken, "%s", callToken);
   sprintf(call->callType, "%s", type);
   call->callReference = 0;
   if(gH323ep.callerid){
     strncpy(call->ourCallerId, gH323ep.callerid, sizeof(call->ourCallerId)-1);
     call->ourCallerId[sizeof(call->ourCallerId)-1] = '\0';
   }else
      call->ourCallerId[0] = '\0';
  
   memset(&call->callIdentifier, 0, sizeof(H225CallIdentifier));
   memset(&call->confIdentifier, 0, sizeof(H225ConferenceIdentifier));

   call->flags = 0;
   if (OO_TESTFLAG(gH323ep.flags, OO_M_TUNNELING))
      OO_SETFLAG (call->flags, OO_M_TUNNELING);

   if(gH323ep.gkClient)
   {
      if(OO_TESTFLAG(gH323ep.flags, OO_M_GKROUTED))
      {
         OO_SETFLAG(call->flags, OO_M_GKROUTED);
      }
   }

   if (OO_TESTFLAG(gH323ep.flags, OO_M_FASTSTART))
      OO_SETFLAG (call->flags, OO_M_FASTSTART);

  
   call->callState = OO_CALL_CREATED;
   call->callEndReason = OO_REASON_UNKNOWN;
   call->pCallFwdData = NULL;

   if(!strcmp(call->callType, "incoming"))
   {
      call->callingPartyNumber = NULL;
   }else{     
      if(ooUtilsIsStrEmpty(gH323ep.callingPartyNumber))
      {
         call->callingPartyNumber = NULL;
      }else{
         call->callingPartyNumber = (char*) memAlloc(call->pctxt,
                                         strlen(gH323ep.callingPartyNumber)+1);
         if(call->callingPartyNumber)
         {
            strcpy(call->callingPartyNumber, gH323ep.callingPartyNumber);
         }else{
            OOTRACEERR3("Error:Memory - ooCreateCall - callingPartyNumber"
                        ".(%s, %s)\n", call->callType, call->callToken);
            freeContext(pctxt);
            return NULL;
         }
      }
   }

   call->calledPartyNumber = NULL;
   call->h245SessionState = OO_H245SESSION_IDLE;
   call->dtmfmode = gH323ep.dtmfmode;
   call->mediaInfo = NULL;
   strcpy(call->localIP, gH323ep.signallingIP);
   call->pH225Channel = NULL;
   call->pH245Channel = NULL;
   call->h245listener = NULL;
   call->h245listenport = NULL;
   call->remoteIP[0] = '\0';
   call->remotePort = 0;
   call->remoteH245Port = 0;
   call->remoteDisplayName = NULL;
   call->remoteAliases = NULL;
   call->ourAliases = NULL;
   call->masterSlaveState = OO_MasterSlave_Idle;
   call->statusDeterminationNumber = 0;
   call->localTermCapState = OO_LocalTermCapExchange_Idle;
   call->remoteTermCapState = OO_RemoteTermCapExchange_Idle;
   call->ourCaps = NULL;
   call->remoteCaps = NULL;
   call->jointCaps = NULL;
   dListInit(&call->remoteFastStartOLCs);
   call->remoteTermCapSeqNo =0;
   call->localTermCapSeqNo = 0;
   memcpy(&call->capPrefs, &gH323ep.capPrefs, sizeof(ooCapPrefs));   
   call->logicalChans = NULL;
   call->noOfLogicalChannels = 0;
   call->logicalChanNoBase = 1001;
   call->logicalChanNoMax = 1100;
   call->logicalChanNoCur = 1001;
   call->nextSessionID = 4; /* 1,2,3 are reserved for audio, video and data */
   dListInit(&call->timerList);
   call->msdRetries = 0;
   call->usrData = NULL;
   OOTRACEINFO3("Created a new call (%s, %s)\n", call->callType,
                 call->callToken);
   /* Add new call to calllist */
   ooAddCallToList (call);
   if(gH323ep.h323Callbacks.onNewCallCreated)
     gH323ep.h323Callbacks.onNewCallCreated(call);
   return call;
}

int ooAddCallToList(OOH323CallData *call)
{
   if(!gH323ep.callList)
   {
      gH323ep.callList = call;
      call->next = NULL;
      call->prev = NULL;
   }
   else{
      call->next = gH323ep.callList;
      call->prev = NULL;
      gH323ep.callList->prev = call;
      gH323ep.callList = call;
   }
   return OO_OK;
}


int ooEndCall(OOH323CallData *call)
{
   OOTRACEDBGA4("In ooEndCall call state is - %s (%s, %s)\n",
                 ooGetCallStateText(call->callState), call->callType,
                 call->callToken);

   if(call->callState == OO_CALL_CLEARED)
   {
      ooCleanCall(call);
      return OO_OK;
   }

   if(call->logicalChans)
   {
      OOTRACEINFO3("Clearing all logical channels. (%s, %s)\n", call->callType,
                    call->callToken);
      ooClearAllLogicalChannels(call);
   }

   if(!OO_TESTFLAG(call->flags, OO_M_ENDSESSION_BUILT))
   {
      if(call->h245SessionState == OO_H245SESSION_ACTIVE ||
         call->h245SessionState == OO_H245SESSION_ENDRECVD)
      {
         ooSendEndSessionCommand(call);
         OO_SETFLAG(call->flags, OO_M_ENDSESSION_BUILT);
      }
   }


   if(!call->pH225Channel || call->pH225Channel->sock ==0)
   {
      call->callState = OO_CALL_CLEARED;
   }else{
      if(!OO_TESTFLAG(call->flags, OO_M_RELEASE_BUILT))  
      {
         if(call->callState == OO_CALL_CLEAR ||
            call->callState == OO_CALL_CLEAR_RELEASERECVD)
         {
            ooSendReleaseComplete(call);
            OO_SETFLAG(call->flags, OO_M_RELEASE_BUILT);
         }
      }
   }
     
   return OO_OK;
}



int ooRemoveCallFromList (OOH323CallData *call)
{
   if(!call)
      return OO_OK;

   if(call == gH323ep.callList)
   {
      if(!call->next)
         gH323ep.callList = NULL;
      else{
         call->next->prev = NULL;
         gH323ep.callList = call->next;
      }
   }
   else{
      call->prev->next = call->next;
      if(call->next)
         call->next->prev = call->prev;
   }
   return OO_OK;
}

int ooCleanCall(OOH323CallData *call)
{
   OOCTXT *pctxt;

   OOTRACEWARN4 ("Cleaning Call (%s, %s)- reason:%s\n",
                 call->callType, call->callToken,
                 ooGetReasonCodeText (call->callEndReason));

   /* First clean all the logical channels, if not already cleaned. */
   if(call->logicalChans)
      ooClearAllLogicalChannels(call);
  
   /* Close H.245 connection, if not already closed */
   if(call->h245SessionState != OO_H245SESSION_CLOSED)
      ooCloseH245Connection(call);
   else{
      if(call->pH245Channel && call->pH245Channel->outQueue.count > 0)
      {
         dListFreeAll(call->pctxt, &(call->pH245Channel->outQueue));
         memFreePtr(call->pctxt, call->pH245Channel);
      }
   }
  
   /* Close H225 connection, if not already closed. */
   if (0 != call->pH225Channel && 0 != call->pH225Channel->sock)
   {
      ooCloseH225Connection(call);
   }

   /* Clean timers */
   if(call->timerList.count > 0)
   {
      dListFreeAll(call->pctxt, &(call->timerList));
   }

   if(gH323ep.gkClient && !OO_TESTFLAG(call->flags, OO_M_DISABLEGK))
   {
      ooGkClientCleanCall(gH323ep.gkClient, call);
   }

   ooRemoveCallFromList (call);
   OOTRACEINFO3("Removed call (%s, %s) from list\n", call->callType,
                 call->callToken);

   if(call->pCallFwdData && call->pCallFwdData->fwdedByRemote)
   {

      if(gH323ep.h323Callbacks.onCallForwarded)
         gH323ep.h323Callbacks.onCallForwarded(call);

      if(ooH323HandleCallFwdRequest(call)!= OO_OK)
      {
         OOTRACEERR3("Error:Failed to forward call (%s, %s)\n", call->callType,
                     call->callToken);
      }
   }else {
      if(gH323ep.h323Callbacks.onCallCleared)
         gH323ep.h323Callbacks.onCallCleared(call);
   }

   pctxt = call->pctxt;
   freeContext(pctxt);
   ASN1CRTFREE0(pctxt);
   return OO_OK;
}


int ooCallSetCallerId(OOH323CallData* call, const char* callerid)
{
   if(!call || !callerid) return OO_FAILED;
   strncpy(call->ourCallerId, callerid, sizeof(call->ourCallerId)-1);
   call->ourCallerId[sizeof(call->ourCallerId)-1]='\0';
   return OO_OK;
}

int ooCallSetCallingPartyNumber(OOH323CallData *call, const char *number)
{
   if(call->callingPartyNumber)
      memFreePtr(call->pctxt, call->callingPartyNumber);

   call->callingPartyNumber = (char*) memAlloc(call->pctxt, strlen(number)+1);
   if(call->callingPartyNumber)
   {
     strcpy(call->callingPartyNumber, number);
   }else{
      OOTRACEERR3("Error:Memory - ooCallSetCallingPartyNumber - "
                  "callingPartyNumber.(%s, %s)\n", call->callType,
                  call->callToken);
      return OO_FAILED;
   }
   /* Set dialed digits alias */
   if(!strcmp(call->callType, "outgoing"))
   {
      ooCallAddAliasDialedDigits(call, number);
   }
   return OO_OK;
}

int ooCallGetCallingPartyNumber(OOH323CallData *call, char *buffer, int len)
{
   if(call->callingPartyNumber)
   {
      if(len>(int)strlen(call->callingPartyNumber))
      {
         strcpy(buffer, call->callingPartyNumber);
         return OO_OK;
      }
   }
  
   return OO_FAILED;
}


int ooCallSetCalledPartyNumber(OOH323CallData *call, const char *number)
{
   if(call->calledPartyNumber)
      memFreePtr(call->pctxt, call->calledPartyNumber);

   call->calledPartyNumber = (char*) memAlloc(call->pctxt, strlen(number)+1);
   if(call->calledPartyNumber)
   {
     strcpy(call->calledPartyNumber, number);
   }else{
      OOTRACEERR3("Error:Memory - ooCallSetCalledPartyNumber - "
                  "calledPartyNumber.(%s, %s)\n", call->callType,
                  call->callToken);
      return OO_FAILED;
   }
   return OO_OK;
}

int ooCallGetCalledPartyNumber(OOH323CallData *call, char *buffer, int len)
{
   if(call->calledPartyNumber)
   {
      if(len>(int)strlen(call->calledPartyNumber))
      {
         strcpy(buffer, call->calledPartyNumber);
         return OO_OK;
      }
   }
  
   return OO_FAILED;
}

int ooCallClearAliases(OOH323CallData *call)
{
   if(call->ourAliases)
      memFreePtr(call->pctxt, call->ourAliases);
   call->ourAliases = NULL;
   return OO_OK;
}

int ooCallAddAliasH323ID(OOH323CallData *call, const char* h323id)
{
   ooAliases * psNewAlias=NULL;
   psNewAlias = (ooAliases*)memAlloc(call->pctxt, sizeof(ooAliases));
   if(!psNewAlias)
   {
      OOTRACEERR3("Error:Memory - ooCallAddAliasH323ID - psNewAlias"
                  "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   psNewAlias->type = T_H225AliasAddress_h323_ID;
   psNewAlias->value = (char*) memAlloc(call->pctxt, strlen(h323id)+1);
   if(!psNewAlias->value)
   {
      OOTRACEERR3("Error:Memory - ooCallAddAliasH323ID - psNewAlias->value"
                  " (%s, %s)\n", call->callType, call->callToken);
      memFreePtr(call->pctxt, psNewAlias);
      return OO_FAILED;
   }
   strcpy(psNewAlias->value, h323id);
   psNewAlias->next = call->ourAliases;
   call->ourAliases = psNewAlias;
   OOTRACEDBGC4("Added alias H323ID %s to call. (%s, %s)\n", h323id,
                call->callType, call->callToken);
   return OO_OK;
}


int ooCallAddAliasDialedDigits(OOH323CallData *call, const char* dialedDigits)
{

   ooAliases * psNewAlias=NULL;
   psNewAlias = (ooAliases*)memAlloc(call->pctxt, sizeof(ooAliases));
   if(!psNewAlias)
   {
      OOTRACEERR3("Error:Memory - ooCallAddAliasDialedDigits - psNewAlias"
                  "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   psNewAlias->type = T_H225AliasAddress_dialedDigits;
   psNewAlias->value = (char*) memAlloc(call->pctxt, strlen(dialedDigits)+1);
   if(!psNewAlias->value)
   {
      OOTRACEERR3("Error:Memory - ooCallAddAliasDialedDigits - "
                  "psNewAlias->value. (%s, %s)\n", call->callType,
                   call->callToken);
      memFreePtr(call->pctxt, psNewAlias);
      return OO_FAILED;
   }
   strcpy(psNewAlias->value, dialedDigits);
   psNewAlias->next = call->ourAliases;
   call->ourAliases = psNewAlias;
   OOTRACEDBGC4("Added alias dialedDigits %s to call. (%s, %s)\n",dialedDigits,
                call->callType, call->callToken);

   return OO_OK;
}


int ooCallAddAliasEmailID(OOH323CallData *call, const char* email)
{

   ooAliases * psNewAlias=NULL;
   psNewAlias = (ooAliases*)memAlloc(call->pctxt, sizeof(ooAliases));
   if(!psNewAlias)
   {
      OOTRACEERR3("Error:Memory - ooCallAddAliasEmailID - psNewAlias"
                  "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   psNewAlias->type = T_H225AliasAddress_email_ID;
   psNewAlias->value = (char*) memAlloc(call->pctxt, strlen(email)+1);
   if(!psNewAlias->value)
   {
      OOTRACEERR3("Error:Memory - ooCallAddAliasEmailID - psNewAlias->value "
                  "(%s, %s)\n", call->callType, call->callToken);
      memFreePtr(call->pctxt, psNewAlias);
      return OO_FAILED;
   }
   strcpy(psNewAlias->value, email);
   psNewAlias->next = call->ourAliases;
   call->ourAliases = psNewAlias;
   OOTRACEDBGC4("Added alias Email-id %s to call. (%s, %s)\n", email,
                call->callType, call->callToken);

   return OO_OK;
}


int ooCallAddAliasURLID(OOH323CallData *call, const char* url)
{

   ooAliases * psNewAlias=NULL;
   psNewAlias = (ooAliases*)memAlloc(call->pctxt, sizeof(ooAliases));
   if(!psNewAlias)
   {
      OOTRACEERR3("Error:Memory - ooCallAddAliasURLID - psNewAlias"
                  "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   psNewAlias->type = T_H225AliasAddress_url_ID;
   psNewAlias->value = (char*) memAlloc(call->pctxt, strlen(url)+1);
   if(!psNewAlias->value)
   {
      OOTRACEERR3("Error:Memory - ooCallAddAliasURLID - psNewAlias->value"
                  "(%s, %s)\n", call->callType, call->callToken);
      memFreePtr(call->pctxt, psNewAlias);
      return OO_FAILED;
   }
   strcpy(psNewAlias->value, url);
   psNewAlias->next = call->ourAliases;
   call->ourAliases = psNewAlias;
   OOTRACEDBGC4("Added alias Url-id %s to call. (%s, %s)\n", url,
                call->callType, call->callToken);

   return OO_OK;
}


int ooCallAddRemoteAliasH323ID(OOH323CallData *call, const char* h323id)
{
   ooAliases * psNewAlias=NULL;
   psNewAlias = (ooAliases*)memAlloc(call->pctxt, sizeof(ooAliases));
   if(!psNewAlias)
   {
      OOTRACEERR3("Error:Memory - ooCallAddRemoteAliasH323ID - psNewAlias "
                  "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   psNewAlias->type = T_H225AliasAddress_h323_ID;
   psNewAlias->value = (char*) memAlloc(call->pctxt, strlen(h323id)+1);
   if(!psNewAlias->value)
   {
      OOTRACEERR3("Error:Memory - ooCallAddRemoteAliasH323ID - "
                  "psNewAlias->value. (%s, %s)\n", call->callType,
                   call->callToken);
      memFreePtr(call->pctxt, psNewAlias);
      return OO_FAILED;
   }
   strcpy(psNewAlias->value, h323id);
   psNewAlias->next = call->remoteAliases;
   call->remoteAliases = psNewAlias;
   OOTRACEDBGC4("Added remote alias H323ID %s to call. (%s, %s)\n", h323id,
                call->callType, call->callToken);
   return OO_OK;
}



/* Used to override global end point capabilities and add call specific
   capabilities */

int ooCallAddG7231Capability(OOH323CallData *call, int cap, int txframes,
                            int rxframes, OOBOOL silenceSuppression, int dir,
                            cb_StartReceiveChannel startReceiveChannel,
                            cb_StartTransmitChannel startTransmitChannel,
                            cb_StopReceiveChannel stopReceiveChannel,
                            cb_StopTransmitChannel stopTransmitChannel)
{
   return ooCapabilityAddSimpleCapability(call, cap, txframes, rxframes,
                                silenceSuppression, dir, startReceiveChannel,
                                startTransmitChannel, stopReceiveChannel,
                                stopTransmitChannel, FALSE);
}



int ooCallAddG729Capability(OOH323CallData *call, int cap, int txframes,
                            int rxframes, int dir,
                            cb_StartReceiveChannel startReceiveChannel,
                            cb_StartTransmitChannel startTransmitChannel,
                            cb_StopReceiveChannel stopReceiveChannel,
                            cb_StopTransmitChannel stopTransmitChannel)
{
   return ooCapabilityAddSimpleCapability(call, cap, txframes, rxframes, FALSE,
                          dir, startReceiveChannel, startTransmitChannel,
                          stopReceiveChannel, stopTransmitChannel, FALSE);
}

int ooCallAddG711Capability(OOH323CallData *call, int cap, int txframes,
                            int rxframes, int dir,
                            cb_StartReceiveChannel startReceiveChannel,
                            cb_StartTransmitChannel startTransmitChannel,
                            cb_StopReceiveChannel stopReceiveChannel,
                            cb_StopTransmitChannel stopTransmitChannel)
{
   return ooCapabilityAddSimpleCapability(call, cap, txframes, rxframes, FALSE,
                            dir, startReceiveChannel, startTransmitChannel,
                            stopReceiveChannel, stopTransmitChannel, FALSE);
}

int ooCallAddGSMCapability(OOH323CallData* call, int cap, ASN1USINT framesPerPkt,
                             OOBOOL comfortNoise, OOBOOL scrambled, int dir,
                             cb_StartReceiveChannel startReceiveChannel,
                             cb_StartTransmitChannel startTransmitChannel,
                             cb_StopReceiveChannel stopReceiveChannel,
                             cb_StopTransmitChannel stopTransmitChannel)
{
   return ooCapabilityAddGSMCapability(call, cap, framesPerPkt, comfortNoise,
                                     scrambled, dir, startReceiveChannel,
                                     startTransmitChannel, stopReceiveChannel,
                                     stopTransmitChannel, FALSE);
}


int ooCallEnableDTMFRFC2833(OOH323CallData *call, int dynamicRTPPayloadType)
{
   return ooCapabilityEnableDTMFRFC2833(call, dynamicRTPPayloadType);
}

int ooCallDisableDTMFRFC2833(OOH323CallData *call)
{
  return ooCapabilityDisableDTMFRFC2833(call);
}

OOH323CallData* ooFindCallByToken(char *callToken)
{
   OOH323CallData *call;
   if(!callToken)
   {
      OOTRACEERR1("ERROR:Invalid call token passed - ooFindCallByToken\n");
      return NULL;
   }
   if(!gH323ep.callList)
   {
      OOTRACEERR1("ERROR: Empty calllist - ooFindCallByToken failed\n");
      return NULL;
   }
   call = gH323ep.callList;
   while(call)
   {
      if(!strcmp(call->callToken, callToken))
         break;
      else
         call = call->next;
   }
  
   if(!call)
   {
      OOTRACEERR2("ERROR:Call with token %s not found\n", callToken);
      return NULL;
   }
   return call;
}



ooLogicalChannel* ooAddNewLogicalChannel(OOH323CallData *call, int channelNo,
                                         int sessionID, char *type, char * dir,
                                         ooH323EpCapability *epCap)
{
   ooLogicalChannel *pNewChannel=NULL, *pChannel=NULL;
   ooMediaInfo *pMediaInfo = NULL;
   OOTRACEDBGC5("Adding new media channel for cap %d dir %s (%s, %s)\n",
                epCap->cap, dir, call->callType, call->callToken);
   /* Create a new logical channel entry */
   pNewChannel = (ooLogicalChannel*)memAlloc(call->pctxt,
                                                     sizeof(ooLogicalChannel));
   if(!pNewChannel)
   {
      OOTRACEERR3("ERROR:Memory - ooAddNewLogicalChannel - pNewChannel "
                  "(%s, %s)\n", call->callType, call->callToken);
      return NULL;
   }
  
   memset(pNewChannel, 0, sizeof(ooLogicalChannel));
   pNewChannel->channelNo = channelNo;
   pNewChannel->sessionID = sessionID;
   pNewChannel->state = OO_LOGICALCHAN_IDLE;
   strcpy(pNewChannel->type, type);
   strcpy(pNewChannel->dir, dir);

   pNewChannel->chanCap = epCap;
   OOTRACEDBGC4("Adding new channel with cap %d (%s, %s)\n", epCap->cap,
                call->callType, call->callToken);
   /* As per standards, media control port should be same for all
      proposed channels with same session ID. However, most applications
      use same media port for transmit and receive of audio streams. Infact,
      testing of OpenH323 based asterisk assumed that same ports are used.
      Hence we first search for existing media ports for smae session and use
      them. This should take care of all cases.
   */
   if(call->mediaInfo)
   {
      pMediaInfo = call->mediaInfo;
      while(pMediaInfo)
      {
         if(!strcmp(pMediaInfo->dir, dir) &&
            (pMediaInfo->cap == epCap->cap))
         {
            break;
         }
         pMediaInfo = pMediaInfo->next;
      }
   }
   
   if(pMediaInfo)
   {
      OOTRACEDBGC3("Using configured media info (%s, %s)\n", call->callType,
                  call->callToken);
      pNewChannel->localRtpPort = pMediaInfo->lMediaPort;
      pNewChannel->localRtcpPort = pMediaInfo->lMediaCntrlPort;
      strcpy(pNewChannel->localIP, pMediaInfo->lMediaIP);
   }else{
      OOTRACEDBGC3("Using default media info (%s, %s)\n", call->callType,
                  call->callToken);
      pNewChannel->localRtpPort = ooGetNextPort (OORTP);

      /* Ensures that RTP port is an even one */
      if((pNewChannel->localRtpPort & 1) == 1)
        pNewChannel->localRtpPort = ooGetNextPort (OORTP);

      pNewChannel->localRtcpPort = ooGetNextPort (OORTP);
      strcpy(pNewChannel->localIP, call->localIP);
   }
  
   /* Add new channel to the list */
   pNewChannel->next = NULL;
   if(!call->logicalChans)
      call->logicalChans = pNewChannel;
   else{
      pChannel = call->logicalChans;
      while(pChannel->next)  pChannel = pChannel->next;
      pChannel->next = pNewChannel;
   }
  
   /* increment logical channels */
   call->noOfLogicalChannels++;
   OOTRACEINFO3("Created new logical channel entry (%s, %s)\n", call->callType,
                call->callToken);
   return pNewChannel;
}

ooLogicalChannel* ooFindLogicalChannelByLogicalChannelNo(OOH323CallData *call,
                                                         int ChannelNo)
{
   ooLogicalChannel *pLogicalChannel=NULL;
   if(!call->logicalChans)
   {
      OOTRACEERR3("ERROR: No Open LogicalChannels - Failed "
                  "FindLogicalChannelByChannelNo (%s, %s\n", call->callType,
                   call->callToken);
      return NULL;
   }
   pLogicalChannel = call->logicalChans;
   while(pLogicalChannel)
   {
      if(pLogicalChannel->channelNo == ChannelNo)
         break;
      else
         pLogicalChannel = pLogicalChannel->next;
   }

   return pLogicalChannel;
}

ooLogicalChannel * ooFindLogicalChannelByOLC(OOH323CallData *call,
                               H245OpenLogicalChannel *olc)
{
   H245DataType * psDataType=NULL;
   H245H2250LogicalChannelParameters * pslcp=NULL;
   OOTRACEDBGC4("ooFindLogicalChannel by olc %d (%s, %s)\n",
            olc->forwardLogicalChannelNumber, call->callType, call->callToken);
   if(olc->m.reverseLogicalChannelParametersPresent)
   {
      OOTRACEDBGC3("Finding receive channel (%s,%s)\n", call->callType,
                                                       call->callToken);
      psDataType = &olc->reverseLogicalChannelParameters.dataType;
      /* Only H2250LogicalChannelParameters are supported */
      if(olc->reverseLogicalChannelParameters.multiplexParameters.t !=
         T_H245OpenLogicalChannel_reverseLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters){
         OOTRACEERR4("Error:Invalid olc %d received (%s, %s)\n",
           olc->forwardLogicalChannelNumber, call->callType, call->callToken);
         return NULL;
      }
      pslcp = olc->reverseLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;

      return ooFindLogicalChannel(call, pslcp->sessionID, "receive", psDataType);
   }
   else{
      OOTRACEDBGC3("Finding transmit channel (%s, %s)\n", call->callType,
                                                           call->callToken);
      psDataType = &olc->forwardLogicalChannelParameters.dataType;
      /* Only H2250LogicalChannelParameters are supported */
      if(olc->forwardLogicalChannelParameters.multiplexParameters.t !=
         T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters)
      {
         OOTRACEERR4("Error:Invalid olc %d received (%s, %s)\n",
           olc->forwardLogicalChannelNumber, call->callType, call->callToken);
         return NULL;
      }
      pslcp = olc->forwardLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;
      return ooFindLogicalChannel(call, pslcp->sessionID, "transmit", psDataType);
   }
}

ooLogicalChannel * ooFindLogicalChannel(OOH323CallData *call, int sessionID,
                                        char *dir, H245DataType * dataType)
{
   ooLogicalChannel * pChannel = NULL;
   pChannel = call->logicalChans;
   while(pChannel)
   {
      if(pChannel->sessionID == sessionID)
      {
         if(!strcmp(pChannel->dir, dir))
         {
            if(dataType->t == T_H245DataType_audioData)
            {
               if(!strcmp(dir, "receive"))
               {
                 if(ooCheckCompatibility_1(call, pChannel->chanCap,
                     dataType->u.audioData, OORX))
                     return pChannel;
               }else if(!strcmp(dir, "transmit"))
               {
                 if(ooCheckCompatibility_1(call, pChannel->chanCap,
                     dataType->u.audioData, OOTX))
                     return pChannel;
               }
            }
         }
      }
      pChannel = pChannel->next;
   }
   return NULL;
}

/* This function is used to get a logical channel with a particular session ID */
ooLogicalChannel* ooGetLogicalChannel(OOH323CallData *call, int sessionID)
{
   ooLogicalChannel * pChannel = NULL;
   pChannel = call->logicalChans;
   while(pChannel)
   {
      if(pChannel->sessionID == sessionID)
         return pChannel;
      else
         pChannel = pChannel->next;
   }
   return NULL;
}

/* Checks whether session with suplied ID and direction is already active*/
ASN1BOOL ooIsSessionEstablished(OOH323CallData *call, int sessionID, char* dir)
{
   ooLogicalChannel * temp = NULL;
   temp = call->logicalChans;
   while(temp)
   {
      if(temp->sessionID == sessionID              &&
         temp->state == OO_LOGICALCHAN_ESTABLISHED &&
         !strcmp(temp->dir, dir)                     )
         return TRUE;
      temp = temp->next;
   }
   return FALSE;
}

int ooClearAllLogicalChannels(OOH323CallData *call)
{
   ooLogicalChannel * temp = NULL, *prev = NULL;

   OOTRACEINFO3("Clearing all logical channels (%s, %s)\n", call->callType,
                 call->callToken);
  
   temp = call->logicalChans;
   while(temp)
   {
      prev = temp;
      temp = temp->next;
      ooClearLogicalChannel(call, prev->channelNo);/* TODO: efficiency - This causes re-search
                                                      of of logical channel in the list. Can be
                                                      easily improved.*/
   }
   return OO_OK;
}

int ooClearLogicalChannel(OOH323CallData *call, int channelNo)
{
   int ret = OO_OK;
   ooLogicalChannel *pLogicalChannel = NULL;
   ooH323EpCapability *epCap=NULL;

   OOTRACEDBGC4("Clearing logical channel number %d. (%s, %s)\n", channelNo,
                call->callType, call->callToken);

   pLogicalChannel = ooFindLogicalChannelByLogicalChannelNo(call,channelNo);
   if(!pLogicalChannel)
   {
      OOTRACEWARN4("Logical Channel %d doesn't exist (%s, %s)\n",
                  channelNo, call->callType, call->callToken);
      return OO_OK;
   }

   epCap = (ooH323EpCapability*) pLogicalChannel->chanCap;
   if(!strcmp(pLogicalChannel->dir, "receive"))
   {
      if(epCap->stopReceiveChannel)
      {
         epCap->stopReceiveChannel(call, pLogicalChannel);
         OOTRACEINFO4("Stopped Receive channel %d (%s, %s)\n",
                                 channelNo, call->callType, call->callToken);
      }
      else{
         OOTRACEERR4("ERROR:No callback registered for stopReceiveChannel %d "
                     "(%s, %s)\n", channelNo, call->callType, call->callToken);
      }
   }
   else
   {
      if(pLogicalChannel->state == OO_LOGICALCHAN_ESTABLISHED)
      {
         if(epCap->stopTransmitChannel)
         {
            epCap->stopTransmitChannel(call, pLogicalChannel);
            OOTRACEINFO4("Stopped Transmit channel %d (%s, %s)\n",
                          channelNo, call->callType, call->callToken);
         }
         else{
            OOTRACEERR4("ERROR:No callback registered for stopTransmitChannel"
                        " %d (%s, %s)\n", channelNo, call->callType,
                        call->callToken);
         }
      }
   }
   ooRemoveLogicalChannel(call, channelNo);/* TODO: efficiency - This causes re-search of
                                                    of logical channel in the list. Can be
                                                    easily improved.*/
   return OO_OK;
}

int ooRemoveLogicalChannel(OOH323CallData *call, int ChannelNo)
{
   ooLogicalChannel * temp = NULL, *prev=NULL;
   if(!call->logicalChans)
   {
      OOTRACEERR4("ERROR:Remove Logical Channel - Channel %d not found "
                  "Empty channel List(%s, %s)\n", ChannelNo, call->callType,
                  call->callToken);
      return OO_FAILED;
   }

   temp = call->logicalChans;
   while(temp)
   {
      if(temp->channelNo == ChannelNo)
      {
         if(!prev)   call->logicalChans = temp->next;
         else   prev->next = temp->next;
         //ASN1MEMFREEPTR(call->pctxt, temp->chanCap->cap);
         memFreePtr(call->pctxt, temp->chanCap);
         memFreePtr(call->pctxt, temp);
         OOTRACEDBGC4("Removed logical channel %d (%s, %s)\n", ChannelNo,
                       call->callType, call->callToken);
         call->noOfLogicalChannels--;
         return OO_OK;
      }
      prev = temp;
      temp = temp->next;
   }
  
   OOTRACEERR4("ERROR:Remove Logical Channel - Channel %d not found "
                  "(%s, %s)\n", ChannelNo, call->callType, call->callToken);
   return OO_FAILED;
}

int ooOnLogicalChannelEstablished
   (OOH323CallData *call, ooLogicalChannel * pChannel)
{
   ooLogicalChannel * temp = NULL, *prev=NULL;
   /* Change the state of the channel as established and close all other
      channels with same session IDs. This is useful for handling fastStart,
      as the endpoint can open multiple logical channels for same sessionID.
      Once the remote endpoint confirms it's selection, all other channels for
      the same sessionID must be closed.
   */
   OOTRACEDBGC3("In ooOnLogicalChannelEstablished (%s, %s)\n",
                call->callType, call->callToken);
   pChannel->state = OO_LOGICALCHAN_ESTABLISHED;
   temp = call->logicalChans;
   while(temp)
   {
      if(temp->channelNo != pChannel->channelNo &&
         temp->sessionID == pChannel->sessionID &&
         !strcmp(temp->dir, pChannel->dir)        )
      {
         prev = temp;
         temp = temp->next;
         ooClearLogicalChannel(call, prev->channelNo);
      }
      else
         temp = temp->next;
   }
   return OO_OK;  
}



int ooAddMediaInfo(OOH323CallData *call, ooMediaInfo mediaInfo)
{
   ooMediaInfo *newMediaInfo=NULL;

   if(!call)
   {
      OOTRACEERR3("Error:Invalid 'call' param for ooAddMediaInfo.(%s, 5s)\n",
                   call->callType, call->callToken);
      return OO_FAILED;
   }
   newMediaInfo = (ooMediaInfo*) memAlloc(call->pctxt, sizeof(ooMediaInfo));
   if(!newMediaInfo)
   {
      OOTRACEERR3("Error:Memory - ooAddMediaInfo - newMediaInfo. "
                  "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   memset(newMediaInfo, 0, sizeof(ooMediaInfo));
   strcpy(newMediaInfo->dir, mediaInfo.dir);
   newMediaInfo->lMediaCntrlPort = mediaInfo.lMediaCntrlPort;
   strcpy(newMediaInfo->lMediaIP,mediaInfo.lMediaIP);
   newMediaInfo->lMediaPort = mediaInfo.lMediaPort;
   newMediaInfo->cap = mediaInfo.cap;
   newMediaInfo->next = NULL;
   OOTRACEDBGC4("Configured mediainfo for cap %s (%s, %s)\n",
                ooGetAudioCapTypeText(mediaInfo.cap),
                call->callType, call->callToken);
   if(!call->mediaInfo)
      call->mediaInfo = newMediaInfo;
   else{
      newMediaInfo->next = call->mediaInfo;
      call->mediaInfo = newMediaInfo;
   }
   return OO_OK;
}
