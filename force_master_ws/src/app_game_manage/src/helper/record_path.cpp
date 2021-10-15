#include <fstream>
#include <cstring>
#include <string>
#include "record_path.h"
#include "thread_pool.h"
#include "path_file.h"
using namespace ur_data_type;
using namespace math_calculate;

RecordPath::RecordPath()
{
	joint_file_ = JOINT_PATH_FILE_DIR;
	cart_pose_file_ = CART_PATH_FILE_DIR;
	joint_file_impendance_ = JOINT_IMPENDANCE_PATH_FILE_DIR;
	cart_pose_file_impendance_ = CART_IMPENDANCE_PATH_FILE_DIR;
    cart_path_.clear();
    joint_path_.clear();
    cart_path_impendance_.clear();
    joint_path_impendance_.clear();

    impendance_count_ = 0;

	thread_index_ = 1;
	is_running_ = false;
	sleep_count_ = 0;

    memset(&record_limit_, 0, sizeof(record_limit_));
}

RecordPath::~RecordPath()
{
    cart_path_.clear();
    joint_path_.clear();
    cart_path_impendance_.clear();
    joint_path_impendance_.clear();

    stop();
}

bool RecordPath::start(ControlArmRealtime* robot_rt, struct RecordLimit limit)
{
    if(robot_rt == nullptr) 
    {
        printf("RecordPath: robot_rt pointer is nunnptr\n");
        return false;
    }

    robot_rt_ = robot_rt;
    
	joint_ = robot_rt_->getActualJoint();
    if (joint_.jVal[0] < DOUBLE_ZERO
            && joint_.jVal[1] < DOUBLE_ZERO
            && joint_.jVal[2] < DOUBLE_ZERO
            && joint_.jVal[3] < DOUBLE_ZERO
            && joint_.jVal[4] < DOUBLE_ZERO
            && joint_.jVal[5] < DOUBLE_ZERO
    )
    {
        printf("RecordPath: joint data error\n");
        return false;
    }

	joint_path_.push_back(joint_); // 存入轨迹复现轨迹
	joint_path_impendance_.push_back(joint_); // 存入阻抗复现轨迹
	
	double rotation[3][3];
    ur_kine_.FK(joint_, cart_pose_, rotation);
	cart_path_.push_back(cart_pose_);
	cart_path_impendance_.push_back(cart_pose_);
    
    is_running_ = true;

	ThreadPool::getInstance().registerHandler(this, RECORD_PATH_THREAD_INDEX);
	printf("RecordPath is running!\n");
	return true;
}

void RecordPath::process()
{
	if (!is_running_) return;

	if (sleep_count_ < 10)
	{
		sleep_count_++;
		return;
	}
	sleep_count_ = 0;

	joint_ = robot_rt_->getActualJoint();

	if (fabs(joint_.jVal[0]) < 0.01
		&& fabs(joint_.jVal[1]) < 0.01
		&& fabs(joint_.jVal[2]) < 0.01
		&& fabs(joint_.jVal[3]) < 0.01
		&& fabs(joint_.jVal[4]) < 0.01
		&& fabs(joint_.jVal[5]) < 0.01)
	{
		printf("TrackerRecord: joint value error\n");
		return;
	}

	getJointPath(joint_, impendance_count_);

	double rotation[3][3];
    ur_kine_.FK(joint_, cart_pose_, rotation);

	getCartPath(cart_pose_, impendance_count_);

	if (impendance_count_ >= 5)
	{
		impendance_count_ = 0;
	}
	else
	{
		impendance_count_++;
	}
}

void RecordPath::stop()
{
	if (!is_running_) return;

	is_running_ = false;
	ThreadPool::getInstance().deregisterHandler(RECORD_PATH_THREAD_INDEX);

	scaleRpyDistanceP2P(cart_pose_last_.rpy, cart_pose_.rpy);
	if (getP2PDistance(cart_pose_last_.point, cart_pose_.point) > 0.01)
	{
		cart_path_.push_back(cart_pose_);
		cart_path_impendance_.push_back(cart_pose_);
	}

	if (maxJointDistanceP2P(joint_last_, joint_) > 0.02)
	{
		joint_path_.push_back(joint_); // 存入轨迹复现轨迹
		joint_path_impendance_.push_back(joint_); // 存入阻抗复现轨迹
	}
	printf("RecordPath: path size: %d, %d, %d, %d\n", 
		static_cast<int>(joint_path_.size()), 
		static_cast<int>(joint_path_impendance_.size()), 
		static_cast<int>(cart_path_.size()), 
		static_cast<int>(cart_path_impendance_.size()));

	if (!joint_path_.empty())
	{
		recordJointPath(joint_file_);
	}
	if (!joint_path_impendance_.empty())
	{
		recordJointPathImpendance(joint_file_impendance_);
	}
	if (!cart_path_.empty())
	{
		recorCartPath(cart_pose_file_);
	}
	if (!cart_path_impendance_.empty())
	{
		recordCartPathImpendance(cart_pose_file_impendance_);
	}
	printf("RecordPath: record path successful. handler removed\n");
}

bool RecordPath::isRunning()
{
    return is_running_;
}

void RecordPath::getJointPath(ur_data_type::Joint joint, int impendance_count)
{
	/* 处理第一个轨迹点 */
	if (joint_path_.empty())
	{
		joint_path_.push_back(joint);
		joint_path_impendance_.push_back(joint);
		joint_last_ = joint;
		return;
	}

	/* 处理余下的点 */
	if (maxJointDistanceP2P(joint_last_, joint) < 0.02)
	{
		return;
	}

	joint_path_.push_back(joint); // 存入轨迹复现轨迹
	if (impendance_count > 5)
	{
		joint_path_impendance_.push_back(joint); // 存入阻抗复现轨迹
	}
}

void RecordPath::recordJointPath(std::string file)
{
	std::ofstream file_joint(joint_file_.c_str(), fstream::out|ios_base::trunc);
	for (int i = 0; i != static_cast<int>(joint_path_.size()); ++i)
	{
		file_joint
			<< joint_path_[i].jVal[0] << std::string(",")
			<< joint_path_[i].jVal[1] << ","
			<< joint_path_[i].jVal[2] << ","
			<< joint_path_[i].jVal[3] << ","
			<< joint_path_[i].jVal[4] << ","
			<< joint_path_[i].jVal[5] << std::endl;
	}
	file_joint.close();
}

void RecordPath::recordJointPathImpendance(std::string file)
{
	std::ofstream file_joint_impendance(joint_file_impendance_.c_str(), fstream::out|ios_base::trunc);

	for (Joint joint : joint_path_impendance_)
	{
		file_joint_impendance
			<< joint.jVal[0] << ","
			<< joint.jVal[1] << ","
			<< joint.jVal[2] << ","
			<< joint.jVal[3] << ","
			<< joint.jVal[4] << ","
			<< joint.jVal[5]
			<< std::endl;
	}
	file_joint_impendance.close();
}

void RecordPath::getCartPath(ur_data_type::CartPose pose, int impendance_count)
{
	/* 处理第一个轨迹点 */
	if (cart_path_.empty())
	{
		cart_path_.push_back(pose);
		cart_path_impendance_.push_back(pose);

		cart_pose_last_ = pose;
	}

	double distance = getP2PDistance(cart_pose_last_.point, pose.point);
	if (distance > record_limit_.cart_p2p_distance_min)
	{
		scaleRpyDistanceP2P(cart_pose_last_.rpy, pose.rpy);
		cart_path_.push_back(pose); // 存入轨迹复现轨迹

		if (impendance_count_ > 5)
		{
			cart_path_impendance_.push_back(pose); // 存入阻抗复现轨迹
		}

		cart_pose_last_ = pose;
	}
}

void RecordPath::recorCartPath(std::string file)
{
	std::ofstream file_cart(cart_pose_file_.c_str(), fstream::out|ios_base::trunc);
	for (CartPose pose : cart_path_)
	{
		file_cart
			<< pose.point.x << ","
			<< pose.point.y << ","
			<< pose.point.z << ","
			<< pose.rpy.rx << ","
			<< pose.rpy.ry << ","
			<< pose.rpy.rz
			<< std::endl;
	}
	file_cart.close();
}

void RecordPath::recordCartPathImpendance(std::string file)
{
	std::ofstream file_impendance(cart_pose_file_impendance_.c_str(), ios::out|ios::trunc);
	for (CartPose pose : cart_path_impendance_)
	{
		file_impendance
			<< pose.point.x << ","
			<< pose.point.y << ","
			<< pose.point.z << ","
			<< pose.rpy.rx << ","
			<< pose.rpy.ry << ","
			<< pose.rpy.rz
			<< std::endl;
	}
	file_impendance.close();
}

std::string RecordPath::getName() 
{
    return "RecordPath";
}
