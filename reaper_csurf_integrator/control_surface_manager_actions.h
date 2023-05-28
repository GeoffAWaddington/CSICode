//
//  control_surface_manager_actions.h
//  reaper_csurf_integrator
//
//

#ifndef control_surface_manager_actions_h
#define control_surface_manager_actions_h

#include "control_surface_integrator.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SendMIDIMessage : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SendMIDIMessage"; }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        vector<string> tokens = GetTokens(context->GetStringParam());
        
        if(tokens.size() == 3)
        {
            context->GetSurface()->SendMidiMessage(strToHex(tokens[0]), strToHex(tokens[1]), strToHex(tokens[2]));
        }
        else
        {
            struct
            {
                MIDI_event_ex_t evt;
                char data[128];
            } midiSysExData;
            
            midiSysExData.evt.frame_offset = 0;
            midiSysExData.evt.size = 0;
            
            for(int i = 0; i < tokens.size(); i++)
                midiSysExData.evt.midi_message[midiSysExData.evt.size++] = strToHex(tokens[i]);
            
            context->GetSurface()->SendMidiSysExMessage(&midiSysExData.evt);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SendOSCMessage : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SendOSCMessage"; }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        vector<string> tokens = GetTokens(context->GetStringParam());

        if(tokens.size() != 2)
            return;
        
        if(regex_match(tokens[1], regex("(\\+|-)?[[:digit:]]+")))
            context->GetSurface()->SendOSCMessage(tokens[0], stoi(tokens[1]));
        else if(regex_match(tokens[1], regex("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)")))
            context->GetSurface()->SendOSCMessage(tokens[0], stod(tokens[1]));
        else
            context->GetSurface()->SendOSCMessage(tokens[0], tokens[1]);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SpeakOSARAMessage : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SpeakOSARAMessage"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateColorValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        TheManager->Speak(context->GetStringParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetXTouchDisplayColors : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SetXTouchDisplayColors"; }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetZone()->SetXTouchDisplayColors(context->GetStringParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class RestoreXTouchDisplayColors : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "RestoreXTouchDisplayColors"; }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetZone()->RestoreXTouchDisplayColors();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SaveProject : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SaveProject"; }
    
    void RequestUpdate(ActionContext* context) override
    {
        if(DAW::IsProjectDirty())
            context->UpdateWidgetValue(1);
        else
            context->UpdateWidgetValue(0);
    }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(DAW::IsProjectDirty())
            DAW::SaveProject();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Undo : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "Undo"; }
    
    void RequestUpdate(ActionContext* context) override
    {
        if(DAW::CanUndo())
            context->UpdateWidgetValue(1);
        else
            context->UpdateWidgetValue(0);
    }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(DAW::CanUndo())
            DAW::Undo();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Redo : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "Redo"; }
    
    void RequestUpdate(ActionContext* context) override
    {
        if(DAW::CanRedo())
            context->UpdateWidgetValue(1);
        else
            context->UpdateWidgetValue(0);
    }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(DAW::CanRedo())
            DAW::Redo();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ToggleSynchPageBanking : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "ToggleSynchPageBanking"; }
    
    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(context->GetPage()->GetSynchPages());
    }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetPage()->ToggleSynchPages();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ToggleScrollLink : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "ToggleScrollLink"; }
    
    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(context->GetPage()->GetScrollLink());
    }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetPage()->ToggleScrollLink(context->GetIntParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GlobalModeDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GlobalModeDisplay"; }
    
    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(context->GetSurface()->GetGlobal());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CycleTimeDisplayModes : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "CycleTimeDisplayModes"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateColorValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        TheManager->NextTimeDisplayMode();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoNextPage : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GoNextPage"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateColorValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        TheManager->NextPage();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoPage : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GoPage"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateColorValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        TheManager->GoToPage(context->GetStringParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class PageNameDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "PageNameDisplay"; }
    
    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(context->GetPage()->GetName());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoHome : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GoHome"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetSurface()->GetZoneManager()->GetIsHomeZoneOnlyActive())
            context->UpdateWidgetValue(1.0);
        else
            context->UpdateWidgetValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->GoHome();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class AllSurfacesGoHome : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "AllSurfacesGoHome"; }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return; // ignore button releases
        
        context->GetPage()->GoHome();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoSubZone : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GoSubZone"; }
    
    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return; // ignore button releases
        
        context->GetZone()->GoSubZone(context->GetStringParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class LeaveSubZone : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "LeaveSubZone"; }
    
    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(1.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return; // ignore button releases
        
        context->GetZone()->Deactivate();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoFXSlot  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GoFXSlot"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateColorValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
            context->GetPage()->GoTrackFXSlot(track, context->GetZone()->GetNavigator(), context->GetSlotIndex());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ToggleEnableFocusedFXMapping  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "ToggleEnableFocusedFXMapping"; }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(context->GetSurface()->GetZoneManager()->GetIsFocusedFXMappingEnabled());
    }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->ToggleEnableFocusedFXMapping();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ToggleEnableFocusedFXParamMapping  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "ToggleEnableFocusedFXParamMapping"; }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(context->GetSurface()->GetZoneManager()->GetIsFocusedFXParamMappingEnabled());
    }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->ToggleEnableFocusedFXParamMapping();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ToggleAutoFocusedFXMapping  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "ToggleAutoFocusedFXMapping"; }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(context->GetSurface()->GetZoneManager()->GetIsAutoFocusedFXMappingEnabled());
    }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->ToggleEnableAutoFocusedFXMapping();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ToggleAutoFXMapping  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "ToggleAutoFXMapping"; }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(context->GetSurface()->GetZoneManager()->GetIsAutoFXMappingEnabled());
    }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->ToggleEnableAutoFXMapping();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoSelectedTrackFX  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GoSelectedTrackFX"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetSurface()->GetZoneManager()->GetIsAssociatedZoneActive("SelectedTrackFX"))
            context->UpdateWidgetValue(1.0);
        else
            context->UpdateWidgetValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
            context->GetSurface()->GetZoneManager()->GoSelectedTrackFX();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoAssociatedZone : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GoAssociatedZone"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetSurface()->GetZoneManager()->GetIsAssociatedZoneActive(context->GetStringParam()))
            context->UpdateWidgetValue(1.0);
        else
            context->UpdateWidgetValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return; // ignore button releases
        
        context->GetPage()->GoAssociatedZone(context->GetStringParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ClearFXSlot : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "ClearFXSlot"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetSurface()->GetZoneManager()->GetIsAssociatedZoneActive(context->GetStringParam()))
            context->UpdateWidgetValue(1.0);
        else
            context->UpdateWidgetValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return; // ignore button releases

        context->GetPage()->ClearFXSlot(context->GetZone());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Bank : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "Bank"; }

    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateColorValue(0.0);
    }
    
    void Do(ActionContext* context, double value) override
    {
        if(value < 0 && context->GetRangeMinimum() < 0)
            TheManager->AdjustBank(context->GetPage(), context->GetStringParam(), context->GetIntParam());
        else if(value > 0 && context->GetRangeMinimum() >= 0)
            TheManager->AdjustBank(context->GetPage(), context->GetStringParam(), context->GetIntParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetShift : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SetShift"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        return context->GetSurface()->GetShift();
    }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext* context, double value) override
    {
        context->GetSurface()->SetShift(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetOption : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SetOption"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        return context->GetSurface()->GetOption();
    }
    
    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext* context, double value) override
    {
        context->GetSurface()->SetOption(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetControl : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SetControl"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        return context->GetSurface()->GetControl();
    }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext* context, double value) override
    {
        context->GetSurface()->SetControl(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetAlt : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SetAlt"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        return context->GetSurface()->GetAlt();
    }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext* context, double value) override
    {
        context->GetSurface()->SetAlt(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetFlip : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SetFlip"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        return context->GetSurface()->GetFlip();
    }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext* context, double value) override
    {
        context->GetSurface()->SetFlip(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetGlobal : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SetGlobal"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        return context->GetSurface()->GetGlobal();
    }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext* context, double value) override
    {
        context->GetSurface()->SetGlobal(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetMarker : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SetMarker"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        return context->GetSurface()->GetMarker();
    }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext* context, double value) override
    {
        context->GetSurface()->SetMarker(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetNudge : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SetNudge"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        return context->GetSurface()->GetNudge();
    }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext* context, double value) override
    {
        context->GetSurface()->SetNudge(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetZoom : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SetZoom"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        return context->GetSurface()->GetZoom();
    }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext* context, double value) override
    {
        context->GetSurface()->SetZoom(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetScrub : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SetScrub"; }

    virtual double GetCurrentNormalizedValue(ActionContext* context) override
    {
        return context->GetSurface()->GetScrub();
    }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext* context, double value) override
    {
        context->GetSurface()->SetScrub(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ClearModifiers : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "ClearModifiers"; }
   
    void Do(ActionContext* context, double value) override
    {
        context->GetSurface()->ClearModifiers();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetToggleChannel : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SetToggleChannel"; }
     
    virtual void RequestUpdate(ActionContext* context) override
    {
        context->UpdateColorValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetSurface()->ToggleChannel(context->GetWidget()->GetChannelNumber());
    }
};

#endif /* control_surface_manager_actions_h */
