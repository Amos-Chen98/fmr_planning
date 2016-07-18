#include <aerial_transportation/method/hydrus.h>

namespace aerial_transportation
{
  void Hydrus::initialize(ros::NodeHandle nh, ros::NodeHandle nhp)
  {
    /* super class init */
    baseInit(nh, nhp);

    /* ros param init */
    rosParamInit();

    /* ros pub sub init */
    joint_ctrl_pub_ = nh_.advertise<sensor_msgs::JointState>(joint_ctrl_pub_name_, 1);
    joint_states_sub_ = nh_.subscribe<sensor_msgs::JointState>(joint_states_sub_name_, 1, &Hydrus::jointStatesCallback, this, ros::TransportHints().udp());
    joint_motors_sub_ = nh_.subscribe<dynamixel_msgs::MotorStateList>(joint_motors_sub_name_, 1, &Hydrus::jointMotorStatusCallback, this, ros::TransportHints().udp());

    /* base variables init */
    joint_num_ = 0;
    sub_phase_ = SUB_PHASE1;
    contact_ = false;
    modification_start_time_ = ros::Time::now();
  }

  void Hydrus::graspPhase()
  {
    static bool once_flag = true;
    static int cnt = 0;

    if(joint_num_ == 0) return;

    if(sub_phase_ == SUB_PHASE1)
      {// change pose

        if(once_flag)
          {
            once_flag = false;
            /* send joint angles */
            sensor_msgs::JointState joint_ctrl_msg;
            joint_ctrl_msg.header.stamp = ros::Time::now();
            for(int i = 0; i < joint_num_; i++)
              joint_ctrl_msg.position.push_back(joints_control_[i].approach_angle);
            joint_ctrl_pub_.publish(joint_ctrl_msg);
          }

        /* phase shift condition */
        bool pose_fixed_flag_ = true;
        for(int i = 0; i < joint_num_; i++)
          if(joints_control_[i].moving) pose_fixed_flag_ = false;
        if(pose_fixed_flag_)
          {
            if(++cnt > (pose_fixed_count_ * func_loop_rate_))
              {
                ROS_INFO("Grasping Sub Phase: Succeed to change to sub phase 2");
                sub_phase_ ++;
                cnt = 0; // convergence reset
                once_flag = true;

                /* send nav msg */
                aerial_robot_base::FlightNav nav_msg;
                nav_msg.header.stamp = ros::Time::now();
                nav_msg.pos_xy_nav_mode = aerial_robot_base::FlightNav::POS_MODE;
                nav_msg.target_pos_x = object_position_.x + object_offset_.x();
                nav_msg.target_pos_y = object_position_.y + object_offset_.y();
                nav_msg.pos_z_nav_mode = aerial_robot_base::FlightNav::POS_MODE;
                nav_msg.target_pos_z = object_height_;
                nav_msg.psi_nav_mode = aerial_robot_base::FlightNav::POS_MODE;
                nav_msg.target_psi = object_position_.theta + object_offset_.z();
                uav_nav_pub_.publish(nav_msg);
              }
          }
      }
    else if(sub_phase_ == SUB_PHASE2)
      {// descending
        if(fabs(object_height_ - uav_position_.position.z) < grasping_height_threshold_)
          {
            ROS_INFO("Grasping Sub Phase: Succeed to change to sub phase 3");
            sub_phase_++;

            /* set init target angle */
            for(int i = 0; i < joint_num_; i++)
              joints_control_[i].target_angle = joints_control_[i].approach_angle;

            /* send nav msg: shift to vel control mode */
            aerial_robot_base::FlightNav nav_msg;
            nav_msg.header.stamp = ros::Time::now();
            nav_msg.pos_xy_nav_mode = aerial_robot_base::FlightNav::VEL_MODE;
            nav_msg.target_pos_x = 0;
            nav_msg.target_pos_y = 0;
            nav_msg.pos_z_nav_mode = aerial_robot_base::FlightNav::NO_NAVIGATION;
            nav_msg.psi_nav_mode = aerial_robot_base::FlightNav::NO_NAVIGATION;
            uav_nav_pub_.publish(nav_msg);
          }
      }
    else if(sub_phase_ == SUB_PHASE3)
      {// grasping, most important

        /* calculate the joint command */
        for(int i = 0; i < joint_num_; i++)
          {
            if(joints_control_[i].target_angle != joints_control_[i].hold_angle)
              joints_control_[i].target_angle += ((joints_control_[i].hold_angle - joints_control_[i].approach_angle) / func_loop_rate_ / hold_count_);

            //limitation
            if(joints_control_[i].target_angle > joints_control_[i].hold_angle
               && joints_control_[i].holding_rotation_direction == 1)
              joints_control_[i].target_angle = joints_control_[i].hold_angle;
            if(joints_control_[i].target_angle < joints_control_[i].hold_angle
               && joints_control_[i].holding_rotation_direction == -1)
              joints_control_[i].target_angle = joints_control_[i].hold_angle;
          }

        /* send joint angles */
        sensor_msgs::JointState joint_ctrl_msg;
        joint_ctrl_msg.header.stamp = ros::Time::now();
        for(int i = 0; i < joint_num_; i++)
          joint_ctrl_msg.position.push_back(joints_control_[i].target_angle);
        joint_ctrl_pub_.publish(joint_ctrl_msg);
      }
  }

  void Hydrus::dropPhase()
  {
    static bool once_flag = true;
    static int cnt = 0;

    if(joint_num_ == 0) return;

    if(once_flag)
      {
        once_flag = false;
        /* send joint angles */
        sensor_msgs::JointState joint_ctrl_msg;
        joint_ctrl_msg.header.stamp = ros::Time::now();
        for(int i = 0; i < joint_num_; i++)
          joint_ctrl_msg.position.push_back(joints_control_[i].approach_angle);
        joint_ctrl_pub_.publish(joint_ctrl_msg);
      }

    /* phase shift condition */
    bool pose_fixed_flag_ = true;
    for(int i = 0; i < joint_num_; i++)
      if(joints_control_[i].moving) pose_fixed_flag_ = false;

    if(pose_fixed_flag_)
      {
        if(++cnt > (pose_fixed_count_ * func_loop_rate_))
          {
            ROS_WARN("Object dropped!! Shift to RETURN_PHASE");
            phase_ = RETURN_PHASE;
            cnt = 0; // convergence reset
            once_flag = true;
          }
      }
  }

  void Hydrus::objectPoseApproachOffsetCal() 
  {
    object_offset_.setValue(object_approach_offset_x_ * cos(object_position_.theta) 
                            - object_approach_offset_y_ * sin(object_position_.theta),
                            object_approach_offset_x_ * sin(object_position_.theta) 
                            + object_approach_offset_y_ * cos(object_position_.theta),0);
  }

  void Hydrus::jointStatesCallback(const sensor_msgs::JointStateConstPtr& joint_states_msg)
  {
    if(joint_num_ == 0)
      {
        joint_num_ = joint_states_msg->position.size();
        jointControlParamInit();
      }

    for(int i = 0; i < joint_num_; i++)
      joints_control_[i].current_angle = joint_states_msg->position[i];
  }

  void Hydrus::jointMotorStatusCallback(const dynamixel_msgs::MotorStateListConstPtr& joint_motors_msg)
  {
    bool contact = true;
    bool torque_load_exceed_flag = false;

    if(joint_num_ == 0)
      {
        joint_num_ = joint_motors_msg->motor_states.size();
        jointControlParamInit();
      }

    for(int i = 0; i < joint_num_; i++)
      {
        joints_control_[i].moving = joint_motors_msg->motor_states[i].moving;  
        joints_control_[i].load_rate = joint_motors_msg->motor_states[i].load;
        joints_control_[i].temperature = joint_motors_msg->motor_states[i].temperature;
        joints_control_[i].angle_error = joint_motors_msg->motor_states[i].error;

        if(joints_control_[i].load_rate < torque_min_threshold_) contact = false;

        // 1. the torque exceeds the max threshold
        // 2. in the holding phase(contact_ == true)
        // 3. the modification is not susseccive
        if(joints_control_[i].load_rate > torque_max_threshold_ && contact_
           && ros::Time::now().toSec() - modification_start_time_.toSec() > modification_duration_)
          {
            ROS_WARN("Holding load exceeds in joint%d: %f", i + 1, joints_control_[i].load_rate);
            torque_load_exceed_flag = true;

            joints_control_[i].target_angle -= (joints_control_[i].holding_rotation_direction * modification_delta_angle_);
            joints_control_[i].hold_angle -= (joints_control_[i].holding_rotation_direction * modification_delta_angle_);
          }
      }

    if(torque_load_exceed_flag)
      {
        ROS_WARN("Reset holding start time");
        holding_start_time_ = ros::Time::now(); //reset!!
        modification_start_time_ = ros::Time::now(); //reset!!

        /* send modified angle */
        sensor_msgs::JointState joint_ctrl_msg;
        joint_ctrl_msg.header.stamp = ros::Time::now();
        for(int i = 0; i < joint_num_; i++)
          joint_ctrl_msg.position.push_back(joints_control_[i].target_angle);
        joint_ctrl_pub_.publish(joint_ctrl_msg);
      }

    /* the condition to get into hold phase */
    if(!contact_ && contact &&
       phase_ == GRASPING_PHASE && sub_phase_ == SUB_PHASE3 )
      {
        contact_ = true;
        ROS_WARN("Contact with object");
        holding_start_time_ = ros::Time::now(); //reset!!
      }

    /* the condition to shift to transportation phase */
    if(ros::Time::now().toSec() - holding_start_time_.toSec() > hold_count_)
      {
        ROS_WARN("Pick the object up!! Shift to GRSPED_PHASE");
        phase_ = GRASPED_PHASE;
        sub_phase_ = SUB_PHASE1;
      }
  }

  void Hydrus::joyStickAdditionalCallback(const sensor_msgs::JoyConstPtr & joy_msg)
  {
    if(debug_)
      {
        if(joy_msg->buttons[9] == 1) // RIGHT TOP TRIGGER(R1)
          {
          }
        if(joy_msg->buttons[11] == 1) // RIGHT DOWM TRIGGER(R1)
          {

            ROS_WARN("Debug: Shift to GRASPING_PHASE & Sub_Phase3");
            phase_ = GRASPING_PHASE;
            sub_phase_ = SUB_PHASE3;

            /* set init target angle */
            for(int i = 0; i < joint_num_; i++)
              joints_control_[i].target_angle = joints_control_[i].approach_angle;

            /* send nav msg: shift to vel control mode */
            aerial_robot_base::FlightNav nav_msg;
            nav_msg.header.stamp = ros::Time::now();
            nav_msg.pos_xy_nav_mode = aerial_robot_base::FlightNav::VEL_MODE;
            nav_msg.target_pos_x = 0;
            nav_msg.target_pos_y = 0;
            nav_msg.pos_z_nav_mode = aerial_robot_base::FlightNav::NO_NAVIGATION;
            nav_msg.psi_nav_mode = aerial_robot_base::FlightNav::NO_NAVIGATION;
            uav_nav_pub_.publish(nav_msg);


          }

      }
  }

  void Hydrus::rosParamInit()
  {
    std::string ns = nhp_.getNamespace();
    nhp_.param("joint_ctrl_pub_name", joint_ctrl_pub_name_, std::string("/hydrus/joints_ctrl"));
    nhp_.param("joint_states_sub_name", joint_states_sub_name_, std::string("/joint_states"));
    nhp_.param("joint_motors_sub_name", joint_motors_sub_name_, std::string("/joint_motors"));

    nhp_.param("object_approach_offset_x", object_approach_offset_x_, 0.0);
    nhp_.param("object_approach_offset_y", object_approach_offset_y_, 0.25);
    nhp_.param("pose_fixed_count", pose_fixed_count_, 2.0); //sec
    nhp_.param("grasping_height_threshold", grasping_height_threshold_, 0.01); //m

    nhp_.param("grasping_duration", grasping_duration_, 2.0); 
    nhp_.param("torque_min_threshold", torque_min_threshold_, 0.2); //20%
    nhp_.param("torque_max_threshold", torque_max_threshold_, 0.5); //20%
    nhp_.param("modification_delta_angle", modification_delta_angle_, 0.015);
    nhp_.param("modification_duration", modification_duration_, 0.5);
    nhp_.param("hold_count", hold_count_, 1.0); 
  }

  void  Hydrus::jointControlParamInit()
  {
    joints_control_.resize(joint_num_);

    for(int i = 0; i < joint_num_; i++)
      {
        std::stringstream joint_no;
        joint_no << i + 1;

        nhp_.param(std::string("j") + joint_no.str() + std::string("/approach_angle"), joints_control_[i].approach_angle, 1.2);
        nhp_.param(std::string("j") + joint_no.str() + std::string("/hold_angle"), joints_control_[i].hold_angle, 1.5);
        nhp_.param(std::string("j") + joint_no.str() + std::string("/tighten_angle"), joints_control_[i].tighten_angle, 0.0);

        joints_control_[i].holding_rotation_direction = (joints_control_[i].hold_angle - joints_control_[i].approach_angle) / fabs(joints_control_[i].hold_angle - joints_control_[i].approach_angle);

        ROS_INFO("joint%d, approach_angle: %f, hold_angle: %f, tighten_angle: %f, holding_rotation_direction: %d", i+1, joints_control_[i].approach_angle, joints_control_[i].hold_angle, joints_control_[i].tighten_angle, joints_control_[i].holding_rotation_direction);
      }
  }

};

#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS(aerial_transportation::Hydrus, aerial_transportation::Base);
