
#include <unistd.h> 
#include <cstring>
// #include <string>
#include "ft_sensor.h"

using namespace force_data_type;

FTSensor::FTSensor()
{
    memset(&sensor_data_, 0, sizeof(sensor_data_));
    memset(request_, 0, REQUEST_DATA_BYTES);
    memset(request_bias_, 0, REQUEST_DATA_BYTES);
    memset(request_filter_, 0, REQUEST_DATA_BYTES);
    memset(request_output_rate_, 0, REQUEST_DATA_BYTES);
    
    queue_count_ = 0;
	filter_wid_ = 8;
	smooth_res_.clear();

    is_running_ = false;
    connect_times_ = 0;
    connected_ = false;

	*(uint16_t*)&request_[0] = htons(0x1234);
	*(uint16_t*)&request_[2] = htons(COMMAND);
	*(uint32_t*)&request_[4] = htonl(NUM_SAMPLES);

	*(uint16_t*)&request_filter_[0] = htons(0x1234);
	*(uint16_t*)&request_filter_[2] = htons(0x0081);
	*(uint32_t*)&request_filter_[4] = htonl(0x0006);

	*(uint16_t*)&request_output_rate_[0] = htons(0x1234);
	*(uint16_t*)&request_output_rate_[2] = htons(0x0082);
	*(uint32_t*)&request_output_rate_[4] = htonl(0x0002);

    *(uint16_t*)&request_bias_[0] = htons(0x1234);
    *(uint16_t*)&request_bias_[2] = htons(0x0042);
    *(uint32_t*)&request_bias_[4] = htonl(0xFF);
    mutex_ = PTHREAD_MUTEX_INITIALIZER;
    
    memset(&force_sum_, 0, sizeof(force_sum_));
    memset(&force_avg_, 0, sizeof(force_avg_));
    memset(&force_value_, 0, sizeof(force_value_));
}

FTSensor::~FTSensor()
{
    stop();
}

bool FTSensor::start(std::string ip, bool zero_sensor)
{
    udp_client_ = new UdpClient(ip, UDP_SPORT);

    if(!udp_client_->inetSocket())
    {
        printf("FTSensor: UDP communication initial failed.\n");
        return false;
    }

    if(!ThreadBase<FTSensor>::start(SCHED_RR))
    {
        printf("FTSensor: create thread failed\n");
        return false;
    }

    if(udp_client_->inetSend(request_, sizeof(request_)) < 0)
    {
        printf("FTSensor: send request failed.\n");
        return false;
    }
    usleep(10000);

    if (zero_sensor)
    {
        if(udp_client_->inetSend(request_bias_, sizeof(request_bias_)) < 0)
        {
            printf("FTSensor: send request_bias failed.\n");
            return false;
        }
    }

	usleep(10000);

    if(udp_client_->inetSend(request_filter_, sizeof(request_filter_)) < 0)
    {
        printf("FTSensor: send request_filter failed.\n");
        return false;
    }
	usleep(10000);

    if(udp_client_->inetSend(request_output_rate_, sizeof(request_output_rate_)) < 0)
    {
        printf("FTSensor: send request_output_rate failed.\n");
        return false;
    }
	usleep(10000);

	for (int32_t i = 0; i < 6; i++)	
    {
        FTSensorData force_data;
        smooth_res_.push_back(force_data);
    }

    if(!ThreadBase<FTSensor>::start(50))
    {
        printf("FTSensor: create thread failed\n");
        return false;
    }

    printf("FTSensor: start successful\n");

    connected_ = true;
    is_running_ = true;
    return true;
}

void FTSensor::run()
{
	uint8_t recv_buf[FT_SENSOR_UDP_CLIENT_BUF_SIZE]; /* The raw response data received from the Net F/T. */
    int recv_bytes = 0;
    
	while (is_running_)
	{
        if(connected_)
        {
            // LEN = ARM_COMM_BUF_SIZE - offset; // to do ...
            memset(recv_buf, 0, FT_SENSOR_UDP_CLIENT_BUF_SIZE);
            recv_bytes = udp_client_->inetRecv(recv_buf, sizeof(recv_buf));
            if(recv_bytes >= 0)
            {
                if(recv_bytes == 0) continue;
                if(recv_bytes < FT_SENSOR_UDP_CLIENT_BUF_SIZE)
                {
                    printf("FTSensor: recv data length error.\n");
                    continue;
                }
                decodeSensorData(recv_buf, 10000.0);
            }
            else
            {
                connected_ = false;
                printf("FTSensor: UDP communication is disconected.\n");
                continue;
            }

            //usleep(100000);
        }
        else
        {
            sleep(1);
            connect_times_++;
            printf("FTSensor: Trying to connect: the %dth time.\n", connect_times_);
            memset(recv_buf, 0, FT_SENSOR_UDP_CLIENT_BUF_SIZE);

            if(!udp_client_->inetRecv(recv_buf, sizeof(recv_buf) < 0))
            {
                printf("FTSensor: Connection failed for the %dth time\n", connect_times_);
           
                if(connect_times_ > FT_SENSOR_UDP_CONNECT_MAX_COUNT)
                {
                    udp_client_->inetClose();
                    printf("FTSensor: Connection failed, program exited\n");
                    return;
                }

                continue;
            }
            
            connected_ = true;
            printf("FTSensor: UDP communication connection success\n"); 
        }
    }
}

void FTSensor::decodeSensorData(uint8_t* buf, double scale_factor)
{
    Response resp;
    resp.rdt_sequence = ntohl(*(uint32_t*)&buf[0]);
    resp.ft_sequence = ntohl(*(uint32_t*)&buf[4]);
    resp.status = ntohl(*(uint32_t*)&buf[8]);
    for (int i = 0; i < 6; ++i)
    {
        resp.ft_data[i] = ntohl(*(int32_t*)&buf[12 + i * 4]);
    }

    FTSensorData last_force;
    last_force.force.fx = static_cast<double>(resp.ft_data[0]) / scale_factor;
    last_force.force.fy = static_cast<double>(resp.ft_data[1]) / scale_factor;
    last_force.force.fz = static_cast<double>(resp.ft_data[2]) / scale_factor;
    last_force.torque.tx = static_cast<double>(resp.ft_data[3]) / scale_factor / 10.0;
    last_force.torque.ty = static_cast<double>(resp.ft_data[4]) / scale_factor / 10.0;
    last_force.torque.tz = static_cast<double>(resp.ft_data[5]) / scale_factor / 10.0;
    // last_force.printValue("last_force: ");
    if (queue_count_ < filter_wid_ - 1)
    {
        smooth_res_.erase(smooth_res_.begin());
        force_sum_ += last_force;
        queue_count_++;
        smooth_res_.push_back(last_force);
    }
    else if (queue_count_ == filter_wid_ - 1)
    {
        smooth_res_.erase(smooth_res_.begin());
        smooth_res_.push_back(last_force);

        force_sum_ += last_force;

        double temp = (double)(1.0 /filter_wid_);

        force_avg_ = force_sum_ * (double)(1.0 /filter_wid_);

        FTSensorData smooth_res_begin = *smooth_res_.begin();
        force_sum_ -= smooth_res_begin;
    }

    pthread_mutex_lock(&mutex_);
    sensor_data_ = force_avg_;
    //sensor_data_.printValue("sensor_data_: ");
    pthread_mutex_unlock(&mutex_);
}

void FTSensor::stop()
{
    if(!is_running_) return;
    removeBias();
    is_running_ = false;
    ThreadBase<FTSensor>::stop();
    udp_client_->inetClose();

    if(udp_client_ != nullptr)
    {
        delete udp_client_;
        udp_client_ = nullptr;
    }

	smooth_res_.clear();
}

bool FTSensor::removeBias()
{
	//去除力传感器偏置
	return udp_client_->inetSend((uint8_t*)request_bias_, sizeof(request_bias_));
}

FTSensorData FTSensor::getSensorData()
{
    FTSensorData data;
    pthread_mutex_lock(&mutex_);
    data = sensor_data_;
    pthread_mutex_unlock(&mutex_);
    return data;
}


