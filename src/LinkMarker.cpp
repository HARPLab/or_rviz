#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/make_shared.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/range/adaptor/map.hpp>
#include <ros/ros.h>
#include "or_conversions.h"
#include "LinkMarker.h"

using boost::adaptors::transformed;
using boost::algorithm::join;
using boost::algorithm::iends_with;
using boost::format;
using boost::str;
using geometry_msgs::Vector3;
using OpenRAVE::KinBodyPtr;
using OpenRAVE::RobotBase;
using OpenRAVE::RobotBasePtr;
using OpenRAVE::EnvironmentBasePtr;
using visualization_msgs::Marker;
using visualization_msgs::MarkerPtr;
using visualization_msgs::InteractiveMarker;
using visualization_msgs::InteractiveMarkerPtr;
using visualization_msgs::InteractiveMarkerControl;
using visualization_msgs::InteractiveMarkerFeedbackConstPtr;
using interactive_markers::InteractiveMarkerServer;

typedef OpenRAVE::KinBody::LinkPtr LinkPtr;
typedef boost::shared_ptr<OpenRAVE::TriMesh> TriMeshPtr;
typedef OpenRAVE::RobotBase::ManipulatorPtr ManipulatorPtr;
typedef OpenRAVE::KinBody::Link::GeometryPtr GeometryPtr;
typedef boost::shared_ptr<InteractiveMarkerServer> InteractiveMarkerServerPtr;

// TODO: Don't hardcode this.
static std::string const kWorldFrameId = "/world";

namespace or_interactivemarker {

LinkMarker::LinkMarker(boost::shared_ptr<InteractiveMarkerServer> server,
                       LinkPtr link, bool is_ghost)
    : server_(server)
    , interactive_marker_(boost::make_shared<InteractiveMarker>())
    , render_mode_(RenderMode::kVisual)
    , link_(link)
    , is_ghost_(is_ghost)
    , force_update_(false)
{
    BOOST_ASSERT(server);
    BOOST_ASSERT(link);

    // TODO: How should we handle this?
    //manipulator_ = InferManipulator();

    interactive_marker_->header.frame_id = kWorldFrameId;
    interactive_marker_->name = id();
    interactive_marker_->description = "";
    interactive_marker_->pose = toROSPose(link->GetTransform());
    interactive_marker_->scale = 0.25;

    // Show the visual geometry.
    interactive_marker_->controls.resize(1);
    visual_control_ = &interactive_marker_->controls[0];
    visual_control_->orientation.w = 1;
    visual_control_->name = str(format("%s.Geometry[visual]") % id());
    visual_control_->orientation_mode = InteractiveMarkerControl::INHERIT;
    visual_control_->interaction_mode = InteractiveMarkerControl::BUTTON;
    visual_control_->always_visible = true;
}

LinkMarker::~LinkMarker()
{
    server_->erase(interactive_marker_->name);
}

std::string LinkMarker::id() const
{
    LinkPtr const link = this->link();
    KinBodyPtr const body = link->GetParent();
    EnvironmentBasePtr const env = body->GetEnv();
    int const environment_id = OpenRAVE::RaveGetEnvironmentId(env);

    std::string suffix;
    if (is_ghost_) {
        suffix = ".Ghost";
    }

    return str(format("Environment[%d].KinBody[%s].Link[%s]%s")
               % environment_id % body->GetName() % link->GetName() % suffix);
}

LinkPtr LinkMarker::link() const
{
    return link_.lock();
}

void LinkMarker::set_pose(OpenRAVE::Transform const &pose) const
{
    server_->setPose(interactive_marker_->name, toROSPose(pose));
}

void LinkMarker::clear_color()
{
    force_update_ = force_update_ || !!override_color_;
    override_color_.reset();
}

void LinkMarker::set_color(OpenRAVE::Vector const &color)
{
    force_update_ = force_update_ || !override_color_
                                  || (color[0] != (*override_color_)[0])
                                  || (color[1] != (*override_color_)[1])
                                  || (color[2] != (*override_color_)[2])
                                  || (color[3] != (*override_color_)[3]);
    override_color_.reset(color);
}

InteractiveMarkerPtr LinkMarker::interactive_marker()
{
    return interactive_marker_;
}

bool LinkMarker::EnvironmentSync()
{
    LinkPtr const link = this->link();
    bool is_changed = force_update_;

    // Check if we need to re-create the marker to propagate changes in the
    // OpenRAVE environment.
    for (GeometryPtr const &geometry : link->GetGeometries()) {
        // Check if visibility changed.
        auto const it = geometry_markers_.find(geometry.get());
        bool const is_missing = it == geometry_markers_.end();
        bool const is_visible = geometry->IsVisible();
        is_changed = is_changed || (is_visible == is_missing);

        // TODO  Check if color changed.
        // TODO: Check if the transform changed.
        // TODO: Check if the geometry changed.

        if (is_changed) {
            break;
        }
    }

    // Re-create the geometry.
    if (is_changed) {
        CreateGeometry();
        server_->insert(*interactive_marker_);
    }

    force_update_ = false;
    return is_changed;
}

void LinkMarker::CreateGeometry()
{
    visual_control_->markers.clear();
    geometry_markers_.clear();

    LinkPtr const link = this->link();

    for (GeometryPtr const geometry : link->GetGeometries()) {
        if (!geometry->IsVisible()) {
            continue;
        }

        MarkerPtr new_marker = CreateGeometry(geometry);
        if (new_marker) {
            visual_control_->markers.push_back(*new_marker);
            geometry_markers_[geometry.get()] = &visual_control_->markers.back();
        }
        // This geometry is empty. Insert a dummy marker to simplify the
        // change-detection logic.
        else {
            geometry_markers_[geometry.get()] = NULL;
        }
    }
}
void LinkMarker::SetRenderMode(RenderMode::Type mode)
{
    render_mode_ = mode;
}

MarkerPtr LinkMarker::CreateGeometry(GeometryPtr geometry)
{
    MarkerPtr marker = boost::make_shared<Marker>();
    marker->pose = toROSPose(geometry->GetTransform());

    if (override_color_) {
        marker->color = toROSColor(*override_color_);
    } else {
        marker->color = toROSColor(geometry->GetDiffuseColor());
        marker->color.a = 1.0 - geometry->GetTransparency();
    }

    // If a render filename is specified, then we should ignore the rest of the
    // geometry. This is true regardless of the mesh type.
    std::string render_mesh_path = geometry->GetRenderFilename();
    if (boost::algorithm::starts_with(render_mesh_path, "__norenderif__")) {
        render_mesh_path = "";
    }

    // Pass the path to the mesh to RViz and let RViz load it directly. This is
    // only possible if RViz supports the mesh format.
    if (!render_mesh_path.empty() && HasRVizSupport(render_mesh_path)) {
        marker->type = Marker::MESH_RESOURCE;
        marker->scale = toROSVector(geometry->GetRenderScale());
        marker->mesh_resource = "file://" + render_mesh_path;

        bool const has_texture = !override_color_ && HasTexture(render_mesh_path);
        marker->mesh_use_embedded_materials = has_texture;

        // Color must be zero to use the embedded material.
        if (has_texture) {
            marker->color.r = 0;
            marker->color.g = 0;
            marker->color.b = 0;
            marker->color.a = 0;
        }
        return marker;
    }
    // Otherwise, load the mesh with OpenRAVE and serialize the full mesh it
    // into the marker.
    else if (!render_mesh_path.empty()) {
        OpenRAVE::EnvironmentBasePtr const env = link()->GetParent()->GetEnv();
        TriMeshPtr trimesh = boost::make_shared<OpenRAVE::TriMesh>();
        trimesh = env->ReadTrimeshURI(trimesh, render_mesh_path);
        if (!trimesh) {
            RAVELOG_WARN("Loading trimesh '%s' using OpenRAVE failed.",
                render_mesh_path.c_str()
            );
            return MarkerPtr();
        }

        TriMeshToMarker(*trimesh, marker);

        RAVELOG_WARN("Loaded mesh '%s' with OpenRAVE because this format is not"
                     " supported by RViz. This may be slow for large files.\n",
            render_mesh_path.c_str()
        );
        return marker;
    }

    // Otherwise, we have to render the underlying geometry type.
    switch (geometry->GetType()) {
    case OpenRAVE::GeometryType::GT_None:
        return MarkerPtr();

    case OpenRAVE::GeometryType::GT_Box:
        // TODO: This may be off by a factor of two.
        marker->type = Marker::CUBE;
        marker->scale = toROSVector(geometry->GetBoxExtents());
        marker->scale.x *= 2.0;
        marker->scale.y *= 2.0;
        marker->scale.z *= 2.0;
        break;

    case OpenRAVE::GeometryType::GT_Sphere: {
        double const sphere_radius = geometry->GetSphereRadius();
        marker->type = Marker::SPHERE;
        marker->scale.x = 0.5 * sphere_radius;
        marker->scale.y = 0.5 * sphere_radius;
        marker->scale.z = 0.5 * sphere_radius;
        break;
    }

    case OpenRAVE::GeometryType::GT_Cylinder: {
        // TODO: This may be rotated and/or off by a factor of two.
        double const cylinder_radius = geometry->GetCylinderRadius();
        double const cylinder_height= geometry->GetCylinderHeight();
        marker->type = Marker::CYLINDER;
        marker->scale.x = 0.5 * cylinder_radius;
        marker->scale.y = 0.5 * cylinder_radius;
        marker->scale.z = cylinder_height;
        break;
    }

    case OpenRAVE::GeometryType::GT_TriMesh:
        // TODO: Fall back on the OpenRAVE's mesh loader if this format is not
        // supported by RViz.
        return MarkerPtr();
        break;

    default:
        RAVELOG_WARN("Unknown geometry type '%d'.\n", geometry->GetType());
        return MarkerPtr();
    }
    return marker;
}

void LinkMarker::TriMeshToMarker(OpenRAVE::TriMesh const &trimesh,
                                 MarkerPtr const &marker)
{
    marker->type = Marker::TRIANGLE_LIST;
    marker->points.clear();

    BOOST_ASSERT(trimesh.indices.size() % 3 == 0);
    for (size_t i = 0; i < trimesh.indices.size(); ++i) {
        OpenRAVE::Vector const &p1 = trimesh.vertices[i + 0];
        OpenRAVE::Vector const &p2 = trimesh.vertices[i + 1];
        OpenRAVE::Vector const &p3 = trimesh.vertices[i + 2];
        marker->points.push_back(toROSPoint(p1));
        marker->points.push_back(toROSPoint(p2));
        marker->points.push_back(toROSPoint(p3));
    }
}

bool LinkMarker::HasTexture(std::string const &uri) const
{
    return iends_with(uri, ".dae");
}

bool LinkMarker::HasRVizSupport(std::string const &uri) const
{
    return iends_with(uri, ".dae")
        || iends_with(uri, ".stl")
        || iends_with(uri, ".mesh");
}

}
