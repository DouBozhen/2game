#include <cstring>
#include <sys/time.h>
#include <unistd.h>

#include "path_file.h"
#include "thread_pool.h"
#include "football_game.h"

using namespace ur_data_type;
using namespace app_game_model;
using namespace force_data_type;
using namespace math_calculate;

FootballGame::FootballGame()
{
   // in_case_force_[2] = {0,0};//用来储存足球被踢出前一瞬间的力，使足球被踢出后力逐渐减小
    football_flag_ = 0, //力控的线段序号
    football_gone_ = 1;//足球是否被踢出
	football_rigidity_ = 0.5;
	football_path_wid_ = 0.01f;
	football_cut_switch_ = 0.009, 
    football_cut_slow_ = 3 * football_cut_switch_;
	cut_force_ = 0.0, //沿着某条线切的末端力
    move_force_ = 0.0, //移动到某个点的末端力
    maximum_force_ = 0.01;//random mode 下的最大力
	height_ = 0.0;
    speed_level_=0;

    memset(&start_point_, 0, sizeof(start_point_));
    memset(&transition_wp2base_, 0, sizeof(transition_wp2base_)); 
    memset(&tool_pose_in_base_, 0, sizeof(tool_pose_in_base_));
    memset(&tool_pose_in_wp_, 0, sizeof(tool_pose_in_wp_));
    memset(&passive_force_, 0, sizeof(passive_force_));

    control_arm_rt_ = nullptr;
    ft_sensor_ = nullptr;

    running_state_ = IDEL;
    TrainingMode::training_mode_ = NONE;

}

FootballGame::~FootballGame()
{

}

bool FootballGame::initParams(app_game_model::AppGameParams params)
{
    initCutAndMoveForce(params.force_level);
    maximum_force_ = (cut_force_) / 100.0;

    initRigidity(params.rigidity_level);
    football_cut_slow_ = 3 * football_path_wid_;
	football_cut_switch_ = football_path_wid_;

    ur_kine_.calibrateWorkpiece(transition_wp2base_);

    return true;

};

bool FootballGame::start(ControlArmRealtime* control_arm_rt, FTSensor* ft_sensor)
{
    if (params_.force_level == FORCE_LEVEL_ZERO)
    {
        printf("FootballGame: start failed, force level: %d\n", params_.force_level);
        return false; 
    }

    if (control_arm_rt == nullptr)
    {
        printf("FootballGame: start failed, control_arm_rt pointer is nullptr\n");
        return false;
    }
    control_arm_rt_ = control_arm_rt;
    usleep(1000000);

    Joint actual_joint = control_arm_rt_->getActualJoint();//actual_joint=(q1,q2,,,,q6)
    if(actual_joint.jVal[0] < DOUBLE_ZERO
        && actual_joint.jVal[1] < DOUBLE_ZERO
        && actual_joint.jVal[2] < DOUBLE_ZERO
        && actual_joint.jVal[3] < DOUBLE_ZERO
        && actual_joint.jVal[4] < DOUBLE_ZERO
        && actual_joint.jVal[5] < DOUBLE_ZERO
        )
    {
        printf("FootballGame: start failed, control_arm_rt point error.\n");
        return false; 
    }

    /* start ft sensor*/
    ft_sensor_ = ft_sensor;
    FTSensorData sensor_data = ft_sensor_->getSensorData();
    printf("Warnning: ManualMovement: onceProcess: sensor_data %lf, %lf, %lf, %lf, %lf, %lf\n",
        sensor_data.force.fx, sensor_data.force.fy, sensor_data.force.fz,
        sensor_data.torque.tx, sensor_data.torque.ty, sensor_data.torque.tz);

    if (fabs(sensor_data.force.fx) < DOUBLE_ZERO
        && fabs(sensor_data.force.fy) < DOUBLE_ZERO
        && fabs(sensor_data.force.fz) < DOUBLE_ZERO
        && fabs(sensor_data.torque.tx) < DOUBLE_ZERO
        && fabs(sensor_data.torque.ty) < DOUBLE_ZERO
        && fabs(sensor_data.torque.tz) < DOUBLE_ZERO)
    {
        printf("ManualMovement: start ft sensor failed: data error.\n");
        return false;
    }


	CartPose actual_pose;//(point,rpy)=(x,y,z,rx,ry,rz)
    double rotation[3][3] = {};
	ur_kine_.FK(actual_joint, actual_pose, rotation);//actual_joint[]--> actual_pose[]
    height_ = actual_pose.point.z;

    force_control_.calculateEndEffectorForceBias(rotation, sensor_data.force);
    force_control_.updatePidFactors(params_.force_level);

    game_action_ = MOVING;
    running_state_ = PROCESSING;
    is_processing_ = true;

    ThreadPool::getInstance().registerHandler(this, THREAD_INDEX);
    return true;
}

void FootballGame::pause()
{
	if (running_state_ == WAITING)
	{
		running_state_ = PROCESSING;
	}
}

void FootballGame::stop()
{
    if (!is_processing_) return;

    is_processing_ = false;
    running_state_ = GameRunningState::STOPING;
	ThreadPool::getInstance().deregisterHandler(THREAD_INDEX);

    force_control_.initPidFactor();
}

void FootballGame::replay() 
{
    running_state_ = REPLAYING;
}

void FootballGame::process()
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

bool FootballGame::onceProcess()
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
        case Compliance:// active force
        {
            forceCalculate();//get passive_force
            if (!force_control_.calculateComplianceVW(force_pid_output, torque_pid_output, vw))
            {
                return false;
            }
        }
        break;
        case Plane://force along x or y axis
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
}

void FootballGame::ForceCalculate()
{
    if (football_point_.ByteSize() == 0 
        || running_state_ == GameRunningState::IDEL 
        || training_mode_ == TrainingMode::NONE 
        || game_force_level_ == GameForceLevel::FORCE_LEVEL_ZERO)
	{
		return false;
	}
  
	static int angle_para0 = 2, angle_para2 = 15;//angle_para1=30,
	static float width_para1 = 0.05, width_para2 = 0.01;//width_para3=0.03,
	planar_data_type::PlanarPoint shoot_point = { 0, 0 };

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
            football_tcp.set_gamereplay(FootballTcp::Ubuntu2Unity_GameReplayEnum_game_pause);
			passive_force_.fx = 0.0;
			passive_force_.fy = 0.0;
			force_control_.setPlanarPassiveForce(passive_force_);//force->SetForceAndTorque();
			force_control_.initPidOutput();
        }
            break;
        case STOPING:
        {
            football_tcp_.set_gamereplay(FootballTcp::Ubuntu2Unity_GameReplayEnum_game_back);
            football_flag_ = 0;
            football_gone_ = 0;
            flag_clock_ = FootballPoint::Unity2Ubuntu_ClockwiseEnum_none;//圆弧上移动的方向
            football_point_.Clear();

            start_point_.x = tool_pose_in_base_.point.x;
            start_point_.y = tool_pose_in_base_.point.y;
        }
            break;
        case REPLAYING:
        {
            passive_force_.fx = 0.0;
			passive_force_.fy = 0.0;
            football_flag_ = 0;
            football_gone_ = 0;
            flag_clock_ = FootballPoint::Unity2Ubuntu_ClockwiseEnum_none;
            football_point_.Clear();
            
            start_point_.x = tool_pose_in_base_.point.x;
            start_point_.y = tool_pose_in_base_.point.y;

            running_state_ = PROCESSING;
            football_tcp_.set_gamereplay(FootballTcp::Ubuntu2Unity_GameReplayEnum_game_replay);
        }
            break;
        default:;
    };

	force_control_.updateForceInToolFrame(control_arm_rt_->getActualJoint());
    packageCommData();

}

void FootballGame::updatePassiveForceOnProcessing(ur_data_type::CartPose tcp_pose_in_base)
{
    football_tcp.set_gamereplay(FootballTcp::Ubuntu2Unity_GameReplayEnum_game_ing);
    
    if (football_point_.ifshoot())
    {
        if (football_gone_)
        {
            football_flag_ = 0;
            football_gone_ = 0;
            flag_clock_ = 0;
        }

        planar_data_type::PlanarPoint football, shoot, tangency, present_point;

        football.x = football_point_.pointfootball().x();
        football.y = football_point_.pointfootball().y();
        getPoseInWpToBase(transition_wp2base_, football);

        shoot.x = football_point_.pointshoot().x();
        shoot.y = football_point_.pointshoot().y();
        getPoseInWpToBase(transition_wp2base_, shoot);

        tangency.x = football_point_.pointtangency().x();
        tangency.y = football_point_.pointtangency().y();
        getPoseInWpToBase(transition_wp2base_, tangency);
    
        present_point.x = tool_pose_in_base_.point.x;
        present_point.y = tool_pose_in_base_.point.y;

	switch (training_mode_)
	{
		case TrainingMode::ACTIVE:
		{
		    passive_force_.fx = 0;
			passive_force_.fy = 0;
        }
			break;
		case TrainingMode::ASSIST:
		{	
			
        if (football_flag_ == 0)
        {
            PlanarForceValue planner_force = force_control_.getPlanarPassiveForce();
            force_control_.variableDigidity(start_point_, tangency, present_point, football_rigidity_, football_path_wid_, planner_force);//tcp
            force_control_.getPlanarPassiveForceInLine(start_point_, tangency, present_point, 0.0,speed_level_);

            passive_force_ = planner_force + force_control_.getPlanarPassiveForce(start_point_, tangency, present_point, speed_level_, 0.0);
            force_control_.setPlanarPassiveForce(passive_force_);

            double distance_tangency2tcp = getPlanarP2PDistance(tangency, present_point);  

            double angle = getVecendrialAngle(football, present_point, tangency);//
            if (if_points_coincide_ && distance_tangency2tcp < width_para1)
            {
                football_flag_ = 3;
            }
            else if (!if_points_coincide_ && abs(angle) < angle_para0)
            {
                //                        cout<<"normal force"<<passive_force[0]<<","<<passive_force[1]<<endl;
                football_flag_ = 1;
            }
        }
        else if (football_flag_ == 1)
        {
            float angle = fabs(getVecendrialAngle(football, present_point, shoot);
            planar_data_type::PlanarPoint second_point = tangency;

            double tem_force[2] = {};
            //??MoveAlongCircle
            force_control_.updatePlanarPassiveForceByMoveDirection(football_point_.clockdirection(), passive_force_);
            //??MoveToCircle  tem_force
            force_control_.getPlanarPassiveForceInCircle(football, football_point_.radium(), tem_force, football_rigidity_, football_path_wid_);
      
            passive_force_.fx += tem_force[0];
            passive_force_.fy += tem_force[1];
            
            if (angle < angle_para2)
            {
                football_flag_ = 3;
                shoot_point = present_point;
            }
        }
        else if (football_flag_ == 2)
        {
            //force->MoveToPoint(football, move_force_, 0);//??move_force_
            force_control_.getPlanarPassiveForce(football,present_point,move_force_,0.0);
            double distance =getPlanarP2PDistance(shoot_point,present_point); 
            if (distance < width_para2)
            {
                passive_force_.fx *= distance / width_para2;
                passive_force_.fy *= distance / width_para2;
            }

            planar_data_type::PlanarPoint football;
            football.x = football_point_.pointfootball().x();
            football.y = football_point_.pointfootball().y() ;
            getPoseInWpToBase(transition_wp2base_, football);

            if (getPlanarP2PDistance(football, present_point) < 0.01)
            {
                football_flag_ = 3;
            }
        }
        else if (football_flag_ == 3)
        {
            passive_force_.fx *= 0.96;
			passive_force_.fy *= 0.96;
        }
        }
			break;
		case TrainingMode::IMPEDANCE:
		{
			force_control_.setPassiveForce(force_control_.getCartRandomPassiveForce(maximum_force_, RANDOM_TIME));
		}
			break;
	}

        in_case_force[0] = passive_force_.fx;
        in_case_force[1] = passive_force_.fy;
    }
    else 
    {
        passive_force_.fx *= 0.96;
		passive_force_.fy *= 0.96;
        football_gone_ = 1;
        start_point_.x = tool_pose_in_base_.point.x;
        start_point_.y = tool_pose_in_base_.point.y;
        flag_clock_ = FootballPoint::Unity2Ubuntu_ClockwiseEnum_none;
    }
}

void FootballGame::packageCommData()
{
    //football_tcp_.set_game_length(cut_distance_);
    football_tcp_.set_tcp_pose(0, tool_pose_in_base_.point.x);
    football_tcp_.set_tcp_pose(1, tool_pose_in_base_.point.y);
    football_tcp_.set_tcp_pose(2, tool_pose_in_base_.rpy.rz);
    football_tcp_.set_sensor_force_z(ft_sensor_->getSensorData().force.fz);
    football_tcp_.set_game_device(SCREEN); 

    auto tcp_tem = force->TCP_TEM();
	
    football_tcp_.set_tcpspeed(0, sqrt(pow(force->RawFTData()[0], 2)) + pow(force->RawFTData()[1], 2));
   
    #if 0
    MessageHead Head;
    Head.Command = 2006;
    Head.Length = football_tcp_.ByteSize();
    CIOBuffer Buffer;
    Head.Serialize(Buffer.Buffer);
    int8_t * ptr = Buffer.Buffer + MessageHead::Size();
    football_tcp_.SerializeToArray(ptr, football_tcp_.ByteSize());
    Buffer.Length = Head.Length + sizeof(int16_t) + MessageHead::Size();
    AppServer::GetInstance().WriteBuffer(&Buffer);
    #endif
}

void FootballGame::encodeRuntimeInfo(char* buf, int &buf_size)
{
    buf_size = unity2ubuntu_.ByteSize();
    ubuntu2unity_.SerializeToArray(buf, buf_size);
}

bool FootballGame::decodeRuntimeCmd(char* buf, int buf_size)
{
   if(!unity2ubuntu_.ParseFromArray(buf, buf_size))
    {
        printf("FootballGame: parse data failed.\n");
        return false;
    }

    

    return true;
}

std::string FootballGame::getName() 
{
    return "TrajRecurrent";
}
