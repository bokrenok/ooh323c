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

#include "ooports.h"
#include "oochannels.h"
#include "ootrace.h"
#include "ooq931.h"
#include "ooh245.h"
#include "ooh323.h"
#include "ooCalls.h"
#include "printHandler.h"
#include "ooras.h"
#include "stdio.h"
#include "ooTimer.h"

/** Global endpoint structure */
extern ooEndPoint gH323ep;

extern DList g_TimerList;


int ooCreateH245Listener(ooCallData *call)
{
   int ret=0;
   OOSOCKET channelSocket=0;
   OOTRACEINFO1("Creating H245 listener\n");
   if((ret=ooSocketCreate (&channelSocket))!=ASN_OK)
   {
      OOTRACEERR3("ERROR: Failed to create socket for H245 listener "
                  "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   ret = ooBindPort(&gH323ep, OOTCP, channelSocket);
   if(ret == OO_FAILED)
   {
      OOTRACEERR3("Error:Unable to bind to a TCP port - H245 listener creation"
                  " (%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   call->h245listenport = (int*) ASN1MALLOC(call->pctxt, sizeof(int));
   *(call->h245listenport) = ret;
   call->h245listener = (OOSOCKET*)ASN1MALLOC(call->pctxt, sizeof(OOSOCKET));
   *(call->h245listener) = channelSocket;
   ret = ooSocketListen(*(call->h245listener), 5);
   if(ret != ASN_OK)
   {
      OOTRACEERR3("Error:Unable to listen on H.245 socket (%s, %s)\n",
                   call->callType, call->callToken);
      return OO_FAILED;
   }
  
   OOTRACEINFO4("H245 listener creation - successful(port %d) (%s, %s)\n",
                *(call->h245listenport),call->callType, call->callToken);
   return OO_OK;
}

int ooCreateH245Connection(ooCallData *call)
{
   int ret=0;
   OOSOCKET channelSocket=0;
   OOTRACEINFO1("Creating H245 Connection\n");
   if((ret=ooSocketCreate (&channelSocket))!=ASN_OK)
   {
      OOTRACEERR3("ERROR:Failed to create socket for H245 connection "
                  "(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   else
   {
      if (0 == call->pH245Channel) {
         call->pH245Channel =
            (OOH323Channel*) memAllocZ (call->pctxt, sizeof(OOH323Channel));
      }

      /*
         bind socket to a port before connecting. Thus avoiding
         implicit bind done by a connect call.
      */
      ret = ooBindPort(&gH323ep, OOTCP, channelSocket);
      if(ret == OO_FAILED)
      {
         OOTRACEERR3("Error:Unable to bind to a TCP port - h245 connection "
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
      call->pH245Channel->port = ret;
      OOTRACEDBGC4("Local H.245 port is %d (%s, %s)\n",
                   call->pH245Channel->port,
                   call->callType, call->callToken);
      OOTRACEINFO5("Trying to connect to remote endpoint to setup H245 "
                   "connection %s:%d(%s, %s)\n", call->remoteIP,
                    call->remoteH245Port, call->callType, call->callToken);
             
      if((ret=ooSocketConnect(channelSocket, call->remoteIP,
                              call->remoteH245Port))==ASN_OK)
      {
         call->pH245Channel->sock = channelSocket;
         call->h245SessionState = OO_H245SESSION_ACTIVE;

         OOTRACEINFO3("H245 connection creation succesful (%s, %s)\n",
                      call->callType, call->callToken);

         /*Start terminal capability exchange and master slave determination */
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
      else
      {
         OOTRACEINFO3("ERROR:Failed to connect to remote destination for H245 "
                     "connection (%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
   }
   return OO_OK;
}

int ooSendH245Msg(ooCallData *call, H245Message *msg)
{
   int iRet=0;
   ASN1OCTET * encodebuf;
   if(!call)
      return OO_FAILED;

   encodebuf = (ASN1OCTET*) memAlloc (call->pctxt, MAXMSGLEN);
   if(!encodebuf)
   {
      OOTRACEERR3("Error:Failed to allocate memory for encoding H245 "
                  "message(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   iRet = ooEncodeH245Message(call, msg, encodebuf, MAXMSGLEN);

   if(iRet != OO_OK)
   {
      OOTRACEERR3("Error:Failed to encode H245 message. (%s, %s)\n",
                                             call->callType, call->callToken);
      memFreePtr (call->pctxt, encodebuf);
      return OO_FAILED;
   }
   if(!call->pH245Channel)
   {
      call->pH245Channel = 
              (OOH323Channel*) memAllocZ (call->pctxt, sizeof(OOH323Channel));
      if(!call->pH245Channel)
      {
         OOTRACEERR3("Error:Failed to allocate memory for H245Channel "
                     "structure. (%s, %s)\n", call->callType, call->callToken);
         memFreePtr (call->pctxt, encodebuf);
         return OO_FAILED;
      }
   }
          
   dListAppend (call->pctxt, &call->pH245Channel->outQueue, encodebuf);

   OOTRACEDBGC4("Queued H245 messages %d. (%s, %s)\n",
                call->pH245Channel->outQueue.count,
                call->callType, call->callToken);  

   return OO_OK;
}


int ooSendH225Msg(ooCallData *call, Q931Message *msg)
{
   int iRet=0;
   ASN1OCTET * encodebuf;
   if(!call)
      return OO_FAILED;

   encodebuf = (ASN1OCTET*) memAlloc (call->pctxt, MAXMSGLEN);
   if(!encodebuf)
   {
      OOTRACEERR3("Error:Failed to allocate memory for encoding H225 "
                  "message(%s, %s)\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   iRet = ooEncodeH225Message(call, msg, encodebuf, MAXMSGLEN);
   if(iRet != OO_OK)
   {
      OOTRACEERR3("Error:Failed to encode H225 message. (%s, %s)\n",
                  call->callType, call->callToken);
      memFreePtr (call->pctxt, encodebuf);
      return OO_FAILED;
   }

   dListAppend (call->pctxt, &call->pH225Channel->outQueue, encodebuf);

   OOTRACEDBGC4("Queued H225 messages %d. (%s, %s)\n",
                call->pH225Channel->outQueue.count,
                call->callType, call->callToken); 

   return OO_OK;
}

int ooCreateH225Connection(ooCallData *call)
{
   int ret=0;
   OOSOCKET channelSocket=0;

   if((ret=ooSocketCreate (&channelSocket))!=ASN_OK)
   {
      OOTRACEERR3("Failed to create socket for transmit H2250 channel (%s, %s)"
                  "\n", call->callType, call->callToken);
      return OO_FAILED;
   }
   else
   {
      /*
         bind socket to a port before connecting. Thus avoiding
         implicit bind done by a connect call. Avoided on windows as
         windows sockets have problem in reusing the addresses even after
         setting SO_REUSEADDR, hence in windows we just allow os to bind
         to any random port.
      */
#ifndef _WIN32
      ret = ooBindPort(&gH323ep, OOTCP,channelSocket);
#else
      ret = ooBindOSAllocatedPort(channelSocket);
#endif
     
      if(ret == OO_FAILED)
      {
         OOTRACEERR3("Error:Unable to bind to a TCP port (%s, %s)\n",
         call->callType, call->callToken);
         return OO_FAILED;
      }

      if (0 == call->pH225Channel) {
         call->pH225Channel =
            (OOH323Channel*) memAllocZ (call->pctxt, sizeof(OOH323Channel));
      }
      call->pH225Channel->port = ret;

      OOTRACEINFO5("Trying to connect to remote endpoint(%s:%d) to setup "
                   "H2250 channel (%s, %s)\n", call->remoteIP,
                   call->remotePort, call->callType, call->callToken);

      if((ret=ooSocketConnect(channelSocket, call->remoteIP,
                              call->remotePort))==ASN_OK)
      {
         call->pH225Channel->sock = channelSocket;

         OOTRACEINFO3("H2250 transmiter channel creation - succesful "
                      "(%s, %s)\n", call->callType, call->callToken);

         return OO_OK;
      }
      else
      {
         OOTRACEERR3("ERROR:Failed to connect to remote destination for "
                    "transmit H2250 channel\n",call->callType,call->callToken);
         return OO_FAILED;
      }

      return OO_FAILED;
   }
}

int ooCloseH225Connection (ooCallData *call)
{
   if (0 != call->pH225Channel)
   {
      if(call->pH225Channel->sock != 0)
         ooSocketClose (call->pH225Channel->sock);
      if (call->pH225Channel->outQueue.count > 0)
      {
         dListFreeAll (call->pctxt, &(call->pH225Channel->outQueue));
      }
      memFreePtr (call->pctxt, call->pH225Channel);
      call->pH225Channel = NULL;
   }
   return OO_OK;
}

int ooCreateH323Listener()
{
   int ret=0;
   OOSOCKET channelSocket=0;
   OOIPADDR ipaddrs;

    /* Create socket */
   if((ret=ooSocketCreate (&channelSocket))!=ASN_OK)
   {
      OOTRACEERR1("Failed to create socket for H323 Listener\n");
      return OO_FAILED;
   }
   ret= ooSocketStrToAddr (gH323ep.signallingIP, &ipaddrs);
   if((ret=ooSocketBind (channelSocket, ipaddrs,
                         gH323ep.listenPort))==ASN_OK)
   {
      gH323ep.listener = (OOSOCKET*)ASN1MALLOC(&gH323ep.ctxt,sizeof(OOSOCKET));
      *(gH323ep.listener) = channelSocket;
         
      ooSocketListen(channelSocket, 5); /*listen on socket*/
      OOTRACEINFO1("H323 listener creation - successful\n");
      return OO_OK;
   }
   else
   {
      OOTRACEERR1("ERROR:Failed to create H323 listener\n");
      return OO_FAILED;
   }
}



int ooAcceptH225Connection()   
{
   ooCallData * call;
   int ret;
   char callToken[20];
   OOSOCKET h225Channel=0;
   ret = ooSocketAccept (*(gH323ep.listener), &h225Channel,
                         NULL, NULL);
   if(ret != ASN_OK)
   {
      OOTRACEERR1("Error:Accepting h225 connection\n");
      return OO_FAILED;
   }
   ooGenerateCallToken(callToken, sizeof(callToken));

   call = ooCreateCall("incoming", callToken);
   if(!call)
   {
      OOTRACEERR1("ERROR:Failed to create an incoming call\n");
      return OO_FAILED;
   }

   call->pH225Channel = (OOH323Channel*)
      memAllocZ (call->pctxt, sizeof(OOH323Channel));

   call->pH225Channel->sock = h225Channel;
 
   return OO_OK;
}

int ooAcceptH245Connection(ooCallData *call)
{
   int ret;
   OOSOCKET h245Channel=0;
   ret = ooSocketAccept (*(call->h245listener), &h245Channel,
                         NULL, NULL);
   if(ret != ASN_OK)
   {
      OOTRACEERR1("Error:Accepting h245 connection\n");
      return OO_FAILED;
   }

   if (0 == call->pH245Channel) {
      call->pH245Channel =
         (OOH323Channel*) memAllocZ (call->pctxt, sizeof(OOH323Channel));
   }
   call->pH245Channel->sock = h245Channel;
   call->h245SessionState = OO_H245SESSION_ACTIVE;

   OOTRACEINFO3("H.245 connection established (%s, %s)\n",
                call->callType, call->callToken);

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
   return OO_OK;
}

int ooMonitorChannels()
{
   int ret=0, nfds=0;
   struct timeval toMin, toNext;
   fd_set readfds, writefds;
   ooCallData *call, *prev=NULL;
   DListNode *curNode;
   ooCommand *cmd;
   OOSOCKET tRasSocket=0;
   int i=0;  

   gMonitor = 1;

   toMin.tv_sec = 3;
   toMin.tv_usec = 0;

   while(1)
   {
      FD_ZERO(&readfds);
      FD_ZERO(&writefds);
      nfds = 0;
      ooRasExplorer();
      tRasSocket = ooRasGetSocket();
     
      if(tRasSocket)
      {
         FD_SET( tRasSocket, &readfds );
         if ( nfds < (int)tRasSocket )
            nfds = (int)tRasSocket;
      }
      if(gH323ep.listener)
      {
         FD_SET(*(gH323ep.listener), &readfds);
         if(nfds < (int)*(gH323ep.listener))
            nfds = *((int*)gH323ep.listener);
      }
     
      if(gH323ep.callList)
      {
         call = gH323ep.callList;
         while(call)
         {
            if (0 != call->pH225Channel)
            {
               FD_SET (call->pH225Channel->sock, &readfds);
               if (call->pH225Channel->outQueue.count > 0 ||
                  (call->isTunnelingActive && call->pH245Channel &&
                   call->pH245Channel->outQueue.count > 0))
                  FD_SET (call->pH225Channel->sock, &writefds);
               if(nfds < (int)call->pH225Channel->sock)
                  nfds = call->pH225Channel->sock;
            }
           
            if (0 != call->pH245Channel &&  call->pH245Channel->sock != 0)
            {
               FD_SET(call->pH245Channel->sock, &readfds);
               if (call->pH245Channel->outQueue.count>0)
                  FD_SET(call->pH245Channel->sock, &writefds);
               if(nfds < (int)call->pH245Channel->sock)
                  nfds = call->pH245Channel->sock;
            }
            else if(call->h245listener)
            {
               OOTRACEINFO3("H.245 Listerner socket being monitored "
                            "(%s, %s)\n", call->callType, call->callToken);
                FD_SET(*(call->h245listener), &readfds);
                if(nfds < (int)*(call->h245listener))
                  nfds = *(call->h245listener);
             }
             call = call->next;
           
         }/* while(call) */
      }/*if(gH323ep.callList) */


      if(!gMonitor)
      {
         OOTRACEINFO1("Ending Monitor thread\n");
         break;
      }

      if(nfds != 0) nfds = nfds+1;
     
      if(nfds == 0)
#ifdef _WIN32
         Sleep(10);
#else
      {
         toMin.tv_sec = 0;
         toMin.tv_usec = 10000;
         ooSocketSelect(1, 0, 0, 0, &toMin);
      }
#endif
      else
         ret = ooSocketSelect(nfds, &readfds, &writefds,
                           NULL, &toMin);
     
      if(ret == -1)
      {
        
         OOTRACEERR1("Error in select ...exiting\n");
         exit(-1);
      }

      toMin.tv_sec = 3;
      toMin.tv_usec = 0;
      /*This is for test application. Not part of actual stack */
 
      ooTimerFireExpired(&gH323ep.ctxt, &g_TimerList);
      if(ooTimerNextTimeout(&g_TimerList, &toNext))
      {
         if(ooCompareTimeouts(&toMin, &toNext)>0)
         {
            toMin.tv_sec = toNext.tv_sec;
            toMin.tv_usec = toNext.tv_usec;
         }
      }
  
     

#ifdef _WIN32
      EnterCriticalSection(&gCmdMutex);
#else
      pthread_mutex_lock(&gCmdMutex);
#endif
      /* If gatekeeper is present, then we should not be processing
         any call related command till we are registered with the gk.
      */
      if(gCmdList.count >0)
      {
         for(i=0; i< (int)gCmdList.count; i++)
         {
            curNode = dListFindByIndex (&gCmdList, i) ;
            cmd = (ooCommand*) curNode->data;
            switch(cmd->type)
            {
            case OO_CMD_MAKECALL:
#ifdef __USING_RAS
               if(RasNoGatekeeper != ooRasGetGatekeeperMode() &&
                  !ooRasIsRegistered())
               {
                  OOTRACEWARN1("WARN:New outgoing call cmd is waiting for "
                               "gatekeeper registration.\n");
                  continue;
               }
#endif
               OOTRACEINFO2("Processing MakeCall command %s\n",
                             (char*)cmd->param2);
               ooH323MakeCall((char*)cmd->param1, (char*)cmd->param2);
               break;
            case OO_CMD_MAKECALL_3:
#ifdef __USING_RAS
               if(RasNoGatekeeper != ooRasGetGatekeeperMode() &&
                  !ooRasIsRegistered())
               {
                  OOTRACEWARN1("WARN:New outgoing call cmd is waiting for "
                              "gatekeeper registration.\n");
                  continue;
               }
#endif
               OOTRACEINFO2("Processing MakeCall_3 command %s\n",
                            (char*)cmd->param2);
               ooH323MakeCall_3((char*)cmd->param1, (char*)cmd->param2,
                                                   *((ASN1USINT*)cmd->param3));
               break;
            case OO_CMD_ANSCALL:
#ifdef __USING_RAS
               if(RasNoGatekeeper != ooRasGetGatekeeperMode() &&
                  !ooRasIsRegistered())
               {
                  OOTRACEWARN1("New answer call cmd is waiting for "
                               "gatekeeper registration.\n");
                  continue;
               }
#endif
               OOTRACEINFO2("Processing Answer Call command for %s\n",
                            (char*)cmd->param1);
               ooSendConnect(ooFindCallByToken((char*)cmd->param1));
               break;
            case OO_CMD_REJECTCALL:
                   OOTRACEINFO2("Rejecting call %s\n", (char*)cmd->param1);
               ooEndCall(ooFindCallByToken((char*)cmd->param1));
               break;
            case OO_CMD_HANGCALL:
               OOTRACEINFO2("Processing Hang call command %s\n",
                             (char*)cmd->param1);
               ooH323HangCall((char*)cmd->param1);
               break;
            case OO_CMD_STOPMONITOR:
               OOTRACEINFO1("Processing StopMonitor command\n");
               ooStopMonitorCalls();
               break;
            default: printf("Unhandled command\n");
            }
           dListRemove (&gCmdList, curNode);
           memFreePtr(&gCtxt, curNode);
           if(cmd->param1) memFreePtr(&gCtxt, cmd->param1);
           if(cmd->param2) memFreePtr(&gCtxt, cmd->param2);
           if(cmd->param3) memFreePtr(&gCtxt, cmd->param3);
           memFreePtr(&gCtxt, cmd);
         }
      }
#ifdef _WIN32
   LeaveCriticalSection(&gCmdMutex);
#else
   pthread_mutex_unlock(&gCmdMutex);
#endif
      /* Manage ready descriptors after select */

      if(tRasSocket)
      {
         /* TODO: This is always true on win32. It is ok for now
            as recvFrom returns <=0 bytes and hence the function
            ooRasReceive just returns without doing anything.
            Need to investigate*/
         if(FD_ISSET( tRasSocket, &readfds) )
         {
            ooRasReceive();
         }
      }

      if(gH323ep.listener)
      {
         if(FD_ISSET(*(gH323ep.listener), &readfds))
         {
            OOTRACEDBGA1("New connection at H225 receiver\n");
            ooAcceptH225Connection();
         }     
      }
     

      if(gH323ep.callList)
      {
         call = gH323ep.callList;
         while(call)
         {
            ooTimerFireExpired(call->pctxt, &call->timerList);
            if (0 != call->pH225Channel && 0 != call->pH225Channel->sock)
            {
               if(FD_ISSET(call->pH225Channel->sock, &readfds))
               {
                  ret = ooH2250Receive(call);
                  if(ret != OO_OK)
                  {
                     OOTRACEERR3("ERROR:Failed ooH2250Receive - Clearing call "
                                "(%s, %s)\n", call->callType, call->callToken);
                     if(call->callState < OO_CALL_CLEAR)
                     {
                        call->callEndReason = OO_HOST_CLEARED;
                        call->callState = OO_CALL_CLEAR;
                      }
                  }
               }
            }
           
            if (0 != call->pH225Channel && 0 != call->pH225Channel->sock)
            {
               if(FD_ISSET(call->pH225Channel->sock, &writefds))
               {
                  if(call->pH225Channel->outQueue.count>0)
                  {
                     OOTRACEDBGC3("Sending H225 message (%s, %s)\n",
                                 call->callType, call->callToken);
                     ooSendMsg(call, OOQ931MSG);
                  }
                  if(call->pH245Channel &&
                     call->pH245Channel->outQueue.count>0 &&
                     call->isTunnelingActive)
                  {
                     OOTRACEDBGC3("Tunneling H245 message (%s, %s)\n",
                                  call->callType, call->callToken);
                     ooSendMsg(call, OOH245MSG);
                  }
               }                               
            }

            if (0 != call->pH245Channel && 0 != call->pH245Channel->sock)
            {
               if(FD_ISSET(call->pH245Channel->sock, &readfds))
               {                          
                  ooH245Receive(call);
               }
            }
           
            if (0 != call->pH245Channel && 0 != call->pH245Channel->sock)
            {
               if(FD_ISSET(call->pH245Channel->sock, &writefds))
               {                          
                  if(call->pH245Channel->outQueue.count>0)
                     ooSendMsg(call, OOH245MSG);
               }
            }
            else if(call->h245listener)
            {
               if(FD_ISSET(*(call->h245listener), &readfds))
               {
                  OOTRACEDBGC3("Incoming H.245 connection (%s, %s)\n",
                               call->callType, call->callToken);
                  ooAcceptH245Connection(call);
               }                          
            }
            if(ooTimerNextTimeout(&call->timerList, &toNext))
            {
               if(ooCompareTimeouts(&toMin, &toNext) > 0)
               {
                  toMin.tv_sec = toNext.tv_sec;
                  toMin.tv_usec = toNext.tv_usec;
               }
            }
            prev = call;
            call = call->next;
            if(prev->callState >= OO_CALL_CLEAR)
               ooEndCall(prev);
         }/* while(call) */
      }/* if(gH323ep.callList) */
   }/* while(1)*/
   return OO_OK;
}

int ooH2250Receive(ooCallData *call)
{
   int  recvLen=0, total=0, ret=0;
   ASN1OCTET message[MAXMSGLEN], message1[MAXMSGLEN];
   int size =MAXMSGLEN, len;
   Q931Message *pmsg;
   OOCTXT *pctxt = &gH323ep.msgctxt;
   struct timeval timeout;
   fd_set readfds;

  
   pmsg = (Q931Message*)ASN1MALLOC(pctxt, sizeof(Q931Message));
   if(!pmsg)
   {
      OOTRACEERR3("ERROR:Failed to allocate memory for incoming H.2250 message"
                  " (%s, %s)\n", call->callType, call->callToken);
      memReset(&gH323ep.msgctxt);
      return OO_FAILED;
   }
  
   /* First read just TPKT header which is four bytes */
   recvLen = ooSocketRecv (call->pH225Channel->sock, message, 4);
   if(recvLen == 0)
   {
      OOTRACEWARN3("Warn:RemoteEndpoint closed connection (%s, %s)\n",
                   call->callType, call->callToken);
      if(call->callState < OO_CALL_CLEARED)
      {
         call->callEndReason = OO_REMOTE_CLOSED_CONNECTION;
         call->callState = OO_CALL_CLEARED;
        
      }
      ooFreeQ931Message(pmsg);
      return OO_OK;
   }
   OOTRACEDBGC3("Receiving H.2250 message (%s, %s)\n",
                call->callType, call->callToken);  
   /* Since we are working with TCP, need to determine the
      message boundary. Has to be done at channel level, as channels
      know the message formats and can determine boundaries
   */
   if(recvLen != 4)
   {
      OOTRACEERR4("Error: Reading TPKT header for H225 message "
                  "recvLen= %d (%s, %s)\n", recvLen, call->callType,
                  call->callToken);
      ooFreeQ931Message(pmsg);
      if(call->callState < OO_CALL_CLEAR)
      {
         call->callEndReason = OO_HOST_CLEARED;
         call->callState = OO_CALL_CLEAR;
      }
      return OO_FAILED;
   }

  
   len = message[2];
   len = len<<8;
   len = len | message[3];
   /* Remaining message length is length - tpkt length */
   len = len - 4;

   /* Now read actual Q931 message body. We should make sure that we
      receive complete message as indicated by len. If we don't then there
      is something wrong. The loop below receives message, then checks whether
      complete message is received. If not received, then uses select to peek
      for remaining bytes of the message. If message is not received in 3
      seconds, then we have a problem. Report an error and exit.
   */
   while(total < len)
   {
      recvLen = ooSocketRecv (call->pH225Channel->sock, message1, len-total);
      memcpy(message+total, message1, recvLen);
      total = total + recvLen;

      if(total == len) break; /* Complete message is received */
     
      FD_ZERO(&readfds);
      FD_SET(call->pH225Channel->sock, &readfds);
      timeout.tv_sec = 3;
      timeout.tv_usec = 0;
      ret = ooSocketSelect(call->pH225Channel->sock+1, &readfds, NULL,
                           NULL, &timeout);
      if(ret == -1)
      {
         OOTRACEERR3("Error in select while receiving H.2250 message - "
                     "clearing call (%s, %s)\n", call->callType,
                     call->callToken);
         ooFreeQ931Message(pmsg);
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_HOST_CLEARED;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }
      /* If remaining part of the message is not received in 3 seconds
         exit */
      if(!FD_ISSET(call->pH225Channel->sock, &readfds))
      {
         OOTRACEERR3("Error: Incomplete H.2250 message received - clearing "
                     "call (%s, %s)\n", call->callType, call->callToken);
         ooFreeQ931Message(pmsg);
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_HOST_CLEARED;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }
   }

   OOTRACEDBGC3("Received H.2250 message: (%s, %s)\n",
                call->callType, call->callToken);
   initializePrintHandler(&printHandler, "Received H.2250 Message");

   /* Add event handler to list */
   rtAddEventHandler (pctxt, &printHandler);
   ret = ooQ931Decode (pmsg, len, message);
   if(ret != OO_OK)
   {
      OOTRACEERR3("Error:Failed to decode received H.2250 message. (%s, %s)\n",
                   call->callType, call->callToken);
   }
   OOTRACEDBGC3("Decoded Q931 message (%s, %s)\n", call->callType,
                                                             call->callToken);
   finishPrint();
   rtRemoveEventHandler(pctxt, &printHandler);
   if(ret == OO_OK)
      ooHandleH2250Message(call, pmsg);
   return ret;
}



int ooH245Receive(ooCallData *call)
{
   int  recvLen, ret, len, total=0;
   ASN1OCTET message[MAXMSGLEN], message1[MAXMSGLEN];
   int  size =MAXMSGLEN;
   ASN1BOOL aligned = TRUE, trace = FALSE;
   H245Message *pmsg;
   OOCTXT *pctxt = &gH323ep.msgctxt;
   struct timeval timeout;
   fd_set readfds;
  
   pmsg = (H245Message*)ASN1MALLOC(pctxt, sizeof(H245Message));

   /* First read just TPKT header which is four bytes */
   recvLen = ooSocketRecv (call->pH245Channel->sock, message, 4);
   /* Since we are working with TCP, need to determine the
      message boundary. Has to be done at channel level, as channels
      know the message formats and can determine boundaries
   */
   if(recvLen==0)
   {
     
      OOTRACEINFO3("Closing H.245 channels as remote end point closed H.245"
                   " connection (%s, %s)\n", call->callType, call->callToken);
      ooCloseH245Connection(call);
      ooFreeH245Message(call, pmsg);
      if(call->callState < OO_CALL_CLEAR_ENDSESSION)
      {
         call->callEndReason = OO_REMOTE_CLOSED_H245_CONNECTION;
         call->callState = OO_CALL_CLEAR_ENDSESSION;
      }
      return OO_FAILED;
   }
   OOTRACEDBGC1("Receiving H245 message\n");
   if(recvLen != 4)
   {
      OOTRACEERR3("Error: Reading TPKT header for H245 message (%s, %s)\n",
                  call->callType, call->callToken);
      ooFreeH245Message(call, pmsg);
      if(call->callState < OO_CALL_CLEAR)
      {
         call->callEndReason = OO_HOST_CLEARED;
         call->callState = OO_CALL_CLEAR;
      }
      return OO_FAILED;
   }

   len = message[2];
   len = len<<8;
   len = (len | message[3]);
   /* Remaining message length is length - tpkt length */
   len = len - 4;
   /* Now read actual H245 message body. We should make sure that we
      receive complete message as indicated by len. If we don't then there
      is something wrong. The loop below receives message, then checks whether
      complete message is received. If not received, then uses select to peek
      for remaining bytes of the message. If message is not received in 3
      seconds, then we have a problem. Report an error and exit.
   */
   while(total < len)
   {
      recvLen = ooSocketRecv (call->pH245Channel->sock, message1, len-total);
      memcpy(message+total, message1, recvLen);
      total = total + recvLen;
      if(total == len) break; /* Complete message is received */
      FD_ZERO(&readfds);
      FD_SET(call->pH245Channel->sock, &readfds);
      timeout.tv_sec = 3;
      timeout.tv_usec = 0;
      ret = ooSocketSelect(call->pH245Channel->sock+1, &readfds, NULL,
                           NULL, &timeout);
      if(ret == -1)
      {
         OOTRACEERR3("Error in select...H245 Receive-Clearing call (%s, %s)\n",
                     call->callType, call->callToken);
         ooFreeH245Message(call, pmsg);
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_HOST_CLEARED;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }
      /* If remaining part of the message is not received in 3 seconds
         exit */
      if(!FD_ISSET(call->pH245Channel->sock, &readfds))
      {
         OOTRACEERR3("Error: Incomplete h245 message received (%s, %s)\n",
                     call->callType, call->callToken);
         ooFreeH245Message(call, pmsg);
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_HOST_CLEARED;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }
   }

   OOTRACEDBGC3("Complete H245 message received (%s, %s)\n",
                 call->callType, call->callToken);
   setPERBuffer(pctxt, message, recvLen, aligned);
   initializePrintHandler(&printHandler, "Received H.245 Message");

   /* Add event handler to list */
   rtAddEventHandler (pctxt, &printHandler);
   ret = asn1PD_H245MultimediaSystemControlMessage(pctxt, &(pmsg->h245Msg));
   if(ret != ASN_OK)
   {
      OOTRACEERR3("Error decoding H245 message (%s, %s)\n",
                  call->callType, call->callToken);
      ooFreeH245Message(call, pmsg);
      return OO_FAILED;
   }
   finishPrint();
   rtRemoveEventHandler(pctxt, &printHandler);
   ooHandleH245Message(call, pmsg);
   return OO_OK;
}

/* Generic Send Message functionality. Based on type of message to be sent,
   it calls the corresponding function to retrieve the message buffer and
   then transmits on the associated channel
   Interpreting msgptr:
      Q931 messages except facility
                             1st octet - msgType, next 4 octets - tpkt header,
                             followed by encoded msg
      Q931 message facility
                             1st octect - OOFacility, 2nd octet - tunneled msg
                             type(in case no tunneled msg - OOFacility),
                             3rd and 4th octet - associated logical channel
                             of the tunneled msg(0 when no channel is
                             associated. ex. in case of MSD, TCS), next
                             4 octets - tpkt header, followed by encoded
                             message.

      H.245 messages no tunneling
                             1st octet - msg type, next two octets - logical
                             channel number(0, when no channel is associated),
                             next two octets - total length of the message
                            (including tpkt header)

      H.245 messages - tunneling.
                             1st octet - msg type, next two octets - logical
                             channel number(0, when no channel is associated),
                             next two octets - total length of the message.
                             Note, no tpkt header is present in this case.
                           
*/
int ooSendMsg(ooCallData *call, int type)
{

   int len=0, ret=0, msgType=0, tunneledMsgType=0, logicalChannelNo = 0;
   int i =0;
   DListNode * p_msgNode=NULL;
   ASN1OCTET *msgptr, *msgToSend=NULL;

   if(type == OOQ931MSG)
   {
      if(call->pH225Channel->outQueue.count == 0)
      {
         OOTRACEWARN3("WARN:No H.2250 message to send. (%s, %s)\n",
                      call->callType, call->callToken);
         return OO_FAILED;
      }
      OOTRACEDBGA3("Sending Q931 message (%s, %s)\n", call->callType,
                                                      call->callToken);
      p_msgNode = call->pH225Channel->outQueue.head;
      msgptr = (ASN1OCTET*) p_msgNode->data;
      msgType = msgptr[0];
      if(msgType == OOFacility)
      {
         tunneledMsgType = msgptr[1];
         logicalChannelNo = msgptr[2];
         logicalChannelNo = logicalChannelNo << 8;
         logicalChannelNo = (logicalChannelNo | msgptr[3]);
         len = msgptr[6];
         len = len<<8;
         len = (len | msgptr[7]);
         msgToSend = msgptr+4;
      }else {
         len = msgptr[3];
         len = len<<8;
         len = (len | msgptr[4]);
         msgToSend = msgptr+1;
      }

      /* Remove the message from rtdlist pH225Channel->outQueue */
      dListRemove(&(call->pH225Channel->outQueue), p_msgNode);
      if(p_msgNode)
         memFreePtr(call->pctxt, p_msgNode);

      /* Send message out via TCP */
      ret = ooSocketSend(call->pH225Channel->sock, msgToSend, len);
      if(ret == ASN_OK)
      {
         memFreePtr (call->pctxt, msgptr);
         OOTRACEDBGC3("H2250/Q931 Message sent successfully (%s, %s)\n",
                      call->callType, call->callToken);
         ooOnSendMsg(call, msgType, tunneledMsgType, logicalChannelNo);
         return OO_OK;
      }
      else{
         OOTRACEERR3("H2250Q931 Message send failed (%s, %s)\n",
                     call->callType, call->callToken);
         memFreePtr (call->pctxt, msgptr);
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_HOST_CLEARED;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_FAILED;
      }
   }/* end of type==OOQ931MSG */
   if(type == OOH245MSG)
   {
      if(call->pH245Channel->outQueue.count == 0)
      {
         OOTRACEWARN3("WARN:No H.245 message to send. (%s, %s)\n",
                                 call->callType, call->callToken);
         return OO_FAILED;
      }
      OOTRACEDBGA3("Sending H245 message (%s, %s)\n", call->callType,
                                                      call->callToken);
      p_msgNode = call->pH245Channel->outQueue.head;
      msgptr = (ASN1OCTET*) p_msgNode->data;
      msgType = msgptr[0];

      logicalChannelNo = msgptr[1];
      logicalChannelNo = logicalChannelNo << 8;
      logicalChannelNo = (logicalChannelNo | msgptr[2]);

      len = msgptr[3];
      len = len<<8;
      len = (len | msgptr[4]);
      /* Remove the message from queue */
      dListRemove(&(call->pH245Channel->outQueue), p_msgNode);
      if(p_msgNode)
         memFreePtr(call->pctxt, p_msgNode);

      /* Send message out */
      if (0 == call->pH245Channel && !call->isTunnelingActive)
      {
         OOTRACEWARN3("Neither H.245 channel nor tunneling active "
                     "(%s, %s)\n", call->callType, call->callToken);
         memFreePtr (call->pctxt, msgptr);
         /*ooCloseH245Session(call);*/
         if(call->callState < OO_CALL_CLEAR)
         {
            call->callEndReason = OO_HOST_CLEARED;
            call->callState = OO_CALL_CLEAR;
         }
         return OO_OK;
      }
     
      if (0 != call->pH245Channel && 0 != call->pH245Channel->sock)
      {
         OOTRACEDBGC4("Sending %s H245 message over H.245 channel. "
                      "(%s, %s)\n", ooGetText(msgType), call->callType,
                      call->callToken);
         
         ret = ooSocketSend(call->pH245Channel->sock, msgptr+5, len);
         if(ret == ASN_OK)
         {
            memFreePtr (call->pctxt, msgptr);
            OOTRACEDBGA3("H245 Message sent successfully (%s, %s)\n",
                          call->callType, call->callToken);
            ooOnSendMsg(call, msgType, tunneledMsgType, logicalChannelNo);
            return OO_OK;
         }
         else{
            memFreePtr (call->pctxt, msgptr);
            OOTRACEERR3("ERROR:H245 Message send failed (%s, %s)\n",
                        call->callType, call->callToken);
            if(call->callState < OO_CALL_CLEAR)
            {
               call->callEndReason = OO_HOST_CLEARED;
               call->callState = OO_CALL_CLEAR;
            }
            return OO_FAILED;
         }
      }else if(call->isTunnelingActive){
         OOTRACEDBGC4("Sending %s H245 message as a tunneled message."
                      "(%s, %s)\n", ooGetText(msgType), call->callType,
                      call->callToken);
         
         ret = ooSendAsTunneledMessage(call, msgptr+5,len,msgType,
                                                          logicalChannelNo);
         if(ret != OO_OK)
         {
            memFreePtr (call->pctxt, msgptr);
            OOTRACEERR3("ERROR:Failed to tunnel H.245 message (%s, %s)\n",
                         call->callType, call->callToken);
            if(call->callState < OO_CALL_CLEAR)
            {
               call->callEndReason = OO_HOST_CLEARED;
               call->callState = OO_CALL_CLEAR;
            }
            return OO_FAILED;
         }
         memFreePtr (call->pctxt, msgptr);
         return OO_OK;
      }
   }
   /* Need to add support for other messages such as T38 etc */
   OOTRACEWARN3("ERROR:Unknown message type - message not Sent (%s, %s)\n",
                call->callType, call->callToken);
   return OO_FAILED;
}      

int ooCloseH245Connection(ooCallData *call)
{
   OOTRACEINFO3("Closing H.245 connection (%s, %s)\n", call->callType,
                call->callToken);

   if (0 != call->pH245Channel)
   {
      if(0 != call->pH245Channel->sock)
         ooSocketClose (call->pH245Channel->sock);
      if (call->pH245Channel->outQueue.count > 0)
         dListFreeAll(call->pctxt, &(call->pH245Channel->outQueue));
      memFreePtr (call->pctxt, call->pH245Channel);
      call->pH245Channel = NULL;
   }
   if(call->h245listener)
   {
      ooSocketClose(*(call->h245listener));
      memFreePtr(call->pctxt, call->h245listener);
      call->h245listener = NULL;
   }
   call->h245SessionState = OO_H245SESSION_INACTIVE;
   OOTRACEDBGC3("Closed H245 connection. (%s, %s)\n", call->callType,
                                                       call->callToken);
   return OO_OK;
}

int ooOnSendMsg
      (ooCallData *call, int msgType, int tunneledMsgType, int associatedChan)
{
   ooTimerCallback *cbData=NULL;
   switch(msgType)
   {
   case OOSetup:
      OOTRACEINFO3("Sent Message - Setup (%s, %s)\n", call->callType,
                    call->callToken);
      /* Start call establishment timer */
      cbData = (ooTimerCallback*) ASN1MALLOC(call->pctxt,
                                                     sizeof(ooTimerCallback));
      if(!cbData)
      {
         OOTRACEERR3("Error:Unable to allocate memory for timer callback."
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
      cbData->call = call;
      cbData->timerType = OO_CALLESTB_TIMER;
      if(!ooTimerCreate(call->pctxt, &call->timerList, &ooCallEstbTimerExpired,
                        gH323ep.callEstablishmentTimeout, cbData, FALSE))
      {
         OOTRACEERR3("Error:Unable to create call establishment timer. "
                     "(%s, %s)\n", call->callType, call->callToken);
         ASN1MEMFREEPTR(call->pctxt, cbData);
         return OO_FAILED;
      }

      if(gH323ep.onOutgoingCall)
         gH323ep.onOutgoingCall(call);
      break;
   case OOCallProceeding:
      OOTRACEINFO3("Sent Message - CallProceeding (%s, %s)\n", call->callType,
                    call->callToken);
      break;
   case OOAlert:
      OOTRACEINFO3("Sent Message - Alerting (%s, %s)\n", call->callType,
                    call->callToken);
      if(gH323ep.onAlerting)
         gH323ep.onAlerting(call);
      break;
   case OOConnect:
      OOTRACEINFO3("Sent Message - Connect (%s, %s)\n", call->callType,
                    call->callToken);
      if(gH323ep.onCallEstablished)
         gH323ep.onCallEstablished(call);
      break;
   case OOReleaseComplete:
      OOTRACEINFO3("Sent Message - ReleaseComplete (%s, %s)\n", call->callType,
                    call->callToken);
      if(gH323ep.onCallCleared)
         gH323ep.onCallCleared(call);
#ifdef __USING_RAS
      if(RasNoGatekeeper != ooRasGetGatekeeperMode())
         ooRasSendDisengageRequest(call);
#endif
      break;
   case OOFacility:
      if(tunneledMsgType == OOFacility)
      {
         OOTRACEINFO3("Sent Message - Facility. (%s, %s)\n",
                      call->callType, call->callToken);
      }
      else{
         OOTRACEINFO4("Sent Message - Facility(%s) (%s, %s)\n",
                      ooGetText(tunneledMsgType), call->callType,
                      call->callToken);
         ooOnSendMsg(call, tunneledMsgType, 0, associatedChan);
      }

     
      break;
   case OOMasterSlaveDetermination:
     if(call->isTunnelingActive)
        OOTRACEINFO3("Tinneled Message - MasterSlaveDetermination (%s, %s)\n",
                      call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - MasterSlaveDetermination (%s, %s)\n",
                       call->callType, call->callToken);
       /* Start MSD timer */
      cbData = (ooTimerCallback*) ASN1MALLOC(call->pctxt,
                                                     sizeof(ooTimerCallback));
      if(!cbData)
      {
         OOTRACEERR3("Error:Unable to allocate memory for timer callback data."
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
      cbData->call = call;
      cbData->timerType = OO_MSD_TIMER;
      if(!ooTimerCreate(call->pctxt, &call->timerList, &ooMSDTimerExpired,
                        gH323ep.msdTimeout, cbData, FALSE))
      {
         OOTRACEERR3("Error:Unable to create MSD timer. "
                     "(%s, %s)\n", call->callType, call->callToken);
         ASN1MEMFREEPTR(call->pctxt, cbData);
         return OO_FAILED;
      }

      break;
   case OOMasterSlaveAck:
     if(call->isTunnelingActive)
         OOTRACEINFO3("Tunneled Message - MasterSlaveDeterminationAck (%s, %s)"
                      "\n",  call->callType, call->callToken);
     else
        OOTRACEINFO3("Sent Message - MasterSlaveDeterminationAck (%s, %s)\n",
                    call->callType, call->callToken);
      break;
   case OOMasterSlaveReject:
     if(call->isTunnelingActive)
        OOTRACEINFO3("Tunneled Message - MasterSlaveDeterminationReject "
                     "(%s, %s)\n", call->callType, call->callToken);
     else
        OOTRACEINFO3("Sent Message - MasterSlaveDeterminationReject(%s, %s)\n",
                    call->callType, call->callToken);
      break;
   case OOMasterSlaveRelease:
      if(call->isTunnelingActive)
         OOTRACEINFO3("Tunneled Message - MasterSlaveDeterminationRelease "
                      "(%s, %s)\n", call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - MasterSlaveDeterminationRelease "
                      "(%s, %s)\n", call->callType, call->callToken);
      break;
   case OOTerminalCapabilitySet:
      if(call->isTunnelingActive)
         OOTRACEINFO3("Tunneled Message - TerminalCapabilitySet (%s, %s)\n",
                       call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - TerminalCapabilitySet (%s, %s)\n",
                       call->callType, call->callToken);
      /* Start TCS timer */
      cbData = (ooTimerCallback*) ASN1MALLOC(call->pctxt,
                                                     sizeof(ooTimerCallback));
      if(!cbData)
      {
         OOTRACEERR3("Error:Unable to allocate memory for timer callback data."
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
      cbData->call = call;
      cbData->timerType = OO_MSD_TIMER;
      if(!ooTimerCreate(call->pctxt, &call->timerList, &ooTCSTimerExpired,
                        gH323ep.tcsTimeout, cbData, FALSE))
      {
         OOTRACEERR3("Error:Unable to create TCS timer. "
                     "(%s, %s)\n", call->callType, call->callToken);
         ASN1MEMFREEPTR(call->pctxt, cbData);
         return OO_FAILED;
      }

      break;
   case OOTerminalCapabilitySetAck:
      if(call->isTunnelingActive)
         OOTRACEINFO3("Tunneled Message - TerminalCapabilitySetAck (%s, %s)\n",
                       call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - TerminalCapabilitySetAck (%s, %s)\n",
                       call->callType, call->callToken);
      break;
   case OOTerminalCapabilitySetReject:
      if(call->isTunnelingActive)
         OOTRACEINFO3("Tunneled Message - TerminalCapabilitySetReject "
                      "(%s, %s)\n",  call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - TerminalCapabilitySetReject (%s, %s)\n",
                       call->callType, call->callToken);
      break;
   case OOOpenLogicalChannel:
      if(call->isTunnelingActive)
         OOTRACEINFO3("Tunneled Message - OpenLogicalChannel (%s, %s)\n",
                       call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - OpenLogicalChannel (%s, %s)\n",
                       call->callType, call->callToken);
      /* Start LogicalChannel timer */
      cbData = (ooTimerCallback*) ASN1MALLOC(call->pctxt,
                                                     sizeof(ooTimerCallback));
      if(!cbData)
      {
         OOTRACEERR3("Error:Unable to allocate memory for timer callback data."
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
      cbData->call = call;
      cbData->timerType = OO_OLC_TIMER;
      cbData->channelNumber = associatedChan;
      if(!ooTimerCreate(call->pctxt, &call->timerList,
          &ooOpenLogicalChannelTimerExpired, gH323ep.logicalChannelTimeout,
          cbData, FALSE))
      {
         OOTRACEERR3("Error:Unable to create OpenLogicalChannel timer. "
                     "(%s, %s)\n", call->callType, call->callToken);
         ASN1MEMFREEPTR(call->pctxt, cbData);
         return OO_FAILED;
      }
     
      break;
   case OOOpenLogicalChannelAck:
      if(call->isTunnelingActive)
         OOTRACEINFO3("Tunneled Message - OpenLogicalChannelAck (%s, %s)\n",
                       call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - OpenLogicalChannelAck (%s, %s)\n",
                       call->callType, call->callToken);
      break;
   case OOOpenLogicalChannelReject:
      if(call->isTunnelingActive)
         OOTRACEINFO3("Tunneled Message - OpenLogicalChannelReject (%s, %s)\n",
                       call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - OpenLogicalChannelReject (%s, %s)\n",
                       call->callType, call->callToken);
      break;
   case OOEndSessionCommand:
      if(call->isTunnelingActive)
         OOTRACEINFO1("Tunneled Message - EndSessionCommand\n");
      else
         OOTRACEINFO1("Sent Message - EndSessionCommand\n");
      break;
   case OOCloseLogicalChannel:
      if(call->isTunnelingActive)
         OOTRACEINFO3("Tunneled Message - CloseLogicalChannel (%s, %s)\n",
                       call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - CloseLogicalChannel (%s, %s)\n",
                       call->callType, call->callToken);
      /* Start LogicalChannel timer */
      cbData = (ooTimerCallback*) ASN1MALLOC(call->pctxt,
                                                     sizeof(ooTimerCallback));
      if(!cbData)
      {
         OOTRACEERR3("Error:Unable to allocate memory for timer callback data."
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
      cbData->call = call;
      cbData->timerType = OO_CLC_TIMER;
      cbData->channelNumber = associatedChan;
      if(!ooTimerCreate(call->pctxt, &call->timerList,
          &ooCloseLogicalChannelTimerExpired, gH323ep.logicalChannelTimeout,
          cbData, FALSE))
      {
         OOTRACEERR3("Error:Unable to create CloseLogicalChannel timer. "
                     "(%s, %s)\n", call->callType, call->callToken);
         ASN1MEMFREEPTR(call->pctxt, cbData);
         return OO_FAILED;
      }
     
      break;
   case OOCloseLogicalChannelAck:
      if(call->isTunnelingActive)
         OOTRACEINFO3("Tunneled Message - CloseLogicalChannelAck (%s, %s)\n",
                       call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - CloseLogicalChannelAck (%s, %s)\n",
                       call->callType, call->callToken);
      break;
   case OORequestChannelClose:
      if(call->isTunnelingActive)
         OOTRACEINFO3("Tunneled Message - RequestChannelClose (%s, %s)\n",
                       call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - RequestChannelClose (%s, %s)\n",
                       call->callType, call->callToken);
      /* Start RequestChannelClose timer */
      cbData = (ooTimerCallback*) ASN1MALLOC(call->pctxt,
                                                     sizeof(ooTimerCallback));
      if(!cbData)
      {
         OOTRACEERR3("Error:Unable to allocate memory for timer callback data."
                     "(%s, %s)\n", call->callType, call->callToken);
         return OO_FAILED;
      }
      cbData->call = call;
      cbData->timerType = OO_RCC_TIMER;
      cbData->channelNumber = associatedChan;
      if(!ooTimerCreate(call->pctxt, &call->timerList,
          &ooRequestChannelCloseTimerExpired, gH323ep.logicalChannelTimeout,
          cbData, FALSE))
      {
         OOTRACEERR3("Error:Unable to create RequestChannelClose timer. "
                     "(%s, %s)\n", call->callType, call->callToken);
         ASN1MEMFREEPTR(call->pctxt, cbData);
         return OO_FAILED;
      }
      break;
   case OORequestChannelCloseAck:
      if(call->isTunnelingActive)
         OOTRACEINFO3("Tunneled Message - RequestChannelCloseAck (%s, %s)\n",
                       call->callType, call->callToken);
      else
         OOTRACEINFO3("Sent Message - RequestChannelCloseAck (%s, %s)\n",
                       call->callType, call->callToken);
      break;
  
   default:
     ;
   }
   return OO_OK;
}


int ooStopMonitorCalls()
{
   ooCallData * call;
   OOTRACEINFO1("Doing ooStopMonitorCalls\n");
   if(gH323ep.callList)
   {
      OOTRACEWARN1("Warn:Abruptly ending calls as stack going down\n");
      call = gH323ep.callList;
      while(call)
      {
         OOTRACEWARN3("Clearing call (%s, %s)\n", call->callType,
                       call->callToken);
         call->callEndReason = OO_HOST_CLEARED;
         ooCleanCall(call);
         call = NULL;
         call = gH323ep.callList;
      }
   }
   OOTRACEINFO1("Stopping listener for incoming calls\n");  
   if(gH323ep.listener)
   {
      ooSocketClose(*(gH323ep.listener));
      memFreePtr(&gH323ep.ctxt, gH323ep.listener);
   }

   gMonitor = 0;
   OOTRACEINFO1("Returning form ooStopMonitorCalls\n");
   return OO_OK;
}
     


