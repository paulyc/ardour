#ifndef mackie_surface_bcf_h
#define mackie_surface_bcf_h
/*
	Initially generated by scripts/generate-surface.rb
*/

#include "surface.h"

namespace Mackie
{

class MackieButtonHandler;

class BcfSurface : public Surface
{
public:
	BcfSurface( uint32_t max_strips ) : Surface( max_strips )
	{
	}
	
	virtual void handle_button( MackieButtonHandler & mbh, ButtonState bs, Button & button );
	virtual void init_controls();
	
	virtual float scrub_scaling_factor() { return 50.0; }
	virtual float scaled_delta( const ControlState & state, float current_speed );
};

}

#endif
