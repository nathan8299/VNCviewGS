/*********************************************************************
* vncsession.cc - Routines for initiating/conducting a VNC session
* 		  with the remote host
*********************************************************************/

#if __ORCAC__
#pragma lint -1
#pragma noroot
#endif

#if DEBUG
/* #pragma debug 25 */
#endif

#include <types.h>
#include <orca.h>
#include <stdio.h>       
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <tcpip.h>
#include <quickdraw.h>
#include <qdaux.h>
#include <event.h>
#include <window.h>
#include <resources.h>
#include <misctool.h>
#include <desk.h>
#include <control.h>
#include <locator.h>
#include <memory.h>
#include <cryptotool.h>
#include <menu.h>

#include "vncview.h"
#include "vncsession.h"
#include "vncdisplay.h"
#include "menus.h"

#define linServer	3
#define linPassword	7

#define noCryptoError		2005
#define authFailedError		2006
#define authTooManyError	2007
#define noTCPIPConnectionError	2011
#define badGetIpidError		2012
#define badOptionNegotiationError	2013
#define badHandshakingError	2014
#define badReadTCPError		2015

GrafPortPtr connectStatusWindowPtr = NULL;

unsigned int hostIpid;
void ** readBufferHndl;		/* Handle to the data read by the last	
							 * DoReadTCP call.  Copy this elsewhere if more
                             * data may be read while it is still in use.
                             */
BOOLEAN readError;
BOOLEAN alerted = FALSE;

#define buffTypePointer 0x0000		/* For TCPIPReadTCP() */
#define buffTypeHandle 0x0001
#define buffTypeNewHandle 0x0002

#define vncConnectionFailed		SwapBytes4(0)
#define vncNoAuthentication     SwapBytes4(1)
#define vncVNCAuthentication	SwapBytes4(2)

/***************************************************************
* DoConnect - establish connection to server
***************************************************************/

void DoConnect (void) {
	int i;			/* loop counter */

	/* Get server & password */
	
    GetLETextByID(newConnWindow, linServer, (StringPtr) vncServer);
	GetLETextByID(newConnWindow, linPassword, (StringPtr) vncPassword);

    /* Try to establish connection before continuing; if unsuccessful, stop */
	if (ConnectTCPIP() == FALSE) {
        SysBeep();
        AlertWindow(awResource, NULL, noTCPIPConnectionError);
        return;
        }

	if (GetIpid() == FALSE) {
        SysBeep();
        AlertWindow(awResource, NULL, badGetIpidError);
		return;
        }

    if (DoVNCHandshaking() == FALSE) {
		SetHandleSize(1,readBufferHndl);
        CloseConnectStatusWindow();
        InitCursor();
        SysBeep();
        if (alerted == FALSE)
        	AlertWindow(awResource, NULL, badHandshakingError);
        else
	        alerted = FALSE;
        return;
        }
    if (FinishVNCHandshaking() == FALSE) {
		SetHandleSize(1,readBufferHndl);
        CloseConnectStatusWindow();
        InitCursor();
        AlertWindow(awResource, NULL, badOptionNegotiationError);
        SysBeep();
        return;
	    }

    InitVNCWindow();

    CloseConnectStatusWindow();
    InitCursor();

	DoClose(newConnWindow);
    DisableMItem(fileNewConnection);

	myEvent.wmTaskMask = 0x001D79FE;  /* don't let TaskMaster process keys */
	InitMenus(noKB);
    vncConnected = TRUE;
  	}

/*******************************************************************
* DisplayConnectStatus - Display modal dialog with status information
*	statusString - P-String to display
*	cancelMessage - determines whether to display string about OA-.
*******************************************************************/

void DisplayConnectStatus(char *statusString, BOOLEAN cancelMessage) {
	#define wrNum		 1002
	#define cancelStr	10002

	GrafPortPtr oldPort;
	Rect bigRect = {0,9,15,293};

    if (connectStatusWindowPtr == NULL) {
		connectStatusWindowPtr = NewWindow2(NULL, NULL, NULL, NULL,
        				    				0x02, wrNum, rWindParam1);
        }

	if (connectStatusWindowPtr != NULL) {	/* Only draw if window was */
	    if (GetMasterSCB() & 0x0080)					/* If in 640 mode... */
		    MoveWindow(169, 85, connectStatusWindowPtr);
    	else											/* If in 320 mode... */
	    	MoveWindow(9, 85, connectStatusWindowPtr);

   		oldPort = GetPort();            	/* created successfully    */
    	SetPort(connectStatusWindowPtr);
    	EraseRect(&bigRect);	/* Clipped to window's GrafPort */
    	MoveTo(bigRect.h1, 13);
    	DrawStringWidth(0x6000, (Long) statusString, bigRect.h2 - bigRect.h1);
    	if (cancelMessage) {
        	MoveTo(bigRect.h1, 24);
    		DrawStringWidth(0x0002, cancelStr, bigRect.h2 - bigRect.h1);
			}
    	SetPort(oldPort);
		}

    #undef wrNum
	#undef cancelStr
	}

/***********************************************************************
* DisplayConnectStatusFromTool - Can be passed to Marinetti
***********************************************************************/
#pragma databank	1	/* Set data bank register to access globals. */
#pragma toolparms	1	/* Use tool-style stack model */
void DisplayConnectStatusFromTool (char *statusString) {
	DisplayConnectStatus(statusString, TRUE);
    }
#pragma toolparms	0	/* Use ORCA stack model */
#pragma databank	0	/* Must restore data bank register on exit */

/***********************************************************************
* CloseConnectStatusWindow - Close connect status window (if open)
***********************************************************************/
void CloseConnectStatusWindow (void) {
	if (connectStatusWindowPtr != NULL) {
	    CloseWindow(connectStatusWindowPtr);
        connectStatusWindowPtr = NULL;
    	}
	}

/***********************************************************************
* ConnectTCPIP - Try to establish a TCP/IP connection through Marinetti
***********************************************************************/
BOOLEAN ConnectTCPIP (void)
{
	BOOLEAN connected = FALSE; 		/* Are we connected to the network now? */

	if (TCPIPGetConnectStatus() == FALSE) {	/* If no TCP/IP connection... */
		WaitCursor();
		TCPIPConnect(&DisplayConnectStatusFromTool);
		if (!toolerror())
			connected = TRUE;
		CloseConnectStatusWindow();
		InitCursor();
    	}
    else						            /* Already connected */
	    return TRUE;

	if (connected)
		return TRUE;
	return FALSE;
}

/***********************************************************************
* GetIpid() - parse the server name and attempt to get an ipid for it
***********************************************************************/
BOOLEAN GetIpid (void)
{
	#define baseDisplayNum	5900

	int i;
	long hostPort;
	cvtRec hostInfo;
	static dnrBuffer dnrInfo;
    unsigned long initialTime;

    /* Find ":" character that delimits name (or IP) from display number */
	for (i = vncServer[0]; isdigit(vncServer[i]) && i>0; i--);

	/* Set port to connect to */
    if (sscanf(&vncServer[i], ":%ld", &hostPort) == 0)
		hostPort = 0;
    hostPort += baseDisplayNum;
    
    /* Modify the string so it only contains the hostname or IP */
	if (vncServer[i] == ':') {
		vncServer[0] = i - 1;
		vncServer[i] = 0;
        	}

    /* If it's an IP address, then put it in the record */
	if (TCPIPValidateIPString(vncServer))
		TCPIPConvertIPToHex(&hostInfo, vncServer);
	else {												/* Do a DNS lookup */
        hostInfo.cvtPort = TCPIPMangleDomainName(0xF800, vncServer);
        TCPIPDNRNameToIP(vncServer, &dnrInfo);
		if (toolerror())
	        return FALSE;
        WaitCursor();
        DisplayConnectStatus("\pResolving domain name...", FALSE);
        initialTime = TickCount();
        while (dnrInfo.DNRstatus == DNR_Pending) {
	        if (TickCount() >= initialTime + 15*60)
		        break;
            TCPIPPoll();						/* Call TCPIPPoll() so that	 */
            }                           		/* Marinetti can process data */
        CloseConnectStatusWindow();
        InitCursor();
        if (dnrInfo.DNRstatus == DNR_OK)
	        hostInfo.cvtIPAddress == dnrInfo.DNRIPaddress;
        else
	        return FALSE;
        }

    hostIpid = TCPIPLogin(userid(), hostInfo.cvtIPAddress, (int) hostPort,
		      				0x0010 /* minimize latency */,
                            0x0040 /* Normal TTL*/);
	if (toolerror())
		return FALSE;

    if (TCPIPOpenTCP(hostIpid) == tcperrOK)
		if (!toolerror())    
	        return TRUE;
            
    return FALSE;      

	#undef baseDisplayNum
	}                   


/* Read data, waiting for up to 15 seconds for the data to be ready */
BOOLEAN DoWaitingReadTCP(unsigned long dataLength) {
    unsigned long stopTime;
	BOOLEAN result = FALSE;

    stopTime = TickCount() + 15 * 60;
    do {
        result = DoReadTCP(dataLength);
    	} while (result == FALSE && TickCount() < stopTime);

    return result;
    }


/* Fix things when TCPIPReadTCP returns less data than it's supposed to */
BOOLEAN ReadFixup (unsigned long requested, unsigned long returned) {
	static rrBuff theRRBuff;

    SetHandleSize(requested, readBufferHndl);
    if (toolerror())
	    return FALSE;

    do {
    	TCPIPPoll();
    	if (TCPIPReadTCP(hostIpid, buffTypeNewHandle, NULL,
    				requested-returned, &theRRBuff) != tcperrOK)
	    	return FALSE;
		if (toolerror())
	    	return FALSE;
    	
        if (theRRBuff.rrBuffCount == 0)		/* To avoid infinite loops */
	    	return FALSE;

        HandToPtr(theRRBuff.rrBuffHandle, (char *)*readBufferHndl + returned,
			        theRRBuff.rrBuffCount);

        returned += theRRBuff.rrBuffCount;

        } while (returned < requested);

    return TRUE;
	}

/**********************************************************************
* DoReadTCP() - Issue TCPIPReadTCP() call w/ appropriate parameters
*	Return value = did the read succeed?
**********************************************************************/
BOOLEAN DoReadTCP (unsigned long dataLength) {
    static srBuff theSRBuff;
    static rrBuff theRRBuff;

    TCPIPPoll();

    if (TCPIPStatusTCP(hostIpid, &theSRBuff) != tcperrOK)
	    return FALSE;
    if (toolerror())
	    return FALSE;

	if (theSRBuff.srRcvQueued < dataLength)
	    return FALSE;

    if (TCPIPReadTCP(hostIpid, buffTypeHandle, (Ref) readBufferHndl,
					    dataLength, &theRRBuff) != tcperrOK)
    	return FALSE;
	if (toolerror())
	    return FALSE;

    if (theRRBuff.rrBuffCount != dataLength)
        return ReadFixup(dataLength, theRRBuff.rrBuffCount);

    return TRUE;
	}


/**********************************************************************
* DoVNCHandshaking() - Establish connection to VNC server
**********************************************************************/
BOOLEAN DoVNCHandshaking (void) {
    #define connectionFailedAlert 2004
    #define badRFBVersionAlert 2008
    #define badAuthTypeAlert 2009
    static char versionString[12];
    unsigned long reasonLength;
    char *errorString;

    WaitCursor();
    DisplayConnectStatus("\pConnecting to VNC server...", FALSE);

    /* Read RFB version string from the server */
    strcpy(versionString, "");
    if (! DoWaitingReadTCP(12))
	    return FALSE;
    HLock(readBufferHndl);
    if ( ! ((strncmp((char *)*readBufferHndl, "RFB ", 4) == 0) &&
		    (strncmp((char *)*readBufferHndl+4, RFBMAJORVERSIONSTR, 3) >= 0) &&
            (strncmp((char *)*readBufferHndl+7, ".", 1) == 0) &&
            (strncmp((char *)*readBufferHndl+11, "\n", 1) == 0))) {
        HUnlock(readBufferHndl);
        InitCursor();
		AlertWindow(awResource, NULL, badRFBVersionAlert);
        alerted = TRUE;
        return FALSE;
        }
    HUnlock(readBufferHndl);

    strcpy(versionString, RFBVERSIONSTR);
    if (TCPIPWriteTCP(hostIpid, versionString, 12, TRUE, FALSE)) {
	    return FALSE;
        }
    if (toolerror()) {
	   	return FALSE;
        }

    if (! DoWaitingReadTCP(4)) {				/* Read authentication type */
        return FALSE;
        }
    HLock(readBufferHndl);
    switch ((unsigned long) (**readBufferHndl)) {
	    case vncConnectionFailed:	HUnlock(readBufferHndl);
                                    if (! DoWaitingReadTCP(4))
	                                    return FALSE; 
                                    if (toolerror())
	                                    return FALSE;
                                    HLock(readBufferHndl);
                                    reasonLength = SwapBytes4(**readBufferHndl);
                                    HUnlock(readBufferHndl);
                                    if (! DoWaitingReadTCP(reasonLength))
	                                    return FALSE;
                                    if (toolerror())
	                                    return FALSE;  
                                    SetHandleSize(
                                    		GetHandleSize(readBufferHndl)+1,
	                                        readBufferHndl);
                                    if (! toolerror()) {	
                                    	HLock(readBufferHndl);
                                    	*((char *) *readBufferHndl+reasonLength)
                                        		= 0;
                                    	InitCursor();
                                    	AlertWindow(awResource,
                                        		(Pointer) readBufferHndl,
                                				connectionFailedAlert);
                                        alerted = TRUE;
	                                	HUnlock(readBufferHndl);
                                        }
        							return FALSE;
        case vncNoAuthentication:	break;
        case vncVNCAuthentication:	if (DoDES())
								        break;
                                    HUnlock(readBufferHndl);
                                    return FALSE;
        default:					HUnlock(readBufferHndl);
							        AlertWindow(awResource, NULL,
                                    		badAuthTypeAlert);
                                    alerted = TRUE;
        							return FALSE;
        }

    HUnlock(readBufferHndl);
    return TRUE;
    #undef connectionFailedAlert
    #undef badRFBVersionAlert
    #undef badAuthTypeAlert
    }

/**********************************************************************
* DoDES() - Try to do DES (aka VNC) authentication
**********************************************************************/
BOOLEAN DoDES (void) {
	/* This reverses the order of the low 7 bits of a byte. */
	/* Uses the high bit (7) as scratch space. */
	#define SwitchBits(x) do {	x &= 0x7f;				/* Clear 7 */	\
								x ^= (x << 7) & 0x80;	/* 0 -> 7 */	\
                                x &= 0xfe;				/* Clear 0 */	\
                                x ^= (x >> 6) & 0x01;	/* 6 -> 0 */	\
                                x &= 0xbf;				/* Clear 6 */	\
                                x ^= (x >> 1) & 0x40;	/* 7 -> 6 */	\
                                x &= 0x7f;				/* Clear 7 */	\
                                x ^= (x << 6) & 0x80;	/* 1 -> 7 */	\
                                x &= 0xfd;				/* Clear 1 */	\
                                x ^= (x >> 4) & 0x02;	/* 5 -> 1 */	\
                                x &= 0xdf;				/* Clear 5 */	\
                                x ^= (x >> 2) & 0x20;	/* 7 -> 5 */	\
                                x &= 0x7f;				/* Clear 7 */	\
                                x ^= (x << 5) & 0x80;	/* 2 -> 7 */	\
                                x &= 0xfb;				/* Clear 2 */	\
                                x ^= (x >> 2) & 0x04;	/* 4 -> 2 */	\
                                x &= 0xef;				/* Clear 4 */	\
                                x ^= (x >> 3) & 0x10;	/* 7 -> 4 */	\
                                } while (0)
	#define statusOK		SwapBytes4(0)
    #define statusFailed	SwapBytes4(1)
    #define statusTooMany	SwapBytes4(2)
    unsigned char theResponse[16];
    unsigned char theKey[8];
    BOOLEAN success;
    BOOLEAN startedCrypto = FALSE;	/* True if we started CryptoTool */
    Handle dpSpace;
    int i;

    DisplayConnectStatus("\pAuthenticating...", FALSE);

    if (!(CryptoStatus() && !toolerror())) {	/* if Crypto isn't started */
	    startedCrypto = TRUE;
    	LoadOneTool(129, 0x100);			/* load Crypto tool 1.0+ */
		if (toolerror()) {		    	    /* Check that it is available */
			AlertWindow(awResource, NULL, noCryptoError);
        	alerted = TRUE;
	    	return FALSE;
	    	}

   		dpSpace = NewHandle(0x0100, userid(),
    		   	attrLocked|attrFixed|attrNoCross|attrBank, 0x00000000);
    	CryptoStartUp((Word) *dpSpace);
        }

    if (! (DoWaitingReadTCP(16))) {
		success = FALSE;
        goto UnloadCrypto;
        }

    /* Pad password with nulls, as per VNC precedent */
    for (i=vncPassword[0]+1; i<=8; i++)
	    vncPassword[i] = 0;

	/* Flip bits around to be in format expected by CryptoTool */
    for (i=1; i<9; i++)
	    SwitchBits(vncPassword[i]);
		
    /* Shift password to form 56-bit key */
    vncPassword[1] <<= 1;
    vncPassword[1] += (vncPassword[2] & 0x7f) >> 6;
    vncPassword[2] <<= 2;
    vncPassword[2] += (vncPassword[3] & 0x7f) >> 5;
    vncPassword[3] <<= 3;
    vncPassword[3] += (vncPassword[4] & 0x7f) >> 4;
    vncPassword[4] <<= 4;
    vncPassword[4] += (vncPassword[5] & 0x7f) >> 3;
    vncPassword[5] <<= 5;
    vncPassword[5] += (vncPassword[6] & 0x7f) >> 2;
    vncPassword[6] <<= 6;
    vncPassword[6] += (vncPassword[7] & 0x7f) >> 1;
    vncPassword[7] <<= 7;
    vncPassword[7] += vncPassword[8] & 0x7f;

    DESAddParity(theKey, &vncPassword[1]);

    HLock(readBufferHndl);
    DESCipher(theResponse, theKey, *(char **)readBufferHndl, modeEncrypt);
    DESCipher(&theResponse[8], theKey, *(char **)readBufferHndl+8, modeEncrypt);
    HUnlock(readBufferHndl);

    if (TCPIPWriteTCP(hostIpid, theResponse, sizeof(theResponse), TRUE, FALSE))
    	{
	    success = FALSE;
	    goto UnloadCrypto;
        }
    if (toolerror()) {
	   	success = FALSE;
        goto UnloadCrypto;
        }
    if (! (DoWaitingReadTCP(4))) {
	    success = FALSE;    
        goto UnloadCrypto;
        }

    HLock(readBufferHndl);
    if ((**readBufferHndl) == statusOK) {
    	success = TRUE;
        goto UnloadCrypto;
        }
   	else if ((**readBufferHndl) == statusFailed) {
		InitCursor();
        AlertWindow(awResource, NULL, authFailedError);
        alerted = TRUE;
    	success = FALSE;
        goto UnloadCrypto;
        }
   	else if ((**readBufferHndl) == statusTooMany) {
   		InitCursor();
        AlertWindow(awResource, NULL, authTooManyError);
        alerted = TRUE;
    	success = FALSE;
        goto UnloadCrypto;
        }     	
	/* else */
    	success = FALSE;

    UnloadCrypto:

    HUnlock(readBufferHndl);

    if (startedCrypto) {
    	CryptoShutDown();						/* Shut down Crypto tool set */
    	DisposeHandle(dpSpace);
		UnloadOneTool(129);
        }
    return success;

#undef statusOK
#undef statusFailed
#undef statusTooMany
#undef SwitchBits
    }

/**********************************************************************
* FinishVNCHandshaking() - Complete VNC protocol initialization
**********************************************************************/
BOOLEAN FinishVNCHandshaking (void) {
#define screenTooBigError	2010
	unsigned char sharedFlag;
	unsigned long serverNameLen;
    unsigned long encodingInfoSize;
    struct PixelFormat {
		    unsigned char messageType;
            unsigned char padding1;
            unsigned int  padding2;
    		unsigned char bitsPerPixel;
            unsigned char depth;
            unsigned char bigEndianFlag;
            unsigned char trueColorFlag;
            unsigned int  redMax;
            unsigned int  greenMax;
            unsigned int  blueMax;
            unsigned char redShift;
            unsigned char greenShift;
            unsigned char blueShift;
            unsigned char padding3;
            unsigned int  padding4;
            } pixelFormat = { 	
		            0				/* message type - SetPixelFormat */,
                    0,0				/* padding */,
            		8				/* bpp */,
            		8				/* depth */,
                    0				/* big endian flag - irrelevant */,
                    TRUE			/* true color flag */,
                    SwapBytes2(7)	/* red-max */,
                    SwapBytes2(7)	/* green-max */,
                    SwapBytes2(3)	/* blue-max */,
                    0				/* red-shift */,
                    3				/* green-shift */,
                    6				/* blue-shift */,
                    0,0				/* padding */
            	};

	struct Encodings {
		    unsigned char messageType;
            unsigned char padding;
            unsigned int  numberOfEncodings;
            unsigned long firstEncoding;
            unsigned long secondEncoding;
            } encodings = {
		            2,				/* Message Type - SetEncodings */
                    0,				/* padding */
                    0,				/* number of encodings  - set below */
                    SwapBytes4(1),	/* first encoding: CopyRect */
                    SwapBytes4(5)	/* second encoding: Hextile */
                    /* Per the spec, raw encoding is supported even though
                     * it is not listed here explicitly.
                     */
            	};

    DisplayConnectStatus("\pNegotiating protocol options...", FALSE);

    /* ClientInitialisation */
    sharedFlag = !!requestSharedSession;
	if (TCPIPWriteTCP(hostIpid, &sharedFlag, sizeof(sharedFlag), TRUE, FALSE))
    	return FALSE;
    if (toolerror())
	    return FALSE;

    /* ServerInitialisation */
    if (! DoWaitingReadTCP(2))
	    return FALSE;
	HLock(readBufferHndl);
    fbWidth = SwapBytes2(**(unsigned **)readBufferHndl);
	HUnlock(readBufferHndl);
    if (! DoWaitingReadTCP(2))
	    return FALSE;
	HLock(readBufferHndl);
    fbHeight = SwapBytes2(**(unsigned **)readBufferHndl);
	HUnlock(readBufferHndl);

    if ((fbWidth > 16384) || (fbHeight > 16384)) {
        AlertWindow(awResource, NULL, screenTooBigError);
		return FALSE;
        }

    /* Ignore server's pixel format and display name */
    if (! DoWaitingReadTCP(16))
	    return FALSE;
    if (! DoWaitingReadTCP(4))
	    return FALSE;
	HLock(readBufferHndl);
	serverNameLen = SwapBytes4(**(unsigned long **)readBufferHndl);
	HUnlock(readBufferHndl);
    if (! DoWaitingReadTCP(serverNameLen))
	    return FALSE;

    if (TCPIPWriteTCP(hostIpid, &pixelFormat.messageType, sizeof(pixelFormat),
    				TRUE, FALSE))
	    return FALSE;
    if (toolerror())
	    return FALSE;

    if (useHextile) {
	    encodings.numberOfEncodings = SwapBytes2(2);
        encodingInfoSize = sizeof(encodings);
    } else {
	    /* No Hextile */
	    encodings.numberOfEncodings = SwapBytes2(1);
        encodingInfoSize = sizeof(encodings) - 4;
    }

    if (TCPIPWriteTCP(hostIpid, &encodings.messageType, encodingInfoSize,
    				TRUE, FALSE))
	    return FALSE;
    if (toolerror())
	    return FALSE;

        return TRUE;
#undef screenTooBigError
  	}	


/**********************************************************************
* CloseTCPConnection() - Close the TCP connection to the host
**********************************************************************/
void CloseTCPConnection (void) {
	TCPIPCloseTCP(hostIpid);
    WaitCursor();
    DisplayConnectStatus("\pClosing VNC session...", FALSE);
    do {
        TCPIPPoll();
	    TCPIPLogout(hostIpid);
        } while (toolerror() == terrSOCKETOPEN);
    CloseConnectStatusWindow();
    InitCursor();
    }
