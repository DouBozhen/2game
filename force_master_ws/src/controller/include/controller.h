#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "app_game_manage.h"
#include "control_arm_rt.h"
#include "control_dashboard.h"
#include "ft_sensor.h"
// #include "control_gpio.h"

#include "thread_base.h"
#include "thread_pool.h"
#include "app_game_model.h"

#define THREAD_PRIORITY 50

#define INPUT_INDEX_LAMB_BELT_GREEN 4
#define INPUT_INDEX_LAMB_BELT_BLUE 5
#define OUTPUT_INDEX_SENSOR_STATUE 4
#define ROBOT_COMM_COUNT 10

class Controller: public ThreadBase<Controller>
{
public:
    static Controller* getInstance();
	~Controller();
	bool init();

	bool start();
	void stop();
	void run();
    bool isRunning();

    /* control game */
    bool createGame(int game_mode, app_game_model::AppGameParamsSub sub_params);
    bool startGame();
    void stopGame();
    void dropGame();

    bool setGameForceLevel(int level);
    bool getGameForceLevel(int &level);
	bool setGameRigidityLevel(int level);
	bool setGameRepeatedCount(int count);
	bool setGameSpeedLevel(int level);
	bool getGameRepeatedCount(int &count);
	bool getGameSpeedLevel(int &level);

    bool createTrajRecurrent();
    bool startTrajRecurrent();
    bool stopTrajRecurrent();
    bool dropTrajRecurrent();

    /* control robot */
    void shutDown();
    void powerOff();

private:
    static Controller* instance_;
    AppGameManage app_game_manage_;
    ControlArmRealtime* control_arm_rt_;
    FTSensor ft_sensor_;
    ControlDashboard* control_db_;

    std::string ft_sensor_ip_;
    std::string robot_arm_ip_;

    bool startRobot();
    void stopRobot();

    bool createManualMovement();
    void dropManualMovement();

    /* control gpio */
    bool getGpioSensorStatus();
    bool setLampBeltGreen();
    bool setLampBeltBlue();

    Controller();
};

#endif

