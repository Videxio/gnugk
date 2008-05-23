//////////////////////////////////////////////////////////////////
//
// MakeCall.h
//
// This work is published under the GNU Public License (GPL)
// see file COPYING for details.
// We also explicitely grant the right to link this code
// with the OpenH323 library.
//
// initial author: Jan Willamowius
//
//////////////////////////////////////////////////////////////////

#ifndef _MakeCall_H
#define _MakeCall_H

#include <ptlib.h>
#include <h323.h>
#include "singleton.h"
#include "config.h"
#include <map>
using namespace std;

class MakeCallEndPoint : public Singleton<MakeCallEndPoint>, public H323EndPoint
{
public:
    MakeCallEndPoint();

    // overrides from H323EndPoint
    virtual PBoolean OnIncomingCall(H323Connection &, const H323SignalPDU &, H323SignalPDU &);
    virtual PBoolean OnConnectionForwarded(H323Connection &, const PString &, const H323SignalPDU &);
    virtual void OnConnectionEstablished(H323Connection & connection, const PString & token);
    virtual PBoolean OpenAudioChannel(H323Connection &, PBoolean, unsigned, H323AudioCodec &);
	virtual void OnRegistrationConfirm();
	virtual void OnRegistrationReject();

	virtual void ThirdPartyMakeCall(PString & user1, PString & user2);
	virtual PBoolean GatekeeperIsRegistered(void);

protected:    
    void AddDestination(PString token, PString alias);
	// get and remove destination from list
    PString GetDestination(PString token);

    PMutex destinationMutex;
    std::map<PString, PString> destinations; 

	PBoolean useH450Transfer;
	PBoolean isRegistered;
};

#endif  // _MakeCall_H

