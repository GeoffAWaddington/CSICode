//
//  control_surface_manager_actions.h
//  reaper_csurf_integrator
//
//

#ifndef control_surface_manager_actions_h
#define control_surface_manager_actions_h

/*
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class DumpHex : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "DumpHex"; }

    void Do(ActionContext *context, double value) override
    {
        string binFilePath = string(DAW::GetResourcePath()) + "/CSI/EncoderModule.bin";
        string hexFilePath = string(DAW::GetResourcePath()) + "/CSI/EncoderModuleFirmware.h";

        // open the file:
        streampos fileSize;
        ifstream file(binFilePath, ios::binary);

        // get its size:
        file.seekg(0, ios::end);
        fileSize = file.tellg();
        file.seekg(0, ios::beg);

        // read the data:
        vector<BYTE> fileData(fileSize);
        file.read((char*) &fileData[0], fileSize);
        
        file.close();
        
        ofstream hexFile(hexFilePath);
        
        hexFile << "const uint8_t encoderModuleFirmware[";
        
        hexFile << fileSize;
        
        hexFile << "] = {\n";
        
        int byteCount = 0;
        
        for (auto byte : fileData)
        {
            byteCount++;
            
            hexFile << "0x";
            
            hexFile << uppercase << hex << setw(2) << setfill('0') << int(byte);

            if (byteCount < fileSize)
                hexFile << ", ";
            else
                hexFile << "};";
            
            if (byteCount % 16 == 0)
                hexFile << "\n";
        }
        
        hexFile.close();
    }
};
*/
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SendMIDIMessage : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "SendMIDIMessage"; }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        vector<string> tokens;
        GetTokens(tokens, context->GetStringParam());
        
        if (tokens.size() == 3)
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
            
            for (int i = 0; i < tokens.size(); i++)
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
    virtual const char *GetName() override { return "SendOSCMessage"; }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        vector<string> tokens;
        GetTokens(tokens, context->GetStringParam());

        if (tokens.size() != 2)
            return;
        
        if (regex_match(tokens[1], regex("(\\+|-)?[[:digit:]]+")))
            context->GetSurface()->SendOSCMessage(tokens[0], stoi(tokens[1]));
        else if (regex_match(tokens[1], regex("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)")))
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
    virtual const char *GetName() override { return "SpeakOSARAMessage"; }
    
    virtual void RequestUpdate(ActionContext *context) override
    {
        context->UpdateColorValue(0.0);
    }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        context->GetCSI()->Speak(context->GetStringParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetXTouchDisplayColors : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "SetXTouchDisplayColors"; }
    
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        context->GetZone()->SetXTouchDisplayColors(context->GetStringParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class RestoreXTouchDisplayColors : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "RestoreXTouchDisplayColors"; }
    
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        context->GetZone()->RestoreXTouchDisplayColors();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SaveProject : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "SaveProject"; }
    
    void RequestUpdate(ActionContext *context) override
    {
        if (DAW::IsProjectDirty())
            context->UpdateWidgetValue(1);
        else
            context->UpdateWidgetValue(0.0);
    }
    
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        if (DAW::IsProjectDirty())
            DAW::SaveProject();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Undo : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "Undo"; }
    
    void RequestUpdate(ActionContext *context) override
    {
        if (DAW::CanUndo())
            context->UpdateWidgetValue(1);
        else
            context->UpdateWidgetValue(0.0);
    }
    
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        if (DAW::CanUndo())
            DAW::Undo();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Redo : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "Redo"; }
    
    void RequestUpdate(ActionContext *context) override
    {
        if (DAW::CanRedo())
            context->UpdateWidgetValue(1);
        else
            context->UpdateWidgetValue(0.0);
    }
    
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        if (DAW::CanRedo())
            DAW::Redo();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ToggleSynchPageBanking : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "ToggleSynchPageBanking"; }
    
    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(context->GetPage()->GetSynchPages());
    }
    
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        context->GetPage()->ToggleSynchPages();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ToggleScrollLink : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "ToggleScrollLink"; }
    
    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(context->GetPage()->GetScrollLink());
    }
    
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        context->GetPage()->ToggleScrollLink(context->GetIntParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ToggleRestrictTextLength : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "ToggleRestrictTextLength"; }
        
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        context->GetSurface()->ToggleRestrictTextLength(context->GetIntParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CSINameDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "CSINameDisplay"; }
    
    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(s_CSIName);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CSIVersionDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "CSIVersionDisplay"; }
    
    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(s_CSIVersionDisplay);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GlobalModeDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "GlobalModeDisplay"; }
    
    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(context->GetSurface()->GetGlobal());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CycleTimeDisplayModes : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "CycleTimeDisplayModes"; }

    virtual void RequestUpdate(ActionContext *context) override
    {
        context->UpdateColorValue(0.0);
    }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        context->GetCSI()->NextTimeDisplayMode();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoNextPage : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "GoNextPage"; }

    virtual void RequestUpdate(ActionContext *context) override
    {
        context->UpdateColorValue(0.0);
    }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        context->GetCSI()->NextPage();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoPage : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "GoPage"; }
    
    virtual void RequestUpdate(ActionContext *context) override
    {
        context->UpdateColorValue(0.0);
    }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        context->GetCSI()->GoToPage(context->GetStringParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class PageNameDisplay : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "PageNameDisplay"; }
    
    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(context->GetPage()->GetName());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoHome : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "GoHome"; }
    
    virtual void RequestUpdate(ActionContext *context) override
    {
        if (context->GetSurface()->GetZoneManager()->GetIsHomeZoneOnlyActive())
            context->UpdateWidgetValue(1.0);
        else
            context->UpdateWidgetValue(0.0);
    }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0)
            return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->DeclareGoHome();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class AllSurfacesGoHome : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "AllSurfacesGoHome"; }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0)
            return; // ignore button releases
        
        context->GetPage()->GoHome();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoSubZone : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "GoSubZone"; }
    
    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(0.0);
    }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0)
            return; // ignore button releases
        
        context->GetZone()->GoSubZone(context->GetStringParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class LeaveSubZone : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "LeaveSubZone"; }
    
    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(1.0);
    }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0)
            return; // ignore button releases
        
        context->GetZone()->Deactivate();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoFXSlot  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "GoFXSlot"; }

    virtual void RequestUpdate(ActionContext *context) override
    {
        context->UpdateColorValue(0.0);
    }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        if (MediaTrack *track = context->GetTrack())
            context->GetSurface()->GetZoneManager()->DeclareGoFXSlot(track, context->GetZone()->GetNavigator(), context->GetSlotIndex());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ShowFXSlot  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "ShowFXSlot"; }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        if (MediaTrack *track = context->GetTrack())
            DAW::TrackFX_SetOpen(track, context->GetSlotIndex(), true);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class HideFXSlot  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "HideFXSlot"; }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        if (MediaTrack *track = context->GetTrack())
            DAW::TrackFX_SetOpen(track, context->GetSlotIndex(), false);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ToggleUseLocalModifiers  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "ToggleUseLocalModifiers"; }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        context->GetSurface()->ToggleUseLocalModifiers();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetLatchTime  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "SetLatchTime"; }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        context->GetSurface()->SetLatchTime(context->GetIntParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ToggleEnableFocusedFXMapping  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "ToggleEnableFocusedFXMapping"; }

    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(context->GetSurface()->GetZoneManager()->GetIsFocusedFXMappingEnabled());
    }
    
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->DeclareToggleEnableFocusedFXMapping();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ToggleEnableFocusedFXParamMapping  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "ToggleEnableFocusedFXParamMapping"; }

    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(context->GetSurface()->GetZoneManager()->GetIsFocusedFXParamMappingEnabled());
    }
    
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->DeclareToggleEnableFocusedFXParamMapping();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class RemapAutoZone  : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "RemapAutoZone"; }
    
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->RemapAutoZone();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class AutoMapSlotFX : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "AutoMapFX"; }
    
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0)
            return; // ignore button releases
        
        if (MediaTrack *track = context->GetTrack())
        {
            int slotIndex = context->GetZone()->GetSlotIndex();
            
            char fxName[BUFSZ];
            DAW::TrackFX_GetFXName(track, slotIndex, fxName, sizeof(fxName));

            context->GetSurface()->GetZoneManager()->AutoMapFX(fxName, track, slotIndex);
        }
        
        context->GetZone()->Deactivate();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class AutoMapFocusedFX : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "AutoMapFocusedFX"; }
    
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0)
            return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->AutoMapFocusedFX();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoAssociatedZone : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "GoAssociatedZone"; }
    
    virtual void RequestUpdate(ActionContext *context) override
    {
        if (context->GetSurface()->GetZoneManager()->GetIsAssociatedZoneActive(context->GetStringParam()))
            context->UpdateWidgetValue(1.0);
        else
            context->UpdateWidgetValue(0.0);
    }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0)
            return; // ignore button releases
       
        const char *name = context->GetStringParam();
        
        if (!strcmp(name, "Folder") ||
            !strcmp(name, "VCA") ||
            !strcmp(name, "TrackSend") ||
            !strcmp(name, "TrackReceive") ||
            !strcmp(name, "MasterTrackFXMenu") ||
            !strcmp(name, "TrackFXMenu"))
            context->GetPage()->GoAssociatedZone(name);
        else
            context->GetSurface()->GetZoneManager()->GoAssociatedZone(name);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class GoFXLayoutZone : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "GoFXLayoutZone"; }
    
    virtual void RequestUpdate(ActionContext *context) override
    {
        if (context->GetSurface()->GetZoneManager()->GetIsAssociatedZoneActive(context->GetStringParam()))
            context->UpdateWidgetValue(1.0);
        else
            context->UpdateWidgetValue(0.0);
    }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0)
            return; // ignore button releases
        
        context->GetSurface()->GetZoneManager()->GoFXLayoutZone(context->GetStringParam(), context->GetZone()->GetSlotIndex());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ClearFocusedFXParam : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "ClearFocusedFXParam"; }
    
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0)
            return; // ignore button releases

        context->GetSurface()->GetZoneManager()->DeclareClearFocusedFXParam();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ClearFocusedFX : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "ClearFocusedFX"; }
    
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0)
            return; // ignore button releases

        context->GetSurface()->GetZoneManager()->DeclareClearFocusedFX();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ClearSelectedTrackFX : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "ClearSelectedTrackFX"; }
    
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0)
            return; // ignore button releases

        context->GetSurface()->GetZoneManager()->DeclareClearSelectedTrackFX();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ClearFXSlot : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "ClearFXSlot"; }
    
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0)
            return; // ignore button releases

        context->GetSurface()->GetZoneManager()->DeclareClearFXSlot(context->GetZone());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Bank : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "Bank"; }

    virtual void RequestUpdate(ActionContext *context) override
    {
        context->UpdateColorValue(0.0);
    }
    
    void Do(ActionContext *context, double value) override
    {
        if (value < 0 && context->GetRangeMinimum() < 0)
            context->GetCSI()->AdjustBank(context->GetPage(), context->GetStringParam(), context->GetIntParam());
        else if (value > 0 && context->GetRangeMinimum() >= 0)
            context->GetCSI()->AdjustBank(context->GetPage(), context->GetStringParam(), context->GetIntParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetShift : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "SetShift"; }

    virtual double GetCurrentNormalizedValue(ActionContext *context) override
    {
        return context->GetSurface()->GetShift();
    }

    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext *context, double value) override
    {
        context->GetSurface()->SetShift(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetOption : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "SetOption"; }

    virtual double GetCurrentNormalizedValue(ActionContext *context) override
    {
        return context->GetSurface()->GetOption();
    }
    
    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext *context, double value) override
    {
        context->GetSurface()->SetOption(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetControl : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "SetControl"; }

    virtual double GetCurrentNormalizedValue(ActionContext *context) override
    {
        return context->GetSurface()->GetControl();
    }

    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext *context, double value) override
    {
        context->GetSurface()->SetControl(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetAlt : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "SetAlt"; }

    virtual double GetCurrentNormalizedValue(ActionContext *context) override
    {
        return context->GetSurface()->GetAlt();
    }

    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext *context, double value) override
    {
        context->GetSurface()->SetAlt(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetFlip : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "SetFlip"; }

    virtual double GetCurrentNormalizedValue(ActionContext *context) override
    {
        return context->GetSurface()->GetFlip();
    }

    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext *context, double value) override
    {
        context->GetSurface()->SetFlip(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetGlobal : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "SetGlobal"; }

    virtual double GetCurrentNormalizedValue(ActionContext *context) override
    {
        return context->GetSurface()->GetGlobal();
    }

    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext *context, double value) override
    {
        context->GetSurface()->SetGlobal(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetMarker : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "SetMarker"; }

    virtual double GetCurrentNormalizedValue(ActionContext *context) override
    {
        return context->GetSurface()->GetMarker();
    }

    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext *context, double value) override
    {
        context->GetSurface()->SetMarker(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetNudge : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "SetNudge"; }

    virtual double GetCurrentNormalizedValue(ActionContext *context) override
    {
        return context->GetSurface()->GetNudge();
    }

    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext *context, double value) override
    {
        context->GetSurface()->SetNudge(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetZoom : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "SetZoom"; }

    virtual double GetCurrentNormalizedValue(ActionContext *context) override
    {
        return context->GetSurface()->GetZoom();
    }

    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext *context, double value) override
    {
        context->GetSurface()->SetZoom(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetScrub : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "SetScrub"; }

    virtual double GetCurrentNormalizedValue(ActionContext *context) override
    {
        return context->GetSurface()->GetScrub();
    }

    void RequestUpdate(ActionContext *context) override
    {
        context->UpdateWidgetValue(GetCurrentNormalizedValue(context));
    }
    
    void Do(ActionContext *context, double value) override
    {
        context->GetSurface()->SetScrub(value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ClearModifier : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "ClearModifier"; }
   
    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases

        context->GetSurface()->ClearModifier(context->GetStringParam());
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ClearModifiers : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "ClearModifiers"; }
   
    void Do(ActionContext *context, double value) override
    {
        context->GetSurface()->ClearModifiers();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SetToggleChannel : public Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual const char *GetName() override { return "SetToggleChannel"; }
     
    virtual void RequestUpdate(ActionContext *context) override
    {
        context->UpdateColorValue(0.0);
    }

    void Do(ActionContext *context, double value) override
    {
        if (value == 0.0) return; // ignore button releases
        
        context->GetSurface()->ToggleChannel(context->GetWidget()->GetChannelNumber());
    }
};

#endif /* control_surface_manager_actions_h */
