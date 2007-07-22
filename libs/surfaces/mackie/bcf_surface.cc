#include "bcf_surface.h"

#include <cmath>

using namespace Mackie;

float BcfSurface::scaled_delta( const ControlState & state, float current_speed )
{
	return state.sign * ( std::pow( float(state.ticks + 1), 2 ) + current_speed ) / 100.0;
}

