// Tailsitter logic, VTOL Specifivc
#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/module.h>
#include <px4_platform_common/log.h>
#include <px4_platform_common/px4_work_queue/ScheduledWorkItem.hpp>
#include <px4_platform_common/time.h>
#include <uORB/topics/vehicle_status.h>
#include <uORB/topics/vtol_vehicle_status.h>
#include <uORB/topics/vehicle_attitude.h>
#include <uORB/topics/actuator_outputs.h>

extern "C" __EXPORT int vimana_main(int argc, char *argv[]);

class VimanaModule : public px4::ScheduledWorkItem
{
public:
	VimanaModule() : ScheduledWorkItem("vimana", px4::wq_configurations::lp_default) {}

	~VimanaModule() {
		// Cleaned up the uORB subs
		if (_vehicle_status_sub >= 0) {
			orb_unsubscribe(_vehicle_status_sub);
		}
		if (_vtol_status_sub >= 0) {
			orb_unsubscribe(_vtol_status_sub);
		}
		if (_vehicle_attitude_sub >= 0) {
			orb_unsubscribe(_vehicle_attitude_sub);
		}
		if (_actuator_outputs_sub >= 0) {
			orb_unsubscribe(_actuator_outputs_sub);
		}
	}

	bool init();
	void Run() override;

private:

	// uORB subscriptions
	int _vehicle_status_sub{-1};
	int _vtol_status_sub{-1};
	int _vehicle_attitude_sub{-1};
	int _actuator_outputs_sub{-1};

	bool _initialized{false};
	uint64_t _last_log_time{0};

	// Tailsitter specific monitoring
	float _pitch_angle{0.0f};
	float _roll_angle{0.0f};
	bool _in_transition{false};
};

bool VimanaModule::init()
{
	// sub to relavant topics
	_vehicle_status_sub = orb_subscribe(ORB_ID(vehicle_status));
	_vtol_status_sub = orb_subscribe(ORB_ID(vtol_vehicle_status));
	_vehicle_attitude_sub = orb_subscribe(ORB_ID(vehicle_attitude));
	_actuator_outputs_sub = orb_subscribe(ORB_ID(actuator_outputs));

	if (_vehicle_status_sub < 0 || _vtol_status_sub < 0 ||
	    _vehicle_attitude_sub < 0 || _actuator_outputs_sub < 0) {
		PX4_ERR("Subscription failed");
		return false;
	}

	_initialized = true;
	PX4_INFO("Vimana VTOL module initialized - TAILSITTER configuration");
	ScheduleOnInterval(100000);
	return true;
}

void VimanaModule::Run()
{
	if (!_initialized) {
		return;
	}

	// attitude and orientation monitoring
	vehicle_attitude_s attitude;
	if (orb_copy(ORB_ID(vehicle_attitude), _vehicle_attitude_sub, &attitude) == 0) {
		// For tailsitter: pitch 90 deg in MC mode, 0 deg in FW mode
		_pitch_angle = attitude.q[1];
		_roll_angle = attitude.q[0];
	}

	// Read VTOL status
	vtol_vehicle_status_s vtol_status;
	if (orb_copy(ORB_ID(vtol_vehicle_status), _vtol_status_sub, &vtol_status) == 0) {
		// transition state changes detection
		bool was_in_transition = _in_transition;
		_in_transition = (vtol_status.vehicle_vtol_state == vtol_vehicle_status_s::VEHICLE_VTOL_STATE_TRANSITION_TO_FW ||
		                  vtol_status.vehicle_vtol_state == vtol_vehicle_status_s::VEHICLE_VTOL_STATE_TRANSITION_TO_MC);

		// logs of changes
		uint64_t now = hrt_absolute_time();
		if ((now - _last_log_time > 1000000) || (_in_transition != was_in_transition)) {
			const char *mode_str = "UNKNOWN";
			switch (vtol_status.vehicle_vtol_state) {
			case vtol_vehicle_status_s::VEHICLE_VTOL_STATE_MC:
				mode_str = "MULTICOPTER (Vertical)";
				break;
			case vtol_vehicle_status_s::VEHICLE_VTOL_STATE_FW:
				mode_str = "FIXED_WING (Horizontal)";
				break;
			case vtol_vehicle_status_s::VEHICLE_VTOL_STATE_TRANSITION_TO_FW:
				mode_str = "TRANSITION->FW (Pitching down)";
				break;
			case vtol_vehicle_status_s::VEHICLE_VTOL_STATE_TRANSITION_TO_MC:
				mode_str = "TRANSITION->MC (Pitching up)";
				break;
			}

			PX4_INFO("VTOL State: %s | Pitch: %.1f° Roll: %.1f°",
			         mode_str, (double)_pitch_angle, (double)_roll_angle);
			_last_log_time = now;
		}

		// some safety checks
		if (_in_transition) {
			// TODO: Add airspeed monitoring during transition
			// TODO: Add altitude loss monitoring
			// TODO: Implement transition abort logic if needed
		}
	}

	// actuators
	actuator_outputs_s outputs;
	if (orb_copy(ORB_ID(actuator_outputs), _actuator_outputs_sub, &outputs) == 0) {
		// TODO: Monitor motor outputs
		// TODO: Monitor control surface positions (if applicable)
		// TODO: Detect output saturation
	}

	// TODO: Add custom failsafe logic for tailsitter
	// - Transition abort on low airspeed
	// - Emergency MC recovery
	// - Altitude monitoring during transition

	// TODO: Add telemetry logging
	// - Log transition times
	// - Log attitude during transitions
	// - Log control surface activity
}

static VimanaModule *instance = nullptr;

int vimana_main(int argc, char *argv[])
{
	if (argc < 2) {
		PX4_INFO("Usage: vimana {start|stop|status}");
		return 1;
	}

	if (!strcmp(argv[1], "start")) {
		if (instance != nullptr) {
			PX4_WARN("Already running");
			return 0;
		}

		instance = new VimanaModule();
		if (instance == nullptr) {
			PX4_ERR("Allocation failed");
			return -1;
		}

		if (!instance->init()) {
			delete instance;
			instance = nullptr;
			PX4_ERR("Init failed");
			return -1;
		}

		return 0;
	}

	if (!strcmp(argv[1], "stop")) {
		if (instance == nullptr) {
			PX4_WARN("Not running");
			return 0;
		}

		delete instance;
		instance = nullptr;
		PX4_INFO("Stopped");
		return 0;
	}

	if (!strcmp(argv[1], "status")) {
		if (instance == nullptr) {
			PX4_INFO("Not running");
		} else {
			PX4_INFO("Running - Tailsitter VTOL monitoring active");
		}
		return 0;
	}

	PX4_WARN("Unknown command: %s", argv[1]);
	return 1;
}
