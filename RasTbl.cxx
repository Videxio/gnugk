//////////////////////////////////////////////////////////////////
//
// bookkeeping for RAS-Server in H.323 gatekeeper
//
// This work is published under the GNU Public License (GPL)
// see file COPYING for details.
//
// History:
//  990500	initial version (Xiang Ping Chen, Rajat Todi, Joe Metzger)
//  990600	ported to OpenH323 V. 1.08 (Jan Willamowius)
//  991003	switched to STL (Jan Willamowius)
//  000215	call removed from table when <=1 ep remains; marked with "towi*1" (towi)
//
//////////////////////////////////////////////////////////////////


#if (_MSC_VER >= 1200)  
#pragma warning( disable : 4800 ) // one performance warning off
#pragma warning( disable : 4786 ) // warning about too long debug symbol off
#define snprintf	_snprintf
#endif

#include <time.h>
#include <ptlib.h>
#include "h323pdu.h"
#include "ANSI.h"
#include "h323util.h"
#include "Toolkit.h"
#include "RasTbl.h"
#include "gk_const.h"
#include "stl_supp.h"


conferenceRec::conferenceRec(const H225_EndpointIdentifier & src, const H225_ConferenceIdentifier & cid, const H225_BandWidth & bw)
{
    m_src = src;
    m_cid = cid;
    m_bw = bw;
}

bool conferenceRec::operator< (const conferenceRec & other) const
{
	return (this->m_cid < other.m_cid);
};

resourceManager::resourceManager()
{
	m_capacity = 0;
};


void resourceManager::SetBandWidth(int bw)
{
	m_capacity = bw;
	cout << endl << "Available BandWidth " << m_capacity  << endl;
}


unsigned int resourceManager::GetAvailableBW(void) const
{
	unsigned int RemainingBW = m_capacity.GetValue();
	std::set<conferenceRec>::const_iterator Iter;

	for (Iter = ConferenceList.begin(); Iter != ConferenceList.end(); ++Iter)
	{
		if( RemainingBW >= (*Iter).m_bw.GetValue() )
			RemainingBW = RemainingBW - (*Iter).m_bw.GetValue();
		else {
			// we have already granted more bandwidth than we have capacity
			// this sould not happen
			// BUG: it happens now, because we count bandwidth twice, if both endpoints are registered with us
			RemainingBW = 0;
			return RemainingBW;
		}
	};
	return RemainingBW;
}

   
BOOL resourceManager::GetAdmission(const H225_EndpointIdentifier & src, const H225_ConferenceIdentifier & cid, const H225_BandWidth & bw)
{
    if( bw.GetValue() > GetAvailableBW() )
		return FALSE;
      
    conferenceRec cRec( src, cid, bw );
	ConferenceList.insert(cRec);
    PTRACE(2, "GK\tTotal sessions : " << ConferenceList.size() << "\tAvailable BandWidth " << GetAvailableBW());
    return TRUE;
}

 
BOOL resourceManager::CloseConference(const H225_EndpointIdentifier & src, const H225_ConferenceIdentifier & cid)
{
	std::set<conferenceRec>::iterator Iter;

	for (Iter = ConferenceList.begin(); Iter != ConferenceList.end(); ++Iter)
	{  
		if( ((*Iter).m_src == src) && ((*Iter).m_cid == cid) )
		{
			ConferenceList.erase(Iter);
    		PTRACE(2, "GK\tTotal sessions : " << ConferenceList.size() << "\tAvailable BandWidth " << GetAvailableBW());
			return TRUE;
		}
	}
	return FALSE;
}


//void EndpointRec::PrintOn( ostream &strm ) const
//{
//    strm << "{";
//    strm << endl << " callSignalAddress ";
//    m_callSignalAddress.PrintOn( strm );
//    strm << endl << " terminalAlias ";
//    m_terminalAliases.PrintOn( strm );
//    strm << endl << " endpointIdentifier ";
//    m_endpointIdentifier.PrintOn( strm );
//    strm << "}" << endl;
//}


EndpointRec::EndpointRec(const H225_RasMessage &completeRRQ, bool Permanent)
      :	m_RasMsg(completeRRQ), m_timeToLive(1), m_usedCount(0)
{
	SetTimeToLive(SoftPBX::TimeToLive);
	if (m_RasMsg.GetTag() == H225_RasMessage::e_registrationRequest) {
		H225_RegistrationRequest & rrq = m_RasMsg;
		if (rrq.m_rasAddress.GetSize() > 0)
			m_rasAddress = rrq.m_rasAddress[0];
		if (rrq.m_callSignalAddress.GetSize() > 0)
			m_callSignalAddress = rrq.m_callSignalAddress[0];
		m_endpointIdentifier = rrq.m_endpointIdentifier;
		m_terminalAliases = rrq.m_terminalAlias;
		m_terminalType = rrq.m_terminalType;
		if (rrq.HasOptionalField(H225_RegistrationRequest::e_timeToLive))
			SetTimeToLive(rrq.m_timeToLive);
		PTRACE(1, "New EP|" << PrintOn(false));
	} else if (m_RasMsg.GetTag() == H225_RasMessage::e_locationConfirm) {
		H225_LocationConfirm & lcf = m_RasMsg;
		m_rasAddress = lcf.m_rasAddress;
		m_callSignalAddress = lcf.m_callSignalAddress;
		if (lcf.HasOptionalField(H225_LocationConfirm::e_destinationInfo))
			m_terminalAliases = lcf.m_destinationInfo;
		if (lcf.HasOptionalField(H225_LocationConfirm::e_destinationType))
			m_terminalType = lcf.m_destinationType;
		m_timeToLive = (SoftPBX::TimeToLive > 0) ? SoftPBX::TimeToLive : 600;
	}
	if (Permanent)
		m_timeToLive = 0;
}	

EndpointRec::~EndpointRec()
{
#ifndef NDEBUG
	PTRACE(3, "remove endpoint: " << (const unsigned char *)m_endpointIdentifier.GetValue() << " " << m_usedCount);
#endif
}

void EndpointRec::SetRasAddress(const H225_TransportAddress &a)
{
	PWaitAndSignal lock(m_usedLock);
	m_rasAddress = a;
}

void EndpointRec::SetEndpointIdentifier(const H225_EndpointIdentifier &i)
{
	PWaitAndSignal lock(m_usedLock);
	m_endpointIdentifier = i;
}
        
void EndpointRec::SetTimeToLive(int seconds)
{
	if (m_timeToLive > 0) {
		PWaitAndSignal lock(m_usedLock);
		m_timeToLive = (SoftPBX::TimeToLive > 0) ?
			std::min(SoftPBX::TimeToLive, seconds) : 0;
	}
}

void EndpointRec::SetAliases(const H225_ArrayOf_AliasAddress &a)
{
	PWaitAndSignal lock(m_usedLock);
        m_terminalAliases = a;
}
        
void EndpointRec::SetEndpointType(const H225_EndpointType &t) 
{
	PWaitAndSignal lock(m_usedLock);
        m_terminalType = t;
}

void EndpointRec::Update(const H225_RasMessage & ras_msg)
{
        if (ras_msg.GetTag() == H225_RasMessage::e_registrationRequest) {
		const H225_RegistrationRequest & rrq = ras_msg;

		SetRasAddress(rrq.m_rasAddress[0]);

		if (rrq.HasOptionalField(H225_RegistrationRequest::e_endpointIdentifier))
			SetEndpointIdentifier(rrq.m_endpointIdentifier);

		if (rrq.HasOptionalField(H225_RegistrationRequest::e_timeToLive))
			SetTimeToLive(rrq.m_timeToLive);

		// H.225.0v4: ignore fields other than rasAddress, endpointIdentifier,
		// timeToLive for a lightweightRRQ
		if (!(rrq.HasOptionalField(H225_RegistrationRequest::e_keepAlive) &&
			rrq.m_keepAlive.GetValue())) {
			if (rrq.HasOptionalField(H225_RegistrationRequest::e_terminalAlias)
				&& (rrq.m_terminalAlias.GetSize() >= 1))
				SetAliases(rrq.m_terminalAlias);
		}
	} else if (ras_msg.GetTag() == H225_RasMessage::e_locationConfirm) {
		const H225_LocationConfirm & lcf = ras_msg;
		SetRasAddress(lcf.m_rasAddress);
		if (lcf.HasOptionalField(H225_LocationConfirm::e_destinationInfo))
			SetAliases(lcf.m_destinationInfo);
	}
	PWaitAndSignal lock(m_usedLock);
	m_updatedTime = PTime();
}

// due to strange bug of gcc, I have to pass pointer instead of reference
bool EndpointRec::CompareAlias(const H225_ArrayOf_AliasAddress *a) const
{
	for (PINDEX i = 0; i < a->GetSize(); i++)
		for (PINDEX j = 0; j < m_terminalAliases.GetSize(); j++)
			if ((*a)[i] == m_terminalAliases[j])
				return true;
	return false;
}

EndpointRec *EndpointRec::Unregister()
{
	SendURQ(H225_UnregRequestReason::e_maintenance);
	return this;
}

EndpointRec *EndpointRec::Expired()
{
	SendURQ(H225_UnregRequestReason::e_ttlExpired);
	return this;
}

void EndpointRec::BuildLCF(H225_LocationConfirm & obj_lcf) const
{
	obj_lcf.m_callSignalAddress = GetCallSignalAddress();
	obj_lcf.m_rasAddress = GetRasAddress();
	obj_lcf.IncludeOptionalField(H225_LocationConfirm::e_destinationInfo);
	obj_lcf.m_destinationInfo = GetAliases();
	obj_lcf.IncludeOptionalField(H225_LocationConfirm::e_destinationType);
	obj_lcf.m_destinationType = GetEndpointType();
}

PString EndpointRec::PrintOn(bool verbose) const
{
	PString msg(PString::Printf, "%s|%s|%s|%s\r\n",
		    (const unsigned char *) AsDotString(GetCallSignalAddress()),
		    (const unsigned char *) AsString(GetAliases()),
		    (const unsigned char *) AsString(GetEndpointType()),
		    (const unsigned char *) GetEndpointIdentifier().GetValue() );
	if (verbose) {
		msg += GetUpdatedTime().AsString();
		if (m_timeToLive == 0)
			msg += " (permanent)";
		msg += "\r\n";
	}
	return msg;
}

bool EndpointRec::SendURQ(H225_UnregRequestReason::Choices reason)
{
	if (GetRasAddress().GetTag() != H225_TransportAddress::e_ipAddress)
		return false;  // no valid ras address

	static int RequestNum = 0;

	H225_RasMessage ras_msg;
	ras_msg.SetTag(H225_RasMessage::e_unregistrationRequest);
	H225_UnregistrationRequest & urq = ras_msg;
	urq.m_requestSeqNum.SetValue(++RequestNum);
	urq.IncludeOptionalField(urq.e_gatekeeperIdentifier);
	urq.m_gatekeeperIdentifier.SetValue( Toolkit::GKName() );
	urq.IncludeOptionalField(urq.e_endpointIdentifier);
	urq.m_endpointIdentifier = GetEndpointIdentifier();
	urq.m_callSignalAddress.SetSize(1);
	urq.m_callSignalAddress[0] = GetCallSignalAddress();
	urq.IncludeOptionalField(H225_UnregistrationRequest::e_reason);
	urq.m_reason.SetTag(reason);

	PString msg(PString::Printf, "URQ|%s|%s|%s;\r\n", 
			(const unsigned char *) AsDotString(GetRasAddress()),
			(const unsigned char *) GetEndpointIdentifier().GetValue(),
			(const unsigned char *) urq.m_reason.GetTagName());
        GkStatus::Instance()->SignalStatus(msg);

	return SendRasPDU(ras_msg, GetRasAddress());
}

void EndpointRec::Lock()
{
	PWaitAndSignal lock(m_usedLock);
	++m_usedCount;
}

void EndpointRec::Unlock()
{
	PWaitAndSignal lock(m_usedLock);
	--m_usedCount;
}

GatewayRec::GatewayRec(const H225_RasMessage &completeRRQ, bool Permanent)
      : EndpointRec(completeRRQ, Permanent), defaultGW(false)
{
	Prefixes.reserve(8);
	GatewayRec::LoadConfig(); // static binding
}

void GatewayRec::SetAliases(const H225_ArrayOf_AliasAddress &a)
{
	EndpointRec::SetAliases(a);
	LoadConfig();
}

void GatewayRec::SetEndpointType(const H225_EndpointType &t)
{
	if (!t.HasOptionalField(H225_EndpointType::e_gateway)) {
		PTRACE(1, "RRJ: terminal type changed|" << (const unsigned char *)m_endpointIdentifier.GetValue());
		return;
	}
	EndpointRec::SetEndpointType(t);
	LoadConfig();
}

void GatewayRec::Update(const H225_RasMessage & ras_msg)
{
        if (ras_msg.GetTag() == H225_RasMessage::e_registrationRequest) {
		const H225_RegistrationRequest & rrq = ras_msg;
		if (!(rrq.HasOptionalField(H225_RegistrationRequest::e_keepAlive) &&
			rrq.m_keepAlive.GetValue()) && (m_terminalType != rrq.m_terminalType)) {
			SetEndpointType(rrq.m_terminalType);
		}
	} else if (ras_msg.GetTag() == H225_RasMessage::e_locationConfirm) {
		const H225_LocationConfirm & lcf = ras_msg;
		if (lcf.HasOptionalField(H225_LocationConfirm::e_destinationType))
			SetEndpointType(lcf.m_destinationType);
	}
			
	EndpointRec::Update(ras_msg);
}

void GatewayRec::AddPrefixes(const H225_ArrayOf_SupportedProtocols &protocols)
{
	for (PINDEX i=0; i < protocols.GetSize(); i++) {
		H225_SupportedProtocols &p = protocols[i];
		if (p.GetTag() == H225_SupportedProtocols::e_voice) {
			H225_VoiceCaps &v = p;
			if (v.HasOptionalField(H225_VoiceCaps::e_supportedPrefixes))
				for (PINDEX s=0; s<v.m_supportedPrefixes.GetSize(); s++) {
					H225_AliasAddress &a = v.m_supportedPrefixes[s].m_prefix;
					if (a.GetTag() == H225_AliasAddress::e_dialedDigits)
						Prefixes.push_back((const char *)AsString(a, false));
				}

		}
	}
}

void GatewayRec::SortPrefixes()
{
	// remove duplicate aliases
	sort(Prefixes.begin(), Prefixes.end(), greater<string>());
	prefix_iterator Iter = unique(Prefixes.begin(), Prefixes.end());
	Prefixes.erase(Iter, Prefixes.end());
	defaultGW = (find_if(Prefixes.begin(), Prefixes.end(), bind2nd(equal_to<string>(), "*")) != Prefixes.end());
}

bool GatewayRec::LoadConfig()
{
	PWaitAndSignal lock(m_usedLock);
	Prefixes.clear();
	if (m_terminalType.m_gateway.HasOptionalField(H225_GatewayInfo::e_protocol))
		AddPrefixes(m_terminalType.m_gateway.m_protocol);
	for (PINDEX i=0; i<m_terminalAliases.GetSize(); i++) {
		PStringArray p = (GkConfig()->GetString("RasSvr::GWPrefixes",
				  H323GetAliasAddressString(m_terminalAliases[i]), "")
				 ).Tokenise(" ,;\t\n", false);
		for (PINDEX s=0; s<p.GetSize(); s++)
			Prefixes.push_back((const char *)p[s]);
	}
	SortPrefixes();
	return true;
}

int GatewayRec::PrefixMatch(const H225_ArrayOf_AliasAddress &a) const
{
	int maxlen = (defaultGW) ? 0 : -1;
	for (PINDEX i = 0; i < a.GetSize(); i++)
		if (a[i].GetTag() == H225_AliasAddress::e_dialedDigits) {
			PString AliasStr = H323GetAliasAddressString(a[i]);
			const_prefix_iterator Iter = Prefixes.begin(), eIter= Prefixes.end();
			while (Iter != eIter) {
				int len = Iter->length();
				if ((maxlen < len) && (strncmp(AliasStr, Iter->c_str(), len)==0)) {
					PTRACE(2, ANSI::DBG << "Gateway " << (const unsigned char *)m_endpointIdentifier.GetValue() << " match " << Iter->c_str() << ANSI::OFF);
					maxlen = len;
				}
				++Iter;
			}
		}
	return maxlen;
}

void GatewayRec::BuildLCF(H225_LocationConfirm & obj_lcf) const
{
	EndpointRec::BuildLCF(obj_lcf);
	if (PINDEX as = Prefixes.size()) {
		obj_lcf.IncludeOptionalField(H225_LocationConfirm::e_supportedProtocols);
		obj_lcf.m_supportedProtocols.SetSize(1);
		H225_SupportedProtocols &protocol = obj_lcf.m_supportedProtocols[0];
		protocol.SetTag(H225_SupportedProtocols::e_voice);
		((H225_VoiceCaps &)protocol).m_supportedPrefixes.SetSize(as);
		const_prefix_iterator Iter = Prefixes.begin();
		for (PINDEX p=0; p < as; ++p, ++Iter)
			H323SetAliasAddress(Iter->c_str(), ((H225_VoiceCaps &)protocol).m_supportedPrefixes[p].m_prefix);
	}
}

PString GatewayRec::PrintOn(bool verbose) const
{
	PString msg = EndpointRec::PrintOn(verbose);
	if (verbose) {
		msg += "Prefixes: ";
		if (Prefixes.size() == 0) {
			msg += "<none>";
		} else {
			string m=Prefixes.front();
			const_prefix_iterator Iter = Prefixes.begin(), eIter= Prefixes.end();
			while (++Iter != eIter)
				m += "," + (*Iter);
			msg += m.c_str();
		}
		msg += "\r\n";
	}
	return msg;
}

OuterZoneEPRec::OuterZoneEPRec(const H225_RasMessage & completeLCF, const H225_EndpointIdentifier &epID) : EndpointRec(completeLCF, false)
{
	m_endpointIdentifier = epID;
	PTRACE(1, "New OZEP|" << PrintOn(false));
}

OuterZoneGWRec::OuterZoneGWRec(const H225_RasMessage & completeLCF, const H225_EndpointIdentifier &epID) : GatewayRec(completeLCF, false)
{
	m_endpointIdentifier = epID;

	const H225_LocationConfirm & obj_lcf = completeLCF;
	if (obj_lcf.HasOptionalField(H225_LocationConfirm::e_supportedProtocols)) {
		AddPrefixes(obj_lcf.m_supportedProtocols);
		SortPrefixes();
	}
	defaultGW = false; // don't let outer zone gateway be default
	PTRACE(1, "New OZGW|" << PrintOn(false));
}


RegistrationTable::RegistrationTable()
{
	recCnt = rand()%9000+1000;

	LoadConfig();
}

RegistrationTable::~RegistrationTable()
{
	ClearTable();
	for_each(RemovedList.begin(), RemovedList.end(), delete_ep);
}

endptr RegistrationTable::InsertRec(H225_RasMessage & ras_msg)
{
	switch (ras_msg.GetTag())
	{
		case H225_RasMessage::e_registrationRequest: {
			H225_RegistrationRequest & rrq = ras_msg;
			if (endptr ep = FindBySignalAdr(rrq.m_callSignalAddress[0])) {
				ep->Update(ras_msg);
				return ep;
			} else
				return InternalInsertEP(ras_msg);
		}
		case H225_RasMessage::e_locationConfirm: {
			H225_LocationConfirm & lcf = ras_msg;
			endptr ep = InternalFind(compose1(bind2nd(equal_to<H225_TransportAddress>(), lcf.m_callSignalAddress), mem_fun(&EndpointRec::GetCallSignalAddress)), &OuterZoneList);
			if (ep) {
				ep->Update(ras_msg);
				return ep;
			} else
				return InternalInsertOZEP(ras_msg);
		}
	}

	PTRACE(1, "RegistrationTable: unable to insert " << ras_msg.GetTagName());
	return endptr(NULL);
}

endptr RegistrationTable::InternalInsertEP(H225_RasMessage & ras_msg)
{
	H225_RegistrationRequest & rrq = ras_msg;
	if (!rrq.HasOptionalField(H225_RegistrationRequest::e_endpointIdentifier)) {
		rrq.IncludeOptionalField(H225_RegistrationRequest::e_endpointIdentifier);
		GenerateEndpointId(rrq.m_endpointIdentifier);
	}
	if (!(rrq.HasOptionalField(H225_RegistrationRequest::e_terminalAlias) && (rrq.m_terminalAlias.GetSize() >= 1))) {
		rrq.IncludeOptionalField(H225_RegistrationRequest::e_terminalAlias);
		GenerateAlias(rrq.m_terminalAlias, rrq.m_endpointIdentifier);
	}

	EndpointRec *ep = rrq.m_terminalType.HasOptionalField(H225_EndpointType::e_gateway) ?
			  new GatewayRec(ras_msg) : new EndpointRec(ras_msg);
	WriteLock lock(listLock);
	EndpointList.push_back(ep);
	return endptr(ep);
}

endptr RegistrationTable::InternalInsertOZEP(H225_RasMessage & ras_msg)
{
	static int ozCnt = 1000; // arbitrary chosen constant
	H225_EndpointIdentifier epID;
	epID = "oz_" + PString(PString::Unsigned, ozCnt++) + endpointIdSuffix;

	H225_LocationConfirm & lcf = ras_msg;
	EndpointRec *ep;
	if (lcf.HasOptionalField(H225_LocationConfirm::e_destinationType) &&
	    lcf.m_destinationType.HasOptionalField(H225_EndpointType::e_gateway))
		ep = new OuterZoneGWRec(ras_msg, epID);
	else
		ep = new OuterZoneEPRec(ras_msg, epID);

	WriteLock lock(listLock);
	OuterZoneList.push_front(ep);
	return endptr(ep);
}

void RegistrationTable::RemoveByEndptr(const endptr & eptr)
{
	EndpointRec *ep = eptr.operator->(); // evil
	WriteLock lock(listLock);
	if (ep) {
		EndpointList.remove(ep);
		RemovedList.push_back(ep);
	}
}

void RegistrationTable::RemoveByEndpointId(const H225_EndpointIdentifier & epId)
{
	WriteLock lock(listLock);
	iterator Iter = find_if(EndpointList.begin(), EndpointList.end(),
			compose1(bind2nd(equal_to<H225_EndpointIdentifier>(), epId),
			mem_fun(&EndpointRec::GetEndpointIdentifier)));
	if (Iter != EndpointList.end()) {
		RemovedList.push_back(*Iter);
		EndpointList.erase(Iter);	// list<> is O(1), slist<> O(n) here
	} else {
	        PTRACE(1, "Warning: RemoveByEndpointId " << epId << " failed.");
	}
}

/*
template<class F> endptr RegistrationTable::InternalFind(const F & FindObject,
	const list<EndpointRec *> *List) const
{
	ReadLock lock(listLock);
	const_iterator Iter = find_if(List->begin(), List->end(), FindObject);
	return endptr((Iter != List->end()) ? *Iter : NULL);
}
*/

endptr RegistrationTable::FindByEndpointId(const H225_EndpointIdentifier & epId) const
{
	return InternalFind(compose1(bind2nd(equal_to<H225_EndpointIdentifier>(), epId),
			mem_fun(&EndpointRec::GetEndpointIdentifier)));
}

endptr RegistrationTable::FindBySignalAdr(const H225_TransportAddress &sigAd) const
{
	return InternalFind(compose1(bind2nd(equal_to<H225_TransportAddress>(), sigAd),
			mem_fun(&EndpointRec::GetCallSignalAddress)));
}


/*
bool GWAliasEqual(H225_AliasAddress GWAlias, H225_AliasAddress OtherAlias)
{
	if(!GWAlias.IsValid()) return FALSE;
	if(!OtherAlias.IsValid()) return FALSE;
	PString GWAliasStr = H323GetAliasAddressString(GWAlias);
	PString OtherAliasStr = H323GetAliasAddressString(OtherAlias);

	if (GWAlias.GetTag() == H225_AliasAddress::e_dialedDigits)
	{
		// for E.164 aliases we only compare the prefix the gateway registered
		// and assume they provide acces to the whole address space
		return (strncmp(GWAliasStr, OtherAliasStr, strlen(GWAliasStr)) == 0);
	}
	else {
	  return (GWAlias == OtherAlias);		
	}
};


void RegistrationTable::AddPrefixes(const PString & NewAliasStr, const PString &prefixes, const PString &flags)
{
	// create new
	PStringArray *prefixArr = new PStringArray(prefixes.Tokenise(" ,;\t\n", FALSE));
	GatewayPrefixes[NewAliasStr] = prefixArr;

	// allow for multiple flags
	PStringArray *flagsArr = new PStringArray(flags.Tokenise(" ,;\t\n", FALSE));
	GatewayFlags[NewAliasStr] = flagsArr;
}


void RegistrationTable::AddAlias(const PString & NewAliasStr)
{
	PString gatewayline = GkConfig()->GetString("RasSvr::GWPrefixes", NewAliasStr, "");
	PString flags = "";

	RemovePrefixes(NewAliasStr);

	const PStringArray gateway = gatewayline.Tokenise(":", FALSE);
	// gateway[0] = prefix, gateway[1] = flags
	if (!gatewayline) {
		if (gateway.GetSize() == 2)
			flags = gateway[1];
		AddPrefixes(NewAliasStr, gateway[0], flags);

		PTRACE(2, ANSI::DBG << "Gateway prefixes for '" << NewAliasStr << "' are now '" << gateway[0] << "'" << ANSI::OFF);
		PTRACE(2, ANSI::DBG << "Gateway flags for '" << NewAliasStr << "' are now '" << flags << "'" << ANSI::OFF);
	}
}


void RegistrationTable::RemovePrefixes(const PString & AliasStr)
{
	// delete old if existing
	PStringArray *prefixArr = GatewayPrefixes[AliasStr];
	if (prefixArr != NULL) {
		delete prefixArr;
		GatewayPrefixes[AliasStr] = NULL;
	}
	PStringArray *flagsArr = GatewayFlags[AliasStr];
	if (flagsArr != NULL) {
		delete flagsArr;
		GatewayFlags[AliasStr] = NULL;
	}
}

void RegistrationTable::RemovePrefixes(const H225_AliasAddress & alias)
{
	if (alias.GetTag() != H225_AliasAddress::e_dialedDigits) 
		return;

	RemovePrefixes(H323GetAliasAddressString(alias));
}
*/

endptr RegistrationTable::FindByAliases(const H225_ArrayOf_AliasAddress & alias) const
{
	return InternalFind(bind2nd(mem_fun(&EndpointRec::CompareAlias), &alias));
}

endptr RegistrationTable::FindEndpoint(const H225_ArrayOf_AliasAddress & alias, bool s)
{
	endptr ep = InternalFindEP(alias, &EndpointList);
	return (ep) ? ep : s ? InternalFindEP(alias, &OuterZoneList) : endptr(NULL);
}

endptr RegistrationTable::InternalFindEP(const H225_ArrayOf_AliasAddress & alias,
	list<EndpointRec *> *List)
{
	endptr ep = InternalFind(bind2nd(mem_fun(&EndpointRec::CompareAlias), &alias), List);
	if (ep) {
		PTRACE(4, "Alias match for EP " << AsDotString(ep->GetCallSignalAddress()));
		return ep;
	}

	int maxlen = 0;
	list<EndpointRec *> GWlist;
	listLock.StartRead();
	const_iterator Iter = List->begin(), IterLast = List->end();
	while (Iter != IterLast) {
		if ((*Iter)->IsGateway()) {
			int len = dynamic_cast<GatewayRec *>(*Iter)->PrefixMatch(alias);
			if (maxlen < len) {
				GWlist.clear();
				maxlen = len;
			}
			if (maxlen == len)
				GWlist.push_back(*Iter);
		}
		++Iter;
	}
	listLock.EndRead();

	if (GWlist.size() > 0) {
		EndpointRec *e = GWlist.front();
		if (GWlist.size() > 1) {
			PTRACE(3, ANSI::DBG << "Prefix apply round robin" << ANSI::OFF);
			WriteLock lock(listLock);
			List->remove(e);
			List->push_back(e);
		}
		PTRACE(4, "Alias match for GW " << AsDotString(e->GetCallSignalAddress()));
		return endptr(e);
	}
	return endptr(NULL);
}


/*
endptr RegistrationTable::FindByPrefix(const H225_AliasAddress & alias)
{
	iterator EPIter;
  
	if (alias.GetTag() != H225_AliasAddress::e_dialedDigits)
		return endptr(NULL);

	// Here is a bug. We find the first prefix, but no the longest one, so we have to fix it.

	iterator EPmax;
	PINDEX maxprefix=0;

	// note that found prefix has equal length to an other.
	// if so, the found endpoint is moves to the end, so we get a round-robin.
	bool maxprefixIsConcurrent = false;

	PString aliasStr = H323GetAliasAddressString(alias);

	// Hmmm... a big lock here! anyone has better solution?
	WriteLock lock(listLock);
	// loop over endpoints
	for (EPIter = EndpointList.begin(); EPIter != EndpointList.end(); ++EPIter)
	{
		endpointRec *ep = *EPIter;
		PINDEX s = ep->GetAliases().GetSize();
		// loop over alias list
		for(PINDEX AliasIndex = 0; AliasIndex < s; ++AliasIndex)
		{
			if ((*EPIter)->IsGateway()) {
				const PString GWAliasStr = H323GetAliasAddressString(ep->GetAliases()[AliasIndex]);

				const PStringArray *prefixes = GatewayPrefixes[GWAliasStr];
				if (prefixes) {
					// try all prefixes
					int max = prefixes->GetSize();
					for (int i=0; i < max; i++) {
						const PString &prefix = (*prefixes)[i];
						if (aliasStr.Find(prefix) == 0) {	// TODO: lack of 'aliasStr.StartsWith(prefix)'
							// found at position 0 => is a prefix
							PINDEX prefixLength = prefix.GetLength();
							if(prefixLength > maxprefix)
							{
								PTRACE(2, ANSI::DBG << "Gateway '" << GWAliasStr << "' prefix '"<<prefix
									<< "' matched for '" << aliasStr << "'" << ANSI::OFF);
								if (maxprefix)
									PTRACE(2, ANSI::DBG << "Prefix '" << prefix
										<< "' is longer than other" << ANSI::OFF);
								maxprefix = prefix.GetLength();
								EPmax = EPIter;
								maxprefixIsConcurrent = false;
							}
							else if (prefixLength == maxprefix) {
								maxprefixIsConcurrent = true;
							}
						}
					}
					// no prefix matched
				}
			}
		}
	}
	// no gw matched with one of its prefixes

	// if prefix is not found
	if (maxprefix == 0)
		return endptr(NULL);

	endpointRec *ep = *EPmax;
	// round-robin
	if(maxprefixIsConcurrent) {
		PTRACE(3, ANSI::DBG << "Prefix apply round robin" << ANSI::OFF);
		EndpointList.erase(EPmax);
		EndpointList.push_back(ep);
	}

	return endptr(ep);
}

*/

void RegistrationTable::GenerateEndpointId(H225_EndpointIdentifier & NewEndpointId)
{
	NewEndpointId = PString(PString::Unsigned, ++recCnt) + endpointIdSuffix;
}


void RegistrationTable::GenerateAlias(H225_ArrayOf_AliasAddress & AliasList, const H225_EndpointIdentifier & endpointId) const
{
	AliasList.SetSize(1);
	H323SetAliasAddress(endpointId, AliasList[0]);
}

void RegistrationTable::PrintAllRegistrations(GkStatus::Client &client, BOOL verbose)
{
	client.WriteString("AllRegistrations\r\n");
	InternalPrint(client, verbose, &EndpointList);
}

void RegistrationTable::PrintAllCached(GkStatus::Client &client, BOOL verbose)
{
	client.WriteString("AllCached\r\n");
	InternalPrint(client, verbose, &OuterZoneList);
}

void RegistrationTable::PrintRemoved(GkStatus::Client &client, BOOL verbose)
{
	client.WriteString("AllRemoved\r\n");
	InternalPrint(client, verbose, &RemovedList);
}

void RegistrationTable::InternalPrint(GkStatus::Client &client, BOOL verbose, list<EndpointRec *> * List)
{
	const_iterator IterLast = List->end();

	ReadLock lock(listLock);
	for (const_iterator Iter = List->begin(); Iter != IterLast; ++Iter) {
		PString msg = "RCF|" + (*Iter)->PrintOn(verbose);
	//	PTRACE(2, msg);
		client.WriteString(msg);
	}
	
	client.WriteString(";\r\n");
}

namespace {

void SetIpAddress(const PString &ipAddress, H225_TransportAddress & address)
{
	address.SetTag(H225_TransportAddress::e_ipAddress);
	H225_TransportAddress_ipAddress & ip = address;
	PIPSocket::Address addr;
	PString ipAddr = ipAddress.Trim();
	PINDEX p=ipAddr.Find(':');
	PIPSocket::GetHostAddress(ipAddr.Left(p), addr);
	ip.m_ip[0] = addr.Byte1();
	ip.m_ip[1] = addr.Byte2();
	ip.m_ip[2] = addr.Byte3();
	ip.m_ip[3] = addr.Byte4();
	ip.m_port = (p != P_MAX_INDEX) ? ipAddr.Mid(p+1).AsUnsigned() : GK_DEF_ENDPOINT_SIGNAL_PORT;
}

} // end of anonymous namespace

void RegistrationTable::LoadConfig()
{
	endpointIdSuffix = GkConfig()->GetString("EndpointIDSuffix", "_endp");

	// Load permanent endpoints
	PStringToString cfgs=GkConfig()->GetAllKeyValues("RasSvr::PermanentEndpoints");
	for (PINDEX i=0; i < cfgs.GetSize(); i++) {
		EndpointRec *ep;
		H225_RasMessage rrq_ras;
		rrq_ras.SetTag(H225_RasMessage::e_registrationRequest);
		H225_RegistrationRequest &rrq = rrq_ras;

		rrq.m_callSignalAddress.SetSize(1);
		SetIpAddress(cfgs.GetKeyAt(i), rrq.m_callSignalAddress[0]);
		// is the endpoint exist?
		if (FindBySignalAdr(rrq.m_callSignalAddress[0])) {
			PTRACE(3, "Endpoint " << AsDotString(rrq.m_callSignalAddress[0]) << " exists, ignore!");
			continue;
		}

		// a permanent endpoint may not support RAS
		// we set an arbitrary address here
		rrq.m_rasAddress.SetSize(1);
		rrq.m_rasAddress[0] = rrq.m_callSignalAddress[0];

		rrq.IncludeOptionalField(H225_RegistrationRequest::e_endpointIdentifier);
		GenerateEndpointId(rrq.m_endpointIdentifier);

//		PString aliases=GkConfig()->GetString("RasSvr::PermanentEndpoints", cfgs[i], "");
		rrq.IncludeOptionalField(rrq.e_terminalAlias);
		PStringArray sp=cfgs.GetDataAt(i).Tokenise(";", FALSE);
		PStringArray aa=sp[0].Tokenise(",", FALSE);
		PINDEX as=aa.GetSize();
		if (as > 0) {
			rrq.m_terminalAlias.SetSize(as);
			for (PINDEX p=0; p<as; p++)
				H323SetAliasAddress(aa[p], rrq.m_terminalAlias[p]);
        	}
		// GatewayInfo
		if (sp.GetSize() > 1) {
			aa=sp[1].Tokenise(",", FALSE);
			as=aa.GetSize();
			if (as > 0) {
				rrq.m_terminalType.IncludeOptionalField(H225_EndpointType::e_gateway);
				rrq.m_terminalType.m_gateway.IncludeOptionalField(H225_GatewayInfo::e_protocol);
				rrq.m_terminalType.m_gateway.m_protocol.SetSize(1);
				H225_SupportedProtocols &protocol=rrq.m_terminalType.m_gateway.m_protocol[0];
				protocol.SetTag(H225_SupportedProtocols::e_voice);
				((H225_VoiceCaps &)protocol).m_supportedPrefixes.SetSize(as);
				for (PINDEX p=0; p<as; p++)
					H323SetAliasAddress(aa[p], ((H225_VoiceCaps &)protocol).m_supportedPrefixes[p].m_prefix);
			}
			ep = new GatewayRec(rrq_ras, true);
                } else {
			rrq.m_terminalType.IncludeOptionalField(H225_EndpointType::e_terminal);
			ep = new EndpointRec(rrq_ras, true);
		}

		PTRACE(2, "Add permanent endpoint " << AsDotString(rrq.m_callSignalAddress[0]));
		WriteLock lock(listLock);
		EndpointList.push_back(ep);
        }

	// Load config for each endpoint
	ReadLock lock(listLock);
	for_each(EndpointList.begin(), EndpointList.end(),
		 mem_fun(&EndpointRec::LoadConfig));
}

void RegistrationTable::ClearTable()
{
	WriteLock lock(listLock);
	// Unregister all endpoints, and move the records into RemovedList
	transform(EndpointList.begin(), EndpointList.end(),
		back_inserter(RemovedList), mem_fun(&EndpointRec::Unregister));
	EndpointList.clear();
	copy(OuterZoneList.begin(), OuterZoneList.end(), back_inserter(RemovedList));
	OuterZoneList.clear();
}

void RegistrationTable::CheckEndpoints()
{
	WriteLock lock(listLock);

	iterator Iter = partition(EndpointList.begin(), EndpointList.end(),
			mem_fun(&EndpointRec::IsUpdated));
#ifdef PTRACING
	if (ptrdiff_t s = distance(Iter, EndpointList.end()))
		PTRACE(2, s << " endpoint(s) expired.");
#endif
	transform(Iter, EndpointList.end(), back_inserter(RemovedList),
		mem_fun(&EndpointRec::Expired));
	EndpointList.erase(Iter, EndpointList.end());

	Iter = partition(OuterZoneList.begin(), OuterZoneList.end(),
		mem_fun(&EndpointRec::IsUpdated));
#ifdef PTRACING
	if (ptrdiff_t s = distance(Iter, OuterZoneList.end()))
		PTRACE(2, s << " outerzone endpoint(s) expired.");
#endif
	copy(Iter, OuterZoneList.end(), back_inserter(RemovedList));
	OuterZoneList.erase(Iter, OuterZoneList.end());

	// Cleanup unused EndpointRec in RemovedList
	Iter = partition(RemovedList.begin(), RemovedList.end(), mem_fun(&EndpointRec::IsUsed));
	for_each(Iter, RemovedList.end(), delete_ep);
	RemovedList.erase(Iter, RemovedList.end());
}

//void RegistrationTable::PrintOn(ostream & strm) const
//{
//	list<endpointRec>::const_iterator Iter;
//
//	for (Iter = EndpointList.begin(); Iter != EndpointList.end(); ++Iter)
//	{
//      	(*Iter).PrintOn( strm );
//	};
//}


EndpointCallRec::EndpointCallRec(H225_TransportAddress callSignalAddress, H225_TransportAddress rasAddress, H225_CallReferenceValue callReference)
  : m_callSignalAddress(callSignalAddress),
	m_rasAddress(rasAddress),
	m_callReference(callReference)
{
}

bool EndpointCallRec::operator< (const EndpointCallRec & other) const
{
	return this->m_callSignalAddress <  other.m_callSignalAddress;
}

CallRec::CallRec()
	: m_startTime(time(NULL)),
	  Calling(NULL),
	  Called(NULL)
{
	m_conferenceIdentifier = "";
	m_callIdentifier.m_guid = "";
	m_CallNumber = 0;
}


CallRec::CallRec(const CallRec & Other)

{
	m_conferenceIdentifier = Other.m_conferenceIdentifier;
	m_callIdentifier = Other.m_callIdentifier;
	m_bandWidth = Other.m_bandWidth;
	m_startTime = Other.m_startTime;
	m_CallNumber = Other.m_CallNumber;
	m_destInfo = Other.m_destInfo;

	// copy EndpointCallrec
	if (Other.Calling == NULL)
		Calling = NULL;
	else
		Calling = new EndpointCallRec(*Other.Calling);

	if (Other.Called == NULL)
		Called = NULL;
	else
		Called = new EndpointCallRec(*Other.Called);
};


CallRec::~CallRec()
{
	// C++ guarantees deleting null pointer is safe
	delete Calling;
	delete Called;
}

CallRec & CallRec::operator=(const CallRec & Other)

{
	if (this == &Other)
		return *this;

	m_conferenceIdentifier = Other.m_conferenceIdentifier;
	m_callIdentifier = Other.m_callIdentifier;
	m_bandWidth = Other.m_bandWidth;
	m_startTime = Other.m_startTime;
	m_CallNumber = Other.m_CallNumber;
	m_destInfo = Other.m_destInfo;

	Calling = NULL;
	Called = NULL;

	// copy EndpointCallRec
	if (Other.Calling)
		Calling = new EndpointCallRec(*Other.Calling);
	if (Other.Called)
		Called = new EndpointCallRec(*Other.Called);

	return *this;
};



bool CallRec::operator< (const CallRec & other) const
{
	return this->m_callIdentifier < other.m_callIdentifier;
};

void CallRec::SetCalling(const EndpointCallRec & NewCalling)
{
	delete Calling;
	Calling = new EndpointCallRec(NewCalling);
};

void CallRec::SetCalled(const EndpointCallRec & NewCalled)
{
	delete Called;
	Called = new EndpointCallRec(NewCalled);
};

void CallRec::SetBandwidth(int Bandwidth)
{
	m_bandWidth.SetValue(Bandwidth);
};

void CallRec::RemoveCalling(void)
{
	delete Calling;
	Calling = NULL;
};

void CallRec::RemoveCalled(void)
{
	delete Called;
	Called = NULL;
};

void CallRec::RemoveAll(void)
{
	RemoveCalled();
	RemoveCalling();
};

int CallRec::CountEndpoints(void) const
{
	int result = 0;
	if(Called != NULL) result++;
	if(Calling != NULL) result++;
	return result;
};


CallTable::CallTable()
{
	m_CallNumber = 1;
}

void CallTable::Insert(const CallRec & NewRec)
{
	PTRACE(3, "CallTable::Insert(CALL)");
	CallList.insert(NewRec);
}

void CallTable::Insert(const EndpointCallRec & Calling, const EndpointCallRec & Called, int Bandwidth, H225_CallIdentifier CallId, H225_ConferenceIdentifier ConfId, const PString &destInfo)
{
	CallRec Call;

	PTRACE(3, "CallTable::Insert(EP,EP) Call No. " << m_CallNumber);
	
	Call.SetCalling(Calling);
	Call.SetCalled(Called);
	Call.SetBandwidth(Bandwidth);
	Call.m_callIdentifier = CallId;
	Call.m_conferenceIdentifier = ConfId;
	Call.m_CallNumber = m_CallNumber++;
	Call.m_destInfo = destInfo;
	Insert(Call);
}

void CallTable::Insert(const EndpointCallRec & Calling, int Bandwidth, H225_CallIdentifier CallId, H225_ConferenceIdentifier ConfId, const PString &destInfo)
{
	CallRec Call;

	PTRACE(3, "CallTable::Insert(EP)");
	
	Call.SetCalling(Calling);
	Call.SetBandwidth(Bandwidth);
	Call.m_callIdentifier = CallId;
	Call.m_conferenceIdentifier = ConfId;
	Call.m_CallNumber = m_CallNumber++;
	Call.m_destInfo = destInfo;
	Insert(Call);
}

void CallTable::RemoveEndpoint(const H225_CallReferenceValue & CallRef)
{
	static PMutex mutex;
	GkProtectBlock _using(mutex); // Auto protect the whole method!
	BOOL hasRemoved = FALSE;
	time_t startTime;

// dirty hack, I hate it...:p
	CallRec theCall;
	std::set<CallRec>::iterator CallIter;
	char callRefString[10];
	sprintf(callRefString, "%u", (unsigned)CallRef.GetValue());

#ifndef NDEBUG
	GkStatus::Instance()->SignalStatus("DEBUG\tremoving callRef:" + PString(callRefString) + "...\n\r", 1);
	PTRACE(5, ANSI::PIN << "DEBUG\tremoving CallRef:" << CallRef << "...\n" << ANSI::OFF);
#endif

	// look at all calls
	CallIter = CallList.begin();
	while(CallIter != CallList.end())
	{
		// look at each endpoint in this call if it has this call reference
		if (((*CallIter).Calling != NULL) &&
			((*CallIter).Calling->m_callReference.GetValue() == CallRef.GetValue()))
		{
			CallRec rec = theCall = *CallIter;
			CallList.erase(CallIter);
			rec.RemoveCalling();
			CallList.insert(rec);
			CallIter = CallList.begin();
			hasRemoved = TRUE;
			startTime = rec.m_startTime;
#ifndef NDEBUG
	GkStatus::Instance()->SignalStatus("DEBUG\tcallRef:" + PString(callRefString) + "found&removed for calling\n\r", 1);
	PTRACE(5, ANSI::PIN << "DEBUG\tCallRef:" << CallRef << "found&removed for calling\n" << ANSI::OFF);
#endif
		}

		if (((*CallIter).Called != NULL) &&
			((*CallIter).Called->m_callReference.GetValue() == CallRef.GetValue()))
		{
			CallRec rec = *CallIter;
			if (theCall.Called == NULL)
				theCall = rec;
			CallList.erase(CallIter);
			rec.RemoveCalled();
			CallList.insert(rec);
			CallIter = CallList.begin();
			hasRemoved = TRUE;
			startTime = rec.m_startTime;
#ifndef NDEBUG
	GkStatus::Instance()->SignalStatus("DEBUG\tcallRef:" + PString(callRefString) + "found&removed for called\n\r", 1);
	PTRACE(5, ANSI::PIN <<"DEBUG\tCallRef:" << CallRef << "found&removed for called...\n" << ANSI::OFF);
#endif
		}
		
		if((*CallIter).CountEndpoints() <= 1)
		{
			CallRec rec = *CallIter;
			CallList.erase(CallIter);
			rec.RemoveAll();
			CallIter = CallList.begin();
			hasRemoved = TRUE;
			startTime = rec.m_startTime;
#ifndef NDEBUG
	GkStatus::Instance()->SignalStatus("DEBUG\tcall completely removed\n\r", 1);
	PTRACE(5, ANSI::PIN << "DEBUG\tcall completely removed\n" << ANSI::OFF);
#endif
		}
		else
			++CallIter;
	}
	if (hasRemoved) {
		struct tm * timeStructStart;
		struct tm * timeStructEnd;
		time_t now;
		double callDuration;

		timeStructStart = gmtime(&startTime);
		if (timeStructStart == NULL) 
			PTRACE(1, "ERROR\t ##################### timeconversion-error(1)!!\n");
		PString startTimeString(asctime(timeStructStart));
		startTimeString.Replace("\n", "");

		now = time(NULL);
		timeStructEnd = gmtime(&now);
		if (timeStructEnd == NULL) 
			PTRACE(1, "ERROR\t ##################### timeconversion-error(2)!!\n");
		PString endTimeString(asctime(timeStructEnd));
		endTimeString.Replace("\n", "");

		PString caller, callee;
		if (theCall.Calling) {
			H225_TransportAddress & addr = theCall.Calling->m_callSignalAddress;
			const endptr rec=RegistrationTable::Instance()->FindBySignalAdr(addr);
			caller = PString(PString::Printf, "%s|%s",
				(const char *) AsString(H225_TransportAddress_ipAddress(addr)),
				(rec) ? (const char *)rec->GetEndpointIdentifier().GetValue() : "");
		}
		if (theCall.Called) {
			H225_TransportAddress & addr = theCall.Called->m_callSignalAddress;
			const endptr rec=RegistrationTable::Instance()->FindBySignalAdr(addr);
			callee = PString(PString::Printf, "%s|%s",
				(const char *) AsString(H225_TransportAddress_ipAddress(addr)),
				(rec) ? (const char *)rec->GetEndpointIdentifier().GetValue() : "");
		}

		callDuration = difftime(now, startTime);
		PString cdrString(PString::Printf, "CDR|%s|%.0f|%s|%s|%s|%s|%s\r\n",
				 callRefString, callDuration,
				 (const char *)startTimeString,
				 (const char *)endTimeString,
				 (const char *)caller, (const char *)callee,
				 (const char *)theCall.m_destInfo);

		GkStatus::Instance()->SignalStatus(cdrString, 1);
		PTRACE(3, cdrString);
	}
#ifndef NDEBUG
	GkStatus::Instance()->SignalStatus("DEBUG\tdone for "  + PString(callRefString) + "...\n\r", 1);
	PTRACE(5, ANSI::PIN << "DEBUG\tdone for " + PString(callRefString) + "...\n" << ANSI::OFF);
#endif
}


const CallRec * CallTable::FindCallRec(const Q931 & m_q931) const
{
	std::set<CallRec>::const_iterator CallIter;
	PObject::Comparison result;


	if (m_q931.HasIE(Q931::UserUserIE)) {
		H225_H323_UserInformation signal;

		PPER_Stream q = m_q931.GetIE(Q931::UserUserIE);
		if ( ! signal.Decode(q) ) {
			PTRACE(4, "GK\tERROR DECODING Q931.UserInformation!");
			return NULL;
		}

		H225_H323_UU_PDU & pdu = signal.m_h323_uu_pdu;
		H225_H323_UU_PDU_h323_message_body & body = pdu.m_h323_message_body;
		H225_Setup_UUIE & setup = body;

		// look at all calls
		for (CallIter = CallList.begin(); CallIter != CallList.end(); ++CallIter)
		{
			// look at each endpoint in this call if it has this call reference
			if ((result = (*CallIter).m_callIdentifier.Compare(setup.m_callIdentifier)) == PObject::EqualTo)
				return &(*CallIter);
		};
	} else
		PTRACE(3, "ERROR\tQ931 has no UUIE!!\n");

	// callIdentifier is optional, so in case we don't find it, look for the
	// CallRec by its callReferenceValue
	H225_CallReferenceValue m_crv = m_q931.GetCallReference();
	return FindCallRec (m_crv);
};


const CallRec * CallTable::FindCallRec(const H225_CallIdentifier & m_callIdentifier) const
{
	std::set<CallRec>::const_iterator CallIter;
	PObject::Comparison result;

	// look at all calls
	for (CallIter = CallList.begin(); CallIter != CallList.end(); ++CallIter)
	{
		// look at each endpoint in this call if it has this call reference
		if ((result = (*CallIter).m_callIdentifier.Compare(m_callIdentifier)) == PObject::EqualTo)
			return &(*CallIter);
	};

	return NULL;
};


const CallRec * CallTable::FindCallRec(const H225_CallReferenceValue & CallRef) const
{
	std::set<CallRec>::const_iterator CallIter;

	// look at all calls
	for (CallIter = CallList.begin(); CallIter != CallList.end(); ++CallIter)
	{
		// look at each endpoint in this call if it has this call reference
		if ((*CallIter).Calling != NULL)
			if ((*CallIter).Calling->m_callReference.GetValue() == CallRef.GetValue())
				return &(*CallIter);
		if ((*CallIter).Called != NULL)
			if ((*CallIter).Called->m_callReference.GetValue() == CallRef.GetValue())
				return &(*CallIter);
	};
	return NULL;
};


const CallRec * CallTable::FindCallRec(PINDEX CallNumber) const
{
	std::set<CallRec>::const_iterator CallIter;

	// look at all calls
	for (CallIter = CallList.begin(); CallIter != CallList.end(); ++CallIter)
	{
		// look at each call if it has this call number
		if (CallIter->m_CallNumber == CallNumber)
			return &(*CallIter);
	};

	return NULL;
};


const CallRec * CallTable::FindBySignalAdr(const H225_TransportAddress & SignalAdr) const
{
	std::set<CallRec>::const_iterator CallIter;

	// look at all calls
	for (CallIter = CallList.begin(); CallIter != CallList.end(); ++CallIter)
	{
		// look at each endpoint in this call if it has this call reference
		if ((*CallIter).Calling != NULL)
			if ((*CallIter).Calling->m_callSignalAddress == SignalAdr)
				return &(*CallIter);
		if ((*CallIter).Called != NULL)
			if ((*CallIter).Called->m_callSignalAddress == SignalAdr)
				return &(*CallIter);
	}
	return NULL;
}

void CallTable::PrintCurrentCalls(GkStatus::Client &client, BOOL verbose) const
{
	static PMutex mutex;
	GkProtectBlock _using(mutex);

	std::set<CallRec>::const_iterator CallIter;
	char MsgBuffer[1024];
	char Val[10];

	client.WriteString("CurrentCalls\r\n");
	for (CallIter = CallList.begin(); CallIter != CallList.end(); ++CallIter)
	{
		const CallRec &Call = (*CallIter);
		strcpy (MsgBuffer, "Call No. ");
		sprintf(Val, "%d", Call.m_CallNumber);
		strcat (MsgBuffer, Val);
		strcat (MsgBuffer, "  CallID");
		for (PINDEX i = 0; i < Call.m_callIdentifier.m_guid.GetDataLength(); i++)
		{
			sprintf(Val, " %02x", Call.m_callIdentifier.m_guid[i]);
			strcat(MsgBuffer, Val);
		}
		strcat(MsgBuffer, "\r\n");
		client.WriteString(PString(MsgBuffer));
		if (Call.Calling)
		{
			client.WriteString(PString(PString::Printf, "ACF|%s\r\n",
				(const char *) AsString(H225_TransportAddress_ipAddress(Call.Calling->m_callSignalAddress))));
		}
		if (Call.Called)
		{
			client.WriteString(PString(PString::Printf, "ACF|%s\r\n",
				(const char *) AsString(H225_TransportAddress_ipAddress(Call.Called->m_callSignalAddress))));
		}
		if (verbose)
		{
			PString from = "?";
			PString to   = "?";
			if (Call.Calling) {
				const endptr e = RegistrationTable::Instance()->FindBySignalAdr(Call.Calling->m_callSignalAddress);
				if (e)
					from = AsString(e->GetAliases(), FALSE);
			}
			if (Call.Called) {
				const endptr e = RegistrationTable::Instance()->FindBySignalAdr(Call.Called->m_callSignalAddress);
				if (e)
					to = AsString(e->GetAliases(), FALSE);
			}
			int bw = Call.m_bandWidth;
			char ctime[100];
#if defined (WIN32)
			strncpy(ctime, asctime(localtime(&(Call.m_startTime))), 100);
#elif  defined (P_SOLARIS)
			asctime_r(localtime(&(Call.m_startTime)), ctime, 100);
#else
			asctime_r(localtime(&(Call.m_startTime)), ctime);
#endif
			sprintf(MsgBuffer, "# %s|%s|%d|%s", (const char*)from, (const char*)to, bw, ctime);
			client.WriteString(PString(MsgBuffer));
		}
	}
	client.WriteString(";\r\n");
}

