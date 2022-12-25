//
//  control_surface_Reaper_actions.h
//  reaper_csurf_integrator
//
//

#ifndef control_surface_Reaper_actions_h
#define control_surface_Reaper_actions_h

#include "control_surface_action_contexts.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FXParam : public FXAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "FXParam"; }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
            DAW::TrackFX_SetParam(track, context->GetSlotIndex(), context->GetParamIndex(), value);
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double min, max = 0;
            
            if(value == 0)
                DAW::TrackFX_EndParamEdit(track, context->GetSlotIndex(), context->GetParamIndex());
            else
                DAW::TrackFX_SetParam(track, context->GetSlotIndex(), context->GetParamIndex(), DAW::TrackFX_GetParam(track, context->GetSlotIndex(), context->GetParamIndex(), &min, &max));
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TCPFXParam : public FXAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TCPFXParam"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int index = context->GetIntParam();
            
            if(DAW::CountTCPFXParms(track) > index)
            {
                int fxIndex = 0;
                int paramIndex = 0;
                
                if(DAW::GetTCPFXParm(track, index, &fxIndex, &paramIndex))
                {
                    double min, max = 0.0;
                    
                    return DAW::TrackFX_GetParam(track, fxIndex, paramIndex, &min, &max);
                }
                else
                    return 0.0;
            }
            else
                return 0.0;
        }
        else
            return 0.0;
    }

    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int index = context->GetIntParam();
            
            if(DAW::CountTCPFXParms(track) > index)
            {
                int fxIndex = 0;
                int paramIndex = 0;
                
                if(DAW::GetTCPFXParm(track, index, &fxIndex, &paramIndex))
                    DAW::TrackFX_SetParam(track, fxIndex, paramIndex, value);
            }
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        /*
        if(MediaTrack* track = context->GetTrack())
        {
            double min, max = 0;
            
            if(value == 0)
                DAW::TrackFX_EndParamEdit(track, context->GetSlotIndex(), context->GetParamIndex());
            else
                DAW::TrackFX_SetParam(track, context->GetSlotIndex(), context->GetParamIndex(), DAW::TrackFX_GetParam(track, context->GetSlotIndex(), context->GetParamIndex(), &min, &max));
        }
         */
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FXParamRelative : public FXAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "FXParamRelative"; }
    
    virtual void Do(ActionContext* context, double relativeValue) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double min, max = 0;
            double value = DAW::TrackFX_GetParam(track, context->GetSlotIndex(), context->GetParamIndex(), &min, &max);
            value +=  relativeValue;
            
            if(value < min) value = min;
            if(value > max) value = max;
            
            DAW::TrackFX_SetParam(track, context->GetSlotIndex(), context->GetParamIndex(), value);
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double min, max = 0;
            
            if(value == 0)
                DAW::TrackFX_EndParamEdit(track, context->GetSlotIndex(), context->GetParamIndex());
            else
                DAW::TrackFX_SetParam(track, context->GetSlotIndex(), context->GetParamIndex(), DAW::TrackFX_GetParam(track, context->GetSlotIndex(), context->GetParamIndex(), &min, &max));
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FocusedFXParam : public FXAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    int trackNum_ = 0;
    int fxSlotNum_ = 0;
    int fxParamNum_ = 0;
    
public:
    virtual string GetName() override { return "FocusedFXParam"; }
   
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = DAW::GetTrack(trackNum_))
        {
            double min = 0.0;
            double max = 0.0;
            return DAW::TrackFX_GetParam(track, fxSlotNum_, fxParamNum_, &min, &max);
        }
        
        return 0.0;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(DAW::GetLastTouchedFX(&trackNum_, &fxSlotNum_, &fxParamNum_))
            if(DAW::GetTrack(trackNum_))
                context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(DAW::GetLastTouchedFX(&trackNum_, &fxSlotNum_, &fxParamNum_))
            if(MediaTrack* track = DAW::GetTrack(trackNum_))
                DAW::TrackFX_SetParam(track, fxSlotNum_, fxParamNum_, value);
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        if(DAW::GetLastTouchedFX(&trackNum_, &fxSlotNum_, &fxParamNum_))
        {
            if(MediaTrack* track =  DAW::GetTrack(trackNum_))
            {
                if(value == 0)
                    DAW::TrackFX_EndParamEdit(track, fxSlotNum_, fxParamNum_);
                else
                    DAW::TrackFX_SetParam(track, fxSlotNum_, fxParamNum_, GetCurrentNormalizedValue(context));
            }
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ToggleFXBypass : public FXAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "ToggleFXBypass"; }
   
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            //I_FXEN : fx enabled, 0=bypassed, !0=fx active
            if(DAW::GetMediaTrackInfo_Value(track, "I_FXEN") == 0)
                context->UpdateWidgetValue(0.0);
            else if(DAW::TrackFX_GetCount(track) > context->GetSlotIndex())
            {
                if(DAW::TrackFX_GetEnabled(track, context->GetSlotIndex()))
                    context->UpdateWidgetValue(1.0);
                else
                    context->UpdateWidgetValue(0.0);
            }
            else
                context->ClearWidget();
        }
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases

        if(MediaTrack* track = context->GetTrack())
            DAW::TrackFX_SetEnabled(track, context->GetSlotIndex(), ! DAW::TrackFX_GetEnabled(track, context->GetSlotIndex()));
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FXBypassDisplay : public FXAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "FXBypassDisplay"; }
   
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            //I_FXEN : fx enabled, 0=bypassed, !0=fx active
            if(DAW::GetMediaTrackInfo_Value(track, "I_FXEN") == 0)
                context->UpdateWidgetValue("Bypassed");
            else if(DAW::TrackFX_GetCount(track) > context->GetSlotIndex())
            {
                if(DAW::TrackFX_GetEnabled(track, context->GetSlotIndex()))
                    context->UpdateWidgetValue("Enabled");
                else
                    context->UpdateWidgetValue("Bypassed");
            }
            else
                context->ClearWidget();
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ToggleFXOffline : public FXAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "ToggleFXOffline"; }
   
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(DAW::TrackFX_GetCount(track) > context->GetSlotIndex())
            {
                if(DAW::TrackFX_GetOffline(track, context->GetSlotIndex()))
                    context->UpdateWidgetValue(0.0);
                else
                    context->UpdateWidgetValue(1.0);
            }
            else
                context->ClearWidget();
        }
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases

        if(MediaTrack* track = context->GetTrack())
            DAW::TrackFX_SetOffline(track, context->GetSlotIndex(), ! DAW::TrackFX_GetOffline(track, context->GetSlotIndex()));
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FXOfflineDisplay : public FXAction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "FXOfflineDisplay"; }
   
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(DAW::TrackFX_GetCount(track) > context->GetSlotIndex())
            {
                if(DAW::TrackFX_GetOffline(track, context->GetSlotIndex()))
                    context->UpdateWidgetValue("Offline");
                else
                    context->UpdateWidgetValue("Online");
            }
            else
                context->ClearWidget();
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackVolume : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackVolume"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double vol, pan = 0.0;
            DAW::GetTrackUIVolPan(track, &vol, &pan);
            return volToNormalized(vol);
        }
        else
            return 0.0;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
            DAW::CSurf_SetSurfaceVolume(track, DAW::CSurf_OnVolumeChange(track, normalizedToVol(value), false), NULL);
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        context->GetZone()->GetNavigator()->SetIsVolumeTouched(value);
        if(MediaTrack* track = context->GetTrack())
            DAW::CSurf_SetSurfaceVolume(track, DAW::CSurf_OnVolumeChange(track, normalizedToVol(GetCurrentNormalizedValue(context)), false), NULL);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SoftTakeover7BitTrackVolume : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SoftTakeover7BitTrackVolume"; }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double trackVolume, trackPan = 0.0;
            DAW::GetTrackUIVolPan(track, &trackVolume, &trackPan);
            trackVolume = volToNormalized(trackVolume);
            
            if( fabs(value - trackVolume) < 0.025) // GAW -- Magic number -- ne touche pas
                DAW::CSurf_SetSurfaceVolume(track, DAW::CSurf_OnVolumeChange(track, normalizedToVol(value), false), NULL);
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        context->GetZone()->GetNavigator()->SetIsVolumeTouched(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SoftTakeover14BitTrackVolume : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SoftTakeover14BitTrackVolume"; }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double trackVolume, trackPan = 0.0;
            DAW::GetTrackUIVolPan(track, &trackVolume, &trackPan);
            trackVolume = volToNormalized(trackVolume);
            
            if( fabs(value - trackVolume) < 0.0025) // GAW -- Magic number -- ne touche pas
                DAW::CSurf_SetSurfaceVolume(track, DAW::CSurf_OnVolumeChange(track, normalizedToVol(value), false), NULL);
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        context->GetZone()->GetNavigator()->SetIsVolumeTouched(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackVolumeDB : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackVolumeDB"; }
    
    virtual double GetCurrentDBValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double vol, pan = 0.0;
            DAW::GetTrackUIVolPan(track, &vol, &pan);
            return VAL2DB(vol);
        }
        else
            return 0.0;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            context->UpdateWidgetValue(GetCurrentDBValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
            DAW::CSurf_SetSurfaceVolume(track, DAW::CSurf_OnVolumeChange(track, DB2VAL(value), false), NULL);
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        context->GetZone()->GetNavigator()->SetIsVolumeTouched(value);
        if(MediaTrack* track = context->GetTrack())
            DAW::CSurf_SetSurfaceVolume(track, DAW::CSurf_OnVolumeChange(track, DB2VAL(GetCurrentDBValue(context)), false), NULL);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPan : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackPan"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) != 6)
            {
                double vol, pan = 0.0;
                DAW::GetTrackUIVolPan(track, &vol, &pan);
                return panToNormalized(pan);
            }
        }
        
        return 0.0;
    }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) != 6)
                DAW::CSurf_SetSurfacePan(track, DAW::CSurf_OnPanChange(track, normalizedToPan(value), false), NULL);
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        context->GetZone()->GetNavigator()->SetIsPanTouched(value);
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) != 6)
                DAW::CSurf_SetSurfacePan(track, DAW::CSurf_OnPanChange(track, normalizedToPan(GetCurrentNormalizedValue(context)), false), NULL);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanPercent : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackPanPercent"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) != 6)
            {
                double vol, pan = 0.0;
                DAW::GetTrackUIVolPan(track, &vol, &pan);
                context->UpdateWidgetValue(pan * 100.0);
            }
        }
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
            if(GetPanMode(track) != 6)
                DAW::CSurf_SetSurfacePan(track, DAW::CSurf_OnPanChange(track, value / 100.0, false), NULL);
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        context->GetZone()->GetNavigator()->SetIsPanTouched(value);
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) != 6)
            {
                double vol, pan = 0.0;
                DAW::GetTrackUIVolPan(track, &vol, &pan);
                DAW::CSurf_SetSurfacePan(track, DAW::CSurf_OnPanChange(track, pan, false), NULL);
            }
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanWidth : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackPanWidth"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            return panToNormalized(DAW::GetMediaTrackInfo_Value(track, "D_WIDTH"));
        else
            return 0.0;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) != 6)
                context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        }
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
            if(GetPanMode(track) != 6)
                DAW::CSurf_OnWidthChange(track, normalizedToPan(value), false);
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        context->GetZone()->GetNavigator()->SetIsPanWidthTouched(value);
        if(MediaTrack* track = context->GetTrack())
            if(GetPanMode(track) != 6)
                DAW::CSurf_OnWidthChange(track, normalizedToPan(GetCurrentNormalizedValue(context)), false);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanWidthPercent : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackPanWidthPercent"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) != 6)
                context->UpdateWidgetValue(DAW::GetMediaTrackInfo_Value(track, "D_WIDTH") * 100.0);
        }
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
            if(GetPanMode(track) != 6)
                DAW::CSurf_OnWidthChange(track, value / 100.0, false);
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        context->GetZone()->GetNavigator()->SetIsPanWidthTouched(value);
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) != 6)
                DAW::CSurf_OnWidthChange(track, DAW::GetMediaTrackInfo_Value(track, "D_WIDTH"), false);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanL : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackPanL"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            return panToNormalized(DAW::GetMediaTrackInfo_Value(track, "D_DUALPANL"));
        else
            return 0.0;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
                context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        }
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
            {
                double pan = normalizedToPan(value);
                DAW::GetSetMediaTrackInfo(track, "D_DUALPANL", &pan);
            }
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        context->GetZone()->GetNavigator()->SetIsPanLeftTouched(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanLPercent : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackPanLPercent"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
                context->UpdateWidgetValue(DAW::GetMediaTrackInfo_Value(track, "D_DUALPANL") * 100.0);
        }
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
            {
                double panFromPercent = value / 100.0;
                DAW::GetSetMediaTrackInfo(track, "D_DUALPANL", &panFromPercent);
            }
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        context->GetZone()->GetNavigator()->SetIsPanLeftTouched(value);
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
            {
                double panL = DAW::GetMediaTrackInfo_Value(track, "D_DUALPANL");
                DAW::GetSetMediaTrackInfo(track, "D_DUALPANL", &panL);
            }
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanR : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackPanR"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            return panToNormalized(DAW::GetMediaTrackInfo_Value(track, "D_DUALPANR"));
        else
            return 0.0;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
                context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        }
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
            {
                double pan = normalizedToPan(value);
                DAW::GetSetMediaTrackInfo(track, "D_DUALPANR", &pan);
            }
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        context->GetZone()->GetNavigator()->SetIsPanRightTouched(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanRPercent : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackPanRPercent"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
                context->UpdateWidgetValue(DAW::GetMediaTrackInfo_Value(track, "D_DUALPANR") * 100.0);
        }
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
            {
                double panFromPercent = value / 100.0;
                DAW::GetSetMediaTrackInfo(track, "D_DUALPANR", &panFromPercent);
            }
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        context->GetZone()->GetNavigator()->SetIsPanRightTouched(value);
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
            {
                double panL = DAW::GetMediaTrackInfo_Value(track, "D_DUALPANR");
                DAW::GetSetMediaTrackInfo(track, "D_DUALPANR", &panL);
            }
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanAutoLeft : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackPanAutoLeft"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
                return panToNormalized(DAW::GetMediaTrackInfo_Value(track, "D_DUALPANL"));
            else
            {
                double vol, pan = 0.0;
                DAW::GetTrackUIVolPan(track, &vol, &pan);
                return panToNormalized(pan);
            }
        }
        else
            return 0.0;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
                context->UpdateWidgetValue(panToNormalized(DAW::GetMediaTrackInfo_Value(track, "D_DUALPANL")));
            else
                context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        }
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
            {
                double pan = normalizedToPan(value);
                DAW::GetSetMediaTrackInfo(track, "D_DUALPANL", &pan);
            }
            else
                DAW::CSurf_SetSurfacePan(track, DAW::CSurf_OnPanChange(track, normalizedToPan(value), false), NULL);
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
                context->GetZone()->GetNavigator()->SetIsPanLeftTouched(value);
            else
            {
                context->GetZone()->GetNavigator()->SetIsPanTouched(value);
                DAW::CSurf_SetSurfacePan(track, DAW::CSurf_OnPanChange(track, normalizedToPan(GetCurrentNormalizedValue(context)), false), NULL);
            }
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanAutoRight : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackPanAutoRight"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
                return panToNormalized(DAW::GetMediaTrackInfo_Value(track, "D_DUALPANR"));
            else
                return panToNormalized(DAW::GetMediaTrackInfo_Value(track, "D_WIDTH"));
        }
        else
            return 0.0;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
                context->UpdateWidgetValue(panToNormalized(DAW::GetMediaTrackInfo_Value(track, "D_DUALPANR")));
            else
                context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        }
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
            {
                double pan = normalizedToPan(value);
                DAW::GetSetMediaTrackInfo(track, "D_DUALPANR", &pan);
            }
            else
                DAW::CSurf_OnWidthChange(track, normalizedToPan(value), false);
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
                context->GetZone()->GetNavigator()->SetIsPanRightTouched(value);
            else
            {
                context->GetZone()->GetNavigator()->SetIsPanWidthTouched(value);
                DAW::CSurf_OnWidthChange(track, normalizedToPan(GetCurrentNormalizedValue(context)), false);

            }
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSendVolume : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackSendVolume"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int numHardwareSends = DAW::GetTrackNumSends(track, 1);
            double vol, pan = 0.0;
            DAW::GetTrackSendUIVolPan(track, context->GetSlotIndex() + numHardwareSends, &vol, &pan);
            return volToNormalized(vol);
        }
        else
            return 0.0;
    }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int numHardwareSends = DAW::GetTrackNumSends(track, 1);
            DAW::SetTrackSendUIVol(track, context->GetSlotIndex() + numHardwareSends, normalizedToVol(value), 0);
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int numHardwareSends = DAW::GetTrackNumSends(track, 1);
            double vol, pan = 0.0;
            DAW::GetTrackSendUIVolPan(track, context->GetSlotIndex() + numHardwareSends, &vol, &pan);
            
            if(value == 0)
                DAW::SetTrackSendUIVol(track, context->GetSlotIndex() + numHardwareSends, vol, 1);
            else
                DAW::SetTrackSendUIVol(track, context->GetSlotIndex() + numHardwareSends, vol, 0);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSendVolumeDB : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackSendVolumeDB"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int numHardwareSends = DAW::GetTrackNumSends(track, 1);
            double vol, pan = 0.0;
            DAW::GetTrackSendUIVolPan(track, context->GetParamIndex() + numHardwareSends, &vol, &pan);
            context->UpdateWidgetValue(VAL2DB(vol));
        }
        else
            context->ClearWidget();
        
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int numHardwareSends = DAW::GetTrackNumSends(track, 1);
            DAW::SetTrackSendUIVol(track, context->GetParamIndex() + numHardwareSends, DB2VAL(value), 0);
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int numHardwareSends = DAW::GetTrackNumSends(track, 1);
            double vol, pan = 0.0;
            DAW::GetTrackSendUIVolPan(track, context->GetParamIndex() + numHardwareSends, &vol, &pan);
            
            if(value == 0)
                DAW::SetTrackSendUIVol(track, context->GetParamIndex() + numHardwareSends, vol, 1);
            else
                DAW::SetTrackSendUIVol(track, context->GetParamIndex() + numHardwareSends, vol, 0);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSendPan : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackSendPan"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int numHardwareSends = DAW::GetTrackNumSends(track, 1);
            double vol, pan = 0.0;
            DAW::GetTrackSendUIVolPan(track, context->GetSlotIndex() + numHardwareSends, &vol, &pan);
            return panToNormalized(pan);
        }
        else
            return 0.0;
    }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int numHardwareSends = DAW::GetTrackNumSends(track, 1);
            DAW::SetTrackSendUIPan(track, context->GetSlotIndex() + numHardwareSends, normalizedToPan(value), 0);
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int numHardwareSends = DAW::GetTrackNumSends(track, 1);
            double vol, pan = 0.0;
            DAW::GetTrackSendUIVolPan(track, context->GetSlotIndex() + numHardwareSends, &vol, &pan);
            
            if(value == 0)
                DAW::SetTrackSendUIPan(track, context->GetSlotIndex() + numHardwareSends, pan, 1);
            else
                DAW::SetTrackSendUIPan(track, context->GetSlotIndex() + numHardwareSends, pan, 0);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSendPanPercent : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackSendPanPercent"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int numHardwareSends = DAW::GetTrackNumSends(track, 1);
            double vol, pan = 0.0;
            DAW::GetTrackSendUIVolPan(track, context->GetParamIndex() + numHardwareSends, &vol, &pan);
            context->UpdateWidgetValue(pan * 100.0);
        }
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int numHardwareSends = DAW::GetTrackNumSends(track, 1);
            DAW::SetTrackSendUIPan(track, context->GetParamIndex() + numHardwareSends, value / 100.0, 0);
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int numHardwareSends = DAW::GetTrackNumSends(track, 1);
            double vol, pan = 0.0;
            DAW::GetTrackSendUIVolPan(track, context->GetParamIndex() + numHardwareSends, &vol, &pan);
            
            if(value == 0)
                DAW::SetTrackSendUIPan(track, context->GetParamIndex() + numHardwareSends, pan, 1);
            else
                DAW::SetTrackSendUIPan(track, context->GetParamIndex() + numHardwareSends, pan, 0);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSendMute : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackSendMute"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int numHardwareSends = DAW::GetTrackNumSends(track, 1);
            bool mute = false;
            DAW::GetTrackSendUIMute(track, context->GetSlotIndex() + numHardwareSends, &mute);
            return mute;
        }
        else
            return 0.0;
    }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
        {
            bool isMuted = ! DAW::GetTrackSendInfo_Value(track, 0, context->GetSlotIndex(), "B_MUTE");
            
            DAW::GetSetTrackSendInfo(track, 0, context->GetSlotIndex(), "B_MUTE", &isMuted);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSendInvertPolarity : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackSendInvertPolarity"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            return DAW::GetTrackSendInfo_Value(track, 0, context->GetSlotIndex(), "B_PHASE");
        else
            return 0.0;
    }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
        {
            bool reversed = ! DAW::GetTrackSendInfo_Value(track, 0, context->GetSlotIndex(), "B_PHASE");
            
            DAW::GetSetTrackSendInfo(track, 0, context->GetSlotIndex(), "B_PHASE", &reversed);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSendStereoMonoToggle : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackSendStereoMonoToggle"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            return DAW::GetTrackSendInfo_Value(track, 0, context->GetSlotIndex(), "B_MONO");
        else
            return 0.0;
    }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
        {
            bool mono = ! DAW::GetTrackSendInfo_Value(track, 0, context->GetSlotIndex(), "B_MONO");
            
            DAW::GetSetTrackSendInfo(track, 0, context->GetSlotIndex(), "B_MONO", &mono);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSendPrePost : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackSendPrePost"; }
       
    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateColorValue(0.0);
    }

    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
        {
            int mode = DAW::GetTrackSendInfo_Value(track, 0, context->GetSlotIndex(), "I_SENDMODE");
            
            if(mode == 0)
                mode = 1; // switch to pre FX
            else if(mode == 1)
                mode = 3; // switch to post FX
            else
                mode = 0; // switch to post pan
            
            DAW::GetSetTrackSendInfo(track, 0, context->GetSlotIndex(), "I_SENDMODE", &mode);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackReceiveVolume : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackReceiveVolume"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double vol, pan = 0.0;
            DAW::GetTrackReceiveUIVolPan(track, context->GetSlotIndex(), &vol, &pan);
            return volToNormalized(vol);
        }
        else
            return 0.0;
    }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
            DAW::SetTrackSendUIVol(track, -(context->GetSlotIndex() + 1), normalizedToVol(value), 0);
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double vol, pan = 0.0;
            DAW::GetTrackReceiveUIVolPan(track, context->GetSlotIndex(), &vol, &pan);
            
            if(value == 0)
                DAW::SetTrackSendUIVol(track, -(context->GetSlotIndex() + 1), vol, 1);
            else
                DAW::SetTrackSendUIVol(track, -(context->GetSlotIndex() + 1), vol, 0);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackReceiveVolumeDB : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackReceiveVolumeDB"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double vol, pan = 0.0;
            DAW::GetTrackReceiveUIVolPan(track, context->GetParamIndex(), &vol, &pan);
            context->UpdateWidgetValue(VAL2DB(vol));
        }
        else
            context->ClearWidget();
        
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            DAW::SetTrackSendUIVol(track, -(context->GetSlotIndex() + 1), DB2VAL(value), 0);
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double vol, pan = 0.0;
            DAW::GetTrackReceiveUIVolPan(track, context->GetParamIndex(), &vol, &pan);
            
            if(value == 0)
                DAW::SetTrackSendUIVol(track, -(context->GetSlotIndex() + 1), vol, 1);
            else
                DAW::SetTrackSendUIVol(track, -(context->GetSlotIndex() + 1), vol, 0);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackReceivePan : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackReceivePan"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double vol, pan = 0.0;
            DAW::GetTrackReceiveUIVolPan(track, context->GetSlotIndex(), &vol, &pan);
            return panToNormalized(pan);
        }
        else
            return 0.0;
    }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
            DAW::SetTrackSendUIPan(track, -(context->GetSlotIndex() + 1), normalizedToPan(value), 0);
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double vol, pan = 0.0;
            DAW::GetTrackReceiveUIVolPan(track, context->GetSlotIndex(), &vol, &pan);
            
            if(value == 0)
                DAW::SetTrackSendUIPan(track, -(context->GetSlotIndex() + 1), pan, 1);
            else
                DAW::SetTrackSendUIPan(track, -(context->GetSlotIndex() + 1), pan, 0);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackReceivePanPercent : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackReceivePanPercent"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double vol, pan = 0.0;
            DAW::GetTrackReceiveUIVolPan(track, context->GetParamIndex(), &vol, &pan);
            context->UpdateWidgetValue(pan * 100.0);
        }
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
            DAW::SetTrackSendUIPan(track, -(context->GetSlotIndex() + 1), value / 100.0, 0);
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double vol, pan = 0.0;
            DAW::GetTrackReceiveUIVolPan(track, context->GetParamIndex(), &vol, &pan);
            
            if(value == 0)
                DAW::SetTrackSendUIPan(track, -(context->GetSlotIndex() + 1), pan, 1);
            else
                DAW::SetTrackSendUIPan(track, -(context->GetSlotIndex() + 1), pan, 0);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackReceiveMute : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackReceivMute"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            bool mute = false;
            DAW::GetTrackReceiveUIMute(track, context->GetSlotIndex(), &mute);
            return mute;
        }
        else
            return 0.0;
    }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
        {
            bool isMuted = ! DAW::GetTrackSendInfo_Value(track, -1, context->GetSlotIndex(), "B_MUTE");
            
            DAW::GetSetTrackSendInfo(track, -1, context->GetSlotIndex(), "B_MUTE", &isMuted);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackReceiveInvertPolarity : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackReceiveInvertPolarity"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            return DAW::GetTrackSendInfo_Value(track, -1, context->GetSlotIndex(), "B_PHASE");
        else
            return 0.0;
    }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
        {
            bool reversed = ! DAW::GetTrackSendInfo_Value(track, -1, context->GetSlotIndex(), "B_PHASE");
            
            DAW::GetSetTrackSendInfo(track, -1, context->GetSlotIndex(), "B_PHASE", &reversed);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackReceiveStereoMonoToggle : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackReceiveStereoMonoToggle"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            return DAW::GetTrackSendInfo_Value(track, -1, context->GetSlotIndex(), "B_MONO");
        else
            return 0.0;
    }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
        {
            bool mono = ! DAW::GetTrackSendInfo_Value(track, -1, context->GetSlotIndex(), "B_MONO");
            
            DAW::GetSetTrackSendInfo(track, -1, context->GetSlotIndex(), "B_MONO", &mono);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackReceivePrePost : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackReceivePrePost"; }
        
    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateColorValue(0.0);
    }

    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
        {
            int mode = DAW::GetTrackSendInfo_Value(track, -1, context->GetSlotIndex(), "I_SENDMODE");
            
            if(mode == 0)
                mode = 1; // switch to pre FX
            else if(mode == 1)
                mode = 3; // switch to post FX
            else
                mode = 0; // switch to post pan

            DAW::GetSetTrackSendInfo(track, -1, context->GetSlotIndex(), "I_SENDMODE", &mode);
        }
    }
};


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MetronomePrimaryVolume : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    string GetName() override { return "MetronomePrimaryVolume"; }

    double GetCurrentNormalizedValue(ActionContext*) override
    {
        if (const double* volume = TheManager->GetMetronomePrimaryVolumePtr())
             return volToNormalized(*volume);
        else
            return 0.0;
    }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }

    void Do(ActionContext*, double value) override
    {
        double* primaryVolume = TheManager->GetMetronomePrimaryVolumePtr();
        double* secondaryVolume = TheManager->GetMetronomeSecondaryVolumePtr();

        if (primaryVolume && secondaryVolume)
        {
            const auto oldPrimaryVolume = *primaryVolume;
            const auto oldSecondaryVolume = *secondaryVolume;

            const auto normalizedValue = normalizedToVol(value);

            *primaryVolume = normalizedValue;
            *secondaryVolume = normalizedValue * (oldPrimaryVolume / oldSecondaryVolume);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MetronomeSecondaryVolume : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    string GetName() override { return "MetronomeSecondaryVolume"; }

    double GetCurrentNormalizedValue(ActionContext*) override
    {
        const auto* primaryVolume = TheManager->GetMetronomePrimaryVolumePtr();
        const auto* secondaryVolume = TheManager->GetMetronomeSecondaryVolumePtr();

        if (primaryVolume && secondaryVolume)
            return volToNormalized((*secondaryVolume) / (*primaryVolume));
        else
            return 0.0;
    }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }

    void Do(ActionContext*, double value) override
    {
        const auto* primaryVolume = TheManager->GetMetronomePrimaryVolumePtr();
        auto* secondaryVolume = TheManager->GetMetronomeSecondaryVolumePtr();

        if (primaryVolume && secondaryVolume)
            *secondaryVolume = normalizedToVol(value) * (*primaryVolume);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MetronomeVolumeDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    string GetName() override { return "MetronomeVolumeDisplay"; }

    // Should write to the provided argument the metronome volume (in linear factor). Returns true
    // if the call was sucessful, false otherwise.
    virtual bool GetVolume(double& value) const = 0;

    void RequestUpdate(ActionContext* context) override final
    {
        double volume = 0.0;

        if (GetVolume(volume))
        {
            // The min value Reaper (as of v6.68) shows for the metronome volume before displaying "-inf".
            constexpr double reaperMinMetronomeVolumeInDb = -135.0;

            // String formatters for one or two decimal digits.
            // If there is a prefix, we reduce the number of decimal digits when the volume is smaller
            // or equal to -10.0. This is to accomodate the prefix and the volume better on the display.
            constexpr auto oneDecDigitFormatter = "%7.1lf";
            constexpr auto twoDecDigitsFormatter = "%7.2lf";

            const auto stringArgument = context->GetStringParam();
            const auto hasPrefix = !stringArgument.empty();
            const auto prefix = hasPrefix ? stringArgument.front() : ' ';

            const auto volumeInDb = VAL2DB(volume);

            char str[128];

            if (volumeInDb < reaperMinMetronomeVolumeInDb)
                  snprintf(str, sizeof(str), "   -inf");
            else
                snprintf(str, sizeof(str), hasPrefix && volumeInDb <= -10.0 ? oneDecDigitFormatter : twoDecDigitsFormatter, volumeInDb);

            if (hasPrefix)
                str[0] = prefix;

            context->UpdateWidgetValue(string(str));
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MetronomePrimaryVolumeDisplay : public MetronomeVolumeDisplay
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    string GetName() override { return "MetronomePrimaryVolumeDisplay"; }

    bool GetVolume(double& value) const override
    {
        if (const double* volume = TheManager->GetMetronomePrimaryVolumePtr())
        {
            value = *volume;
            return true;
        }
        else
            return false;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MetronomeSecondaryVolumeDisplay : public MetronomeVolumeDisplay
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    string GetName() override { return "MetronomeSecondaryVolumeDisplay"; }

    bool GetVolume(double& value) const override
    {
        const auto* primaryVolume = TheManager->GetMetronomePrimaryVolumePtr();
        const auto* secondaryVolume = TheManager->GetMetronomeSecondaryVolumePtr();

        if (primaryVolume && secondaryVolume)
        {
            return value = (*secondaryVolume) / (*primaryVolume);
            return true;
        }
        else
            return false;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FXNameDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "FXNameDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            context->UpdateWidgetValue(context->GetName());
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FXMenuNameDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "FXMenuNameDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            string name = "NoMap";
            
            if(context->GetSlotIndex() >= DAW::TrackFX_GetCount(track))
                name= "";
            else
            {
                char fxName[BUFSZ];
                
                DAW::TrackFX_GetFXName(track, context->GetSlotIndex(), fxName, sizeof(fxName));
                
                name = context->GetSurface()->GetZoneManager()->GetName(fxName);
            }
            
            context->UpdateWidgetValue(name);
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SpeakFXMenuName : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SpeakFXMenuName"; }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases

        if(MediaTrack* track = context->GetTrack())
        {
            string name = "";
            
            if(context->GetSlotIndex() >= DAW::TrackFX_GetCount(track))
                name= "No FX present in this slot";
            else
            {
                char fxName[BUFSZ];
                
                DAW::TrackFX_GetFXName(track, context->GetSlotIndex(), fxName, sizeof(fxName));
                
                name = context->GetSurface()->GetZoneManager()->GetName(fxName);
                
                if(name == "No Map")
                    name = "No Zone definition for " + string(fxName);
            }
            
            TheManager->Speak(name);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FXParamNameDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "FXParamNameDisplay"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            context->UpdateWidgetValue(context->GetFxParamDisplayName());
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TCPFXParamNameDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TCPFXParamNameDisplay"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int index = context->GetIntParam();
            
            if(DAW::CountTCPFXParms(track) > index)
            {
                int fxIndex = 0;
                int paramIndex = 0;
                
                if(DAW::GetTCPFXParm(track, index, &fxIndex, &paramIndex))
                {
                    char fxParamName[BUFSZ];
                    DAW::TrackFX_GetParamName(track, fxIndex, paramIndex, fxParamName, sizeof(fxParamName));
                    context->UpdateWidgetValue(string(fxParamName));
                }
                else
                    context->ClearWidget();
            }
            else
                context->ClearWidget();
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FXParamValueDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "FXParamValueDisplay"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            char fxParamValue[128];
            DAW::TrackFX_GetFormattedParamValue(track, context->GetSlotIndex(), context->GetParamIndex(), fxParamValue, sizeof(fxParamValue));
            context->UpdateWidgetValue(string(fxParamValue));
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TCPFXParamValueDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TCPFXParamValueDisplay"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int index = context->GetIntParam();
            
            if(DAW::CountTCPFXParms(track) > index)
            {
                int fxIndex = 0;
                int paramIndex = 0;
                
                if(DAW::GetTCPFXParm(track, index, &fxIndex, &paramIndex))
                {
                    char fxParamValue[128];
                    DAW::TrackFX_GetFormattedParamValue(track, fxIndex, paramIndex, fxParamValue, sizeof(fxParamValue));
                    context->UpdateWidgetValue(string(fxParamValue));
                }
                else
                    context->ClearWidget();
            }
            else
                context->ClearWidget();
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FocusedFXParamNameDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "FocusedFXParamNameDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        int trackNum = 0;
        int fxSlotNum = 0;
        int fxParamNum = 0;
        
        if(DAW::GetLastTouchedFX(&trackNum, &fxSlotNum, &fxParamNum))
        {
            if(MediaTrack* track = DAW::GetTrack(trackNum))
            {
                char fxParamName[128];
                DAW::TrackFX_GetParamName(track, fxSlotNum, fxParamNum, fxParamName, sizeof(fxParamName));
                context->UpdateWidgetValue(string(fxParamName));
            }
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FocusedFXParamValueDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "FocusedFXParamValueDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        int trackNum = 0;
        int fxSlotNum = 0;
        int fxParamNum = 0;
        
        if(DAW::GetLastTouchedFX(&trackNum, &fxSlotNum, &fxParamNum))
        {
            if(MediaTrack* track = DAW::GetTrack(trackNum))
            {
                char fxParamValue[128];
                DAW::TrackFX_GetFormattedParamValue(track, fxSlotNum, fxParamNum, fxParamValue, sizeof(fxParamValue));
                context->UpdateWidgetValue(string(fxParamValue));
            }
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSendNameDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackSendNameDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            string sendTrackName = "";
            MediaTrack* destTrack = (MediaTrack *)DAW::GetSetTrackSendInfo(track, 0, context->GetSlotIndex() + DAW::GetTrackNumSends(track, 1), "P_DESTTRACK", 0);;
            if(destTrack)
                sendTrackName = (char *)DAW::GetSetMediaTrackInfo(destTrack, "P_NAME", NULL);
            context->UpdateWidgetValue(sendTrackName);
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SpeakTrackSendDestination : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SpeakTrackSendDestination"; }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases

        if(MediaTrack* track = context->GetTrack())
        {
            string sendTrackName = "No Send Track";
            MediaTrack* destTrack = (MediaTrack *)DAW::GetSetTrackSendInfo(track, 0, context->GetSlotIndex() + DAW::GetTrackNumSends(track, 1), "P_DESTTRACK", 0);;
            if(destTrack)
                sendTrackName = (char *)DAW::GetSetMediaTrackInfo(destTrack, "P_NAME", NULL);
            TheManager->Speak("Track " + to_string(context->GetPage()->GetIdFromTrack(destTrack)) + " " + string(sendTrackName));
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSendVolumeDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackSendVolumeDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            MediaTrack* destTrack = (MediaTrack *)DAW::GetSetTrackSendInfo(track, 0, context->GetSlotIndex() + DAW::GetTrackNumSends(track, 1), "P_DESTTRACK", 0);;
            if(destTrack)
            {
                int numHardwareSends = DAW::GetTrackNumSends(track, 1);
                double vol, pan = 0.0;
                DAW::GetTrackSendUIVolPan(track, context->GetSlotIndex() + numHardwareSends, &vol, &pan);

                char trackVolume[128];
                snprintf(trackVolume, sizeof(trackVolume), "%7.2lf", VAL2DB(vol));
                context->UpdateWidgetValue(string(trackVolume));
            }
            else
                context->ClearWidget();
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSendPanDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackSendPanDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            MediaTrack* destTrack = (MediaTrack *)DAW::GetSetTrackSendInfo(track, 0, context->GetSlotIndex() + DAW::GetTrackNumSends(track, 1), "P_DESTTRACK", 0);;
            if(destTrack)
            {
                double panVal = DAW::GetTrackSendInfo_Value(track, 0, context->GetSlotIndex() + DAW::GetTrackNumSends(track, 1), "D_PAN");
                
                context->UpdateWidgetValue(context->GetPanValueString(panVal, ""));
            }
            else
                context->ClearWidget();
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSendPrePostDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackSendPrePostDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            MediaTrack* destTrack = (MediaTrack *)DAW::GetSetTrackSendInfo(track, 0, context->GetSlotIndex() + DAW::GetTrackNumSends(track, 1), "P_DESTTRACK", 0);;
            if(destTrack)
            {
                // I_SENDMODE : returns int *, 0=post-fader, 1=pre-fx, 2=post-fx (deprecated), 3=post-fx
                
                double prePostVal = DAW::GetTrackSendInfo_Value(track, 0, context->GetSlotIndex() + DAW::GetTrackNumSends(track, 1), "I_SENDMODE");
                
                string prePostValueString = "";
                
                if(prePostVal == 0)
                    prePostValueString = "PostPan";
                else if(prePostVal == 1)
                    prePostValueString = "PreFX";
                else if(prePostVal == 2 || prePostVal == 3)
                    prePostValueString = "PostFX";
                
                context->UpdateWidgetValue(prePostValueString);
            }
            else
                context->ClearWidget();
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackReceiveNameDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackReceiveNameDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            MediaTrack* srcTrack = (MediaTrack *)DAW::GetSetTrackSendInfo(track, -1, context->GetSlotIndex(), "P_SRCTRACK", 0);
            if(srcTrack)
            {
                string receiveTrackName = "";
                receiveTrackName = (char *)DAW::GetSetMediaTrackInfo(srcTrack, "P_NAME", NULL);
                context->UpdateWidgetValue(receiveTrackName);
            }
            else
                context->ClearWidget();
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SpeakTrackReceiveSource : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SpeakTrackReceiveSource"; }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases

        if(MediaTrack* track = context->GetTrack())
        {
            MediaTrack* srcTrack = (MediaTrack *)DAW::GetSetTrackSendInfo(track, -1, context->GetSlotIndex(), "P_SRCTRACK", 0);
            if(srcTrack)
            {
                string receiveTrackName = (char *)DAW::GetSetMediaTrackInfo(srcTrack, "P_NAME", NULL);
                TheManager->Speak("Track " + to_string(context->GetPage()->GetIdFromTrack(srcTrack)) + " " + receiveTrackName);
            }
            else
                TheManager->Speak("No Receive Track");
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackReceiveVolumeDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackReceiveVolumeDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            MediaTrack* srcTrack = (MediaTrack *)DAW::GetSetTrackSendInfo(track, -1, context->GetSlotIndex(), "P_SRCTRACK", 0);
            if(srcTrack)
            {
                char trackVolume[128];
                snprintf(trackVolume, sizeof(trackVolume), "%7.2lf", VAL2DB(DAW::GetTrackSendInfo_Value(track, -1, context->GetSlotIndex(), "D_VOL")));
                context->UpdateWidgetValue(string(trackVolume));
            }
            else
                context->ClearWidget();
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackReceivePanDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackReceivePanDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            MediaTrack* srcTrack = (MediaTrack *)DAW::GetSetTrackSendInfo(track, -1, context->GetSlotIndex(), "P_SRCTRACK", 0);
            if(srcTrack)
            {
                double panVal = DAW::GetTrackSendInfo_Value(track, -1, context->GetSlotIndex(), "D_PAN");
                
                context->UpdateWidgetValue(context->GetPanValueString(panVal, ""));
            }
            else
                context->ClearWidget();
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackReceivePrePostDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackReceivePrePostDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            MediaTrack* srcTrack = (MediaTrack *)DAW::GetSetTrackSendInfo(track, -1, context->GetSlotIndex(), "P_SRCTRACK", 0);
            if(srcTrack)
            {
                // I_SENDMODE : returns int *, 0=post-fader, 1=pre-fx, 2=post-fx (deprecated), 3=post-fx
                
                double prePostVal = DAW::GetTrackSendInfo_Value(track, -1, context->GetSlotIndex(), "I_SENDMODE");
                
                string prePostValueString = "";
                
                if(prePostVal == 0)
                    prePostValueString = "PostPan";
                else if(prePostVal == 1)
                    prePostValueString = "PreFX";
                else if(prePostVal == 2 || prePostVal == 3)
                    prePostValueString = "PostFX";
                
                context->UpdateWidgetValue(prePostValueString);
            }
            else
                context->ClearWidget();
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FixedTextDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "FixedTextDisplay"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(context->GetStringParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FixedRGBColorDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "FixedRGBColorDisplay"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(0);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackNameDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackNameDisplay"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            char buf[BUFSZ];
            
            DAW::GetTrackName(track, buf, sizeof(buf));
            
            context->UpdateWidgetValue(string(buf));
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackNumberDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackNumberDisplay"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double index = DAW::GetMediaTrackInfo_Value(track, "IP_TRACKNUMBER");
            char idx[128];
            snprintf(idx, sizeof(idx), "%d", (int)index);

            context->UpdateWidgetValue(string(idx));
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackVolumeDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackVolumeDisplay"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double vol, pan = 0.0;
            DAW::GetTrackUIVolPan(track, &vol, &pan);

            char trackVolume[128];
            snprintf(trackVolume, sizeof(trackVolume), "%7.2lf", VAL2DB(vol));
            context->UpdateWidgetValue(string(trackVolume));
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackPanDisplay"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double vol, pan = 0.0;
            DAW::GetTrackUIVolPan(track, &vol, &pan);

            context->UpdateWidgetValue(context->GetPanValueString(pan, ""));
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanWidthDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackPanWidthDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double widthVal = DAW::GetMediaTrackInfo_Value(track, "D_WIDTH");
            
            context->UpdateWidgetValue(context->GetPanWidthValueString(widthVal));
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanLeftDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackPanLeftDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double panVal = DAW::GetMediaTrackInfo_Value(track, "D_DUALPANL");
            
            context->UpdateWidgetValue(context->GetPanValueString(panVal, "L"));
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanRightDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackPanRightDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double panVal = DAW::GetMediaTrackInfo_Value(track, "D_DUALPANR");
            
            context->UpdateWidgetValue(context->GetPanValueString(panVal, "R"));
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanAutoLeftDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackPanAutoLeftDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
            {
                double panVal = DAW::GetMediaTrackInfo_Value(track, "D_DUALPANL");
                context->UpdateWidgetValue(context->GetPanValueString(panVal, "L"));
            }
            else
            {
                double vol, pan = 0.0;
                DAW::GetTrackUIVolPan(track, &vol, &pan);
                context->UpdateWidgetValue(context->GetPanValueString(pan, ""));
            }
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackPanAutoRightDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackPanAutoRightDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) == 6)
            {
                double panVal = DAW::GetMediaTrackInfo_Value(track, "D_DUALPANR");
                context->UpdateWidgetValue(context->GetPanValueString(panVal, "R"));
            }
            else
            {
                double widthVal = DAW::GetMediaTrackInfo_Value(track, "D_WIDTH");
                context->UpdateWidgetValue(context->GetPanWidthValueString(widthVal));
            }
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Rewind : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "Rewind"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetSurface()->GetIsRewinding())
            context->UpdateWidgetValue(1.0);
        else
            context->UpdateWidgetValue(0.0);
    }

    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases

        context->GetSurface()->StartRewinding();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FastForward : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "FastForward"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetSurface()->GetIsFastForwarding())
            context->UpdateWidgetValue(1.0);
        else
            context->UpdateWidgetValue(0.0);
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetSurface()->StartFastForwarding();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Play : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "Play"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        int playState = DAW::GetPlayState();
        if(playState == 1 || playState == 2 || playState == 5 || playState == 6) // playing or paused or recording or paused whilst recording
            playState = 1;
        else playState = 0;

        if(context->GetSurface()->GetIsRewinding() || context->GetSurface()->GetIsFastForwarding())
            playState = 0;

        return playState;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetSurface()->Play();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Stop : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "Stop"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        int stopState = DAW::GetPlayState();
        if(stopState == 0 || stopState == 2 || stopState == 6) // stopped or paused or paused whilst recording
            stopState = 1;
        else stopState = 0;
       
        if(context->GetSurface()->GetIsRewinding() || context->GetSurface()->GetIsFastForwarding())
            stopState = 0;
        
        return stopState;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetSurface()->Stop();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Record : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "Record"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        int recordState = DAW::GetPlayState();
        if(recordState == 5 || recordState == 6) // recording or paused whilst recording
            recordState = 1;
        else recordState = 0;
        
        if(context->GetSurface()->GetIsRewinding() || context->GetSurface()->GetIsFastForwarding())
            recordState = 0;
        
        return recordState;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetSurface()->Record();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackToggleVCASpill : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackToggleVCASpill"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateColorValue(0.0);
        if(MediaTrack* track = context->GetTrack())
            context->UpdateWidgetValue(context->GetPage()->GetIsVCASpilled(track));
        else
            context->UpdateWidgetValue(0.0);
    }

    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
            context->GetPage()->ToggleVCASpill(track);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackToggleFolderSpill : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackToggleFolderSpill"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateColorValue(0.0);
        if(MediaTrack* track = context->GetTrack())
            context->UpdateWidgetValue(context->GetPage()->GetIsFolderSpilled(track));
        else
            context->UpdateWidgetValue(0.0);
    }

    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
            context->GetPage()->ToggleFolderSpill(track);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSelect : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackSelect"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            return DAW::GetMediaTrackInfo_Value(track, "I_SELECTED");
        else
            return 0.0;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }

    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases

        if(MediaTrack* track = context->GetTrack())
        {
            DAW::CSurf_SetSurfaceSelected(track, DAW::CSurf_OnSelectedChange(track, ! DAW::GetMediaTrackInfo_Value(track, "I_SELECTED")), NULL);
            context->GetPage()->OnTrackSelectionBySurface(track);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackUniqueSelect : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackUniqueSelect"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            return DAW::GetMediaTrackInfo_Value(track, "I_SELECTED");
        else
            return 0.0;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }

    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases

        if(MediaTrack* track = context->GetTrack())
        {
            DAW::SetOnlyTrackSelected(track);
            context->GetPage()->OnTrackSelectionBySurface(track);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackRangeSelect : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackRangeSelect"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            return DAW::GetMediaTrackInfo_Value(track, "I_SELECTED");
        else
            return 0.0;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }

    virtual void Do(ActionContext* context, double value) override
    {
        // GAW TBD  fix highest track bug 
        
        if(value == 0.0) return; // ignore button releases

        int currentlySelectedCount = 0;
        int selectedTrackIndex = 0;
        int trackIndex = 0;
        
       
        for(int i = 1; i <= context->GetPage()->GetNumTracks(); i++)
        {
            MediaTrack* currentTrack = context->GetPage()->GetTrackFromId(i);
           
            if(currentTrack == nullptr)
                continue;
            
            if(currentTrack == context->GetTrack())
                trackIndex = i;
            
            if(DAW::GetMediaTrackInfo_Value(currentTrack, "I_SELECTED"))
            {
                selectedTrackIndex = i;
                currentlySelectedCount++;
            }
        }
        
        if(currentlySelectedCount != 1)
            return;
        
        int lowerBound = trackIndex < selectedTrackIndex ? trackIndex : selectedTrackIndex;
        int upperBound = trackIndex > selectedTrackIndex ? trackIndex : selectedTrackIndex;

        for(int i = lowerBound; i <= upperBound; i++)
        {
            MediaTrack* currentTrack = context->GetPage()->GetTrackFromId(i);
            
            if(currentTrack == nullptr)
                continue;
            
            if(context->GetPage()->GetIsTrackVisible(currentTrack))
                DAW::CSurf_SetSurfaceSelected(currentTrack, DAW::CSurf_OnSelectedChange(currentTrack, 1), NULL);
        }
        
        MediaTrack* lowestTrack = context->GetPage()->GetTrackFromId(lowerBound);
        
        if(lowestTrack != nullptr)
            context->GetPage()->OnTrackSelectionBySurface(lowestTrack);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackRecordArm : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackRecordArm"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            return DAW::GetMediaTrackInfo_Value(track, "I_RECARM");
        else
            return 0.0;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
        {
            DAW::CSurf_SetSurfaceRecArm(track, DAW::CSurf_OnRecArmChange(track, ! DAW::GetMediaTrackInfo_Value(track, "I_RECARM")), NULL);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackMute : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackMute"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            bool mute = false;
            DAW::GetTrackUIMute(track, &mute);
            return mute;
        }
        else
            return 0.0;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
        {
            bool mute = false;
            DAW::GetTrackUIMute(track, &mute);
            DAW::CSurf_SetSurfaceMute(track, DAW::CSurf_OnMuteChange(track, ! mute), NULL);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSolo : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackSolo"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            return DAW::GetMediaTrackInfo_Value(track, "I_SOLO") > 0 ? 1 : 0;
        else
            return 0.0;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
        {
            DAW::CSurf_SetSurfaceSolo(track, DAW::CSurf_OnSoloChange(track, ! DAW::GetMediaTrackInfo_Value(track, "I_SOLO")), NULL);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ClearAllSolo : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "ClearAllSolo"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        return DAW::AnyTrackSolo(nullptr);
    }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        DAW::SoloAllTracks(0);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackInvertPolarity : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackInvertPolarity"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            return DAW::GetMediaTrackInfo_Value(track, "B_PHASE");
        else
            return 0.0;
    }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetTrack())
            context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
        {
            bool reversed = ! DAW::GetMediaTrackInfo_Value(track, "B_PHASE");
            
            DAW::GetSetMediaTrackInfo(track, "B_PHASE", &reversed);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GlobalAutoMode : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GlobalAutoMode"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(context->GetIntParam() == DAW::GetGlobalAutomationOverride())
            return 1.0;
        else
            return 0.0;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        DAW::SetGlobalAutomationOverride(context->GetIntParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackAutoMode : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackAutoMode"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        double retVal = 0.0;
        
        for(auto selectedTrack : context->GetPage()->GetSelectedTracks())
        {
            if(context->GetIntParam() == DAW::GetMediaTrackInfo_Value(selectedTrack, "I_AUTOMODE"))
            {
                retVal = 1.0;
                break;
            }
        }

        return retVal;
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return; // ignore button releases
        
        int mode = context->GetIntParam();
        
        for(auto selectedTrack : context->GetPage()->GetSelectedTracks())
            DAW::GetSetMediaTrackInfo(selectedTrack, "I_AUTOMODE", &mode);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CycleTrackAutoMode : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "CycleTrackAutoMode"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            context->UpdateWidgetValue(context->GetPage()->GetAutoModeDisplayName(DAW::GetMediaTrackInfo_Value(track, "I_AUTOMODE")));
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return;
        
        if(MediaTrack* track = context->GetTrack())
        {
            int autoModeIndex_ = DAW::GetMediaTrackInfo_Value(track, "I_AUTOMODE");
            
            if(autoModeIndex_ == 2) // skip over write mode when cycling
                autoModeIndex_ += 2;
            else
                autoModeIndex_++;
            
            if(autoModeIndex_ > 5)
                autoModeIndex_ = 0;

            DAW::GetSetMediaTrackInfo(track, "I_AUTOMODE", &autoModeIndex_);

        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CycleTimeline : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "CycleTimeline"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        return DAW::GetSetRepeatEx(nullptr, -1);
    }

    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        DAW::GetSetRepeatEx(nullptr, ! DAW::GetSetRepeatEx(nullptr, -1));
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CycleTrackInputMonitor : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "CycleTrackInputMonitor"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateColorValue(0.0);
    }

    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return;

        if(MediaTrack* track = context->GetTrack())
            context->GetPage()->NextInputMonitorMode(track);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackAutoModeDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackAutoModeDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            context->UpdateWidgetValue(context->GetPage()->GetAutoModeDisplayName(DAW::GetMediaTrackInfo_Value(track, "I_AUTOMODE")));
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackVCALeaderDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackVCALeaderDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(DAW::GetTrackGroupMembership(track, "VOLUME_VCA_LEAD") != 0 || DAW::GetTrackGroupMembershipHigh(track, "VOLUME_VCA_LEAD") != 0)
                context->UpdateWidgetValue("Leader");
            else
                context->UpdateWidgetValue("");
        }
        else
            context->UpdateWidgetValue("");
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackFolderParentDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackFolderParentDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(DAW::GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH") == 1)
                context->UpdateWidgetValue("Parent");
            else
                context->UpdateWidgetValue("");
        }
        else
            context->UpdateWidgetValue("");
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GlobalAutoModeDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GlobalAutoModeDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            context->UpdateWidgetValue(context->GetPage()->GetGlobalAutoModeDisplayName());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackInputMonitorDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackInputMonitorDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
            context->UpdateWidgetValue(context->GetPage()->GetCurrentInputMonitorMode(track));
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MCUTimeDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "MCUTimeDisplay"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(0);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSCTimeDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "OSCTimeDisplay"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        string timeStr = "";
        
        double pp=(DAW::GetPlayState()&1) ? DAW::GetPlayPosition() : DAW::GetCursorPosition();

        int *tmodeptr = TheManager->GetTimeMode2Ptr();
        
        int tmode = 0;
        
        if(tmodeptr && (*tmodeptr)>=0) tmode = *tmodeptr;
        else
        {
            tmodeptr = TheManager->GetTimeModePtr();
            if (tmodeptr)
                tmode=*tmodeptr;
        }

        if(tmode == 3) // seconds
        {
            double *toptr = TheManager->GetTimeOffsPtr();
            
            if (toptr)
                pp+=*toptr;
            
            timeStr = to_string((int)pp) + " " + to_string(((int)(pp*100.0))%100);
        }
        else if(tmode==4) // samples
        {
            char buf[128];
            DAW::format_timestr_pos(pp, buf, sizeof(buf), 4);
            timeStr = string(buf);
        }
        else if(tmode == 5) // frames
        {
            char buf[128];
            DAW::format_timestr_pos(pp, buf, sizeof(buf), 5);
            timeStr = string(buf);
        }
        else if(tmode > 0)
        {
            int num_measures=0;
            int currentTimeSignatureNumerator=0;
            double beats=DAW::TimeMap2_timeToBeats(NULL,pp,&num_measures,&currentTimeSignatureNumerator,NULL,NULL)+ 0.000000000001;
            double nbeats = floor(beats);
            
            beats -= nbeats;
               
            if (num_measures <= 0 && pp < 0.0)
                --num_measures;
            
            int *measptr = TheManager->GetMeasOffsPtr();
          
            timeStr = to_string(num_measures+1+(measptr ? *measptr : 0)) + " " + to_string((int)(nbeats + 1)) + " ";
            
            int subBeats = (int)(1000.0 * beats);

            if(subBeats < 10)
                timeStr += "00";
            else if(subBeats < 100)
                timeStr += "0";
            
            timeStr += to_string(subBeats);
        }
        else
        {
            double *toptr = TheManager->GetTimeOffsPtr();
            if (toptr) pp+=(*toptr);
            
            int ipp=(int)pp;
            int fr=(int)((pp-ipp)*1000.0);
            
            int hours = (int)(ipp/3600);
            if(hours < 10)
                timeStr += "00";
            else if(hours < 100)
                timeStr += "0";
            
            timeStr += to_string(hours) + ":";
            
            int minutes = ((int)(ipp/60)) %3600;
            if(minutes < 10)
                timeStr += "0";
            
            timeStr += to_string(minutes) + ":";

            int seconds = ((int)ipp) %60;
            if(seconds < 10)
                timeStr += "0";
            
            timeStr += to_string(seconds) + ":";

            int frames =(int)fr;
            if(frames < 10)
                timeStr += "00";
            else if(frames < 100)
                timeStr += "0";
            
            timeStr += to_string((int)fr);
        }

        context->UpdateWidgetValue(timeStr);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackOutputMeter : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackOutputMeter"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {           
            if(DAW::AnyTrackSolo(nullptr) && ! DAW::GetMediaTrackInfo_Value(track, "I_SOLO"))
                context->ClearWidget();
            else
                context->UpdateWidgetValue(volToNormalized(DAW::Track_GetPeakInfo(track, context->GetIntParam())));
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackOutputMeterAverageLR : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackOutputMeterAverageLR"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double lrVol = (DAW::Track_GetPeakInfo(track, 0) + DAW::Track_GetPeakInfo(track, 1)) / 2.0;
            
            if(DAW::AnyTrackSolo(nullptr) && ! DAW::GetMediaTrackInfo_Value(track, "I_SOLO"))
                context->ClearWidget();
            else
                context->UpdateWidgetValue(volToNormalized(lrVol));
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackOutputMeterMaxPeakLR : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackOutputMeterMaxPeakLR"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double lVol = DAW::Track_GetPeakInfo(track, 0);
            double rVol = DAW::Track_GetPeakInfo(track, 1);
            
            double lrVol =  lVol > rVol ? lVol : rVol;
            
            if(DAW::AnyTrackSolo(nullptr) && ! DAW::GetMediaTrackInfo_Value(track, "I_SOLO"))
                context->ClearWidget();
            else
                context->UpdateWidgetValue(volToNormalized(lrVol));
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FXGainReductionMeter : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "FXGainReductionMeter"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        char buffer[BUFSZ];
        
        if(MediaTrack* track = context->GetTrack())
        {
            if(DAW::TrackFX_GetNamedConfigParm(track, context->GetSlotIndex(), "GainReduction_dB", buffer, sizeof(buffer)))
                context->UpdateWidgetValue(-atof(buffer)/20.0);
            else
                context->UpdateWidgetValue(0.0);
        }
        else
            context->ClearWidget();
    }
};

#endif /* control_surface_Reaper_actions_h */
