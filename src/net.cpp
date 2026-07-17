#include "net.hpp"

namespace
{
	net::Hooks hooks;

	/* Not copied: string literals or static buffers owned by the network build (see net.hpp). */
	net::Radio the_radio;
	bool radio_set;

	/* Written by the network's threads, read by the loop. A plain word store is atomic on this
	 * core, and a stale read costs nothing worse than one cycle of a stale indicator. */
	bool is_commissioned;
	bool is_link_up;
} // namespace

void net::set_hooks(const Hooks &h)
{
	hooks = h;
}

void net::set_radio(const Radio &r)
{
	the_radio = r;
	radio_set = (r.name != nullptr);
}

const net::Radio *net::radio()
{
	return radio_set ? &the_radio : nullptr;
}

bool net::has_radio()
{
	return radio_set;
}

bool net::can_factory_reset()
{
	return hooks.factory_reset != nullptr;
}

void net::set_commissioned(bool on_network)
{
	is_commissioned = on_network;
}

bool net::commissioned()
{
	return is_commissioned;
}

void net::set_link_up(bool up)
{
	is_link_up = up;
}

bool net::link_up()
{
	return is_link_up;
}

void net::publish_reading(const Reading &r)
{
	/* Unconditionally, and that is a decision (see net.hpp): BTHome is a beacon -- Home
	 * Assistant marks the device unavailable when the adverts stop -- and Matter no-ops an
	 * unchanged attribute write, so neither transport wants a dedup here. */
	if (hooks.reading)
	{
		hooks.reading(r);
	}
}

void net::open_pairing()
{
	if (hooks.pairing_open)
	{
		hooks.pairing_open();
	}
}

void net::factory_reset()
{
	if (hooks.factory_reset)
	{
		hooks.factory_reset();
	}
}
