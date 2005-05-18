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

#include "ooStackCmds.h"
#include "ootrace.h"
#include "ooq931.h"

/** Global endpoint structure */
extern ooEndPoint gH323ep;

extern OO_MUTEX gCmdMutex;

int ooMakeCall
   (const char* dest, char* callToken, size_t bufsiz, ooCallOptions *opts)
{
   ooCommand *cmd;
   int ret;
#ifdef _WIN32
   EnterCriticalSection(&gCmdMutex);
#else
   pthread_mutex_lock(&gCmdMutex);
#endif
   cmd = (ooCommand*) memAlloc (&gH323ep.ctxt, sizeof(ooCommand));
   if(!cmd)
   {
      OOTRACEERR1("Error:Memory - ooMakeCall - cmd\n");
      return OO_FAILED;
   }
   cmd->type = OO_CMD_MAKECALL;
   cmd->param1 = (void*) memAlloc(&gH323ep.ctxt, strlen(dest)+1);
   if(!cmd->param1)
   {
      OOTRACEERR1("ERROR:Memory - ooMakeCall - param1\n");
      memFreePtr(&gH323ep.ctxt, cmd);
      return OO_FAILED;
   }
   strcpy((char*)cmd->param1, dest);

   /* Generate call token*/
   if(!callToken)
   {
      OOTRACEERR1("ERROR:Invalid 'callToken' parameter to 'ooMakeCall'\n");
      memFreePtr(&gH323ep.ctxt, cmd->param1);
      memFreePtr(&gH323ep.ctxt, cmd);
      return OO_FAILED;
   }

   if ((ret = ooGenerateCallToken (callToken, bufsiz)) != 0)
      return ret;

   cmd->param2 = (void*) memAlloc(&gH323ep.ctxt, strlen(callToken)+1);
   if(!cmd->param2)
   {
      OOTRACEERR1("ERROR:Memory - ooMakeCall - param2\n");
      memFreePtr(&gH323ep.ctxt, cmd->param1);
      memFreePtr(&gH323ep.ctxt, cmd);
      return OO_FAILED;
   }
  
   strcpy((char*)cmd->param2, callToken);

   if(!opts)
   {
      cmd->param3 = 0;
   }else {
      cmd->param3 = (void*) memAlloc(&gH323ep.ctxt, sizeof(ooCallOptions));
      if(!cmd->param3)
      {
         OOTRACEERR1("ERROR:Memory - ooMakeCall - param3\n");
         memFreePtr(&gH323ep.ctxt, cmd->param1);
         memFreePtr(&gH323ep.ctxt, cmd->param2);
         memFreePtr(&gH323ep.ctxt, cmd);
         return OO_FAILED;
      }
      memcpy((void*)cmd->param3, opts, sizeof(ooCallOptions));
   }


   dListAppend (&gH323ep.ctxt, &gH323ep.stkCmdList, cmd);
#ifdef HAVE_PIPE
   if(write(gH323ep.cmdPipe[1], "c", 1)<0)
   {
      OOTRACEERR1("ERROR:Failed to write to command pipe\n");
   }
#endif

#ifdef _WIN32
   LeaveCriticalSection(&gCmdMutex);
#else
   pthread_mutex_unlock(&gCmdMutex);
#endif

   return OO_OK;
}

int ooAnswerCall(char *callToken)
{
   ooCommand *cmd;
#ifdef _WIN32
   EnterCriticalSection(&gCmdMutex);
#else
   pthread_mutex_lock(&gCmdMutex);
#endif
   cmd = (ooCommand*)memAlloc(&gH323ep.ctxt, sizeof(ooCommand));
   if(!cmd)
   {
      OOTRACEERR1("Error:Memory - ooAnswerCall - cmd\n");
      return OO_FAILED;
   }
   memset(cmd, 0, sizeof(ooCommand));
   cmd->type = OO_CMD_ANSCALL;

   cmd->param1 = (void*) memAlloc(&gH323ep.ctxt, strlen(callToken)+1);
   if(!cmd->param1)
   {
      OOTRACEERR1("ERROR:Memory - ooAnswerCall - param1\n");
      return OO_FAILED;
   }
   strcpy((char*)cmd->param1, callToken);
  
   dListAppend(&gH323ep.ctxt, &gH323ep.stkCmdList, cmd);
  
#ifdef HAVE_PIPE
   if(write(gH323ep.cmdPipe[1], "c", 1)<0)
   {
      OOTRACEERR1("ERROR:Failed to write to command pipe\n");
   }
#endif

#ifdef _WIN32
   LeaveCriticalSection(&gCmdMutex);
#else
   pthread_mutex_unlock(&gCmdMutex);
#endif

   return OO_OK;
}

int ooForwardCall(char* callToken, char *dest)
{
   ooCommand *cmd;
#ifdef _WIN32
   EnterCriticalSection(&gCmdMutex);
#else
   pthread_mutex_lock(&gCmdMutex);
#endif
   cmd = (ooCommand*)memAlloc(&gH323ep.ctxt, sizeof(ooCommand));
   if(!cmd)
   {
      OOTRACEERR1("Error:Memory - ooForwardCall - cmd\n");
      return OO_FAILED;
   }
   memset(cmd, 0, sizeof(ooCommand));
   cmd->type = OO_CMD_FWDCALL;

   cmd->param1 = (void*) memAlloc(&gH323ep.ctxt, strlen(callToken)+1);
   cmd->param2 = (void*) memAlloc(&gH323ep.ctxt, strlen(dest)+1);
   if(!cmd->param1)
   {
      OOTRACEERR1("ERROR:Memory - ooForwardCall - param1/param2\n");
      return OO_FAILED;
   }
   strcpy((char*)cmd->param1, callToken);
   strcpy((char*)cmd->param2, dest);
   dListAppend(&gH323ep.ctxt, &gH323ep.stkCmdList, cmd);
  
#ifdef HAVE_PIPE
   if(write(gH323ep.cmdPipe[1], "c", 1)<0)
   {
      OOTRACEERR1("ERROR:Failed to write to command pipe\n");
   }
#endif
#ifdef _WIN32
   LeaveCriticalSection(&gCmdMutex);
#else
   pthread_mutex_unlock(&gCmdMutex);
#endif

   return OO_OK;
}


int ooHangCall(char * callToken, OOCallClearReason reason)
{
   ooCommand *cmd;
#ifdef _WIN32
   EnterCriticalSection(&gCmdMutex);
#else
   pthread_mutex_lock(&gCmdMutex);
#endif
   cmd = (ooCommand*)memAlloc(&gH323ep.ctxt, sizeof(ooCommand));
   if(!cmd)
   {
      OOTRACEERR1("Error:Allocating memory for command structure - HangCall\n");
      return OO_FAILED;
   }

   memset(cmd, 0, sizeof(ooCommand));
   cmd->type = OO_CMD_HANGCALL;
   cmd->param1 = (void*) memAlloc(&gH323ep.ctxt, strlen(callToken)+1);
   cmd->param2 = (void*)memAlloc(&gH323ep.ctxt, sizeof(OOCallClearReason));
   if(!cmd->param1 || !cmd->param2)
   {
      OOTRACEERR1("ERROR:Allocating memory for cmd param1/param2- HangCall\n");
      return OO_FAILED;
   }
   strcpy((char*)cmd->param1, callToken);
   *((OOCallClearReason*)cmd->param2) = reason;
  
   dListAppend(&gH323ep.ctxt, &gH323ep.stkCmdList, cmd);
  
#ifdef HAVE_PIPE
   if(write(gH323ep.cmdPipe[1], "c", 1)<0)
   {
      OOTRACEERR1("ERROR:Failed to write to command pipe\n");
   }
#endif
#ifdef _WIN32
   LeaveCriticalSection(&gCmdMutex);
#else
   pthread_mutex_unlock(&gCmdMutex);
#endif

   return OO_OK;
}

int ooStopMonitor()
{
   ooCommand *cmd;
#ifdef _WIN32
   EnterCriticalSection(&gCmdMutex);
#else
   pthread_mutex_lock(&gCmdMutex);
#endif
   cmd = (ooCommand*)memAlloc(&gH323ep.ctxt, sizeof(ooCommand));
   if(!cmd)
   {
      OOTRACEERR1("Error:Allocating memory for command structure - StopMonitor\n");
      return OO_FAILED;
   }
   memset(cmd, 0, sizeof(ooCommand));
   cmd->type = OO_CMD_STOPMONITOR;
  
   dListAppend (&gH323ep.ctxt, &gH323ep.stkCmdList, cmd);

#ifdef HAVE_PIPE
   if(write(gH323ep.cmdPipe[1], "c", 1)<0)
   {
      OOTRACEERR1("ERROR:Failed to write to command pipe\n");
   }
#endif
  
#ifdef _WIN32
   LeaveCriticalSection(&gCmdMutex);
#else
   pthread_mutex_unlock(&gCmdMutex);
#endif
   return OO_OK;
}
