#ifndef FOOTBALL_GAME_H
#define FOOTBALL_GAME_H

#include "app_game.h"
#include <future>
#include "force_datatype.h"
#include "ft_sensor.h"
#include "app_game.h"
#include "force_control.h"
#include "ur_kinematics.h"
#include "solve_jacobian.h"
#include "matrix_inverse.h"
#include "planar_datatype.h"

#include "FootballPoint.pb.h"
#include "FootballTcp.pb.h"
#include "FootballMode.pb.h"

//to check 
#define PUZZLE_LOOP_NUM (10)
#define PUZZLE_PATH_WIDTH (0.005)fixing_state_
#define TEST_COUNT (8)
#define RANDOM_TIME (1000.0)

class FootballGame: public AppGame, public std::enable_shared_from_this<FootballGame>
{
	
public:
   	FootballGame();
	~FootballGame();
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
   
	FootballPoint::Unity2Ubuntu football_point_;
	FootballTcp::Ubuntu2Unity football_tcp_;
	int flag_clock_ = FootballPoint::Unity2Ubuntu_ClockwiseEnum_none;//圆弧上移动的方向
    planar_data_type::PlanarPoint start_point_;
    planar_data_type::PlanarForceValue passive_force_;

    ur_data_type::CartPose tool_pose_in_base_;
    ur_data_type::CartPose tool_pose_in_wp_;

    ControlArmRealtime *control_arm_rt_;
    FTSensor* ft_sensor_;
    UrKinematics ur_kine_;
    ForceControl force_control_;
    SloveJacobian solve_jacobian_;
    MatrixInverse44 matrix_inverse_44_;

    unsigned int game_action_;
    int football_flag_;//力控的线段序号
    int football_gone_;//足球是否被踢出
    int speed_level_;

	double football_rigidity_;
	double football_path_wid_;
	double football_cut_switch_;
    double football_cut_slow_ = 3 * football_cut_switch_;
	double cut_force_; //沿着某条线切的末端力
    double move_force_; //移动到某个点的末端力
    double maximum_force_;//random mode 下的最大力
    double in_case_force_[2];//用来储存足球被踢出前一瞬间的力，使足球被踢出后力逐渐减小
    double transition_wp2base_[4][4];
    double start_time_;
    double height_;

    bool is_processing_;
    bool if_points_coincide_;//??

    void forceCalculate();
    void packageCommData();
    bool onceProcess();
    void updatePassiveForceOnProcessing(ur_data_type::CartPose tcp_pose_in_base);
    void updatePassiveForceByTrainMode(int train_mode, PlanarPoint football,PlanarPoint shoot, PlanarPoint tency, PlanarPoint tcp);
    void initRigidity(unsigned int rigidity_level);
    void initCutAndMoveForce(unsigned int force_level);
    double getWallTime();  
	
};

#endif




