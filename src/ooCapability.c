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
#include "ooCapability.h"
#include "oo.h"
#include "ooCalls.h"

/** Global endpoint structure */
extern ooEndPoint gH323ep;

static int giDynamicRTPPayloadType = 101;

int ooEnableDTMFRFC2833(int dynamicRTPPayloadType)
{
   gH323ep.dtmfmode |= OO_CAP_DTMF_RFC2833;
   /*Dynamic RTP payload type range is from 96 - 127 */
   if(dynamicRTPPayloadType >= 96 && dynamicRTPPayloadType <= 127)
      giDynamicRTPPayloadType = dynamicRTPPayloadType;
   OOTRACEINFO1("Enabled RFC2833 DTMF capability \n");
   return OO_OK;
}

int ooDisableDTMFRFC2833()
{
   gH323ep.dtmfmode ^= OO_CAP_DTMF_RFC2833;
   OOTRACEINFO1("Disabled RFC2833 DTMF capability\n");
   return OO_OK;
}
  

int ooAddCapability(int cap, int dir,
                    cb_StartReceiveChannel startReceiveChannel,
                    cb_StartTransmitChannel startTransmitChannel,
                    cb_StopReceiveChannel stopReceiveChannel,
                    cb_StopTransmitChannel stopTransmitChannel)
{
   int iRet=0;
   ooH323EpCapability *epCap = NULL, *cur=NULL;
     
   epCap = (ooH323EpCapability*)ASN1MALLOC(&gH323ep.ctxt,
                                            sizeof(ooH323EpCapability));
   if(!epCap)
   {
      OOTRACEERR1("Error: Failed to allocate memory for adding new end point "
                  "capability.\n");
      return OO_FAILED;
   }
   memset(epCap, 0, sizeof(ooH323EpCapability));
   epCap->dir = dir;
   epCap->startReceiveChannel = startReceiveChannel;
   epCap->startTransmitChannel = startTransmitChannel;
   epCap->stopReceiveChannel = stopReceiveChannel;
   epCap->stopTransmitChannel = stopTransmitChannel;
   epCap->next = NULL;
  
   switch(cap)
   {
   case OO_CAP_ULAW_64k_240:
   case OO_CAP_ULAW_64k_180:
   case OO_CAP_ULAW_64k_30:
      epCap->cap = cap;
      epCap->capType = T_H245AudioCapability_g711Ulaw64k;
      iRet = OO_OK;
      break;
   case OO_CAP_ULAW_56k_240:
   case OO_CAP_ULAW_56k_180:
   case OO_CAP_ULAW_56k_30:
      epCap->cap = cap;
      epCap->capType = T_H245AudioCapability_g711Ulaw56k;
      iRet = OO_OK;
      break;
   case OO_CAP_ALAW_64k_240:
      epCap->cap = cap;
      epCap->capType = T_H245AudioCapability_g711Alaw64k;
      iRet = OO_OK;
      break;
  default:
      OOTRACEERR1("Capability type not defined in stack\n");
      ASN1MEMFREEPTR(&gH323ep.ctxt, epCap);
      iRet = OO_FAILED;
   }
   if(iRet == OO_OK)
   {
      if(!gH323ep.myCaps)
         gH323ep.myCaps = epCap;
      else{
         cur = gH323ep.myCaps;
         while(cur->next) cur = cur->next;
         cur->next = epCap;
      }
      gH323ep.noOfCaps++;
   }
   return iRet;
}

  
struct H245AudioCapability* ooCreateAudioCapability(int cap, OOCTXT *pctxt)
{

   if(cap < OO_AUCAPS_MIN || cap > OO_AUCAPS_MAX)
   {
      OOTRACEERR2("Error:Audio capability %d not supported \n", cap);
      return NULL;
   }
   switch(cap)
   {
   case OO_CAP_ULAW_64k_240:
   case OO_CAP_ULAW_64k_180:
   case OO_CAP_ULAW_64k_30:
   case OO_CAP_ALAW_64k_240:
   case OO_CAP_ALAW_64k_180:
   case OO_CAP_ALAW_64k_30:
  
      return ooCreateG711Capability(cap, pctxt);
   default:
      OOTRACEERR2("ERROR: Don't know how to create audio capability %d\n",
                  cap);
   }
   return NULL;
}


void* ooCreateDTMFCapability(int cap, OOCTXT *pctxt)
{
   H245AudioTelephonyEventCapability *pATECap=NULL;
   char *events=NULL;
   switch(cap)
   {
   case OO_CAP_DTMF_RFC2833:
      pATECap = (H245AudioTelephonyEventCapability*)ASN1MALLOC(pctxt,
                                   sizeof(H245AudioTelephonyEventCapability));
      if(!pATECap)
      {
         OOTRACEERR1("Error:Failed to allocate memory to Audio Telephony Event"
                    "capability\n");
         return NULL;
      }
      memset(pATECap, 0, sizeof(H245AudioTelephonyEventCapability));
      pATECap->dynamicRTPPayloadType = giDynamicRTPPayloadType;
      events = (char*)ASN1MALLOC(pctxt, strlen("0-16")+1);
      if(!events)
      {
         OOTRACEERR1("Error: Failed to allocate memory for dtmf events\n");
         ASN1MEMFREEPTR(pctxt, pATECap);
         return NULL;
      }
      strncpy(events, "0-16", strlen("0-16"));
      pATECap->audioTelephoneEvent = events;
      return pATECap;
   default:
     OOTRACEERR1("Error:unknown dtmf capability type\n");
   }
   return NULL;
}

struct H245AudioCapability* ooCreateG711Capability(int cap, OOCTXT* pctxt)
{
   H245AudioCapability *pAudio=NULL;
   pAudio = (H245AudioCapability*)ASN1MALLOC(pctxt,
                                                sizeof(H245AudioCapability));
   if(!pAudio)
   {
      OOTRACEERR1("ERROR:Failed to allocate mem for g711 capability\n");
      return NULL;
   }
   memset(pAudio, 0, sizeof(H245AudioCapability));
  
   switch(cap)
   {
   case OO_CAP_ULAW_64k_240:
      pAudio->t = T_H245AudioCapability_g711Ulaw64k;
      pAudio->u.g711Ulaw64k = 240;  
      return pAudio;
   case OO_CAP_ULAW_64k_180:
      pAudio->t = T_H245AudioCapability_g711Ulaw64k;
      pAudio->u.g711Ulaw64k = 180;
      return pAudio;
   case OO_CAP_ULAW_64k_30:
      pAudio->t = T_H245AudioCapability_g711Ulaw64k;
      pAudio->u.g711Ulaw64k = 30;
      return pAudio;
    case OO_CAP_ALAW_64k_240:
      pAudio->t = T_H245AudioCapability_g711Alaw64k;
      pAudio->u.g711Alaw64k = 240;  
      return pAudio;
   case OO_CAP_ALAW_64k_180:
      pAudio->t = T_H245AudioCapability_g711Alaw64k;
      pAudio->u.g711Alaw64k = 180;
      return pAudio;
   case OO_CAP_ALAW_64k_30:
      pAudio->t = T_H245AudioCapability_g711Alaw64k;
      pAudio->u.g711Alaw64k = 30;
      return pAudio;
   default:
      OOTRACEERR2("ERROR: Don't know how to create audio capability %d\n",
                  cap);
   }
   return NULL;
}

ooH323EpCapability* ooIsAudioCapSupported
   (ooCallData *call, H245AudioCapability* audioCap, int dir)
{
   int i=0;
   ooH323EpCapability *epCap=NULL, *cur=NULL;
   int found = 0;

   epCap = gH323ep.myCaps;
   while(epCap)
   {
      if(epCap->dir != dir || epCap->capType != audioCap->t)
      {
         epCap = epCap->next;
         continue;
      }
      switch(audioCap->t)
      {
      case T_H245AudioCapability_g711Ulaw56k:
      case T_H245AudioCapability_g711Ulaw64k:
         found = ooCompareUlawCaps(epCap->cap, audioCap, dir);
         break;
      case T_H245AudioCapability_g711Alaw64k:
      case T_H245AudioCapability_g711Alaw56k:
         found = ooCompareAlawCaps(epCap->cap, audioCap, dir);
         break;
      case T_H245AudioCapability_nonStandard:
      case T_H245AudioCapability_g722_64k:
      case T_H245AudioCapability_g722_56k:
      case T_H245AudioCapability_g722_48k:
      case T_H245AudioCapability_g7231:
      case T_H245AudioCapability_g728:
      case T_H245AudioCapability_g729:
      case T_H245AudioCapability_g729AnnexA:
      case T_H245AudioCapability_is11172AudioCapability:
      case T_H245AudioCapability_is13818AudioCapability:
      case T_H245AudioCapability_g729wAnnexB:
      case T_H245AudioCapability_g729AnnexAwAnnexB:
      case T_H245AudioCapability_g7231AnnexCCapability:
         break;
      case T_H245AudioCapability_gsmFullRate:
      case T_H245AudioCapability_gsmHalfRate:
      case T_H245AudioCapability_gsmEnhancedFullRate:
         break;
      case T_H245AudioCapability_genericAudioCapability:
      case T_H245AudioCapability_g729Extensions:
      case T_H245AudioCapability_vbd:
      case T_H245AudioCapability_audioTelephonyEvent:
      case T_H245AudioCapability_audioTone:
      default:;
      }
      if(found) break;
      epCap = epCap->next;
   }
   return epCap;
}

int ooCompareAudioCaps(int cap, H245AudioCapability * audioCap, int dir)
{
   switch(audioCap->t)
   {
   case T_H245AudioCapability_g711Ulaw56k:
   case T_H245AudioCapability_g711Ulaw64k:
      return ooCompareUlawCaps(cap, audioCap, dir);
   case T_H245AudioCapability_g711Alaw64k:
   case T_H245AudioCapability_g711Alaw56k:
      return ooCompareAlawCaps(cap, audioCap, dir);
   default:
      return 0;
   }
}

int ooCompareUlawCaps(int cap, H245AudioCapability* audioCap, int dir)
{

   OOTRACEDBGC1("Comparing ulaw caps\n");   
   if(audioCap->t == T_H245AudioCapability_g711Ulaw64k &&
      cap > OO_CAP_ULAW_64k_MAX)
      return 0;

   if(audioCap->t == T_H245AudioCapability_g711Ulaw56k &&
      (cap < OO_CAP_ULAW_56k_MIN || cap > OO_CAP_ULAW_56k_MAX))
      return 0;
  
   switch(cap)
   {
   case OO_CAP_ULAW_64k_240:
      if(dir == T_H245Capability_receiveAudioCapability)
      {
         if(audioCap->u.g711Ulaw64k <= 240)
            return 1;
      }else{
         if(audioCap->u.g711Ulaw64k >= 240)
            return 1;
      }
      break;
   case OO_CAP_ULAW_64k_180:
      if(dir == T_H245Capability_receiveAudioCapability)
      {
         if(audioCap->u.g711Ulaw64k <= 180)
            return 1;
      }else{
         if(audioCap->u.g711Ulaw64k >= 180)
            return 1;
      }
      break;
   case OO_CAP_ULAW_64k_30:
      if(dir == T_H245Capability_receiveAudioCapability)
      {
         if(audioCap->u.g711Ulaw64k <= 30)
            return 1;
      }else{
         if(audioCap->u.g711Ulaw64k >= 30)
            return 1;
      }
      break;
   case OO_CAP_ULAW_56k_240:
      if(dir == T_H245Capability_receiveAudioCapability)
      {
         if(audioCap->u.g711Ulaw56k <= 240)
            return 1;
      }else{
         if(audioCap->u.g711Ulaw56k >= 240)
            return 1;
      }
      break;
   case OO_CAP_ULAW_56k_180:
      if(dir == T_H245Capability_receiveAudioCapability)
      {
         if(audioCap->u.g711Ulaw56k <= 180)
            return 1;
      }else{
         if(audioCap->u.g711Ulaw56k >= 180)
            return 1;
      }
      break;
   case OO_CAP_ULAW_56k_30:
      if(dir == T_H245Capability_receiveAudioCapability)
      {
         if(audioCap->u.g711Ulaw56k <= 30)
            return 1;
      }else{
         if(audioCap->u.g711Ulaw56k >= 30)
            return 1;
      }
      break;
   default:
       return 0;
   }
   return 0;
}

int ooCompareAlawCaps(int cap, H245AudioCapability* audioCap, int dir)
{
   
   if(audioCap->t == T_H245AudioCapability_g711Alaw64k &&
      cap > OO_CAP_ALAW_64k_MAX)
      return 0;

   if(audioCap->t == T_H245AudioCapability_g711Alaw56k &&
      (cap < OO_CAP_ALAW_56k_MIN || cap > OO_CAP_ALAW_56k_MAX))
      return 0;
  
   switch(cap)
   {
   case OO_CAP_ALAW_64k_240:
      if(dir == T_H245Capability_receiveAudioCapability)
      {
         if(audioCap->u.g711Alaw64k <= 240)
            return 1;
      }else{
         if(audioCap->u.g711Alaw64k >= 240)
            return 1;
      }
      break;
   case OO_CAP_ALAW_64k_180:
      if(dir == T_H245Capability_receiveAudioCapability)
      {
         if(audioCap->u.g711Alaw64k <= 180)
            return 1;
      }else{
         if(audioCap->u.g711Alaw64k >= 180)
            return 1;
      }
      break;
   case OO_CAP_ALAW_64k_30:
      if(dir == T_H245Capability_receiveAudioCapability)
      {
         if(audioCap->u.g711Alaw64k <= 30)
            return 1;
      }else{
         if(audioCap->u.g711Alaw64k >= 30)
            return 1;
      }
      break;
   case OO_CAP_ALAW_56k_240:
      if(dir == T_H245Capability_receiveAudioCapability)
      {
         if(audioCap->u.g711Alaw56k <= 240)
            return 1;
      }else{
         if(audioCap->u.g711Alaw56k >= 240)
            return 1;
      }
      break;
   case OO_CAP_ALAW_56k_180:
      if(dir == T_H245Capability_receiveAudioCapability)
      {
         if(audioCap->u.g711Alaw56k <= 180)
            return 1;
      }else{
         if(audioCap->u.g711Alaw56k >= 180)
            return 1;
      }
      break;
   case OO_CAP_ALAW_56k_30:
      if(dir == T_H245Capability_receiveAudioCapability)
      {
         if(audioCap->u.g711Alaw56k <= 30)
            return 1;
      }else{
         if(audioCap->u.g711Alaw56k >= 30)
            return 1;
      }
      break;
   default:
       return 0;
   }
   return 0;
}
          
          

          

ooH323EpCapability* ooIsDataTypeSupported(ooCallData *call, H245DataType *data, int dir)
{
   switch(data->t)
   {
   case T_H245DataType_nonStandard:
      return NULL;
   case T_H245DataType_nullData:
      return NULL;
   case T_H245DataType_videoData:
      return NULL;
   case T_H245DataType_audioData:
      return ooIsAudioCapSupported(call, data->u.audioData, dir);
   case T_H245DataType_data:
     return NULL;
   case T_H245DataType_encryptionData:
     return NULL;
   case T_H245DataType_h235Control:
     return NULL;
   case T_H245DataType_h235Media:
     return NULL;
   case T_H245DataType_multiplexedStream:
     return NULL;
   default:
      OOTRACEINFO3("Unknown data type (%s, %s)\n", call->callType, call->callToken);
   }
   return NULL;
}


int ooResetCapPref(ooCallData *call)
{
   ooCapPrefs *capPrefs=NULL;
   if(call)
      capPrefs = &call->capPrefs;
   else
      capPrefs = &gH323ep.capPrefs;
   memset(capPrefs, 0, sizeof(ooCapPrefs));
   return OO_OK;
}

int ooRemoveCapFromCapPref(ooCallData *call, int cap)
{
   int i=0, j=0;
   ooCapPrefs *capPrefs=NULL, oldPrefs;
   if(call)
      capPrefs = &call->capPrefs;
   else
      capPrefs = &gH323ep.capPrefs;

   memcpy(&oldPrefs, capPrefs, sizeof(ooCapPrefs));
   memset(capPrefs, 0, sizeof(ooCapPrefs));
   for(i=0; i<oldPrefs.index; i++)
   { 
      if(oldPrefs.order[i] != cap)
         capPrefs->order[j++] = oldPrefs.order[i];
   }
   capPrefs->index = j;
   return OO_OK;
}


int ooAppendCapToCapPref(ooCallData *call, int cap)
{
   ooCapPrefs *capPrefs=NULL;
   if(call)
      capPrefs = &call->capPrefs;
   else
      capPrefs = &gH323ep.capPrefs;

   ooRemoveCapFromCapPref(call, cap);

   capPrefs->order[capPrefs->index++] = cap;
   return OO_OK;
}

int ooPreppendCapToCapPref(ooCallData *call, int cap)
{
   int i=0, j=0;
   ooCapPrefs *capPrefs=NULL, oldPrefs;
   if(call)
      capPrefs = &call->capPrefs;
   else
      capPrefs = &gH323ep.capPrefs;

   memcpy(&oldPrefs, capPrefs, sizeof(ooCapPrefs));
   memset(capPrefs, 0, sizeof(ooCapPrefs));

   capPrefs->order[j++] = cap;

   for(i=0; i<oldPrefs.index; i++)
   { 
      if(oldPrefs.order[i] != cap)
         capPrefs->order[j++] = oldPrefs.order[i];
   }
   capPrefs->index = j;
   return OO_OK;
}

      


