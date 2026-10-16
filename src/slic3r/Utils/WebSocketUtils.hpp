#ifndef slic3r_Utils_WebSocketUtils_hpp_
#define slic3r_Utils_WebSocketUtils_hpp_

#include <boost/beast/websocket.hpp>

namespace Slic3r {

template<class WebSocketStream>
void enable_websocket_permessage_deflate(WebSocketStream& ws)
{
    boost::beast::websocket::permessage_deflate pmd;
    pmd.client_enable      = true;
    pmd.compLevel          = 6;
    pmd.memLevel           = 4;
    pmd.msg_size_threshold = 1024;
    ws.set_option(pmd);
}

} // namespace Slic3r

#endif // slic3r_Utils_WebSocketUtils_hpp_
