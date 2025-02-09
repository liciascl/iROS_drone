#include <fstream>
#include <iostream>
#include <math.h>

#include <Eigen/Core>
#include <mav_msgs/conversions.h>
#include <mav_msgs/default_topics.h>
#include <ros/ros.h>
#include <trajectory_msgs/MultiDOFJointTrajectory.h>

#include <nav_msgs/Odometry.h>
// #include <sensor_msgs/Joy.h>
#include <geometry_msgs/Twist.h>

#include <eigen_conversions/eigen_msg.h>

// Use the structure definitions from the rotors_joy_interface 
// #include "rotors_joy_interface/joy.h"

#define DEG2RAD(x) ((x) / 180.0 * M_PI)

ros::Publisher trajectory_pub;
ros::Subscriber odom_sub, joy_sub;
nav_msgs::Odometry odom_msg;
// sensor_msgs::Joy joy_msg;
geometry_msgs::Twist joy_msg;

bool joy_msg_ready = false;

int axis_roll  , axis_pitch, 
    axis_thrust, axis_yaw;
int axis_direction_roll, 
    axis_direction_pitch, 
    axis_direction_thrust, 
    axis_direction_yaw;
 
double max_vel,
       max_yawrate;

double distance_traveled;


void joy_callback(const geometry_msgs::TwistConstPtr& msg);
void odom_callback(const nav_msgs::OdometryConstPtr& msg);

int main(int argc, char** argv) {

  ros::init(argc, argv, "velocity_control_by_dronet");
  ros::NodeHandle nh("~");

  // Continuously publish waypoints.
  trajectory_pub = nh.advertise<trajectory_msgs::MultiDOFJointTrajectory>(
                    mav_msgs::default_topics::COMMAND_TRAJECTORY, 10);

  // Subscribe to bebop velocity commands messages
  // joy_sub = nh.subscribe("joy", 10, &joy_callback);
  joy_sub = nh.subscribe("/bebop/cmd_vel", 10, &joy_callback);

  // Subscribe to gt odometry messages
  odom_sub = nh.subscribe("odom", 10, &odom_callback);

  ROS_INFO("Started velocity_control_by_dronet.");

  nh.param("axis_roll_"  , axis_roll, 3);
  nh.param("axis_pitch_" , axis_pitch, 4);
  nh.param("axis_yaw_"   , axis_yaw, 0);
  nh.param("axis_thrust_", axis_thrust, 1);

  nh.param("axis_direction_roll_"  , axis_direction_roll, -1);
  nh.param("axis_direction_pitch_" , axis_direction_pitch, 1);
  nh.param("axis_direction_yaw_"   , axis_direction_yaw, 1);
  nh.param("axis_direction_thrust_", axis_direction_thrust, -1);
 
  nh.param("max_vel", max_vel, 1.0);
  nh.param("max_yawrate", max_yawrate, DEG2RAD(45));

  distance_traveled = 0.0;
  
  ros::spin();
}

void joy_callback(const geometry_msgs::TwistConstPtr& msg){
  joy_msg = *msg;
  joy_msg_ready = true;
}

void odom_callback(const nav_msgs::OdometryConstPtr& msg){
  if(joy_msg_ready == false)
    return;

  odom_msg = *msg;
 
  static bool init_pose_set = false;
  static ros::Time prev_time = ros::Time::now();
  static Eigen::Vector3d init_position;
  static double init_yaw;

  // ROS_INFO("controls");
  // ROS_INFO_STREAM(joy_msg.linear.x);
  // ROS_INFO_STREAM(joy_msg.angular.z);

  if(init_pose_set == false){
    Eigen::Affine3d eigen_affine;
    tf::poseMsgToEigen(odom_msg.pose.pose, eigen_affine);
    init_position = eigen_affine.matrix().topRightCorner<3, 1>();
    //init_yaw = eigen_affine.matrix().topLeftCorner<3, 3>().eulerAngles(0, 1, 2)(2);
    // Direct cosine matrix, a.k.a. rotation matrix
    Eigen::Matrix3d dcm = eigen_affine.matrix().topLeftCorner<3, 3>();
    double phi = asin(dcm(2, 1));
    double cosphi = cos(phi);
    double the = atan2(-dcm(2, 0) / cosphi, dcm(2, 2) / cosphi);
    double psi = atan2(-dcm(0, 1) / cosphi, dcm(1, 1) / cosphi);
    init_yaw = psi;
    init_pose_set = true;
  }

  double dt = (ros::Time::now() - prev_time).toSec();
  prev_time = ros::Time::now();

  // double yaw    = joy_msg.axes[axis_yaw]    * axis_direction_yaw;
  // double roll   = joy_msg.axes[axis_roll]   * axis_direction_roll;
  // double pitch  = joy_msg.axes[axis_pitch]  * axis_direction_pitch;
  // double thrust = joy_msg.axes[axis_thrust] * axis_direction_thrust;

  double yaw    = joy_msg.angular.z    * axis_direction_yaw;
  double roll   = 0.0;
  double pitch  = joy_msg.linear.x;
  double thrust = 0.0;

  Eigen::Vector3d desired_dposition( cos(init_yaw) * pitch - sin(init_yaw) * roll, 
                                     sin(init_yaw) * pitch + cos(init_yaw) * roll, 
                                     thrust);

  Eigen::Vector3d desired_dposition_scaled = desired_dposition * max_vel * dt;

  Eigen::Vector3d old_position(init_position.data());

  init_position += desired_dposition * max_vel * dt;

  distance_traveled += (old_position - init_position).lpNorm<2>();

  ROS_INFO("Distance Traveled: %f", distance_traveled);

  init_yaw = init_yaw + max_yawrate * yaw * dt;

  static trajectory_msgs::MultiDOFJointTrajectory trajectory_msg;
  trajectory_msg.header.stamp = ros::Time::now();
  trajectory_msg.header.seq++;

  mav_msgs::msgMultiDofJointTrajectoryFromPositionYaw(init_position,
      init_yaw, &trajectory_msg);

  trajectory_msg.points[0].time_from_start = ros::Duration(1.0);

  trajectory_pub.publish(trajectory_msg);
}