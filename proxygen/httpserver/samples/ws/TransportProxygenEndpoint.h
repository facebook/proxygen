#ifndef TRANSPORT_PROXYGEN_ENDPOINT_H
#define TRANSPORT_PROXYGEN_ENDPOINT_H

#include "TransportProxygenConnection.h"

namespace websocketpp
{
	namespace transport
	{
		namespace proxygen
		{

			template <typename config>
			class endpoint : public config::socket_type
			{
			public:
				using type = endpoint<config>;

				using socket_type = typename config::socket_type;
				using alog_type = typename config::alog_type;
				using elog_type = typename config::elog_type;

				using socket_con_type = typename socket_type::socket_con_type;
				using socket_con_ptr = typename socket_con_type::ptr;

				using transport_con_type = proxygen::connection<config>;
				using transport_con_ptr = typename transport_con_type::ptr;

				using timer_type = typename config::timer_type;
				using timer_ptr = typename config::timer_ptr;
			};

		}
	}
}

#endif

