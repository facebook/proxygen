#ifndef TRANSPORT_PROXYGEN_SOCKET_H
#define TRANSPORT_PROXYGEN_SOCKET_H

#include <websocketpp/common/memory.hpp>

class TransportProxygenSocketConnection : public websocketpp::lib::enable_shared_from_this<TransportProxygenSocketConnection>
{
public:
	using ptr = websocketpp::lib::shared_ptr<TransportProxygenSocketConnection>;

public:
	ptr get_shared()
	{
		return shared_from_this();
	}
};

class TransportProxygenSocket
{
public:
	using socket_con_type = TransportProxygenSocketConnection;
};

#endif

