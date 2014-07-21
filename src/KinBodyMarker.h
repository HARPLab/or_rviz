#ifndef KINBODYMARKER_H_
#define KINBODYMARKER_H_
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>
#include <openrave/openrave.h>
#include "LinkMarker.h"
#include "JointMarker.h"
#include "ManipulatorMarker.h"

namespace or_interactivemarker {

class KinBodyMarker;
typedef boost::shared_ptr<KinBodyMarker> KinBodyMarkerPtr;

struct LinkMarkerWrapper {
    typedef interactive_markers::MenuHandler::EntryHandle MenuEntry;

    LinkMarkerWrapper() : has_menu(false) { }

    LinkMarkerPtr link_marker;
    bool has_menu;
    boost::optional<MenuEntry> menu_parent;
    boost::optional<MenuEntry> menu_joints;
    boost::optional<MenuEntry> menu_enabled;
    boost::optional<MenuEntry> menu_visible;
};

class KinBodyMarker : public OpenRAVE::UserData {
public:
    KinBodyMarker(boost::shared_ptr<interactive_markers::InteractiveMarkerServer> server,
                  OpenRAVE::KinBodyPtr kinbody);
    virtual ~KinBodyMarker();

    bool IsGhost() const;

    void EnvironmentSync();

private:
    boost::shared_ptr<interactive_markers::InteractiveMarkerServer> server_;
    OpenRAVE::KinBodyWeakPtr kinbody_;
    OpenRAVE::RobotBaseWeakPtr robot_;
    bool has_joint_controls_;

    OpenRAVE::KinBodyPtr ghost_kinbody_;
    OpenRAVE::RobotBasePtr ghost_robot_;

    boost::unordered_map<OpenRAVE::KinBody::Link *, LinkMarkerWrapper> link_markers_;
    boost::unordered_map<OpenRAVE::KinBody::Joint *, JointMarkerPtr> joint_markers_;
    boost::unordered_map<OpenRAVE::RobotBase::Manipulator *, ManipulatorMarkerPtr> manipulator_markers_;

    void CreateMenu(LinkMarkerWrapper &link_wrapper);

    void UpdateMenu();
    void UpdateMenu(LinkMarkerWrapper &link_wrapper);
    void MenuCallback(LinkMarkerWrapper &link_wrapper,
                      visualization_msgs::InteractiveMarkerFeedbackConstPtr const &feedback);

    void CreateGhost();
};

}

#endif
