#include <cstring>
#include "ur_datatype.h"
#include "schulteGrid_game.h"
#include "math_calculate.h"

using namespace ur_data_type;
using namespace app_game_model;
using namespace force_data_type;
using namespace math_calculate;
using namespace planar_data_type;


void SchulteGridGame::initRigidity(unsigned int rigidity_level)
{
    switch(rigidity_level)
    {
        case RIGIDITY_ZERO:
        {
            cut_force_ = 0.0;
            move_force_ = 0.0;
        }
        break;
        case RIGIDITY_SMALL:
        {
            puzzle_rigidity_ = 0.8;//0.5
		    schulte_path_wid_ = 0.005;
        }
        break;
        case RIGIDITY_MEDIUM:
        {
            puzzle_rigidity_ = 1.2;//0.75
		    schulte_path_wid_ = 0.003;
        }
        break;
        case RIGIDITY_LARGE:
        {
            puzzle_rigidity_ = 1.5;//1
		    schulte_path_wid_ = 0.002;
        }
        break;
	    default:
            printf("\n app_game_pathwidth_param!\n");
            exit(1);
		break;
    };
    
}

void SchulteGridGame::initCutAndMoveForce(unsigned int force_level)
{
    switch(force_level)
    {
        case FORCE_LEVEL_ZERO:
        {
            cut_force_ = 0.0;
            move_force_ = 0.0;
        }
        break;
        case FORCE_LEVEL_SMALL:
        {
            cut_force_ = 4.0;
            move_force_ = 4.0;
        }
        break;
        case FORCE_LEVEL_MEDIUM:
        {
            cut_force_ = 6.0;
            move_force_ = 6.0;
        }
        break;
        case FORCE_LEVEL_LARGE:
        {
            cut_force_ = 8.0;
            move_force_ = 8.0;
        }
        break;
        default:
        printf("\napp_force_level_param error!\n");
        break;
    };
    
}