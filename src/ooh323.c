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
#include "ooq931.h"
#include "ootrace.h"
#include "oochannels.h"
#include "ooh245.h"
#include "ooCalls.h"
#include "printHandler.h"
#include "ooh323.h"
#include "ooras.h"

/** Global endpoint structure */
extern ooEndPoint gH323ep;

int ooOnReceivedSetup(ooCallData *call, Q931Message *q931Msg)
{
   H225Setup_UUIE *setup=NULL;
   int i=0, ret=0;
   H245OpenLogicalChannel* olc;
   ASN1OCTET msgbuf[MAXMSGLEN];
   H225TransportAddress_ipAddress_ip *ip = NULL;
   DListNode* pNode=NULL;
   H225AliasAddress *pAliasAddress=NULL;
   Q931InformationElement* pDisplayIE=NULL;

   call->callReference = q931Msg->callReference;

   if(!q931Msg->userInfo)
   {
      OOTRACEERR3("ERROR:No User-User IE in received SETUP message (%s, %s)\n",
                  call->callType, call->callToken);
      return OO_FAILED;
   }
   setup = q931Msg->userInfo->h323_uu_pdu.h323_message_body.u.setup;
   if(!setup)
   {
      OOTRACEERR3("Error: Setup UUIE not found in received setup message - %s "
                  "%s\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   memcpy(call->callIdentifier.guid.data, setup->callIdentifier.guid.data,
          setup->callIdentifier.guid.numocts);
   call->callIdentifier.guid.numocts = setup->callIdentifier.guid.numocts;
  
   memcpy(call->confIdentifier.data, setup->conferenceID.data,
          setup->conferenceID.numocts);
   call->confIdentifier.numocts = setup->conferenceID.numocts;

   /* check for display ie */
   pDisplayIE = ooQ931GetIE(q931Msg, Q931DisplayIE);
   if(pDisplayIE)
   {
      call->remoteDisplayName = (ASN1OCTET*) ASN1MALLOC(call->pctxt,
                                 pDisplayIE->length*sizeof(ASN1OCTET)+1);
      memset(call->remoteDisplayName, 0,
                                       pDisplayIE->length*sizeof(ASN1OCTET)+1);
      memcpy(call->remoteDisplayName, pDisplayIE->data, pDisplayIE->length);
   }
   /*Extract Remote Aliases, if present*/
   if(setup->m.sourceAddressPresent)
   {
      if(setup->sourceAddress.count>0)
      {
         ooRetrieveAliases(call, &setup->sourceAddress);
      }
   }
        
   /* Check for tunneling */
   if(q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent)
   {
      /* Tunneling enabled only when tunneling is set to true and h245
         address is abscent. In the presence of H.245 address in received
         SETUP message, tunneling is disabled, irrespective of tunneling
         flag in the setup message*/
      if(q931Msg->userInfo->h323_uu_pdu.h245Tunneling &&
         !setup->m.h245AddressPresent)
      {
         if(gH323ep.h245Tunneling)
         {
            call->isTunnelingActive = 1;
            OOTRACEINFO3("Call has tunneling active (%s,%s)\n", call->callType,
                          call->callToken);
         }
        else
           OOTRACEINFO3("ERROR:Remote endpoint wants to use h245Tunneling, "
                        "local endpoint has it disabled (%s,%s)\n",
                        call->callType, call->callToken);
      }else {
         if(gH323ep.h245Tunneling)
         {
            OOTRACEINFO3("Tunneling disabled by remote endpoint. (%s, %s)\n",
                         call->callType, call->callToken);
         }
         call->isTunnelingActive = FALSE;
      }
   }else {
      if(gH323ep.h245Tunneling)
      {
         OOTRACEINFO3("Tunneling disabled by remote endpoint. (%s, %s)\n",
                       call->callType, call->callToken);
      }
      call->isTunnelingActive = FALSE;
   }
  
   /* Extract Remote IP address */
   if(!setup->m.sourceCallSignalAddressPresent)
   {
      OOTRACEERR3("ERROR:Missing source call signal address in received setup "
                  "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }

   if(setup->sourceCallSignalAddress.t != T_H225TransportAddress_ipAddress)
   {
      OOTRACEERR3("ERROR: Source call signalling address type not ip (%s, %s)"
                  "\n", call->callType, call->callToken);
      return OO_FAILED;
   }

   ip = &setup->sourceCallSignalAddress.u.ipAddress->ip;
   sprintf(call->remoteIP, "%d.%d.%d.%d", ip->data[0], ip->data[1],
                                          ip->data[2], ip->data[3]);
  
   /* check for fast start */
  
   if(setup->m.fastStartPresent)
   {
      if(!gH323ep.fastStart)
      {
         OOTRACEINFO3("Local endpoint does not support fastStart. Ignoring "
                      "fastStart. (%s, %s)\n", call->callType, call->callToken);
         call->isFastStartActive = FALSE;
      }
      else if(setup->fastStart.n == 0)
      {
         OOTRACEINFO3("Empty faststart element received. Ignoring fast start. "
                      "(%s, %s)\n", call->callType, call->callToken);
         call->isFastStartActive = FALSE;
      }
      else{
         call->isFastStartActive = TRUE;
         OOTRACEINFO3("FastStart enabled for call(%s, %s)\n", call->callType,
                       call->callToken);
      }
   }

   if(call->isFastStartActive)
   {
      /* For printing the decoded message to log, initialize handler. */
      initializePrintHandler(&printHandler, "FastStart Elements");

      /* Add print handler to list */
      rtAddEventHandler (call->pctxt, &printHandler);

      for(i=0; i<(int)setup->fastStart.n; i++)
      {
         olc = NULL;
         memset(msgbuf, 0, sizeof(msgbuf));
         olc = (H245OpenLogicalChannel*)ASN1MALLOC(call->pctxt,
                                              sizeof(H245OpenLogicalChannel));
         if(!olc)
         {
            OOTRACEERR3("ERROR:Allocating memory for OLC, in received "
                        "faststart SETUP message (%s, %s)\n", call->callType,
                        call->callToken);
            /*Mark call for clearing */
            if(call->callState < OO_CALL_CLEAR)
            {
               call->callEndReason = OO_HOST_CLEARED;
               call->callState = OO_CALL_CLEAR;
            }
            return OO_FAILED;
         }
         memset(olc, 0, sizeof(H245OpenLogicalChannel));
         memcpy(msgbuf, setup->fastStart.elem[i].data,
                setup->fastStart.elem[i].numocts);

         setPERBuffer(call->pctxt, msgbuf,
                      setup->fastStart.elem[i].numocts, 1);
         ret = asn1PD_H245OpenLogicalChannel(call->pctxt, olc);
         if(ret != ASN_OK)
         {
            OOTRACEERR3("ERROR:Failed to decode fast start olc element "
                        "(%s, %s)\n", call->callType, call->callToken);
            /* Mark call for clearing */
            if(call->callState < OO_CALL_CLEAR)
            {
               call->callEndReason = OO_HOST_CLEARED;
               call->callState = OO_CALL_CLEAR;
            }
            return OO_FAILED;
         }
         /* For now, just add decoded fast start elemts to list. This list
            will be processed at the time of sending CONNECT message. */
         dListAppend(call->pctxt, &call->remoteFastStartOLCs, olc);
      }
      finishPrint();
      rtRemoveEventHandler(call->pctxt, &printHandler);
   }

   return OO_OK;
}

int ooOnReceivedSignalConnect(ooCallData* call, Q931Message *q931Msg)
{
   int ret, i;
   H225TransportAddress *h245Address;
   H225Connect_UUIE *connect;
   H245OpenLogicalChannel* olc;
   ASN1OCTET msgbuf[MAXMSGLEN];
   ooLogicalChannel * pChannel = NULL;
   H245H2250LogicalChannelParameters * h2250lcp = NULL; 
   H245UnicastAddress *unicastAddress;

   if(!q931Msg->userInfo)
   {
      OOTRACEERR3("Error: UUIE not found in received H.225 Connect message"
                  " (%s, %s)\n", call->callType, call->callToken);
      /* Mark call for clearing */
      if(call->callState < OO_CALL_CLEAR)
      {
         call->callEndReason = OO_HOST_CLEARED;
         call->callState = OO_CALL_CLEAR;
      }
      return OO_FAILED;
   }
   /* Retrieve the connect message from the user-user IE */
   connect = q931Msg->userInfo->h323_uu_pdu.h323_message_body.u.connect;
   if(connect == NULL)
   {
      OOTRACEERR3("Error: Received Connect message does not have Connect UUIE"
                  " (%s, %s)\n", call->callType, call->callToken);
      /* Mark call for clearing */
      if(call->callState < OO_CALL_CLEAR)
      {
         call->callEndReason = OO_HOST_CLEARED;
         call->callState = OO_CALL_CLEAR;
      }
      return OO_FAILED;
   }

   /*Handle fast-start */
   if(call->isFastStartActive)
   {
      if(!connect->m.fastStartPresent)
      {
         OOTRACEINFO3("Remote endpoint has rejected fastStart. (%s, %s)\n",
                      call->callType, call->callToken);
         /* Clear all channels we might have created */
         ooClearAllLogicalChannels(call);
         call->isFastStartActive = FALSE;
      }
   }
   /*call->isFastStartActive = connect->m.fastStartPresent;*/
   if(connect->m.fastStartPresent)
   {
      /* For printing the decoded message to log, initialize handler. */
      initializePrintHandler(&printHandler, "FastStart Elements");

      /* Add print handler to list */
      rtAddEventHandler (call->pctxt, &printHandler);

      for(i=0; i<(int)connect->fastStart.n; i++)
      {
         olc = NULL;
         memset(msgbuf, 0, sizeof(msgbuf));
         olc = (H245OpenLogicalChannel*)ASN1MALLOC(call->pctxt,
                                              sizeof(H245OpenLogicalChannel));
         if(!olc)
         {
            OOTRACEERR3("ERROR:Allocating memory for OLC, in received "
                        "faststart CONNECT message (%s, %s)\n",
                        call->callType, call->callToken);
            /*Mark call for clearing */
            if(call->callState < OO_CALL_CLEAR)
            {
               call->callEndReason = OO_HOST_CLEARED;
               call->callState = OO_CALL_CLEAR;
            }
            return OO_FAILED;
         }
         memset(olc, 0, sizeof(H245OpenLogicalChannel));
         memcpy(msgbuf, connect->fastStart.elem[i].data,
                                           connect->fastStart.elem[i].numocts);
         setPERBuffer(call->pctxt, msgbuf,
                      connect->fastStart.elem[i].numocts, 1);
         ret = asn1PD_H245OpenLogicalChannel(call->pctxt, olc);
         if(ret != ASN_OK)
         {
            OOTRACEERR3("ERROR:Failed to decode fast start olc element "
                        "(%s, %s)\n", call->callType, call->callToken);
            /* Mark call for clearing */
            if(call->callState < OO_CALL_CLEAR)
            {
               call->callEndReason = OO_HOST_CLEARED;
               call->callState = OO_CALL_CLEAR;
            }
            return OO_FAILED;
         }

         dListAppend(call->pctxt, &call->remoteFastStartOLCs, olc);

         pChannel = ooFindLogicalChannelByOLC(call, olc);
         if(!pChannel)
         {
            OOTRACEERR4("ERROR: Logical Channel %d not found. (%s, %s)\n",
                         olc->forwardLogicalChannelNumber, call->callType,
                         call->callToken);
            return OO_FAILED;
         }
         if(pChannel->channelNo != olc->forwardLogicalChannelNumber)
         {
            OOTRACEINFO5("Remote endpoint changed forwardLogicalChannelNumber"
                         "from %d to %d (%s, %s)\n", pChannel->channelNo,
                          olc->forwardLogicalChannelNumber, call->callType,
                          call->callToken);
            pChannel->channelNo = olc->forwardLogicalChannelNumber;
         }
         if(!strcmp(pChannel->dir, "transmit"))
         {
            if(olc->forwardLogicalChannelParameters.multiplexParameters.t !=
               T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters)
            {
               OOTRACEERR4("ERROR:Unknown multiplex parameter type for channel"
                           " %d (%s, %s)\n", olc->forwardLogicalChannelNumber,
                           call->callType, call->callToken);
               continue;
            }
           
            /* Extract the remote media endpoint address */
            h2250lcp = olc->forwardLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;
            if(!h2250lcp)
            {
               OOTRACEERR3("ERROR:Invalid OLC received in fast start. No "
                           "forward Logical Channel Parameters found. (%s, %s)"
                           "\n", call->callType, call->callToken);
               return OO_FAILED;
            }
            if(!h2250lcp->m.mediaChannelPresent)
            {
               OOTRACEERR3("ERROR:Invalid OLC received in fast start. No "
                           "reverse media channel information found. (%s, %s)"
                           "\n", call->callType, call->callToken);
               return OO_FAILED;
            }
            if(h2250lcp->mediaChannel.t !=
                                         T_H245TransportAddress_unicastAddress)
            {
               OOTRACEERR3("ERROR:Unsupported media channel address type "
                           "(%s, %s)\n", call->callType, call->callToken);
               return OO_FAILED;
            }
     
            unicastAddress = h2250lcp->mediaChannel.u.unicastAddress;
            if(unicastAddress->t != T_H245UnicastAddress_iPAddress)
            {
               OOTRACEERR3("ERROR:media channel address type is not IP"
                           "(%s, %s)\n", call->callType, call->callToken);
               return OO_FAILED;
            }
            pChannel->remoteRtpPort = unicastAddress->u.iPAddress->tsapIdentifier; 
            if(!pChannel->chanCap->startTransmitChannel)
            {
               OOTRACEERR3("ERROR:No callback registered to start transmit "
                           "channel (%s, %s)\n",call->callType, call->callToken);
               return OO_FAILED;
            }
            pChannel->chanCap->startTransmitChannel(call, pChannel);
         }
         /* Mark the current channel as established and close all other
            logical channels with same session id and in same direction.
         */
         ooOnLogicalChannelEstablished(call, pChannel);
      }
      finishPrint();
      rtRemoveEventHandler(call->pctxt, &printHandler);
   }

   /* Retrieve the H.245 control channel address from the connect msg */
   if(connect->m.h245AddressPresent)
   {
      if(call->isTunnelingActive)
      {
         call->isTunnelingActive = FALSE;
         OOTRACEINFO3("Tunneling is disabled for call as H245 address is "
                      "provided in connect message (%s, %s)\n",
                      call->callType, call->callToken);
      }
      h245Address = &(connect->h245Address);
      if(h245Address->t != T_H225TransportAddress_ipAddress)
      {
         OOTRACEERR3("Error: Unknown H245 address type in received Connect "
                     "message (%s, %s)", call->callType, call->callToken);
         /* Mark call for clearing */
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_HOST_CLEARED;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }
      sprintf(call->remoteIP, "%d.%d.%d.%d",
              h245Address->u.ipAddress->ip.data[0],
              h245Address->u.ipAddress->ip.data[1],
              h245Address->u.ipAddress->ip.data[2],
              h245Address->u.ipAddress->ip.data[3]);
      call->remoteH245Port = h245Address->u.ipAddress->port;
      /* Create an H.245 connection.
      */
      if(ooCreateH245Connection(call)== OO_FAILED)
      {
         OOTRACEERR3("Error: H.245 channel creation failed (%s, %s)\n",
                     call->callType, call->callToken);
         /* Mark call for clearing */
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_HOST_CLEARED;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }
   }

   if(q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent)
   {
      if(!q931Msg->userInfo->h323_uu_pdu.h245Tunneling)
      {
         if(call->isTunnelingActive)
         {
            call->isTunnelingActive = FALSE;
            OOTRACEINFO3("Tunneling is disabled by remote endpoint.(%s, %s)\n",
                          call->callType, call->callToken);
         }
      }
   }
   if(call->isTunnelingActive)
   {
      ret = ooHandleTunneledH245Messages(call,
                                             &q931Msg->userInfo->h323_uu_pdu);
      /* It is good to send TCS even in case of faststart */
     /*     if(!call->isFastStartActive)
            /* {

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
        /*     }*/
   }
   return OO_OK; 
}

int ooHandleH2250Message(ooCallData *call, Q931Message *q931Msg)
{
   int ret=OO_OK;
   int type = q931Msg->messageType;
   switch(type)
   {
      case Q931SetupMsg: /* Setup message is received */
         OOTRACEINFO3("Received SETUP message (%s, %s)\n", call->callType,
                       call->callToken);
         ooOnReceivedSetup(call, q931Msg);
         /* Free up the mem used by the received message, as it's processing
            is done.
         */
         ooFreeQ931Message(q931Msg);
        
         ooSendCallProceeding(call);/* Send call proceeding message*/
#ifdef __USING_RAS
         if(RasNoGatekeeper != ooRasGetGatekeeperMode())
         {
            if(ooRasIsRegistered()) 
            {
               ret = ooRasSendAdmissionRequest(call, RasDirect,
                                       gH323ep.aliases, call->remoteAliases);
               call->callState = OO_CALL_WAITING_ADMISSION;
            }else{
               /* TODO: Should send Release complete with reject reason */
               OOTRACEERR1("Error:Ignoring incoming call as not yet"
                           "registered with Gk\n");
            }
         }
         else
            ret = ooH323CallAdmitted (call);
#else
         ret = ooH323CallAdmitted (call);
#endif
         break;
      case Q931CallProceedingMsg:/* Call proceeding message is received */
         OOTRACEINFO3("H.225 Call Proceeding message received (%s, %s)\n",
                      call->callType, call->callToken);
         ooFreeQ931Message(q931Msg);
         break;
      case Q931AlertingMsg:/* Alerting message received */
         OOTRACEINFO3("H.225 Alerting message received (%s, %s)\n",
                      call->callType, call->callToken);
         if(gH323ep.onAlerting)
            gH323ep.onAlerting(call);
         ooFreeQ931Message(q931Msg);
         break;
      case Q931ConnectMsg:/* Connect message received */
         OOTRACEINFO3("H.225 Connect message received (%s, %s)\n",
                      call->callType, call->callToken);        
         ret = ooOnReceivedSignalConnect(call, q931Msg);
         if(ret != OO_OK)
            OOTRACEERR3("Error:Invalid Connect message received. (%s, %s)\n",
                        call->callType, call->callToken);
         else{
            if(gH323ep.onCallEstablished)
               gH323ep.onCallEstablished(call);
         }
         ooFreeQ931Message(q931Msg);
         break;
      case Q931InformationMsg:
         OOTRACEINFO3("H.225 Information msg received (%s, %s)\n",
                       call->callType, call->callToken);
         ooFreeQ931Message(q931Msg);
         break;
      case Q931ReleaseCompleteMsg:/* Release complete message received */
         OOTRACEINFO3("H.225 Release Complete message received (%s, %s)\n",
                      call->callType, call->callToken);
         ooCloseH225Connection(call);
         if(call->callState < OO_CALL_CLEARED)
         {
            if(call->callState < OO_CALL_CLEAR)
               call->callEndReason = OO_REMOTE_CLEARED;
            call->callState = OO_CALL_CLEARED;
         }
         if(gH323ep.onCallCleared)
            gH323ep.onCallCleared(call);        
#ifdef __USING_RAS
         ooRasSendDisengageRequest(call);
#endif
         ooFreeQ931Message(q931Msg);
         break;
      case Q931FacilityMsg:
         OOTRACEINFO3("H.225 Facility message Received (%s, %s)\n",
                       call->callType, call->callToken);
         ooOnReceivedFacility(call, q931Msg);
         ooFreeQ931Message(q931Msg);
         break;
      case Q931ProgressMsg:
         OOTRACEINFO3("H.225 Progress message received (%s, %s)\n",
                       call->callType, call->callToken);
         ooFreeQ931Message(q931Msg);
         break;
      case Q931StatusMsg:
         OOTRACEINFO3("H.225 Status message received (%s, %s)\n",
                       call->callType, call->callToken);
         ooFreeQ931Message(q931Msg);
         break;
      case Q931StatusEnquiryMsg:
         OOTRACEINFO3("H.225 Status Inquiry message Received (%s, %s)\n",
                       call->callType, call->callToken);
         ooFreeQ931Message(q931Msg);
         break;
      case Q931SetupAckMsg:
         OOTRACEINFO3("H.225 Setup Ack message received (%s, %s)\n",
                       call->callType, call->callToken);
         ooFreeQ931Message(q931Msg);
         break;
      case Q931NotifyMsg:
         OOTRACEINFO3("H.225 Notify message Received (%s, %s)\n",
                       call->callType, call->callToken);
         ooFreeQ931Message(q931Msg);
         break;
      default:
         OOTRACEWARN3("Invalid H.225 message type received (%s, %s)\n",
                      call->callType, call->callToken);
         ooFreeQ931Message(q931Msg);
   }
   return ret;
}

int ooOnReceivedFacility(ooCallData *call, Q931Message * pQ931Msg)
{
   H225H323_UU_PDU * pH323UUPdu = NULL;
   H225Facility_UUIE * facility = NULL;
   int i=0, ret;
  
   /* Get Reference to H323_UU_PDU */
   if(!pQ931Msg->userInfo)
   {
      OOTRACEERR3("Error: UserInfo not found in received H.225 Facility message"
                  " (%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   pH323UUPdu = &pQ931Msg->userInfo->h323_uu_pdu;
   if(!pH323UUPdu)
   {
      OOTRACEERR1("ERROR: H225H323_UU_PDU absent in incoming facility "
                  "message\n");
      return OO_FAILED;
   }
   facility = pH323UUPdu->h323_message_body.u.facility;
   if(facility)
   {
      /* Depending on the reason of facility message handle the message */
      if(facility->reason.t == T_H225FacilityReason_transportedInformation)
      {
         if(call->isTunnelingActive)
            ooHandleTunneledH245Messages(call, pH323UUPdu);
         else
         {
            OOTRACEERR3("ERROR:Tunneled H.245 message received in facility. "
                        "Tunneling is disabled at local for this call (%s, %s)\n",
                        call->callType, call->callToken);
            return OO_FAILED;
         }
      }
      else if(facility->reason.t == T_H225FacilityReason_startH245)
      {
         OOTRACEINFO3("Remote wants to start a separate H.245 Channel (%s, %s)\n",
                      call->callType, call->callToken);
         /*start H.245 channel*/
         ret = ooHandleStartH245FacilityMessage(call, facility);
         if(ret != OO_OK)
         {
            OOTRACEERR3("ERROR: Handling startH245 facility message (%s, %s) \n",
                         call->callType, call->callToken);
            return ret;
         }
      }
      else{
         OOTRACEINFO3("Unhandled Facility reason type received (%s, %s)\n",
                       call->callType, call->callToken);
      }
   }
   else{ /* Empty facility message Check for tunneling */
      ooHandleTunneledH245Messages(call, pH323UUPdu);
   }
  
   return OO_OK;
}

int ooHandleStartH245FacilityMessage(ooCallData *call, H225Facility_UUIE *facility)
{
   H225TransportAddress_ipAddress *ipAddress = NULL;
   int ret;
  
   /* Extract H245 address */
   if(!facility->m.h245AddressPresent)
   {
      OOTRACEERR3("ERROR: startH245 facility message received with no h245 "
                  "address (%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   if(facility->h245Address.t != T_H225TransportAddress_ipAddress)
   {
      OOTRACEERR3("ERROR:Unknown H245 address type in received startH245 "
               "facility message (%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   ipAddress = facility->h245Address.u.ipAddress;
   if(!ipAddress)
   {
      OOTRACEERR3("ERROR:Invalid startH245 facility message. No H245 ip "
                  "address found. (%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
  
   sprintf(call->remoteIP, "%d.%d.%d.%d", ipAddress->ip.data[0],
                                          ipAddress->ip.data[1],
                                          ipAddress->ip.data[2],
                                          ipAddress->ip.data[3]);
   call->remoteH245Port = ipAddress->port;
   /* disable tunneling for this call */
   call->isTunnelingActive = 0;
   /*Establish an H.245 connection */
   ret = ooCreateH245Connection(call);
   if(ret != OO_OK)
   {
      OOTRACEERR3("ERROR: Failed to establish an H.245 connection with remote"
                  " endpoint (%s, %s)\n", call->callType, call->callToken);
      return ret;
   }
   return OO_OK;
}

int ooHandleTunneledH245Messages(ooCallData *call, H225H323_UU_PDU * pH323UUPdu)
{
   H245Message *pmsg;
   OOCTXT *pctxt = &gH323ep.msgctxt;
   int ret=0,i=0;
  
   OOTRACEDBGC3("Checking for tunneled H.245 messages (%s, %s)\n",
                 call->callType, call->callToken);

   /* Check whether there are tunneled messages */ 
   if(pH323UUPdu->m.h245TunnelingPresent)
   {
      if(pH323UUPdu->h245Tunneling)
      {
         for(i=0; i< (int)pH323UUPdu->h245Control.n; i++)
         {
            OOTRACEDBGC3("Retrieving next tunneled H.245 message. (%s, %s)\n",
                          call->callType, call->callToken);
            pmsg = (H245Message*)ASN1MALLOC(pctxt, sizeof(H245Message));

            setPERBuffer(pctxt,
                         (ASN1OCTET*)pH323UUPdu->h245Control.elem[i].data,
                         pH323UUPdu->h245Control.elem[i].numocts, 1); 

            initializePrintHandler(&printHandler, "Tunneled H.245 Message");

            /* Add event handler to list */
            rtAddEventHandler (pctxt, &printHandler);
            ret = asn1PD_H245MultimediaSystemControlMessage(pctxt,
                                                            &(pmsg->h245Msg));
            if(ret != ASN_OK)
            {
               OOTRACEERR3("Error decoding H245 message (%s, %s)\n",
                            call->callType, call->callToken);
               ooFreeH245Message(call,pmsg);
               return OO_FAILED;
            }
            finishPrint();
            rtRemoveEventHandler(pctxt, &printHandler);
            ooHandleH245Message(call, pmsg);
            ASN1MEMFREEPTR(pctxt, pmsg);
            pmsg = NULL;
         }/* End of For loop */
      }/* End of if(h245Tunneling) */
   }
   return OO_OK;
}

int ooRetrieveAliases(ooCallData *call, H225_SeqOfH225AliasAddress *pAddresses)
{
   int i=0,j=0,k=0;
   DListNode* pNode=NULL;
   H225AliasAddress *pAliasAddress=NULL;
   ooAliases *newAlias=NULL;
   H225TransportAddress *pTransportAddrss=NULL;

   if(!pAddresses)
   {
      OOTRACEWARN3("Warn:No Aliases present (%s, %s)\n", call->callType,
                    call->callToken);
      return OO_OK;
   }
   /* check for aliases */
   if(pAddresses->count>0)
   {
      for(i=0; i<(int)pAddresses->count; i++)
      {
         pNode = dListFindByIndex (pAddresses, i);
         if(pNode)
         {
            pAliasAddress = (H225AliasAddress*)pNode->data;
            if(pAliasAddress)
            {
               newAlias = (ooAliases*)ASN1MALLOC(call->pctxt,
                                                           sizeof(ooAliases));
               if(!newAlias)
               {
                  OOTRACEERR3("ERROR:Failed to allocate memory for alias "
                              "(%s, %s)\n", call->callType, call->callToken);
                  return OO_FAILED;
               }
               memset(newAlias, 0, sizeof(ooAliases));
               switch(pAliasAddress->t)
               {
               case T_H225AliasAddress_dialedDigits:
                  newAlias->type = T_H225AliasAddress_dialedDigits;
                  newAlias->value = (char*) ASN1MALLOC(call->pctxt,
                         strlen(pAliasAddress->u.dialedDigits)*sizeof(char)+1);
                  memset(newAlias->value, 0,
                         strlen(pAliasAddress->u.dialedDigits)*sizeof(char)+1);
                  memcpy(newAlias->value, pAliasAddress->u.dialedDigits,
                           strlen(pAliasAddress->u.dialedDigits)*sizeof(char));
                  break;
               case T_H225AliasAddress_h323_ID:
                  newAlias->type = T_H225AliasAddress_h323_ID;
                  newAlias->value = (char*)ASN1MALLOC(call->pctxt,
                           (pAliasAddress->u.h323_ID.nchars+1)*sizeof(char)+1);
                  memset(newAlias->value, 0,
                           (pAliasAddress->u.h323_ID.nchars+1)*sizeof(char)+1);
                  for(j=0, k=0; j<(int)pAliasAddress->u.h323_ID.nchars; j++)
                  {
                     if(pAliasAddress->u.h323_ID.data[j] < 256)
                     {
                        newAlias->value[k++] = (char) pAliasAddress->u.h323_ID.data[j];
                     }
                  }
                  newAlias->value[k] = '\0';
                  break;  
               case T_H225AliasAddress_url_ID:
                  newAlias->type = T_H225AliasAddress_url_ID;
                  newAlias->value = (char*)ASN1MALLOC(call->pctxt,
                               strlen(pAliasAddress->u.url_ID)*sizeof(char)+1);
                  memset(newAlias->value, 0,
                               strlen(pAliasAddress->u.url_ID)*sizeof(char)+1);
                  memcpy(newAlias->value, pAliasAddress->u.url_ID,
                                 strlen(pAliasAddress->u.url_ID)*sizeof(char));
                  break;
               case T_H225AliasAddress_transportID:
                  newAlias->type = T_H225AliasAddress_transportID;
                  pTransportAddrss = pAliasAddress->u.transportID;
                  if(pTransportAddrss->t != T_H225TransportAddress_ipAddress)
                  {
                     OOTRACEERR3("Error:Alias transportID not an IP address"
                                "(%s, %s)\n", call->callType, call->callToken);
                     ASN1MEMFREEPTR(call->pctxt, newAlias);
                     break;
                  }
                  /* hopefully ip:port value can't exceed more than 30
                     characters */
                  newAlias->value = (char*)ASN1MALLOC(call->pctxt,
                                                             30*sizeof(char));
                  memset(newAlias->value, 0, 30*sizeof(char));
                  sprintf(newAlias->value, "%d.%d.%d.%d:%d",
                                     pTransportAddrss->u.ipAddress->ip.data[0],
                                     pTransportAddrss->u.ipAddress->ip.data[1],
                                     pTransportAddrss->u.ipAddress->ip.data[2],
                                     pTransportAddrss->u.ipAddress->ip.data[3],
                                     pTransportAddrss->u.ipAddress->port);
                  break;
               case T_H225AliasAddress_email_ID:
                  newAlias->type = T_H225AliasAddress_email_ID;
                  newAlias->value = (char*)ASN1MALLOC(call->pctxt,
                             strlen(pAliasAddress->u.email_ID)*sizeof(char)+1);
                  memset(newAlias->value, 0,
                             strlen(pAliasAddress->u.email_ID)*sizeof(char)+1);
                  memcpy(newAlias->value, pAliasAddress->u.email_ID,
                               strlen(pAliasAddress->u.email_ID)*sizeof(char));
                  break;
               default:
                  OOTRACEERR3("Error:Unhandled Alias type (%s, %s)\n",
                               call->callType, call->callToken);
                  ASN1MEMFREEPTR(call->pctxt, newAlias);
               }
               newAlias->next = call->remoteAliases;
               call->remoteAliases = newAlias;
               newAlias = NULL;
            }/* endof: if(pAliasAddress) */
            pAliasAddress = NULL;
         }/* endof: if(pNode) */
         pNode = NULL;
      }/* endof: for */
   }
   return OO_OK;
}
