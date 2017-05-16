#include "DemoWSHandler.h"

DemoWSHandler::DemoWSHandler(folly::HHWheelTimer* timer)
: WebSocketHandler(timer)
{}

void
DemoWSHandler::onWSMessage(const std::string& payload) noexcept
{
	LOG(INFO) << "Received message : " << payload << '\n';
	sendWSMessage("Here's a reply…");
}

