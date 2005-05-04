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
/**
 * @file ooh323ep.h
 * This file contains H323 endpoint related functions.
 */
#ifndef OO_H323EP_H_
#define OO_H323EP_H_
#include "ootypes.h"
#include "ooasn1.h"


#define DEFAULT_TRACEFILE "trace.log"
#define DEFAULT_TERMTYPE 50
#define DEFAULT_PRODUCTID  "objsys"
#define DEFAULT_CALLERID   "objsyscall"
#define DEFAULT_T35COUNTRYCODE 1
#define DEFAULT_T35EXTENSION 0
#define DEFAULT_MANUFACTURERCODE 71
#define DEFAULT_CALLESTB_TIMEOUT 60
#define DEFAULT_MSD_TIMEOUT 30
#define DEFAULT_TCS_TIMEOUT 30
#define DEFAULT_LOGICALCHAN_TIMEOUT 30
#define DEFAULT_ENDSESSION_TIMEOUT 15
#define DEFAULT_H323PORT 1720

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXTERN
#ifdef _WIN32
#define EXTERN __declspec(dllexport)
#else
#define EXTERN
#endif /* _WIN32 */
#endif /* EXTERN */

/**
 * @defgroup h323ep  H323 Endpoint management functions
 * @{
 */

/**
 * This function is the first function to be invoked before using stack. It
 * initializes the H323 Endpoint.
 * @param callerid       ID to be used for outgoing calls.
 * @param callMode       Type of calls to be made(audio/video/fax).
 *                       (OO_CALLMODE_AUDIO, OO_CALLMODE_VIDEO)
 * @param tracefile      Trace file name.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure
 */
EXTERN int ooH323EpInitialize
   (const char *callerid, int callMode, const char* tracefile);


/**
 * This function is used to assign a local ip address to be used for call
 * signalling.
 * @param localip        Dotted IP address to be used for call signalling.
 * @param listenport     Port to be used for listening for incoming calls.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpSetLocalAddress(char * localip, int listenport);

/**
 * This function is used to set the trace level for the H.323 endpoint.
 * @param traceLevel     Level of tracing.
 *
 * @return               OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN int ooH323EpSetTraceLevel(int traceLevel);

/**
 * This function is used to add the h323id alias for the endpoint.
 * @param h323id         H323-ID to be set as alias.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpAddAliasH323ID(char * h323id);

/**
 * This function is used to add the dialed digits alias for the
 * endpoint.
 * @param dialedDigits   Dialed-Digits to be set as alias.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpAddAliasDialedDigits(char * dialedDigits);

/**
 * This function is used to add the url alias for the endpoint.
 * @param url            URL to be set as an alias.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpAddAliasURLID(char * url);

/**
 * This function is used to add an email id as an alias for the endpoint.
 * @param email          Email id to be set as an alias.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpAddAliasEmailID(char * email);

/**
 * This function is used to add an ip address as an alias.
 * @param ipaddress      IP address to be set as an alias.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpAddAliasTransportID(char * ipaddress);

/**
 * This function is used to clear all the aliases used by the
 * H323 endpoint.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpClearAllAliases(void);

/**
 * This function is used to set the H225 message callbacks for the
 * endpoint.
 * @param h225Callbacks  Callback structure containing various callbacks.
 *
 * @return               OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpSetH225MsgCallbacks(OOH225MsgCallbacks h225Callbacks);

/**
 * This function is used to register the H323 Endpoint callback functions.
 * @param onAlerting            Callback function to be called when alerting
 *                              message is sent.
 * @param onIncomingCall        Callback function to be called when a new
 *                              incoming call is accepted.
 * @param onOutgoingCallAdmitted Callback function to be called when a
 *                                outgoing call is admitted by gk.
 *
 * @param onOutgoingCall        Callback function to be called when an outgoing
 *                              call is placed on behalf of the application.
 * @param onCallEstablished     Callback function to be called when a call is
 *                              established with the remote end point.
 * @param onCallCleared         Callback function to be called when a call is
 *                              cleared.
 *                             
 *
 * @return                      OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpRegisterCallbacks(cb_OnAlerting onAlerting,
                              cb_OnIncomingCall onIncomingCall,
                              cb_OnOutgoingCallAdmitted onOutgoingCallAdmitted,
                              cb_OnOutgoingCall onOutgoingCall,
                              cb_OnCallEstablished onCallEstablished,
                              cb_OnCallCleared onCallCleared);


/**
 * This function is the last function to be invoked after done using the
 * stack. It closes the H323 Endpoint for an application, releasing all
 * the associated memory.
 *
 * @param None
 *
 * @return          OO_OK on success
 *                  OO_FAILED on failure
 */
EXTERN int ooH323EpDestroy(void);


/**
 * This function is used to enable the auto answer feature for
 * incoming calls
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpEnableAutoAnswer(void);

/**
 * This function is used to disable the auto answer feature for
 * incoming calls.
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpDisableAutoAnswer(void);

/**
 * This function is used to enable faststart.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpEnableFastStart(void);

/**
 * This function is used to disable faststart.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpDisableFastStart(void);

/**
 * This function is used to enable tunneling.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpEnableH245Tunneling(void);

/**
 * This function is used to disable tunneling.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpDisableH245Tunneling(void);

/**
 * This function is used to enable GkRouted calls.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpEnableGkRouted(void);

/**
 * This function is used to disable Gkrouted calls.
 *
 * @return            OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpDisableGkRouted(void);

/**
 * This function is used to set the product ID.
 * @param productID  New value for the product id.
 *
 * @return           OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpSetProductID (const char * productID);

/**
 * This function is used to set version id.
 * @param versionID  New value for the version id.
 *
 * @return           OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpSetVersionID (const char * versionID);

/**
 * This function is used to set callerid to be used for outbound
 * calls.
 * @param callerID  New value for the caller id.
 *
 * @return          OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooH323EpSetCallerID (const char * callerID);

/**
 * This function is used to set calling party number to be used for outbound
 * calls.Note, you can override it for a specific call by using
 * ooCallSetCallingPartyNumber function.
 * @param number   e164 number to be used as calling party number.
 *
 * @return         OO_OK, on success; OO_FAILED, otherwise.
 */
EXTERN int ooH323EpSetCallingPartyNumber(const char * number);

/**
 * This function is used to print the current configuration information of
 * the H323 endpoint to log file.
 */
void ooH323EpPrintConfig(void);

/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#endif
