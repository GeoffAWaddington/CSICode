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
            if(DAW::TrackFX_GetCount(track) > context->GetSlotIndex())
            {
                if(DAW::TrackFX_GetEnabled(track, context->GetSlotIndex()))
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
            DAW::TrackFX_SetEnabled(track, context->GetSlotIndex(), ! DAW::TrackFX_GetEnabled(track, context->GetSlotIndex()));
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
class MCUTrackPan : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "MCUTrackPan"; }
    
    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) != 6)
            {
                if(context->GetZone()->GetNavigator()->GetIsMCUTrackPanWidth() == false)
                {
                    double vol, pan = 0.0;
                    DAW::GetTrackUIVolPan(track, &vol, &pan);
                    return panToNormalized(pan);
                }
                else
                    return panToNormalized(DAW::GetMediaTrackInfo_Value(track, "D_WIDTH"));
            }
            else
            {
                if(context->GetZone()->GetNavigator()->GetIsMCUTrackPanWidth() == false)
                    return panToNormalized(DAW::GetMediaTrackInfo_Value(track, "D_DUALPANL"));
                else
                    return panToNormalized(DAW::GetMediaTrackInfo_Value(track, "D_DUALPANR"));
            }
        }
        
        return 0.0;
    }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            int displayMode = 0;
            
            if(GetPanMode(track) != 6 && context->GetZone()->GetNavigator()->GetIsMCUTrackPanWidth())
                displayMode = 1;
            
            context->UpdateWidgetValue(displayMode, GetCurrentNormalizedValue(context));
        }
        else
            context->ClearWidget();
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            double pan = normalizedToPan(value);
            
            if(GetPanMode(track) != 6)
            {
                if(context->GetZone()->GetNavigator()->GetIsMCUTrackPanWidth() == false)
                    DAW::CSurf_SetSurfacePan(track, DAW::CSurf_OnPanChange(track, pan, false), NULL);
                else
                    DAW::CSurf_OnWidthChange(track, pan, false);
            }
            else
            {               
                if(context->GetZone()->GetNavigator()->GetIsMCUTrackPanWidth() == false)
                    DAW::GetSetMediaTrackInfo(track, "D_DUALPANL", &pan);
                else
                    DAW::GetSetMediaTrackInfo(track, "D_DUALPANR", &pan);
            }
        }
    }
    
    virtual void Touch(ActionContext* context, double value) override
    {
        context->GetZone()->GetNavigator()->SetIsPanTouched(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ToggleMCUTrackPanWidth : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "ToggleMCUTrackPanWidth"; }

    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases

        context->GetZone()->GetNavigator()->ToggleIsMCUTrackPanWidth();
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
            context->UpdateWidgetValue(context->GetIntParam(), GetCurrentNormalizedValue(context));
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
                context->UpdateWidgetValue(context->GetIntParam(), GetCurrentNormalizedValue(context));
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
                
                name = context->GetSurface()->GetZoneManager()->GetNameOrAlias(fxName);
            }
            
            context->UpdateWidgetValue(name);
        }
        else
            context->ClearWidget();
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
class FXBypassedDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "FXBypassedDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(DAW::TrackFX_GetCount(track) > context->GetSlotIndex())
            {
                if(DAW::TrackFX_GetEnabled(track, context->GetSlotIndex()))
                    context->UpdateWidgetValue("Enabled");
                else
                    context->UpdateWidgetValue("Bypassd");
            }
            else
                context->UpdateWidgetValue("");
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
                
                context->UpdateWidgetValue(context->GetPanValueString(panVal));
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
                
                context->UpdateWidgetValue(context->GetPanValueString(panVal));
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

            context->UpdateWidgetValue(context->GetPanValueString(pan));
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
            
            context->UpdateWidgetValue(context->GetPanValueString(panVal));
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
            
            context->UpdateWidgetValue(context->GetPanValueString(panVal));
        }
        else
            context->ClearWidget();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MCUTrackPanDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "MCUTrackPanDisplay"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(MediaTrack* track = context->GetTrack())
        {
            if(GetPanMode(track) != 6 && context->GetZone()->GetNavigator()->GetIsMCUTrackPanWidth() == true)
            {
                double widthVal = DAW::GetMediaTrackInfo_Value(track, "D_WIDTH");

                context->UpdateWidgetValue(context->GetPanWidthValueString(widthVal));
            }
            else
            {
                double panVal = 0.0;
                
                if(GetPanMode(track) != 6)
                    panVal = DAW::GetMediaTrackInfo_Value(track, "D_PAN");
                else
                {
                    if(context->GetZone()->GetNavigator()->GetIsMCUTrackPanWidth() == false)
                        panVal = DAW::GetMediaTrackInfo_Value(track, "D_DUALPANL");
                    else
                        panVal = DAW::GetMediaTrackInfo_Value(track, "D_DUALPANR");
                }
                
                context->UpdateWidgetValue(context->GetPanValueString(panVal));
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

    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
            context->GetPage()->ToggleVCASpill(track);
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
        context->GetPage()->SetAutoModeIndex();
     
        context->UpdateWidgetValue(context->GetPage()->GetAutoModeDisplayName());
    }
    
    virtual void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return;
        
        context->GetPage()->NextAutoMode();
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
