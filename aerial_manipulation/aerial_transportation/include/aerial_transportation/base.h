#ifndef BASE_PLUGIN_H
#define BASE_PLUGIN_H


/* ros */
#include <ros/ros.h>

#include <nav_msgs/Odometry.h>
#include <aerial_robot_base/FlightNav.h>
#include <sensor_msgs/Joy.h>
#include <geometry_msgs/Point.h>
#include <geometry_msgs/Pose2D.h>
#include <tf/transform_listener.h> /* for vector3 */


namespace aerial_transportation
{
  class Base
  {
  public:
    virtual void initialize(ros::NodeHandle nh, ros::NodeHandle nhp)  = 0;

    virtual ~Base() { }

    static const uint8_t IDLE_PHASE = 0;
    static const uint8_t APPROACH_PHASE = 1;
    static const uint8_t GRASPING_PHASE = 2;
    static const uint8_t GRASPED_PHASE = 3;
    static const uint8_t TRANSPORT_PHASE = 4;
    static const uint8_t DROPPING_PHASE = 5;
    static const uint8_t RETURN_PHASE = 6;

  protected:
    /* ros node */
    ros::NodeHandle nh_;
    ros::NodeHandle nhp_;

    /* main thread */
    ros::Timer  func_timer_;

    /* ros publisher & subscirber */
    ros::Publisher uav_nav_pub_;
    ros::Subscriber uav_state_sub_;
    ros::Subscriber object_pos_sub_;
    ros::Subscriber joy_stick_sub_;

    /* rosparam based variables */
    bool debug_;
    std::string uav_nav_pub_name_;
    std::string uav_state_sub_name_;
    std::string object_pos_sub_name_;
    double func_loop_rate_;
    double nav_vel_limit_;
    double vel_nav_threshold_;
    double vel_nav_gain_;
    double approach_threshold_; // the convergence condition for object approach
    double approach_count_; //the convergence duration (sec)
    bool object_head_direction_; //whether need to consider the head direction of object
    double object_height_; //TODO: should be detected!!!
    double falling_speed_; //the vel to fall down to object
    double grasping_height_offset_; //the offset between the bottom of uav and the top plat of object
    double transportation_threshold_; // the convergence condition to carry to box
    double transportation_count_; // the convergence duration
    double dropping_offset_;  //the offset between the top of box and the bottom of object
    geometry_msgs::Point box_point_; //TODO: should be detected!!!
    tf::Vector3 object_offset_, box_offset_; //TODO: should be calculated!!!

    /* base variable */
    int phase_;
    int contact_cnt_;
    bool get_uav_state_;
    bool object_found_;
    double target_height_;
    geometry_msgs::Pose uav_position_;
    geometry_msgs::Pose uav_init_position_; // not important
    geometry_msgs::Pose2D object_position_;

    /* base function */
    void baseInit(ros::NodeHandle nh, ros::NodeHandle nhp);
    void mainFunc(const ros::TimerEvent & e);
    virtual void graspPhase() = 0;
    virtual void dropPhase() = 0;

    virtual void rosParamInit() = 0;
    void baseRosParamInit();

    void stateCallback(const nav_msgs::OdometryConstPtr & msg);
    void objectPoseCallback(const geometry_msgs::Pose2DConstPtr & object_msg);
    void joyStickCallback(const sensor_msgs::JoyConstPtr & joy_msg);
    virtual void joyStickAdditionalCallback(const sensor_msgs::JoyConstPtr & joy_msg){}

  };

};

#endif  // OBJECT_TRANSPORTATION_H
