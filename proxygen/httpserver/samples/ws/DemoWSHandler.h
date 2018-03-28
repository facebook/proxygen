#ifndef DEMO_WS_HANDLER_H
#define DEMO_WS_HANDLER_H

#include "WebSocketHandler.h"

class DemoWSHandler : public WebSocketHandler
{
public:
	explicit DemoWSHandler(folly::HHWheelTimer* timer);

	void onWSMessage(const std::string& payload) noexcept final;
};

#endif

