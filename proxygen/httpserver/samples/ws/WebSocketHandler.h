/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/Memory.h>
#include <memory>
#include <proxygen/httpserver/RequestHandler.h>
#include <websocketpp/connection.hpp>

#include "WebsocketppProxygenConfig.h"

namespace folly
{
  class EventBase;
}

namespace proxygen {
	class ResponseHandler;
}

struct CancellableCallback;

class WebSocketHandler : public proxygen::RequestHandler, public websocketpp::transport::proxygen::connection_interface<websocketppConfig::transport_config>
{
public:
	friend websocketpp::transport::proxygen::connection<websocketppConfig>;

public:
	explicit WebSocketHandler(folly::HHWheelTimer* timer);
	~WebSocketHandler();

	void init(websocketpp::transport::init_handler handler) override;
	void async_read_at_least(size_t num_bytes, char *buf, size_t len, websocketpp::transport::read_handler handler) override;
	void async_shutdown(websocketpp::transport::shutdown_handler handler) override;
	void async_write(std::vector<websocketpp::transport::buffer> & bufs, websocketpp::transport::write_handler handler) override;
	void dispatch(typename websocketpp::transport::dispatch_handler) override;
	std::string get_remote_endpoint() override;
	timer_ptr set_timer(long duration, websocketpp::transport::timer_handler handler) override;

	void closeWS(websocketpp::close::status::value code, const std::string& reason) noexcept;
	virtual void onWSMessage(const std::string& payload) noexcept = 0;
	void sendWSMessage(const std::string& payload) noexcept;

	void onRequest(std::unique_ptr<proxygen::HTTPMessage> headers) noexcept final;
	void onBody(std::unique_ptr<folly::IOBuf> body) noexcept final;
	void onEOM() noexcept override;
	void onUpgrade(proxygen::UpgradeProtocol proto) noexcept final;
	void requestComplete() noexcept override;
	void onError(proxygen::ProxygenError err) noexcept final;

private:
	void cancelCallback() noexcept;
	void schedulePing() noexcept;
	void schedulePongTimeout() noexcept;
	void sendCloseFrame(websocketpp::close::status::value code, const std::string& reason, bool ack, bool terminal) noexcept;
	void sendPingFrame() noexcept;

private:
	std::unique_ptr<folly::IOBuf> body_;
	std::unique_ptr<proxygen::HTTPMessage> message;
	websocketppConfig::alog_type alog;
	websocketppConfig::elog_type elog;
	websocketppConfig::con_msg_manager_type::ptr msg_manager_ptr;
	websocketppConfig::rng_type rng;
	websocketpp::lib::shared_ptr<websocketpp::connection<websocketppConfig>> connection;
	//websocketpp::processor::hybi13<websocketppConfig> processor;
	std::shared_ptr<CancellableCallback> pingPongLifecycleCallback;
	folly::HHWheelTimer* timer;
	folly::EventBase* eventBase;
	std::function<void()> onBodyHandler;
	bool headersSent;
};

