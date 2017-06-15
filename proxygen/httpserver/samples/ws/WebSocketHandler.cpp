/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "WebSocketHandler.h"

#include <chrono>
#include <folly/io/async/EventBaseManager.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <websocketpp/server.hpp>

#include "WebsocketppProxygenConfig.h"

using namespace proxygen;

using config_type = websocketppConfig;

struct CancellableCallback
{
	CancellableCallback(std::function<void()>&& f) : f(std::move(f)) {}
	void operator()()
	{
		if (cancelled) {
			LOG(INFO) << "cancelled: " << std::addressof(cancelled) << '\n';
		} else {
			f();
		}
	}

	std::function<void()> f;
	bool cancelled{false};
};

WebSocketHandler::WebSocketHandler(folly::HHWheelTimer* timer)
: msg_manager_ptr{new config_type::con_msg_manager_type()}, connection{std::make_shared<websocketpp::connection<config_type>>(true/*isServer*/, "websocketppProxygen"/*userAgent*/, alog, elog, websocketpp::lib::ref(rng))}, timer(timer), eventBase{folly::EventBaseManager::get()->getEventBase()}, headersSent{false}, eomSent{false}
//, processor{true/*isSecure*/, true/*isServer*/, msg_manager_ptr, websocketpp::lib::ref(rng)}
{
}

WebSocketHandler::~WebSocketHandler()
{
	cancelCallback();
}

void
WebSocketHandler::onRequest(std::unique_ptr<HTTPMessage> message) noexcept {
	LOG(INFO) << (void*)this << " onRequest\n";
	size_t capacity = 0;
	const auto& methodString = message->getMethodString();
	const auto& path = message->getPath();
	const std::string httpVersion = " HTTP/1.1\r\n";
	capacity += methodString.length();
	capacity += 1;
	capacity += path.length();
	capacity += httpVersion.length();
	message->getHeaders().forEach([&capacity](const std::string& header, const std::string& val) {
		capacity += header.length() + val.length() + 4/* header: val\r\n */;
	});
	capacity += 2; /* \r\n */
	body_ = folly::IOBuf::create(capacity);
	auto data = body_->writableData();
	memcpy(data, methodString.c_str(), methodString.length());
	data += methodString.length();
	data[0] = ' ';
	++data;
	memcpy(data, path.c_str(), path.length());
	data += path.length();
	memcpy(data, httpVersion.c_str(), httpVersion.length());
	data += httpVersion.length();
	message->getHeaders().forEach([&data](const std::string& header, const std::string& val) mutable {
		memcpy(data, header.c_str(), header.length());
		data += header.length();
		data[0] = ':';
		data[1] = ' ';
		data += 2;
		memcpy(data, val.c_str(), val.length());
		data += val.length();
		data[0] = '\r';
		data[1] = '\n';
		data += 2;
	});
	data[0] = '\r';
	data[1] = '\n';
	body_->append(capacity);
	this->message = std::move(message);
	const_cast<websocketpp::transport::proxygen::request&>(connection->get_request()).m = this->message.get();
	connection->impl = this;
	connection->set_message_handler([this](websocketpp::connection_hdl handler, config_type::message_ptr message) {
		LOG(INFO) << "message FIN = " << message->get_fin() << '\n';
		onWSMessage(message->get_payload());
	});
	connection->start();
	/*WebsocketppProxygenAdapter::RequestAdapter reqAdapter{request.get()};
	auto errorCode = processor.validate_handshake(reqAdapter);
	if (!errorCode) {
		auto negotiation = processor.negotiate_extensions(reqAdapter);
		const auto& errorCode = negotiation.first;
		if (!errorCode) {
			reqAdapter.replace_header("Sec-WebSocket-Extensions", negotiation.second);

			auto response = std::make_unique<proxygen::HTTPMessage>(); 
			response->constructDirectResponse({1, 1}, 101, "Switching Protocols");
			response->getHeaders().add(HTTP_HEADER_UPGRADE, "websocket");
			response->getHeaders().add(HTTP_HEADER_CONNECTION, "Upgrade");

			std::string subprotocol{};
			WebsocketppProxygenAdapter::ResponseAdapter respAdapter{response.get()};
			processor.process_handshake(reqAdapter, subprotocol, respAdapter);

			downstream_->sendHeaders(*response);
		} else {
			LOG(WARNING) << "RejectUpgradeRequest, échec de négociation des extensions.\n";
			proxygen::ResponseBuilder(downstream_).rejectUpgradeRequest();
		}
	} else {
		LOG(WARNING) << "RejectUpgradeRequest, échec de validation du handshake.\n";
		proxygen::ResponseBuilder(downstream_).rejectUpgradeRequest();
	}
	schedulePing();*/
}

void
WebSocketHandler::cancelCallback() noexcept
{
	if (pingPongLifecycleCallback) {
		LOG(INFO) << "cancelCallback: " << std::addressof(pingPongLifecycleCallback->cancelled) << '\n';
		pingPongLifecycleCallback->cancelled = true;
	}
}

void
WebSocketHandler::schedulePing() noexcept
{
	cancelCallback();
	pingPongLifecycleCallback = std::make_shared<CancellableCallback>([this]() {
		this->sendPingFrame();
	});
	LOG(INFO) << "schedulePing: " << std::addressof(pingPongLifecycleCallback->cancelled) << '\n';
	const auto& callback = pingPongLifecycleCallback;
	timer->scheduleTimeoutFn([callback]() {callback->operator()();}, std::chrono::seconds{30});
}

void
WebSocketHandler::schedulePongTimeout() noexcept
{
	cancelCallback();
	pingPongLifecycleCallback = std::make_shared<CancellableCallback>([this]() {
		this->downstream_->sendEOM();
	});
	LOG(INFO) << "schedulePongTimeout: " << std::addressof(pingPongLifecycleCallback->cancelled) << '\n';
	const auto& callback = pingPongLifecycleCallback;
	timer->scheduleTimeoutFn([callback]() {callback->operator()();}, std::chrono::seconds{3});
}

void
WebSocketHandler::sendPingFrame() noexcept
{
	/*LOG(INFO) << "sendPingFrame\n";
	auto egressMsg = msg_manager_ptr->get_message();
	processor.prepare_ping(egressMsg->get_payload(), egressMsg);
	const std::string& headerData = egressMsg->get_header();
	const std::string& payloadData = egressMsg->get_payload();
	downstream_->sendBody(folly::IOBuf::copyBuffer(headerData.c_str(), headerData.length()));
	downstream_->sendBody(folly::IOBuf::copyBuffer(payloadData.c_str(), payloadData.length()));
	schedulePongTimeout();*/
}

void
WebSocketHandler::closeWS(websocketpp::close::status::value code, const std::string& reason) noexcept
{
	// Truncate reason to maximum size allowable in a close frame.
	std::string tr(reason, 0, std::min<size_t>(reason.size(), websocketpp::frame::limits::close_reason_size));
	sendCloseFrame(code, tr, false, websocketpp::close::status::terminal(code));
}

void
WebSocketHandler::sendCloseFrame(websocketpp::close::status::value code, const std::string& reason, bool ack, bool terminal) noexcept
{
	/*
	if (config::silent_close) {
		m_alog.write(log::alevel::devel,"closing silently");
		m_local_close_code = close::status::no_status;
		m_local_close_reason.clear();
	} else if (code != close::status::blank) {
		m_alog.write(log::alevel::devel,"closing with specified codes");
		m_local_close_code = code;
		m_local_close_reason = reason;
	} else if (!ack) {
		m_alog.write(log::alevel::devel,"closing with no status code");
		m_local_close_code = close::status::no_status;
		m_local_close_reason.clear();
	} else if (m_remote_close_code == close::status::no_status) {
		m_alog.write(log::alevel::devel,
				"acknowledging a no-status close with normal code");
		m_local_close_code = close::status::normal;
		m_local_close_reason.clear();
	} else {
		m_alog.write(log::alevel::devel,"acknowledging with remote codes");
		m_local_close_code = m_remote_close_code;
		m_local_close_reason = m_remote_close_reason;
	}
	websocketppConfig::message_type::ptr msg = msg_manager_ptr->get_message();
	processor.prepare_close(m_local_close_code, m_local_close_reason, msg);

	// Messages flagged terminal will result in the TCP connection being dropped
	// after the message has been written. This is typically used when servers
	// send an ack and when any endpoint encounters a protocol error
	msg->set_terminal(terminal);
	*/
}

void
WebSocketHandler::sendWSMessage(const std::string& payload) noexcept
{
	connection->send(payload);
	/*websocketppConfig::message_type::ptr msg = msg_manager_ptr->get_message();
	msg->set_opcode(websocketpp::frame::opcode::text);
	msg->append_payload(payload);
	websocketppConfig::message_type::ptr egressMsg = msg_manager_ptr->get_message();
	processor.prepare_data_frame(msg, egressMsg);
	const std::string& headerData = egressMsg->get_header();
	const std::string& payloadData = egressMsg->get_payload();
	LOG(INFO) << headerData.length() << " : " << payloadData.length() << '\n';
	downstream_->sendBody(folly::IOBuf::copyBuffer(headerData.c_str(), headerData.length()));
	downstream_->sendBody(folly::IOBuf::copyBuffer(payloadData.c_str(), payloadData.length()));*/
}

void
WebSocketHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
	LOG(INFO) << (void*)this << " onBody\n";
  if (body_) {
    body_->prependChain(std::move(body));
  } else {
    body_ = std::move(body);
  }
	if (onBodyHandler) {
		LOG(INFO) << "onBodyHandler" << std::endl;
		onBodyHandler();
	} else {
		LOG(INFO) << "no handler" << std::endl;
	}
	/*LOG(INFO) << "onBody\n";
  if (body_) {
    body_->prependChain(std::move(body));
  } else {
    body_ = std::move(body);
  }
	websocketpp::lib::error_code errorCode;
	size_t bytesConsumed;
	do {
		bytesConsumed = processor.consume(body_->writableData(), body_->length(), errorCode);
		body_->trimStart(bytesConsumed);
		if (body_->length() == 0) {
			body_ = body_->pop();
		}
		if (errorCode) {
			LOG(WARNING) << errorCode << '\n';
		}
	} while (body_ && body_->length() > 0 && bytesConsumed > 0);

	websocketppConfig::message_type::ptr ingressMsg = processor.get_message();
	while (ingressMsg) {
		const auto& op = ingressMsg->get_opcode();
		if (websocketpp::frame::opcode::is_control(op)) {
			LOG(INFO) << "Received control frame : " << op << '\n';
			if (op == websocketpp::frame::opcode::PING) {
				LOG(INFO) << "Ping\n";
				auto msg = msg_manager_ptr->get_message();
				auto egressMsg = msg_manager_ptr->get_message();
				processor.prepare_pong(ingressMsg->get_payload(), egressMsg);
				const std::string& headerData = egressMsg->get_header();
				const std::string& payloadData = egressMsg->get_payload();
				downstream_->sendBody(folly::IOBuf::copyBuffer(headerData.c_str(), headerData.length()));
				downstream_->sendBody(folly::IOBuf::copyBuffer(payloadData.c_str(), payloadData.length()));
			} else if (op == websocketpp::frame::opcode::PONG) {
				LOG(INFO) << "Pong\n";
				schedulePing();
			} else if (op == websocketpp::frame::opcode::CLOSE) {
				LOG(INFO) << "Close\n";
				websocketpp::lib::error_code ec;
				auto remote_close_code = websocketpp::close::extract_code(ingressMsg->get_payload(), ec);
				auto remote_close_reason = websocketpp::close::extract_reason(ingressMsg->get_payload(), ec);
				websocketpp::close::status::value closeCode;
				std::string closeReason;
				if (remote_close_code == websocketpp::close::status::no_status) {
					closeCode = websocketpp::close::status::normal;
				} else {
					closeCode = remote_close_code;
					closeReason = remote_close_reason;
				}
				websocketppConfig::message_type::ptr egressMsg = msg_manager_ptr->get_message();
				processor.prepare_close(closeCode, closeReason, egressMsg);
				const std::string& headerData = egressMsg->get_header();
				const std::string& payloadData = egressMsg->get_payload();
				downstream_->sendBody(folly::IOBuf::copyBuffer(headerData.c_str(), headerData.length()));
				downstream_->sendBody(folly::IOBuf::copyBuffer(payloadData.c_str(), payloadData.length()));
				downstream_->sendEOM();
			} else {
				// got an invalid control opcode
				LOG(WARNING) << "Got control frame with invalid opcode\n";
			}
		} else {
			onWSMessage(ingressMsg->get_payload());
		}
		ingressMsg = processor.get_message();
	}*/
}

void
WebSocketHandler::onEOM() noexcept {
	LOG(INFO) << (void*)this << " onEOM\n";
	if (!eomSent) {
		downstream_->sendEOM();
		eomSent = true;
	}
}

void
WebSocketHandler::onUpgrade(UpgradeProtocol protocol) noexcept {
	LOG(INFO) << (void*)this << " onUpgrade\n";
}

void
WebSocketHandler::requestComplete() noexcept {
	LOG(INFO) << (void*)this << " requestComplete\n";
	delete this;
}

void
WebSocketHandler::onError(ProxygenError err) noexcept {
	LOG(INFO) << "onError: " << err << '\n';
	LOG(INFO) << getErrorString(err);
	delete this;
}

void
WebSocketHandler::init(websocketpp::transport::init_handler handler)
{
	LOG(INFO) << "WebSocketHandler::init\n";
	handler(websocketpp::lib::error_code{});
}

void
WebSocketHandler::async_read_at_least(size_t num_bytes, char *buf, size_t len, websocketpp::transport::read_handler handler)
{
	LOG(INFO) << (void*)this << " WebSocketHandler::async_read_at_least(" << num_bytes << ", " << (void*)buf << ", " << len << ");\n";
	if (body_) {
		auto chainLength = body_->computeChainDataLength();
		LOG(INFO) << "Chain length is: " << chainLength << '\n';
		if (chainLength >= num_bytes) {
			onBodyHandler = nullptr;

			auto bytesMax = std::min(chainLength, len);
			size_t bytesCopied = 0;
			auto dest = buf;
			do {
				auto currentLength = body_->length();
				if (currentLength <= bytesMax) {
					memcpy(dest, body_->data(), currentLength);
					body_ = std::move(body_->pop());
					bytesCopied += currentLength;
				} else {
					memcpy(dest, body_->data(), bytesMax);
					body_->trimStart(bytesMax);
					bytesCopied += bytesMax;
				}
				dest = buf + bytesCopied;
				len -= bytesCopied;
			} while (bytesCopied < num_bytes);

			LOG(INFO) << "Total bytes copied: " << bytesCopied << '\n';
			if (handler) {
				websocketpp::lib::error_code ec;
				LOG(INFO) << "Call read_handler\n";
				handler(ec, bytesCopied);
			}
		} else {
			LOG(INFO) << "Waiting for incoming body\n";
			onBodyHandler = [num_bytes, buf, len, handler, this]() {
				LOG(INFO) << (void*)this << " onBodyHandler called" << std::endl;
				async_read_at_least(num_bytes, buf, len, handler);
			};
		}
	} else {
		LOG(INFO) << "Waiting for incoming body 2\n";
		onBodyHandler = [num_bytes, buf, len, handler, this]() {
			LOG(INFO) << (void*)this << " onBodyHandler 2 called" << std::endl;
			async_read_at_least(num_bytes, buf, len, handler);
		};
	}
}

void
WebSocketHandler::async_shutdown(websocketpp::transport::shutdown_handler handler)
{
	LOG(INFO) << "WebSocketHandler::async_shutdown\n";
	if (!eomSent) {
		downstream_->sendEOM();
		eomSent = true;
	}
	handler(websocketpp::lib::error_code{});
}

void
WebSocketHandler::async_write(std::vector<websocketpp::transport::buffer> & bufs, websocketpp::transport::write_handler handler)
{
	LOG(INFO) << "WebSocketHandler::async_write\n";

	if (headersSent) {
		for (const auto& buf : bufs) {
			downstream_->sendBody(folly::IOBuf::copyBuffer(buf.buf, buf.len, 0, 0));
		}
	} else {
		const auto& response = connection->get_response();
		downstream_->sendHeaders(const_cast<::proxygen::HTTPMessage&>(response.m));
		headersSent = true;
	}
	handler(websocketpp::lib::error_code{});
}

void
WebSocketHandler::dispatch(typename websocketpp::transport::dispatch_handler handler)
{
	LOG(INFO) << "WebSocketHandler::dispatch\n";
	eventBase->runInEventBaseThread([handler]() {
		handler();
	});
}

std::string
WebSocketHandler::get_remote_endpoint()
{
	LOG(INFO) << "WebSocketHandler::get_remote_endpoint()\n";
	return message->getClientAddress().getAddressStr();
}

WebSocketHandler::timer_ptr
WebSocketHandler::set_timer(long duration, websocketpp::transport::timer_handler handler)
{
	LOG(INFO) << "WebSocketHandler::set_timer\n";
	auto callback = std::make_shared<WebSocketHandler::timer_type>(handler);
	timer->scheduleTimeout(callback.get(), std::chrono::milliseconds{duration});
	return callback;
}

