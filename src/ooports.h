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
 * @file ooports.h
 * This file contains functions to manage ports used by the stack.
 */

#ifndef _OOPORTS_H_
#define _OOPORTS_H_

#include "ootypes.h"

typedef enum OOH323PortType {
   OOTCP, OOUDP, OORTP
} OOH323PortType;


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
 * Get the next port of type TCP/UDP/RTP from the corresponding range.
 * When max value for the range is reached, it starts again from the
 * first port number of the range.
 *
 * @param ep         Reference to the H323 Endpoint structure.
 * @param type       Type of the port to be retrieved(OOTCP/OOUDP/OORTP).
 *
 * @return           The next port number for the specified type is returned.
 */
EXTERN int ooGetNextPort (OOH323PortType type);

/**
 * Bind socket to a port within the port range specified by the
 * application at the startup.
 *
 * @param ep        Reference to H323 Endpoint structure.
 * @param type      Type of the port required for the socket.
 * @param socket    The socket to be bound.
 *
 * @return          In case of success returns the port number to which
 *                  socket is bound and in case of failure just returns
 *                  a negative value.
*/
EXTERN int ooBindPort (OOH323PortType type, OOSOCKET socket);

/**
 * This function is supported for windows version only.
 *  Windows sockets have problem in reusing the addresses even after
 *  setting SO_REUSEADDR, hence in windows we just allow os to bind
 *  to any random port.
*/

#ifdef _WIN32       
EXTERN int ooBindOSAllocatedPort(OOSOCKET socket);
#endif

#ifdef __cplusplus
}
#endif

#endif
