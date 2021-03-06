#ifndef __CONTROLLER_TEMPLATE__
#define __CONTROLLER_TEMPLATE__

#include <ros/ros.h>
#include <sensor_msgs/JointState.h>
#include <actionlib/server/simple_action_server.h>
#include <cmath>

namespace generic_control_toolbox
{
  const double MAX_DT = 0.5;

  /**
  Defines the basic cartesian controller interface.
  **/
  class ControllerBase
  {
  public:
    ControllerBase();
    virtual ~ControllerBase();

    /**
      Method for computing the desired joint states given the control algorithm.

      @param current_state Current joint states.
      @param dt Elapsed time since last control loop.

      @return Desired joint states.
    **/
    virtual sensor_msgs::JointState updateControl(const sensor_msgs::JointState &current_state, const ros::Duration &dt) = 0;

    /**
      Indicates if the controller is active.

      @return True if active, and the controller output is to be used, False otherwise.
    **/
    virtual bool isActive() const = 0 ;

    /**
      Allows resetting the internal controller state.
    **/
    virtual void resetInternalState() = 0;
  };

  /**
    A controller interface which implements the SimpleActionServer actionlib
    protocol.
  **/
  template <class ActionClass, class ActionGoal, class ActionFeedback, class ActionResult>
  class ControllerTemplate : public ControllerBase
  {
  public:
    ControllerTemplate(const std::string &action_name);
    virtual ~ControllerTemplate();

    /**
      Wraps the control algorithm with actionlib-related management.
    **/
    virtual sensor_msgs::JointState updateControl(const sensor_msgs::JointState &current_state, const ros::Duration &dt);

    virtual bool isActive() const;

    virtual void resetInternalState();

  protected:
    /**
      Implementation of the actual control method.
    **/
    virtual sensor_msgs::JointState controlAlgorithm(const sensor_msgs::JointState &current_state, const ros::Duration &dt) = 0;

    /**
      Read goal data.

      @param goal The goal pointer from actionlib.
      @return True in case of success, false otherwise.
    **/
    virtual bool parseGoal(boost::shared_ptr<const ActionGoal> goal) = 0;

    /**
      Reset the controller to a default state.
    **/
    virtual void resetController() = 0;

    /**
    Return the last controlled joint state. If the controller does not have
    an active actionlib goal, it will set the references of the joint controller
    to the last desired position (and null velocity).

    @param current The current joint state.
    @return The last commanded joint state before the actionlib goal was
    preempted or completed.
    **/
    sensor_msgs::JointState lastState(const sensor_msgs::JointState &current);

    boost::shared_ptr<actionlib::SimpleActionServer<ActionClass> > action_server_;
    ActionFeedback feedback_;
    ActionResult result_;

  private:
    /**
      Method that manages the starting of the actionlib server of each cartesian
    controller.
    **/
    void startActionlib();

    /**
      Goal callback method.
    **/
    virtual bool goalCB();

    /**
      Preempt callback method.
    **/
    virtual void preemptCB();

    /**
      Resets controller flags
    **/
    void resetFlags();


    std::string action_name_;
    ros::NodeHandle nh_;
    sensor_msgs::JointState last_state_;
    bool has_state_, acquired_goal_;
  };

  template <class ActionClass, class ActionGoal, class ActionFeedback, class ActionResult>
  ControllerTemplate<ActionClass, ActionGoal, ActionFeedback, ActionResult>::ControllerTemplate(const std::string &action_name) : action_name_(action_name)
  {
    nh_ = ros::NodeHandle("~");
    resetFlags();
    startActionlib();
  }

  template <class ActionClass, class ActionGoal, class ActionFeedback, class ActionResult>
  sensor_msgs::JointState ControllerTemplate<ActionClass, ActionGoal, ActionFeedback, ActionResult>::updateControl(const sensor_msgs::JointState &current_state, const ros::Duration &dt)
  {
    if (!action_server_->isActive() || !acquired_goal_)
    {
      return lastState(current_state);
    }

    ROS_DEBUG_THROTTLE(10, "Calling %s control algorithm", action_name_.c_str());
    if (dt.toSec() > MAX_DT) // lost communication for too much time
    {
      ROS_ERROR_STREAM(action_name_ << " did not receive updates for more than " << MAX_DT << " seconds, aborting");
      action_server_->setAborted(result_);
      return lastState(current_state);
    }

    sensor_msgs::JointState ret = controlAlgorithm(current_state, dt);
    action_server_->publishFeedback(feedback_);

    if (!action_server_->isActive())
    {
      resetInternalState();
    }

    // verify sanity of values
    for (unsigned int i = 0; i < ret.name.size(); i++)
    {
      if (!std::isfinite(ret.position[i]) || !std::isfinite(ret.velocity[i]))
      {
        ROS_ERROR("Invalid joint states in %s", action_name_.c_str());
        return lastState(current_state);
      }
    }

    return ret;
  }

  template <class ActionClass, class ActionGoal, class ActionFeedback, class ActionResult>
  bool ControllerTemplate<ActionClass, ActionGoal, ActionFeedback, ActionResult>::isActive() const
  {
    if (!action_server_->isActive())
    {
      return false;
    }

    return true;
  }

  template <class ActionClass, class ActionGoal, class ActionFeedback, class ActionResult>
  void ControllerTemplate<ActionClass, ActionGoal, ActionFeedback, ActionResult>::resetInternalState()
  {
    resetFlags();
    resetController();
  }

  template <class ActionClass, class ActionGoal, class ActionFeedback, class ActionResult>
  void ControllerTemplate<ActionClass, ActionGoal, ActionFeedback, ActionResult>::resetFlags()
  {
    has_state_ = false;
    acquired_goal_ = false;
  }

  template <class ActionClass, class ActionGoal, class ActionFeedback, class ActionResult>
  sensor_msgs::JointState ControllerTemplate<ActionClass, ActionGoal, ActionFeedback, ActionResult>::lastState(const sensor_msgs::JointState &current)
  {
    if (current.position.size() == 0) // Invalid state
    {
      ROS_WARN("lastState got invalid state");
      return last_state_;
    }

    if(!has_state_)
    {
      last_state_ = current;
      for (unsigned long i = 0; i < last_state_.velocity.size(); i++)
      {
        last_state_.velocity[i] = 0.0;
      }

      has_state_ = true;
    }

    return last_state_;
  }

  template <class ActionClass, class ActionGoal, class ActionFeedback, class ActionResult>
  bool ControllerTemplate<ActionClass, ActionGoal, ActionFeedback, ActionResult>::goalCB()
  {
    boost::shared_ptr<const ActionGoal> goal = action_server_->acceptNewGoal();

    if (!parseGoal(goal))
    {
      action_server_->setAborted(result_);
      return false;
    }

    acquired_goal_ = true;
    ROS_INFO("New goal received in %s", action_name_.c_str());
    return true;
  }

  template <class ActionClass, class ActionGoal, class ActionFeedback, class ActionResult>
  void ControllerTemplate<ActionClass, ActionGoal, ActionFeedback, ActionResult>::preemptCB()
  {
    action_server_->setPreempted(result_);
    ROS_WARN("%s preempted!", action_name_.c_str());
    resetInternalState();
  }

  template <class ActionClass, class ActionGoal, class ActionFeedback, class ActionResult>
  ControllerTemplate<ActionClass, ActionGoal, ActionFeedback, ActionResult>::~ControllerTemplate() {}

  template <class ActionClass, class ActionGoal, class ActionFeedback, class ActionResult>
  void ControllerTemplate<ActionClass, ActionGoal, ActionFeedback, ActionResult>::startActionlib()
  {
    // Initialize actionlib server
    action_server_ = boost::shared_ptr<actionlib::SimpleActionServer<ActionClass> >(new actionlib::SimpleActionServer<ActionClass>(nh_, action_name_, false));

    // Register callbacks
    action_server_->registerGoalCallback(boost::bind(&ControllerTemplate::goalCB, this));
    action_server_->registerPreemptCallback(boost::bind(&ControllerTemplate::preemptCB, this));

    action_server_->start();

    ROS_INFO("%s initialized successfully!", action_name_.c_str());
  }

  typedef boost::shared_ptr<ControllerBase> BasePtr;
}

#endif
