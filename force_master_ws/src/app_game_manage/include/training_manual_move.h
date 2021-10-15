#ifndef TRAINING_MANUAL_MOVE_H
#define TRAINING_MANUAL_MOVE_H

#include "force_datatype.h"
#include "ft_sensor.h"
#include "app_game.h"
#include "force_control.h"
#include "ur_kinematics.h"
#include "solve_jacobian.h"
#include "record_path.h"


class TrainingManualMove : public AppGame
{
public:
    TrainingManualMove();
   	virtual bool initParams(app_game_model::AppGameParams params) override;
	virtual bool start(ControlArmRealtime* control_arm_rt, FTSensor* ft_sensor) override;
	virtual void pause() override;
	virtual void stop() override;
	virtual void replay() override; 
    virtual void process() override;
    virtual std::string getName() override;
    bool isRunning() { return is_processing_; }

    virtual void encodeRuntimeInfo(char* buf, int &buf_size) override;
    virtual bool decodeRuntimeCmd(char* buf, int buf_size) override;
private:
    ControlArmRealtime *control_arm_rt_;
	RecordPath record_path_;
	FTSensor* ft_sensor_;
	UrKinematics ur_kine_;
	ForceControl force_control_;
	SloveJacobian solve_jacobian_;
    double transition_wp2base_[4][4];

	RecordLimit record_path_limit_;

	double force_in_sensor_frame_[4];
	ur_data_type::Joint cmd_speed_;
	double cmd_acc_;

	bool onceProcess();
};

#endif
