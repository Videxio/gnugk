//////////////////////////////////////////////////////////////////
//
// pwlib_compat.h PWLib compatibility header
//
// This work is published under the GNU Public License (GPL)
// see file COPYING for details.
// We also explicitely grant the right to link this code
// with the OpenH323 library.
//
//////////////////////////////////////////////////////////////////


#ifndef PWLIB_COMPAT_H
#define PWLIB_COMPAT_H "@(#) $Id$"

#include "ptbuildopts.h"
#include "openh323buildopts.h"

// use at least PWLib Pandora
#if PWLIB_MAJOR == 1
  #if PWLIB_MINOR < 7
    #if PWLIB_BUILD < 5
      #error "PWLib too old, use at least 1.7.5.2"
    #endif
  #endif
#endif

// define PTimedMutex for PWLib < 1.9.2
#if PWLIB_MAJOR == 1
  #if PWLIB_MINOR < 9
    #define PTimedMutex PMutex
  #endif
#endif
#if PWLIB_MAJOR == 1
  #if PWLIB_MINOR == 9
    #if PWLIB_BUILD < 2
      #define PTimedMutex PMutex
    #endif
  #endif
#endif

///////////////////////////////////////////////

// OpenH323 version matching
#if OPENH323_MAJOR == 1
	#if OPENH323_MINOR >= 19
	  #ifdef P_DNS
			#define hasSRV 1	// DNS SRV
	  #endif
	  #define OpenH323Factory 1 // OpenH323 Factory Loader Auth
	  #if OPENH323_BUILD > 1
		#ifdef H323_H460
			#define hasH460 1	// H460 support
		#endif
		#define h323v6 1		// Version 6 features
      #endif
	#endif
#endif

#endif
