#include <cstring>
#include <unistd.h> 
#include <mutex>

#include "read_file.h"
#include "controller.h"
#include "path_file.h"
#include "thread_pool.h"
#include "app_game_model.h"
#include "force_datatype.h"

using namespace force_data_type;

Controller* Controller::instance_ = nullptr;

Controller::Controller()
{
    is_running_ = false;
    control_arm_rt_ = nullptr;
    control_db_  = nullptr;
    app_game_manage_ = AppGameManage();
    ft_sensor_ = FTSensor();
}

Controller* Controller::getInstance()
{
    if (instance_ == nullptr)
	{
		instance_ = new Controller();
	}

	return instance_;
}

Controller::~Controller()
{
    //std::mutex lock_;
    //std::unique_lock<std::mutex> l{ lock_ };

    stop();
    if (control_arm_rt_ != nullptr)
    {
        delete control_arm_rt_;
        control_arm_rt_ = nullptr;
    }

    if (control_db_ != nullptr)
    {
        delete control_db_;
        control_db_ = nullptr;
    }

    if (instance_ == nullptr)
    {
        delete instance_;
        instance_ = nullptr;
    }
}

bool Controller::init()
{
    if(!readFile(CONFIG_FILE_DIR, ft_sensor_ip_, robot_arm_ip_))
    {
        return false;
    }

    printf("Controller: FT Sensor ip: %s\n", ft_sensor_ip_.c_str());
    printf("Controller: Robot ip: %s\n", robot_arm_ip_.c_str());
    control_arm_rt_ = new ControlArmRealtime(robot_arm_ip_);
    control_db_ = new ControlDashboard(robot_arm_ip_);
    return true;
}

bool Controller::start()
{
    if (is_running_) return true;

    /* start ft sensor*/
	if (!ft_sensor_.start("192.168.1.12"))
    {
        printf("Controller: start ft sensor failed.\n");
        return false;
    }
    usleep(2000000);
    FTSensorData sensor_data = ft_sensor_.getSensorData();

    if (fabs(sensor_data.force.fx) < 0.0001
        && fabs(sensor_data.force.fy) < 0.0001
        && fabs(sensor_data.force.fz) < 0.0001
        && fabs(sensor_data.torque.tx) < 0.0001
        && fabs(sensor_data.torque.ty) < 0.0001
        && fabs(sensor_data.torque.tz) < 0.0001)
    {
        printf("Controller: start ft sensor failed: data error\n");
        sensor_data.printValue("sensor_data: ");
        usleep(1000000);
    }

    if (!startRobot())
    {
        printf("Controller: start robot failed.\n");
        return false;
    }
    usleep(100000);

    ThreadPool::getInstance().startAllocatePool();

    is_running_ = true;
    ThreadBase<Controller>::start(THREAD_PRIORITY);
    return true;
}

void Controller::stop()
{
    if (!is_running_) 
    {
        return;
    }

    is_running_ = false;
    ThreadBase<Controller>::stop();

    dropGame();
    
    control_db_->halt();
    control_arm_rt_->halt();
    ft_sensor_.stop();
    
    ThreadPool::getInstance().stopAllocatePool();
}

void Controller::run()
{
	bool sensor_status = false;
    while (is_running_)
    {
 #if 1
		sensor_status = getGpioSensorStatus();
        //printf("sensor_status = %d\n", sensor_status);
        int game_mode = app_game_manage_.getRunningGameMode();
	    // printf("game mode = %d\n", game_mode);
		if (app_game_manage_.isGameRunning()
		    && game_mode == app_game_model::MANUAL_MOVEMENT)
		{
			if (sensor_status)
			{
				usleep(100000);
				continue;
			}
			
			usleep(100000);
            dropManualMovement();
			setLampBeltGreen();
			continue;
		}

		if (sensor_status && game_mode == 0)
		{
            createManualMovement();
			setLampBeltBlue();
		}
#endif
        // ThreadPool::getInstance().notifyCall();
        usleep(100000);
    }
}

bool Controller::isRunning()
{
    return is_running_;
}

