//
//  control_surface_integrator_ui.h
//  reaper_csurf_integrator
//


#ifndef control_surface_integrator_ui_h
#define control_surface_integrator_ui_h

#include "control_surface_integrator.h"
#include "WDL/wdlstring.h"
#include "resource.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CSurfIntegrator : public IReaperControlSurface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    WDL_String descspace;
    char configtmp[1024];
    
public:
    CSurfIntegrator();
    ~CSurfIntegrator();
    virtual void SetTrackListChange() override;
    virtual void OnTrackSelection(MediaTrack *trackid) override;
    virtual int Extended(int call, void *parm1, void *parm2, void *parm3) override;
    virtual bool GetTouchState(MediaTrack *trackid, int touchedControl) override;
    virtual void Run() override;
    const char *GetTypeString() override;
    const char *GetDescString() override;
    const char *GetConfigString() override; // string of configuration data
};

#endif /* control_surface_integrator_ui_h */
