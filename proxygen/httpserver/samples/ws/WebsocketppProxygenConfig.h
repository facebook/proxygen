#ifndef WEBSOCKETPP_CONFIG_H
#define WEBSOCKETPP_CONFIG_H

#include "TransportProxygenEndpoint.h"
#include "TransportProxygenRequest.h"
#include "TransportProxygenResponse.h"
#include "TransportProxygenSocket.h"
#include "TransportProxygenTimer.h"
#include <websocketpp/common/functional.hpp>
#include <websocketpp/config/core.hpp>
#include <websocketpp/connection_base.hpp>
#include <websocketpp/endpoint_base.hpp>
#include <websocketpp/extensions/permessage_deflate/disabled.hpp>
#include <websocketpp/frame.hpp>
#include <websocketpp/logger/stub.hpp>
#include <websocketpp/message_buffer/message.hpp>
#include <websocketpp/message_buffer/alloc.hpp>
#include <websocketpp/processors/hybi13.hpp>
#include <websocketpp/processors/processor.hpp>
#include <websocketpp/random/none.hpp>

struct websocketppConfig : public websocketpp::config::core {
	using type = websocketppConfig;
	using base = websocketpp::config::core;

	// Concurrency policy
	// typedef websocketpp::concurrency::basic concurrency_type;

	// HTTP Parser Policies
	using request_type = websocketpp::transport::proxygen::request;
	using response_type = websocketpp::transport::proxygen::response;
	using timer_type = websocketpp::transport::proxygen::timer;
	using timer_ptr = websocketpp::lib::shared_ptr<timer_type>;

	// Message Policies
	using message_type = base::message_type;
	using message_ptr = base::message_type::ptr;
	using con_msg_manager_type = base::con_msg_manager_type;
	using endpoint_msg_manager_type = base::endpoint_msg_manager_type;

	/// Logging policies
	using elog_type = websocketpp::log::stub;
	using alog_type = websocketpp::log::stub;

	/// RNG policies
	using rng_type = base::rng_type;

	/// Controls compile time enabling/disabling of thread syncronization
	/// code Disabling can provide a minor performance improvement to single
	/// threaded applications
	//static bool const enable_multithreading = true;

	struct transport_config : public base::transport_config
	{
		using concurrency_type = type::concurrency_type;
		using alog_type = type::alog_type;
		using elog_type = type::elog_type;
		using request_type = type::request_type;
		using response_type = type::response_type;
		using timer_type = type::timer_type;
		using timer_ptr = type::timer_ptr;
		using socket_type = TransportProxygenSocket;
	};

	/// Transport Endpoint Component
	using transport_type = websocketpp::transport::proxygen::endpoint<transport_config>;
};

#endif

