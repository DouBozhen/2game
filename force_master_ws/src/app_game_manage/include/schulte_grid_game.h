#ifndef SCHULTEGRID_GAME_H
#define SCHULTEGRID_GAME_H

#include <future>

#include "force_datatype.h"
#include "ft_sensor.h"
#include "app_game.h"
#include "force_control.h"
#include "ur_kinematics.h"
#include "solve_jacobian.h"
#include "SchulteGrid.pb.h"
#include "math_calculate.h"


#define PUZZLE_LOOP_NUM (10)  // to check
#define PUZZLE_PATH_WIDTH (0.005) // to check
#define TEST_COUNT (8) // to check
#define RANDOM_TIME (500.0) // to check
// #include "../protocol/GameMode.pb.h"

class SchulteGridGame :	public AppGame
{
public:
	SchulteGridGame();
	~SchulteGridGame();
	virtual bool initParams(app_game_model::AppGameParams params) override;
	virtual bool start(ControlArmRealtime* control_arm_rt, FTSensor* ft_sensor) override;
	virtual void pause() override;
	virtual void stop() override;
	virtual void replay() override;
    virtual void process() override;
    virtual std::string getName() override;
    bool isRunning();
    virtual void encodeRuntimeInfo(char* buf, int &buf_size) override;
    virtual bool decodeRuntimeCmd(char* buf, int buf_size) override;

private:
    ControlArmRealtime *control_arm_rt_;
    ForceControl force_control_;
    FTSensor* ft_sensor_;
    UrKinematics ur_kine_;

	SchulteGrid::Unity2Ubuntu unity2ubuntu_;
	SchulteGrid::Ubuntu2Unity ubuntu2unity_;

    double height_;
    planar_data_type::PlanarPoint  start_point_;
    planar_data_type::PlanarPoint  start_point_next_ ;

    float schulte_rigidity_;
    float schulte_path_wid_;
    double cut_force_;                          /* 沿着某条线切的末端力 */
    double move_force_;                         /* 移动到某个点的末端力 */
    double maximum_force_;                      /* random mode 下的最大力 */
    planar_data_type::PlanarForceValue passive_force_;

    /* to delete */
    bool if_set_start_;
    bool alock_ ;
    bool if_ini_ ;
    float k_speed_ ;

    int speed_level_;
    int cut_distance_;
    // double passive_force_[2] ;//力的临时变量
    double transition_wp2base_[4][4];
    ur_data_type::CartPose tool_pose_in_base_;
    ur_data_type::CartPose tool_pose_in_wp_;

    /* to delete end */

    void forceCalculate();
    void packageCommData();
    bool onceProcess();
    void updatePassiveForceOnProcessing(ur_data_type::CartPose tcp_pose_in_base);
    void updatePassiveForceByTrainMode(int train_mode, PlanarPoint start, PlanarPoint end, PlanarPoint tcp);
    void initRigidity(unsigned int rigidity_level);
    void initCutAndMoveForce(unsigned int force_level);
};
#endif


