//////////////////////////////////////////////////////////////////
//
// bookkeeping for RAS-Server in H.323 gatekeeper
//
// This work is published under the GNU Public License (GPL)
// see file COPYING for details.
// We also explicitely grant the right to link this code
// with the OpenH323 library.
//
// History:
// 	990500	initial version (Xiang Ping Chen, Rajat Todi, Joe Metzger)
//	990600	ported to OpenH323 V. 1.08 (Jan Willamowius)
//	991003	switched to STL (Jan Willamowius)
//
//////////////////////////////////////////////////////////////////

#ifndef RASTBL_H
#define RASTBL_H "@(#) $Id$"

#include <list>
#include <vector>
#include <string>
#include "rwlock.h"
#include "singleton.h"

#if (_MSC_VER >= 1200)
#pragma warning( disable : 4786 ) // warning about too long debug symbol off
#pragma warning( disable : 4800 )
#endif

class GkDestAnalysisList;
class USocket;
class CallSignalSocket;
class RasServer;
class Q931;

// Template of smart pointer
// The class T must have Lock() & Unlock() methods
template<class T> class SmartPtr {
public:
	explicit SmartPtr(T *t = 0) : pt(t) { Inc(); }
	SmartPtr(const SmartPtr<T> & p) : pt(p.pt) { Inc(); }
	~SmartPtr() { Dec(); }
	operator bool() const { return pt != 0; }
	T *operator->() const { return pt; }

	bool operator==(const SmartPtr<T> & p) const { return pt == p.pt; }
	bool operator!=(const SmartPtr<T> & p) const { return pt != p.pt; }

	SmartPtr<T> &operator=(const SmartPtr<T> & p) {
		if (pt != p.pt)
			Dec(), pt = p.pt, Inc();
		return *this;
	}

private:
	void Inc() const { if (pt) pt->Lock(); }
	void Dec() const { if (pt) pt->Unlock(); }
	T &operator*();
	T *pt;
};

class EndpointRec
{
public:
	/** Construct internal/outer zone endpoint from the specified RAS message.
		RRQ builds an internal zone endpoint, ARQ, ACF and LCF build outer zone
		endpoints.
	*/
	EndpointRec(
		/// RRQ, ARQ, ACF or LCF that contains a description of the endpoint
		const H225_RasMessage& ras,
		/// permanent endpoint flag
		bool permanent = false
		);

	virtual ~EndpointRec();

	// public interface to access EndpointRec
	const H225_TransportAddress & GetRasAddress() const
	{ return m_rasAddress; }
	const H225_TransportAddress & GetCallSignalAddress() const
	{ return m_callSignalAddress; }
	const H225_EndpointIdentifier & GetEndpointIdentifier() const
	{ return m_endpointIdentifier; }
	const H225_ArrayOf_AliasAddress & GetAliases() const
	{ return m_terminalAliases; }
	const H225_EndpointType & GetEndpointType() const
	{ return *m_terminalType; }
	int GetTimeToLive() const
	{ return m_timeToLive; }
	PIPSocket::Address GetNATIP() const
	{ return m_natip; }
	CallSignalSocket *GetSocket();

	/** checks if the given aliases are prefixes of the aliases which are stored
	    for the endpoint in the registration table. #fullMatch# returns #TRUE# if
	    a full match is found.
	    @returns #TRUE# if a match is found
	 */
        bool PrefixMatch_IncompleteAddress(const H225_ArrayOf_AliasAddress &aliases,
	                                  bool &fullMatch) const;

	virtual void SetRasAddress(const H225_TransportAddress &);
	virtual void SetEndpointIdentifier(const H225_EndpointIdentifier &);
	virtual void SetTimeToLive(int);
	virtual void SetAliases(const H225_ArrayOf_AliasAddress &);
	virtual void SetEndpointType(const H225_EndpointType &);

	virtual void Update(const H225_RasMessage & lightweightRRQ);
	virtual bool IsGateway() const { return false; }

	/** Find if one of the given aliases matches any alias for this endpoint.

		@return
		true if the match has been found, false otherwise.
	*/
	virtual bool CompareAlias(
		/// aliases to be matched (one of them)
		const H225_ArrayOf_AliasAddress* aliases
		) const;

	/** Find if one of the given aliases matches any alias for this endpoint
		and return an index for the matching alias.

		@return
		true if the match has been found, false otherwise.
	*/
	virtual bool MatchAlias(
		/// aliases to be matched (one of them)
		const H225_ArrayOf_AliasAddress& aliases,
		/// filled with an index into aliases for the matching alias (if found)
		int& matchedalias
		) const;

	virtual bool LoadConfig() { return true; } // workaround: VC need a return value

	virtual EndpointRec *Unregister();
	virtual EndpointRec *Expired();

	//virtual void BuildACF(H225_AdmissionConfirm &) const;
	//virtual void BuildLCF(H225_LocationConfirm &) const;

	virtual PString PrintOn(bool verbose) const;

	void SetNAT(bool nat) { m_nat = nat; }
	void SetNATAddress(const PIPSocket::Address &);
	void SetSocket(CallSignalSocket *);

	/** @return
		true if this is a permanent endpoint loaded from the config file entry.
	*/
	bool IsPermanent() const { return m_permanent; }
	bool IsUsed() const;
	bool IsUpdated(const PTime *) const;
	bool IsFromParent() const { return m_fromParent; }
	bool IsNATed() const { return m_nat; }
	bool HasNATSocket() const { return m_natsocket; }
	PTime GetUpdatedTime() const { return m_updatedTime; }

	/** If this Endpoint would be register itself again with all the same data
	 * how would this RRQ would look like? May be implemented with a
	 * built-together-RRQ, but for the moment a stored RRQ.
	 */
	const H225_RasMessage & GetCompleteRegistrationRequest() const
	{ return m_RasMsg; }

	void AddCall();
	void AddConnectedCall();
	void RemoveCall();

	void Lock();
	void Unlock();

	bool SendIRQ();

	// smart pointer for EndpointRec
	typedef SmartPtr<EndpointRec> Ptr;

protected:

	void SetEndpointRec(H225_RegistrationRequest &);
	void SetEndpointRec(H225_AdmissionRequest &);
	void SetEndpointRec(H225_AdmissionConfirm &);
	void SetEndpointRec(H225_LocationConfirm &);

	bool SendURQ(H225_UnregRequestReason::Choices);

	/**This field may disappear sometime when GetCompleteRegistrationRequest() can
	 * build its return value itself.
	 * @see GetCompleteRegistrationRequest()
	 */
	H225_RasMessage m_RasMsg;

	H225_TransportAddress m_rasAddress;
	H225_TransportAddress m_callSignalAddress;
	H225_EndpointIdentifier m_endpointIdentifier;
	H225_ArrayOf_AliasAddress m_terminalAliases;
	H225_EndpointType *m_terminalType;
	int m_timeToLive;   // seconds

	int m_activeCall, m_connectedCall, m_totalCall;
	int m_pollCount, m_usedCount;
	mutable PMutex m_usedLock;

	PTime m_updatedTime;
	bool m_fromParent, m_nat;
	PIPSocket::Address m_natip;
	CallSignalSocket *m_natsocket;
	/// permanent (preconfigured) endpoint flag
	bool m_permanent;

private: // not assignable
	EndpointRec(const EndpointRec &);
	EndpointRec & operator= (const EndpointRec &);
};

typedef EndpointRec::Ptr endptr;


class GatewayRec : public EndpointRec {
public:
	typedef std::vector<std::string>::iterator prefix_iterator;
	typedef std::vector<std::string>::const_iterator const_prefix_iterator;

	GatewayRec(const H225_RasMessage & completeRAS, bool Permanent=false);

	virtual void SetAliases(const H225_ArrayOf_AliasAddress &);
	virtual void SetEndpointType(const H225_EndpointType &);

	virtual void Update(const H225_RasMessage & lightweightRRQ);
	virtual bool IsGateway() const { return true; }
	virtual bool LoadConfig();

	/** Find if at least one of the given aliases matches any prefix
		for this gateway.

		@return
		Length (number of characters) of the match, 0 if no match has been
		found and this is the default gateway, -1 if no match has been found
		and this is not the default gateway.
	*/
	virtual int PrefixMatch(
		/// aliases to be matched (one of them)
		const H225_ArrayOf_AliasAddress& aliases
		) const;

	/** Find if at least one of the given aliases matches any prefix
		for this gateway and return an index of the matched alias.

		@return
		Length (number of characters) of the match, 0 if no match has been
		found and this is the default gateway, -1 if no match has been found
		and this is not the default gateway.
	*/
	virtual int PrefixMatch(
		/// aliases to be matched (one of them)
		const H225_ArrayOf_AliasAddress& aliases,
		/// filled with an index of the matching alias (if found)
		int& matchedalias
		) const;

	//virtual void BuildLCF(H225_LocationConfirm &) const;

	virtual PString PrintOn(bool verbose) const;

	void AddPrefixes(const H225_ArrayOf_SupportedProtocols &);
	void AddPrefixes(const PString &);
	void SortPrefixes();

protected:
	// strange! can't compile in debug mode, anybody know why??
	//vector<PString> Prefixes;
	std::vector<std::string> Prefixes;
	bool defaultGW;
};


class OuterZoneEPRec : public EndpointRec {
public:
	OuterZoneEPRec(const H225_RasMessage & completeRAS, const H225_EndpointIdentifier &);

	virtual EndpointRec *Unregister() { return this; }
	virtual EndpointRec *Expired() { return this; }
};


class OuterZoneGWRec : public GatewayRec {
public:
	OuterZoneGWRec(const H225_RasMessage & completeRAS, const H225_EndpointIdentifier &);

	virtual EndpointRec *Unregister() { return this; }
	virtual EndpointRec *Expired() { return this; }
};


class RegistrationTable : public Singleton<RegistrationTable> {
public:
	typedef std::list<EndpointRec *>::iterator iterator;
	typedef std::list<EndpointRec *>::const_iterator const_iterator;

	RegistrationTable();
	~RegistrationTable();

	void Initialize(GkDestAnalysisList & list) { m_destAnalysisList = &list; }

	endptr InsertRec(H225_RasMessage & rrq, PIPSocket::Address = INADDR_ANY);
	void RemoveByEndptr(const endptr & eptr);
	void RemoveByEndpointId(const H225_EndpointIdentifier & endpointId);

	endptr FindByEndpointId(const H225_EndpointIdentifier & endpointId) const;
	endptr FindBySignalAdr(const H225_TransportAddress &, PIPSocket::Address = INADDR_ANY) const;
	endptr FindOZEPBySignalAdr(const H225_TransportAddress &) const;
	endptr FindByAliases(const H225_ArrayOf_AliasAddress & alias) const;
	endptr FindEndpoint(const H225_ArrayOf_AliasAddress & alias, bool RoundRobin, bool SearchOuterZone = true);

	template<class MsgType> endptr getMsgDestination(const MsgType & msg, unsigned int & reason,
	                                                 bool SearchOuterZone = true)
	{
	  endptr ep;
	  bool ok = getGkDestAnalysisList().getMsgDestination(msg, EndpointList, listLock,
	                                                      ep, reason);
	  if (!ok && SearchOuterZone) {
            ok = getGkDestAnalysisList().getMsgDestination(msg, OuterZoneList, listLock,
	                                                   ep, reason);
	  }
	  return (ok) ? ep : endptr(0);
	}

	void ClearTable();
	void CheckEndpoints();

	void PrintAllRegistrations(USocket *client, BOOL verbose=FALSE);
	void PrintAllCached(USocket *client, BOOL verbose=FALSE);
	void PrintRemoved(USocket *client, BOOL verbose=FALSE);

	PString PrintStatistics() const;

//	void PrintOn( ostream &strm ) const;

	/** Updates Prefix + Flags for all aliases */
	void LoadConfig();

	PINDEX Size() const { return regSize; }

public:
  enum enumGatewayFlags {
                e_SCNType		// "trunk" or "residential"
  };

private:

	endptr InternalInsertEP(H225_RasMessage &);
	endptr InternalInsertOZEP(H225_RasMessage &, H225_LocationConfirm &);
	endptr InternalInsertOZEP(H225_RasMessage &, H225_AdmissionConfirm &);

	void InternalPrint(USocket *, BOOL, std::list<EndpointRec *> *, PString &);
	void InternalStatistics(const std::list<EndpointRec *> *, unsigned & s, unsigned & t, unsigned & g, unsigned & n) const;

	void InternalRemove(iterator);

	template<class F> endptr InternalFind(const F & FindObject) const
	{ return InternalFind(FindObject, &EndpointList); }

	template<class F> endptr InternalFind(const F & FindObject, const std::list<EndpointRec *> *ListToBeFound) const
	{   //  The function body must be put here,
	    //  or the Stupid VC would fail to instantiate it
        	ReadLock lock(listLock);
        	const_iterator Iter(find_if(ListToBeFound->begin(), ListToBeFound->end(), FindObject));
	        return endptr((Iter != ListToBeFound->end()) ? *Iter : 0);
	}

	endptr InternalFindEP(const H225_ArrayOf_AliasAddress & alias, std::list<EndpointRec *> *ListToBeFound, bool);

	void GenerateEndpointId(H225_EndpointIdentifier &);
	void GenerateAlias(H225_ArrayOf_AliasAddress &, const H225_EndpointIdentifier &) const;

	GkDestAnalysisList & getGkDestAnalysisList() { return *m_destAnalysisList; }
	std::list<EndpointRec *> EndpointList;
	std::list<EndpointRec *> OuterZoneList;
	std::list<EndpointRec *> RemovedList;
	int regSize;
	mutable PReadWriteMutex listLock;
	GkDestAnalysisList * m_destAnalysisList;

	// counter to generate endpoint identifier
	// this is NOT the count of endpoints!
	int recCnt, ozCnt;
	PString endpointIdSuffix; // Suffix of the generated Endpoint IDs

	// not assignable
	RegistrationTable(const RegistrationTable &);
	RegistrationTable& operator=(const RegistrationTable &);
};


template<class> class RasPDU;

// record of one active call
class CallRec {
public:
	/// build a new call record from the received ARQ message
	CallRec(
		/// ARQ with call information
		const RasPDU<H225_AdmissionRequest>& arq,
		/// bandwidth occupied by the call
		int bandwidth,
		/// called party's aliases in a string form
		const PString& destInfo
		);

	/// build a new call record from the received Setup message
	CallRec(
		/// Q.931 Setup pdu with call information
		const Q931& q931pdu,
		/// H.225.0 Setup-UUIE pdu with call information
		const H225_Setup_UUIE& setup,
		/// force H.245 routed mode
		bool routeH245,
		/// called party's aliases in a string form
		const PString& destInfo
		);
		
	virtual ~CallRec();

	enum NATType { // who is nated?
		none = 0,
		callingParty = 1,
		calledParty = 2,
		both = 3,
		citronNAT = 4  // caller with Citron NAT Technology?
	};

	PINDEX GetCallNumber() const
	{ return m_CallNumber; }
	const H225_CallIdentifier & GetCallIdentifier() const
	{ return m_callIdentifier; }
	const H225_ConferenceIdentifier & GetConferenceIdentifier() const
	{ return m_conferenceIdentifier; }
	endptr GetCallingParty() const { return m_Calling; }
	endptr GetCalledParty() const { return m_Called; }
	endptr GetForwarder() const { return m_Forwarder; }
	int GetBandwidth() const { return m_bandwidth; }

	/** @return
	    A bit mask with NAT flags for calling and called parties. 
	    See #NATType enum# for more details.
	*/
	int GetNATType() const { return m_nattype; }
	int GetNATType(
		/// filled with NAT IP of the calling party (if nat type is callingParty)
		PIPSocket::Address& callingPartyNATIP, 
		/// filled with NAT IP of the called party (if nat type is calledParty)
		PIPSocket::Address& calledPartyNATIP
		) const;

	CallSignalSocket *GetCallSignalSocketCalled() { return m_calledSocket; }
	CallSignalSocket *GetCallSignalSocketCalling() { return m_callingSocket; }
	const H225_ArrayOf_CryptoH323Token & GetAccessTokens() const { return m_accessTokens; }
	PString GetInboundRewriteId() const { return m_inbound_rewrite_id; }
	PString GetOutboundRewriteId() const { return m_outbound_rewrite_id; }

	void SetCallNumber(PINDEX i) { m_CallNumber = i; }
	void SetCalling(const endptr & NewCalling);
	void SetCalled(const endptr & NewCalled);
	void SetForward(CallSignalSocket *, const H225_TransportAddress &, const endptr &, const PString &, const PString &);
	void SetBandwidth(int bandwidth) { m_bandwidth = bandwidth; }
	void SetSocket(CallSignalSocket *, CallSignalSocket *);
	void SetToParent(bool toParent) { m_toParent = toParent; }
	void SetAccessTokens(const H225_ArrayOf_CryptoH323Token & tokens) { m_accessTokens = tokens; }
	void SetInboundRewriteId(PString id) { m_inbound_rewrite_id = id; }
	void SetOutboundRewriteId(PString id) { m_outbound_rewrite_id = id; }

	void SetConnected();

	void Disconnect(bool = false); // Send Release Complete?
	void RemoveAll();
	void RemoveSocket();
	void SendReleaseComplete(const H225_CallTerminationCause * = 0);
	void BuildDRQ(H225_DisengageRequest &, unsigned reason) const;

	int CountEndpoints() const;

	bool CompareCallId(const H225_CallIdentifier *CallId) const;
	bool CompareCRV(WORD crv) const;
	bool CompareCallNumber(PINDEX CallNumber) const;
	bool CompareEndpoint(const endptr *) const;
	bool CompareSigAdr(const H225_TransportAddress *adr) const;

	bool IsUsed() const { return (m_usedCount != 0); }

	/** @return
		true if the call has been connected - a Connect message
		has been received in gk routed signalling or the call has been admitted
		(ARQ->ACF) in direct signalling. Does not necessary mean
		that the call is still in progress (may have been already disconnected).
	*/
	bool IsConnected() const { return (m_connectTime != 0); }

	bool IsH245Routed() const { return m_h245Routed; }
	bool IsToParent() const { return m_toParent; }
	bool IsForwarded() const { return m_forwarded; }
	bool IsSocketAttached() const { return (m_callingSocket != 0); }

	PString GenerateCDR() const;
	PString PrintOn(bool verbose) const;

	void Lock();
	void Unlock();

	/** @return
		Q.931 ReleaseComplete cause code for the call.
		0 if the disconnect cause could not be determined.
	*/
	unsigned GetDisconnectCause() const;

	/** Set Q.931 ReleaseComplete cause code associated with this call. */
	void SetDisconnectCause(
		unsigned causeCode
		);

	/** Set maximum duration limit (in seconds) for this call */
	void SetDurationLimit(
		long seconds /// duration limit to be set
		);

	/** @return
		Duration limit (in seconds) set for this call.
		0 if call duration is not limited.
	*/
	long GetDurationLimit() const;

	/** This function can be used to determine, if the call has been
		disconnected due to call duration limit excess.

		@return
		true if the call duration limit has been exceeded, false otherwise.
	*/
	bool IsDurationLimitExceeded() const;

	/** @return
		Timestamp (number of seconds since 1st January 1970) for the call creation
		(when this CallRec object has been instantiated).
	*/
	time_t GetCreationTime() const;

	/** @return
		Timestamp (number of seconds since 1st January 1970)
		for the Setup message associated with this call. 0 if Setup
		has not been yet received.
		Meaningful only in GK routed mode.
	*/
	time_t GetSetupTime() const;

	/** Set timestamp for a Setup message associated with this call. */
	void SetSetupTime(
		time_t tm /// timestamp (seconds since 1st January 1970)
		);

	/** @return
		Timestamp (number of seconds since 1st January 1970)
		for the Connect message associated with this call. 0 if Connect
		has not been yet received. If GK is not in routed mode, this is
		timestamp for ACF generated as a response to ARQ.
	*/
	time_t GetConnectTime() const;

	/** Set timestamp for a Connect (or ACF) message associated with this call. */
	void SetConnectTime(
		time_t tm /// timestamp (seconds since 1st January 1970)
		);

	/** @return
		Timestamp (number of seconds since 1st January 1970)
		for the call disconnect event. 0 if call has not been yet disconnected
		or connected.
	*/
	time_t GetDisconnectTime() const;

	/** Set timestamp for a disconnect event for this call. */
	void SetDisconnectTime(
		time_t tm /// timestamp (seconds since 1st January 1970)
		);

	/** @return
		Timestamp for the most recent accounting update event logged for this call.
	*/
	time_t GetLastAcctUpdateTime() const { return m_acctUpdateTime; }

	/** Set timestamp for the most recent accounting update event logged
		for this call.
	*/
	void SetLastAcctUpdateTime(
		const time_t tm /// timestamp of the recent accounting update operation
		)
	{
		m_acctUpdateTime = tm;
	}

	/** Check if:
		- a signalling channel associated with this call is not timed out
		  and the call should be disconnected (removed from CallTable);
		- call duration limit has been exceeded
		- call should be disconnected from other reason

		@return
		true if call is timed out and should be disconnected, false otherwise.
	*/
	bool IsTimeout(
		/// point in time for timeouts to be measured relatively to
		/// (made as a parameter for performance reasons)
		const time_t now
		) const;

	/** @return
		Call duration in seconds. 0 for unconnected calls. Actual
		duration for calls in progress.
	*/
	long GetDuration() const;

	/** @return
		A string that identifies uniquelly this call for accounting
		purposes. This string should be unique across subsequent GK
		start/stop events.
	*/
	PString GetAcctSessionId() const { return m_acctSessionId; }

	/** @return
		A string with ARQ.m_srcInfo for registered endpoints
		and Setup.m_sourceAddress for unregistered endpoints.
		The string has alias type appended (example: '772:dialedDigits')
		and for forwarded calls contains alias of forwarding
		after '=' (example: '772:dialedDigits=775:forward').
	*/
	PString GetSrcInfo() const { return m_srcInfo; }

	/** Set a new address for the calling party signalling channel.
	*/
	void SetSrcSignalAddr(
		const H225_TransportAddress & addr /// new signalling transport address
		);

	/** Set a new address for the called party signalling channel.
	*/
	void SetDestSignalAddr(
		const H225_TransportAddress & addr /// new signalling transport address
		);

	/** Get IP and port for the calling party. It is a signal address
		for registered endpoints and remote signalling socket address
		for unregistered endpoints.

		@return
		true if the address has been retrieved successfully, false otherwise.
	*/
	bool GetSrcSignalAddr(
		PIPSocket::Address& addr, /// will receive the IP address
		WORD& port /// will receive the port number
		) const;

	/** Get IP and port for the called party. It is a signal address
		for registered endpoints and remote signalling socket address
		for unregistered endpoints.

		@return
		true if the address has been retrieved successfully, false otherwise.
	*/
	bool GetDestSignalAddr(
		PIPSocket::Address& addr, /// will receive the IP address
		WORD& port /// will receive the port number
		) const;

	/** @return
		A string with ARQ.m_destinationInfo or ARQ.m_destCallSignalAddress
		or "unknown" for registered endpoints
		and Setup.m_destinationAddress or called endpoint IP address
		for unregistered endpoints.
		The string has alias type appended (example: '772:dialedDigits')
		and for forwarded calls contains alias of forwarding
		after '=' (example: '772:dialedDigits=775:forward').
	*/
	PString GetDestInfo() const { return m_destInfo; }

	/** @return
	    Calling party's aliases, as presented in ARQ or Setup messages.
	    This does not change during the call.
	*/
	const H225_ArrayOf_AliasAddress& GetSourceAddress() const 
		{ return m_sourceAddress; }
		
	/** @return
	    Called party's aliases, as presented in ARQ or Setup messages.
	    This does not change during the call now, but should be fixed
	    to handle gatekeeper call forwarding properly.
	*/
	const H225_ArrayOf_AliasAddress& GetDestinationAddress() const
		{ return m_destinationAddress; }

	/** @return
	    Calling party's number or an empty string, if the number has not been
	    yet determined.
	*/
	PString GetCallingStationId();
	
	/// Set calling party's number
	void SetCallingStationId(
		const PString& id /// Calling-Station-Id
		);
		
	/** @return
	    Called party's number or an empty string, if the number has not been
	    yet determined.
	*/
	PString GetCalledStationId();

	/// Set calling party's number
	void SetCalledStationId(
		const PString& id /// Called-Station-Id
		);

	// smart pointer for CallRec
	typedef SmartPtr<CallRec> Ptr;

private:
	void SendDRQ();
	void InternalSetEP(endptr &, const endptr &);

	CallRec(const CallRec & Other);
	CallRec & operator= (const CallRec & other);

private:
	/// internal call number generated by the gatekeeper
	PINDEX m_CallNumber;
	/// H.323 Call Identifier (identifies this particular call leg)
	H225_CallIdentifier m_callIdentifier;
	/// H.323 Conference Identifier
	H225_ConferenceIdentifier m_conferenceIdentifier;
	/// Call Reference Value for the call
	WORD m_crv;
	/// EndpointRec for the calling party (if it is a registered endpoint)
	/// NOTE: it does not change during CallRec lifetime
	endptr m_Calling;
	/// EndpointRec for the called party (if it is a registered endpoint)
	/// NOTE: it can change during CallRec lifetime
	endptr m_Called;
	/// aliases identifying a calling party (as presented in ARQ or Setup)
	H225_ArrayOf_AliasAddress m_sourceAddress;
	/// aliases identifying a called party (as presented in ARQ or Setup)
	H225_ArrayOf_AliasAddress m_destinationAddress;
	/// calling party aliases in a string form
	PString m_srcInfo;
	/// called party aliases in a string form
	PString m_destInfo;
	/// bandwidth occupied by this call (as declared in ARQ)
	int m_bandwidth;

	PString m_callerAddr, m_callerId;
	PString m_calleeAddr, m_calleeId;
	// rewrite id for inbound leg of call
	PString m_inbound_rewrite_id;
	// rewrite id for outbound leg of call
	PString m_outbound_rewrite_id;

	/// current timeout (or duration limit) for the call
	long m_timeout;
	/// timestamp for call timeout measuring
	time_t m_timer;
	/// timestamp (seconds since 1st January, 1970) for the call creation
	/// (triggered by ARQ or Setup)
	time_t m_creationTime;
	/// timestamp (seconds since 1st January, 1970) for a Setup message reception
	time_t m_setupTime;
	/// timestamp (seconds since 1st January, 1970) for a Connect (routed mode)
	/// or ARQ/ACF (direct mode) message reception
	time_t m_connectTime;
	/// timestamp (seconds since 1st January, 1970) for the call disconnect
	time_t m_disconnectTime;
	/// timestamp for the most recent accounting update event logged for this call
	time_t m_acctUpdateTime;
	/// duration limit (seconds) for this call, 0 means no limit
	long m_durationLimit;
	/// Q.931 release complete cause code
	unsigned m_disconnectCause;
	/// unique accounting session id associated with this call
	PString m_acctSessionId;
	/// signalling transport address of the calling party
	H225_TransportAddress m_srcSignalAddress;
	/// signalling transport address of the called party
	H225_TransportAddress m_destSignalAddress;
	/// calling party's number
	PString m_callingStationId;
	/// called party's number
	PString m_calledStationId;

	CallSignalSocket *m_callingSocket, *m_calledSocket;

	int m_usedCount;
	mutable PMutex m_usedLock, m_sockLock;
	int m_nattype;

	bool m_h245Routed;
	/// the call is routed to this gatekeeper's parent gatekeeper
	bool m_toParent;
	bool m_forwarded;
	endptr m_Forwarder;

	H225_ArrayOf_CryptoH323Token m_accessTokens;
};

typedef CallRec::Ptr callptr;

// all active calls
class CallTable : public Singleton<CallTable>
{
public:
	typedef std::list<CallRec *>::iterator iterator;
	typedef std::list<CallRec *>::const_iterator const_iterator;

	CallTable();
	~CallTable();

	void Insert(CallRec * NewRec);

	// bandwidth management
	void SetTotalBandwidth(int bw);
	bool GetAdmission(int bw);
	bool GetAdmission(int bw, const callptr &);
	int GetAvailableBW() const { return m_capacity; }

	callptr FindCallRec(const H225_CallIdentifier & CallId) const;
	callptr FindCallRec(const H225_CallReferenceValue & CallRef) const;
	callptr FindCallRec(PINDEX CallNumber) const;
	callptr FindCallRec(const endptr &) const;
	callptr FindBySignalAdr(const H225_TransportAddress & SignalAdr) const;

	void ClearTable();
	void CheckCalls(
		RasServer* rassrv // to avoid call RasServer::Instance every second
		);

	void RemoveCall(const H225_DisengageRequest & obj_drq, const endptr &);
	void RemoveCall(const callptr &);

	void PrintCurrentCalls(USocket *client, BOOL verbose=FALSE) const;
	PString PrintStatistics() const;

	void AddForwardedCall(const callptr &);
	endptr IsForwardedCall(const callptr &);

	void LoadConfig();

	PINDEX Size() const { return m_activeCall; }

	/** @return
		ConnectTimeout value (milliseconds).
	*/
	long GetConnectTimeout() const { return m_connectTimeout; }

	/** @return
		Default call duration limit value (seconds).
	*/
	long GetDefaultDurationLimit() const { return m_defaultDurationLimit; }

private:
	template<class F> callptr InternalFind(const F & FindObject) const
	{
        	ReadLock lock(listLock);
        	const_iterator Iter(find_if(CallList.begin(), CallList.end(), FindObject));
	        return callptr((Iter != CallList.end()) ? *Iter : 0);
	}

	bool InternalRemovePtr(CallRec *call);
	void InternalRemove(const H225_CallIdentifier & CallId);
	void InternalRemove(WORD CallRef);
	void InternalRemove(iterator);

	void InternalStatistics(unsigned & n, unsigned & act, unsigned & nb, unsigned & np, PString & msg, BOOL verbose) const;

	std::list<CallRec *> CallList;
	std::list<CallRec *> RemovedList;

	bool m_genNBCDR;
	bool m_genUCCDR;

	PINDEX m_CallNumber;
	mutable PReadWriteMutex listLock;

	int m_capacity;

	// statistics
	unsigned m_CallCount, m_successCall, m_neighborCall, m_parentCall, m_activeCall;

	/// timeout for a Connect message to be received
	/// and for a signalling channel to be opened after ACF/ARQ
	/// (0 if GK is not in routed mode)
	long m_connectTimeout;
	/// default call duration limit read from the config
	long m_defaultDurationLimit;
	/// default interval (seconds) for accounting updates to be logged
	long m_acctUpdateInterval;

	CallTable(const CallTable &);
	CallTable& operator==(const CallTable &);
};

// inline functions of EndpointRec
inline bool EndpointRec::IsUsed() const
{
//	PWaitAndSignal lock(m_usedLock);
	return (m_activeCall > 0 || m_usedCount > 0);
}

inline bool EndpointRec::IsUpdated(const PTime *now) const
{
	return (!m_timeToLive || (*now - m_updatedTime).GetSeconds() < m_timeToLive);
}

inline void EndpointRec::AddCall()
{
	PWaitAndSignal lock(m_usedLock);
	++m_activeCall, ++m_totalCall;
}

inline void EndpointRec::AddConnectedCall()
{
	PWaitAndSignal lock(m_usedLock);
	++m_connectedCall;
}

inline void EndpointRec::RemoveCall()
{
	PWaitAndSignal lock(m_usedLock);
	--m_activeCall;
}

inline void EndpointRec::Lock()
{
	PWaitAndSignal lock(m_usedLock);
	++m_usedCount;
}

inline void EndpointRec::Unlock()
{
	PWaitAndSignal lock(m_usedLock);
	--m_usedCount;
}

// inline functions of CallRec
inline void CallRec::Lock()
{
	PWaitAndSignal lock(m_usedLock);
	++m_usedCount;
}

inline void CallRec::Unlock()
{
	PWaitAndSignal lock(m_usedLock);
	--m_usedCount;
}

inline bool CallRec::CompareCallId(const H225_CallIdentifier *CallId) const
{
	return (m_callIdentifier == *CallId);
}

inline bool CallRec::CompareCRV(WORD crv) const
{
	return m_crv == (crv & 0x7fffu);
}

inline bool CallRec::CompareCallNumber(PINDEX CallNumber) const
{
	return (m_CallNumber == CallNumber);
}

inline bool CallRec::CompareEndpoint(const endptr *ep) const
{
	return (m_Calling && m_Calling == *ep) || (m_Called && m_Called == *ep);
}

inline bool CallRec::CompareSigAdr(const H225_TransportAddress *adr) const
{
	return (m_Calling && m_Calling->GetCallSignalAddress() == *adr) ||
		(m_Called && m_Called->GetCallSignalAddress() == *adr);
}

inline long CallRec::GetDurationLimit() const
{
	return m_durationLimit;
}

inline time_t CallRec::GetCreationTime() const
{
	return m_creationTime;
}

inline time_t CallRec::GetSetupTime() const
{
	return m_setupTime;
}

inline time_t CallRec::GetConnectTime() const
{
	return m_connectTime;
}

inline time_t CallRec::GetDisconnectTime() const
{
	return m_disconnectTime;
}

inline unsigned CallRec::GetDisconnectCause() const
{
	return m_disconnectCause;
}

inline void CallRec::SetDisconnectCause( unsigned causeCode )
{
	// set the cause only if it has not been already set
	if( m_disconnectCause == 0 )
		m_disconnectCause = causeCode;
}

inline bool CallRec::IsTimeout(const time_t now) const
{
	return m_timeout > 0 && now >= m_timer && (now - m_timer) >= m_timeout;
}

#endif // RASTBL_H
