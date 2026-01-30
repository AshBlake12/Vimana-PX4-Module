
 //Dual tiltrotor monitoring and custom flight logic


#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/module.h>
#include <px4_platform_common/log.h>
#include <px4_platform_common/px4_work_queue/ScheduledWorkItem.hpp>
#include <px4_platform_common/time.h>
#include <uORB/topics/vehicle_status.h>
#include <uORB/topics/vtol_vehicle_status.h>

extern "C" __EXPORT int vimana_main(int argc, char *argv[]);

class VimanaModule : public px4::ScheduledWorkItem
{
public:
	VimanaModule() : ScheduledWorkItem(MODULE_NAME, px4::wq_configurations::lp_default) {}
	~VimanaModule() = default;

	bool init();
	void Run() override;

private:
	static constexpr const char *MODULE_NAME = "vimana";

	// uORB subs
	int _vehicle_status_sub{-1};
	int _vtol_status_sub{-1};

	bool _initialized{false};
	uint64_t _last_log_time{0};
};

bool VimanaModule::init()
{
	// Subscribing to relevant topics
	_vehicle_status_sub = orb_subscribe(ORB_ID(vehicle_status));
	_vtol_status_sub = orb_subscribe(ORB_ID(vtol_vehicle_status));

	if (_vehicle_status_sub < 0 || _vtol_status_sub < 0) {
		PX4_ERR("Subscription failed");
		return false;
	}

	_initialized = true;
	PX4_INFO("Vimana VTOL module initialized - Dual Tiltrotor");

	ScheduleOnInterval(100_ms);

	return true;
}

void VimanaModule::Run()
{
	if (!_initialized) {
		return;
	}

	// Read VTOL status
	vtol_vehicle_status_s vtol_status;
	if (orb_copy(ORB_ID(vtol_vehicle_status), _vtol_status_sub, &vtol_status) == 0) {

		// Log transition state every 1 second
		uint64_t now = hrt_absolute_time();
		if (now - _last_log_time > 1_s) {
			const char *mode_str = "UNKNOWN";

			switch (vtol_status.vehicle_vtol_state) {
				case vtol_vehicle_status_s::VEHICLE_VTOL_STATE_MC:
					mode_str = "MULTICOPTER";
					break;
				case vtol_vehicle_status_s::VEHICLE_VTOL_STATE_FW:
					mode_str = "FIXED_WING";
					break;
				case vtol_vehicle_status_s::VEHICLE_VTOL_STATE_TRANSITION_TO_FW:
					mode_str = "TRANSITION->FW";
					break;
				case vtol_vehicle_status_s::VEHICLE_VTOL_STATE_TRANSITION_TO_MC:
					mode_str = "TRANSITION->MC";
					break;
			}

			PX4_INFO("VTOL State: %s", mode_str);
			_last_log_time = now;
		}
	}

	// TODO: Add servo position monitoring
	// TODO: Add custom failsafe logic
	// TODO: Add telemetry logging
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
			PX4_INFO("Running");
		}
		return 0;
	}

	PX4_WARN("Unknown command: %s", argv[1]);
	return 1;
}
