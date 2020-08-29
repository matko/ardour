/*
 * Copyright (C) 2020 Luciano Iam <lucianito@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "ardour/dB.h"
#include "ardour/meter.h"
#include "ardour/plugin_insert.h"
#include "ardour/session.h"
#include "pbd/controllable.h"

#include "mixer.h"

using namespace ARDOUR;

ArdourMixerPlugin::ArdourMixerPlugin (boost::shared_ptr<ARDOUR::PluginInsert> insert)
	: _insert (insert)
	, _connections (boost::shared_ptr<PBD::ScopedConnectionList> (new PBD::ScopedConnectionList()))
{}

boost::shared_ptr<ARDOUR::PluginInsert>
ArdourMixerPlugin::insert () const
{
	return _insert;
}

bool
ArdourMixerPlugin::enabled () const
{
	insert ()->enabled ();
}

void
ArdourMixerPlugin::set_enabled (bool enabled)
{
	insert ()->enable (enabled);
}

TypedValue
ArdourMixerPlugin::param_value (uint32_t param_n)
{
	boost::shared_ptr<ARDOUR::AutomationControl> control = param_control (param_n);
	TypedValue value = TypedValue ();

	if (control) {
		ParameterDescriptor pd = control->desc ();

		if (pd.toggled) {
			value = TypedValue (static_cast<bool> (control->get_value ()));
		} else if (pd.enumeration || pd.integer_step) {
			value = TypedValue (static_cast<int> (control->get_value ()));
		} else {
			value = TypedValue (control->get_value ());
		}
	}

	return value;
}

void
ArdourMixerPlugin::set_param_value (uint32_t param_n, TypedValue value)
{
	boost::shared_ptr<AutomationControl> control = param_control (param_n);

	if (control) {
		ParameterDescriptor pd = control->desc ();
		double              dbl_val;

		if (pd.toggled) {
			dbl_val = static_cast<double> (static_cast<bool> (value));
		} else if (pd.enumeration || pd.integer_step) {
			dbl_val = static_cast<double> (static_cast<int> (value));
		} else {
			dbl_val = static_cast<double> (value);
		}

		control->set_value (dbl_val, PBD::Controllable::NoGroup);
	}
}

boost::shared_ptr<ARDOUR::AutomationControl>
ArdourMixerPlugin::param_control (uint32_t param_n) const
{
	bool                      ok         = false;
	boost::shared_ptr<Plugin> plugin     = _insert->plugin ();
	uint32_t                  control_id = plugin->nth_parameter (param_n, ok);

	if (!ok || !plugin->parameter_is_input (control_id)) {
		throw ArdourMixerNotFoundException("invalid automation control");
	}

	return _insert->automation_control (Evoral::Parameter (PluginAutomation, 0, control_id));
}

ArdourMixerStrip::ArdourMixerStrip (boost::shared_ptr<ARDOUR::Stripable> stripable)
	: _stripable (stripable)
	, _connections (boost::shared_ptr<PBD::ScopedConnectionList> (new PBD::ScopedConnectionList()))
{
	if (_stripable->presentation_info ().flags () & ARDOUR::PresentationInfo::VCA) {
		return;
	}

	boost::shared_ptr<Route> route = boost::dynamic_pointer_cast<Route> (_stripable);

	if (!route) {
		return;
	}

	for (uint32_t plugin_n = 0;; ++plugin_n) {
		boost::shared_ptr<Processor> processor = route->nth_plugin (plugin_n);

		if (processor) {
			boost::shared_ptr<PluginInsert> insert = boost::static_pointer_cast<PluginInsert> (processor);

			if (insert) {
				ArdourMixerPlugin plugin (insert);
				_plugins.push_back (plugin);
			}
		}
	}
}

boost::shared_ptr<ARDOUR::Stripable>
ArdourMixerStrip::stripable () const
{
	return _stripable;
}

boost::shared_ptr<PBD::ScopedConnectionList>
ArdourMixerStrip::connections () const
{
	return _connections;
}

int
ArdourMixerStrip::plugin_count () const
{
	return _plugins.size ();
}

ArdourMixerPlugin&
ArdourMixerStrip::nth_plugin (uint32_t plugin_n)
{
	if (plugin_n < _plugins.size ()) {
		return _plugins[plugin_n];
	}

	throw ArdourMixerNotFoundException (""/*"Plugin with ID " + plugin_n + " not found"*/);
}

double
ArdourMixerStrip::gain () const
{
	return to_db (_stripable->gain_control ()->get_value ());
}

void
ArdourMixerStrip::set_gain (double db)
{
	_stripable->gain_control ()->set_value (from_db (db), PBD::Controllable::NoGroup);
}

double
ArdourMixerStrip::pan () const
{
	boost::shared_ptr<AutomationControl> ac = _stripable->pan_azimuth_control ();
	if (!ac) {
		/* TODO: inform GUI that strip has no panner */
		return 0;
	}
	return ac->internal_to_interface (ac->get_value ());
}

void
ArdourMixerStrip::set_pan (double value)
{
	boost::shared_ptr<AutomationControl> ac = _stripable->pan_azimuth_control ();
	if (!ac) {
		return;
	}
	ac->set_value (ac->interface_to_internal (value), PBD::Controllable::NoGroup);
}

bool
ArdourMixerStrip::mute () const
{
	return _stripable->mute_control ()->muted ();
}

void
ArdourMixerStrip::set_mute (bool mute)
{
	_stripable->mute_control ()->set_value (mute ? 1.0 : 0.0, PBD::Controllable::NoGroup);
}

float
ArdourMixerStrip::meter_level_db () const
{
	boost::shared_ptr<PeakMeter> meter = _stripable->peak_meter ();
	return meter ? meter->meter_level (0, MeterMCP) : -193;
}

std::string
ArdourMixerStrip::name () const
{
	return _stripable->name ();
}

void
ArdourMixerStrip::on_drop_plugin (uint32_t)
{
	//uint32_t key = (strip_n << 16) | plugin_n;	
	//_plugin_connections[key]->drop_connections ();
	//_plugin_connections.erase (key);
}

double
ArdourMixerStrip::to_db (double k)
{
	if (k == 0) {
		return -std::numeric_limits<double>::infinity ();
	}

	float db = accurate_coefficient_to_dB (static_cast<float> (k));

	return static_cast<double> (db);
}

double
ArdourMixerStrip::from_db (double db)
{
	if (db < -192) {
		return 0;
	}

	float k = dB_to_coefficient (static_cast<float> (db));

	return static_cast<double> (k);
}

int
ArdourMixer::start ()
{
	/* take an indexed snapshot of current strips */
	StripableList strips;
	session ().get_stripables (strips, PresentationInfo::AllStripables);
	uint32_t strip_n = 0;

	for (StripableList::iterator it = strips.begin (); it != strips.end (); ++it) {
		ArdourMixerStrip strip (*it);
		//(*it)->DropReferences.connect (_connections, MISSING_INVALIDATOR,
		//							boost::bind (&ArdourMixer::on_drop_strip, this, strip_n), event_loop ());
		_strips.push_back (strip);
		strip_n++;
	}

	return 0;
}

int
ArdourMixer::stop ()
{
	_strips.clear ();
	return 0;
}

uint32_t
ArdourMixer::strip_count () const
{
	return _strips.size ();
}

ArdourMixerStrip&
ArdourMixer::nth_strip (uint32_t strip_n)
{
	if (strip_n < _strips.size ()) {
		return _strips[strip_n];
	}

	throw ArdourMixerNotFoundException (""/*"Strip with ID " + strip_n + " not found"*/);
}

void
ArdourMixer::on_drop_strip (uint32_t strip_n)
{
	/*for (uint32_t plugin_n = 0;; ++plugin_n) {
		boost::shared_ptr<PluginInsert> insert = strip_plugin_insert (strip_n, plugin_n);
		if (!insert) {
			break;
		}

		on_drop_plugin (strip_n, plugin_n);
	}*/

	//_strip_connections[strip_n]->drop_connections ();
	//_strip_connections.erase (strip_n);
}
