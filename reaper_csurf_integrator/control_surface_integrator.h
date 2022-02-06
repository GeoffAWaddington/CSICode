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

#ifdef _WIN32
#include "oscpkt.hh"
#include "udp.hh"
#include <bitset>
#endif

#include "control_surface_integrator_Reaper.h"

#ifdef _WIN32
#include <memory>
#include "direntWin.h"
#include <functional>
#include "commctrl.h"
#else
#include <dirent.h>
#include "oscpkt.hh"
#include "udp.hh"
#endif

extern string GetLineEnding();

extern REAPER_PLUGIN_HINSTANCE g_hInst;

const string ControlSurfaceIntegrator = "ControlSurfaceIntegrator";

const string FollowMCPToken = "FollowMCP";
const string MidiSurfaceToken = "MidiSurface";
const string OSCSurfaceToken = "OSCSurface";
const string PageToken = "Page";

const string Shift = "Shift";
const string Option = "Option";
const string Control = "Control";
const string Alt = "Alt";

const string BadFileChars = "[ \\:*?<>|.,()/]";
const string CRLFChars = "[\r\n]";
const string TabChars = "[\t]";

const int TempDisplayTime = 1250;

enum ActivationType
{
    Activating,
    Deactivating,
    TogglingActivation,
};

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
    
    virtual string GetName() { return "Navigator"; }
    virtual MediaTrack* GetTrack() { return nullptr; }
    virtual string GetChannelNumString() { return ""; }
    virtual bool GetIsChannelPinned() { return false; }
    virtual void IncBias() {}
    virtual void DecBias() {}
    virtual void PinChannel() {}
    virtual void SetPinnedTrack(MediaTrack* track) { }
    virtual void UnpinChannel() {}
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackNavigator : public Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    int const channelNum_ = 0;
    int bias_ = 0;
    MediaTrack* pinnedTrack_ = nullptr;
    
protected:
    TrackNavigationManager* const manager_;

public:
    TrackNavigator(Page*  page, TrackNavigationManager* manager, int channelNum) : Navigator(page), manager_(manager), channelNum_(channelNum) {}
    TrackNavigator(Page*  page, TrackNavigationManager* manager) : Navigator(page), manager_(manager) {}
    virtual ~TrackNavigator() {}
    
    virtual bool GetIsChannelPinned() override { return pinnedTrack_ != nullptr; }
    virtual void IncBias() override { bias_++; }
    virtual void DecBias() override { bias_--; }
    
    virtual void PinChannel() override;
    virtual void UnpinChannel() override;
    
    virtual void SetPinnedTrack(MediaTrack* track) override { pinnedTrack_ = track; }

    virtual string GetName() override { return "TrackNavigator"; }
    
    virtual string GetChannelNumString() override { return to_string(channelNum_ + 1); }
    
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
    Zone* const zone_ = nullptr;
    
    Widget* associatedWidget_ = nullptr;
    
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
    ActionContext(Action* action, Widget* widget, Zone* zone, vector<string> params, vector<vector<string>> properties);

    virtual ~ActionContext() {}
    
    Widget* GetWidget() { return widget_; }
    Zone* GetZone() { return zone_; }
    int GetSlotIndex();
    string GetName();

    void SetAssociatedWidget(Widget* widget) { associatedWidget_ = widget; }
    Widget* GetAssociatedWidget() { return associatedWidget_; }

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
    void ForceWidgetValue(double value);
    
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
private:
    ZoneManager* const zoneManager_ = nullptr;
    Navigator* navigator_= nullptr;
    int slotIndex_ = 0;
    string const name_ = "";
    string const alias_ = "";
    string const sourceFilePath_ = "";
    
    string const basedOnZone_ = "Standard";
    
    bool isActive_ = false;
    map<string, string> touchIds_;
    map<string, bool> activeTouchIds_;
    
    vector<Widget*> widgets_;
    
    vector<Zone*> includedZones_;

    vector<Zone*> subZones_;

    map<Widget*, map<string, vector<ActionContext*>>> actionContextDictionary_;
    vector<ActionContext*> defaultContexts_;
    
public:
    Zone(ZoneManager* const zoneManager,  Navigator* navigator, string basedOnZone, int slotIndex, map<string, string> touchIds, string name, string alias, string sourceFilePath): zoneManager_(zoneManager), navigator_(navigator), basedOnZone_(basedOnZone), slotIndex_(slotIndex), touchIds_(touchIds), name_(name), alias_(alias), sourceFilePath_(sourceFilePath) {}
    
    Zone() {}
   
    void SetNavigator(Navigator* navigator) { navigator_ = navigator; }
    Navigator* GetNavigator() { return navigator_; }
    string GetBasedOnZone() { return basedOnZone_; }
    void SetSlotIndex(int index) { slotIndex_ = index; }
    int GetSlotIndex();
    
    vector<ActionContext*> &GetActionContexts(Widget* widget);
    
    void RequestUpdateWidget(Widget* widget);
    void Activate();
    void Deactivate();

    void AddIncludedZone(Zone* &zone) { includedZones_.push_back(zone); }
    vector<Zone*> &GetIncludedZones() { return includedZones_; }
      
    vector<Widget*> &GetWidgets() { return widgets_; }

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
        GetWidgets().push_back(widget);
    }
    
    void AddActionContext(Widget* widget, string modifier, ActionContext* actionContext)
    {
        actionContextDictionary_[widget][modifier].push_back(actionContext);
    }
    
    void RequestUpdate(map<Widget*, bool> &usedWidgets)
    {
        if(! isActive_)
            return;
      
        for(auto widget : GetWidgets())
        {
            if(usedWidgets[widget] == false)
            {
                usedWidgets[widget] = true;
                RequestUpdateWidget(widget);
            }
        }

        for(auto zone : includedZones_)
            zone->RequestUpdate(usedWidgets);
    }

    void Unmap()
    {
        for(auto &[key, value] : actionContextDictionary_)
            for(auto &[key, value] : value)
                for(ActionContext* context : value)
                    delete context;
        
        actionContextDictionary_.clear();
        
        for(Zone* zone : includedZones_)
            zone->Unmap();
        
        includedZones_.clear();
        
        for(Zone* zone : subZones_)
            zone->Unmap();
        
        subZones_.clear();
    }
    
    void DoAction(Widget* widget, bool &isUsed, double value)
    {
        if(! isActive_ || isUsed)
            return;
        
        if(find(GetWidgets().begin(), GetWidgets().end(), widget) != GetWidgets().end())
        {
            isUsed = true;
            
            for(auto context : GetActionContexts(widget))
                context->DoAction(value);
        }
        
        for(auto zone : includedZones_)
            zone->DoAction(widget, isUsed, value);
    }
       
    void DoRelativeAction(Widget* widget, bool &isUsed, double delta)
    {
        if(! isActive_ || isUsed)
            return;
        
        if(find(GetWidgets().begin(), GetWidgets().end(), widget) != GetWidgets().end())
        {
            isUsed = true;

            for(auto context : GetActionContexts(widget))
                context->DoRelativeAction(delta);
        }
        
        for(auto zone : includedZones_)
            zone->DoRelativeAction(widget, isUsed, delta);
    }
    
    void DoRelativeAction(Widget* widget, bool &isUsed, int accelerationIndex, double delta)
    {
        if(! isActive_ || isUsed)
            return;

        if(find(GetWidgets().begin(), GetWidgets().end(), widget) != GetWidgets().end())
        {
            isUsed = true;

            for(auto context : GetActionContexts(widget))
                context->DoRelativeAction(accelerationIndex, delta);
        }
        
        for(auto zone : includedZones_)
            zone->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
    }
    
    void DoTouch(Widget* widget, string widgetName, bool &isUsed, double value)
    {
        if(! isActive_ || isUsed)
            return;

        if(find(GetWidgets().begin(), GetWidgets().end(), widget) != GetWidgets().end())
        {
            isUsed = true;

            activeTouchIds_[widgetName + "Touch"] = value;
            activeTouchIds_[widgetName + "TouchPress"] = value;
            activeTouchIds_[widgetName + "TouchRelease"] = ! value;

            for(auto context : GetActionContexts(widget))
                context->DoTouch(value);
        }
        
        for(auto zone : includedZones_)
            zone->DoTouch(widget, widgetName, isUsed, value);
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
    bool isToggled_ = false;
    
public:
    Widget(ControlSurface* surface, string name) : surface_(surface), name_(name) {}
    ~Widget();
    
    string GetName() { return name_; }
    ControlSurface* GetSurface() { return surface_; }
    ZoneManager* GetZoneManager();
    
    bool GetIsModifier() { return isModifier_; }
    void SetIsModifier() { isModifier_ = true; }
    
    void Toggle() { isToggled_ = ! isToggled_; }
    bool GetIsToggled() { return isToggled_; }

    void SetProperties(vector<vector<string>> properties);
    void UpdateValue(double value);
    void UpdateValue(int mode, double value);
    void UpdateValue(string value);
    void UpdateRGBValue(int r, int g, int b);
    void ForceValue(double value);
    void ForceRGBValue(int r, int g, int b);
    void ClearCache();
    void Clear();
    void ForceClear();
    void LogInput(double value);

    void GetFormattedFXParamValue(char *buffer, int bufferSize)
    {
        //currentWidgetContext_.GetFormattedFXParamValue(buffer, bufferSize);
    }
    
    void AddFeedbackProcessor(FeedbackProcessor* feedbackProcessor)
    {
        feedbackProcessors_.push_back(feedbackProcessor);
    }
};

struct CSIZoneInfo
{
    string filePath = "";
    string alias = "";
    string navigator = "NotFocusedFXNavigator";
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ZoneManager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    vector<string> broadcast_;
    vector<string> receive_;

    ControlSurface* const surface_;
    string const zoneFolder_ = "";
    
    int const numChannels_ = 0;
    int const numSends_ = 0;
    int const numFXSlots_ = 0;
    
    bool allowOverlay_ = false;
      
    vector<string> associatedZoneNames_;
    map<string, vector<string>> associatedSubZoneNames_;

    map<Widget*, bool> usedWidgets_;

    vector<Zone*> focusedFXZones_;
    map<int, map<int, int>> focusedFXDictionary;
    
    vector<Zone*> fxZones_;
    
    vector<Zone*> selectedTrackZones_;
    
    vector<Zone*> selectedTrackFXMenuZones_;
    vector<Zone*> trackFXMenuZones_;
    
    vector<Zone*> selectedTrackReceivesZones_;
    vector<Zone*> trackReceivesZones_;
    
    vector<Zone*> selectedTrackSendsZones_;
    vector<Zone*> trackSendsZones_;

    vector<Zone*> homeZone_;
    
    vector<vector<Zone*>> fixedZones_;
    
    map<int, Navigator*> navigators_;

    map<string, CSIZoneInfo> zoneFilePaths_;
           
    void MapFocusedFXToWidgets();
    void UnmapFocusedFXFromWidgets();
    
    void MapSelectedTrackFXToWidgets();
    
    void MapSelectedTrackFXSlotToWidgets(vector<Zone*> &zones, int fxSlot);
    
    void Activate(ActivationType activationType, string zoneType, vector<Zone*> &zones);
    
    void Activate(ActivationType activationType, vector<string> &zoneTypes);
    
    void DeactivateZones(vector<Zone*> &zones);
    
    void ActivatingZone(string zoneName)
    {
        
        
    }
    
    void UnmapZones(vector<Zone*> &zones)
    {
        for(auto zone : zones)
            zone->Unmap();

        for(auto zone : zones)
            delete zone;
    
        zones.clear();
    }

public:
    ZoneManager(ControlSurface* surface, string zoneFolder, int numChannels, int numSends, int numFX, int channelOffset);
    
    void ForceClearAllWidgets() { } // GAW clear all widgets in context
    
    void Initialize();
   
    void RequestUpdate();
    
    void PreProcessZones();
    
    Navigator* GetMasterTrackNavigator();
    Navigator* GetSelectedTrackNavigator();
    Navigator* GetFocusedFXNavigator();
    Navigator* GetDefaultNavigator();
    Navigator* GetNavigatorForChannel(int channelNum);
    int GetSendSlot();
    int GetReceiveSlot();
    int GetFXMenuSlot();
    int GetSlot(string basedOnZone);
    int GetNumChannels();
    int GetNumSendSlots();
    int GetNumReceiveSlots();
    int GetNumFXSlots();
    
    void ReceiveActivate(ActivationType activationType, string zoneName);
    void GoHome();
    void ActivateFocusedFXZone(string zoneName, int slotNumber, vector<Zone*> &zones);
    void ActivateFXZone(string zoneName, int slotNumber, vector<Zone*> &zones);
    void ActivateFXSubZone(string zoneName, Zone &originatingZone, int slotNumber, vector<Zone*> &zones);
    void GoSubZone(Zone* enclosingZone, string zoneName, double value);
    
    map<string, CSIZoneInfo> &GetZoneFilePaths() { return zoneFilePaths_; }
    ControlSurface* GetSurface() { return surface_; }
      
    void MapTrackFXSlotToWidgets(MediaTrack* track, int fxSlot)
    {
        char FXName[BUFSZ];
        
        DAW::TrackFX_GetFXName(track, fxSlot, FXName, sizeof(FXName));
        
        if(zoneFilePaths_.count(FXName) > 0)
            ActivateFXZone(FXName, fxSlot, fxZones_);
    }
    
    void AllowOverlay()
    {
        allowOverlay_ = true;
    }
    
    void AddAssociatedZoneName(string name)
    {
        associatedZoneNames_.push_back(name);
    }
    
    void AddAssociatedSubZoneName(string basedOnZone, string name)
    {
        associatedSubZoneNames_[basedOnZone].push_back(name);
    }

    void AddWidget(Widget* widget)
    {
        usedWidgets_[widget] = false;
    }

    void SetBroadcast(ActionContext* context)
    {
        for(string param : context->GetZoneNames())
            broadcast_.push_back(param);
    }

    void SetReceive(ActionContext* context)
    {
        for(string param : context->GetZoneNames())
            receive_.push_back(param);
    }

    void Activate(vector<string> &zoneTypes)
    {
        Activate(ActivationType::Activating, zoneTypes);
    }

    void Deactivate(vector<string> &zoneTypes)
    {
        Activate(ActivationType::Deactivating, zoneTypes);
    }

    void ToggleActivation(vector<string> &zoneTypes)
    {
        Activate(ActivationType::TogglingActivation, zoneTypes);
    }

    string GetNameOrAlias(string name)
    {
        if(zoneFilePaths_.count(name) > 0)
        {
            if(zoneFilePaths_[name].alias != "")
                return zoneFilePaths_[name].alias;
            else
                return name;
        }
                
        return "";
    }
    
    void AddZoneFilePath(string name, struct CSIZoneInfo info)
    {
        if(name != "")
            zoneFilePaths_[name] = info;
    }
    
    void CheckFocusedFXState()
    {
        int trackNumber = 0;
        int itemNumber = 0;
        int fxIndex = 0;
        
        int retval = DAW::GetFocusedFX2(&trackNumber, &itemNumber, &fxIndex);
        
        if((retval & 1) && (fxIndex > -1))
        {
            int lastRetval = -1;

            if(focusedFXDictionary.count(trackNumber) > 0 && focusedFXDictionary[trackNumber].count(fxIndex) > 0)
                lastRetval = focusedFXDictionary[trackNumber][fxIndex];
            
            if(lastRetval != retval)
            {
                if(retval == 1)
                    MapFocusedFXToWidgets();
                
                else if(retval & 4)
                    UnmapFocusedFXFromWidgets();
                
                if(focusedFXDictionary[trackNumber].count(trackNumber) < 1)
                    focusedFXDictionary[trackNumber] = map<int, int>();
                                   
                focusedFXDictionary[trackNumber][fxIndex] = retval;;
            }
        }
    }
       
    void DoAction(Widget* widget, double value)
    {
        widget->LogInput(value);
        
        bool isUsed = false;
        
        for(auto zone : focusedFXZones_)
            zone->DoAction(widget, isUsed, value);

        for(auto zone : fxZones_)
            zone->DoAction(widget, isUsed, value);

        for(vector<Zone*> zones : fixedZones_)
            for(auto zone : zones)
                zone->DoAction(widget, isUsed, value);
    }
    
    void DoRelativeAction(Widget* widget, double delta)
    {
        widget->LogInput(delta);
        
        bool isUsed = false;
        
        for(auto zone : focusedFXZones_)
            zone->DoRelativeAction(widget, isUsed, delta);

        for(auto zone : fxZones_)
            zone->DoRelativeAction(widget, isUsed, delta);

        for(vector<Zone*> zones : fixedZones_)
            for(auto zone : zones)
                zone->DoRelativeAction(widget, isUsed, delta);
    }
    
    void DoRelativeAction(Widget* widget, int accelerationIndex, double delta)
    {
        widget->LogInput(delta);
        
        bool isUsed = false;
               
        for(auto zone : focusedFXZones_)
            zone->DoRelativeAction(widget, isUsed, accelerationIndex, delta);

        for(auto zone : fxZones_)
            zone->DoRelativeAction(widget, isUsed, accelerationIndex, delta);

        for(vector<Zone*> zones : fixedZones_)
            for(auto zone : zones)
                zone->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
    }
    
    void DoTouch(Widget* widget, double value)
    {
        widget->LogInput(value);
        
        bool isUsed = false;
        
        for(auto zone : focusedFXZones_)
            zone->DoTouch(widget, widget->GetName(), isUsed, value);

        for(auto zone : fxZones_)
            zone->DoTouch(widget, widget->GetName(), isUsed, value);

        for(vector<Zone*> zones : fixedZones_)
            for(auto zone : zones)
                zone->DoTouch(widget, widget->GetName(), isUsed, value);
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
protected:
    ControlSurface(CSurfIntegrator* CSurfIntegrator, Page* page, const string name, string zoneFolder, int numChannels, int numSends, int numFX, int channelOffset) :  CSurfIntegrator_(CSurfIntegrator), page_(page), name_(name), numChannels_(numChannels), numSends_(numSends), numFXSlots_(numFX), zoneManager_(new ZoneManager(this, zoneFolder, numChannels, numSends, numFX, channelOffset)) { }
    
    CSurfIntegrator* const CSurfIntegrator_ ;
    Page* const page_;
    string const name_;
    ZoneManager* const zoneManager_;
    
    int const numChannels_ = 0;
    int const numSends_ = 0;
    int const numFXSlots_ = 0;
    bool const banksWithOthers_ = true;
    
    vector<Widget*> widgets_;
    map<string, Widget*> widgetsByName_;
    
    map<string, CSIMessageGenerator*> CSIMessageGeneratorsByMessage_;

    virtual void SurfaceOutMonitor(Widget* widget, string address, string value);
    
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
    
    virtual string GetSourceFileName() { return ""; }
    vector<Widget*> GetWidgets() { return widgets_; }
    
    int GetNumChannels() { return numChannels_; }
    int GetNumSendSlots() { return numSends_; }
    int GetNumReceiveSlots() { return numSends_; }
    int GetNumFXSlots() { return numFXSlots_; }
    
    virtual void RequestUpdate()
    {
        zoneManager_->RequestUpdate();
    }

    virtual void ForceClearAllWidgets()
    {
        zoneManager_->ForceClearAllWidgets();
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
    int lastRValue = 0;
    int lastGValue = 0;
    int lastBValue = 0;

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
    
    virtual void Clear()
    {
        SetValue(0.0);
        SetValue(0, 0.0);
        SetValue("");
        SetRGBValue(0, 0, 0);
    }
    
    virtual void ForceClear()
    {
        ForceValue(0.0);
        ForceValue(0, 0.0);
        ForceValue("");
        ForceRGBValue(0, 0, 0);
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
    Midi_ControlSurface(CSurfIntegrator* CSurfIntegrator, Page* page, const string name, string templateFilename, string zoneFolder, int numChannels, int numSends, int numFX, int channelOffset, midi_Input* midiInput, midi_Output* midiOutput)
    : ControlSurface(CSurfIntegrator, page, name, zoneFolder, numChannels, numSends, numFX, channelOffset), templateFilename_(templateFilename), midiInput_(midiInput), midiOutput_(midiOutput)
    {
        Initialize(templateFilename, zoneFolder);
    }
    
    virtual ~Midi_ControlSurface() {}
    
    virtual string GetSourceFileName() override { return "/CSI/Surfaces/Midi/" + templateFilename_; }
    
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
    oscpkt::UdpSocket* const inSocket_ = nullptr;
    oscpkt::UdpSocket* const outSocket_ = nullptr;
    oscpkt::PacketReader packetReader_;
    oscpkt::PacketWriter packetWriter_;
    
    void Initialize(string templateFilename, string zoneFolder);
    void ProcessOSCMessage(string message, double value);

public:
    OSC_ControlSurface(CSurfIntegrator* CSurfIntegrator, Page* page, const string name, string templateFilename, string zoneFolder, int numChannels, int numSends, int numFX, int channelOffset, oscpkt::UdpSocket* inSocket, oscpkt::UdpSocket* outSocket)
    : ControlSurface(CSurfIntegrator, page, name, zoneFolder, numChannels, numSends, numFX, channelOffset), templateFilename_(templateFilename), inSocket_(inSocket), outSocket_(outSocket)
    {
        Initialize(templateFilename, zoneFolder);
    }
    
    virtual ~OSC_ControlSurface() {}
    
    virtual string GetSourceFileName() override { return "/CSI/Surfaces/OSC/" + templateFilename_; }
    
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
    bool scrollLink_ = false;
    bool vcaMode_ = false;
    int targetScrollLinkChannel_ = 0;
    int trackOffset_ = 0;
    int vcaTrackOffset_ = 0;
    vector<MediaTrack*> tracks_;
    vector<MediaTrack*> selectedTracks_;
    vector<MediaTrack*> vcaTopLeadTracks_;
    vector<MediaTrack*> vcaLeadTracks_;
    vector<MediaTrack*> vcaSpillTracks_;
    vector<Navigator*> navigators_;
    Navigator* const masterTrackNavigator_ = nullptr;
    Navigator* const selectedTrackNavigator_ = nullptr;
    Navigator* const focusedFXNavigator_ = nullptr;
    Navigator* const defaultNavigator_ = nullptr;
    
    int sendSlot_ = 0;
    int receiveSlot_ = 0;
    int fxMenuSlot_ = 0;
    
    int maxSendSlot_ = 0;
    int maxReceiveSlot_ = 0;
    int maxFXMenuSlot_ = 0;
    
    vector<string> autoModeDisplayNames__ = { "Trim", "Read", "Touch", "Write", "Latch", "LtchPre" };
    int autoModeIndex_ = 0;
    
    void SavePinnedTracks()
    {
        string pinnedTracks = "";
        
        for(int i = 0; i < navigators_.size(); i++)
        {
            if(navigators_[i]->GetIsChannelPinned())
            {
                int trackNum = CSurf_TrackToID(navigators_[i]->GetTrack(), followMCP_);
                
                pinnedTracks += to_string(i + 1) + "-" + to_string(trackNum) + "_";
            }
        }
        
        DAW:: SetProjExtState(0, "CSI", "PinnedTracks", pinnedTracks.c_str());
    }
   
public:
    TrackNavigationManager(Page* page, bool followMCP, bool synchPages, bool scrollLink, int numChannels) : page_(page), followMCP_(followMCP), synchPages_(synchPages), scrollLink_(scrollLink),
    masterTrackNavigator_(new MasterTrackNavigator(page_)),
    selectedTrackNavigator_(new SelectedTrackNavigator(page_)),
    focusedFXNavigator_(new FocusedFXNavigator(page_)),
    defaultNavigator_(new Navigator(page_))
    {
        for(int i = 0; i < numChannels; i++)
            navigators_.push_back(new TrackNavigator(page_, this, i));
    }
    
    ~TrackNavigationManager()
    {
        for(auto navigator : navigators_)
        {
            delete navigator;
            navigator = nullptr;
        }
        
        delete masterTrackNavigator_;
        delete selectedTrackNavigator_;
        delete focusedFXNavigator_;
        delete defaultNavigator_;
    }
    
    vector<MediaTrack*> &GetSelectedTracks() { return selectedTracks_; }
    bool GetSynchPages() { return synchPages_; }
    bool GetScrollLink() { return scrollLink_; }
    bool GetVCAMode() { return vcaMode_; }
    int  GetNumTracks() { return DAW::CSurf_NumTracks(followMCP_); }
    Navigator* GetMasterTrackNavigator() { return masterTrackNavigator_; }
    Navigator* GetSelectedTrackNavigator() { return selectedTrackNavigator_; }
    Navigator* GetFocusedFXNavigator() { return focusedFXNavigator_; }
    Navigator* GetDefaultNavigator() { return defaultNavigator_; }
    int GetSendSlot() { return sendSlot_; }
    int GetReceiveSlot() { return receiveSlot_; }
    int GetFXMenuSlot() { return fxMenuSlot_; }
    
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

    void ForceScrollLink()
    {
        // Make sure selected track is visble on the control surface
        MediaTrack* selectedTrack = GetSelectedTrack();
        
        if(selectedTrack != nullptr)
        {
            for(auto navigator : navigators_)
                if(selectedTrack == navigator->GetTrack())
                    return;
            
            for(int i = 0; i < tracks_.size(); i++)
                if(selectedTrack == tracks_[i])
                    trackOffset_ = i;
            
            trackOffset_ -= targetScrollLinkChannel_;
            
            if(trackOffset_ <  0)
                trackOffset_ =  0;
            
            int top = GetNumTracks() - navigators_.size();
            
            if(trackOffset_ >  top)
                trackOffset_ = top;
        }
    }
    
    void AdjustTrackBank(int amount)
    {
        if(!vcaMode_)
        {
            int numTracks = GetNumTracks();
            
            if(numTracks <= navigators_.size())
                return;
           
            trackOffset_ += amount;
            
            if(trackOffset_ <  0)
                trackOffset_ =  0;
            
            int top = numTracks - navigators_.size();
            
            if(trackOffset_ >  top)
                trackOffset_ = top;
            
            DAW:: SetProjExtState(0, "CSI", "BankIndex", to_string(trackOffset_).c_str());
        }
        else
        {
            int numTracks = vcaSpillTracks_.size() + vcaLeadTracks_.size();
            
            if(numTracks <= navigators_.size())
                return;
           
            vcaTrackOffset_ += amount;
            
            if(vcaTrackOffset_ <  0)
                vcaTrackOffset_ =  0;
            
            int top = numTracks - navigators_.size();
            
            if(vcaTrackOffset_ >  top)
                vcaTrackOffset_ = top;
        }
    }
    
    void AdjustFXMenuBank(ControlSurface* originatingSurface, int amount)
    {
        fxMenuSlot_ += amount;
        
        if(fxMenuSlot_ < 0)
            fxMenuSlot_ = maxFXMenuSlot_;
        
        if(fxMenuSlot_ > maxFXMenuSlot_)
            fxMenuSlot_ = 0;
    }
    
    void AdjustSendBank(int amount)
    {
        sendSlot_ += amount;
        
        if(sendSlot_ < 0)
            sendSlot_ = 0;
        
        if(sendSlot_ > maxSendSlot_)
            sendSlot_ = maxSendSlot_;
    }

    void AdjustReceiveBank(int amount)
    {
        receiveSlot_ += amount;
        
        if(receiveSlot_ < 0)
            receiveSlot_ = 0;
        
        if(receiveSlot_ > maxReceiveSlot_)
            receiveSlot_ = maxReceiveSlot_;
    }
    
    void TogglePin(MediaTrack* track)
    {
        for(auto navigator : navigators_)
        {
            if(track == navigator->GetTrack())
            {
                if(navigator->GetIsChannelPinned())
                    navigator->UnpinChannel();
                else
                    navigator->PinChannel();
                
                SavePinnedTracks();
                
                break;
            }
        }
    }
    
    void RestorePinnedTracks()
    {
        char buf[8192];
        
        int result = DAW::GetProjExtState(0, "CSI", "PinnedTracks", buf, sizeof(buf));
        
        if(result > 0)
        {
            istringstream kvpTokens(buf);
            string kvp;
            
            while(getline(kvpTokens, kvp, '_'))
            {
                vector<string> tokens;
                
                istringstream iss(kvp);
                string token;
                
                while(getline(iss, token, '-'))
                    tokens.push_back(token);

                if(tokens.size() == 2)
                {
                    int channelNum = atoi(tokens[0].c_str());
                    channelNum--;
                    
                    channelNum = channelNum < 0 ? 0 : channelNum;
                                        
                    int trackNum = atoi(tokens[1].c_str());
                   
                    if(MediaTrack* track =  CSurf_TrackFromID(trackNum, followMCP_))
                        if(navigators_.size() > channelNum)
                            navigators_[channelNum]->SetPinnedTrack(track);
                }
            }
        }
    }
    
    void ToggleVCAMode()
    {
        vcaMode_ = ! vcaMode_;
    }
    
    Navigator* GetNavigatorForChannel(int channelNum)
    {
        if(channelNum < navigators_.size())
            return navigators_[channelNum];
        else
            return nullptr;
    }
    
    MediaTrack* GetTrackFromChannel(int channelNumber, int bias, MediaTrack* pinnedTrack)
    {
        if(! vcaMode_)
        {
            if(pinnedTrack != nullptr)
                return pinnedTrack;
            else
                channelNumber -= bias;
            
            int trackNumber = channelNumber + trackOffset_;
            
            if(tracks_.size() > trackNumber && DAW::ValidateTrackPtr(tracks_[trackNumber]))
                return tracks_[trackNumber];
            else
                return nullptr;
        }
        else if(vcaLeadTracks_.size() == 0)
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
        
        scrollLink_ = ! scrollLink_;
        
        OnTrackSelection();
    }
    
    MediaTrack* GetSelectedTrack()
    {
        if(DAW::CountSelectedTracks(NULL) != 1)
            return nullptr;
        
        MediaTrack* track = nullptr;
        
        for(int i = 0; i <= GetNumTracks(); i++)
        {
            if(DAW::GetMediaTrackInfo_Value(GetTrackFromId(i), "I_SELECTED"))
            {
                track = GetTrackFromId(i);
                break;
            }
        }
        
        return track;
    }
    
//  Page only uses the following:
    
    void SetFXMenuSlot(int fxMenuSlot) { fxMenuSlot_ = fxMenuSlot; }
    
    void OnTrackSelection()
    {
        if(scrollLink_)
            ForceScrollLink();
    }
    
    void OnTrackListChange()
    {
        if(scrollLink_)
            ForceScrollLink();
    }
    
    void IncChannelBias(int channelNum)
    {
        for(int i = channelNum + 1; i < navigators_.size(); i++)
            navigators_[i]->IncBias();
    }
    
    void DecChannelBias(int channelNum)
    {
        for(int i = channelNum + 1; i < navigators_.size(); i++)
            navigators_[i]->DecBias();
    }

    void OnTrackSelectionBySurface(MediaTrack* track)
    {
        if(scrollLink_)
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
        
        for(auto navigator : navigators_)
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
    
    // For vcaSpillTracks_.erase -- see Clean up vcaSpillTracks below
    static bool IsTrackPointerStale(MediaTrack* track)
    {
        return ! DAW::ValidateTrackPtr(track);
    }

    void RebuildTrackList()
    {
        int top = GetNumTracks() - navigators_.size();
        
        if(top < 0)
            trackOffset_ = 0;
        else if(trackOffset_ >  top)
            trackOffset_ = top;

        tracks_.clear();
        vcaTopLeadTracks_.clear();
        vcaSpillTracks_.clear();
        selectedTracks_.clear();
        maxSendSlot_ = 0;
        maxReceiveSlot_ = 0;
        maxFXMenuSlot_ = 0;

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
            
            if(DAW::IsTrackVisible(track, followMCP_))
            {
                int maxSendSlot = DAW::GetTrackNumSends(track, 0) - 1;
                if(maxSendSlot > maxSendSlot_)
                {
                    maxSendSlot_ = maxSendSlot;
                    AdjustSendBank(0);
                }
             
                int maxReceiveSlot = DAW::GetTrackNumSends(track, -1) - 1;
                if(maxReceiveSlot > maxReceiveSlot_)
                {
                    maxReceiveSlot_ = maxReceiveSlot;
                    AdjustReceiveBank(0);
                }

                int maxFXMenuSlot = DAW::TrackFX_GetCount(track) - 1;
                if(maxFXMenuSlot > maxFXMenuSlot_)
                    maxFXMenuSlot_ = maxFXMenuSlot;
                
                if(DAW::GetMediaTrackInfo_Value(track, "I_SELECTED"))
                    selectedTracks_.push_back(track);
                
                tracks_.push_back(track);
                               
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

        if(sendSlot_ > maxSendSlot_)
            sendSlot_ = maxSendSlot_;

        if(receiveSlot_ > maxReceiveSlot_)
            receiveSlot_ = maxReceiveSlot_;

        if(fxMenuSlot_ > maxFXMenuSlot_)
            fxMenuSlot_ = maxFXMenuSlot_;
        
        for(auto navigator : navigators_)
        {
            if(navigator->GetIsChannelPinned())
            {
                if(DAW::ValidateTrackPtr(navigator->GetTrack()))
                    remove(tracks_.begin(), tracks_.end(), navigator->GetTrack());
                else
                    navigator->UnpinChannel();
            }
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
    
    TrackNavigationManager* const trackNavigationManager_ = nullptr;
    
public:
    Page(string name, bool followMCP, bool synchPages, bool scrollLink, int numChannels) : name_(name),  trackNavigationManager_(new TrackNavigationManager(this, followMCP, synchPages, scrollLink, numChannels)) {}
    
    ~Page()
    {
        for(auto surface: surfaces_)
        {
            delete surface;
            surface = nullptr;
        }
        
        delete trackNavigationManager_;
    }
    
    string GetName() { return name_; }
    
    bool GetShift() { return isShift_; }
    bool GetOption() { return isOption_; }
    bool GetControl() { return isControl_; }
    bool GetAlt() { return isAlt_; }

                   
    /*
    int repeats = 0;
    
    void Run()
    {
        int start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        trackNavigationManager_->RebuildTrackList();
        
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
             trackNavigationManager_->RebuildTrackList();
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


    void Run()
    {
        trackNavigationManager_->RebuildTrackList();
        
        for(auto surface : surfaces_)
            surface->HandleExternalInput();
        
        for(auto surface : surfaces_)
            surface->RequestUpdate();
    }

    void ForceClearAllWidgets()
    {
        for(auto surface : surfaces_)
            surface->ForceClearAllWidgets();
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
        SetModifier(value, isShift_, shiftPressedTime_);
    }
 
    void SetOption(bool value)
    {
        SetModifier(value, isOption_, optionPressedTime_);
    }
    
    void SetControl(bool value)
    {
        SetModifier(value, isControl_, controlPressedTime_);
    }
    
    void SetAlt(bool value)
    {
        SetModifier(value, isAlt_, altPressedTime_);
    }
  
    void SetModifier(bool value, bool &modifier, double &modifierPressedTime)
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
            {
                modifier = value;
            }
        }
    }

    string GetModifier()
    {
        string modifier = "";
        
        if(isShift_)
            modifier += Shift + "+";
        if(isOption_)
            modifier += Option + "+";
        if(isControl_)
            modifier +=  Control + "+";
        if(isAlt_)
            modifier += Alt + "+";
        
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
    
    void SignalActivation(ControlSurface* originatingSurface, ActivationType activationType, string zoneName)
    {
        for(auto surface : surfaces_)
            if(surface != originatingSurface)
                surface->GetZoneManager()->ReceiveActivate(activationType, zoneName);

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
    int GetSendSlot() { return trackNavigationManager_->GetSendSlot(); }
    int GetReceiveSlot() { return trackNavigationManager_->GetReceiveSlot(); }
    int GetFXMenuSlot() { return trackNavigationManager_->GetFXMenuSlot(); }
    void ForceScrollLink() { trackNavigationManager_->ForceScrollLink(); }
    void AdjustTrackBank(int amount) { trackNavigationManager_->AdjustTrackBank(amount); }
    void AdjustFXMenuBank(ControlSurface* originatingSurface, int amount) { trackNavigationManager_->AdjustFXMenuBank(originatingSurface, amount); }
    void AdjustSendBank(int amount) { trackNavigationManager_->AdjustSendBank(amount); }
    void AdjustReceiveBank(int amount) { trackNavigationManager_->AdjustReceiveBank(amount); }
    void TogglePin(MediaTrack* track) { trackNavigationManager_->TogglePin(track); }
    void RestorePinnedTracks() { trackNavigationManager_->RestorePinnedTracks(); }
    void ToggleVCAMode() { trackNavigationManager_->ToggleVCAMode(); }
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
    CSurfIntegrator* const CSurfIntegrator_ = nullptr;

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
    ~Manager()
    {
        for(auto page : pages_)
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
    
    Manager(CSurfIntegrator* CSurfIntegrator) : CSurfIntegrator_(CSurfIntegrator)
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
    
    void Shutdown()
    {
        fxParamsDisplay_ = false;
        surfaceInDisplay_ = false;
        surfaceOutDisplay_ = false;
       
        // GAW -- IMPORTANT
        // We want to stop polling, save state, and and zero out all Widgets before shutting down
        shouldRun_ = false;
        
        if(pages_.size() > 0)
            pages_[currentPageIndex_]->ForceClearAllWidgets();
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
   
    ActionContext* GetActionContext(string actionName, Widget* widget, Zone* zone, vector<string> params, vector<vector<string>> properties)
    {
        if(actions_.count(actionName) > 0)
            return new ActionContext(actions_[actionName], widget, zone, params, properties);
        else
            return new ActionContext(actions_["NoAction"], widget, zone, params, properties);
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
    
    void AdjustSendBank(Page* sendingPage, int amount)
    {
        if(! sendingPage->GetSynchPages())
            sendingPage->AdjustSendBank(amount);
        else
            for(auto page: pages_)
                if(page->GetSynchPages())
                    page->AdjustSendBank(amount);
    }
    
    void AdjustReceiveBank(Page* sendingPage, int amount)
    {
        if(! sendingPage->GetSynchPages())
            sendingPage->AdjustReceiveBank(amount);
        else
            for(auto page: pages_)
                if(page->GetSynchPages())
                    page->AdjustReceiveBank(amount);
    }
    
    void AdjustFXMenuBank(Page* sendingPage, ControlSurface* originatingSurface, int amount)
    {
        if(! sendingPage->GetSynchPages())
            sendingPage->AdjustFXMenuBank(originatingSurface, amount);
        else
            for(auto page: pages_)
                if(page->GetSynchPages())
                    page->AdjustFXMenuBank(originatingSurface, amount);
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

                if(fxParamsDisplay_)
                    DAW::ShowConsoleMsg("\n\n\tSelectedTrackNavigator\n");
                
                if(fxParamsWrite_ && fxFile.is_open())
                    fxFile << "\tSelectedTrackNavigator" + GetLineEnding();

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
