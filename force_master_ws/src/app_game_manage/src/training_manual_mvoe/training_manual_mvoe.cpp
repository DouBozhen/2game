#include <cstring>
#include <unistd.h> 
#include <fstream>
#include "ur_datatype.h"
#include "training_manual_move.h"
#include "math_calculate.h"
#include "thread_pool.h"
#include "path_file.h"
using namespace ur_data_type;
using namespace app_game_model;
using namespace force_data_type;
using namespace math_calculate;
using namespace std;

TrainingManualMove::TrainingManualMove()
{
    running_state_ = IDEL;
    control_arm_rt_ = nullptr;
    remove_zero_bias_ = false;

    is_processing_ = false;
    memset(&params_, 0, sizeof(params_));

    memset(force_in_sensor_frame_, 0, sizeof(force_in_sensor_frame_));
    memset(&record_path_limit_, 0, sizeof(record_path_limit_));
    memset(&transition_wp2base_, 0, sizeof(transition_wp2base_));

    record_path_ = RecordPath();    
    cmd_acc_ = 0.0;
    memset(&cmd_speed_, 0, sizeof(cmd_speed_));
    is_processing_ = false;
}

bool TrainingManualMove::initParams(app_game_model::AppGameParams params)
{
    params_.need_record = params.need_record ;

    params_.training_mode = params.training_mode;
    params_.force_level = FORCE_LEVEL_SMALL; //params.force_level;
    params_.rigidity_level = RIGIDITY_SMALL; //params.rigidity_level;
    params_.apply_force_mode = params.apply_force_mode;
    params_.speed_level = params.speed_level;
    params_.repeated_count = params.repeated_count;

    params_.sensor_info.is_sensor_used = params.sensor_info.is_sensor_used;
    if (params_.sensor_info.is_sensor_used)
    {
        params_.sensor_info.remove_zero_bias = params.sensor_info.remove_zero_bias;
        params_.sensor_info.sensor_ip = params.sensor_info.sensor_ip;
    }
    else 
    {
        params_.sensor_info.remove_zero_bias = true;
    }

    ur_kine_.calibrateWorkpiece(transition_wp2base_);

    return true;
}

bool TrainingManualMove::start(ControlArmRealtime* control_arm_rt, FTSensor* ft_sensor)
{
    if (is_processing_) return true;

    if (control_arm_rt == nullptr || ft_sensor == nullptr)
    {
        printf("TrainingManualMove: start failed, pointer is nullptr\n");
        return false;
    }
    control_arm_rt_ = control_arm_rt;
    //usleep(1000000);

    Joint actual_joint = control_arm_rt_->getActualJoint();
    if(actual_joint.jVal[0] < DOUBLE_ZERO
        && actual_joint.jVal[1] < DOUBLE_ZERO
        && actual_joint.jVal[2] < DOUBLE_ZERO
        && actual_joint.jVal[3] < DOUBLE_ZERO
        && actual_joint.jVal[4] < DOUBLE_ZERO
        && actual_joint.jVal[5] < DOUBLE_ZERO
        )
    {
        printf("TrainingManualMove: start failed, control_arm_rt point error.\n");
        return false; 
    }

    ft_sensor_ = ft_sensor;
    FTSensorData sensor_data = ft_sensor_->getSensorData();
    printf("Warnning: TrainingManualMove: onceProcess: sensor_data %lf, %lf, %lf, %lf, %lf, %lf\n",
        sensor_data.force.fx, sensor_data.force.fy, sensor_data.force.fz,
        sensor_data.torque.tx, sensor_data.torque.ty, sensor_data.torque.tz);

    if (fabs(sensor_data.force.fx) < DOUBLE_ZERO
        && fabs(sensor_data.force.fy) < DOUBLE_ZERO
        && fabs(sensor_data.force.fz) < DOUBLE_ZERO
        && fabs(sensor_data.torque.tx) < DOUBLE_ZERO
        && fabs(sensor_data.torque.ty) < DOUBLE_ZERO
        && fabs(sensor_data.torque.tz) < DOUBLE_ZERO)
    {
        printf("TrainingManualMove: start ft sensor failed: data error.\n");
        return false;
    }

    if(!record_path_.start(control_arm_rt, record_path_limit_))
    {
        printf("TrainingManualMove: start failed, start RecordPath failed.\n");
        return false;
    }

	CartPose actual_pose;
    double rotation[3][3] = {};
	ur_kine_.FK(actual_joint, actual_pose, rotation);

    force_control_.calculateEndEffectorForceBias(rotation, sensor_data.force);
    force_control_.updatePidFactors(params_.force_level);

    running_state_ = GameRunningState::IDEL;
    is_processing_ = true;
	ThreadPool::getInstance().registerHandler(this, THREAD_INDEX);

    printf("TrainingManualMove: start successful.\n");
    return true;
}

void TrainingManualMove::pause()
{
    if (running_state_ == GameRunningState::WAITING)
	{
		running_state_ = GameRunningState::PROCESSING;
	}
	else
	{
		running_state_ = GameRunningState::WAITING;
	}
}

void TrainingManualMove::stop()
{
    if(!is_processing_) return;

    is_processing_ = false;
    running_state_ = GameRunningState::STOPING;
	ThreadPool::getInstance().deregisterHandler(THREAD_INDEX);

    record_path_.stop();
    force_control_.initPidFactor();
}

void TrainingManualMove::replay() 
{
    running_state_ = GameRunningState::REPLAYING;
}

void TrainingManualMove::process()
{
    if (!is_processing_) return;

    if (!control_arm_rt_->isConnected())
    {
        printf("TrainingManualMove: exited, because of ControlArmRT discnnected.\n");
        stop();
        return;
    }

    onceProcess();
}

bool TrainingManualMove::onceProcess()
{
    Joint actual_joint = control_arm_rt_->getActualJoint();
    CartPose actual_tcp = control_arm_rt_->getActualToolPose();

    if (actual_joint.jVal[0] < 0.01
        && actual_joint.jVal[1] < 0.01
        && actual_joint.jVal[2] < 0.01
        && actual_joint.jVal[3] < 0.01
        && actual_joint.jVal[4] < 0.01
        && actual_joint.jVal[5] < 0.01
    )
    {
        printf("Warnning: TrainingManualMove: onceProcess: joint value error： %lf, %lf, %lf, %lf, %lf, %lf\n",
            actual_joint.jVal[0], actual_joint.jVal[1], actual_joint.jVal[2], 
            actual_joint.jVal[3], actual_joint.jVal[4], actual_joint.jVal[5]);
        return false;
    }

    FTSensorData sensor_data = ft_sensor_->getSensorData();

    if (force_control_.isForceBeyondLimit(ft_sensor_->getSensorData().force))
    {
        printf("Warnning: TrainingManualMove: sensor force is beyond limit, stoping.\n");
        return false;
    }

	CartPose actual_pose;
    double rotation[3][3] = {};
	ur_kine_.FK(actual_joint, actual_pose, rotation);

    force_data_type::ForceValue force_pid_output;
    force_data_type::TorqueValue torque_pid_output;
    if (!force_control_.calculatePidOutput(
            control_arm_rt_->getActualToolPose(), 
            control_arm_rt_->getActualJoint(),
            rotation,
            ft_sensor_->getSensorData(),
            force_pid_output,
            torque_pid_output
            )
        )
    {
        return false;
    }

    force_data_type::FTSensorData vw;
    if (!force_control_.calculateComplianceVW(force_pid_output, torque_pid_output, vw))
    {
        return false;
    }

    Joint joint_speed;
    if(!solve_jacobian_.inverseJacobian(control_arm_rt_->getActualJoint(), vw, joint_speed))
    {
        printf("Warnning: TrainingManualMove: inverseJacobian failed.\n");
        return false;
    }

    if(!force_control_.oddProtection(control_arm_rt_->getActualJoint(), joint_speed))
    {
        printf("Warnning: TrainingManualMove:oddProtection failed.\n");
        return false;
    }

    generateCmdJointSpeed(control_arm_rt_->getActualJointVel(), joint_speed, vw, params_.sensor_info.remove_zero_bias, cmd_speed_, cmd_acc_);
    if(!control_arm_rt_->speedJointMove(cmd_speed_, cmd_acc_))
    {
        printf("Warnning: TrainingManualMove: speedJointMove failed.\n");
        return false;
    }
    else
    {
        printf("TrainingManualMove: speedJointMove successful.\n");
    }
    return true;
}

std::string TrainingManualMove::getName() 
{
    return "TrainingManualMove";
}

void TrainingManualMove::encodeRuntimeInfo(char* buf, int &buf_size)
{
    return;
}

bool TrainingManualMove::decodeRuntimeCmd(char* buf, int buf_size)
{
    return true;
}



