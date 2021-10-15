#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <mutex>

#include <ros/ros.h>

#include "controller.h"
#include "app_game_model.h"
#include "tcp_server_manager.h"

using namespace app_game_model;

Controller* g_controller = nullptr;
TcpServerManager* g_tcp_server = nullptr;
std::mutex g_lock;

void onExit(int signal_n)
{
    std::unique_lock<std::mutex> l{ g_lock };
#if 0
    if (g_tcp_server->isRunning())
    {
        g_tcp_server->stopService();
    }
#endif
    if (g_controller->isRunning())
    {
        g_controller->stop();
    }

}

int main(int argc, char** argv)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    sigprocmask(SIG_BLOCK, &set, NULL);

#if 0
    g_controller = Controller::getInstance();
    if (!g_controller->init())
    {
        printf("init controller failed.\n");
        return -1;
    }

    printf("init controller successful.\n");
    //sleep(1);
    
    if (!g_controller->start())
    {
        printf("start controller failed.\n");
        return -1;
    }

    printf("init controller successful.\n");
    sleep(1);
#endif
#if 1
    g_tcp_server = TcpServerManager::getInstance();
    if (!g_tcp_server->initServer())
    {
        printf("init servers failed.\n");
        return -1;
    }
    printf("init servers successful.\n");

    if (!g_tcp_server->startService())
    {
        printf("start servers failed.\n");
        return -1;
    }
    printf("start servers successful.\n");
#endif
    ros::init(argc, argv, "tcp_comm_node") ;
    ros::NodeHandle nh;
    ROS_INFO_STREAM("tcp_comm_node running.") ;
    
    while (g_tcp_server->isRunning())
    {
        signal(SIGINT, onExit);
        signal(SIGTERM, onExit);
        signal(SIGSEGV, onExit);
        
        usleep(1000000);
    }

#if 0
    if (g_controller->isRunning())
    {
        g_tcp_server->stopService();
        g_controller->stop();

    }
#endif
    
    printf("stop controller successful.\n");
    return 0;
}
