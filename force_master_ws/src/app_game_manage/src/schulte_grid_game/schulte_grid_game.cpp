#include <cstring>
#include <sys/time.h>
#include <unistd.h>

#include "path_file.h"
#include "thread_pool.h"
#include "schulte_grid_game.h"

using namespace ur_data_type;
using namespace app_game_model;
using namespace force_data_type;
using namespace math_calculate;

SchulteGridGame::SchulteGridGame()
{
    control_arm_rt_ = nullptr;
    //ft_sensor_ = nullptr;

    height_ = 0.0;
    memset(&start_point_, 0, sizeof(start_point_));
    memset(&passive_force_, 0, sizeof(passive_force_));

    schulte_rigidity_ = 0.5;
    schulte_path_wid_ = 0.01f;
    cut_force_ = 0.0;                          
    move_force_ = 0.0;                         
    maximum_force_ = 0.01;                    

    start_point_next_.x = 10.0;
    start_point_next_.y = 10.0;

    alock_ = true;
    if_ini_ = true;
    if_set_start_ = true;
    k_speed_ = 1;

    memset(&tool_pose_in_base_, 0, sizeof(tool_pose_in_base_));
    memset(&transition_wp2base_, 0, sizeof(transition_wp2base_));  

    running_state_ = IDEL;
    TrainingMode::training_mode_ = NONE;
#if 1
    speed_level_ = 0;
    cut_distance_ = 0;
    double passive_force_tem[2] = {};//力的临时变量
    game_action_ = PUZZEL_IDEL;
    puzzle_cut_slow_ = 0.02;
    is_processing_ = false;
#endif
}

SchulteGridGame::~SchulteGridGame()
{
}

bool SchulteGridGame::initParams(app_game_model::AppGameParams params)
{
    initCutAndMoveForce(params.force_level);
    maximum_force_ = (cut_force_) / 100.0;

    initRigidity(params.rigidity_level);
    ur_kine_.calibrateWorkpiece(transition_wp2base_);

    return true;

};

void SchulteGridGame::start(ControlArmRealtime* control_arm_rt)
{
    if (params_.force_level == FORCE_LEVEL_ZERO)
    {
        printf("SchulteGridGame: start failed, force level: %d\n", params_.force_level);
        return false; 
    }

    if (control_arm_rt == nullptr)
    {
        printf("SchulteGridGame: start failed, control_arm_rt pointer is nullptr\n");
        return false;
    }
    control_arm_rt_ = control_arm_rt;
    usleep(1000000);

    Joint actual_joint = control_arm_rt_->getActualJoint();
    if(actual_joint.jVal[0] < DOUBLE_ZERO
        && actual_joint.jVal[1] < DOUBLE_ZERO
        && actual_joint.jVal[2] < DOUBLE_ZERO
        && actual_joint.jVal[3] < DOUBLE_ZERO
        && actual_joint.jVal[4] < DOUBLE_ZERO
        && actual_joint.jVal[5] < DOUBLE_ZERO
        )
    {
        printf("SchulteGridGame: start failed, control_arm_rt point error.\n");
        return false; 
    }

	CartPose actual_pose;
    double rotation[3][3] = {};
	ur_kine_.FK(actual_joint, actual_pose, rotation);

    start_point_.x = actual_pose.point.x;
    start_point_.y = actual_pose.point.y;
    height_ = actual_pose.point.z;

    running_state_ = PROCESSING;

    is_processing_ = true;
    ThreadPool::getInstance().registerHandler(this, THREAD_INDEX);
    return true;
}

void SchulteGridGame::pause()
{
	if (running_state_ == WAITING)
	{
		running_state_ = PROCESSING;
	}
    else
	{
		running_state_ = WAITING;
	}
};

void SchulteGridGame::stop()
{
	if (!is_processing_) return;

    is_processing_ = false;
    running_state_ = STOPING;
	ThreadPool::getInstance().deregisterHandler(THREAD_INDEX);
    force_control_.initPidFactor();
};

void SchulteGridGame::replay()
{
	 running_state_ = REPLAYING;
};

void SchulteGridGame::process()
{
    if (!is_processing_) return;
    if (!control_arm_rt_->isConnected())
    {
        printf("ManualMovement: exited, because of ControlArmRT discnnected.\n");
        stop();
        return;
    }
    onceProcess();
};

void SchulteGridGame::onceProcess(int32_t cmd, int8_t* Data, int32_t Size)
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
        printf("Warnning: ManualMovement: onceProcess: joint value error： %lf, %lf, %lf, %lf, %lf, %lf\n",
            actual_joint.jVal[0], actual_joint.jVal[1], actual_joint.jVal[2], 
            actual_joint.jVal[3], actual_joint.jVal[4], actual_joint.jVal[5]);
        return false;
    }

    FTSensorData sensor_data = ft_sensor_->getSensorData();

    if (force_control_.isForceBeyondLimit(ft_sensor_->getSensorData().force))
    {
        printf("Warnning: ManualMovement: sensor force is beyond limit, stoping.\n");
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

    switch (params_.apply_force_mode)
    {
        case Compliance:
        {
            forceCalculate();
            if (!force_control_.calculateComplianceVW(force_pid_output, torque_pid_output, vw))
            {
                return false;
            }
        }
        break;
        case Plane:
        {
            forceCalculate();
            force_control_.calculatePlaneVW(control_arm_rt_->getActualJoint(), 
                force_pid_output, torque_pid_output, height_, vw);
        }
        break;
        default:;
    };

    Joint joint_speed;
    if(!solve_jacobian_.inverseJacobian(control_arm_rt_->getActualJoint(), vw, joint_speed))
    {
        printf("Warnning: ManualMovement: inverseJacobian failed.\n");
        return false;
    }

    if(!force_control_.oddProtection(control_arm_rt_->getActualJoint(), joint_speed))
    {
        printf("Warnning: ManualMovement:oddProtection failed.\n");
        return false;
    }

    generateCmdJointSpeed(control_arm_rt_->getActualJointVel(), joint_speed, vw, params_.sensor_info.remove_zero_bias, cmd_speed_, cmd_acc_);
    if(!control_arm_rt_->speedJointMove(cmd_speed_, cmd_acc_))
    {
        printf("Warnning: ManualMovement: speedJointMove failed.\n");
        return false;
    }
    else
    {
        printf("ManualMovement: speedJointMove successful.\n");
    }
    return true;
};

std::string SchulteGridGame::getName() 
{
    return "SchulteGridGame";
}

void SchulteGridGame::isRunning() 
{ 
	return is_processing_; 
}

#if 0
void SchulteGridGame::encodeRuntimeInfo(char* buf, int &buf_size)
{
    buf_size = unity2ubuntu_.ByteSize();
    ubuntu2unity_.SerializeToArray(buf, buf_size);
}

bool SchulteGridGame::decodeRuntimeCmd(char* buf, int buf_size)
{
   if(!unity2ubuntu_.ParseFromArray(buf, buf_size))
    {
        printf("SchulteGridGame: parse data failed.\n");
        return false;
    }

    return true;
}
#endif

void SchulteGridGame::forceCalculate()
{
	if (alock_ == true
        || unity2ubuntu_.ByteSize() == 0 
        || running_state_ == GameRunningState::IDEL 
        || training_mode_ == TrainingMode::NONE 
        || game_force_level_ == GameForceLevel::FORCE_LEVEL_ZERO)
	{
		return false;
	}

    tool_pose_in_base_ = control_arm_rt_->getActualToolPose();
    getTcpPoseInWpCoordinate(tool_pose_in_base_, transition_wp2base_, tool_pose_in_wp_);

    switch (running_state_)
    {
        case PROCESSING:
        {
            updatePassiveForceOnProcessing(tool_pose_in_base_);
        }
            break;
        case WAITING:
        {
            ubuntu2unity_.set_gamereplay(SchulteGrid::Ubuntu2Unity_GameReplayEnum_game_pause);
			passive_force_.fx = 0.0;
			passive_force_.fy = 0.0;
			force_control_.setPlanarPassiveForce(passive_force_);//force->SetForceAndTorque();
			force_control_.initPidOutput();
        }
            break;
        case STOPING:
        {
			ubuntu2unity_.set_gamereplay(SchulteGrid::Ubuntu2Unity_GameReplayEnum_game_back);
			unity2ubuntu_.Clear();
			if_ini_ = true;
			if_set_start_ = true;
			passive_force_.fx = 0.0;
    		passive_force_.fy = 0.0;
		
        }
            break;
        case REPLAYING:
        {
			ubuntu2unity_.set_gamereplay(SchulteGrid::Ubuntu2Unity_GameReplayEnum_game_replay);
			unity2ubuntu_.Clear();
			passive_force_.fx = 0.0;
			passive_force_.fy = 0.0;
			running_state_ = PROCESSING;
			if_set_start_ = true;
			if_ini_ = true;
        }
            break;
        default:;
    };

	force_control_.updateForceInToolFrame(control_arm_rt_->getActualJoint());

    packageCommData();
}

void SchulteGridGame::packageCommData()
{
    ubuntu2unity_.set_game_length(cut_distance_);
    ubuntu2unity_.set_tcp_pose(0, tool_pose_in_base_.point.x);
    ubuntu2unity_.set_tcp_pose(1, tool_pose_in_base_.point.y);
    ubuntu2unity_.set_tcp_pose(2, tool_pose_in_base_.rpy.rz);
   // ubuntu2unity_.set_sensor_force_z(ft_sensor_->getSensorData().force.fz);
    ubuntu2unity_.set_game_device(SCREEN); 
   //??
    #if 0
    MessageHead Head;
    Head.Command = 1009;
    Head.Length = ubuntu2unity_.ByteSize();
    CIOBuffer Buffer;
    Head.Serialize(Buffer.Buffer);
    int8_t * ptr = Buffer.Buffer + MessageHead::Size();
    ubuntu2unity_.SerializeToArray(ptr, ubuntu2unity_.ByteSize());
    Buffer.Length = Head.Length + sizeof(int16_t) + MessageHead::Size();

    AppServer::GetInstance().WriteBuffer(&Buffer);
    #endif
}

void SchulteGridGame::updatePassiveForceOnProcessing(
    ur_data_type::CartPose tcp_pose_in_base)
{
    ubuntu2unity_.set_gamereplay(SchulteGrid::Ubuntu2Unity_GameReplayEnum_game_ing);
    
    PlanarPoint planar_tcp_point;
    planar_tcp_point.x = tcp_pose_in_base.point.x;
    planar_tcp_point.y = tcp_pose_in_base.point.y;

	if (unity2ubuntu_.ByteSize() == 0) break;

	if (unity2ubuntu_.backnum() == 1)
	{
		start_point_next_.x =unity2ubuntu_.pointorigin().x();
		start_point_next_.y =unity2ubuntu_.pointorigin().y();

		updatePassiveForceByTrainMode(training_mode_, start_point_, end_point, planar_tcp_point);
	}
	else if (unity2ubuntu_.backnum() == 0)
	{
        PlanarPoint end_point;
		PlanarPoint end_point.x =unity2ubuntu_.pointgrid(0).x();
		PlanarPoint end_point.y =unity2ubuntu_.pointgrid(0).y();
		start_point_next_ = end_point;
		updatePassiveForceByTrainMode(training_mode_, start_point_, end_point, planar_tcp_point);
	}
	else
	{
		break;
	}
	break;
}

void SchulteGridGame::updatePassiveForceByTrainMode(
    int train_mode, PlanarPoint start, PlanarPoint end, PlanarPoint tcp)
{
    planar_data_type::PlanarForceValue planar_force;
    planar_force.fx = 0.0;
    planar_force.fy = 0.0;
    force_control_.setPlanarPassiveForce(planar_force);

	switch (train_mode)
	{
		case TrainingMode::ACTIVE:
		{
			PlanarForceValue planner_force = force_control_.getPlanarPassiveForce();
            force_control_.variableDigidity(start, end, tcp, schulte_rigidity_, schulte_path_wid_, planner_force);
			passive_force_ = force_control_.getPlanarPassiveForce() * force_control_.getPlanarPassiveForceScaleFactor(start, end, tcp, 0.027);
		}
			break;
		case TrainingMode::ASSIST:
		{	
			//tcp??
			PlanarForceValue planner_force = force_control_.getPlanarPassiveForce();
            force_control_.variableDigidity(start, end, tcp, schulte_rigidity_, schulte_path_wid_, planner_force);
            passive_force_ = planner_force + force_control_.getPlanarPassiveForce(start, end, tcp, speed_level_, 0.0);
            force_control_.setPlanarPassiveForce(passive_force_);
			//
			if (unity2ubuntu_.isfinish() == 1)
			{
				passive_force_.fx = 0;
				passive_force_.fy = 0;
				k_speed_ = 0;
			}
			else
			{
				passive_force_.fx = (passive_force_tem[0] + passive_force_.fx)*k_speed_;
				passive_force_.fy = (passive_force_tem[1] + passive_force_.fy)*k_speed_;
				k_speed_ = k_speed_ + 0.005 < 1 ? k_speed_ + 0.005 : 1;
			}		
			passive_force_ = force_control_.getPlanarPassiveForce() * force_control_.getPlanarPassiveForceScaleFactor(start, end, tcp, puzzle_cut_slow_);
			//force->AddSlowForce(start, stop, passive_force_, puzzle_cut_slow_, puzzle_cut_switch_);//puzzle_cut_slow_
		}
			break;
		case TrainingMode::IMPEDANCE:
		{
			force_control_.setPassiveForce(force_control_.getCartRandomPassiveForce(maximum_force_, RANDOM_TIME));
		}
			break;
	}

}

}
