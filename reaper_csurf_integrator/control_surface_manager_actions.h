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
            
            context->GetSurface()->SendMidiMessage(&midiSysExData.evt);
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
        
        // GAW TBD -- get a handle on requirements for this
        
        
        
        
        
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SpeakOSARAMessage : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SpeakOSARAMessage"; }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        TheManager->Speak(context->GetStringParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetAllDisplaysColor : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SetAllDisplaysColor"; }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetZone()->SetAllDisplaysColor(context->GetStringParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class RestoreAllDisplaysColor : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "RestoreAllDisplaysColor"; }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetZone()->RestoreAllDisplaysColor();
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
class WidgetMode : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "WidgetMode"; }
    
    void RequestUpdateWidgetMode(ActionContext* context) override
    {
        context->UpdateWidgetMode(context->GetStringParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetWidgetMode : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SetWidgetMode"; }
    
    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(1.0);
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
class CycleTrackVCAFolderModes : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "CycleTrackVCAFolderModes"; }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetPage()->NextTrackVCAFolderMode();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackVCAFolderModeDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackVCAFolderModeDisplay"; }
    
    void RequestUpdate(ActionContext* context) override
    {
        int trackVCAFolderMode = context->GetPage()->GetCurrentTrackVCAFolderMode();
        
        context->UpdateWidgetValue(trackVCAFolderMode);
        
        string folderModeStr = "";
        
        if(trackVCAFolderMode == 0)
            folderModeStr = "Track";
        else if(trackVCAFolderMode == 1)
            folderModeStr = "VCA";
        else if(trackVCAFolderMode == 2)
            folderModeStr = "Folder";

        context->UpdateWidgetValue(folderModeStr);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CycleTimeDisplayModes : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "CycleTimeDisplayModes"; }

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
            context->ClearWidget();
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->GoHome();
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

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* track = context->GetTrack())
            context->GetSurface()->GetZoneManager()->GoTrackFXSlot(track, context->GetZone()->GetNavigator(), context->GetSlotIndex());
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
class GoTrackSend : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GoTrackSend"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetSurface()->GetZoneManager()->GetIsAssociatedZoneActive("TrackSend"))
           context->UpdateWidgetValue(1.0);
        else
            context->UpdateWidgetValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->GoAssociatedZone("TrackSend");
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoTrackReceive : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GoTrackReceive"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetSurface()->GetZoneManager()->GetIsAssociatedZoneActive("TrackReceive"))
           context->UpdateWidgetValue(1.0);
        else
            context->UpdateWidgetValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->GoAssociatedZone("TrackReceive");
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoTrackFXMenu : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GoTrackFXMenu"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetSurface()->GetZoneManager()->GetIsAssociatedZoneActive("TrackFXMenu"))
           context->UpdateWidgetValue(1.0);
        else
            context->UpdateWidgetValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->GoAssociatedZone("TrackFXMenu");
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoSelectedTrack : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GoSelectedTrack"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetSurface()->GetZoneManager()->GetIsAssociatedZoneActive("SelectedTrack"))
           context->UpdateWidgetValue(1.0);
        else
            context->UpdateWidgetValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->GoAssociatedZone("SelectedTrack");
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoSelectedTrackSend : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GoSelectedTrackSend"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetSurface()->GetZoneManager()->GetIsAssociatedZoneActive("SelectedTrackSend"))
           context->UpdateWidgetValue(1.0);
        else
            context->UpdateWidgetValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->GoAssociatedZone("SelectedTrackSend");
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoSelectedTrackReceive : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GoSelectedTrackReceive"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetSurface()->GetZoneManager()->GetIsAssociatedZoneActive("SelectedTrackReceive"))
           context->UpdateWidgetValue(1.0);
        else
            context->UpdateWidgetValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->GoAssociatedZone("SelectedTrackReceive");
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoSelectedTrackFXMenu : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "GoSelectedTrackFXMenu"; }
    
    virtual void RequestUpdate(ActionContext* context) override
    {
        if(context->GetSurface()->GetZoneManager()->GetIsAssociatedZoneActive("SelectedTrackFXMenu"))
           context->UpdateWidgetValue(1.0);
        else
            context->UpdateWidgetValue(0.0);
    }

    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0)
            return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->GoAssociatedZone("SelectedTrackFXMenu");
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackBank : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackBank"; }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        TheManager->AdjustTrackBank(context->GetPage(), context->GetIntParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SelectedTrackBank : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SelectedTrackBank"; }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        if(MediaTrack* selectedTrack = context->GetSurface()->GetPage()->GetSelectedTrack())
        {
            int trackNum = context->GetSurface()->GetPage()->GetIdFromTrack(selectedTrack);
            
            trackNum += context->GetIntParam();
            
            if(trackNum < 1)
                trackNum = 1;
            
            if(trackNum > context->GetPage()->GetNumTracks())
                trackNum = context->GetPage()->GetNumTracks();
            
            if(MediaTrack* trackToSelect = context->GetPage()->GetTrackFromId(trackNum))
            {
                DAW::SetOnlyTrackSelected(trackToSelect);
                if(context->GetPage()->GetScrollLink())
                    DAW::SetMixerScroll(trackToSelect);
                context->GetSurface()->OnTrackSelection(trackToSelect);
            }
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SelectedTrackSendBank : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SelectedTrackSendBank"; }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->AdjustSelectedTrackSendBank(context->GetIntParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SelectedTrackReceiveBank : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SelectedTrackReceiveBank"; }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->AdjustSelectedTrackReceiveBank(context->GetIntParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SelectedTrackFXMenuBank : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SelectedTrackFXMenuBank"; }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->AdjustSelectedTrackFXMenuBank(context->GetIntParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackSendBank : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackSendBank"; }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->AdjustTrackSendBank(context->GetIntParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackReceiveBank : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackReceiveBank"; }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->AdjustTrackReceiveBank(context->GetIntParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackFXMenuBank : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "TrackFXMenuBank"; }
    
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->AdjustTrackFXMenuBank(context->GetIntParam());
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
        return context->GetPage()->GetShift();
    }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext* context, double value) override
    {
        context->GetPage()->SetShift(value);
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
        return context->GetPage()->GetOption();
    }
    
    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext* context, double value) override
    {
        context->GetPage()->SetOption(value);
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
        return context->GetPage()->GetControl();
    }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext* context, double value) override
    {
        context->GetPage()->SetControl(value);
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
        return context->GetPage()->GetAlt();
    }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext* context, double value) override
    {
        context->GetPage()->SetAlt(value);
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
        return context->GetPage()->GetFlip();
    }

    void RequestUpdate(ActionContext* context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext* context, double value) override
    {
        context->GetPage()->SetFlip(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetToggleChannel : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "SetToggleChannel"; }
       
    void Do(ActionContext* context, double value) override
    {
        if(value == 0.0) return; // ignore button releases
        
        context->GetSurface()->ToggleChannel(context->GetWidget()->GetChannelNumber());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Broadcast  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "Broadcast"; }

    void Do(ActionContext* context, double value) override
    {
        context->GetSurface()->GetZoneManager()->SetBroadcast(context);
    }
    
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Receive  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual string GetName() override { return "Receive"; }

    void Do(ActionContext* context, double value) override
    {
        context->GetSurface()->GetZoneManager()->SetReceive(context);
    }
};

#endif /* control_surface_manager_actions_h */
