/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2012, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/* Author: Ioan Sucan */

#include <moveit/planning_scene_monitor/planning_scene_monitor.h>
#include <tf/transform_listener.h>
#include <moveit/move_group/move_group_capability.h>
#include <boost/algorithm/string/join.hpp>
#include <boost/tokenizer.hpp>
#include <moveit/macros/console_colors.h>
#include <moveit/move_group/node_name.h>

static const std::string ROBOT_DESCRIPTION = "robot_description";    // name of the robot description (a param name, so it can be changed externally)

namespace move_group
{

class MoveGroupExe
{
public:

  MoveGroupExe(const planning_scene_monitor::PlanningSceneMonitorPtr& psm, bool debug) :
    node_handle_("~")
  {
    // if the user wants to be able to disable execution of paths, they can just set this ROS param to false
    bool allow_trajectory_execution;
    node_handle_.param("allow_trajectory_execution", allow_trajectory_execution, true);

    context_.reset(new MoveGroupContext(psm, allow_trajectory_execution, debug));

    // start the capabilities
    configureCapabilities();
  }

  ~MoveGroupExe()
  {
    capabilities_.clear();
    context_.reset();
    capability_plugin_loader_.reset();
  }

  void status()
  {
    if (context_)
    {
      if (context_->status())
      {
        if (capabilities_.empty())
          printf(MOVEIT_CONSOLE_COLOR_BLUE "\nAll is well but no capabilities are loaded. There will be no party :(\n\n" MOVEIT_CONSOLE_COLOR_RESET);
        else
          printf(MOVEIT_CONSOLE_COLOR_GREEN "\nAll is well! Everyone is happy! You can start planning now!\n\n" MOVEIT_CONSOLE_COLOR_RESET);
        fflush(stdout);
      }
    }
    else
      ROS_ERROR("No MoveGroup context created. Nothing will work.");
  }

private:

  void configureCapabilities()
  {
    try
    {
      capability_plugin_loader_.reset(new pluginlib::ClassLoader<MoveGroupCapability>("moveit_ros_move_group", "move_group::MoveGroupCapability"));
    }
    catch(pluginlib::PluginlibException& ex)
    {
      ROS_FATAL_STREAM("Exception while creating plugin loader for move_group capabilities: " << ex.what());
      return;
    }

    // add individual capabilities move_group supports
    std::string capability_plugins;
    if (node_handle_.getParam("capabilities", capability_plugins))
    {
      boost::char_separator<char> sep(" ");
      boost::tokenizer<boost::char_separator<char> > tok(capability_plugins, sep);
      for(boost::tokenizer<boost::char_separator<char> >::iterator beg = tok.begin() ; beg != tok.end(); ++beg)
      {
        std::string plugin = *beg;
        try
        {
          printf(MOVEIT_CONSOLE_COLOR_CYAN "Loading '%s'...\n" MOVEIT_CONSOLE_COLOR_RESET, plugin.c_str());
          MoveGroupCapability *cap = capability_plugin_loader_->createUnmanagedInstance(plugin);
          cap->setContext(context_);
          cap->initialize();
          capabilities_.push_back(boost::shared_ptr<MoveGroupCapability>(cap));
        }
        catch(pluginlib::PluginlibException& ex)
        {
          ROS_ERROR_STREAM("Exception while loading move_group capability '" << plugin << "': " << ex.what() << std::endl
                           << "Available capabilities: " << boost::algorithm::join(capability_plugin_loader_->getDeclaredClasses(), ", "));
        }
      }
    }
    std::stringstream ss;
    ss << std::endl;
    ss << std::endl;
    ss << "********************************************************" << std::endl;
    ss << "* MoveGroup using: " << std::endl;
    for (std::size_t i = 0 ; i < capabilities_.size() ; ++i)
      ss << "*     - " << capabilities_[i]->getName() << std::endl;
    ss << "********************************************************" << std::endl;
    ROS_INFO_STREAM(ss.str());
  }

  ros::NodeHandle node_handle_;
  MoveGroupContextPtr context_;
  boost::shared_ptr<pluginlib::ClassLoader<MoveGroupCapability> > capability_plugin_loader_;
  std::vector<boost::shared_ptr<MoveGroupCapability> > capabilities_;
};

}

int main(int argc, char **argv)
{
  ros::init(argc, argv, move_group::NODE_NAME);

  ros::AsyncSpinner spinner(0);
  spinner.start();

  boost::shared_ptr<tf::TransformListener> tf(new tf::TransformListener(ros::Duration(10.0)));

  planning_scene_monitor::PlanningSceneMonitorPtr planning_scene_monitor(new planning_scene_monitor::PlanningSceneMonitor(ROBOT_DESCRIPTION, tf));

  if (planning_scene_monitor->getPlanningScene())
  {
    bool debug = false;
    for (int i = 1 ; i < argc ; ++i)
      if (strncmp(argv[i], "--debug", 7) == 0)
      {
        debug = true;
        break;
      }
    if (debug)
      ROS_INFO("MoveGroup debug mode is ON");
    else
      ROS_INFO("MoveGroup debug mode is OFF");

    printf(MOVEIT_CONSOLE_COLOR_CYAN "Starting context monitors...\n" MOVEIT_CONSOLE_COLOR_RESET);
    planning_scene_monitor->startSceneMonitor();
    planning_scene_monitor->startWorldGeometryMonitor();
    planning_scene_monitor->startStateMonitor();
    printf(MOVEIT_CONSOLE_COLOR_CYAN "Context monitors started.\n" MOVEIT_CONSOLE_COLOR_RESET);

    move_group::MoveGroupExe mge(planning_scene_monitor, debug);

    planning_scene_monitor->publishDebugInformation(debug);

    mge.status();

    ros::waitForShutdown();
  }
  else
    ROS_ERROR("Planning scene not configured");

  return 0;
}
