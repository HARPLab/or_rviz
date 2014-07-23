#ifndef ORINTERACTIVEMARKER_H_
#define ORINTERACTIVEMARKER_H_
#include <boost/unordered_map.hpp>
#include <boost/signals2.hpp>
#include <openrave/openrave.h>
#include <interactive_markers/interactive_marker_server.h>
#include "KinBodyMarker.h"

namespace or_interactivemarker {

class InteractiveMarkerViewer : public OpenRAVE::ViewerBase {
public:
    InteractiveMarkerViewer(OpenRAVE::EnvironmentBasePtr env);

    virtual void EnvironmentSync();

    virtual int main(bool bShow = true);
    virtual void quitmainloop();

    virtual OpenRAVE::UserDataPtr RegisterItemSelectionCallback(
        OpenRAVE::ViewerBase::ItemSelectionCallbackFn const &fncallback);
    virtual OpenRAVE::UserDataPtr RegisterViewerThreadCallback(
        OpenRAVE::ViewerBase::ViewerThreadCallbackFn const &fncallback);

private:
    typedef void ViewerCallbackFn();
    typedef bool SelectionCallbackFn(OpenRAVE::KinBody::LinkPtr plink,
                                     OpenRAVE::RaveVector<float>,
                                     OpenRAVE::RaveVector<float>);

    OpenRAVE::EnvironmentBasePtr env_;
    boost::shared_ptr<interactive_markers::InteractiveMarkerServer> server_;
    bool running_;

    boost::signals2::signal<ViewerCallbackFn> viewer_callbacks_;
    boost::signals2::signal<SelectionCallbackFn> selection_callbacks_;

    bool AddMenuEntryCommand(std::ostream &out, std::istream &in);
    bool GetMenuSelectionCommand(std::ostream &out, std::istream &in);
};

}

#endif
