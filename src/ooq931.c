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

#include "ooq931.h"
#include "ootrace.h"
#include "ooasn1.h"
#include "oochannels.h"
#include "printHandler.h"
#include "ooCalls.h"
#include "ooh323.h"
#include "ooh245.h"
#include "ooh323ep.h"
#include "ooCapability.h"
#include "ooGkClient.h"
#include "ooUtils.h"
#include <time.h>

/** Global endpoint structure */
extern OOH323EndPoint gH323ep;

extern OO_MUTEX gCallRefMutex;
extern OO_MUTEX gCallTokenMutex;

static ASN1OBJID gProtocolID = {
   6, { 0, 0, 8, 2250, 0, 4 }
};

EXTERN int ooQ931Decode
   (OOH323CallData *call, Q931Message* msg, int length, ASN1OCTET *data)
{
   int offset;
   int rv = ASN_OK;
   char number[128];
   OOCTXT *pctxt = &gH323ep.msgctxt;

   dListInit (&msg->ies); /* clear information elements list */

   if (length < 5)  /* Packet too short */
      return Q931_E_TOOSHORT;

   msg->protocolDiscriminator = data[0];
   OOTRACEDBGB2("   protocolDiscriminator = %d\n", msg->protocolDiscriminator);
   if (data[1] != 2) /* Call reference must be 2 bytes long */
      return Q931_E_INVCALLREF;

   msg->callReference = ((data[2] & 0x7f) << 8) | data[3];

   OOTRACEDBGB2("   callReference = %d\n", msg->callReference);

   msg->fromDestination = (data[2] & 0x80) != 0;

   msg->messageType = data[4];

   /* Have preamble, start getting the informationElements into buffers */
   offset = 5;
   while (offset < length) {
      Q931InformationElement *ie;
      int ieOff = offset;
      /* Get field discriminator */
      int discriminator = data[offset++];

      /* For discriminator with high bit set there is no data */
      if ((discriminator & 0x80) == 0) {
         int len = data[offset++], alen;

         if (discriminator == Q931UserUserIE) {
            /* Special case of User-user field, there is some confusion here as
               the Q931 documentation claims the length is a single byte,
               unfortunately all H.323 based apps have a 16 bit length here, so
               we allow for said longer length. There is presumably an addendum
               to Q931 which describes this, and provides a means to
               discriminate between the old 1 byte and the new 2 byte systems.
               However, at present we assume it is always 2 bytes until we find
               something that breaks it.
            */
            len <<= 8;
            len |= data[offset++];

            /* we also have a protocol discriminator, which we ignore */
            offset++;
            len--;
         }

         /* watch out for negative lengths! (ED, 11/5/03) */
         if (len < 0) {
            return Q931_E_INVLENGTH;
         }
         else if (offset + len > length) {
            alen = 0;
            len = -len;
            rv = Q931_E_INVLENGTH;
         }
         else alen = len;

         ie = (Q931InformationElement*)
            memAlloc (pctxt, sizeof(*ie) - sizeof(ie->data) + alen);
         if(!ie)
         {
            OOTRACEERR3("Error:Memory - ooQ931Decode - ie(%s, %s)\n",
                         call->callType, call->callToken);
            return OO_FAILED;
         }
         ie->discriminator = discriminator;
         ie->offset = ieOff;
         ie->length = len;
         if (alen != 0)
            memcpy(ie->data, data + offset, alen);
         offset += len;
      }
      else {
         ie = (Q931InformationElement*) memAlloc (pctxt,
                                        sizeof(*ie) - sizeof(ie->data));
         if(!ie)
         {
            OOTRACEERR3("Error:Memory - ooQ931Decode - ie(%s, %s)\n",
                         call->callType, call->callToken);
            return OO_FAILED;
         }
         ie->discriminator = discriminator;
         ie->offset = offset;
         ie->length = 0;
      }
     
      /* Extract calling party number TODO:Give respect to presentation and
         screening indicators ;-) */
      if(ie->discriminator == Q931CallingPartyNumberIE)
      {
         OOTRACEDBGB1("   CallingPartyNumber IE = {\n");
         if(ie->length < OO_MAX_NUMBER_LENGTH)
         {
            int numoffset=1;
            if(!(0x80 & ie->data[0])) numoffset = 2;
            memcpy(number, ie->data+numoffset,ie->length-numoffset);
            number[ie->length-numoffset]='\0';
            OOTRACEDBGB2("      %s\n", number);
            if(!call->callingPartyNumber)
               ooCallSetCallingPartyNumber(call, number);
         }else{
            OOTRACEERR3("Error:Calling party number too long. (%s, %s)\n",
                           call->callType, call->callToken);
         }
         OOTRACEDBGB1("   }\n");
      }

      /* Extract called party number */
      if(ie->discriminator == Q931CalledPartyNumberIE)
      {
         OOTRACEDBGB1("   CalledPartyNumber IE = {\n");
         if(ie->length < OO_MAX_NUMBER_LENGTH)
         {
            memcpy(number, ie->data+1,ie->length-1);
            number[ie->length-1]='\0';
            OOTRACEDBGB2("      %s\n", number);
            if(!call->calledPartyNumber)
               ooCallSetCalledPartyNumber(call, number);
         }else{
            OOTRACEERR3("Error:Calling party number too long. (%s, %s)\n",
                           call->callType, call->callToken);
         }
         OOTRACEDBGB1("   }\n");
      }

      /* Handle Cause ie */
      if(ie->discriminator == Q931CauseIE)
      {
         msg->causeIE = ie;
      }

      /* TODO: Get rid of ie list.*/
      dListAppend (pctxt, &msg->ies, ie);
      if (rv != ASN_OK)
         return rv;
   }
  
   rv = ooDecodeUUIE(msg);
   return rv;
}

EXTERN Q931InformationElement* ooQ931GetIE (const Q931Message* q931msg,
                                              int ieCode)
{
   DListNode* curNode;
   unsigned int i;

   for(i = 0, curNode = q931msg->ies.head; i < q931msg->ies.count; i++) {
      Q931InformationElement *ie = (Q931InformationElement*) curNode->data;
      if (ie->discriminator == ieCode) {
         return ie;
      }
      curNode = curNode->next;
   }
   return NULL;
}

char* ooQ931GetMessageTypeName(int messageType, char* buf) {
   switch (messageType) {
      case Q931AlertingMsg :
         strcpy(buf, "Alerting");
         break;
      case Q931CallProceedingMsg :
         strcpy(buf, "CallProceeding");
         break;
      case Q931ConnectMsg :
         strcpy(buf, "Connect");
         break;
      case Q931ConnectAckMsg :
         strcpy(buf, "ConnectAck");
         break;
      case Q931ProgressMsg :
         strcpy(buf, "Progress");
         break;
      case Q931SetupMsg :
         strcpy(buf, "Setup");
         break;
      case Q931SetupAckMsg :
         strcpy(buf, "SetupAck");
         break;
      case Q931FacilityMsg :
         strcpy(buf, "Facility");
         break;
      case Q931ReleaseCompleteMsg :
         strcpy(buf, "ReleaseComplete");
         break;
      case Q931StatusEnquiryMsg :
         strcpy(buf, "StatusEnquiry");
         break;
      case Q931StatusMsg :
         strcpy(buf, "Status");
         break;
      case Q931InformationMsg :
         strcpy(buf, "Information");
         break;
      case Q931NationalEscapeMsg :
         strcpy(buf, "Escape");
         break;
      default:
         sprintf(buf, "<%u>", messageType);
   }
   return buf;
}

char* ooQ931GetIEName(int number, char* buf) {
   switch (number) {
      case Q931BearerCapabilityIE :
         strcpy(buf, "Bearer-Capability");
         break;
      case Q931CauseIE :
         strcpy(buf, "Cause");
         break;
      case Q931FacilityIE :
         strcpy(buf, "Facility");
         break;
      case Q931ProgressIndicatorIE :
         strcpy(buf, "Progress-Indicator");
         break;
      case Q931CallStateIE :
         strcpy(buf, "Call-State");
         break;
      case Q931DisplayIE :
         strcpy(buf, "Display");
         break;
      case Q931SignalIE :
         strcpy(buf, "Signal");
         break;
      case Q931CallingPartyNumberIE :
         strcpy(buf, "Calling-Party-Number");
         break;
      case Q931CalledPartyNumberIE :
         strcpy(buf, "Called-Party-Number");
         break;
      case Q931RedirectingNumberIE :
         strcpy(buf, "Redirecting-Number");
         break;
      case Q931UserUserIE :
         strcpy(buf, "User-User");
         break;
      default:
         sprintf(buf, "0x%02x", number);
   }
   return buf;
}

EXTERN void ooQ931Print (const Q931Message* q931msg) {
   char buf[1000];
   DListNode* curNode;
   unsigned int i;

   printf("Q.931 Message:\n");
   printf("   protocolDiscriminator: %i\n", q931msg->protocolDiscriminator);
   printf("   callReference: %i\n", q931msg->callReference);
   printf("   from: %s\n", (q931msg->fromDestination ?
                                       "destination" : "originator"));
   printf("   messageType: %s (0x%X)\n\n",
              ooQ931GetMessageTypeName(q931msg->messageType, buf),
                                               q931msg->messageType);

   for(i = 0, curNode = q931msg->ies.head; i < q931msg->ies.count; i++) {
      Q931InformationElement *ie = (Q931InformationElement*) curNode->data;
      int length = (ie->length >= 0) ? ie->length : -ie->length;
      printf("   IE[%i] (offset 0x%X):\n", i, ie->offset);
      printf("      discriminator: %s (0x%X)\n",
               ooQ931GetIEName(ie->discriminator, buf), ie->discriminator);
      printf("      data length: %i\n", length);

      curNode = curNode->next;
      printf("\n");
   }
}

int ooCreateQ931Message(Q931Message **q931msg, int msgType)
{
   OOCTXT *pctxt = &gH323ep.msgctxt;
  
   *q931msg = (Q931Message*)memAlloc(pctxt, sizeof(Q931Message));
               
   if(!*q931msg)
   {
      OOTRACEERR1("Error:Memory -  ooCreateQ931Message - q931msg\n");
      return OO_FAILED;
   }
   else
   {
      (*q931msg)->protocolDiscriminator = 8;
      (*q931msg)->fromDestination = FALSE;
      (*q931msg)->messageType = msgType;
      (*q931msg)->tunneledMsgType = msgType;
      (*q931msg)->logicalChannelNo = 0;
      (*q931msg)->bearerCapabilityIE = NULL;
      (*q931msg)->callingPartyNumberIE = NULL;
      (*q931msg)->calledPartyNumberIE = NULL;
      (*q931msg)->causeIE = NULL;
      return OO_OK;
   }
}


int ooGenerateCallToken (char *callToken, size_t size)
{
   static int counter = 1;
   char aCallToken[200];
   int  ret = 0;

#ifdef _WIN32
   EnterCriticalSection (&gCallTokenMutex);
#else
   pthread_mutex_lock (&gCallTokenMutex);
#endif

   sprintf (aCallToken, "ooh323c_%d", counter++);

   if (counter > OO_MAX_CALL_TOKEN)
      counter = 1;

   if (strlen (aCallToken) < size)
      strcpy (callToken, aCallToken);
   else {
      OOTRACEERR1 ("Error: Insufficient buffer size to generate call token");
      ret = OO_FAILED;
   }
#ifdef _WIN32
   LeaveCriticalSection(&gCallTokenMutex);
#else
   pthread_mutex_unlock(&gCallTokenMutex);
#endif

   return ret;
}

/* CallReference is a two octet field, thus max value can be 0xffff
   or 65535 decimal. We restrict max value to 32760, however, this should
   not cause any problems as there won't be those many simultaneous calls
   CallRef has to be locally unique and generated by caller.
*/
ASN1USINT ooGenerateCallReference()
{
   static ASN1USINT lastCallRef=0;
   ASN1USINT newCallRef=0;
#ifdef _WIN32
   EnterCriticalSection(&gCallRefMutex);
#else
   pthread_mutex_lock(&gCallRefMutex);
#endif
  
   if(lastCallRef == 0)
   {
      /* Generate a new random callRef */
      srand((unsigned)time(0));
      lastCallRef = (ASN1USINT)(rand()%100);
   }
   else
      lastCallRef++;

   /* Note callReference can be at the most 15 bits that is from 0 to 32767.
      if we generate number bigger than that, bring it in range.
   */
   if(lastCallRef>=32766)
      lastCallRef=1;

   newCallRef = lastCallRef;
#ifdef _WIN32
   LeaveCriticalSection(&gCallRefMutex);
#else
   pthread_mutex_unlock(&gCallRefMutex);
#endif
   OOTRACEDBGC2("Generated callRef %d\n", newCallRef);
   return newCallRef;
}


int ooGenerateCallIdentifier(H225CallIdentifier *callid)
{
   ASN1INT64 timestamp;
   int i=0;
#ifdef _WIN32
  
   SYSTEMTIME systemTime;
   GetLocalTime(&systemTime);
   SystemTimeToFileTime(&systemTime, (LPFILETIME)&timestamp);
#else
   struct timeval systemTime;
   gettimeofday(&systemTime, NULL);
   timestamp = systemTime.tv_sec * 10000000 + systemTime.tv_usec*10;
#endif

   callid->guid.numocts = 16;
   callid->guid.data[0] = 'o';
   callid->guid.data[1] = 'o';
   callid->guid.data[2] = 'h';
   callid->guid.data[3] = '3';
   callid->guid.data[4] = '2';
   callid->guid.data[5] = '3';
   callid->guid.data[6] = 'c';
   callid->guid.data[7] = '-';

   for (i = 8; i < 16; i++)
       callid->guid.data[i] = (ASN1OCTET)((timestamp>>((i-8+1)*8))&0xff);

   return OO_OK;

}

int ooFreeQ931Message(Q931Message *q931Msg)
{
   if(!q931Msg)
   {
      memReset(&gH323ep.msgctxt);
   }
   return OO_OK;
}

int ooEncodeUUIE(Q931Message *q931msg)
{
   ASN1OCTET msgbuf[1024];
   ASN1OCTET * msgptr=NULL;
   int  len;
   ASN1BOOL aligned = TRUE, trace = FALSE;
   Q931InformationElement* ie=NULL;
   OOCTXT *pctxt = &gH323ep.msgctxt;
   /*   memset(msgbuf, 0, sizeof(msgbuf));*/
   if(!q931msg)
   {
      OOTRACEERR1("ERROR: Invalid Q931 message in add user-user IE\n");
      return OO_FAILED;
   }
       
   if(!q931msg->userInfo)
   {
      OOTRACEERR1("ERROR:No User-User IE to encode\n");
      return OO_FAILED;
   }

   setPERBuffer(pctxt, msgbuf, sizeof(msgbuf), aligned);
  
   if(asn1PE_H225H323_UserInformation (pctxt,
                                           q931msg->userInfo)==ASN_OK)
   {
      OOTRACEDBGC1("UserInfo encoding - successful\n");
   }
   else{
      OOTRACEERR1("Error: UserInfo encoding failed\n");
      return OO_FAILED;
   }
   msgptr = encodeGetMsgPtr(pctxt, &len);

   /* Allocate memory to hold complete UserUser Information */
   ie = (Q931InformationElement*)memAlloc (pctxt,
                                     sizeof(*ie) - sizeof(ie->data) + len);
   if(ie == NULL)
   {
      OOTRACEERR1("Error:Memory -  ooEncodeUUIE - ie\n");
      return OO_FAILED;
   }
   ie->discriminator = Q931UserUserIE;
   ie->length = len;
   memcpy(ie->data, msgptr, len);
   /* Add the user to user IE NOTE: ALL IEs SHOULD BE IN ASCENDING ORDER OF
      THEIR DISCRIMINATOR AS PER SPEC.
   */
   dListInit (&(q931msg->ies));
   if((dListAppend (pctxt,
                      &(q931msg->ies), ie)) == NULL)
   {
      OOTRACEERR1("Error: Failed to add UUIE in outgoing message\n");
      return OO_FAILED;
   }

   return OO_OK;
}

int ooDecodeUUIE(Q931Message *q931Msg)
{
   DListNode* curNode;
   unsigned int i;
   ASN1BOOL aligned=TRUE, trace=FALSE;
   int stat;
   Q931InformationElement *ie;
   OOCTXT *pctxt = &gH323ep.msgctxt;
   if(q931Msg ==NULL)
   {
      OOTRACEERR1("Error: ooDecodeUUIE failed - NULL q931 message\n");
      return OO_FAILED;
   }
       
   /* Search for UserUser IE */
   for(i = 0, curNode = q931Msg->ies.head; i < q931Msg->ies.count;
                                             i++, curNode = curNode->next)
   {
      ie = (Q931InformationElement*) curNode->data;
      if(ie->discriminator == Q931UserUserIE)
         break;
   }
   if(i == q931Msg->ies.count)
   {
      OOTRACEERR1("No UserUser IE found in ooDecodeUUIE\n");
      return OO_FAILED;
   }
       
   /* Decode user-user ie */
   q931Msg->userInfo = (H225H323_UserInformation *) memAlloc(pctxt,
                                             sizeof(H225H323_UserInformation));
   if(!q931Msg->userInfo)
   {
      OOTRACEERR1("ERROR:Memory - ooDecodeUUIE - userInfo\n");
      return OO_FAILED;
   }
   memset(q931Msg->userInfo, 0, sizeof(H225H323_UserInformation));

   setPERBuffer (pctxt, ie->data, ie->length, aligned);

   stat = asn1PD_H225H323_UserInformation (pctxt, q931Msg->userInfo);
   if(stat != ASN_OK)
   {
      OOTRACEERR1("Error: UserUser IE decode failed\n");
      return OO_FAILED;
   }
   OOTRACEDBGC1("UUIE decode successful\n");
   return OO_OK;
}

#ifndef _COMPACT
static void ooPrintQ931Message
   (OOH323CallData* call, ASN1OCTET *msgbuf, ASN1UINT msglen)
{

   OOCTXT *pctxt = &gH323ep.msgctxt;
   Q931Message q931Msg;
   int ret;

   initializePrintHandler(&printHandler, "Q931 Message");

   /* Set event handler */
   setEventHandler (pctxt, &printHandler);

   setPERBuffer (pctxt, msgbuf, msglen, TRUE);

   ret = ooQ931Decode (call, &q931Msg, msglen, msgbuf);
   if(ret != OO_OK)
   {
      OOTRACEERR3("Error:Failed decoding Q931 message. (%s, %s)\n",
                  call->callType, call->callToken);
   }
   finishPrint();
   removeEventHandler(pctxt);

}
#endif



int ooEncodeH225Message(OOH323CallData *call, Q931Message *pq931Msg,
                        char *msgbuf, int size)
{
   int len=0, encodeLen=0, i=0, j=0, ieLen=0;
   int stat=0;
   ASN1OCTET* encodeptr=NULL;
   DListNode* curNode=NULL;
   if(!msgbuf || size<200)
   {
      OOTRACEERR3("Error: Invalid message buffer/size for ooEncodeH245Message."
                  " (%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }

   if(pq931Msg->messageType == Q931SetupMsg){
      msgbuf[i++] = OOSetup;
   }
   else if(pq931Msg->messageType == Q931ConnectMsg){
      msgbuf[i++] = OOConnect;
   }
   else if(pq931Msg->messageType == Q931CallProceedingMsg){
      msgbuf[i++] = OOCallProceeding;
   }
   else if(pq931Msg->messageType == Q931AlertingMsg){
      msgbuf[i++] = OOAlert;
   }
   else if(pq931Msg->messageType == Q931ReleaseCompleteMsg){
      msgbuf[i++] = OOReleaseComplete;
   }
   else if(pq931Msg->messageType == Q931FacilityMsg){
      msgbuf[i++] = OOFacility;
      msgbuf[i++] = pq931Msg->tunneledMsgType;
      msgbuf[i++] = pq931Msg->logicalChannelNo>>8;
      msgbuf[i++] = pq931Msg->logicalChannelNo;
   }
   else{
      OOTRACEERR3("Error:Unknow Q931 message type. (%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }

   stat = ooEncodeUUIE(pq931Msg);
   if(stat != OO_OK)
   {
      OOTRACEERR3("Error:Failed to encode uuie. (%s, %s)\n", call->callType,
                   call->callToken);
      return OO_FAILED;
   }
  
   msgbuf[i++] = 3; /* TPKT version */
   msgbuf[i++] = 0; /* TPKT resevred */
   /* 1st octet of length, will be populated once len is determined */
   msgbuf[i++] = 0;
   /* 2nd octet of length, will be populated once len is determined */
   msgbuf[i++] = 0;
   /* Q931 protocol discriminator */
   msgbuf[i++] = pq931Msg->protocolDiscriminator;
   msgbuf[i++] = 2; /* length of call ref is two octets */
   msgbuf[i++] = (pq931Msg->callReference >> 8); /* populate 1st octet */
   msgbuf[i++] = pq931Msg->callReference; /* populate 2nd octet */
   msgbuf[i++] = pq931Msg->messageType; /* type of q931 message */

   /* Note: the order in which ies are added is important. It is in the
      ascending order of ie codes.
   */
   /* Add bearer IE */
   if(pq931Msg->bearerCapabilityIE)
   {  
      msgbuf[i++] = Q931BearerCapabilityIE; /* ie discriminator */
      msgbuf[i++] = pq931Msg->bearerCapabilityIE->length;
      memcpy(msgbuf+i, pq931Msg->bearerCapabilityIE->data,
                       pq931Msg->bearerCapabilityIE->length);
      i += pq931Msg->bearerCapabilityIE->length;
   }  

   /* Add cause IE */
   if(pq931Msg->causeIE)
   {
      msgbuf[i++] = Q931CauseIE;
      msgbuf[i++] = pq931Msg->causeIE->length;
      memcpy(msgbuf+i, pq931Msg->causeIE->data, pq931Msg->causeIE->length);
      i += pq931Msg->causeIE->length;
   }
     
   /*Add display ie. */
   if(strlen(call->ourCallerId)>0)
   {
      msgbuf[i++] = Q931DisplayIE;
      ieLen = strlen(call->ourCallerId)+1;
      msgbuf[i++] = ieLen;
      memcpy(msgbuf+i, call->ourCallerId, ieLen-1);
      i += ieLen-1;
      msgbuf[i++] = '\0';
   }

   /* Add calling Party ie */
   if(pq931Msg->callingPartyNumberIE)
   {
      msgbuf[i++] = Q931CallingPartyNumberIE;
      msgbuf[i++] = pq931Msg->callingPartyNumberIE->length;
      memcpy(msgbuf+i, pq931Msg->callingPartyNumberIE->data,
                       pq931Msg->callingPartyNumberIE->length);
      i += pq931Msg->callingPartyNumberIE->length;
   }

    /* Add called Party ie */
   if(pq931Msg->calledPartyNumberIE)
   {
      msgbuf[i++] = Q931CalledPartyNumberIE;
      msgbuf[i++] = pq931Msg->calledPartyNumberIE->length;
      memcpy(msgbuf+i, pq931Msg->calledPartyNumberIE->data,
                       pq931Msg->calledPartyNumberIE->length);
      i += pq931Msg->calledPartyNumberIE->length;
   }

   /* Note: Have to fix this, though it works. Need to get rid of ie list.
      Right now we only put UUIE in ie list. Can be easily removed.
   */

   for(j = 0, curNode = pq931Msg->ies.head; j < (int)pq931Msg->ies.count; j++)
   {
      Q931InformationElement *ie = (Q931InformationElement*) curNode->data;
         
      ieLen = ie->length;

      /* Add the ie discriminator in message buffer */
      msgbuf[i++] = ie->discriminator;
         
      /* For user-user IE, we have to add protocol discriminator */
      if (ie->discriminator == Q931UserUserIE)
      {
         ieLen++; /* length includes protocol discriminator octet. */
         msgbuf[i++] = (ieLen>>8); /* 1st octet for length */
         msgbuf[i++] = ieLen;      /* 2nd octet for length */
         ieLen--;
         msgbuf[i++] = 5; /* protocol discriminator */
         memcpy((msgbuf + i), ie->data, ieLen);

         i += ieLen;
        
      }else
      {
         OOTRACEWARN1("Warning: Only UUIE is supported currently\n");
         return OO_FAILED;
      }
   }
   //   len = i+1-4; /* complete message length */
  

   /* Tpkt length octets populated with total length of the message */
   if(msgbuf[0] != OOFacility)
   {
      len = i-1;
      msgbuf[3] = (len >> 8);
      msgbuf[4] = len;        /* including tpkt header */
   }else{
      len = i-4;
      msgbuf[6] = (len >> 8);
      msgbuf[7] = len;
   }
 
#ifndef _COMPACT
   if(msgbuf[0] != OOFacility)
      ooPrintQ931Message (call, msgbuf+5, len-4);
   else
      ooPrintQ931Message (call, msgbuf+8, len-4);
#endif
   return OO_OK;
}

/*

*/

int ooSendCallProceeding(OOH323CallData *call)
{
   int ret;   
   H225VendorIdentifier *vendor;
   H225CallProceeding_UUIE *callProceeding;
   Q931Message *q931msg=NULL;
   OOCTXT *pctxt = &gH323ep.msgctxt;

   OOTRACEDBGC3("Building CallProceeding (%s, %s)\n", call->callType,
                 call->callToken);
   ret = ooCreateQ931Message(&q931msg, Q931CallProceedingMsg);
   if(ret != OO_OK)
   {     
      OOTRACEERR1("Error: In allocating memory for - H225 Call "
                           "Proceeding message\n");
      return OO_FAILED;
   }
  
   q931msg->callReference = call->callReference;

   q931msg->userInfo = (H225H323_UserInformation*)memAlloc(pctxt,
                             sizeof(H225H323_UserInformation));
   if(!q931msg->userInfo)
   {
      OOTRACEERR1("ERROR:Memory - ooSendCallProceeding - userInfo\n");
      return OO_FAILED;
   }
   memset (q931msg->userInfo, 0, sizeof(H225H323_UserInformation));
   q931msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent=1;
   q931msg->userInfo->h323_uu_pdu.h245Tunneling =
                                   OO_TESTFLAG(gH323ep.flags, OO_M_TUNNELING);
   q931msg->userInfo->h323_uu_pdu.h323_message_body.t =
         T_H225H323_UU_PDU_h323_message_body_callProceeding;
  
   callProceeding = (H225CallProceeding_UUIE*)memAlloc(pctxt,
                                             sizeof(H225CallProceeding_UUIE));
   if(!callProceeding)
   {
      OOTRACEERR1("ERROR:Memory - ooSendCallProceeding - callProceeding\n");
      return OO_FAILED;
   }
   memset(callProceeding, 0, sizeof(H225CallProceeding_UUIE));
   q931msg->userInfo->h323_uu_pdu.h323_message_body.u.callProceeding = callProceeding;
   callProceeding->m.multipleCallsPresent = 1;
   callProceeding->m.maintainConnectionPresent = 1;
   callProceeding->multipleCalls = FALSE;
   callProceeding->maintainConnection = FALSE;

   callProceeding->m.callIdentifierPresent = 1;
   callProceeding->callIdentifier.guid.numocts =
                                   call->callIdentifier.guid.numocts;
   memcpy(callProceeding->callIdentifier.guid.data,
          call->callIdentifier.guid.data,
          call->callIdentifier.guid.numocts);
   callProceeding->protocolIdentifier = gProtocolID; 
   callProceeding->destinationInfo.m.vendorPresent = 1;
   vendor = &callProceeding->destinationInfo.vendor;
   if(gH323ep.productID)
   {
      vendor->m.productIdPresent = 1;
      vendor->productId.numocts = ASN1MIN(strlen(gH323ep.productID),
                                    sizeof(vendor->productId.data));
      strncpy(vendor->productId.data, gH323ep.productID,
              vendor->productId.numocts);
   }
   if(gH323ep.versionID)
   {
      vendor->m.versionIdPresent = 1;
      vendor->versionId.numocts = ASN1MIN(strlen(gH323ep.versionID),
                                     sizeof(vendor->versionId.data));
      strncpy(vendor->versionId.data, gH323ep.versionID,
              vendor->versionId.numocts);
   }

   vendor->vendor.t35CountryCode = gH323ep.t35CountryCode;
   vendor->vendor.t35Extension = gH323ep.t35Extension;
   vendor->vendor.manufacturerCode = gH323ep.manufacturerCode;
     
   OOTRACEDBGA3("Built Call Proceeding(%s, %s)\n", call->callType,
                 call->callToken);  
   ret = ooSendH225Msg(call, q931msg);
   if(ret != OO_OK)
   {
      OOTRACEERR3("Error:Failed to enqueue CallProceeding message to outbound queue.(%s, %s)\n", call->callType, call->callToken);
   }

   memReset(&gH323ep.msgctxt);

   return ret;
}

int ooSendAlerting(OOH323CallData *call)
{
   int ret;   
   H225Alerting_UUIE *alerting;
   H225VendorIdentifier *vendor;
   Q931Message *q931msg=NULL;
   OOCTXT *pctxt = &gH323ep.msgctxt;

   ret = ooCreateQ931Message(&q931msg, Q931AlertingMsg);
   if(ret != OO_OK)
   {     
      OOTRACEERR1("Error: In allocating memory for - H225 "
                  "Alerting message\n");
      return OO_FAILED;
   }

   q931msg->callReference = call->callReference;

   q931msg->userInfo = (H225H323_UserInformation*)memAlloc(pctxt,
                             sizeof(H225H323_UserInformation));
   if(!q931msg->userInfo)
   {
      OOTRACEERR1("ERROR:Memory -  ooSendAlerting - userInfo\n");
      return OO_FAILED;
   }
   memset (q931msg->userInfo, 0, sizeof(H225H323_UserInformation));
   q931msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent=1;
   q931msg->userInfo->h323_uu_pdu.h245Tunneling = OO_TESTFLAG(gH323ep.flags,
                                                              OO_M_TUNNELING);
   q931msg->userInfo->h323_uu_pdu.h323_message_body.t =
         T_H225H323_UU_PDU_h323_message_body_alerting;
  
   alerting = (H225Alerting_UUIE*)memAlloc(pctxt,
                                             sizeof(H225Alerting_UUIE));
   if(!alerting)
   {
      OOTRACEERR1("ERROR:Memory -  ooSendAlerting - alerting\n");
      return OO_FAILED;
   }
   memset(alerting, 0, sizeof(H225Alerting_UUIE));
   q931msg->userInfo->h323_uu_pdu.h323_message_body.u.alerting = alerting;
   alerting->m.multipleCallsPresent = 1;
   alerting->m.maintainConnectionPresent = 1;
   alerting->multipleCalls = FALSE;
   alerting->maintainConnection = FALSE;

   /*Populate aliases */
   alerting->m.alertingAddressPresent = TRUE;
   if(call->ourAliases)
      ret = ooPopulateAliasList(pctxt, call->ourAliases,
                                       &alerting->alertingAddress);
   else
      ret = ooPopulateAliasList(pctxt, gH323ep.aliases,
                                       &alerting->alertingAddress);
   if(OO_OK != ret)
   {
      OOTRACEERR1("Error:Failed to populate alias list in Alert message\n");
      memReset(pctxt);
      return OO_FAILED;
   }
   alerting->m.presentationIndicatorPresent = TRUE;
   alerting->presentationIndicator.t =
                             T_H225PresentationIndicator_presentationAllowed;
   alerting->m.screeningIndicatorPresent = TRUE;
   alerting->screeningIndicator = userProvidedNotScreened;



   alerting->m.callIdentifierPresent = 1;
   alerting->callIdentifier.guid.numocts =
                                   call->callIdentifier.guid.numocts;
   memcpy(alerting->callIdentifier.guid.data,
          call->callIdentifier.guid.data,
          call->callIdentifier.guid.numocts);
   alerting->protocolIdentifier = gProtocolID; 
   alerting->destinationInfo.m.vendorPresent = 1;
   vendor = &alerting->destinationInfo.vendor;
   if(gH323ep.productID)
   {
      vendor->m.productIdPresent = 1;
      vendor->productId.numocts = ASN1MIN(strlen(gH323ep.productID),
                                        sizeof(vendor->productId.data));
      strncpy(vendor->productId.data, gH323ep.productID,
                                        vendor->productId.numocts);
   }
   if(gH323ep.versionID)
   {
      vendor->m.versionIdPresent = 1;
      vendor->versionId.numocts = ASN1MIN(strlen(gH323ep.versionID),
                                        sizeof(vendor->versionId.data));
      strncpy(vendor->versionId.data, gH323ep.versionID,
              vendor->versionId.numocts);
   }
     
   vendor->vendor.t35CountryCode = gH323ep.t35CountryCode;
   vendor->vendor.t35Extension = gH323ep.t35Extension;
   vendor->vendor.manufacturerCode = gH323ep.manufacturerCode;
  

   OOTRACEDBGA3("Built Alerting (%s, %s)\n", call->callType, call->callToken);
  
   ret = ooSendH225Msg(call, q931msg);
   if(ret != OO_OK)
   {
      OOTRACEERR3("Error: Failed to enqueue Alerting message to outbound queue. (%s, %s)\n", call->callType, call->callToken);
   }

   memReset(&gH323ep.msgctxt);

   return ret;
}


int ooSendFacility(OOH323CallData *call)
{
   int ret=0;
   Q931Message *pQ931Msg = NULL;
   H225Facility_UUIE *facility=NULL;
   OOCTXT *pctxt = &gH323ep.msgctxt;

   OOTRACEDBGA3("Building Facility message (%s, %s)\n", call->callType,
                 call->callToken);
   ret = ooCreateQ931Message(&pQ931Msg, Q931FacilityMsg);
   if(ret != OO_OK)
   {
      OOTRACEERR3
         ("ERROR: In allocating memory for facility message (%s, %s)\n",
          call->callType, call->callToken);
      return OO_FAILED;
   }

   q931msg->callReference = call->callReference;

   pQ931Msg->userInfo = (H225H323_UserInformation*)memAlloc(pctxt,
                             sizeof(H225H323_UserInformation));
   if(!pQ931Msg->userInfo)
   {
      OOTRACEERR3("ERROR:Memory - ooSendFacility - userInfo(%s, %s)\n",
                   call->callType, call->callToken);
      return OO_FAILED;
   }
   memset (pQ931Msg->userInfo, 0, sizeof(H225H323_UserInformation));
   pQ931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent=1;

   pQ931Msg->userInfo->h323_uu_pdu.h245Tunneling =
      OO_TESTFLAG (call->flags, OO_M_TUNNELING);

   pQ931Msg->userInfo->h323_uu_pdu.h323_message_body.t =
      T_H225H323_UU_PDU_h323_message_body_facility;
  
   facility = (H225Facility_UUIE*)
      memAllocZ (pctxt, sizeof(H225Facility_UUIE));

   if(!facility)
   {
      OOTRACEERR3("ERROR:Memory - ooSendFacility - facility (%s, %s)"
                  "\n", call->callType, call->callToken);
      return OO_FAILED;
   }

   pQ931Msg->userInfo->h323_uu_pdu.h323_message_body.u.facility = facility;

   /* Populate Facility UUIE */
   facility->protocolIdentifier = gProtocolID; 
   facility->m.callIdentifierPresent = 1;
   facility->callIdentifier.guid.numocts =
                                   call->callIdentifier.guid.numocts;
   memcpy(facility->callIdentifier.guid.data,
          call->callIdentifier.guid.data,
          call->callIdentifier.guid.numocts);
   facility->reason.t = T_H225FacilityReason_transportedInformation;
   OOTRACEDBGA3("Built Facility message to send (%s, %s)\n", call->callType,
                 call->callToken);

   ret = ooSendH225Msg(call, pQ931Msg);
   if(ret != OO_OK)
   {
      OOTRACEERR3
         ("Error:Failed to enqueue Facility message to outbound "
          "queue.(%s, %s)\n", call->callType, call->callToken);
   }
   memReset (&gH323ep.msgctxt);
   return ret;
}

int ooSendReleaseComplete(OOH323CallData *call)
{
   int ret;  
   Q931InformationElement* ie=NULL;
   Q931Message *q931msg=NULL;
   H225ReleaseComplete_UUIE *releaseComplete;
   enum Q931CauseValues cause = Q931ErrorInCauseIE;
   unsigned h225ReasonCode = T_H225ReleaseCompleteReason_undefinedReason;

   OOCTXT *pctxt = &gH323ep.msgctxt;  
   OOTRACEDBGA3("Building Release Complete message to send(%s, %s)\n",
                call->callType, call->callToken);
   ret = ooCreateQ931Message(&q931msg, Q931ReleaseCompleteMsg);
   if(ret != OO_OK)
   {     
      OOTRACEERR3("Error: In ooCreateQ931Message - H225 Release Complete "
                  "message(%s, %s)\n", call->callType, call->callToken);
      if(call->callState < OO_CALL_CLEAR)
      {
         call->callEndReason = OO_REASON_LOCAL_CLEARED;
         call->callState = OO_CALL_CLEAR;
      }
      return OO_FAILED;
   }

   q931msg->callReference = call->callReference;

   q931msg->userInfo = (H225H323_UserInformation*)memAlloc(pctxt,
                             sizeof(H225H323_UserInformation));
   if(!q931msg->userInfo)
   {
      OOTRACEERR1("ERROR:Memory - ooSendReleaseComplete - userInfo\n");
      return OO_FAILED;
   }
   memset (q931msg->userInfo, 0, sizeof(H225H323_UserInformation));

   releaseComplete = (H225ReleaseComplete_UUIE*)memAlloc(pctxt,
                                             sizeof(H225ReleaseComplete_UUIE));
   if(!releaseComplete)
   {
      OOTRACEERR3("Error:Memory - ooSendReleaseComplete - releaseComplete"
                  "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   memset(releaseComplete, 0, sizeof(H225ReleaseComplete_UUIE));
   q931msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent=1;
   q931msg->userInfo->h323_uu_pdu.h245Tunneling = OO_TESTFLAG(gH323ep.flags,
                                                              OO_M_TUNNELING);
   q931msg->userInfo->h323_uu_pdu.h323_message_body.t =
         T_H225H323_UU_PDU_h323_message_body_releaseComplete;
  
   /* Get cause value and h225 reason code corresponding to OOCallClearReason*/
   ooQ931GetCauseAndReasonCodeFromCallClearReason(call->callEndReason,
                                                     &cause, &h225ReasonCode);
   /* Set Cause IE */
   ooQ931SetCauseIE(q931msg, cause, 0, 0);
  
   /* Set H225 releaseComplete reasonCode */
   releaseComplete->m.reasonPresent = TRUE;
   releaseComplete->reason.t = h225ReasonCode;

   /* Add user-user ie */
   q931msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent=TRUE;
   q931msg->userInfo->h323_uu_pdu.h245Tunneling = OO_TESTFLAG (call->flags, OO_M_TUNNELING);
   q931msg->userInfo->h323_uu_pdu.h323_message_body.t =
           T_H225H323_UU_PDU_h323_message_body_releaseComplete;
  
   q931msg->userInfo->h323_uu_pdu.h323_message_body.u.releaseComplete =
                                                             releaseComplete;
   releaseComplete->m.callIdentifierPresent = 1;
   releaseComplete->protocolIdentifier = gProtocolID;
   releaseComplete->callIdentifier.guid.numocts =
           call->callIdentifier.guid.numocts;
   memcpy(releaseComplete->callIdentifier.guid.data,
                                  call->callIdentifier.guid.data,
                                  call->callIdentifier.guid.numocts);

   OOTRACEDBGA3("Built Release Complete message (%s, %s)\n",
                call->callType, call->callToken);
   /* Send H225 message */  
   ret = ooSendH225Msg(call, q931msg);
   if(ret != OO_OK)
   {
      OOTRACEERR3("Error:Failed to enqueue ReleaseComplete message to outbound"
                  " queue.(%s, %s)\n", call->callType, call->callToken);
   }
   memReset(&gH323ep.msgctxt);

   return ret;
}


int ooSendConnect(OOH323CallData *call)
{
   ooAcceptCall(call);
   return OO_OK;
}

/*TODO: Need to clean logical channel in case of failure after creating one */
int ooAcceptCall(OOH323CallData *call)
{
   int ret = 0, i=0, j=0, remoteMediaPort=0, remoteMediaControlPort = 0, dir=0;
   char remoteMediaIP[20], remoteMediaControlIP[20];
   H225Connect_UUIE *connect;
   H225TransportAddress_ipAddress *h245IpAddr;
   H225VendorIdentifier *vendor;
   Q931Message *q931msg=NULL;
   DListNode *pNode = NULL;
   H245OpenLogicalChannel *olc = NULL;
   ooH323EpCapability *epCap = NULL;
   ASN1DynOctStr *pFS=NULL;
   OOCTXT *pctxt = &gH323ep.msgctxt;  
   H245H2250LogicalChannelParameters *h2250lcp = NULL; 
   ooLogicalChannel* pChannel;

   ret = ooCreateQ931Message(&q931msg, Q931ConnectMsg);
   if(ret != OO_OK)
   {     
      OOTRACEERR1("Error: In allocating memory for - H225 "
                  "Connect message\n");
      return OO_FAILED;
   }
   q931msg->callReference = call->callReference;

   /* Set bearer capability */
   if(OO_OK != ooSetBearerCapabilityIE(q931msg, Q931CCITTStd,
                          Q931TransferSpeech, Q931TransferCircuitMode,
                          Q931TransferRate64Kbps, Q931UserInfoLayer1G711ULaw))
   {
      OOTRACEERR3("Error: Failed to set bearer capability ie. (%s, %s)\n",
                   call->callType, call->callToken);
      return OO_FAILED;
   }

   q931msg->userInfo = (H225H323_UserInformation*)
      memAllocZ (pctxt,sizeof(H225H323_UserInformation));

   if(!q931msg->userInfo)
   {
      OOTRACEERR1("ERROR:Memory - ooAcceptCall - userInfo\n");
      return OO_FAILED;
   }  

   q931msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent=TRUE;

   q931msg->userInfo->h323_uu_pdu.h245Tunneling =
      OO_TESTFLAG (call->flags, OO_M_TUNNELING);

   q931msg->userInfo->h323_uu_pdu.h323_message_body.t =
      T_H225H323_UU_PDU_h323_message_body_connect;
  
   connect = (H225Connect_UUIE*)
      memAllocZ (pctxt, sizeof(H225Connect_UUIE));

   if(!connect)
   {
      OOTRACEERR1("ERROR:Memory - ooAcceptCall - connect\n");
      return OO_FAILED;
   }

   q931msg->userInfo->h323_uu_pdu.h323_message_body.u.connect = connect;
   connect->m.fastStartPresent = 0;
   connect->m.multipleCallsPresent = 1;
   connect->m.maintainConnectionPresent = 1;
   connect->multipleCalls = FALSE;
   connect->maintainConnection = FALSE;
  
  
   connect->conferenceID.numocts = 16;
   for (i = 0; i < 16; i++)
      connect->conferenceID.data[i] = i + 1;

   connect->m.callIdentifierPresent = 1;
   connect->callIdentifier.guid.numocts =
                                 call->callIdentifier.guid.numocts;
   memcpy(connect->callIdentifier.guid.data, call->callIdentifier.guid.data,
                                         call->callIdentifier.guid.numocts);
  
   connect->conferenceID.numocts = call->confIdentifier.numocts;
   memcpy(connect->conferenceID.data, call->confIdentifier.data,
          call->confIdentifier.numocts);
   /* Populate alias addresses */
   connect->m.connectedAddressPresent = TRUE;
   if(call->ourAliases)
      ret = ooPopulateAliasList(pctxt, call->ourAliases,
                                      &connect->connectedAddress);
   else
      ret =  ooPopulateAliasList(pctxt, gH323ep.aliases,
                                        &connect->connectedAddress);
   if(OO_OK != ret)
   {
      OOTRACEERR1("Error:Failed to populate alias list in Connect message\n");
      memReset(pctxt);
      return OO_FAILED;
   }
   connect->m.presentationIndicatorPresent = TRUE;
   connect->presentationIndicator.t =
                             T_H225PresentationIndicator_presentationAllowed;
   connect->m.screeningIndicatorPresent = TRUE;
   connect->screeningIndicator = userProvidedNotScreened;

   connect->protocolIdentifier = gProtocolID; 

   connect->destinationInfo.m.vendorPresent = 1;
   vendor = &connect->destinationInfo.vendor;
     
   vendor->vendor.t35CountryCode = gH323ep.t35CountryCode;
   vendor->vendor.t35Extension = gH323ep.t35Extension;
   vendor->vendor.manufacturerCode = gH323ep.manufacturerCode;
   if(gH323ep.productID)
   {
      vendor->m.productIdPresent = 1;
      vendor->productId.numocts = ASN1MIN(strlen(gH323ep.productID),
                                            sizeof(vendor->productId.data));
      strncpy(vendor->productId.data, gH323ep.productID,
                                                   vendor->productId.numocts);
   }
   if(gH323ep.versionID)
   {
      vendor->m.versionIdPresent = 1;
      vendor->versionId.numocts = ASN1MIN(strlen(gH323ep.versionID),
                                           sizeof(vendor->versionId.data));
      strncpy(vendor->versionId.data, gH323ep.versionID,
                                                   vendor->versionId.numocts);
   }
   /* If fast start supported and remote endpoint has sent faststart element */
   if(OO_TESTFLAG(gH323ep.flags, OO_M_FASTSTART) &&
      call->remoteFastStartOLCs.count>0)
   {
      pFS = (ASN1DynOctStr*)memAlloc(pctxt,
                        call->remoteFastStartOLCs.count*sizeof(ASN1DynOctStr));
      if(!pFS)
      {
         OOTRACEERR3("Error:Memory - ooAcceptCall - pFS (%s, %s)\n",
                      call->callType, call->callToken);   
         return OO_FAILED;
      }
      memset(pFS, 0, call->remoteFastStartOLCs.count*sizeof(ASN1DynOctStr));


      for(i=0, j=0; i<(int)call->remoteFastStartOLCs.count; i++)
      {

         pNode = dListFindByIndex(&call->remoteFastStartOLCs, i);
         olc = (H245OpenLogicalChannel*)pNode->data;

         if(olc->forwardLogicalChannelParameters.dataType.t !=
                                                   T_H245DataType_nullData &&
            olc->m.reverseLogicalChannelParametersPresent)
         {
            OOTRACEINFO3("Ignoring bidirectional OLC as it is not supported."
                         "(%s, %s)\n", call->callType, call->callToken);
            continue;
         }

         /* Forward Channel - remote transmits - local receives */
         if(olc->forwardLogicalChannelParameters.dataType.t !=
                                                   T_H245DataType_nullData)
         {
            OOTRACEDBGC4("Processing received forward olc %d (%s, %s)\n",
                          olc->forwardLogicalChannelNumber, call->callType,
                          call->callToken);
            dir = OORX;
            epCap = ooIsDataTypeSupported(call,
                                &olc->forwardLogicalChannelParameters.dataType,
                                OORX);
            if(!epCap)
               continue;
            OOTRACEINFO1("Receive Channel data type supported\n");
            if(olc->forwardLogicalChannelParameters.multiplexParameters.t !=
               T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters)
            {
               OOTRACEERR4("ERROR:Unknown multiplex parameter type for "
                           "channel %d (%s, %s)\n",
                           olc->forwardLogicalChannelNumber,
                           call->callType, call->callToken);
               memFreePtr(call->pctxt, epCap);
               epCap = NULL;
               continue;
            }
            h2250lcp = olc->forwardLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;

            if(ooIsSessionEstablished(call, olc->forwardLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters->sessionID, "receive"))
            {

               OOTRACEINFO4("Receive channel with sessionID %d already "
                            "established.(%s, %s)\n", olc->forwardLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters->sessionID,
                            call->callType, call->callToken);
               memFreePtr(call->pctxt, epCap);
               epCap = NULL;
               continue;
            }

            /* Extract mediaControlChannel info, if supplied */
            if(h2250lcp->m.mediaControlChannelPresent)
            {
               if(OO_OK != ooGetIpPortFromH245TransportAddress(call,
                                &h2250lcp->mediaControlChannel,
                                remoteMediaControlIP, &remoteMediaControlPort))
               {
                  OOTRACEERR3("Error: Invalid media control channel address "
                              "(%s, %s)\n", call->callType, call->callToken);
                  memFreePtr(call->pctxt, epCap);
                  epCap = NULL;
                  continue;
               }
            }
         }
         else if(olc->m.reverseLogicalChannelParametersPresent)
         {/* Reverse channel - remote receives - local transmits */
            OOTRACEDBGC4("Processing received reverse olc %d (%s, %s)\n",
                          olc->forwardLogicalChannelNumber, call->callType,
                          call->callToken);
            dir = OOTX;
            epCap = ooIsDataTypeSupported(call,
                                &olc->reverseLogicalChannelParameters.dataType,
                                OOTX);
            if(!epCap)
               continue;

            OOTRACEINFO1("Transmit Channel data type supported\n");

            if(olc->reverseLogicalChannelParameters.multiplexParameters.t !=
               T_H245OpenLogicalChannel_reverseLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters)
            {
               OOTRACEERR4("ERROR:Unknown multiplex parameter type for "
                           "channel %d (%s, %s)\n",
                           olc->forwardLogicalChannelNumber,
                           call->callType, call->callToken);
               memFreePtr(call->pctxt, epCap);
               epCap = NULL;
               continue;
            }
            if(ooIsSessionEstablished(call, olc->reverseLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters->sessionID, "transmit"))
            {

               OOTRACEINFO4("Transmit session with sessionID %d already "
                            "established.(%s, %s)\n", olc->reverseLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters->sessionID, call->callType, call->callToken);

               memFreePtr(call->pctxt, epCap);
               epCap = NULL;
               continue;
            }
             /* Extract the remote media endpoint address */
            h2250lcp = olc->reverseLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;
            if(!h2250lcp)
            {
               OOTRACEERR3("ERROR:Invalid OLC received in fast start. No "
                           "reverse Logical Channel Parameters found. "
                           "(%s, %s)\n", call->callType, call->callToken);
               memFreePtr(call->pctxt, epCap);
               epCap = NULL;
               return OO_FAILED;
            }
            if(!h2250lcp->m.mediaChannelPresent)
            {
               OOTRACEERR3("ERROR:Invalid OLC received in fast start. No "
                           "reverse media channel information found. "
                           "(%s, %s)\n", call->callType, call->callToken);
               memFreePtr(call->pctxt, epCap);
               epCap = NULL;
               return OO_FAILED;
            }
            if(OO_OK != ooGetIpPortFromH245TransportAddress(call,
                                &h2250lcp->mediaChannel,
                                remoteMediaIP, &remoteMediaPort))
            {
               OOTRACEERR3("Error: Invalid media  channel address "
                           "(%s, %s)\n", call->callType, call->callToken);
               memFreePtr(call->pctxt, epCap);
               epCap = NULL;
               continue;
            }

            /* Extract mediaControlChannel info, if supplied */
            if(h2250lcp->m.mediaControlChannelPresent)
            {
               if(OO_OK != ooGetIpPortFromH245TransportAddress(call,
                                &h2250lcp->mediaControlChannel,
                                remoteMediaControlIP, &remoteMediaControlPort))
               {
                  OOTRACEERR3("Error: Invalid media control channel address "
                              "(%s, %s)\n", call->callType, call->callToken);
                  memFreePtr(call->pctxt, epCap);
                  epCap = NULL;
                  continue;
               }
            }
         }

         if(dir & OOTX)
         {  /* According to the spec if we are accepting olc for transmission
               from called endpoint to calling endpoint, called endpoint should
               insert a unqiue forwardLogicalChannelNumber into olc
            */
            olc->forwardLogicalChannelNumber =  call->logicalChanNoCur++;
            if(call->logicalChanNoCur > call->logicalChanNoMax)
               call->logicalChanNoCur = call->logicalChanNoBase;
         }

        
         ooPrepareFastStartResponseOLC(call, olc, epCap, pctxt, dir);
        
         pChannel = ooFindLogicalChannelByLogicalChannelNo
            (call, olc->forwardLogicalChannelNumber);
  
         if(dir & OORX)
         {
            strcpy(pChannel->remoteIP, remoteMediaControlIP);
            pChannel->remoteMediaControlPort = remoteMediaControlPort;
            if(epCap->startReceiveChannel)
            {  
               epCap->startReceiveChannel(call, pChannel);     
               OOTRACEINFO4("Receive channel of type %s started (%s, %s)\n",
                        (epCap->capType == OO_CAP_TYPE_AUDIO)?"audio":"video",
                        call->callType, call->callToken);
            }
            else{
               OOTRACEERR4("ERROR:No callback registered to start receive %s"
                          " channel (%s, %s)\n",
                        (epCap->capType == OO_CAP_TYPE_AUDIO)?"audio":"video",
                           call->callType, call->callToken);
               return OO_FAILED;
            }
         }
         if(dir & OOTX)
         {
            pChannel->remoteMediaPort = remoteMediaPort;
            strcpy(pChannel->remoteIP, remoteMediaIP);
            pChannel->remoteMediaControlPort = remoteMediaControlPort;

            if(epCap->startTransmitChannel)
            {  
               epCap->startTransmitChannel(call, pChannel);     
               OOTRACEINFO3("Transmit channel of type audio started "
                            "(%s, %s)\n", call->callType, call->callToken);
               /*OO_SETFLAG (call->flags, OO_M_AUDIO);*/
            }
            else{
               OOTRACEERR3("ERROR:No callback registered to start transmit"
                           " audio channel (%s, %s)\n", call->callType,
                           call->callToken);
               return OO_FAILED;
            }
         }
         /* Do not specify msg buffer let automatic allocation work */
         setPERBuffer(pctxt, NULL, 0, 1);
         if(asn1PE_H245OpenLogicalChannel(pctxt, olc) != ASN_OK)
         {
            OOTRACEERR3("ERROR:Encoding of olc failed for faststart "
                        "(%s, %s)\n", call->callType, call->callToken);
            ooFreeQ931Message(q931msg);
            if(call->callState < OO_CALL_CLEAR)
            {
               call->callEndReason = OO_REASON_LOCAL_CLEARED;
               call->callState = OO_CALL_CLEAR;
            }
            return OO_FAILED;
         }
         pFS[j].data = encodeGetMsgPtr(pctxt, &(pFS[j].numocts));
         olc = NULL;
         j++;
         epCap = NULL;
      }
      OOTRACEDBGA4("Added %d fast start elements to CONNECT message "
                   "(%s, %s)\n",  j, call->callType, call->callToken);
      if(j != 0)
      {
         connect->m.fastStartPresent = TRUE;
         connect->fastStart.n = j;
         connect->fastStart.elem = pFS;
      }else{
         OOTRACEINFO3("None of the faststart elements received in setup can be"
                      " supported, rejecting faststart.(%s, %s)\n",
                      call->callType, call->callToken);
         connect->m.fastStartPresent = FALSE;
         OO_CLRFLAG(call->flags, OO_M_FASTSTART);
         OOTRACEDBGC3("Faststart for call is disabled by local endpoint."
                      "(%s, %s)\n", call->callType, call->callToken);
      }
   }

    /* Add h245 listener address. Do not add H245 listener address in case
       of fast-start. */
   if ((!OO_TESTFLAG(call->flags, OO_M_FASTSTART) ||
        call->remoteFastStartOLCs.count == 0) &&
       !OO_TESTFLAG (call->flags, OO_M_TUNNELING))
   {
      ooCreateH245Listener(call); /* First create an H.245 listener */
      connect->m.h245AddressPresent = TRUE;
      connect->h245Address.t = T_H225TransportAddress_ipAddress;
  
      h245IpAddr = (H225TransportAddress_ipAddress*)
         memAllocZ (pctxt, sizeof(H225TransportAddress_ipAddress));
      if(!h245IpAddr)
      {
         OOTRACEERR3("Error:Memory - ooAcceptCall - h245IpAddr"
                     "(%s, %s)\n", call->callType, call->callToken);
        return OO_FAILED;
      }
      ooSocketConvertIpToNwAddr(gH323ep.signallingIP, h245IpAddr->ip.data);
      h245IpAddr->ip.numocts=4;
      h245IpAddr->port = *(call->h245listenport);
      connect->h245Address.u.ipAddress = h245IpAddr;
   }

   OOTRACEDBGA3("Built H.225 Connect message (%s, %s)\n", call->callType,
                 call->callToken);

   /* H225 message callback */
   if(gH323ep.h225Callbacks.onBuiltConnect)
      gH323ep.h225Callbacks.onBuiltConnect(call, q931msg);

   ret=ooSendH225Msg(call, q931msg);
   if(ret != OO_OK)
   {
      OOTRACEERR3("Error:Failed to enqueue Connect message to outbound queue.(%s, %s)\n", call->callType, call->callToken);
      memReset(&gH323ep.msgctxt);
      return OO_FAILED;
   }
   memReset(&gH323ep.msgctxt);

#if 0
   if (OO_TESTFLAG (call->flags, OO_M_TUNNELING))
   {
      /* Start terminal capability exchange and master slave determination */
      ret = ooSendTermCapMsg(call);
      if(ret != OO_OK)
      {
         OOTRACEERR3("ERROR:Sending Terminal capability message (%s, %s)\n",
                      call->callType, call->callToken);
         return ret;
      }
      ret = ooSendMasterSlaveDetermination(call);
      if(ret != OO_OK)
      {
         OOTRACEERR3("ERROR:Sending Master-slave determination message "
                  "(%s, %s)\n", call->callType, call->callToken);
         return ret;
      }  
   }
#endif
   return OO_OK;
}

int ooH323HandleCallFwdRequest(OOH323CallData *call)
{
   OOH323CallData *fwdedCall=NULL;
   OOCTXT *pctxt;
   ooAliases *pNewAlias=NULL, *alias=NULL;
   int i=0, ret = OO_OK;
   /* Note: We keep same callToken, for new call which is going
      to replace an existing call, thus treating it as a single call.*/

   fwdedCall = ooCreateCall("outgoing", call->callToken);

   pctxt = fwdedCall->pctxt;

   /* Retrieve new destination info from original call */
   if(!ooUtilsIsStrEmpty(call->pCallFwdData->ip))
   {
      strcpy(fwdedCall->remoteIP, call->pCallFwdData->ip);
   }
   fwdedCall->remotePort = call->pCallFwdData->port;
  
   if(call->pCallFwdData->aliases)
   {
      alias = call->pCallFwdData->aliases;
      while(alias)
      {
         pNewAlias = (ooAliases*) memAlloc(pctxt, sizeof(ooAliases));
         pNewAlias->value = (char*) memAlloc(pctxt, strlen(alias->value)+1);
         if(!pNewAlias || !pNewAlias->value)
         {
            OOTRACEERR3("Error:Memory - ooH323HandleCallFwdRequest - "
                        "pNewAlias/pNewAlias->value"
                        "(%s, %s)\n", call->callType, call->callToken);
            ooCleanCall(fwdedCall);
            return OO_FAILED;
         }
         pNewAlias->type = alias->type;
         strcpy(pNewAlias->value, alias->value);
         pNewAlias->next = fwdedCall->remoteAliases;
         fwdedCall->remoteAliases = pNewAlias;
         alias = alias->next;
         pNewAlias = NULL;
      }
   }

   fwdedCall->callReference = ooGenerateCallReference();
   ooGenerateCallIdentifier(&fwdedCall->callIdentifier);
   fwdedCall->confIdentifier.numocts = 16;
   for (i = 0; i < 16; i++)
      fwdedCall->confIdentifier.data[i] = i+1;
     

   if(gH323ep.gkClient && !OO_TESTFLAG(fwdedCall->flags, OO_M_DISABLEGK))
   {
     /* No need to check registration status here as it is already checked for
        MakeCall command */
      ret = ooGkClientSendAdmissionRequest(gH323ep.gkClient, fwdedCall, FALSE);
      fwdedCall->callState = OO_CALL_WAITING_ADMISSION;
   }
   else
      ret = ooH323CallAdmitted (fwdedCall);

   return OO_OK;

}

int ooH323MakeCall(char *dest, char *callToken, ooCallOptions *opts)
{
   OOCTXT *pctxt;
   OOH323CallData *call;
   int ret=0, i=0;
   char tmp[30]="\0";
   char *ip=NULL, *port = NULL;

   if(!dest)
   {
      OOTRACEERR1("ERROR:Invalid destination for new call\n");
      return OO_FAILED;
   }
   if(!callToken)
   {
      OOTRACEERR1("ERROR: Invalid callToken parameter to make call\n");
      return OO_FAILED;
   }

   call = ooCreateCall("outgoing", callToken);
   pctxt = call->pctxt;
   if(opts)
   {
      if(opts->fastStart)
         OO_SETFLAG(call->flags, OO_M_FASTSTART);
      else
         OO_CLRFLAG(call->flags, OO_M_FASTSTART);

      if(opts->tunneling)
         OO_SETFLAG(call->flags, OO_M_TUNNELING);
      else
         OO_CLRFLAG(call->flags, OO_M_TUNNELING);

      if(opts->disableGk)
         OO_SETFLAG(call->flags, OO_M_DISABLEGK);
      else
         OO_CLRFLAG(call->flags, OO_M_DISABLEGK);

      call->callMode = opts->callMode;
   }


   ret = ooParseDestination(call->pctxt, dest, tmp, 30, &call->remoteAliases);
   if(ret != OO_OK)
   {
      OOTRACEERR2("Error: Failed to parse the destination string %s for "
                  "new call\n", dest);
      ooCleanCall(call);
      return OO_FAILED;
   }
  
   /* Check whether we have ip address */
   if(!ooUtilsIsStrEmpty(tmp))
   {
      ip = tmp;
      port = strchr(tmp, ':');
      *port = '\0';
      port++;
      strcpy(call->remoteIP, ip);
      call->remotePort = atoi(port);
   }

   strcpy(callToken, call->callToken);
   call->callReference = ooGenerateCallReference();
   ooGenerateCallIdentifier(&call->callIdentifier);
   call->confIdentifier.numocts = 16;
   for (i = 0; i < 16; i++)
      call->confIdentifier.data[i] = i+1;
     

   if(gH323ep.gkClient && !OO_TESTFLAG(call->flags, OO_M_DISABLEGK))
   {
     /* No need to check registration status here as it is already checked for
        MakeCall command */
      ret = ooGkClientSendAdmissionRequest(gH323ep.gkClient, call, FALSE);
      call->callState = OO_CALL_WAITING_ADMISSION;
   }
   else
      ret = ooH323CallAdmitted (call);

   return OO_OK;
}


int ooH323CallAdmitted(OOH323CallData *call)
{
   int ret=0;
     
   if(!call)
   {
      /* Call not supplied. Must locate it in list */
      OOTRACEERR1("ERROR: Invalid call parameter to ooH323CallAdmitted");
      return OO_FAILED;
   }

   if(!strcmp(call->callType, "outgoing"))
   {
      ret = ooCreateH225Connection(call);
      if(ret != OO_OK)
      {
         OOTRACEERR3("ERROR:Failed to create H225 connection to %s:%d\n",
                      call->remoteIP, call->remotePort);
         if(call->callState< OO_CALL_CLEAR)
         {
            call->callState = OO_CALL_CLEAR;
            call->callEndReason = OO_REASON_UNKNOWN;
         }
         return OO_FAILED;
      }
     
      ret = ooH323MakeCall_helper(call);
   } else { /* An incoming call */
      if(gH323ep.h323Callbacks.onIncomingCall)
         gH323ep.h323Callbacks.onIncomingCall(call);
      ooSendAlerting(call); /* Send alerting message */
      if(OO_TESTFLAG(gH323ep.flags, OO_M_AUTOANSWER))
         ooSendConnect(call); /* Send connect message - call accepted */
   }
  
   return OO_OK;
}

int ooH323MakeCall_helper(OOH323CallData *call)
{
   int ret=0,i=0, k;
   Q931Message *q931msg = NULL;
   H225Setup_UUIE *setup;

   ASN1DynOctStr *pFS=NULL;
   H225TransportAddress_ipAddress *destCallSignalIpAddress;

   H225TransportAddress_ipAddress *srcCallSignalIpAddress;
   ooH323EpCapability *epCap=NULL;
   DListNode *curNode = NULL;
   OOCTXT *pctxt = NULL;
   H245OpenLogicalChannel *olc;
   ASN1BOOL aligned = 1;
   H225AliasAddress * pAliasAddress=NULL;
   pctxt = &gH323ep.msgctxt;
    
   ret = ooCreateQ931Message(&q931msg, Q931SetupMsg);
   if(ret != OO_OK)
   {
      OOTRACEERR1("ERROR:Failed to Create Q931 SETUP Message\n ");
      return OO_FAILED;
   }

   q931msg->callReference = call->callReference;

   /* Set bearer capability */
   if(OO_OK != ooSetBearerCapabilityIE(q931msg, Q931CCITTStd,
                          Q931TransferSpeech, Q931TransferCircuitMode,
                          Q931TransferRate64Kbps, Q931UserInfoLayer1G711ULaw))
   {
      OOTRACEERR3("Error: Failed to set bearer capability ie.(%s, %s)\n",
                   call->callType, call->callToken);
      return OO_FAILED;
   }

   /* Set calling party number  Q931 IE */
   if(call->callingPartyNumber)
     ooQ931SetCallingPartyNumberIE(q931msg,
                            (const char*)call->callingPartyNumber, 1, 0, 1, 1);

   /* Set called party number Q931 ie */
   if(call->calledPartyNumber)
      ooQ931SetCalledPartyNumberIE(q931msg,
                            (const char*)call->calledPartyNumber, 1, 0);

   q931msg->userInfo = (H225H323_UserInformation*)memAlloc(pctxt,
                             sizeof(H225H323_UserInformation));
   if(!q931msg->userInfo)
   {
      OOTRACEERR1("ERROR:Memory - ooH323MakeCall_helper - userInfo\n");
      return OO_FAILED;
   }
   memset(q931msg->userInfo, 0, sizeof(H225H323_UserInformation));

   setup = (H225Setup_UUIE*) memAlloc(pctxt, sizeof(H225Setup_UUIE));
   if(!setup)
   {
      OOTRACEERR3("Error:Memory -  ooH323MakeCall_helper - setup (%s, %s)\n",
                  call->callType, call->callToken);
      return OO_FAILED;
   }
   memset (setup, 0, sizeof(H225Setup_UUIE));
   setup->protocolIdentifier = gProtocolID;
  
   /* Populate Alias Address.*/

   if(call->ourAliases || gH323ep.aliases)
   {  
      setup->m.sourceAddressPresent = TRUE;
      if(call->ourAliases)
         ret = ooPopulateAliasList(pctxt, call->ourAliases,
                                                       &setup->sourceAddress);
      else if(gH323ep.aliases)
         ret =  ooPopulateAliasList(pctxt, gH323ep.aliases,
                                                       &setup->sourceAddress);
      if(OO_OK != ret)
      {
         OOTRACEERR1("Error:Failed to populate alias list in SETUP message\n");
         memReset(pctxt);
         return OO_FAILED;
      }
   }
   setup->m.presentationIndicatorPresent = TRUE;
   setup->presentationIndicator.t =
                             T_H225PresentationIndicator_presentationAllowed;
   setup->m.screeningIndicatorPresent = TRUE;
   setup->screeningIndicator = userProvidedNotScreened;

   /* Populate Destination aliases */
   if(call->remoteAliases)
   {
      setup->m.destinationAddressPresent = TRUE;
      ret = ooPopulateAliasList(pctxt, call->remoteAliases,
                                                 &setup->destinationAddress);
      if(OO_OK != ret)
      {
         OOTRACEERR1("Error:Failed to populate destination alias list in SETUP"
                     "message\n");
         memReset(pctxt);
         return OO_FAILED;
      }
   }

   /* Populate the vendor information */
   setup->sourceInfo.m.vendorPresent=TRUE;
   setup->sourceInfo.vendor.vendor.t35CountryCode = gH323ep.t35CountryCode;
   setup->sourceInfo.vendor.vendor.t35Extension = gH323ep.t35Extension;
   setup->sourceInfo.vendor.vendor.manufacturerCode= gH323ep.manufacturerCode;
  
   if(gH323ep.productID)
   {
      setup->sourceInfo.vendor.m.productIdPresent=TRUE;
      setup->sourceInfo.vendor.productId.numocts = ASN1MIN(
                              strlen(gH323ep.productID),
                              sizeof(setup->sourceInfo.vendor.productId.data));
      strncpy((char*)setup->sourceInfo.vendor.productId.data,
                gH323ep.productID, setup->sourceInfo.vendor.productId.numocts);
   }
   else
      setup->sourceInfo.vendor.m.productIdPresent=FALSE;
  
   if(gH323ep.versionID)
   {
      setup->sourceInfo.vendor.m.versionIdPresent=TRUE;
      setup->sourceInfo.vendor.versionId.numocts = ASN1MIN(
                              strlen(gH323ep.versionID),
                              sizeof(setup->sourceInfo.vendor.versionId.data));
      strncpy((char*)setup->sourceInfo.vendor.versionId.data,
              gH323ep.versionID, setup->sourceInfo.vendor.versionId.numocts);
   }
   else
      setup->sourceInfo.vendor.m.versionIdPresent=FALSE;
  
   setup->sourceInfo.mc = FALSE;
   setup->sourceInfo.undefinedNode = FALSE;

   /* Populate the destination Call Signal Address */
   setup->destCallSignalAddress.t=T_H225TransportAddress_ipAddress;
   destCallSignalIpAddress = (H225TransportAddress_ipAddress*)memAlloc(pctxt,
                                  sizeof(H225TransportAddress_ipAddress));
   if(!destCallSignalIpAddress)
   {
      OOTRACEERR3("Error:Memory -  ooH323MakeCall_helper - "
                 "destCallSignalAddress. (%s, %s)\n", call->callType,
                 call->callToken);
      return OO_FAILED;
   }
   ooSocketConvertIpToNwAddr(call->remoteIP, destCallSignalIpAddress->ip.data);

   destCallSignalIpAddress->ip.numocts=4;
   destCallSignalIpAddress->port = call->remotePort;

   setup->destCallSignalAddress.u.ipAddress = destCallSignalIpAddress;
   setup->m.destCallSignalAddressPresent=TRUE;
   setup->activeMC=FALSE;

   /* Populate the source Call Signal Address */
   setup->sourceCallSignalAddress.t=T_H225TransportAddress_ipAddress;
   srcCallSignalIpAddress = (H225TransportAddress_ipAddress*)memAlloc(pctxt,
                                  sizeof(H225TransportAddress_ipAddress));
   if(!srcCallSignalIpAddress)
   {
      OOTRACEERR3("Error:Memory - ooH323MakeCall_helper - srcCallSignalAddress"
                  "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   ooSocketConvertIpToNwAddr(gH323ep.signallingIP,
                                              srcCallSignalIpAddress->ip.data);

   srcCallSignalIpAddress->ip.numocts=4;
   srcCallSignalIpAddress->port= call->pH225Channel->port;
   setup->sourceCallSignalAddress.u.ipAddress = srcCallSignalIpAddress;
   setup->m.sourceCallSignalAddressPresent=TRUE;
   /* No fast start */
   if(!OO_TESTFLAG(gH323ep.flags, OO_M_FASTSTART))
   {
      setup->m.fastStartPresent = FALSE;
   }
   else{
      setup->m.fastStartPresent = TRUE;
      pFS = (ASN1DynOctStr*)memAlloc(pctxt, gH323ep.noOfCaps*
                                       sizeof(ASN1DynOctStr));
      if(!pFS)
      {
         OOTRACEERR3("Error:Memory - ooH323MakeCall_helper - pFS(%s, %s)\n",
                     call->callType, call->callToken);
         return OO_FAILED;
      }

      /* Use preference order of codecs */
      i=0;
      for(k=0; k< call->capPrefs.index; k++)
      {
         OOTRACEDBGC5("Preffered capability at index %d is %s. (%s, %s)\n",
                      k, ooGetCapTypeText(call->capPrefs.order[k]),
                      call->callType, call->callToken);

         if(call->ourCaps)
         {
            epCap = call->ourCaps;
            OOTRACEDBGC3("Using call specific capabilities in faststart of "
                         "setup message. (%s, %s)\n", call->callType,
                         call->callToken);
         }
         else{
            epCap = gH323ep.myCaps;
            OOTRACEDBGC3("Using end-point capabilities for faststart of setup"
                         "message. (%s, %s)\n", call->callType,
                         call->callToken);
         }

         while(epCap){
            if(epCap->cap == call->capPrefs.order[k]) break;
            else epCap = epCap->next;
         }
         if(!epCap)
         {
            OOTRACEWARN4("Warn:Preferred capability %s is abscent in "
                         "capability list. (%s, %s)\n",
                         ooGetCapTypeText(call->capPrefs.order[k]),
                         call->callType, call->callToken);
            continue;
         }



         OOTRACEDBGC4("Building olcs with capability %s. (%s, %s)\n",
                      ooGetCapTypeText(epCap->cap), call->callType,
                      call->callToken);
         if(epCap->dir & OORX)
         {
            olc = (H245OpenLogicalChannel*)memAlloc(pctxt,
                                             sizeof(H245OpenLogicalChannel));
            if(!olc)
            {
               OOTRACEERR3("ERROR:Memory - ooH323MakeCall_helper - olc(%s, %s)"
                           "\n", call->callType, call->callToken);
               ooFreeQ931Message(q931msg);
               if(call->callState < OO_CALL_CLEAR)
               {
                  call->callEndReason = OO_REASON_LOCAL_CLEARED;
                  call->callState = OO_CALL_CLEAR;
               }
               return OO_FAILED;
            }
            memset(olc, 0, sizeof(H245OpenLogicalChannel));
            olc->forwardLogicalChannelNumber = call->logicalChanNoCur++;
            if(call->logicalChanNoCur > call->logicalChanNoMax)
               call->logicalChanNoCur = call->logicalChanNoBase;
       
            ooBuildFastStartOLC(call, olc, epCap, pctxt, OORX);
            /* Do not specify msg buffer let automatic allocation work */
            setPERBuffer(pctxt, NULL, 0, aligned);
            if(asn1PE_H245OpenLogicalChannel(pctxt, olc) != ASN_OK)
            {
               OOTRACEERR3("ERROR:Encoding of olc failed for faststart(%s, %s)"
                           "\n", call->callType, call->callToken);
               ooFreeQ931Message(q931msg);
               if(call->callState < OO_CALL_CLEAR)
               {
                  call->callEndReason = OO_REASON_LOCAL_CLEARED;
                  call->callState = OO_CALL_CLEAR;
               }
               return OO_FAILED;
            }
            pFS[i].data = encodeGetMsgPtr(pctxt, &(pFS[i].numocts));
            olc = NULL;
            i++;
            OOTRACEDBGC5("Added RX fs element %d with capability %s(%s, %s)\n",
                          i, ooGetCapTypeText(epCap->cap), call->callType,
                          call->callToken);
         }

         if(epCap->dir & OOTX)
         {
            olc = (H245OpenLogicalChannel*)memAlloc(pctxt,
                                             sizeof(H245OpenLogicalChannel));
            if(!olc)
            {
               OOTRACEERR3("ERROR:Memory - ooH323MakeCall_helper - olc(%s, %s)"
                           "\n", call->callType, call->callToken);
               ooFreeQ931Message(q931msg);
               if(call->callState < OO_CALL_CLEAR)
               {
                  call->callEndReason = OO_REASON_LOCAL_CLEARED;
                  call->callState = OO_CALL_CLEAR;
               }
               return OO_FAILED;
            }
            memset(olc, 0, sizeof(H245OpenLogicalChannel));
            olc->forwardLogicalChannelNumber = call->logicalChanNoCur++;
            if(call->logicalChanNoCur > call->logicalChanNoMax)
               call->logicalChanNoCur = call->logicalChanNoBase;
       
            ooBuildFastStartOLC(call, olc, epCap, pctxt, OOTX);
            /* Do not specify msg buffer let automatic allocation work */
            setPERBuffer(pctxt, NULL, 0, aligned);
            if(asn1PE_H245OpenLogicalChannel(pctxt, olc) != ASN_OK)
            {
               OOTRACEERR3("ERROR:Encoding of olc failed for faststart(%s, %s)"
                           "\n", call->callType, call->callToken);
               ooFreeQ931Message(q931msg);
               if(call->callState < OO_CALL_CLEAR)
               {
                  call->callEndReason = OO_REASON_LOCAL_CLEARED;
                  call->callState = OO_CALL_CLEAR;
               }
               return OO_FAILED;
            }
            pFS[i].data = encodeGetMsgPtr(pctxt, &(pFS[i].numocts));
            olc = NULL;
            i++;
            OOTRACEDBGC5("Added TX fs element %d with capability %s(%s, %s)\n",
                          i, ooGetCapTypeText(epCap->cap), call->callType,
                          call->callToken);
         }

      }
      OOTRACEDBGA4("Added %d fast start elements to SETUP message (%s, %s)\n",
                   i, call->callType, call->callToken);
      setup->fastStart.n = i;
      setup->fastStart.elem = pFS;
   }

   setup->conferenceID.numocts= call->confIdentifier.numocts;
   memcpy(setup->conferenceID.data, call->confIdentifier.data,
          call->confIdentifier.numocts);

   setup->conferenceGoal.t = T_H225Setup_UUIE_conferenceGoal_create;
   /* H.225 point to point call */
   setup->callType.t = T_H225CallType_pointToPoint;

   /* Populate optional fields */
   setup->m.callIdentifierPresent = TRUE;
   /*ooGenerateCallIdentifier(&setup->callIdentifier);*/
   setup->callIdentifier.guid.numocts = call->callIdentifier.guid.numocts;
   memcpy(setup->callIdentifier.guid.data, call->callIdentifier.guid.data,
                               call->callIdentifier.guid.numocts);
  
   setup->m.mediaWaitForConnectPresent = TRUE;
   setup->mediaWaitForConnect = FALSE;
   setup->m.canOverlapSendPresent = TRUE;
   setup->canOverlapSend = FALSE;

   /* Populate the userInfo structure with the setup UUIE */
  
   q931msg->userInfo->h323_uu_pdu.h323_message_body.t =
         T_H225H323_UU_PDU_h323_message_body_setup;
   q931msg->userInfo->h323_uu_pdu.h323_message_body.u.setup = setup;
   q931msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent=1;
  
   q931msg->userInfo->h323_uu_pdu.h245Tunneling =
      OO_TESTFLAG (call->flags, OO_M_TUNNELING);

   /* For H.323 version 4 and higher, if fast connect, tunneling should be
      supported.
   */
   if(OO_TESTFLAG(gH323ep.flags, OO_M_FASTSTART))
      q931msg->userInfo->h323_uu_pdu.h245Tunneling = TRUE;

   OOTRACEDBGA3("Built SETUP message (%s, %s)\n", call->callType,
                 call->callToken);
  
   /* H225 message callback */
   if(gH323ep.h225Callbacks.onBuiltSetup)
      gH323ep.h225Callbacks.onBuiltSetup(call, q931msg);

   ret=ooSendH225Msg(call, q931msg);
   if(ret != OO_OK)
   {
     OOTRACEERR3("Error:Failed to enqueue SETUP message to outbound queue. (%s, %s)\n", call->callType, call->callToken);
   }
   memReset(&gH323ep.msgctxt);

   return ret;
}

  
int ooH323ForwardCall(char* callToken, char *dest)
{
   int ret=0;
   Q931Message *pQ931Msg = NULL;
   H225Facility_UUIE *facility=NULL;
   OOCTXT *pctxt = &gH323ep.msgctxt;
   OOH323CallData *call;
   char ip[30]="\0", *pcPort=NULL;
   int port=0;
   H225TransportAddress_ipAddress *fwdCallSignalIpAddress;

   call= ooFindCallByToken(callToken);
   if(!call)
   {
      OOTRACEERR2("ERROR: Invalid call token for forward - %s\n", callToken);
      return OO_FAILED;
   }
   OOTRACEDBGA3("Building Facility message for call forward (%s, %s)\n",
                                              call->callType, call->callToken);
   call->pCallFwdData = (OOCallFwdData*)memAllocZ(call->pctxt,
                                                     sizeof(OOCallFwdData));
   if(!call->pCallFwdData)
   {
     OOTRACEERR3("Error:Memory - ooH323ForwardCall - pCallFwdData (%s, %s)\n",
     call->callType, call->callToken);
     return OO_FAILED;
   }

   ret = ooParseDestination(call->pctxt, dest, ip, 20,
                                             &call->pCallFwdData->aliases);
   if(ret != OO_OK)
   {
      OOTRACEERR4("Error:Failed to parse the destination %s for call fwd."
                  "(%s, %s)\n", dest, call->callType, call->callToken);
      memFreePtr(call->pctxt, call->pCallFwdData);
      return OO_FAILED;
   }

   if(!ooUtilsIsStrEmpty(ip))
   {
      pcPort = strchr(ip, ':');
      if(pcPort)
      {
         *pcPort = '\0';
         pcPort++;
         call->pCallFwdData->port = atoi(pcPort);
      }
      strcpy(call->pCallFwdData->ip, ip);
   }

   ret = ooCreateQ931Message(&pQ931Msg, Q931FacilityMsg);
   if(ret != OO_OK)
   {
      OOTRACEERR3
         ("ERROR: In allocating memory for call transfer facility message "
          "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }

   q931msg->callReference = call->callReference;

   pQ931Msg->userInfo = (H225H323_UserInformation*)memAlloc(pctxt,
                             sizeof(H225H323_UserInformation));
   if(!pQ931Msg->userInfo)
   {
      OOTRACEERR3("ERROR:Memory - ooH323ForwardCall - userInfo(%s, %s)\n",
                   call->callType, call->callToken);
      return OO_FAILED;
   }
   memset (pQ931Msg->userInfo, 0, sizeof(H225H323_UserInformation));
   pQ931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent=1;

   pQ931Msg->userInfo->h323_uu_pdu.h245Tunneling =
                                   OO_TESTFLAG (call->flags, OO_M_TUNNELING);

   pQ931Msg->userInfo->h323_uu_pdu.h323_message_body.t =
      T_H225H323_UU_PDU_h323_message_body_facility;
  
   facility = (H225Facility_UUIE*)
      memAllocZ (pctxt, sizeof(H225Facility_UUIE));

   if(!facility)
   {
      OOTRACEERR3("ERROR:Memory - ooH323ForwardCall - facility (%s, %s)"
                  "\n", call->callType, call->callToken);
      return OO_FAILED;
   }

   pQ931Msg->userInfo->h323_uu_pdu.h323_message_body.u.facility = facility;
  
   facility->protocolIdentifier = gProtocolID; 
   facility->m.callIdentifierPresent = 1;
   facility->callIdentifier.guid.numocts =
      call->callIdentifier.guid.numocts;

   memcpy(facility->callIdentifier.guid.data,
          call->callIdentifier.guid.data,
          call->callIdentifier.guid.numocts);

   facility->reason.t = T_H225FacilityReason_callForwarded;
  
   if(!ooUtilsIsStrEmpty(call->pCallFwdData->ip))
   {
      facility->m.alternativeAddressPresent = TRUE;
      facility->alternativeAddress.t=T_H225TransportAddress_ipAddress;
      fwdCallSignalIpAddress = (H225TransportAddress_ipAddress*)memAlloc(pctxt,
                                  sizeof(H225TransportAddress_ipAddress));
      if(!fwdCallSignalIpAddress)
      {
         OOTRACEERR3("Error:Memory - ooH323ForwardCall - fwdCallSignalAddress"
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
      ooSocketConvertIpToNwAddr(call->pCallFwdData->ip,
                                          fwdCallSignalIpAddress->ip.data);

      fwdCallSignalIpAddress->ip.numocts=4;
      fwdCallSignalIpAddress->port = call->pCallFwdData->port;
      facility->alternativeAddress.u.ipAddress = fwdCallSignalIpAddress;
   }

   if(call->pCallFwdData->aliases)
   {   
      facility->m.alternativeAliasAddressPresent = TRUE;
      ret = ooPopulateAliasList(pctxt, call->pCallFwdData->aliases,
                                        &facility->alternativeAliasAddress);
      if(ret != OO_OK)
      {
         OOTRACEERR3("Error:Failed to populate alternate aliases in "
                     "ooH323ForwardCall. (%s, %s)\n", call->callType,
                     call->callToken);
         return OO_FAILED;
      }
   }

    ret = ooSendH225Msg(call, pQ931Msg);
   if(ret != OO_OK)
   {
      OOTRACEERR3
         ("Error:Failed to enqueue Forward Facility message to outbound "
          "queue.(%s, %s)\n", call->callType, call->callToken);
   }
   call->callEndReason = OO_REASON_LOCAL_FWDED;
   memReset (&gH323ep.msgctxt);
   return ret;
}

int ooH323HangCall(char * callToken, OOCallClearReason reason)
{
   int ret =0;
   OOH323CallData *call;

   call= ooFindCallByToken(callToken);
   if(!call)
   {
      OOTRACEWARN2("WARN: Call hangup failed - Call %s not present\n",
                    callToken);
      return OO_FAILED;
   }
   OOTRACEINFO3("Hanging up call (%s, %s)\n", call->callType, call->callToken);
   if(call->callState < OO_CALL_CLEAR)
   {
      call->callEndReason = reason;
      call->callState = OO_CALL_CLEAR;
   }
   return OO_OK;
}

int ooSetBearerCapabilityIE
   (Q931Message *pmsg, enum Q931CodingStandard codingStandard,
    enum Q931InformationTransferCapability capability,
    enum Q931TransferMode transferMode, enum Q931TransferRate transferRate,
    enum Q931UserInfoLayer1Protocol userInfoLayer1)
{
   unsigned size = 3;
   OOCTXT *pctxt = &gH323ep.msgctxt;

   if(pmsg->bearerCapabilityIE)
   {
      memFreePtr(pctxt, pmsg->bearerCapabilityIE);
      pmsg->bearerCapabilityIE = NULL;
   }

   pmsg->bearerCapabilityIE = (Q931InformationElement*)
                      memAlloc(pctxt, sizeof(Q931InformationElement)+size-1);
   if(!pmsg->bearerCapabilityIE)
   {
      OOTRACEERR1("Error:Memory - ooSetBearerCapabilityIE - bearerCapabilityIE"
                  "\n");
      return OO_FAILED;
   }

   pmsg->bearerCapabilityIE->discriminator = Q931BearerCapabilityIE;
   pmsg->bearerCapabilityIE->length = size;
   pmsg->bearerCapabilityIE->data[0] = (ASN1OCTET)(0x80 | ((codingStandard&3) << 5) | (capability&31));

   pmsg->bearerCapabilityIE->data[1] = (0x80 | ((transferMode & 3) << 5) | (transferRate & 31));
  
   pmsg->bearerCapabilityIE->data[2] = (0x80 | (1<<5) | userInfoLayer1);

   return OO_OK;
}

int ooQ931SetCallingPartyNumberIE
   (Q931Message *pmsg, const char *number, unsigned plan, unsigned type,
    unsigned presentation, unsigned screening)
{
   unsigned len = 0;
   OOCTXT *pctxt = &gH323ep.msgctxt;

   if(pmsg->callingPartyNumberIE)
   {
      memFreePtr(pctxt, pmsg->callingPartyNumberIE);
      pmsg->callingPartyNumberIE = NULL;
   }

   len = strlen(number);
   pmsg->callingPartyNumberIE = (Q931InformationElement*)
                      memAlloc(pctxt, sizeof(Q931InformationElement)+len+2-1);
   if(!pmsg->callingPartyNumberIE)
   {
      OOTRACEERR1("Error:Memory - ooQ931SetCallingPartyNumberIE - "
                  "callingPartyNumberIE\n");
      return OO_FAILED;
   }
   pmsg->callingPartyNumberIE->discriminator = Q931CallingPartyNumberIE;
   pmsg->callingPartyNumberIE->length = len+2;
   pmsg->callingPartyNumberIE->data[0] = (((type&7)<<4)|(plan&15));
   pmsg->callingPartyNumberIE->data[1] = (0x80|((presentation&3)<<5)|(screening&3));
   memcpy(pmsg->callingPartyNumberIE->data+2, number, len);

   return OO_OK;
}

int ooQ931SetCalledPartyNumberIE
   (Q931Message *pmsg, const char *number, unsigned plan, unsigned type)
{
   unsigned len = 0;
   OOCTXT *pctxt = &gH323ep.msgctxt;

   if(pmsg->calledPartyNumberIE)
   {
      memFreePtr(pctxt, pmsg->calledPartyNumberIE);
      pmsg->calledPartyNumberIE = NULL;
   }

   len = strlen(number);
   pmsg->calledPartyNumberIE = (Q931InformationElement*)
                      memAlloc(pctxt, sizeof(Q931InformationElement)+len+1-1);
   if(!pmsg->calledPartyNumberIE)
   {
      OOTRACEERR1("Error:Memory - ooQ931SetCalledPartyNumberIE - "
                  "calledPartyNumberIE\n");
      return OO_FAILED;
   }
   pmsg->calledPartyNumberIE->discriminator = Q931CalledPartyNumberIE;
   pmsg->calledPartyNumberIE->length = len+1;
   pmsg->calledPartyNumberIE->data[0] = (0x80|((type&7)<<4)|(plan&15));
   memcpy(pmsg->calledPartyNumberIE->data+1, number, len);

   return OO_OK;
}

int ooQ931SetCauseIE
   (Q931Message *pmsg, enum Q931CauseValues cause, unsigned coding,
    unsigned location)
{
   OOCTXT *pctxt = &gH323ep.msgctxt;

   if(pmsg->causeIE){
      memFreePtr(pctxt, pmsg->causeIE);
      pmsg->causeIE = NULL;
   }

   pmsg->causeIE = (Q931InformationElement*)
                      memAlloc(pctxt, sizeof(Q931InformationElement)+1);
   if(!pmsg->causeIE)
   {
      OOTRACEERR1("Error:Memory - ooQ931SetCauseIE - causeIE\n");
      return OO_FAILED;
   }
   pmsg->causeIE->discriminator = Q931CauseIE;
   pmsg->causeIE->length = 2;
   pmsg->causeIE->data[0] = (0x80 | ((coding & 0x03) <<5) | (location & 0x0F));

   pmsg->causeIE->data[1] = (0x80 | cause);
 
   return OO_OK;
}
#if 0
/*TODO: Once we are comfortable with new parse function, get rid of this one */
int ooParseDestination(OOH323CallData *call, char *dest)
{
   int ret=0, iEk=-1, iDon=-1, iTeen=-1, iChaar=-1, iPort = -1, i;
   ooAliases * psNewAlias = NULL;
   char *cAt = NULL, *host=NULL;
   char tmp[256];
   char *alias=NULL;
   OOTRACEINFO2("Parsing destination %s\n", dest);
 
   /* Test for an IP address:Note that only supports dotted IPv4.
      IPv6 won't pass the test and so will numeric IP representation*/
  
   sscanf(dest, "%d.%d.%d.%d:%d", &iEk, &iDon, &iTeen, &iChaar, &iPort);
   if((iEk > 0            && iEk <= 255)    &&
      (iDon >= 0          && iDon <= 255)   &&
      (iTeen >=0          && iTeen <= 255)  &&
      (iChaar >= 0        && iChaar <= 255) &&
      (!strchr(dest, ':') || iPort != -1))
   {
     /*      memset(call->remoteIP, 0, sizeof(call->remoteIP));*/
      sprintf(call->remoteIP, "%d.%d.%d.%d", iEk, iDon, iTeen, iChaar);
      if(strchr(dest, ':'))
         call->remotePort = iPort;
      else
         call->remotePort = 1720; /*Default h.323 port */
      OOTRACEINFO3("Destination for new call parsed as Ip %s and port %d\n",
                   call->remoteIP, call->remotePort);
      return OO_OK;
   }

   /* alias@host */
   strncpy(tmp, dest, sizeof(tmp)-1);
   tmp[sizeof(tmp)-1]='\0';
   if(host=strchr(tmp, '@'))
   {
      *host = '\0';
      host++;
      sscanf(host, "%d.%d.%d.%d:%d", &iEk, &iDon, &iTeen, &iChaar, &iPort);
      if((iEk > 0            && iEk <= 255)    &&
         (iDon >= 0          && iDon <= 255)   &&
         (iTeen >=0          && iTeen <= 255)  &&
         (iChaar >= 0        && iChaar <= 255) &&
         (!strchr(host, ':') || iPort != -1))
      {

         sprintf(call->remoteIP, "%d.%d.%d.%d", iEk, iDon, iTeen, iChaar);
         if(strchr(dest, ':'))
            call->remotePort = iPort;
         else
            call->remotePort = 1720; /*Default h.323 port */
         OOTRACEINFO3("Destination for new call parsed as Ip %s and port %d\n",
                      call->remoteIP, call->remotePort);
         alias = tmp;
      }
   }

   if(!alias)
   {
     alias = dest;
   }
   /* url test */
   if(alias == strstr(alias, "http://"))
   {
      psNewAlias = (ooAliases*) memAlloc(call->pctxt, sizeof(ooAliases));
      if(!psNewAlias)
      {
         OOTRACEERR1("Error:Memory - ooParseDestination - psNewAlias\n");
         return OO_FAILED;
      }
      /*      memset(psNewAlias, 0, sizeof(ooAliases));*/
      psNewAlias->type = T_H225AliasAddress_url_ID;
      psNewAlias->value = (char*) memAlloc(call->pctxt, strlen(alias)+1);
      if(!psNewAlias->value)
      {
         OOTRACEERR1("Error:Memory - ooParseDestination - psNewAlias->value\n");
         memFreePtr(call->pctxt, psNewAlias);
         return OO_FAILED;
      }
      strcpy(psNewAlias->value, alias);
      psNewAlias->next = call->remoteAliases;
      call->remoteAliases = psNewAlias;
      OOTRACEINFO2("Destination for new call parsed as url %s\n",
                    psNewAlias->value);
      return OO_OK;
   }

   /* E-mail ID test */
   if((cAt = strchr(alias, '@')) && alias != strchr(alias, '@'))
   {
      if(strchr(cAt, '.'))
      {
         psNewAlias = (ooAliases*) memAlloc(call->pctxt, sizeof(ooAliases));
         if(!psNewAlias)
         {
            OOTRACEERR1("Error:Memory - ooParseDestination - psNewAlias\n");
            return OO_FAILED;
         }
         /*         memset(psNewAlias, 0, sizeof(ooAliases));*/
         psNewAlias->type = T_H225AliasAddress_email_ID;
         psNewAlias->value = (char*) memAlloc(call->pctxt, strlen(alias)+1);
         if(!psNewAlias->value)
         {
            OOTRACEERR1("Error:Memory - ooParseDestination - "
                        "psNewAlias->value\n");
            memFreePtr(call->pctxt, psNewAlias);
            return OO_FAILED;
         }
         strcpy(psNewAlias->value, alias);
         psNewAlias->next = call->remoteAliases;
         call->remoteAliases = psNewAlias;
         OOTRACEINFO2("Destination for new call is parsed as email %s\n",
                      psNewAlias->value);
         return OO_OK;
      }
   }

 
   /* e-164 */
   /* strspn(dest, "1234567890*#") == strlen(dest)*/
   /* Dialed digits test*/
   for(i=0; i<(int)strlen(alias); i++)
   {
      if(!isdigit(alias[i]))
         break;
   }
   if(i == (int)strlen(alias))
   {
      psNewAlias = (ooAliases*) memAlloc(call->pctxt, sizeof(ooAliases));
      if(!psNewAlias)
      {
         OOTRACEERR1("Error:Memory - ooParseDestination - psNewAlias\n");
         return OO_FAILED;
      }
      /*      memset(psNewAlias, 0, sizeof(ooAliases));*/
      psNewAlias->type = T_H225AliasAddress_dialedDigits;
      psNewAlias->value = (char*) memAlloc(call->pctxt, strlen(alias)+1);
      if(!psNewAlias->value)
      {
         OOTRACEERR1("Error:Memroy - ooParseDestination - psNewAlias->value\n");
         memFreePtr(call->pctxt, psNewAlias);
         return OO_FAILED;
      }
      strcpy(psNewAlias->value, alias);
      psNewAlias->next = call->remoteAliases;
      call->remoteAliases = psNewAlias;
      OOTRACEINFO2("Destination for new call is parsed as dialed digits %s\n",
                   psNewAlias->value);
      return OO_OK;
   }
   /* Evrything else is an h323-id for now */
   psNewAlias = (ooAliases*) memAlloc(call->pctxt, sizeof(ooAliases));
   if(!psNewAlias)
   {
      OOTRACEERR1("Error:Memory - ooParseDestination - psNewAlias\n");
      return OO_FAILED;
   }
   /*   memset(psNewAlias, 0, sizeof(ooAliases));*/
   psNewAlias->type = T_H225AliasAddress_h323_ID;
   psNewAlias->value = (char*) memAlloc(call->pctxt, strlen(alias)+1);
   if(!psNewAlias->value)
   {
      OOTRACEERR1("Error:Memory - ooParseDestination - psNewAlias->value\n");
      memFreePtr(call->pctxt, psNewAlias);
      return OO_FAILED;
   }
   strcpy(psNewAlias->value, alias);
   psNewAlias->next = call->remoteAliases;
   call->remoteAliases = psNewAlias;
   OOTRACEINFO2("Destination for new call is parsed as h323-id %s\n",
                psNewAlias->value);
   return OO_OK;
}
#endif

/* Build a Facility message and tunnel H.245 message through it */
int ooSendAsTunneledMessage(OOH323CallData *call, ASN1OCTET* msgbuf, int h245Len,
                            int h245MsgType, int associatedChan)
{
   DListNode *pNode = NULL;
   Q931Message *pQ931Msg = NULL;
   H225H323_UU_PDU *pH323UUPDU = NULL;
   H225H323_UU_PDU_h245Control *pH245Control = NULL;
   ASN1DynOctStr * elem;
   int ret =0;
   H225Facility_UUIE *facility=NULL;
   OOCTXT *pctxt = &gH323ep.msgctxt;

   OOTRACEDBGA4("Building Facility message for tunneling %s (%s, %s)\n",
                 ooGetMsgTypeText(h245MsgType), call->callType, call->callToken);

   ret = ooCreateQ931Message(&pQ931Msg, Q931FacilityMsg);
   if(ret != OO_OK)
   {
      OOTRACEERR3("ERROR: In allocating memory for facility message "
                  "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }

   q931msg->callReference = call->callReference;

   pQ931Msg->userInfo = (H225H323_UserInformation*)memAlloc(pctxt,
                             sizeof(H225H323_UserInformation));
   if(!pQ931Msg->userInfo)
   {
      OOTRACEERR3("ERROR:Memory - ooSendAsTunneledMessage - userInfo"
                " (%s, %s)\n", call->callType, call->callToken);
      memReset(&gH323ep.msgctxt);
      return OO_FAILED;
   }
   memset (pQ931Msg->userInfo, 0, sizeof(H225H323_UserInformation));
   pQ931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent=1;

   pQ931Msg->userInfo->h323_uu_pdu.h245Tunneling =
      OO_TESTFLAG (call->flags, OO_M_TUNNELING);

   pQ931Msg->userInfo->h323_uu_pdu.h323_message_body.t =
         T_H225H323_UU_PDU_h323_message_body_facility;
  
   facility = (H225Facility_UUIE*)
      memAllocZ (pctxt, sizeof(H225Facility_UUIE));

   if(!facility)
   {
      OOTRACEERR3("ERROR:Memory - ooSendAsTunneledMessage - facility (%s, %s)"
                  "\n", call->callType, call->callToken);
      memReset(&gH323ep.msgctxt);
      return OO_FAILED;
   }

   pQ931Msg->userInfo->h323_uu_pdu.h323_message_body.u.facility = facility;
   /* Populate Facility UUIE */
   facility->protocolIdentifier = gProtocolID; 
   facility->m.callIdentifierPresent = 1;
   facility->callIdentifier.guid.numocts =
      call->callIdentifier.guid.numocts;

   memcpy(facility->callIdentifier.guid.data,
          call->callIdentifier.guid.data,
          call->callIdentifier.guid.numocts);

   facility->reason.t = T_H225FacilityReason_transportedInformation;

   pH323UUPDU = (H225H323_UU_PDU*) &pQ931Msg->userInfo->h323_uu_pdu;
   pH323UUPDU->m.h245TunnelingPresent = TRUE;
   pH323UUPDU->m.h245ControlPresent = TRUE;
   pH323UUPDU->h245Tunneling = TRUE;
   pH245Control = (H225H323_UU_PDU_h245Control*)
                   &pH323UUPDU->h245Control;

   elem = (ASN1DynOctStr*) memAlloc(pctxt,
                                      sizeof(ASN1DynOctStr));
   if(!elem)
   {
      OOTRACEERR3("ERROR:Memory - ooSendAsTunneledMessage - elem "
                  "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   elem->data = msgbuf;
   elem->numocts = h245Len;
   pH245Control->elem = elem;
   pH245Control->n = 1;
   pQ931Msg->tunneledMsgType = h245MsgType;
   pQ931Msg->logicalChannelNo = associatedChan;

  
   ret = ooSendH225Msg(call, pQ931Msg);
   if(ret != OO_OK)
   {
     OOTRACEERR3("Error:Failed to enqueue Facility(tunneling) message to "
                 "outbound queue.(%s, %s)\n", call->callType, call->callToken);
   }

   /* can't do memReset here because if we are sending H.245 message as a
      response to received tunneled h.245 message, we can't reset unless the \
      main received H225 message processing is finished. Rule. No reset when
      tunneling
   */
   memFreePtr(&gH323ep.msgctxt, pQ931Msg);
   return ret;
}

int ooCallEstbTimerExpired(void *data)
{

   ooTimerCallback *cbData = (ooTimerCallback*) data;
   OOH323CallData *call = cbData->call;
   OOTRACEINFO3("Call Establishment timer expired. (%s, %s)\n",
                                            call->callType, call->callToken);
   memFreePtr(call->pctxt, cbData);
   if(call->callState < OO_CALL_CLEAR){
      call->callState = OO_CALL_CLEAR;
      call->callEndReason = OO_REASON_LOCAL_CLEARED;     
   }

   return OO_OK;
}


int ooQ931GetCauseAndReasonCodeFromCallClearReason
   (OOCallClearReason clearReason, enum Q931CauseValues *cause,
    unsigned *reasonCode)
{
   switch(clearReason)
   {
   case OO_REASON_INVALIDMESSAGE:
   case OO_REASON_TRANSPORTFAILURE:
      *reasonCode =  T_H225ReleaseCompleteReason_undefinedReason;
      *cause = Q931ProtocolErrorUnspecified;
      break;
   case OO_REASON_NOBW:
      *reasonCode = T_H225ReleaseCompleteReason_noBandwidth;
      *cause = Q931ErrorInCauseIE;
      break;
   case OO_REASON_GK_NOCALLEDUSER:
      *reasonCode = T_H225ReleaseCompleteReason_calledPartyNotRegistered;
      *cause = Q931SubscriberAbsent;
      break;
   case OO_REASON_GK_NOCALLERUSER:
      *reasonCode = T_H225ReleaseCompleteReason_callerNotRegistered;
      *cause = Q931SubscriberAbsent;     
      break;
   case OO_REASON_GK_UNREACHABLE:
      *reasonCode = T_H225ReleaseCompleteReason_unreachableGatekeeper;
      *cause = Q931TemporaryFailure;
      break;
   case OO_REASON_GK_NORESOURCES:
   case OO_REASON_GK_CLEARED:
      *reasonCode = T_H225ReleaseCompleteReason_gatekeeperResources;
      *cause = Q931Congestion;
      break;
   case OO_REASON_NOCOMMON_CAPABILITIES:
      *reasonCode =  T_H225ReleaseCompleteReason_undefinedReason;
      *cause = Q931IncompatibleDestination;
      break;
   case OO_REASON_LOCAL_FWDED:
   case OO_REASON_REMOTE_FWDED:
      *reasonCode =  T_H225ReleaseCompleteReason_facilityCallDeflection;
      *cause = Q931Redirection;
      break;
   case OO_REASON_REMOTE_CLEARED:
   case OO_REASON_LOCAL_CLEARED:
      *reasonCode = T_H225ReleaseCompleteReason_undefinedReason;
      *cause = Q931NormalCallClearing;
      break;
   case OO_REASON_REMOTE_BUSY:
   case OO_REASON_LOCAL_BUSY:
      *reasonCode =  T_H225ReleaseCompleteReason_inConf;
      *cause = Q931UserBusy;
      break;
   case OO_REASON_REMOTE_NOANSWER:
   case OO_REASON_LOCAL_NOTANSWERED:
      *reasonCode =  T_H225ReleaseCompleteReason_undefinedReason;
      *cause = Q931NoAnswer;
      break;
   case OO_REASON_REMOTE_REJECTED:
   case OO_REASON_LOCAL_REJECTED:
      *reasonCode = T_H225ReleaseCompleteReason_destinationRejection;
      *cause = Q931CallRejected;
      break;
   case OO_REASON_REMOTE_CONGESTED:
   case OO_REASON_LOCAL_CONGESTED:
      *reasonCode =  T_H225ReleaseCompleteReason_noBandwidth;
      *cause = Q931Congestion;
      break;
   case OO_REASON_NOROUTE:
      *reasonCode = T_H225ReleaseCompleteReason_unreachableDestination;
      *cause = Q931NoRouteToDestination;
      break;
   case OO_REASON_NOUSER:
      *reasonCode = T_H225ReleaseCompleteReason_undefinedReason;     
      *cause = Q931SubscriberAbsent;
      break;
   case OO_REASON_UNKNOWN:
   default:
      *reasonCode = T_H225ReleaseCompleteReason_undefinedReason;
      *cause = Q931NormalUnspecified;
   }

   return OO_OK;
}

enum OOCallClearReason ooGetCallClearReasonFromCauseAndReasonCode
   (enum Q931CauseValues cause, unsigned reasonCode)
{
   switch(cause)
   {
      case Q931NormalCallClearing:
        return OO_REASON_REMOTE_CLEARED;

      case Q931UserBusy:
         return OO_REASON_REMOTE_BUSY;

      case Q931NoResponse:
      case Q931NoAnswer:
         return OO_REASON_REMOTE_NOANSWER;

      case Q931CallRejected:
         return OO_REASON_REMOTE_REJECTED;

      case Q931Redirection:
         return OO_REASON_REMOTE_FWDED;

      case Q931NetworkOutOfOrder:
      case Q931TemporaryFailure:
         return OO_REASON_TRANSPORTFAILURE;

      case Q931NoCircuitChannelAvailable:
      case Q931Congestion:
      case Q931RequestedCircuitUnAvailable:
      case Q931ResourcesUnavailable:
         return OO_REASON_REMOTE_CONGESTED;

      case Q931NoRouteToDestination:
      case Q931NoRouteToNetwork:
         return OO_REASON_NOROUTE;
      case Q931NumberChanged:
      case Q931UnallocatedNumber:
      case Q931SubscriberAbsent:
        return OO_REASON_NOUSER;
      case Q931ChannelUnacceptable:
      case Q931DestinationOutOfOrder:
      case Q931InvalidNumberFormat:
      case Q931NormalUnspecified:
      case Q931StatusEnquiryResponse:
      case Q931IncompatibleDestination:
      case Q931ProtocolErrorUnspecified:
      case Q931RecoveryOnTimerExpiry:
      case Q931InvalidCallReference:
      default:
         switch(reasonCode)
         {
            case T_H225ReleaseCompleteReason_noBandwidth:
               return OO_REASON_NOBW;
            case T_H225ReleaseCompleteReason_gatekeeperResources:
               return OO_REASON_GK_NORESOURCES;
            case T_H225ReleaseCompleteReason_unreachableDestination:
               return OO_REASON_NOROUTE;
            case T_H225ReleaseCompleteReason_destinationRejection:
               return OO_REASON_REMOTE_REJECTED;
            case T_H225ReleaseCompleteReason_inConf:
               return OO_REASON_REMOTE_BUSY;
            case T_H225ReleaseCompleteReason_facilityCallDeflection:
               return OO_REASON_REMOTE_FWDED;
            case T_H225ReleaseCompleteReason_calledPartyNotRegistered:
              return OO_REASON_GK_NOCALLEDUSER;
            case T_H225ReleaseCompleteReason_callerNotRegistered:
              return OO_REASON_GK_NOCALLERUSER;
            case T_H225ReleaseCompleteReason_gatewayResources:
              return OO_REASON_GK_NORESOURCES;
            case T_H225ReleaseCompleteReason_unreachableGatekeeper:
               return OO_REASON_GK_UNREACHABLE;
            case T_H225ReleaseCompleteReason_invalidRevision:
            case T_H225ReleaseCompleteReason_noPermission:
            case T_H225ReleaseCompleteReason_badFormatAddress:
            case T_H225ReleaseCompleteReason_adaptiveBusy:
            case T_H225ReleaseCompleteReason_undefinedReason:
            case T_H225ReleaseCompleteReason_securityDenied:
            case T_H225ReleaseCompleteReason_newConnectionNeeded:
            case T_H225ReleaseCompleteReason_nonStandardReason:
            case T_H225ReleaseCompleteReason_replaceWithConferenceInvite:
            case T_H225ReleaseCompleteReason_genericDataReason:
            case T_H225ReleaseCompleteReason_neededFeatureNotSupported:
            case T_H225ReleaseCompleteReason_tunnelledSignallingRejected:
            case T_H225ReleaseCompleteReason_invalidCID:
            case T_H225ReleaseCompleteReason_securityError:
            case T_H225ReleaseCompleteReason_hopCountExceeded:
            case T_H225ReleaseCompleteReason_extElem1:
            default:
               return OO_REASON_UNKNOWN;
         }
   }
   return OO_REASON_UNKNOWN;
}

/**
 This function is used to parse destination string passed to ooH323MakeCall and
 ooH323ForwardCall. If the string contains ip address and port, it is returned
 in the parsedIP buffer and if it contains alias, it is added to aliasList
*/
int ooParseDestination(OOCTXT *pctxt, char *dest, char* parsedIP, unsigned len,
                        ooAliases** aliasList)
{
   int ret=0, iEk=-1, iDon=-1, iTeen=-1, iChaar=-1, iPort = -1, i;
   ooAliases * psNewAlias = NULL;
   char *cAt = NULL, *host=NULL;
   char tmp[256], buf[30];
   char *alias=NULL;
   parsedIP[0] = '\0';

   OOTRACEINFO2("Parsing destination %s\n", dest);
 
   /* Test for an IP address:Note that only supports dotted IPv4.
      IPv6 won't pass the test and so will numeric IP representation*/
  
   sscanf(dest, "%d.%d.%d.%d:%d", &iEk, &iDon, &iTeen, &iChaar, &iPort);
   if((iEk > 0            && iEk <= 255)    &&
      (iDon >= 0          && iDon <= 255)   &&
      (iTeen >=0          && iTeen <= 255)  &&
      (iChaar >= 0        && iChaar <= 255) &&
      (!strchr(dest, ':') || iPort != -1))
   {
      if(!strchr(dest, ':'))
         iPort = 1720; /*Default h.323 port */

      sprintf(buf, "%d.%d.%d.%d:%d", iEk, iDon, iTeen, iChaar, iPort);
      if(strlen(buf)+1>len)
      {
         OOTRACEERR1("Error:Insufficient buffer space for parsed ip - "
                     "ooParseDestination\n");
         return OO_FAILED;
      }
        
      strcpy(parsedIP, buf);
      return OO_OK;
   }

   /* alias@host */
   strncpy(tmp, dest, sizeof(tmp)-1);
   tmp[sizeof(tmp)-1]='\0';
   if(host=strchr(tmp, '@'))
   {
      *host = '\0';
      host++;
      sscanf(host, "%d.%d.%d.%d:%d", &iEk, &iDon, &iTeen, &iChaar, &iPort);
      if((iEk > 0            && iEk <= 255)    &&
         (iDon >= 0          && iDon <= 255)   &&
         (iTeen >=0          && iTeen <= 255)  &&
         (iChaar >= 0        && iChaar <= 255) &&
         (!strchr(host, ':') || iPort != -1))
      {
         if(!strchr(dest, ':'))
            iPort = 1720; /*Default h.323 port */

         sprintf(buf, "%d.%d.%d.%d:%d", iEk, iDon, iTeen, iChaar, iPort);
         if(strlen(buf)+1>len)
         {
            OOTRACEERR1("Error:Insufficient buffer space for parsed ip - "
                        "ooParseDestination\n");
            return OO_FAILED;
         }

         strncpy(parsedIP, buf, len-1);
         parsedIP[len-1]='\0';
         alias = tmp;
      }
   }

   if(!alias)
   {
     alias = dest;
   }
   /* url test */
   if(alias == strstr(alias, "http://"))
   {
      psNewAlias = (ooAliases*) memAlloc(pctxt, sizeof(ooAliases));
      if(!psNewAlias)
      {
         OOTRACEERR1("Error:Memory - ooParseDestination - psNewAlias\n");
         return OO_FAILED;
      }
      psNewAlias->type = T_H225AliasAddress_url_ID;
      psNewAlias->value = (char*) memAlloc(pctxt, strlen(alias)+1);
      if(!psNewAlias->value)
      {
         OOTRACEERR1("Error:Memory - ooParseDestination - "
                     "psNewAlias->value\n");
         memFreePtr(pctxt, psNewAlias);
         return OO_FAILED;
      }
      strcpy(psNewAlias->value, alias);
      psNewAlias->next = *aliasList;
      *aliasList = psNewAlias;
      OOTRACEINFO2("Destination parsed as url %s\n", psNewAlias->value);
      return OO_OK;
   }

   /* E-mail ID test */
   if((cAt = strchr(alias, '@')) && alias != strchr(alias, '@'))
   {
      if(strchr(cAt, '.'))
      {
         psNewAlias = (ooAliases*) memAlloc(pctxt, sizeof(ooAliases));
         if(!psNewAlias)
         {
            OOTRACEERR1("Error:Memory - ooParseDestination - psNewAlias\n");
            return OO_FAILED;
         }
         psNewAlias->type = T_H225AliasAddress_email_ID;
         psNewAlias->value = (char*) memAlloc(pctxt, strlen(alias)+1);
         if(!psNewAlias->value)
         {
            OOTRACEERR1("Error:Memory - ooParseDestination - "
                        "psNewAlias->value\n");
            memFreePtr(pctxt, psNewAlias);
            return OO_FAILED;
         }
         strcpy(psNewAlias->value, alias);
         psNewAlias->next = *aliasList;
         *aliasList = psNewAlias;
         OOTRACEINFO2("Destination is parsed as email %s\n",psNewAlias->value);
         return OO_OK;
      }
   }

 
   /* e-164 */
   /* strspn(dest, "1234567890*#") == strlen(dest)*/
   /* Dialed digits test*/
   for(i=0; i<(int)strlen(alias); i++)
   {
      if(!isdigit(alias[i]))
         break;
   }
   if(i == (int)strlen(alias))
   {
      psNewAlias = (ooAliases*) memAlloc(pctxt, sizeof(ooAliases));
      if(!psNewAlias)
      {
         OOTRACEERR1("Error:Memory - ooParseDestination - psNewAlias\n");
         return OO_FAILED;
      }
      /*      memset(psNewAlias, 0, sizeof(ooAliases));*/
      psNewAlias->type = T_H225AliasAddress_dialedDigits;
      psNewAlias->value = (char*) memAlloc(pctxt, strlen(alias)+1);
      if(!psNewAlias->value)
      {
         OOTRACEERR1("Error:Memroy - ooParseDestination - psNewAlias->value\n");
         memFreePtr(pctxt, psNewAlias);
         return OO_FAILED;
      }
      strcpy(psNewAlias->value, alias);
      psNewAlias->next = *aliasList;
      *aliasList = psNewAlias;
      OOTRACEINFO2("Destination is parsed as dialed digits %s\n",
                   psNewAlias->value);
      return OO_OK;
   }
   /* Evrything else is an h323-id for now */
   psNewAlias = (ooAliases*) memAlloc(pctxt, sizeof(ooAliases));
   if(!psNewAlias)
   {
      OOTRACEERR1("Error:Memory - ooParseDestination - psNewAlias\n");
      return OO_FAILED;
   }
   psNewAlias->type = T_H225AliasAddress_h323_ID;
   psNewAlias->value = (char*) memAlloc(pctxt, strlen(alias)+1);
   if(!psNewAlias->value)
   {
      OOTRACEERR1("Error:Memory - ooParseDestination - psNewAlias->value\n");
      memFreePtr(pctxt, psNewAlias);
      return OO_FAILED;
   }
   strcpy(psNewAlias->value, alias);
   psNewAlias->next = *aliasList;
   *aliasList = psNewAlias;
   OOTRACEINFO2("Destination for new call is parsed as h323-id %s \n",
                psNewAlias->value);
   return OO_OK;
}
