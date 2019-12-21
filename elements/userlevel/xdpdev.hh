#pragma once
#include <click/config.h>
#include <click/error.hh>

class XDPDev {

  protected:

    inline int handle_mode(std::string mode, ErrorHandler *errh)
    {

      if (mode == "drv") {
        _xdp_flags |= XDP_FLAGS_DRV_MODE;
      }
      else if(mode == "skb") {
        _xdp_flags |= XDP_FLAGS_SKB_MODE;
      }
      else if(mode == "copy") {
        _xdp_flags |= XDP_FLAGS_SKB_MODE;
      }
      else {
        errh->error("invalid mode \"%s\" must be (drv|skb|copy)", mode.c_str());
        return CONFIGURE_FAIL;
      }

      return CONFIGURE_SUCCESS;

    }

    std::string _dev,
                _prog;

    u16 _xdp_flags{XDP_FLAGS_UPDATE_IF_NOEXIST},
        _bind_flags{0};

};
