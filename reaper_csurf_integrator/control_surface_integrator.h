//
//  control_surface_integrator.h
//  reaper_control_surface_integrator
//
//

//  Note for Windows environments:
//  use std::byte for C++17 byte
//  use ::byte for Windows byte

#ifndef control_surface_integrator
#define control_surface_integrator

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include "time.h"
#include <sstream>
#include <vector>
#include <map>
#include <iomanip>
#include <fstream>
#include <regex>
#include <cmath>
#include <memory>
#include <filesystem>

#ifdef _WIN32
#include "oscpkt.hh"
#include "udp.hh"
#include <bitset>
#endif

#include "control_surface_integrator_Reaper.h"

#ifdef _WIN32
#include <functional>
#include "commctrl.h"
#else
#include "oscpkt.hh"
#include "udp.hh"
#endif

extern string GetLineEnding();

extern REAPER_PLUGIN_HINSTANCE g_hInst;

const string ControlSurfaceIntegrator = "ControlSurfaceIntegrator";

const string PageToken = "Page";
const string FollowMCPToken = "FollowMCP";
const string SynchPagesToken = "SynchPages";
const string UseScrollLinkToken = "UseScrollLink";
const string MidiSurfaceToken = "MidiSurface";
const string OSCSurfaceToken = "OSCSurface";

const string ShiftToken = "Shift";
const string OptionToken = "Option";
const string ControlToken = "Control";
const string AltToken = "Alt";
const string FlipToken = "Flip";

const string BadFileChars = "[ \\:*?<>|.,()/]";
const string CRLFChars = "[\r\n]";
const string TabChars = "[\t]";

const int TempDisplayTime = 1250;

class Manager;
extern Manager* TheManager;

static vector<string> GetTokens(string line)
{
    vector<string> tokens;
    
    istringstream iss(line);
    string token;
    while (iss >> quoted(token))
        tokens.push_back(token);
    
    return tokens;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CSurfIntegrator;
class Page;
class ControlSurface;
class Midi_ControlSurface;
class OSC_ControlSurface;
class Widget;
class TrackNavigationManager;
class ZoneNavigationManager;
class FeedbackProcessor;
class Zone;
class ZoneManager;
class ActionContext;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual ~Action() {}
    
    virtual string GetName() { return "Action"; }

    virtual void Touch(ActionContext* context, double value) {}
    virtual void RequestUpdate(ActionContext* context) {}
    virtual void Do(ActionContext* context, double value) {}
    virtual double GetCurrentNormalizedValue(ActionContext* context) { return 0.0; }
    virtual double GetCurrentDBValue(ActionContext* context) { return 0.0; }

    int GetPanMode(MediaTrack* track)
    {
        double pan1, pan2 = 0.0;
        int panMode = 0;
        DAW::GetTrackUIPan(track, &pan1, &pan2, &panMode);
        return panMode;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    Page* const page_ = nullptr;
    bool isVolumeTouched_ = false;
    bool isPanTouched_ = false;
    bool isPanWidthTouched_ = false;
    bool isPanLeftTouched_ = false;
    bool isPanRightTouched_ = false;
    bool isMCUTrackPanWidth_ = false;

public:
    Navigator(Page*  page) : page_(page) {}
    virtual ~Navigator() {}
    
    void SetIsVolumeTouched(bool isVolumeTouched) { isVolumeTouched_ = isVolumeTouched;  }
    bool GetIsVolumeTouched() { return isVolumeTouched_;  }
    
    void SetIsPanTouched(bool isPanTouched) { isPanTouched_ = isPanTouched; }
    bool GetIsPanTouched() { return isPanTouched_;  }
    
    void SetIsPanWidthTouched(bool isPanWidthTouched) { isPanWidthTouched_ = isPanWidthTouched; }
    bool GetIsPanWidthTouched() { return isPanWidthTouched_;  }
    
    void SetIsPanLeftTouched(bool isPanLeftTouched) { isPanLeftTouched_ = isPanLeftTouched; }
    bool GetIsPanLeftTouched() { return isPanLeftTouched_;  }
    
    void SetIsPanRightTouched(bool isPanRightTouched) { isPanRightTouched_ = isPanRightTouched; }
    bool GetIsPanRightTouched() { return isPanRightTouched_;  }
    
    void ToggleIsMCUTrackPanWidth() { isMCUTrackPanWidth_ = ! isMCUTrackPanWidth_; }
    bool GetIsMCUTrackPanWidth() { return isMCUTrackPanWidth_;  }
    
    virtual string GetName() { return "Navigator"; }
    virtual MediaTrack* GetTrack() { return nullptr; }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackNavigator : public Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    int const channelNum_ = 0;
    
protected:
    TrackNavigationManager* const manager_;

public:
    TrackNavigator(Page*  page, TrackNavigationManager* manager, int channelNum) : Navigator(page), manager_(manager), channelNum_(channelNum) {}
    virtual ~TrackNavigator() {}
    
    virtual string GetName() override { return "TrackNavigator"; }
   
    virtual MediaTrack* GetTrack() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MasterTrackNavigator : public Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    MasterTrackNavigator(Page*  page) : Navigator(page) {}
    virtual ~MasterTrackNavigator() {}
    
    virtual string GetName() override { return "MasterTrackNavigator"; }
    
    virtual MediaTrack* GetTrack() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SelectedTrackNavigator : public Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    SelectedTrackNavigator(Page*  page) : Navigator(page) {}
    virtual ~SelectedTrackNavigator() {}
    
    virtual string GetName() override { return "SelectedTrackNavigator"; }
    
    virtual MediaTrack* GetTrack() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FocusedFXNavigator : public Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    FocusedFXNavigator(Page*  page) : Navigator(page) {}
    virtual ~FocusedFXNavigator() {}
    
    virtual string GetName() override { return "FocusedFXNavigator"; }
    
    virtual MediaTrack* GetTrack() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ActionContext
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    Action* const action_ = nullptr;
    Widget* const widget_ = nullptr;
    shared_ptr<Zone> const zone_ = nullptr;
    
    string lastStringValue_ = "";
    
    int intParam_ = 0;
    
    string stringParam_ = "";
    
    int paramIndex_ = 0;
    
    string fxParamDisplayName_ = "";
    
    int commandId_ = 0;
    
    double rangeMinimum_ = 0.0;
    double rangeMaximum_ = 1.0;
    
    vector<double> steppedValues_;
    int steppedValuesIndex_ = 0;
    
    double deltaValue_ = 0.0;
    vector<double> acceleratedDeltaValues_;
    vector<int> acceleratedTickValues_;
    int accumulatedIncTicks_ = 0;
    int accumulatedDecTicks_ = 0;
    
    bool isFeedbackInverted_ = false;
    double holdDelayAmount_ = 0.0;
    double delayStartTime_ = 0.0;
    double deferredValue_ = 0.0;
    
    bool shouldUseDisplayStyle_ = false;
    int displayStyle_ = 0;
    
    bool supportsRGB_ = false;
    vector<rgb_color> RGBValues_;
    int currentRGBIndex_ = 0;
    
    bool supportsTrackColor_ = false;
        
    bool noFeedback_ = false;
    
    vector<vector<string>> properties_;
    
    vector<string> zoneNames_;

public:
    ActionContext(Action* action, Widget* widget, shared_ptr<Zone> zone, vector<string> params, vector<vector<string>> properties);

    virtual ~ActionContext() {}
    
    Widget* GetWidget() { return widget_; }
    shared_ptr<Zone> GetZone() { return zone_; }
    int GetSlotIndex();
    string GetName();

    vector<string> &GetZoneNames() { return  zoneNames_; }

    int GetIntParam() { return intParam_; }
    string GetStringParam() { return stringParam_; }
    int GetCommandId() { return commandId_; }
    bool GetShouldUseDisplayStyle() { return shouldUseDisplayStyle_; }
    int GetDisplayStyle() { return displayStyle_; }
    
    MediaTrack* GetTrack();
    
    void DoRangeBoundAction(double value);
    void DoSteppedValueAction(double value);
    void DoAcceleratedSteppedValueAction(int accelerationIndex, double value);
    void DoAcceleratedDeltaValueAction(int accelerationIndex, double value);
    
    Page* GetPage();
    ControlSurface* GetSurface();
    int GetParamIndex() { return paramIndex_; }
    
    bool GetSupportsRGB() { return supportsRGB_; }
    
    void SetIsFeedbackInverted() { isFeedbackInverted_ = true; }
    void SetHoldDelayAmount(double holdDelayAmount) { holdDelayAmount_ = holdDelayAmount * 1000.0; } // holdDelayAmount is specified in seconds, holdDelayAmount_ is in milliseconds
    
    void DoAction(double value);
    void DoRelativeAction(double value);
    void DoRelativeAction(int accelerationIndex, double value);
    
    void RequestUpdate();
    void RunDeferredActions();
    void ClearWidget();
    void UpdateWidgetValue(double value);
    void UpdateWidgetValue(int param, double value);
    void UpdateWidgetValue(string value);
    
    void DoTouch(double value)
    {
        action_->Touch(this, value);
    }

    string GetFxParamDisplayName()
    {
        if(fxParamDisplayName_ != "")
            return fxParamDisplayName_;
        else if(MediaTrack* track = GetTrack())
        {
            char fxParamName[BUFSZ];
            DAW::TrackFX_GetParamName(track, GetSlotIndex(), paramIndex_, fxParamName, sizeof(fxParamName));
            return fxParamName;
        }
        
        return "";
    }

    void SetCurrentRGB(rgb_color newColor)
    {
        supportsRGB_ = true;
        RGBValues_[currentRGBIndex_] = newColor;
    }
    
    rgb_color GetCurrentRGB()
    {
        rgb_color blankColor;
        
        if(RGBValues_.size() > 0 && currentRGBIndex_ < RGBValues_.size())
            return RGBValues_[currentRGBIndex_];
        else return blankColor;
    }
    
    void SetSteppedValueIndex(double value)
    {
        int index = 0;
        double delta = 100000000.0;
        
        for(int i = 0; i < steppedValues_.size(); i++)
            if(abs(steppedValues_[i] - value) < delta)
            {
                delta = abs(steppedValues_[i] - value);
                index = i;
            }
        
        steppedValuesIndex_ = index;
    }

    string GetPanValueString(double panVal)
    {
        bool left = false;
        
        if(panVal < 0)
        {
            left = true;
            panVal = -panVal;
        }
        
        int panIntVal = int(panVal * 100.0);
        string trackPanValueString = "";
        
        if(left)
        {
            if(panIntVal == 100)
                trackPanValueString += "<";
            else if(panIntVal < 100 && panIntVal > 9)
                trackPanValueString += "< ";
            else
                trackPanValueString += "<  ";
            
            trackPanValueString += to_string(panIntVal);
        }
        else
        {
            trackPanValueString += "   ";
            
            trackPanValueString += to_string(panIntVal);
            
            if(panIntVal == 100)
                trackPanValueString += ">";
            else if(panIntVal < 100 && panIntVal > 9)
                trackPanValueString += " >";
            else
                trackPanValueString += "  >";
        }
        
        if(panIntVal == 0)
            trackPanValueString = "  <C>  ";

        return trackPanValueString;
    }
    
    string GetPanWidthValueString(double widthVal)
    {
        bool reversed = false;
        
        if(widthVal < 0)
        {
            reversed = true;
            widthVal = -widthVal;
        }
        
        int widthIntVal = int(widthVal * 100.0);
        string trackPanWidthString = "";
        
        if(reversed)
            trackPanWidthString += "Rev ";
        
        trackPanWidthString += to_string(widthIntVal);
        
        if(widthIntVal == 0)
            trackPanWidthString = " <Mno> ";

        return trackPanWidthString;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Zone
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    ZoneManager* const zoneManager_ = nullptr;
    Navigator* const navigator_= nullptr;
    int slotIndex_ = 0;
    string const name_ = "";
    string const alias_ = "";
    string const sourceFilePath_ = "";
    
    bool isActive_ = false;
    map<string, string> touchIds_;
    map<string, bool> activeTouchIds_;
    
    map<Widget*, bool> widgets_;
    
    vector<shared_ptr<Zone>> includedZones_;
    map<string, vector<shared_ptr<Zone>>> subZones_;
    map<string, vector<shared_ptr<Zone>>> associatedZones_;
    
    map<Widget*, map<string, vector<shared_ptr<ActionContext>>>> actionContextDictionary_;
    vector<shared_ptr<ActionContext>> defaultContexts_;
    
    void AddNavigatorsForZone(string zoneName, vector<Navigator*> &navigators);
    
public:
    Zone(ZoneManager* const zoneManager, Navigator* navigator, int slotIndex, map<string, string> touchIds, string name, string alias, string sourceFilePath, vector<string> includedZones, vector<string> associatedZones);

    void InitSubZones(vector<string> subZones, shared_ptr<Zone> enclosingZone);
    
    virtual ~Zone() {}
    
    void GoAssociatedZone(string associatedZoneName);

    Navigator* GetNavigator() { return navigator_; }
    void SetSlotIndex(int index) { slotIndex_ = index; }
    int GetSlotIndex();
    
    vector<shared_ptr<ActionContext>> &GetActionContexts(Widget* widget);
        
    void RequestUpdate(map<Widget*, bool> &usedWidgets);
    void RequestUpdateWidget(Widget* widget);
    void Activate();
    void Deactivate();
    void OnTrackDeselection();
    void DoAction(Widget* widget, bool &isUsed, double value);
    void DoRelativeAction(Widget* widget, bool &isUsed, double delta);
    void DoRelativeAction(Widget* widget, bool &isUsed, int accelerationIndex, double delta);
    void DoTouch(Widget* widget, string widgetName, bool &isUsed, double value);
    map<Widget*, bool> &GetWidgets() { return widgets_; }
    bool GetIsActive() { return isActive_; }
    
    bool GetIsMainZoneOnlyActive()
    {
        for(auto [key, zones] : associatedZones_)
            for(auto zone : zones)
                if(zone->GetIsActive())
                    return false;
        
        return true;
    }
    
    bool GetIsAssociatedZoneActive(string zoneName)
    {
        if(associatedZones_.count(zoneName) > 0)
            for(auto zone : associatedZones_[zoneName])
                if(zone->GetIsActive())
                    return true;
        
        return false;
    }
    
    void Toggle()
    {
        if(isActive_)
            Deactivate();
        else
            Activate();
    }

    string GetName()
    {
        return name_;
    }
    
    string GetNameOrAlias()
    {
        if(alias_ != "")
            return alias_;
        else
            return name_;
    }
    
    void AddWidget(Widget* widget)
    {
        widgets_[widget] = true;
    }
    
    void AddActionContext(Widget* widget, string modifier, shared_ptr<ActionContext> actionContext)
    {
        actionContextDictionary_[widget][modifier].push_back(actionContext);
    }
    
    virtual void GoSubZone(string subZoneName)
    {
        if(subZones_.count(subZoneName) > 0)
            for(auto zone : subZones_[subZoneName])
                zone->Activate();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SubZone : public Zone
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    shared_ptr<Zone> const enclosingZone_ = nullptr;
    
public:
    SubZone(ZoneManager* const zoneManager, Navigator* navigator, int slotIndex, map<string, string> touchIds, string name, string alias, string sourceFilePath, vector<string> includedZones, vector<string> associatedZones, shared_ptr<Zone> enclosingZone) : Zone(zoneManager, navigator, slotIndex, touchIds, name, alias, sourceFilePath, includedZones, associatedZones), enclosingZone_(enclosingZone) {}

    virtual ~SubZone() {}
    
    virtual void GoSubZone(string subZoneName) override
    {
        Deactivate();
        enclosingZone_->GoSubZone(subZoneName);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Widget
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    ControlSurface* const surface_;
    string const name_;
    vector<FeedbackProcessor*> feedbackProcessors_;
    
    bool isModifier_ = false;
    
public:
    Widget(ControlSurface* surface, string name) : surface_(surface), name_(name) {}
    ~Widget();
    
    string GetName() { return name_; }
    ControlSurface* GetSurface() { return surface_; }
    ZoneManager* GetZoneManager();
    
    bool GetIsModifier() { return isModifier_; }
    void SetIsModifier() { isModifier_ = true; }

    void SetProperties(vector<vector<string>> properties);
    void UpdateValue(double value);
    void UpdateValue(int mode, double value);
    void UpdateValue(string value);
    void UpdateRGBValue(int r, int g, int b);
    void Clear();
    void LogInput(double value);
    
    void AddFeedbackProcessor(FeedbackProcessor* feedbackProcessor)
    {
        feedbackProcessors_.push_back(feedbackProcessor);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct CSIZoneInfo
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    string filePath = "";
    string alias = "";
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ZoneManager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    map<string, string> broadcast_;
    map<string, string> receive_;

    ControlSurface* const surface_;
    string const zoneFolder_ = "";
            
    map<string, CSIZoneInfo> zoneFilePaths_;
    
    map<Widget*, bool> usedWidgets_;

    shared_ptr<Zone> homeZone_ = nullptr;
    shared_ptr<Zone> focusedFXParamZone_ = nullptr;
    bool isFocusedFXParamMappingEnabled_ = false;

    map<int, map<int, int>> focusedFXDictionary_;
    vector<shared_ptr<Zone>> focusedFXZones_;
    bool isFocusedFXMappingEnabled_ = true;
    bool isFocusedFXMappingLockedOut_ = false;
    
    vector<shared_ptr<Zone>> selectedTrackFXZones_;
    vector<shared_ptr<Zone>> fxSlotZones_;

    int trackSendOffset_ = 0;
    int trackReceiveOffset_ = 0;
    int trackFXMenuOffset_ = 0;
    int selectedTrackOffset_ = 0;
    int selectedTrackSendOffset_ = 0;
    int selectedTrackReceiveOffset_ = 0;
    int selectedTrackFXMenuOffset_ = 0;

    void ResetOffsets()
    {
        trackSendOffset_ = 0;
        trackReceiveOffset_ = 0;
        trackFXMenuOffset_ = 0;
        selectedTrackOffset_ = 0;
        selectedTrackSendOffset_ = 0;
        selectedTrackReceiveOffset_ = 0;
        selectedTrackFXMenuOffset_ = 0;
    }
    
    void ResetSelectedTrackOffsets()
    {
        selectedTrackOffset_ = 0;
        selectedTrackSendOffset_ = 0;
        selectedTrackReceiveOffset_ = 0;
        selectedTrackFXMenuOffset_ = 0;
    }
    
    void LockoutFocusedFXMapping()
    {
        focusedFXZones_.clear();
        isFocusedFXMappingLockedOut_ = true;
    }
       
public:
    ZoneManager(ControlSurface* surface, string zoneFolder) : surface_(surface), zoneFolder_(zoneFolder) { }

    void Initialize();

    void RequestUpdate();
    
    void PreProcessZones();
    
    Navigator* GetMasterTrackNavigator();
    Navigator* GetSelectedTrackNavigator();
    Navigator* GetFocusedFXNavigator();
    Navigator* GetDefaultNavigator();
    
    int GetNumChannels();
    void GoHome();
    void OnTrackSelection();
    void OnTrackDeselection();
    void GoFocusedFX();
    void GoSelectedTrackFX();
    void GoTrackFXSlot(MediaTrack* track, Navigator* navigator, int fxSlot);
    void GoAssociatedZone(string zoneName);
    void HandleActivation(string zoneName);
    void AdjustTrackSendBank(int amount);
    void AdjustTrackReceiveBank(int amount);
    void AdjustTrackFXMenuBank(int amount);
    
    map<string, CSIZoneInfo> &GetZoneFilePaths() { return zoneFilePaths_; }
    
    ControlSurface* GetSurface() { return surface_; }   
    
    void SetHomeZone(shared_ptr<Zone> zone) { homeZone_ = zone; }
    void SetFocusedFXParamZone(shared_ptr<Zone> zone) { focusedFXParamZone_ = zone; }

    int GetTrackSendOffset() { return trackSendOffset_; }
    int GetTrackReceiveOffset() { return trackReceiveOffset_; }
    int GetTrackFXMenuOffset() { return trackFXMenuOffset_; }
    int GetSelectedTrackOffset() { return selectedTrackOffset_; }
    int GetSelectedTrackSendOffset() { return selectedTrackSendOffset_; }
    int GetSelectedTrackReceiveOffset() { return selectedTrackReceiveOffset_; }
    int GetSelectedTrackFXMenuOffset() { return selectedTrackFXMenuOffset_; }
    
    bool GetIsFocusedFXMappingEnabled() { return isFocusedFXMappingEnabled_; }
    void ToggleEnableFocusedFXMapping() { isFocusedFXMappingEnabled_ = ! isFocusedFXMappingEnabled_; }
    
    bool GetIsFocusedFXParamMappingEnabled() { return isFocusedFXParamMappingEnabled_; }
    
    void ToggleEnableFocusedFXParamMapping()
    {
        isFocusedFXParamMappingEnabled_ = ! isFocusedFXParamMappingEnabled_;
        
        if(focusedFXParamZone_ != nullptr)
        {
            if(isFocusedFXParamMappingEnabled_)
                focusedFXParamZone_->Activate();
            else
                focusedFXParamZone_->Deactivate();
        }
    }

    bool GetIsHomeZoneOnlyActive()
    {
        if(homeZone_ !=  nullptr)
            return homeZone_->GetIsMainZoneOnlyActive();
        else
            return false;
    }
    
    bool GetIsAssociatedZoneActive(string zoneName)
    {
        if(homeZone_ !=  nullptr)
            return homeZone_->GetIsAssociatedZoneActive(zoneName);
        else
            return false;
    }
    
    void ClearFXMapping()
    {
        isFocusedFXMappingLockedOut_ = false;
        focusedFXZones_.clear();
        selectedTrackFXZones_.clear();
        fxSlotZones_.clear();
    }
    
    void HandleTrackSendBank(int amount)
    {
        if(receive_.count("TrackSend") > 0)
            AdjustTrackSendOffset(amount);
    }

    void HandleTrackReceiveBank(int amount)
    {
        if(receive_.count("TrackReceive") > 0)
            AdjustTrackReceiveOffset(amount);
    }

    void HandleTrackFXMenuBank(int amount)
    {
        if(receive_.count("TTrackFXMenu") > 0)
            AdjustTrackFXMenuOffset(amount);
    }
    
    void AdjustTrackSendOffset(int amount)
    {
        // GAW TBD -- calc max and clamp
        
        trackSendOffset_ += amount;
        
        if(trackSendOffset_ < 0)
            trackSendOffset_ = 0;
    }

    void AdjustTrackReceiveOffset(int amount)
    {
        // GAW TBD -- calc max and clamp
        
        trackReceiveOffset_ += amount;
        
        if(trackReceiveOffset_ < 0)
            trackReceiveOffset_ = 0;
    }
    
    void AdjustTrackFXMenuOffset(int amount)
    {
        // GAW TBD -- calc max and clamp
        
        trackFXMenuOffset_ += amount;
        
        if(trackFXMenuOffset_ < 0)
            trackFXMenuOffset_ = 0;
    }
    
    void AdjustSelectedTrackBank(int amount)
    {
        // GAW TBD -- calc max and clamp
        
        selectedTrackOffset_ += amount;
        
        if(selectedTrackOffset_ < 0)
            selectedTrackOffset_ = 0;
    }
    
    void AdjustSelectedTrackSendBank(int amount)
    {
        // GAW TBD -- calc max and clamp
        
        selectedTrackSendOffset_ += amount;
        
        if(selectedTrackSendOffset_ < 0)
            selectedTrackSendOffset_ = 0;
    }
    
    void AdjustSelectedTrackReceiveBank(int amount)
    {
        // GAW TBD -- calc max and clamp
        
        selectedTrackReceiveOffset_ += amount;
        
        if(selectedTrackReceiveOffset_ < 0)
            selectedTrackReceiveOffset_ = 0;
    }
    
    void AdjustSelectedTrackFXMenuBank(int amount)
    {
        // GAW TBD -- calc max and clamp
        
        selectedTrackFXMenuOffset_ += amount;
        
        if(selectedTrackFXMenuOffset_ < 0)
            selectedTrackFXMenuOffset_ = 0;
    }
        
    void AddWidget(Widget* widget)
    {
        usedWidgets_[widget] = false;
    }

    void SetBroadcast(ActionContext* context)
    {
        for(string param : context->GetZoneNames())
            broadcast_[param] = param;
    }

    void SetReceive(ActionContext* context)
    {
        for(string param : context->GetZoneNames())
            receive_[param] = param;
    }

    string GetNameOrAlias(string name)
    {
        if(zoneFilePaths_.count(name) > 0)
            return zoneFilePaths_[name].alias;
        else
            return "No Map";
    }
    
    void AddZoneFilePath(string name, struct CSIZoneInfo info)
    {
        if(name != "")
            zoneFilePaths_[name] = info;
    }
    
    void CheckFocusedFXState()
    {
        if(isFocusedFXMappingLockedOut_ || ! isFocusedFXMappingEnabled_)
            return;
        
        int trackNumber = 0;
        int itemNumber = 0;
        int fxIndex = 0;
        
        int retval = DAW::GetFocusedFX2(&trackNumber, &itemNumber, &fxIndex);
        
        if((retval & 1) && (fxIndex > -1))
        {
            int lastRetval = -1;

            if(focusedFXDictionary_.count(trackNumber) > 0 && focusedFXDictionary_[trackNumber].count(fxIndex) > 0)
                lastRetval = focusedFXDictionary_[trackNumber][fxIndex];
            
            if(lastRetval != retval)
            {
                if(retval == 1)
                    GoFocusedFX();
                
                else if(retval & 4)
                    focusedFXZones_.clear();
                
                if(focusedFXDictionary_[trackNumber].count(trackNumber) < 1)
                    focusedFXDictionary_[trackNumber] = map<int, int>();
                                   
                focusedFXDictionary_[trackNumber][fxIndex] = retval;;
            }
        }
    }
       
    void DoAction(Widget* widget, double value)
    {
        widget->LogInput(value);
        
        bool isUsed = false;
        
        if(focusedFXParamZone_ != nullptr && isFocusedFXParamMappingEnabled_)
            focusedFXParamZone_->DoAction(widget, isUsed, value);

        for(auto zone : focusedFXZones_)
            zone->DoAction(widget, isUsed, value);
        
        if(isUsed)
            return;
        
        for(auto zone : selectedTrackFXZones_)
            zone->DoAction(widget, isUsed, value);
        
        if(isUsed)
            return;
   
        for(auto zone : fxSlotZones_)
            zone->DoAction(widget, isUsed, value);
        
        if(isUsed)
            return;

        if(homeZone_ != nullptr)
            homeZone_->DoAction(widget, isUsed, value);
    }
    
    void DoRelativeAction(Widget* widget, double delta)
    {
        widget->LogInput(delta);
        
        bool isUsed = false;
        
        if(focusedFXParamZone_ != nullptr && isFocusedFXParamMappingEnabled_)
            focusedFXParamZone_->DoRelativeAction(widget, isUsed, delta);

        for(auto zone : focusedFXZones_)
            zone->DoRelativeAction(widget, isUsed, delta);
        
        if(isUsed)
            return;
        
        for(auto zone : selectedTrackFXZones_)
            zone->DoRelativeAction(widget, isUsed, delta);
        
        if(isUsed)
            return;

        for(auto zone : fxSlotZones_)
            zone->DoRelativeAction(widget, isUsed, delta);
        
        if(isUsed)
            return;

        if(homeZone_ != nullptr)
            homeZone_->DoRelativeAction(widget, isUsed, delta);
    }
    
    void DoRelativeAction(Widget* widget, int accelerationIndex, double delta)
    {
        widget->LogInput(delta);
        
        bool isUsed = false;
           
        if(focusedFXParamZone_ != nullptr && isFocusedFXParamMappingEnabled_)
            focusedFXParamZone_->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
        
        for(auto zone : focusedFXZones_)
            zone->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
        
        if(isUsed)
            return;
        
        for(auto zone : selectedTrackFXZones_)
            zone->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
        
        if(isUsed)
            return;

        for(auto zone : fxSlotZones_)
            zone->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
        
        if(isUsed)
            return;

        if(homeZone_ != nullptr)
            homeZone_->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
    }
    
    void DoTouch(Widget* widget, double value)
    {
        widget->LogInput(value);
        
        bool isUsed = false;
        
        if(focusedFXParamZone_ != nullptr && isFocusedFXParamMappingEnabled_)
            focusedFXParamZone_->DoTouch(widget, widget->GetName(), isUsed, value);
        
        for(auto zone : focusedFXZones_)
            zone->DoTouch(widget, widget->GetName(), isUsed, value);
        
        if(isUsed)
            return;

        for(auto zone : selectedTrackFXZones_)
            zone->DoTouch(widget, widget->GetName(), isUsed, value);
        
        if(isUsed)
            return;

        for(auto zone : fxSlotZones_)
            zone->DoTouch(widget, widget->GetName(), isUsed, value);
        
        if(isUsed)
            return;

        if(homeZone_ != nullptr)
            homeZone_->DoTouch(widget, widget->GetName(), isUsed, value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    Widget* const widget_;
    CSIMessageGenerator(Widget* widget) : widget_(widget) {}
    
public:
    CSIMessageGenerator(Widget* widget, string message);
    virtual ~CSIMessageGenerator() {}
    
    virtual void ProcessMessage(double value)
    {
        widget_->GetZoneManager()->DoAction(widget_, value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Touch_CSIMessageGenerator : CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    Touch_CSIMessageGenerator(Widget* widget, string message) : CSIMessageGenerator(widget, message) {}
    virtual ~Touch_CSIMessageGenerator() {}
    
    virtual void ProcessMessage(double value) override
    {
        widget_->GetZoneManager()->DoTouch(widget_, value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ControlSurface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{    
private:
    int* scrubModePtr = nullptr;
    int configScrubMode = 0;

    bool isRewinding_ = false;
    bool isFastForwarding_ = false;

protected:
    ControlSurface(Page* page, const string name, string zoneFolder, int numChannels, int channelOffset) : page_(page), name_(name), numChannels_(numChannels), channelOffset_(channelOffset), zoneManager_(new ZoneManager(this, zoneFolder))
    {
        int size = 0;
        scrubModePtr = (int*)get_config_var("scrubmode", &size);
    }
    
    Page* const page_;
    string const name_;
    ZoneManager* const zoneManager_;
    
    int const numChannels_ = 0;
    int const channelOffset_ = 0;
    
    vector<Widget*> widgets_;
    map<string, Widget*> widgetsByName_;
    
    map<string, CSIMessageGenerator*> CSIMessageGeneratorsByMessage_;
    
    virtual void InitHardwiredWidgets()
    {
        // Add the "hardwired" widgets
        AddWidget(new Widget(this, "OnTrackSelection"));
        AddWidget(new Widget(this, "OnPageEnter"));
        AddWidget(new Widget(this, "OnPageLeave"));
        AddWidget(new Widget(this, "OnInitialization"));
    }
            
public:
    virtual ~ControlSurface()
    {
        for(auto [key, messageGenerator] : CSIMessageGeneratorsByMessage_)
        {
            delete messageGenerator;
            messageGenerator = nullptr;
        }
        
        for(auto widget : widgets_)
        {
            delete widget;
            widget = nullptr;
        }
    };
    
    void TrackFXListChanged();
    void OnTrackSelection();
    virtual void SetHasMCUMeters(int displayType) {}
    virtual void ActivatingZone(string zoneName) {}
    
    virtual void HandleExternalInput() {}
    virtual void UpdateTimeDisplay() {}
    virtual bool GetIsEuConFXAreaFocused() { return false; }
    virtual void ForceRefreshTimeDisplay() {}
    
    ZoneManager* GetZoneManager() { return zoneManager_; }
    Page* GetPage() { return page_; }
    string GetName() { return name_; }
    
    int GetNumChannels() { return numChannels_; }
    int GetChannelOffset() { return channelOffset_; }
    
    void StartRewinding()
    {
        isRewinding_ = true;
        configScrubMode = *scrubModePtr;
        *scrubModePtr = 2;
    }
    
    void StopRewinding()
    {
        isRewinding_ = false;
        *scrubModePtr = configScrubMode;
    }
    
    void StartFastForwarding()
    {
        isFastForwarding_ = true;
        configScrubMode = *scrubModePtr;
        *scrubModePtr = 2;
    }
    
    void StopFastForwarding()
    {
        isFastForwarding_ = false;
        *scrubModePtr = configScrubMode;
    }

    void RequestUpdate()
    {
        zoneManager_->RequestUpdate();
        
        if(isRewinding_)
            DAW::CSurf_OnRew(1);
        else if(isFastForwarding_)
            DAW::CSurf_OnFwd(1);
    }

    void ClearAllWidgets()
    {
        for(auto widget : widgets_)
            widget->Clear();
    }
       
    void AddWidget(Widget* widget)
    {
        widgets_.push_back(widget);
        widgetsByName_[widget->GetName()] = widget;
        zoneManager_->AddWidget(widget);
    }
    
    void AddCSIMessageGenerator(string message, CSIMessageGenerator* messageGenerator)
    {
        CSIMessageGeneratorsByMessage_[message] = messageGenerator;
    }

    Widget* GetWidgetByName(string name)
    {
        if(widgetsByName_.count(name) > 0)
            return widgetsByName_[name];
        else
            return nullptr;
    }
    
    void OnPageEnter()
    {
        if(widgetsByName_.count("OnPageEnter") > 0)
            zoneManager_->DoAction(widgetsByName_["OnPageEnter"], 1.0);
    }
    
    void OnPageLeave()
    {
        if(widgetsByName_.count("OnPageLeave") > 0)
            zoneManager_->DoAction(widgetsByName_["OnPageLeave"], 1.0);
    }
    
    void OnInitialization()
    {
        if(widgetsByName_.count("OnInitialization") > 0)
            zoneManager_->DoAction(widgetsByName_["OnInitialization"], 1.0);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    double lastDoubleValue_ = 0.0;
    string lastStringValue_ = "";
    int lastRValue_ = 0;
    int lastGValue_ = 0;
    int lastBValue_ = 0;

    Widget* const widget_ = nullptr;
    
public:
    FeedbackProcessor(Widget* widget) : widget_(widget) {}
    virtual ~FeedbackProcessor() {}
    Widget* GetWidget() { return widget_; }
    virtual void SetRGBValue(int r, int g, int b) {}
    virtual void ForceValue() {}
    virtual void ForceValue(double value) {}
    virtual void ForceValue(int param, double value) {}
    virtual void ForceRGBValue(int r, int g, int b) {}
    virtual void ForceValue(string value) {}
    virtual void SetColors(rgb_color textColor, rgb_color textBackground) {}
    virtual void SetCurrentColor(double value) {}
    virtual void SetProperties(vector<vector<string>> properties) {}

    virtual int GetMaxCharacters() { return 0; }
    
    virtual void SetValue(double value)
    {
        if(lastDoubleValue_ != value)
            ForceValue(value);
    }
    
    virtual void SetValue(int param, double value)
    {
        if(lastDoubleValue_ != value)
            ForceValue(value);
    }
    
    virtual void SetValue(string value)
    {
        if(lastStringValue_ != value)
            ForceValue(value);
    }

    virtual void ClearCache()
    {
        lastDoubleValue_ = 0.0;
        lastStringValue_ = "";
    }
    
    void Clear()
    {
        SetValue(0.0);
        SetValue(0, 0.0);
        SetValue("");
        SetRGBValue(0, 0, 0);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Midi_CSIMessageGenerator : public CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    Midi_CSIMessageGenerator(Widget* widget) : CSIMessageGenerator(widget) {}
    
public:
    virtual ~Midi_CSIMessageGenerator() {}
    virtual void ProcessMidiMessage(const MIDI_event_ex_t* midiMessage) {}
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Midi_FeedbackProcessor : public FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    Midi_ControlSurface* const surface_ = nullptr;
    
    MIDI_event_ex_t* lastMessageSent_ = new MIDI_event_ex_t(0, 0, 0);
    MIDI_event_ex_t* midiFeedbackMessage1_ = new MIDI_event_ex_t(0, 0, 0);
    MIDI_event_ex_t* midiFeedbackMessage2_ = new MIDI_event_ex_t(0, 0, 0);
    
    Midi_FeedbackProcessor(Midi_ControlSurface* surface, Widget* widget) : FeedbackProcessor(widget), surface_(surface) {}
    Midi_FeedbackProcessor(Midi_ControlSurface* surface, Widget* widget, MIDI_event_ex_t* feedback1) : FeedbackProcessor(widget), surface_(surface), midiFeedbackMessage1_(feedback1) {}
    Midi_FeedbackProcessor(Midi_ControlSurface* surface, Widget* widget, MIDI_event_ex_t* feedback1, MIDI_event_ex_t* feedback2) : FeedbackProcessor(widget), surface_(surface), midiFeedbackMessage1_(feedback1), midiFeedbackMessage2_(feedback2) {}
    
    void SendMidiMessage(MIDI_event_ex_t* midiMessage);
    void SendMidiMessage(int first, int second, int third);
    void ForceMidiMessage(int first, int second, int third);

public:
    virtual void ClearCache() override
    {
        lastMessageSent_->midi_message[0] = 0;
        lastMessageSent_->midi_message[1] = 0;
        lastMessageSent_->midi_message[2] = 0;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Midi_ControlSurface : public ControlSurface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    string templateFilename_ = "";
    midi_Input* midiInput_ = nullptr;
    midi_Output* midiOutput_ = nullptr;
    map<int, vector<Midi_CSIMessageGenerator*>> Midi_CSIMessageGeneratorsByMessage_;
    
    // special processing for MCU meters
    bool hasMCUMeters_ = false;
    int displayType_ = 0x14;
    
    void ProcessMidiMessage(const MIDI_event_ex_t* evt);
   
    void Initialize(string templateFilename, string zoneFolder);

    void InitializeMCU();
    void InitializeMCUXT();
    
    virtual void InitializeMeters()
    {
        if(hasMCUMeters_)
        {
            if(displayType_ == 0x14)
                InitializeMCU();
            else
                InitializeMCUXT();
        }
    }

public:
    Midi_ControlSurface(Page* page, const string name, string templateFilename, string zoneFolder, int numChannels, int channelOffset, midi_Input* midiInput, midi_Output* midiOutput)
    : ControlSurface(page, name, zoneFolder, numChannels, channelOffset), templateFilename_(templateFilename), midiInput_(midiInput), midiOutput_(midiOutput)
    {
        Initialize(templateFilename, zoneFolder);
    }
    
    virtual ~Midi_ControlSurface() {}
    
    void SendMidiMessage(MIDI_event_ex_t* midiMessage);
    void SendMidiMessage(int first, int second, int third);

    virtual void SetHasMCUMeters(int displayType) override
    {
        hasMCUMeters_ = true;
        displayType_ = displayType;
    }
    
    virtual void HandleExternalInput() override
    {
        if(midiInput_)
        {
            DAW::SwapBufsPrecise(midiInput_);
            MIDI_eventlist* list = midiInput_->GetReadBuf();
            int bpos = 0;
            MIDI_event_t* evt;
            while ((evt = list->EnumItems(&bpos)))
                ProcessMidiMessage((MIDI_event_ex_t*)evt);
        }
    }
    
    void AddCSIMessageGenerator(int message, Midi_CSIMessageGenerator* messageGenerator)
    {
        Midi_CSIMessageGeneratorsByMessage_[message].push_back(messageGenerator);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_FeedbackProcessor : public FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    OSC_ControlSurface* const surface_ = nullptr;
    string oscAddress_ = "";
    
public:
    
    OSC_FeedbackProcessor(OSC_ControlSurface* surface, Widget* widget, string oscAddress) : FeedbackProcessor(widget), surface_(surface), oscAddress_(oscAddress) {}
    ~OSC_FeedbackProcessor() {}

    virtual void SetRGBValue(int r, int g, int b) override;
    virtual void ForceValue(double value) override;
    virtual void ForceValue(int param, double value) override;
    virtual void ForceValue(string value) override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_IntFeedbackProcessor : public OSC_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    OSC_IntFeedbackProcessor(OSC_ControlSurface* surface, Widget* widget, string oscAddress) : OSC_FeedbackProcessor(surface, widget, oscAddress) {}
    ~OSC_IntFeedbackProcessor() {}

    virtual void ForceValue(double value) override;
    virtual void ForceValue(int param, double value) override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_ControlSurface : public ControlSurface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    string templateFilename_ = "";
    shared_ptr<oscpkt::UdpSocket> const inSocket_ = nullptr;
    shared_ptr<oscpkt::UdpSocket> const outSocket_ = nullptr;
    oscpkt::PacketReader packetReader_;
    oscpkt::PacketWriter packetWriter_;
    
    void Initialize(string templateFilename, string zoneFolder);
    void ProcessOSCMessage(string message, double value);

public:
    OSC_ControlSurface(Page* page, const string name, string templateFilename, string zoneFolder, int numChannels, int channelOffset, shared_ptr<oscpkt::UdpSocket> inSocket, shared_ptr<oscpkt::UdpSocket> outSocket)
    : ControlSurface(page, name, zoneFolder, numChannels, channelOffset), templateFilename_(templateFilename), inSocket_(inSocket), outSocket_(outSocket)
    {
        Initialize(templateFilename, zoneFolder);
    }
    
    virtual ~OSC_ControlSurface() {}
    
    virtual void ActivatingZone(string zoneName) override;
    void SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, double value);
    void SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, int value);
    void SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, string value);
    
    virtual void HandleExternalInput() override
    {
        if(inSocket_ != nullptr && inSocket_->isOk())
        {
            while (inSocket_->receiveNextPacket(0))  // timeout, in ms
            {
                packetReader_.init(inSocket_->packetData(), inSocket_->packetSize());
                oscpkt::Message *message;
                
                while (packetReader_.isOk() && (message = packetReader_.popMessage()) != 0)
                {
                    float value = 0;
                    
                    if(message->arg().isFloat())
                    {
                        message->arg().popFloat(value);
                        ProcessOSCMessage(message->addressPattern(), value);
                    }
                }
            }
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackNavigationManager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    Page* const page_ = nullptr;
    bool followMCP_ = true;
    bool synchPages_ = false;
    bool isScrollLinkEnabled_ = false;
    bool isVCAModeEnabled_ = false;
    bool isFolderModeEnabled_ = false;
    int currentTrackVCAFolderMode_ = 0;
    int targetScrollLinkChannel_ = 0;
    int trackOffset_ = 0;
    int vcaTrackOffset_ = 0;
    int folderTrackOffset_ = 0;
    vector<MediaTrack*> selectedTracks_;
    vector<MediaTrack*> vcaTopLeadTracks_;
    vector<MediaTrack*> vcaLeadTracks_;
    vector<MediaTrack*> vcaSpillTracks_;
    vector<MediaTrack*> folderTracks_;
    map<int, Navigator*> trackNavigators_;
    Navigator* const masterTrackNavigator_ = nullptr;
    Navigator* const selectedTrackNavigator_ = nullptr;
    Navigator* const focusedFXNavigator_ = nullptr;
    Navigator* const defaultNavigator_ = nullptr;
    
    vector<string> autoModeDisplayNames__ = { "Trim", "Read", "Touch", "Write", "Latch", "LtchPre" };
    int autoModeIndex_ = 0;
    
   
public:
    TrackNavigationManager(Page* page, bool followMCP, bool synchPages, bool scrollLink) : page_(page), followMCP_(followMCP), synchPages_(synchPages), isScrollLinkEnabled_(scrollLink),
    masterTrackNavigator_(new MasterTrackNavigator(page_)),
    selectedTrackNavigator_(new SelectedTrackNavigator(page_)),
    focusedFXNavigator_(new FocusedFXNavigator(page_)),
    defaultNavigator_(new Navigator(page_))
    {}
    
    ~TrackNavigationManager()
    {
        for(auto [key, navigator] : trackNavigators_)
        {
            delete navigator;
            navigator = nullptr;
        }
        
        delete masterTrackNavigator_;
        delete selectedTrackNavigator_;
        delete focusedFXNavigator_;
        delete defaultNavigator_;
    }
    
    bool GetSynchPages() { return synchPages_; }
    bool GetScrollLink() { return isScrollLinkEnabled_; }
    bool GetVCAMode() { return isVCAModeEnabled_; }
    int GetCurrentTrackVCAFolderMode() { return currentTrackVCAFolderMode_; }
    int  GetNumTracks() { return DAW::CSurf_NumTracks(followMCP_); }
    Navigator* GetMasterTrackNavigator() { return masterTrackNavigator_; }
    Navigator* GetSelectedTrackNavigator() { return selectedTrackNavigator_; }
    Navigator* GetFocusedFXNavigator() { return focusedFXNavigator_; }
    Navigator* GetDefaultNavigator() { return defaultNavigator_; }
    
    void NextTrackVCAFolderMode()
    {
        currentTrackVCAFolderMode_ += 1;
        
        if(currentTrackVCAFolderMode_ > 2)
            currentTrackVCAFolderMode_ = 0;
        
        if(currentTrackVCAFolderMode_ == 1)
            isVCAModeEnabled_ = true;
        else
            isVCAModeEnabled_ = false;
        
        if(currentTrackVCAFolderMode_ == 2)
            isFolderModeEnabled_ = true;
        else
            isFolderModeEnabled_ = false;
    }
    
    void ResetTrackVCAFolderMode()
    {
        currentTrackVCAFolderMode_ = 0;
        isVCAModeEnabled_ = false;
        isFolderModeEnabled_ = false;
    }
    
    void SetAutoModeIndex()
    {
        if(MediaTrack* track =  GetSelectedTrackNavigator()->GetTrack())
            autoModeIndex_ = DAW::GetMediaTrackInfo_Value(track, "I_AUTOMODE");
    }
    
    void NextAutoMode()
    {
        if(MediaTrack* track = GetSelectedTrackNavigator()->GetTrack())
        {
            if(autoModeIndex_ == 2) // skip over write mode when cycling
                autoModeIndex_ += 2;
            else
                autoModeIndex_++;
            
            if(autoModeIndex_ > autoModeDisplayNames__.size() - 1)
                autoModeIndex_ = 0;
    
            DAW::GetSetMediaTrackInfo(track, "I_AUTOMODE", &autoModeIndex_);
        }
    }
    
    string GetAutoModeDisplayName()
    {
        int globalOverride = DAW::GetGlobalAutomationOverride();

        if(globalOverride > -1) // -1=no override, 0=trim/read, 1=read, 2=touch, 3=write, 4=latch, 5=bypass
            return autoModeDisplayNames__[globalOverride];
        else
            return autoModeDisplayNames__[autoModeIndex_];
    }

    string GetAutoModeDisplayName(int modeIndex)
    {
        return autoModeDisplayNames__[modeIndex];
    }

    vector<MediaTrack*> &GetSelectedTracks()
    {
        selectedTracks_.clear();
        
        for(int i = 0; i < DAW::CountSelectedTracks(); i++)
            selectedTracks_.push_back(DAW::GetSelectedTrack(i));
        
        return selectedTracks_;
    }

    
    void ForceScrollLink()
    {
        // Make sure selected track is visble on the control surface
        MediaTrack* selectedTrack = GetSelectedTrack();
        
        if(selectedTrack != nullptr)
        {
            for(auto  [key, navigator] : trackNavigators_)
                if(selectedTrack == navigator->GetTrack())
                    return;
            
            for(int i = 1; i <= GetNumTracks(); i++)
                if(selectedTrack == GetTrackFromId(i))
                    trackOffset_ = i - 1;
            
            trackOffset_ -= targetScrollLinkChannel_;
            
            if(trackOffset_ <  0)
                trackOffset_ =  0;
            
            int top = GetNumTracks() - trackNavigators_.size();
            
            if(trackOffset_ >  top)
                trackOffset_ = top;
        }
    }
    
    void AdjustTrackBank(int amount)
    {
        if(! isVCAModeEnabled_ && ! isFolderModeEnabled_)
        {
            int numTracks = GetNumTracks();
            
            if(numTracks <= trackNavigators_.size())
                return;
           
            trackOffset_ += amount;
            
            if(trackOffset_ <  0)
                trackOffset_ =  0;
            
            int top = numTracks - trackNavigators_.size();
            
            if(trackOffset_ >  top)
                trackOffset_ = top;
            
            DAW:: SetProjExtState(0, "CSI", "BankIndex", to_string(trackOffset_).c_str());
        }
        else if(isVCAModeEnabled_)
        {
            int numTracks = vcaSpillTracks_.size() + vcaLeadTracks_.size();
            
            if(numTracks <= trackNavigators_.size())
                return;
           
            vcaTrackOffset_ += amount;
            
            if(vcaTrackOffset_ <  0)
                vcaTrackOffset_ =  0;
            
            int top = numTracks - trackNavigators_.size();
            
            if(vcaTrackOffset_ >  top)
                vcaTrackOffset_ = top;
        }
        else if(isFolderModeEnabled_)
        {
            int numTracks = folderTracks_.size();
            
            if(numTracks <= trackNavigators_.size())
                return;
           
            folderTrackOffset_ += amount;
            
            if(folderTrackOffset_ <  0)
                folderTrackOffset_ =  0;
            
            int top = numTracks - trackNavigators_.size();
            
            if(folderTrackOffset_ >  top)
                folderTrackOffset_ = top;
        }
    }
        
    void ToggleVCAMode()
    {
        isVCAModeEnabled_ = ! isVCAModeEnabled_;
    }
    
    Navigator* GetNavigatorForChannel(int channelNum)
    {
        if(trackNavigators_.count(channelNum) < 1)
            trackNavigators_[channelNum] = new TrackNavigator(page_, this, channelNum);
            
        return trackNavigators_[channelNum];
    }
    
    MediaTrack* GetTrackFromChannel(int channelNumber)
    {
        if(! isVCAModeEnabled_ && ! isFolderModeEnabled_)
        {
            return GetTrackFromId(channelNumber + trackOffset_ + 1);    // Master Track is idx 0, so add 1
        }
        else if(isFolderModeEnabled_)
        {
            if(channelNumber < folderTracks_.size())
                return folderTracks_[channelNumber];
            else
                return nullptr;
        }
        else if(isVCAModeEnabled_)
        {
            if(vcaLeadTracks_.size() == 0)
            {
                if(vcaTopLeadTracks_.size() > channelNumber && DAW::ValidateTrackPtr(vcaTopLeadTracks_[channelNumber]))
                    return vcaTopLeadTracks_[channelNumber];
                else
                    return nullptr;
            }
            else
            {
                if(channelNumber < vcaLeadTracks_.size())
                    return vcaLeadTracks_[channelNumber];
                else
                {
                    channelNumber -= vcaLeadTracks_.size();
                    
                    channelNumber += vcaTrackOffset_;
                    
                    if(channelNumber < vcaSpillTracks_.size())
                        return vcaSpillTracks_[channelNumber];
                }
            }
        }
        
        return nullptr;
    }
    
    MediaTrack* GetTrackFromId(int trackNumber)
    {
        if(trackNumber <= GetNumTracks())
            return DAW::CSurf_TrackFromID(trackNumber, followMCP_);
        else
            return nullptr;
    }
    
    int GetIdFromTrack(MediaTrack* track)
    {
        return DAW::CSurf_TrackToID(track, followMCP_);
    }
    
    void ToggleVCASpill(MediaTrack* track)
    {
        if(! isVCAModeEnabled_)
            return;
        
        if(DAW::GetTrackGroupMembership(track, "VOLUME_VCA_LEAD") == 0 && DAW::GetTrackGroupMembershipHigh(track, "VOLUME_VCA_LEAD") == 0)
            return;
               
        auto it = find(vcaLeadTracks_.begin(), vcaLeadTracks_.end(), track);
        
        if(it == vcaLeadTracks_.end())
            vcaLeadTracks_.push_back(track);
        else
            vcaLeadTracks_.erase(it, vcaLeadTracks_.end());
        
        vcaTrackOffset_ = 0;
    }
   
    void ToggleScrollLink(int targetChannel)
    {
        targetScrollLinkChannel_ = targetChannel - 1 < 0 ? 0 : targetChannel - 1;
        
        isScrollLinkEnabled_ = ! isScrollLinkEnabled_;
        
        OnTrackSelection();
    }
    
    MediaTrack* GetSelectedTrack()
    {
        if(DAW::CountSelectedTracks() != 1)
            return nullptr;
        else
            return DAW::GetSelectedTrack(0);
    }
    
//  Page only uses the following:
       
    void OnTrackSelection()
    {
        if(isScrollLinkEnabled_)
            ForceScrollLink();
    }
    
    void OnTrackListChange()
    {
        if(isScrollLinkEnabled_)
            ForceScrollLink();
    }

    void OnTrackSelectionBySurface(MediaTrack* track)
    {
        if(isScrollLinkEnabled_)
        {
            if(DAW::IsTrackVisible(track, true))
                DAW::SetMixerScroll(track); // scroll selected MCP tracks into view
            
            if(DAW::IsTrackVisible(track, false))
                DAW::SendCommandMessage(40913); // scroll selected TCP tracks into view
        }
    }

    bool GetIsControlTouched(MediaTrack* track, int touchedControl)
    {
        if(track == GetMasterTrackNavigator()->GetTrack())
            return GetIsNavigatorTouched(GetMasterTrackNavigator(), touchedControl);
        
        for(auto  [key, navigator] : trackNavigators_)
            if(track == navigator->GetTrack())
                return GetIsNavigatorTouched(navigator, touchedControl);
 
        if(MediaTrack* selectedTrack = GetSelectedTrack())
             if(track == selectedTrack)
                return GetIsNavigatorTouched(GetSelectedTrackNavigator(), touchedControl);
        
        if(MediaTrack* focusedFXTrack = GetFocusedFXNavigator()->GetTrack())
            if(track == focusedFXTrack)
                return GetIsNavigatorTouched(GetFocusedFXNavigator(), touchedControl);

        return false;
    }
    
    bool GetIsNavigatorTouched(Navigator* navigator,  int touchedControl)
    {
        if(touchedControl == 0)
            return navigator->GetIsVolumeTouched();
        else if(touchedControl == 1)
        {
            if(navigator->GetIsPanTouched() || navigator->GetIsPanLeftTouched())
                return true;
        }
        else if(touchedControl == 2)
        {
            if(navigator->GetIsPanWidthTouched() || navigator->GetIsPanRightTouched())
                return true;
        }

        return false;
    }
    
    void RebuildVCASpill()
    {   
        if(! isVCAModeEnabled_)
            return;
    
        vcaTopLeadTracks_.clear();
        vcaSpillTracks_.clear();

        MediaTrack* leadTrack = nullptr;
        bitset<32> leadTrackVCALeaderGroup;
        bitset<32> leadTrackVCALeaderGroupHigh;
        
        if(vcaLeadTracks_.size() > 0)
        {
            leadTrack = vcaLeadTracks_.back();
            leadTrackVCALeaderGroup = DAW::GetTrackGroupMembership(leadTrack, "VOLUME_VCA_LEAD");
            leadTrackVCALeaderGroupHigh = DAW::GetTrackGroupMembershipHigh(leadTrack, "VOLUME_VCA_LEAD");
        }
        
        // Get Visible Tracks
        for (int i = 1; i <= GetNumTracks(); i++)
        {
            MediaTrack* track = DAW::CSurf_TrackFromID(i, followMCP_);
            
            if(DAW::GetTrackGroupMembership(track, "VOLUME_VCA_LEAD") != 0 && DAW::GetTrackGroupMembership(track, "VOLUME_VCA_FOLLOW") == 0)
                vcaTopLeadTracks_.push_back(track);
            
            if(DAW::GetTrackGroupMembershipHigh(track, "VOLUME_VCA_LEAD") != 0 && DAW::GetTrackGroupMembershipHigh(track, "VOLUME_VCA_FOLLOW") == 0)
                vcaTopLeadTracks_.push_back(track);
            
            if(leadTrack != nullptr)
            {
                bool isFollower = false;
                
                bitset<32> leadTrackVCAFollowerGroup(DAW::GetTrackGroupMembership(track, "VOLUME_VCA_FOLLOW"));
                bitset<32> leadTrackVCAFollowerGroupHigh(DAW::GetTrackGroupMembershipHigh(track, "VOLUME_VCA_FOLLOW"));

                for(int i = 0; i < 32; i++)
                {
                    if((leadTrackVCALeaderGroup[i] == 1 && leadTrackVCAFollowerGroup[i] == 1)
                       || (leadTrackVCALeaderGroupHigh[i] == 1 && leadTrackVCAFollowerGroupHigh[i] == 1))
                    {
                        isFollower = true;
                        break;
                    }
                }
                
                if(isFollower)
                        vcaSpillTracks_.push_back(track);
            }
        }
    }
    
    void RebuildFolderTracks()
    {
        if(! isFolderModeEnabled_)
            return;
        
        folderTracks_.clear();
        
        for (int i = 1; i <= GetNumTracks(); i++)
        {
            if(MediaTrack* track = DAW::CSurf_TrackFromID(i, followMCP_))
                if(DAW::GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH") == 1)
                    folderTracks_.push_back(track);
        }
    }
    
    void EnterPage()
    {
        /*
         if(colourTracks_)
         {
         // capture track colors
         for(auto* navigator : trackNavigators_)
         if(MediaTrack* track = DAW::GetTrackFromGUID(navigator->GetTrackGUID(), followMCP_))
         trackColours_[navigator->GetTrackGUID()] = DAW::GetTrackColor(track);
         }
         */
    }
    
    void LeavePage()
    {
        /*
         if(colourTracks_)
         {
         DAW::PreventUIRefresh(1);
         // reset track colors
         for(auto* navigator : trackNavigators_)
         if(MediaTrack* track = DAW::GetTrackFromGUID(navigator->GetTrackGUID(), followMCP_))
         if(trackColours_.count(navigator->GetTrackGUID()) > 0)
         DAW::GetSetMediaTrackInfo(track, "I_CUSTOMCOLOR", &trackColours_[navigator->GetTrackGUID()]);
         DAW::PreventUIRefresh(-1);
         }
         */
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Page
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    string name_ = "";
    vector<ControlSurface*> surfaces_;
    
    bool isShift_ = false;
    double shiftPressedTime_ = 0;
    bool isOption_ = false;
    double optionPressedTime_ = 0;
    bool isControl_ = false;
    double controlPressedTime_ = 0;
    bool isAlt_ = false;
    double altPressedTime_ = 0;
    bool isFlip_ = false;
    double flipPressedTime_ = 0;
    
    TrackNavigationManager* const trackNavigationManager_ = nullptr;
    
public:
    Page(string name, bool followMCP, bool synchPages, bool scrollLink) : name_(name),  trackNavigationManager_(new TrackNavigationManager(this, followMCP, synchPages, scrollLink)) {}
    
    ~Page()
    {
        for(auto surface: surfaces_)
        {
            delete surface;
            surface = nullptr;
        }
    }
    
    string GetName() { return name_; }
    
    bool GetShift() { return isShift_; }
    bool GetOption() { return isOption_; }
    bool GetControl() { return isControl_; }
    bool GetAlt() { return isAlt_; }
    bool GetFlip() { return isFlip_; }

                   
    /*
    int repeats = 0;
    
    void Run()
    {
        int start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        trackNavigationManager_->RebuildVCASpill();
        trackNavigationManager_->RebuildFolderTracks();
     
        for(auto surface : surfaces_)
            surface->HandleExternalInput();
        
        for(auto surface : surfaces_)
            surface->RequestUpdate();
        
         repeats++;
         
         if(repeats > 50)
         {
             repeats = 0;
             
             int totalDuration = 0;

             start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
             trackNavigationManager_->RebuildVCASpill();
             int duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - start;
             totalDuration += duration;
             ShowDuration("Rebuild Track List", duration);
             
             for(auto surface : surfaces_)
             {
                 start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
                 surface->HandleExternalInput();
                 duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - start;
                 totalDuration += duration;
                 ShowDuration(surface->GetName(), "HandleExternalInput", duration);
             }
             
             for(auto surface : surfaces_)
             {
                 start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
                 surface->RequestUpdate();
                 duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - start;
                 totalDuration += duration;
                 ShowDuration(surface->GetName(), "Request Update", duration);
             }
             
             char msgBuffer[250];
             
             sprintf(msgBuffer, "Total duration = %d\n\n\n", totalDuration);
             DAW::ShowConsoleMsg(msgBuffer);
         }
    }
    
    
    void ShowDuration(string item, int duration)
    {
        char msgBuffer[250];
        
        sprintf(msgBuffer, "%s - %d microseconds\n", item.c_str(), duration);
        DAW::ShowConsoleMsg(msgBuffer);
    }
    
    void ShowDuration(string surface, string item, int duration)
    {
        char msgBuffer[250];
        
        sprintf(msgBuffer, "%s - %s - %d microseconds\n", surface.c_str(), item.c_str(), duration);
        DAW::ShowConsoleMsg(msgBuffer);
    }
   */

//*
    void Run()
    {
        trackNavigationManager_->RebuildVCASpill();
        trackNavigationManager_->RebuildFolderTracks();
        
        for(auto surface : surfaces_)
            surface->HandleExternalInput();
        
        for(auto surface : surfaces_)
            surface->RequestUpdate();
    }
//*/
    void ClearAllWidgets()
    {
        for(auto surface : surfaces_)
            surface->ClearAllWidgets();
    }
    
    void ForceRefreshTimeDisplay()
    {
        for(auto surface : surfaces_)
            surface->ForceRefreshTimeDisplay();
    }

    void AddSurface(ControlSurface* surface)
    {
        surfaces_.push_back(surface);
    }
    
    bool GetTouchState(MediaTrack* track, int touchedControl)
    {
        return trackNavigationManager_->GetIsControlTouched(track, touchedControl);
    }
    
    void SetShift(bool value)
    {
        SetLatchModifier(value, isShift_, shiftPressedTime_);
    }
 
    void SetOption(bool value)
    {
        SetLatchModifier(value, isOption_, optionPressedTime_);
    }
    
    void SetControl(bool value)
    {
        SetLatchModifier(value, isControl_, controlPressedTime_);
    }
    
    void SetAlt(bool value)
    {
        SetLatchModifier(value, isAlt_, altPressedTime_);
    }
  
    void SetFlip(bool value)
    {
        SetLatchModifier(value, isFlip_, flipPressedTime_);
    }
  
    void SetLatchModifier(bool value, bool &modifier, double &modifierPressedTime)
    {
        if(value && modifier == false)
        {
            modifier = value;
            modifierPressedTime = DAW::GetCurrentNumberOfMilliseconds();
        }
        else
        {
            double keyReleasedTime = DAW::GetCurrentNumberOfMilliseconds();
            
            if(keyReleasedTime - modifierPressedTime > 100)
                modifier = value;
        }
    }

    string GetModifier()
    {
        string modifier = "";
        
        if(isShift_)
            modifier += ShiftToken + "+";
        if(isOption_)
            modifier += OptionToken + "+";
        if(isControl_)
            modifier +=  ControlToken + "+";
        if(isAlt_)
            modifier += AltToken + "+";
        if(isFlip_)
            modifier += FlipToken + "+";

        return modifier;
    }
    
    void OnTrackSelection()
    {
        trackNavigationManager_->OnTrackSelection();
        
        for(auto surface : surfaces_)
            surface->OnTrackSelection();
    }
    
    void OnTrackListChange()
    {
        trackNavigationManager_->OnTrackListChange();
    }
    
    void OnTrackSelectionBySurface(MediaTrack* track)
    {
        trackNavigationManager_->OnTrackSelectionBySurface(track);
        
        for(auto surface : surfaces_)
            surface->OnTrackSelection();
    }

    void TrackFXListChanged(MediaTrack* track)
    {
        for(auto surface : surfaces_)
            surface->TrackFXListChanged();
    }

    void EnterPage()
    {
        trackNavigationManager_->EnterPage();
        
        for(auto surface : surfaces_)
            surface->OnPageEnter();
    }
    
    void LeavePage()
    {
        trackNavigationManager_->LeavePage();
        
        for(auto surface : surfaces_)
            surface->OnPageLeave();
    }
    
    void OnInitialization()
    {
        for(auto surface : surfaces_)
            surface->OnInitialization();
    }
    
    void SignalActivation(ControlSurface* originatingSurface, string zoneName)
    {
        for(auto surface : surfaces_)
            if(surface != originatingSurface)
                surface->GetZoneManager()->HandleActivation(zoneName);

    }
    
    void SignalTrackSendBank(ControlSurface* originatingSurface, int amount)
    {
        for(auto surface : surfaces_)
            if(surface != originatingSurface)
                surface->GetZoneManager()->HandleTrackSendBank(amount);
    }
    
    void SignalTrackReceiveBank(ControlSurface* originatingSurface, int amount)
    {
        for(auto surface : surfaces_)
            if(surface != originatingSurface)
                surface->GetZoneManager()->HandleTrackReceiveBank(amount);
    }
    
    void SignalTrackFXMenuBank(ControlSurface* originatingSurface, int amount)
    {
        for(auto surface : surfaces_)
            if(surface != originatingSurface)
                surface->GetZoneManager()->HandleTrackFXMenuBank(amount);
    }
    
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Page facade for TrackNavigationManager
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    bool GetSynchPages() { return trackNavigationManager_->GetSynchPages(); }
    bool GetScrollLink() { return trackNavigationManager_->GetScrollLink(); }
    bool GetVCAMode() { return trackNavigationManager_->GetVCAMode(); }
    int  GetNumTracks() { return trackNavigationManager_->GetNumTracks(); }
    Navigator* GetMasterTrackNavigator() { return trackNavigationManager_->GetMasterTrackNavigator(); }
    Navigator* GetSelectedTrackNavigator() { return trackNavigationManager_->GetSelectedTrackNavigator(); }
    Navigator* GetFocusedFXNavigator() { return trackNavigationManager_->GetFocusedFXNavigator(); }
    Navigator* GetDefaultNavigator() { return trackNavigationManager_->GetDefaultNavigator(); }
    void ForceScrollLink() { trackNavigationManager_->ForceScrollLink(); }
    void AdjustTrackBank(int amount) { trackNavigationManager_->AdjustTrackBank(amount); }
    void ToggleVCAMode() { trackNavigationManager_->ToggleVCAMode(); }
    void NextTrackVCAFolderMode() { trackNavigationManager_->NextTrackVCAFolderMode(); }
    void ResetTrackVCAFolderMode() { trackNavigationManager_->ResetTrackVCAFolderMode(); }
    int GetCurrentTrackVCAFolderMode() { return trackNavigationManager_->GetCurrentTrackVCAFolderMode(); }
    Navigator* GetNavigatorForChannel(int channelNum) { return trackNavigationManager_->GetNavigatorForChannel(channelNum); }
    MediaTrack* GetTrackFromId(int trackNumber) { return trackNavigationManager_->GetTrackFromId(trackNumber); }
    int GetIdFromTrack(MediaTrack* track) { return trackNavigationManager_->GetIdFromTrack(track); }
    void ToggleVCASpill(MediaTrack* track) { trackNavigationManager_->ToggleVCASpill(track); }
    void ToggleScrollLink(int targetChannel) { trackNavigationManager_->ToggleScrollLink(targetChannel); }
    MediaTrack* GetSelectedTrack() { return trackNavigationManager_->GetSelectedTrack(); }
    void SetAutoModeIndex() { trackNavigationManager_->SetAutoModeIndex(); }
    void NextAutoMode() { trackNavigationManager_->NextAutoMode(); }
    string GetAutoModeDisplayName() { return trackNavigationManager_->GetAutoModeDisplayName(); }
    string GetAutoModeDisplayName(int modeIndex) { return trackNavigationManager_->GetAutoModeDisplayName(modeIndex); }
    vector<MediaTrack*> &GetSelectedTracks() { return trackNavigationManager_->GetSelectedTracks(); }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Manager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    map<string, Action*> actions_;

    vector <Page*> pages_;
    
    int currentPageIndex_ = 0;
    bool surfaceInDisplay_ = false;
    bool surfaceRawInDisplay_ = false;
    bool surfaceOutDisplay_ = false;
    bool fxParamsDisplay_ = false;
    bool fxParamsWrite_ = false;

    bool shouldRun_ = true;
    
    int *timeModePtr_ = nullptr;
    int *timeMode2Ptr_ = nullptr;
    int *measOffsPtr_ = nullptr;
    double *timeOffsPtr_ = nullptr;
    int *projectPanModePtr_ = nullptr;
    
    void InitActionsDictionary();

    double GetPrivateProfileDouble(string key)
    {
        char tmp[512];
        memset(tmp, 0, sizeof(tmp));
        
        DAW::GetPrivateProfileString("REAPER", key.c_str() , "", tmp, sizeof(tmp), DAW::get_ini_file());
        
        return strtod (tmp, NULL);
    }
    
public:
    Manager()
    {
        InitActionsDictionary();

        int size = 0;
        int index = projectconfig_var_getoffs("projtimemode", &size);
        timeModePtr_ = (int *)projectconfig_var_addr(nullptr, index);
        
        index = projectconfig_var_getoffs("projtimemode2", &size);
        timeMode2Ptr_ = (int *)projectconfig_var_addr(nullptr, index);
        
        index = projectconfig_var_getoffs("projmeasoffs", &size);
        measOffsPtr_ = (int *)projectconfig_var_addr(nullptr, index);
        
        index = projectconfig_var_getoffs("projtimeoffs", &size);
        timeOffsPtr_ = (double *)projectconfig_var_addr(nullptr, index);
        
        index = projectconfig_var_getoffs("panmode", &size);
        projectPanModePtr_ = (int*)projectconfig_var_addr(nullptr, index);
    }
    
    ~Manager()
    {
        for(auto page: pages_)
        {
            delete page;
            page = nullptr;
        }
        
        for(auto [key, action] : actions_)
        {
            delete action;
            action = nullptr;
        }
    }

    
    void Shutdown()
    {
        fxParamsDisplay_ = false;
        surfaceInDisplay_ = false;
        surfaceOutDisplay_ = false;
       
        // GAW -- IMPORTANT
        // We want to stop polling, save state, and and zero out all Widgets before shutting down
        shouldRun_ = false;
        
        if(pages_.size() > 0)
            pages_[currentPageIndex_]->ClearAllWidgets();
    }
    
    void Init();

    void ToggleSurfaceInDisplay() { surfaceInDisplay_ = ! surfaceInDisplay_;  }
    void ToggleSurfaceRawInDisplay() { surfaceRawInDisplay_ = ! surfaceRawInDisplay_;  }
    void ToggleSurfaceOutDisplay() { surfaceOutDisplay_ = ! surfaceOutDisplay_;  }
    void ToggleFXParamsDisplay() { fxParamsDisplay_ = ! fxParamsDisplay_;  }
    void ToggleFXParamsWrite() { fxParamsWrite_ = ! fxParamsWrite_;  }

    bool GetSurfaceInDisplay() { return surfaceInDisplay_;  }
    bool GetSurfaceRawInDisplay() { return surfaceRawInDisplay_;  }
    bool GetSurfaceOutDisplay() { return surfaceOutDisplay_;  }
    
    double GetFaderMaxDB() { return GetPrivateProfileDouble("slidermaxv"); }
    double GetFaderMinDB() { return GetPrivateProfileDouble("sliderminv"); }
    double GetVUMaxDB() { return GetPrivateProfileDouble("vumaxvol"); }
    double GetVUMinDB() { return GetPrivateProfileDouble("vuminvol"); }
    
    int *GetTimeModePtr() { return timeModePtr_; }
    int *GetTimeMode2Ptr() { return timeMode2Ptr_; }
    int *GetMeasOffsPtr() { return measOffsPtr_; }
    double *GetTimeOffsPtr() { return timeOffsPtr_; }
    int GetProjectPanMode() { return *projectPanModePtr_; }
   
    shared_ptr<ActionContext> GetActionContext(string actionName, Widget* widget, shared_ptr<Zone> zone, vector<string> params, vector<vector<string>> properties)
    {
        if(actions_.count(actionName) > 0)
            return make_shared<ActionContext>(actions_[actionName], widget, zone, params, properties);
        else
            return make_shared<ActionContext>(actions_["NoAction"], widget, zone, params, properties);
    }

    void OnTrackSelection(MediaTrack *track)
    {
        if(pages_.size() > 0)
            pages_[currentPageIndex_]->OnTrackSelection();
    }
    
    void OnTrackListChange()
    {
        if(pages_.size() > 0)
            pages_[currentPageIndex_]->OnTrackListChange();
    }
    
    void NextTimeDisplayMode()
    {
        int *tmodeptr = GetTimeMode2Ptr();
        if (tmodeptr && *tmodeptr>=0)
        {
            (*tmodeptr)++;
            if ((*tmodeptr)>5)
                (*tmodeptr)=0;
        }
        else
        {
            tmodeptr = GetTimeModePtr();
            
            if (tmodeptr)
            {
                (*tmodeptr)++;
                if ((*tmodeptr)>5)
                    (*tmodeptr)=0;
            }
        }

        if(pages_.size() > 0)
            pages_[currentPageIndex_]->ForceRefreshTimeDisplay();
    }
    
    void AdjustTrackBank(Page* sendingPage, int amount)
    {
        if(! sendingPage->GetSynchPages())
            sendingPage->AdjustTrackBank(amount);
        else
            for(auto page: pages_)
                if(page->GetSynchPages())
                    page->AdjustTrackBank(amount);
    }
        
    void NextPage()
    {
        if(pages_.size() > 0)
        {
            pages_[currentPageIndex_]->LeavePage();
            currentPageIndex_ = currentPageIndex_ == pages_.size() - 1 ? 0 : ++currentPageIndex_;
            DAW::SetProjExtState(0, "CSI", "PageIndex", to_string(currentPageIndex_).c_str());
            pages_[currentPageIndex_]->EnterPage();
        }
    }
    
    void GoToPage(string pageName)
    {
        for(int i = 0; i < pages_.size(); i++)
        {
            if(pages_[i]->GetName() == pageName)
            {
                pages_[currentPageIndex_]->LeavePage();
                currentPageIndex_ = i;
                DAW::SetProjExtState(0, "CSI", "PageIndex", to_string(currentPageIndex_).c_str());
                pages_[currentPageIndex_]->EnterPage();
                break;
            }
        }
    }
    
    bool GetTouchState(MediaTrack* track, int touchedControl)
    {
        if(pages_.size() > 0)
            return pages_[currentPageIndex_]->GetTouchState(track, touchedControl);
        else
            return false;
    }
    
    void TrackFXListChanged(MediaTrack* track)
    {
        for(auto & page : pages_)
            page->TrackFXListChanged(track);
        
        if(fxParamsDisplay_ || fxParamsWrite_)
        {
            char fxName[BUFSZ];
            char fxParamName[BUFSZ];
            
            ofstream fxFile;
            
            for(int i = 0; i < DAW::TrackFX_GetCount(track); i++)
            {
                DAW::TrackFX_GetFXName(track, i, fxName, sizeof(fxName));
                
                if(fxParamsDisplay_)
                    DAW::ShowConsoleMsg(("Zone \"" + string(fxName) + "\"").c_str());
                
                if(fxParamsWrite_)
                {
                    string fxNameNoBadChars(fxName);
                    fxNameNoBadChars = regex_replace(fxNameNoBadChars, regex(BadFileChars), "_");

                    fxFile.open(string(DAW::GetResourcePath()) + "/CSI/Zones/ZoneRawFXFiles/" + fxNameNoBadChars + ".txt");
                    
                    if(fxFile.is_open())
                        fxFile << "Zone \"" + string(fxName) + "\"" + GetLineEnding();
                }

                for(int j = 0; j < DAW::TrackFX_GetNumParams(track, i); j++)
                {
                    DAW::TrackFX_GetParamName(track, i, j, fxParamName, sizeof(fxParamName));

                    if(fxParamsDisplay_)
                        DAW::ShowConsoleMsg(("\n\tFXParam " + to_string(j) + " \"" + string(fxParamName) + "\"").c_str());
  
                    if(fxParamsWrite_ && fxFile.is_open())
                        fxFile <<  "\tFXParam " + to_string(j) + " \"" + string(fxParamName)+ "\"" + GetLineEnding();
                        
                    /* step sizes
                    double stepOut = 0;
                    double smallstepOut = 0;
                    double largestepOut = 0;
                    bool istoggleOut = false;
                    TrackFX_GetParameterStepSizes(track, i, j, &stepOut, &smallstepOut, &largestepOut, &istoggleOut);

                    DAW::ShowConsoleMsg(("\n\n" + to_string(j) + " - \"" + string(fxParamName) + "\"\t\t\t\t Step = " +  to_string(stepOut) + " Small Step = " + to_string(smallstepOut)  + " LargeStep = " + to_string(largestepOut)  + " Toggle Out = " + (istoggleOut == 0 ? "false" : "true")).c_str());
                    */
                }
                
                if(fxParamsDisplay_)
                    DAW::ShowConsoleMsg("\nZoneEnd\n\n");

                if(fxParamsWrite_ && fxFile.is_open())
                {
                    fxFile << "ZoneEnd";
                    fxFile.close();
                }
            }
        }
    }
    
    //int repeats = 0;
    
    void Run()
    {
        //int start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        if(shouldRun_ && pages_.size() > 0)
            pages_[currentPageIndex_]->Run();
        /*
         repeats++;
         
         if(repeats > 50)
         {
         repeats = 0;
         
         int duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - start;
         
         char msgBuffer[250];
         
         sprintf(msgBuffer, "%d microseconds\n", duration);
         DAW::ShowConsoleMsg(msgBuffer);
         }
        */
    }
};

/*
 int start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
 
 
 // code you wish to time goes here
 // code you wish to time goes here
 // code you wish to time goes here
 // code you wish to time goes here
 // code you wish to time goes here
 // code you wish to time goes here
 
 
 
 int duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - start;
 
 char msgBuffer[250];
 
 sprintf(msgBuffer, "%d microseconds\n", duration);
 DAW::ShowConsoleMsg(msgBuffer);
 
 */

#endif /* control_surface_integrator.h */
