/*Vimana VTOL monitoring module
Runs at 10 Hz on the low-priority work queue, watches everything we care
about during a flight and shouts loudly if something looks wrong.
Author: Shashank Madhusudan (mad.shadank@gmail.com)*/


#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/module.h>
#include <px4_platform_common/log.h>
#include <px4_platform_common/px4_work_queue/ScheduledWorkItem.hpp>
#include <px4_platform_common/time.h>

#include <lib/mathlib/mathlib.h>
#include <matrix/math.hpp>

// subscriptions we need
#include <uORB/topics/vehicle_status.h>
#include <uORB/topics/vtol_vehicle_status.h>
#include <uORB/topics/vehicle_attitude.h>
#include <uORB/topics/actuator_outputs.h>
#include <uORB/topics/battery_status.h>
#include <uORB/topics/sensor_gps.h>
#include <uORB/topics/estimator_status.h>
#include <uORB/topics/input_rc.h>
#include <uORB/topics/vehicle_local_position.h>
#include <uORB/topics/airspeed.h>
#include <uORB/topics/vehicle_land_detected.h>
#include <uORB/topics/sensor_combined.h>
#include <uORB/topics/vehicle_magnetometer.h>
#include <uORB/topics/mavlink_log.h>

// publications
#include <uORB/topics/vehicle_command.h>
#include <uORB/uORB.h>

extern "C" __EXPORT int vimana_main(int argc, char *argv[]);

// All tunable thresholds live here so they're easy to find and change.
namespace VimanaThresholds {

// battery
constexpr float BAT_WARN_V       = 14.0f;   // start warning below this voltage
constexpr float BAT_CRIT_V       = 13.2f;   // critically low - land now
constexpr float BAT_WARN_PCT     = 0.25f;   // 25 % remaining warning
constexpr float BAT_CRIT_PCT     = 0.10f;   // 10 % remaining - really land now

// airspeed during transitions
constexpr float TRANS_MIN_IAS_MS  = 8.0f;   // need at least this to stay in FW
constexpr float TRANS_ABORT_IAS_MS = 5.0f;  // below this we call the transition off

// attitude limits
constexpr float PITCH_FW_WARN_DEG  = 40.0f; // seems steep for cruise
constexpr float ROLL_TRANS_WARN_DEG = 35.0f; // too much bank mid-transition
constexpr float PITCH_MC_WARN_DEG  = 25.0f; // nose shouldn't tilt much in hover

// EKF position accuracy we're happy with
constexpr float EKF_HACC_WARN = 5.0f;  // metres horizontal
constexpr float EKF_VACC_WARN = 5.0f;  // metres vertical

// logging intervals
constexpr uint32_t LOG_INTERVAL_US = 1000000; // 1 Hz when nothing exciting is happening
constexpr uint32_t TRANS_LOG_US    = 200000;  // 5 Hz during transitions so we don't miss anything
constexpr uint32_t MAX_TRANS_DUR_S = 15;      // if a transition takes longer than this, something is wrong

} // namespace VimanaThresholds


// helper to turn the VTOL state enum into something readable
static const char *vtol_state_str(uint8_t s)
{
	switch (s) {
	case vtol_vehicle_status_s::VEHICLE_VTOL_STATE_MC:               return "MC  [HOVER]";
	case vtol_vehicle_status_s::VEHICLE_VTOL_STATE_FW:               return "FW  [CRUISE]";
	case vtol_vehicle_status_s::VEHICLE_VTOL_STATE_TRANSITION_TO_FW: return "TX->FW";
	case vtol_vehicle_status_s::VEHICLE_VTOL_STATE_TRANSITION_TO_MC: return "TX->MC";
	default:                                                           return "UNKNOWN";
	}
}


class VimanaModule : public px4::ScheduledWorkItem
{
public:
	VimanaModule();
	~VimanaModule() override;

	bool init();
	void print_status() const;

protected:
	void Run() override;

private:
	//   uORB subscriptions
	int _vehicle_status_sub   {-1};
	int _vtol_status_sub      {-1};
	int _attitude_sub         {-1};
	int _actuator_outputs_sub {-1};
	int _battery_sub          {-1};
	int _gps_sub              {-1};
	int _estimator_sub        {-1};
	int _rc_sub               {-1};
	int _local_pos_sub        {-1};
	int _airspeed_sub         {-1};
	int _land_detected_sub    {-1};
	int _sensor_combined_sub  {-1};
	int _magnetometer_sub     {-1};

	//   uORB publications
	orb_advert_t _mavlink_log_pub {nullptr};

	//   module state
	bool _initialized {false};

	// attitude (degrees)
	float _pitch_deg {0.0f};
	float _roll_deg  {0.0f};
	float _yaw_deg   {0.0f};

	// battery
	float _batt_v      {0.0f};
	float _batt_a      {0.0f};
	float _batt_pct    {0.0f};
	float _batt_temp_c {0.0f};
	bool  _batt_warn_sent {false};
	bool  _batt_crit_sent {false};

	// GPS
	double  _gps_lat   {0.0};
	double  _gps_lon   {0.0};
	float   _gps_alt_m {0.0f};
	uint8_t _gps_sats  {0};
	uint8_t _gps_fix   {0};
	float   _gps_hdop  {99.0f};

	// EKF2
	bool     _ekf_ok          {false};
	float    _ekf_hacc        {99.0f};
	float    _ekf_vacc        {99.0f};
	uint32_t _ekf_fault_flags {0};

	// magnetometer (gauss)
	float _mag_x {0.0f};
	float _mag_y {0.0f};
	float _mag_z {0.0f};

	// navigation
	float _local_alt_m {0.0f};
	float _vz_ms       {0.0f};
	float _ias_ms      {0.0f};

	// system flags
	bool     _armed          {false};
	bool     _rc_ok          {false};
	bool     _landed         {true};
	uint64_t _flight_start_us {0};

	// VTOL transition tracking
	uint8_t  _vtol_state         {255};
	bool     _in_transition      {false};
	uint64_t _trans_start_us     {0};
	bool     _trans_abort_sent   {false};
	bool     _trans_spd_warn_sent {false};

	// per-session statistics
	uint32_t _transition_count {0};
	float    _min_batt_v       {99.0f};
	float    _max_current_a    {0.0f};
	float    _max_alt_m        {0.0f};
	uint64_t _total_flight_us  {0};

	uint64_t _last_log_us {0};

	//   private methods
	void _subscribe_all();
	void _unsubscribe_all();

	void _update_attitude();
	void _update_battery();
	void _update_gps();
	void _update_estimator();
	void _update_rc();
	void _update_local_position();
	void _update_airspeed();
	void _update_land_detected();
	void _update_magnetometer();
	void _update_vehicle_status();

	void _check_vtol_transitions();
	void _check_safety();
	void _log_telemetry();

	void _mavlink_crit(const char *fmt, ...);
};


//   constructor / destructor

VimanaModule::VimanaModule()
	: ScheduledWorkItem("vimana", px4::wq_configurations::lp_default)
{}

VimanaModule::~VimanaModule()
{
	ScheduleClear();
	_unsubscribe_all();

	if (_mavlink_log_pub) {
		orb_unadvertise(_mavlink_log_pub);
	}

	// print a quick lifetime summary on the way out
	uint64_t flight_s = _total_flight_us / 1000000ULL;
	PX4_INFO("Vimana stopped. Total flight time: %us, transitions: %u, lowest battery: %.2fV, peak current: %.1fA, highest altitude: %.1fm",
		 (unsigned)flight_s, (unsigned)_transition_count,
		 (double)_min_batt_v, (double)_max_current_a, (double)_max_alt_m);
}


//   subscription helpers

void VimanaModule::_subscribe_all()
{
	_vehicle_status_sub   = orb_subscribe(ORB_ID(vehicle_status));
	_vtol_status_sub      = orb_subscribe(ORB_ID(vtol_vehicle_status));
	_attitude_sub         = orb_subscribe(ORB_ID(vehicle_attitude));
	_actuator_outputs_sub = orb_subscribe(ORB_ID(actuator_outputs));
	_battery_sub          = orb_subscribe(ORB_ID(battery_status));
	_gps_sub              = orb_subscribe(ORB_ID(sensor_gps));
	_estimator_sub        = orb_subscribe(ORB_ID(estimator_status));
	_rc_sub               = orb_subscribe(ORB_ID(input_rc));
	_local_pos_sub        = orb_subscribe(ORB_ID(vehicle_local_position));
	_airspeed_sub         = orb_subscribe(ORB_ID(airspeed));
	_land_detected_sub    = orb_subscribe(ORB_ID(vehicle_land_detected));
	_sensor_combined_sub  = orb_subscribe(ORB_ID(sensor_combined));
	_magnetometer_sub     = orb_subscribe(ORB_ID(vehicle_magnetometer));
}

void VimanaModule::_unsubscribe_all()
{
	auto unsub = [](int &fd) {
		if (fd >= 0) { orb_unsubscribe(fd); fd = -1; }
	};
	unsub(_vehicle_status_sub);
	unsub(_vtol_status_sub);
	unsub(_attitude_sub);
	unsub(_actuator_outputs_sub);
	unsub(_battery_sub);
	unsub(_gps_sub);
	unsub(_estimator_sub);
	unsub(_rc_sub);
	unsub(_local_pos_sub);
	unsub(_airspeed_sub);
	unsub(_land_detected_sub);
	unsub(_sensor_combined_sub);
	unsub(_magnetometer_sub);
}


//   init

bool VimanaModule::init()
{
	_subscribe_all();

	// can't do much without these three
	if (_vehicle_status_sub < 0 || _vtol_status_sub < 0 || _attitude_sub < 0) {
		PX4_ERR("Vimana: failed to subscribe to critical topics, bailing out");
		return false;
	}

	_initialized = true;
	PX4_INFO("Vimana: up and running, tailsitter VTOL monitoring active");
	ScheduleOnInterval(100000); // 10 Hz
	return true;
}


//   update helpers

void VimanaModule::_update_vehicle_status()
{
	bool updated = false;
	orb_check(_vehicle_status_sub, &updated);
	if (!updated) { return; }

	vehicle_status_s vs{};
	if (orb_copy(ORB_ID(vehicle_status), _vehicle_status_sub, &vs) != 0) { return; }

	_armed = (vs.arming_state == vehicle_status_s::ARMING_STATE_ARMED);
}

void VimanaModule::_update_attitude()
{
	bool updated = false;
	orb_check(_attitude_sub, &updated);
	if (!updated) { return; }

	vehicle_attitude_s att{};
	if (orb_copy(ORB_ID(vehicle_attitude), _attitude_sub, &att) != 0) { return; }

	// quaternion to Euler
	matrix::Quatf  q(att.q[0], att.q[1], att.q[2], att.q[3]);
	matrix::Eulerf e(q);
	_roll_deg  = math::degrees(e.phi());
	_pitch_deg = math::degrees(e.theta());
	_yaw_deg   = math::degrees(e.psi());
}

void VimanaModule::_update_battery()
{
	bool updated = false;
	orb_check(_battery_sub, &updated);
	if (!updated) { return; }

	battery_status_s bat{};
	if (orb_copy(ORB_ID(battery_status), _battery_sub, &bat) != 0) { return; }

	_batt_v      = bat.voltage_v;
	_batt_a      = bat.current_a;
	_batt_pct    = bat.remaining;
	_batt_temp_c = bat.temperature - 273.15f; // convert from Kelvin

	// keep running worst-case records
	if (_batt_v > 1.0f && _batt_v < _min_batt_v) { _min_batt_v    = _batt_v; }
	if (_batt_a > _max_current_a)                  { _max_current_a = _batt_a; }
}

void VimanaModule::_update_gps()
{
	bool updated = false;
	orb_check(_gps_sub, &updated);
	if (!updated) { return; }

	sensor_gps_s gps{};
	if (orb_copy(ORB_ID(sensor_gps), _gps_sub, &gps) != 0) { return; }

	_gps_lat   = gps.latitude_deg;
	_gps_lon   = gps.longitude_deg;
	_gps_alt_m = gps.altitude_msl_m;
	_gps_sats  = gps.satellites_used;
	_gps_fix   = gps.fix_type;
	_gps_hdop  = gps.hdop;
}

void VimanaModule::_update_estimator()
{
	bool updated = false;
	orb_check(_estimator_sub, &updated);
	if (!updated) { return; }

	estimator_status_s est{};
	if (orb_copy(ORB_ID(estimator_status), _estimator_sub, &est) != 0) { return; }

	_ekf_fault_flags = est.filter_fault_flags;
	_ekf_hacc        = est.pos_horiz_accuracy;
	_ekf_vacc        = est.pos_vert_accuracy;

	// healthy means no faults and accuracy within acceptable bounds
	_ekf_ok = (_ekf_fault_flags == 0)
	           && (_ekf_hacc < VimanaThresholds::EKF_HACC_WARN)
	           && (_ekf_vacc < VimanaThresholds::EKF_VACC_WARN);
}

void VimanaModule::_update_rc()
{
	bool updated = false;
	orb_check(_rc_sub, &updated);
	if (!updated) { return; }

	input_rc_s rc{};
	if (orb_copy(ORB_ID(input_rc), _rc_sub, &rc) != 0) { return; }

	_rc_ok = (!rc.rc_lost) && (rc.channel_count > 0);
}

void VimanaModule::_update_local_position()
{
	bool updated = false;
	orb_check(_local_pos_sub, &updated);
	if (!updated) { return; }

	vehicle_local_position_s lp{};
	if (orb_copy(ORB_ID(vehicle_local_position), _local_pos_sub, &lp) != 0) { return; }

	// NED frame: z is down, so flip sign for altitude and vertical speed
	_local_alt_m = -lp.z;
	_vz_ms       = -lp.vz;

	if (_local_alt_m > _max_alt_m) { _max_alt_m = _local_alt_m; }
}

void VimanaModule::_update_airspeed()
{
	bool updated = false;
	orb_check(_airspeed_sub, &updated);
	if (!updated) { return; }

	airspeed_s asp{};
	if (orb_copy(ORB_ID(airspeed), _airspeed_sub, &asp) != 0) { return; }

	_ias_ms = asp.indicated_airspeed_m_s;
}

void VimanaModule::_update_magnetometer()
{
	bool updated = false;
	orb_check(_magnetometer_sub, &updated);
	if (!updated) { return; }

	vehicle_magnetometer_s mag{};
	if (orb_copy(ORB_ID(vehicle_magnetometer), _magnetometer_sub, &mag) != 0) { return; }

	_mag_x = mag.magnetometer_ga[0];
	_mag_y = mag.magnetometer_ga[1];
	_mag_z = mag.magnetometer_ga[2];
}

void VimanaModule::_update_land_detected()
{
	bool updated = false;
	orb_check(_land_detected_sub, &updated);
	if (!updated) { return; }

	vehicle_land_detected_s ld{};
	if (orb_copy(ORB_ID(vehicle_land_detected), _land_detected_sub, &ld) != 0) { return; }

	const bool was_landed = _landed;
	_landed = ld.landed;

	if (was_landed && !_landed) {
		// just took off — reset per-flight state
		_flight_start_us = hrt_absolute_time();
		_batt_warn_sent  = false;
		_batt_crit_sent  = false;
		PX4_INFO("Vimana: takeoff detected. Battery: %.2fV, altitude: %.1fm",
			 (double)_batt_v, (double)_local_alt_m);

	} else if (!was_landed && _landed) {
		// just landed — log the flight summary
		const uint64_t dur_s = (hrt_absolute_time() - _flight_start_us) / 1000000ULL;
		_total_flight_us += (hrt_absolute_time() - _flight_start_us);
		PX4_INFO("Vimana: landed. Flight time: %us, lowest battery: %.2fV, peak current: %.1fA, max altitude: %.1fm",
			 (unsigned)dur_s, (double)_min_batt_v, (double)_max_current_a, (double)_max_alt_m);
	}
}


//   VTOL transition state machine

void VimanaModule::_check_vtol_transitions()
{
	bool updated = false;
	orb_check(_vtol_status_sub, &updated);
	if (!updated) { return; }

	vtol_vehicle_status_s vs{};
	if (orb_copy(ORB_ID(vtol_vehicle_status), _vtol_status_sub, &vs) != 0) { return; }

	const uint8_t cur          = vs.vehicle_vtol_state;
	const bool was_in_trans    = _in_transition;

	_in_transition = (cur == vtol_vehicle_status_s::VEHICLE_VTOL_STATE_TRANSITION_TO_FW ||
	                  cur == vtol_vehicle_status_s::VEHICLE_VTOL_STATE_TRANSITION_TO_MC);

	// transition just started
	if (!was_in_trans && _in_transition) {
		_trans_start_us       = hrt_absolute_time();
		_trans_abort_sent     = false;
		_trans_spd_warn_sent  = false;
		_transition_count++;

		PX4_WARN("Vimana: transition started -> %s | airspeed: %.1fm/s, altitude: %.1fm, battery: %.2fV",
			 vtol_state_str(cur), (double)_ias_ms, (double)_local_alt_m, (double)_batt_v);
		_mavlink_crit("Vimana transition start: %s, IAS=%.1f, alt=%.1f",
			      vtol_state_str(cur), (double)_ias_ms, (double)_local_alt_m);
	}

	// transition just completed
	if (was_in_trans && !_in_transition) {
		const float dur_s = (float)(hrt_absolute_time() - _trans_start_us) / 1e6f;
		PX4_INFO("Vimana: transition complete -> %s | took %.2fs, pitch: %.1f, roll: %.1f",
			 vtol_state_str(cur), (double)dur_s, (double)_pitch_deg, (double)_roll_deg);
		_mavlink_crit("Vimana transition done: %s in %.2fs", vtol_state_str(cur), (double)dur_s);
	}

	// checks to run while a transition is in progress
	if (_in_transition) {
		const float elapsed_s = (float)(hrt_absolute_time() - _trans_start_us) / 1e6f;

		// transition is taking way too long
		if ((uint32_t)elapsed_s > VimanaThresholds::MAX_TRANS_DUR_S && !_trans_abort_sent) {
			PX4_ERR("Vimana: transition has been running for %.0fs — check airspeed and attitude", (double)elapsed_s);
			_mavlink_crit("Vimana transition timeout at %.0fs", (double)elapsed_s);
			_trans_abort_sent = true;
		}

		// airspeed checks only apply when we're going to FW
		if (cur == vtol_vehicle_status_s::VEHICLE_VTOL_STATE_TRANSITION_TO_FW) {
			if (_ias_ms < VimanaThresholds::TRANS_ABORT_IAS_MS && !_trans_abort_sent) {
				PX4_ERR("Vimana: airspeed critically low during TX->FW (%.1fm/s), aborting transition", (double)_ias_ms);
				_mavlink_crit("Vimana abort: airspeed %.1f below limit %.1f",
					     (double)_ias_ms, (double)VimanaThresholds::TRANS_ABORT_IAS_MS);
				_trans_abort_sent = true;

			} else if (_ias_ms < VimanaThresholds::TRANS_MIN_IAS_MS && !_trans_spd_warn_sent) {
				PX4_WARN("Vimana: low airspeed during TX->FW: %.1fm/s", (double)_ias_ms);
				_trans_spd_warn_sent = true;
			}
		}

		// excessive roll during any transition is worth noting
		if (fabsf(_roll_deg) > VimanaThresholds::ROLL_TRANS_WARN_DEG) {
			PX4_WARN("Vimana: high roll during transition: %.1f deg", (double)_roll_deg);
		}
	}

	// log any state change
	if (cur != _vtol_state) {
		PX4_INFO("Vimana: state -> %s | pitch: %.1f, roll: %.1f, yaw: %.1f",
			 vtol_state_str(cur), (double)_pitch_deg, (double)_roll_deg, (double)_yaw_deg);
		_vtol_state = cur;
	}
}


//   safety checks

void VimanaModule::_check_safety()
{
	// battery voltage
	if (_batt_v > 1.0f) {
		if (_batt_v < VimanaThresholds::BAT_CRIT_V && !_batt_crit_sent) {
			PX4_ERR("Vimana: battery critical at %.2fV (%.0f%%) — land immediately",
				(double)_batt_v, (double)(_batt_pct * 100.0f));
			_mavlink_crit("Battery critical: %.2fV, %.0f%% — land now",
				      (double)_batt_v, (double)(_batt_pct * 100.0f));
			_batt_crit_sent = true;

		} else if (_batt_v < VimanaThresholds::BAT_WARN_V && !_batt_warn_sent) {
			PX4_WARN("Vimana: battery getting low: %.2fV (%.0f%%)",
				 (double)_batt_v, (double)(_batt_pct * 100.0f));
			_mavlink_crit("Battery low: %.2fV, %.0f%%", (double)_batt_v, (double)(_batt_pct * 100.0f));
			_batt_warn_sent = true;
		}

		if (_batt_pct < VimanaThresholds::BAT_CRIT_PCT) {
			PX4_ERR("Vimana: battery below 10%% — land immediately");
		}
	}

	// attitude sanity checks — what's acceptable depends on the current flight mode
	if (_vtol_state == vtol_vehicle_status_s::VEHICLE_VTOL_STATE_FW) {
		if (fabsf(_pitch_deg) > VimanaThresholds::PITCH_FW_WARN_DEG) {
			PX4_WARN("Vimana: pitch looks high for FW cruise: %.1f deg", (double)_pitch_deg);
		}

	} else if (_vtol_state == vtol_vehicle_status_s::VEHICLE_VTOL_STATE_MC) {
		if (fabsf(_pitch_deg) > VimanaThresholds::PITCH_MC_WARN_DEG) {
			PX4_WARN("Vimana: unusual pitch while hovering: %.1f deg", (double)_pitch_deg);
		}
	}

	// EKF health
	if (_armed && !_ekf_ok) {
		PX4_WARN("Vimana: EKF2 degraded — fault flags: 0x%08X, hacc: %.1fm, vacc: %.1fm",
			 (unsigned)_ekf_fault_flags, (double)_ekf_hacc, (double)_ekf_vacc);
	}

	// RC link
	if (_armed && !_rc_ok) {
		PX4_WARN("Vimana: RC signal lost");
		_mavlink_crit("Vimana: RC signal lost");
	}

	// GPS fix
	if (_armed && _gps_fix < 3) {
		PX4_WARN("Vimana: GPS fix lost (fix type: %d, sats: %d)", _gps_fix, _gps_sats);
	}
}


//   periodic telemetry log

void VimanaModule::_log_telemetry()
{
	const uint64_t now      = hrt_absolute_time();

	// log more often when we're mid-transition so we have a good record of what happened
	const uint32_t interval = _in_transition
	                          ? VimanaThresholds::TRANS_LOG_US
	                          : VimanaThresholds::LOG_INTERVAL_US;

	if (now - _last_log_us < interval) { return; }
	_last_log_us = now;

	const char *fix_str = (_gps_fix >= 3) ? "3D" : (_gps_fix == 2 ? "2D" : "none");

	PX4_INFO("--- Vimana | %s | %s ---",
		 vtol_state_str(_vtol_state), _armed ? "armed" : "disarmed");
	PX4_INFO("att    pitch %+.1f  roll %+.1f  yaw %+.1f  (deg)",
		 (double)_pitch_deg, (double)_roll_deg, (double)_yaw_deg);
	PX4_INFO("bat    %.2fV  %.1fA  %.0f%%  %.1f C",
		 (double)_batt_v, (double)_batt_a,
		 (double)(_batt_pct * 100.0f), (double)_batt_temp_c);
	PX4_INFO("gps    fix: %s  sats: %d  hdop: %.1f  alt: %.1fm  pos: %.6f %.6f",
		 fix_str, _gps_sats, (double)_gps_hdop, (double)_gps_alt_m, _gps_lat, _gps_lon);
	PX4_INFO("nav    agl: %.1fm  vz: %+.1fm/s  ias: %.1fm/s",
		 (double)_local_alt_m, (double)_vz_ms, (double)_ias_ms);
	PX4_INFO("mag    [%.3f  %.3f  %.3f] Ga",
		 (double)_mag_x, (double)_mag_y, (double)_mag_z);
	PX4_INFO("sys    ekf: %s (%.1f/%.1f)  rc: %s  landed: %s  transitions: %u",
		 _ekf_ok ? "ok" : "degraded", (double)_ekf_hacc, (double)_ekf_vacc,
		 _rc_ok  ? "ok" : "lost",
		 _landed ? "yes" : "no",
		 (unsigned)_transition_count);
}


//   MAVLink critical message helper

void VimanaModule::_mavlink_crit(const char *fmt, ...)
{
	char buf[100];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	int instance;

	mavlink_log_s ml{};
	ml.timestamp = hrt_absolute_time();
	ml.severity  = 2; // MAV_SEVERITY_CRITICAL
	strncpy((char *)ml.text, buf, sizeof(ml.text) - 1);
	ml.text[sizeof(ml.text) - 1] = '\0';

	if (_mavlink_log_pub == nullptr) {
		_mavlink_log_pub = orb_advertise_multi(ORB_ID(mavlink_log), &ml, &instance);
	} else {
		orb_publish(ORB_ID(mavlink_log), _mavlink_log_pub, &ml);
	}
}


//   main work item

void VimanaModule::Run()
{
	if (!_initialized) { return; }

	// pull in fresh data from all topics
	_update_vehicle_status();
	_update_attitude();
	_update_battery();
	_update_gps();
	_update_estimator();
	_update_rc();
	_update_local_position();
	_update_airspeed();
	_update_land_detected();
	_update_magnetometer();

	// run checks and logging
	_check_vtol_transitions();
	_check_safety();
	_log_telemetry();
}


//   status print

void VimanaModule::print_status() const
{
	PX4_INFO(" Vimana status ");
	PX4_INFO("state    : %s", vtol_state_str(_vtol_state));
	PX4_INFO("armed    : %s   landed: %s", _armed ? "yes" : "no", _landed ? "yes" : "no");
	PX4_INFO("battery  : %.2fV  %.0f%%  %.1fA  %.1f C",
		 (double)_batt_v, (double)(_batt_pct * 100.0f), (double)_batt_a, (double)_batt_temp_c);
	PX4_INFO("gps      : %d sats  fix: %d  hdop: %.1f  alt: %.1fm",
		 _gps_sats, _gps_fix, (double)_gps_hdop, (double)_gps_alt_m);
	PX4_INFO("attitude : pitch %.1f  roll %.1f  yaw %.1f  (deg)",
		 (double)_pitch_deg, (double)_roll_deg, (double)_yaw_deg);
	PX4_INFO("airspeed : %.1f m/s", (double)_ias_ms);
	PX4_INFO("ekf2     : %s  hacc: %.1fm  vacc: %.1fm  faults: 0x%08X",
		 _ekf_ok ? "healthy" : "degraded",
		 (double)_ekf_hacc, (double)_ekf_vacc, (unsigned)_ekf_fault_flags);
	PX4_INFO("rc       : %s", _rc_ok ? "ok" : "lost");
	PX4_INFO("mag      : [%.3f  %.3f  %.3f] Ga", (double)_mag_x, (double)_mag_y, (double)_mag_z);
	PX4_INFO("--- lifetime stats ---");
	PX4_INFO("transitions  : %u", (unsigned)_transition_count);
	PX4_INFO("lowest batt  : %.2fV", (double)_min_batt_v);
	PX4_INFO("peak current : %.1fA", (double)_max_current_a);
	PX4_INFO("max altitude : %.1fm", (double)_max_alt_m);
	PX4_INFO("total flight : %us", (unsigned)(_total_flight_us / 1000000ULL));
}


//   entry point

static VimanaModule *g_instance = nullptr;

int vimana_main(int argc, char *argv[])
{
	if (argc < 2) {
		PX4_INFO("usage: vimana {start|stop|status}");
		return 1;
	}

	if (!strcmp(argv[1], "start")) {
		if (g_instance) { PX4_WARN("Vimana is already running"); return 0; }

		g_instance = new VimanaModule();
		if (!g_instance) { PX4_ERR("Vimana: failed to allocate module"); return -1; }

		if (!g_instance->init()) {
			delete g_instance;
			g_instance = nullptr;
			return -1;
		}

		return 0;
	}

	if (!strcmp(argv[1], "stop")) {
		if (!g_instance) { PX4_WARN("Vimana is not running"); return 0; }
		delete g_instance;
		g_instance = nullptr;
		return 0;
	}

	if (!strcmp(argv[1], "status")) {
		if (!g_instance) { PX4_INFO("Vimana is not running"); return 0; }
		g_instance->print_status();
		return 0;
	}

	PX4_WARN("Vimana: unknown command '%s'", argv[1]);
	return 1;
}
