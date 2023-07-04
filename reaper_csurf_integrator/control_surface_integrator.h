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
#include <chrono>

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

const string VersionToken = "Version 3.0";
const string PageToken = "Page";
const string FollowMCPToken = "FollowMCP";
const string SynchPagesToken = "SynchPages";
const string UseScrollLinkToken = "UseScrollLink";
const string MidiSurfaceToken = "MidiSurface";
const string OSCSurfaceToken = "OSCSurface";

const string BadFileChars = "[ \\:*?<>|.,()/]";
const string CRLFChars = "[\r\n]";
const string TabChars = "[\t]";
const string BeginAutoSection = "#Begin auto generated section";
const string EndAutoSection = "#End auto generated section";

const int TempDisplayTime = 1250;

class Manager;
class ZoneManager;
extern unique_ptr<Manager> TheManager;
extern bool RemapAutoZoneDialog(shared_ptr<ZoneManager> zoneManager, string fullPath, vector<string> &fxPrologue,  vector<string> &fxEpilogue);

static vector<string> GetTokens(string line)
{
    vector<string> tokens;
    
    istringstream iss(line);
    string token;
    while (iss >> quoted(token))
        tokens.push_back(token);
    
    return tokens;
}

static int strToHex(string valueStr)
{
    return strtol(valueStr.c_str(), nullptr, 16);
}

static map<int, vector<double>> SteppedValueDictionary
{
    { 2, { 0.00, 1.00 } },
    { 3, { 0.00, 0.50, 1.00 } },
    { 4, { 0.00, 0.33, 0.67, 1.00 } },
    { 5, { 0.00, 0.25, 0.50, 0.75, 1.00 } },
    { 6, { 0.00, 0.20, 0.40, 0.60, 0.80, 1.00 } },
    { 7, { 0.00, 0.17, 0.33, 0.50, 0.67, 0.83, 1.00 } },
    { 8, { 0.00, 0.14, 0.29, 0.43, 0.57, 0.71, 0.86, 1.00 } },
    { 9, { 0.00, 0.13, 0.25, 0.38, 0.50, 0.63, 0.75, 0.88, 1.00 } },
    { 10, { 0.00, 0.11, 0.22, 0.33, 0.44, 0.56, 0.67, 0.78, 0.89, 1.00 } },
    { 11, { 0.00, 0.10, 0.20, 0.30, 0.40, 0.50, 0.60, 0.70, 0.80, 0.90, 1.00 } },
    { 12, { 0.00, 0.09, 0.18, 0.27, 0.36, 0.45, 0.55, 0.64, 0.73, 0.82, 0.91, 1.00 } },
    { 13, { 0.00, 0.08, 0.17, 0.25, 0.33, 0.42, 0.50, 0.58, 0.67, 0.75, 0.83, 0.92, 1.00 } },
    { 14, { 0.00, 0.08, 0.15, 0.23, 0.31, 0.38, 0.46, 0.54, 0.62, 0.69, 0.77, 0.85, 0.92, 1.00 } },
    { 15, { 0.00, 0.07, 0.14, 0.21, 0.29, 0.36, 0.43, 0.50, 0.57, 0.64, 0.71, 0.79, 0.86, 0.93, 1.00 } },
    { 16, { 0.00, 0.07, 0.13, 0.20, 0.27, 0.33, 0.40, 0.47, 0.53, 0.60, 0.67, 0.73, 0.80, 0.87, 0.93, 1.00 } },
    { 17, { 0.00, 0.06, 0.13, 0.19, 0.25, 0.31, 0.38, 0.44, 0.50, 0.56, 0.63, 0.69, 0.75, 0.81, 0.88, 0.94, 1.00 } },
    { 18, { 0.00, 0.06, 0.12, 0.18, 0.24, 0.29, 0.35, 0.41, 0.47, 0.53, 0.59, 0.65, 0.71, 0.76, 0.82, 0.88, 0.94, 1.00 } },
    { 19, { 0.00, 0.06, 0.11, 0.17, 0.22, 0.28, 0.33, 0.39, 0.44, 0.50, 0.56, 0.61, 0.67, 0.72, 0.78, 0.83, 0.89, 0.94, 1.00 } },
    { 20, { 0.00, 0.05, 0.11, 0.16, 0.21, 0.26, 0.32, 0.37, 0.42, 0.47, 0.53, 0.58, 0.63, 0.68, 0.74, 0.79, 0.84, 0.89, 0.95, 1.00 } },
    { 21, { 0.00, 0.05, 0.10, 0.15, 0.20, 0.25, 0.30, 0.35, 0.40, 0.45, 0.50, 0.55, 0.60, 0.65, 0.70, 0.75, 0.80, 0.85, 0.90, 0.95, 1.00 } },
    { 22, { 0.00, 0.05, 0.10, 0.14, 0.19, 0.24, 0.29, 0.33, 0.38, 0.43, 0.48, 0.52, 0.57, 0.62, 0.67, 0.71, 0.76, 0.81, 0.86, 0.90, 0.95, 1.00 } },
    { 23, { 0.00, 0.05, 0.09, 0.14, 0.18, 0.23, 0.27, 0.32, 0.36, 0.41, 0.45, 0.50, 0.55, 0.59, 0.64, 0.68, 0.73, 0.77, 0.82, 0.86, 0.91, 0.95, 1.00 } },
    { 24, { 0.00, 0.04, 0.09, 0.13, 0.17, 0.22, 0.26, 0.30, 0.35, 0.39, 0.43, 0.48, 0.52, 0.57, 0.61, 0.65, 0.70, 0.74, 0.78, 0.83, 0.87, 0.91, 0.96, 1.00 } },
    { 25, { 0.00, 0.04, 0.08, 0.13, 0.17, 0.21, 0.25, 0.29, 0.33, 0.38, 0.42, 0.46, 0.50, 0.54, 0.58, 0.63, 0.67, 0.71, 0.75, 0.79, 0.83, 0.88, 0.92, 0.96, 1.00 } },
    { 26, { 0.00, 0.04, 0.08, 0.12, 0.16, 0.20, 0.24, 0.28, 0.32, 0.36, 0.40, 0.44, 0.48, 0.52, 0.56, 0.60, 0.64, 0.68, 0.72, 0.76, 0.80, 0.84, 0.88, 0.92, 0.96, 1.00 } },
    { 27, { 0.00, 0.04, 0.08, 0.12, 0.15, 0.19, 0.23, 0.27, 0.31, 0.35, 0.38, 0.42, 0.46, 0.50, 0.54, 0.58, 0.62, 0.65, 0.69, 0.73, 0.77, 0.81, 0.85, 0.88, 0.92, 0.96, 1.00 } },
    { 28, { 0.00, 0.04, 0.07, 0.11, 0.15, 0.19, 0.22, 0.26, 0.30, 0.33, 0.37, 0.41, 0.44, 0.48, 0.52, 0.56, 0.59, 0.63, 0.67, 0.70, 0.74, 0.78, 0.81, 0.85, 0.89, 0.93, 0.96, 1.00 } },
    { 29, { 0.00, 0.04, 0.07, 0.11, 0.14, 0.18, 0.21, 0.25, 0.29, 0.32, 0.36, 0.39, 0.43, 0.46, 0.50, 0.54, 0.57, 0.61, 0.64, 0.68, 0.71, 0.75, 0.79, 0.82, 0.86, 0.89, 0.93, 0.96, 1.00 } },
    { 30, { 0.00, 0.03, 0.07, 0.10, 0.14, 0.17, 0.21, 0.24, 0.28, 0.31, 0.34, 0.38, 0.41, 0.45, 0.48, 0.52, 0.55, 0.59, 0.62, 0.66, 0.69, 0.72, 0.76, 0.79, 0.83, 0.86, 0.90, 0.93, 0.97, 1.00 } }
};

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
    shared_ptr<Page> const page_ = nullptr;
    bool isVolumeTouched_ = false;
    bool isPanTouched_ = false;
    bool isPanWidthTouched_ = false;
    bool isPanLeftTouched_ = false;
    bool isPanRightTouched_ = false;
    bool isMCUTrackPanWidth_ = false;

    Navigator(shared_ptr<Page>  page) : page_(page) {}

public:
    virtual ~Navigator() {}
    
    virtual string GetName() { return "Navigator"; }
    virtual MediaTrack* GetTrack() { return nullptr; }

    bool GetIsNavigatorTouched() { return isVolumeTouched_ || isPanTouched_ || isPanWidthTouched_ || isPanLeftTouched_ || isPanRightTouched_; }
    
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
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackNavigator : public Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    int const channelNum_ = 0;
    
protected:
    shared_ptr<TrackNavigationManager> const trackNavigationManager_;

public:
    TrackNavigator(shared_ptr<Page> page, shared_ptr<TrackNavigationManager> trackNavigationManager, int channelNum) : Navigator(page), trackNavigationManager_(trackNavigationManager), channelNum_(channelNum) {}
    virtual ~TrackNavigator() {}
    
    virtual string GetName() override { return "TrackNavigator"; }
   
    virtual MediaTrack* GetTrack() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MasterTrackNavigator : public Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    MasterTrackNavigator(shared_ptr<Page>  page) : Navigator(page) {}
    virtual ~MasterTrackNavigator() {}
    
    virtual string GetName() override { return "MasterTrackNavigator"; }
    
    virtual MediaTrack* GetTrack() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SelectedTrackNavigator : public Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    SelectedTrackNavigator(shared_ptr<Page>  page) : Navigator(page) {}
    virtual ~SelectedTrackNavigator() {}
    
    virtual string GetName() override { return "SelectedTrackNavigator"; }
    
    virtual MediaTrack* GetTrack() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FocusedFXNavigator : public Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    FocusedFXNavigator(shared_ptr<Page>  page) : Navigator(page) {}
    virtual ~FocusedFXNavigator() {}
    
    virtual string GetName() override { return "FocusedFXNavigator"; }
    
    virtual MediaTrack* GetTrack() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ActionContext
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    shared_ptr<Action> const action_ = nullptr;
    shared_ptr<Widget> const widget_ = nullptr;
    shared_ptr<Zone> const zone_ = nullptr;
    
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
    
    bool isValueInverted_ = false;
    bool isFeedbackInverted_ = false;
    double holdDelayAmount_ = 0.0;
    double delayStartTime_ = 0.0;
    double deferredValue_ = 0.0;
    
    bool supportsColor_ = false;
    vector<rgba_color> colorValues_;
    int currentColorIndex_ = 0;
    
    bool supportsTrackColor_ = false;
        
    bool provideFeedback_ = false;
    
    map<string, string> widgetProperties_;
    
    vector<string> zoneNames_;
    
    void UpdateTrackColor();

public:
    ActionContext(shared_ptr<Action> action, shared_ptr<Widget> widget, shared_ptr<Zone> zone, vector<string> params);
    ActionContext(shared_ptr<Action> action, shared_ptr<Widget> widget, shared_ptr<Zone> zone, int paramIndex) : action_(action), widget_(widget), zone_(zone), paramIndex_(paramIndex)
    {
        if(acceleratedTickValues_.size() < 1)
            acceleratedTickValues_.push_back(10);
    }
    ActionContext(shared_ptr<Action> action, shared_ptr<Widget> widget, shared_ptr<Zone> zone, string stringParam) : action_(action), widget_(widget), zone_(zone), stringParam_(stringParam)
    {
        if(acceleratedTickValues_.size() < 1)
            acceleratedTickValues_.push_back(10);
    }

    virtual ~ActionContext() {}
    
    shared_ptr<Action> GetAction() { return action_; }
    shared_ptr<Widget> GetWidget() { return widget_; }
    shared_ptr<Zone> GetZone() { return zone_; }
    int GetSlotIndex();
    string GetName();
    
    vector<string> &GetZoneNames() { return  zoneNames_; }

    int GetIntParam() { return intParam_; }
    string GetStringParam() { return stringParam_; }
    int GetCommandId() { return commandId_; }
    
    string GetFXParamDisplayName() { return fxParamDisplayName_; }
    
    MediaTrack* GetTrack();
    
    void DoRangeBoundAction(double value);
    void DoSteppedValueAction(double value);
    void DoAcceleratedSteppedValueAction(int accelerationIndex, double value);
    void DoAcceleratedDeltaValueAction(int accelerationIndex, double value);
    
    shared_ptr<Page> GetPage();
    shared_ptr<ControlSurface> GetSurface();
    int GetParamIndex() { return paramIndex_; }
       
    void SetIsValueInverted() { isValueInverted_ = true; }
    void SetIsFeedbackInverted() { isFeedbackInverted_ = true; }
    void SetHoldDelayAmount(double holdDelayAmount) { holdDelayAmount_ = holdDelayAmount * 1000.0; } // holdDelayAmount is specified in seconds, holdDelayAmount_ is in milliseconds
    void SetProvideFeedback(bool provideFeedback) { provideFeedback_ = provideFeedback; }
    
    void DoAction(double value);
    void DoRelativeAction(double value);
    void DoRelativeAction(int accelerationIndex, double value);
    
    void RequestUpdate();
    void RequestUpdateWidgetMode();
    void RunDeferredActions();
    void ClearWidget();
    void UpdateWidgetValue(double value);
    void UpdateJSFXWidgetSteppedValue(double value);
    void UpdateWidgetValue(string value);
    void UpdateColorValue(double value);

    void   SetAccelerationValues(vector<double> acceleratedDeltaValues) { acceleratedDeltaValues_ = acceleratedDeltaValues; }
    void   SetStepSize(double deltaValue) { deltaValue_ = deltaValue; }
    double GetStepSize() { return deltaValue_; }
    void   SetStepValues(vector<double> steppedValues) { steppedValues_ = steppedValues; }
    int    GetNumberOfSteppedValues() { return steppedValues_.size(); }
    void   SetTickCounts(vector<int> acceleratedTickValues) { acceleratedTickValues_ = acceleratedTickValues; }
    void   SetColorValues(vector<rgba_color> colorValues) { colorValues_ = colorValues; }

    double GetRangeMinimum() { return rangeMinimum_; }
    double GetRangeMaximum() { return rangeMaximum_; }
    
    void DoTouch(double value)
    {
        action_->Touch(this, value);
    }

    void SetRange(vector<double> range)
    {
        if(range.size() != 2)
            return;
        
        rangeMinimum_ = range[0];
        rangeMaximum_ = range[1];
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

    string GetPanValueString(double panVal, string dualPan)
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
            
            if(dualPan != "")
                trackPanValueString += dualPan;
        }
        else
        {
            if(dualPan == "")
                trackPanValueString += "   ";
            else
                trackPanValueString += "  " + dualPan;
            
            trackPanValueString += to_string(panIntVal);
            
            if(panIntVal == 100)
                trackPanValueString += ">";
            else if(panIntVal < 100 && panIntVal > 9)
                trackPanValueString += " >";
            else
                trackPanValueString += "  >";
        }
        
        if(panIntVal == 0)
        {
            if(dualPan == "")
                trackPanValueString = "  <C>  ";
            if(dualPan == "L")
                trackPanValueString = " L<C>  ";
            if(dualPan == "R")
                trackPanValueString = " <C>R  ";

        }

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
        
        if(! reversed)
            trackPanWidthString += "Wid ";
        else
            trackPanWidthString += "Rev ";
        
        trackPanWidthString += to_string(widthIntVal);
        
        if(widthIntVal == 0)
            trackPanWidthString = "<Mono> ";

        return trackPanWidthString;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Zone
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    shared_ptr<ZoneManager> const zoneManager_ = nullptr;
    shared_ptr<Navigator> const navigator_= nullptr;
    int slotIndex_ = 0;
    string const name_ = "";
    string const alias_ = "";
    string const sourceFilePath_ = "";
    
    bool isActive_ = false;
    
    map<shared_ptr<Widget>, bool> widgets_;
    map<string, shared_ptr<Widget>> widgetsByName_;
    
    vector<shared_ptr<Zone>> includedZones_;
    map<string, vector<shared_ptr<Zone>>> subZones_;
    map<string, vector<shared_ptr<Zone>>> associatedZones_;
    
    map<shared_ptr<Widget>, map<int, vector<shared_ptr<ActionContext>>>> actionContextDictionary_;
    map<shared_ptr<Widget>, int> currentActionContextModifiers_;
    vector<shared_ptr<ActionContext>> defaultContexts_;
    
    void AddNavigatorsForZone(string zoneName, vector<shared_ptr<Navigator>> &navigators);
    void UpdateCurrentActionContextModifier(shared_ptr<Widget> widget);
    
public:
    Zone(shared_ptr<ZoneManager> const zoneManager, shared_ptr<Navigator> navigator, int slotIndex, string name, string alias, string sourceFilePath, vector<string> includedZones, vector<string> associatedZones);
    
    virtual ~Zone() {}
    
    string GetSourceFilePath() { return sourceFilePath_; }
    
    shared_ptr<Navigator> GetNavigator() { return navigator_; }
    void SetSlotIndex(int index) { slotIndex_ = index; }
    bool GetIsActive() { return isActive_; }

    void InitSubZones(vector<string> subZones, shared_ptr<Zone> enclosingZone);

    void GoAssociatedZone(string associatedZoneName);
    void ReactivateFXMenuZone();

    int GetSlotIndex();
    
    void SetXTouchDisplayColors(string color);
    void RestoreXTouchDisplayColors();

    void UpdateCurrentActionContextModifiers();
    vector<shared_ptr<ActionContext>> &GetActionContexts(shared_ptr<Widget> widget);

    void Activate();
    void Deactivate();
    void ClearWidgets();
    
    void DoAction(shared_ptr<Widget> widget, bool &isUsed, double value);
    
    int GetChannelNumber();
    
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
    
    void AddWidget(shared_ptr<Widget> widget, string name)
    {
        widgets_[widget] = true;
        widgetsByName_[name] = widget;
    }
    
    shared_ptr<Widget> GetWidgetByName(string name)
    {
        if(widgetsByName_.count(name) > 0)
            return widgetsByName_[name];
        else
            return nullptr;
    }
    
    void AddActionContext(shared_ptr<Widget> widget, int modifier, shared_ptr<ActionContext> actionContext)
    {
        actionContextDictionary_[widget][modifier].push_back(actionContext);
    }
    
    virtual void GoSubZone(string subZoneName)
    {
        if(subZones_.count(subZoneName) > 0)
        {
            for(auto zone : subZones_[subZoneName])
            {
                zone->SetSlotIndex(GetSlotIndex());
                zone->Activate();
            }
        }
    }
    
    void OnTrackDeselection()
    {
        isActive_ = true;
        
        for(auto zone : includedZones_)
            zone->Activate();
       
        for(auto [key, zones] : associatedZones_)
            if(key == "SelectedTrack" || key == "SelectedTrackSend" || key == "SelectedTrackReceive" || key == "SelectedTrackFXMenu")
                for(auto zone : zones)
                    zone->Deactivate();
    }

    void RequestUpdateWidget(shared_ptr<Widget> widget)
    {
        for(auto context : GetActionContexts(widget))
        {
            context->RunDeferredActions();
            context->RequestUpdate();
        }
    }

    void RequestUpdate(map<shared_ptr<Widget>, bool> &usedWidgets)
    {
        if(! isActive_)
            return;
      
        for(auto [key, zones] : subZones_)
            for(auto zone : zones)
                zone->RequestUpdate(usedWidgets);
        
        for(auto [key, zones] : associatedZones_)
            for(auto zone : zones)
                zone->RequestUpdate(usedWidgets);

        for(auto zone : includedZones_)
            zone->RequestUpdate(usedWidgets);
        
        for(auto [widget, value] : widgets_)
        {
            if(usedWidgets[widget] == false)
            {
                usedWidgets[widget] = true;
                RequestUpdateWidget(widget);
            }
        }
    }

    void DoRelativeAction(shared_ptr<Widget> widget, bool &isUsed, double delta)
    {
        if(! isActive_ || isUsed)
            return;
        
        for(auto [key, zones] : subZones_)
            for(auto zone : zones)
                zone->DoRelativeAction(widget, isUsed, delta);

        for(auto [key, zones] : associatedZones_)
            for(auto zone : zones)
                zone->DoRelativeAction(widget, isUsed, delta);

        if(isUsed)
            return;

        if(widgets_.count(widget) > 0)
        {
            isUsed = true;

            for(auto context : GetActionContexts(widget))
                context->DoRelativeAction(delta);
        }
        else
        {
            for(auto zone : includedZones_)
                zone->DoRelativeAction(widget, isUsed, delta);
        }
    }

    void DoRelativeAction(shared_ptr<Widget> widget, bool &isUsed, int accelerationIndex, double delta)
    {
        if(! isActive_ || isUsed)
            return;

        for(auto [key, zones] : subZones_)
            for(auto zone : zones)
                zone->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
        
        for(auto [key, zones] : associatedZones_)
            for(auto zone : zones)
                zone->DoRelativeAction(widget, isUsed, accelerationIndex, delta);

        if(isUsed)
            return;

        if(widgets_.count(widget) > 0)
        {
            isUsed = true;

            for(auto context : GetActionContexts(widget))
                context->DoRelativeAction(accelerationIndex, delta);
        }
        else
        {
            for(auto zone : includedZones_)
                zone->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
        }
    }

    void DoTouch(shared_ptr<Widget> widget, string widgetName, bool &isUsed, double value)
    {
        if(! isActive_ || isUsed)
            return;

        for(auto [key, zones] : subZones_)
            for(auto zone : zones)
                zone->DoTouch(widget, widgetName, isUsed, value);
        
        for(auto [key, zones] : associatedZones_)
            for(auto zone : zones)
                zone->DoTouch(widget, widgetName, isUsed, value);

        if(isUsed)
            return;

        if(widgets_.count(widget) > 0)
        {
            isUsed = true;

            for(auto context : GetActionContexts(widget))
                context->DoTouch(value);
        }
        else
        {
            for(auto zone : includedZones_)
                zone->DoTouch(widget, widgetName, isUsed, value);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SubZone : public Zone
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    shared_ptr<Zone> const enclosingZone_ = nullptr;
    
public:
    SubZone(shared_ptr<ZoneManager> const zoneManager, shared_ptr<Navigator> navigator, int slotIndex, string name, string alias, string sourceFilePath, vector<string> includedZones, vector<string> associatedZones, shared_ptr<Zone> enclosingZone) : Zone(zoneManager, navigator, slotIndex, name, alias, sourceFilePath, includedZones, associatedZones), enclosingZone_(enclosingZone) {}

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
    shared_ptr<ControlSurface> const surface_;
    string const name_;
    vector<shared_ptr<FeedbackProcessor>> feedbackProcessors_;
    int channelNumber_ = 0;
    double lastIncomingMessageTime_ = 0.0;
       
    double stepSize_ = 0.0;
    vector<double> accelerationValues_;
    
public:
    Widget(shared_ptr<ControlSurface> surface, string name) : surface_(surface), name_(name)
    {
        int index = name.length() - 1;
        if(isdigit(name[index]))
        {
            while(isdigit(name[index]))
                index--;
               
            index++;
            
            channelNumber_ = stoi(name.substr(index, name.length() - index));
        }
    }
    
    string GetName() { return name_; }
    shared_ptr<ControlSurface> GetSurface() { return surface_; }
    shared_ptr<ZoneManager> GetZoneManager();
    int GetChannelNumber() { return channelNumber_; }
    
    void SetStepSize(double stepSize) { stepSize_ = stepSize; }
    double GetStepSize() { return stepSize_; }
    
    void SetAccelerationValues( vector<double> accelerationValues) { accelerationValues_ = accelerationValues; }
    vector<double> &GetAccelerationValues() { return accelerationValues_; }
    
    void SetIncomingMessageTime(double lastIncomingMessageTime) { lastIncomingMessageTime_ = lastIncomingMessageTime; }
    double GetLastIncomingMessageTime() { return lastIncomingMessageTime_; }
    
    void UpdateValue(map<string, string> &properties, double value);
    void UpdateValue(map<string, string> &properties, string value);
    void UpdateColorValue(rgba_color);
    void SetColorValue(map<string, string> &properties);
    void SetXTouchDisplayColors(string zoneName, string color);
    void RestoreXTouchDisplayColors();
    void Clear();
    void ForceClear();
    void LogInput(double value);
    
    void AddFeedbackProcessor(shared_ptr<FeedbackProcessor> feedbackProcessor)
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
struct CSILayoutInfo
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    string modifiers = "";
    string suffix = "";
    int channelCount = 0;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ZoneManager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    shared_ptr<ControlSurface> const surface_;
    string const zoneFolder_ = "";
    string const fxZoneFolder_ = "";

    map<string, CSIZoneInfo> zoneFilePaths_;
    
    map<shared_ptr<Widget>, bool> usedWidgets_;
    
    shared_ptr<Zone> homeZone_ = nullptr;
    
    shared_ptr<ZoneManager> sharedThisPtr_ = nullptr;
    
    bool isAutoFocusedFXMappingEnabled_ = true;
    bool isAutoFXMappingEnabled_ = true;
    
    vector<vector<string>> surfaceFXLayout_;
    vector<vector<string>> surfaceFXLayoutTemplate_;
    vector<CSILayoutInfo> fxLayouts_;
    vector<string> fxPrologue_;
    vector<string> fxEpilogue_;
    
    shared_ptr<Zone> focusedFXParamZone_ = nullptr;
    bool isFocusedFXParamMappingEnabled_ = false;
    
    map<int, map<int, int>> focusedFXDictionary_;
    vector<shared_ptr<Zone>> focusedFXZones_;
    bool isFocusedFXMappingEnabled_ = true;
    
    vector<shared_ptr<Zone>> selectedTrackFXZones_;
    vector<shared_ptr<Zone>> fxSlotZones_;
    
    map <string, map<int, vector<double>>> steppedValues_;
    
    int trackSendOffset_ = 0;
    int trackReceiveOffset_ = 0;
    int trackFXMenuOffset_ = 0;
    int selectedTrackSendOffset_ = 0;
    int selectedTrackReceiveOffset_ = 0;
    int selectedTrackFXMenuOffset_ = 0;
    int masterTrackFXMenuOffset_ = 0;

    map<string, int*> bankOffsets_ =
    {
        { "TrackSend",               &trackSendOffset_ },
        { "TrackReceive",            &trackReceiveOffset_ },
        { "TrackFXMenu",             &trackFXMenuOffset_ },
        { "SelectedTrackSend",       &selectedTrackSendOffset_ },
        { "SelectedTrackReceive",    &selectedTrackReceiveOffset_ },
        { "SelectedTrackFXMenu",     &selectedTrackFXMenuOffset_ },
        { "MasterTrackFXMenu",       &masterTrackFXMenuOffset_ },
    };
    
    bool EnsureZoneAvailable(string fxName, MediaTrack* track, int fxIndex);
    void CalculateSteppedValues(string fxName, MediaTrack* track, int fxIndex);

    void ResetOffsets()
    {
        trackSendOffset_ = 0;
        trackReceiveOffset_ = 0;
        trackFXMenuOffset_ = 0;
        selectedTrackSendOffset_ = 0;
        selectedTrackReceiveOffset_ = 0;
        selectedTrackFXMenuOffset_ = 0;
        masterTrackFXMenuOffset_ = 0;
    }
    
    void ResetSelectedTrackOffsets()
    {
        selectedTrackSendOffset_ = 0;
        selectedTrackReceiveOffset_ = 0;
        selectedTrackFXMenuOffset_ = 0;
    }
    
public:
    ZoneManager(shared_ptr<ControlSurface> surface, string zoneFolder, string fxZoneFolder) : surface_(surface), zoneFolder_(zoneFolder), fxZoneFolder_(fxZoneFolder) {}
        
    void Initialize();
    
    void PreProcessZones();
    
    bool EnsureFXZoneAvailable(string fxName, MediaTrack* track, int fxIndex);
    
    shared_ptr<Navigator> GetMasterTrackNavigator();
    shared_ptr<Navigator> GetSelectedTrackNavigator();
    shared_ptr<Navigator> GetFocusedFXNavigator();
    
    int  GetNumChannels();
    void GoFocusedFX();
    void GoSelectedTrackFX();
    void GoTrackFXSlot(MediaTrack* track, shared_ptr<Navigator> navigator, int fxSlot);
    void RemapAutoZone();

    void DoTouch(shared_ptr<Widget> widget, double value);
    
    void SetSharedThisPtr(shared_ptr<ZoneManager> thisPtr) { sharedThisPtr_ = thisPtr; }

    map<string, CSIZoneInfo> &GetZoneFilePaths() { return zoneFilePaths_; }
    vector<CSILayoutInfo> &GetFXLayouts() { return fxLayouts_; }
    vector<vector<string>> &GetSurfaceFXLayoutTemplate() { return surfaceFXLayoutTemplate_;}

    shared_ptr<ControlSurface> GetSurface() { return surface_; }
    
    void SetHomeZone(shared_ptr<Zone> zone) { homeZone_ = zone; }
    void SetFocusedFXParamZone(shared_ptr<Zone> zone) { focusedFXParamZone_ = zone; }
    
    int GetTrackSendOffset() { return trackSendOffset_; }
    int GetTrackReceiveOffset() { return trackReceiveOffset_; }
    int GetTrackFXMenuOffset() { return trackFXMenuOffset_; }
    int GetSelectedTrackSendOffset() { return selectedTrackSendOffset_; }
    int GetSelectedTrackReceiveOffset() { return selectedTrackReceiveOffset_; }
    int GetSelectedTrackFXMenuOffset() { return selectedTrackFXMenuOffset_; }
    int GetMasterTrackFXMenuOffset() { return masterTrackFXMenuOffset_; }

    bool GetIsFocusedFXMappingEnabled() { return isFocusedFXMappingEnabled_; }
    bool GetIsAutoFXMappingEnabled() { return isAutoFXMappingEnabled_; }
    bool GetIsAutoFocusedFXMappingEnabled() { return isAutoFocusedFXMappingEnabled_; }

    bool GetIsFocusedFXParamMappingEnabled() { return isFocusedFXParamMappingEnabled_; }
      
    void ClearFocusedFXParam()
    {
        if(focusedFXParamZone_ != nullptr)
        {
            focusedFXParamZone_->ClearWidgets();
            focusedFXParamZone_ = nullptr;
        }
    }
    
    void ClearFocusedFX()
    {
        for(auto zone : focusedFXZones_)
            zone->ClearWidgets();
        
        focusedFXZones_.clear();
        focusedFXDictionary_.clear();
    }
    
    void ClearSelectedTrackFX()
    {
        for(auto zone : selectedTrackFXZones_)
            zone->ClearWidgets();
        
        selectedTrackFXZones_.clear();
    }
    
    void ClearFXSlot(shared_ptr<Zone> zone)
    {
        for(int i = 0; i < fxSlotZones_.size(); i++)
        {
            if(fxSlotZones_[i]->GetName() == zone->GetName() && fxSlotZones_[i]->GetSlotIndex() == zone->GetSlotIndex())
            {
                fxSlotZones_[i]->ClearWidgets();
                fxSlotZones_.erase(fxSlotZones_.begin() + i);
                if(homeZone_ != nullptr)
                    homeZone_->ReactivateFXMenuZone();
                break;
            }
        }
    }
    
    vector<vector<string>> &GetSurfaceFXLayout(string surfaceFXLayout)
    {
        return surfaceFXLayout_;
    }
    
    void GoAssociatedZone(string zoneName)
    {
        if(homeZone_ != nullptr)
        {
            ClearFXMapping();
            ResetOffsets();
                    
            homeZone_->GoAssociatedZone(zoneName);
        }
    }

    void GoHome()
    {
        ClearFXMapping();

        if(homeZone_ != nullptr)
        {
            ResetOffsets();
            homeZone_->Activate();
        }
    }
    
    void OnTrackSelection()
    {
        fxSlotZones_.clear();
    }

    void OnTrackDeselection()
    {
        if(homeZone_ != nullptr)
        {
            ResetSelectedTrackOffsets();
            
            selectedTrackFXZones_.clear();
            
            homeZone_->OnTrackDeselection();
        }
    }
    
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

    void ToggleEnableFocusedFXMapping()
    {
        isFocusedFXMappingEnabled_ = ! isFocusedFXMappingEnabled_;
    }

    void ToggleEnableAutoFocusedFXMapping()
    {
        isAutoFocusedFXMappingEnabled_ = ! isAutoFocusedFXMappingEnabled_;
    }
    
    void ToggleEnableAutoFXMapping()
    {
        isAutoFXMappingEnabled_ = ! isAutoFXMappingEnabled_;
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
        focusedFXZones_.clear();
        selectedTrackFXZones_.clear();
        fxSlotZones_.clear();
    }
           
    void AdjustBank(string zoneName, int amount)
    {
        if(bankOffsets_.count(zoneName) > 0)
        {
            *bankOffsets_[zoneName] += amount;
            
            if(*bankOffsets_[zoneName] < 0)
                *bankOffsets_[zoneName] = 0;
        }
    }
            
    void AddWidget(shared_ptr<Widget> widget)
    {
        usedWidgets_[widget] = false;
    }

    string GetName(string name)
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
        
    void AddZoneFilePath(string fxZoneFolder, string name, struct CSIZoneInfo info)
    {
        if(fxZoneFolder == fxZoneFolder_)
            AddZoneFilePath(name, info);
    }
        
    void CheckFocusedFXState()
    {
        if(! isFocusedFXMappingEnabled_)
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
       
    void UpdateCurrentActionContextModifiers()
    {
        if(focusedFXParamZone_ != nullptr)
            focusedFXParamZone_->UpdateCurrentActionContextModifiers();
        
        for(auto zone : focusedFXZones_)
            zone->UpdateCurrentActionContextModifiers();
        
        for(auto zone : selectedTrackFXZones_)
            zone->UpdateCurrentActionContextModifiers();
        
        for(auto zone : fxSlotZones_)
            zone->UpdateCurrentActionContextModifiers();
        
        if(homeZone_ != nullptr)
            homeZone_->UpdateCurrentActionContextModifiers();
    }

    void RequestUpdate()
    {
        CheckFocusedFXState();
            
        for(auto &[key, value] : usedWidgets_)
            value = false;
        
        if(focusedFXParamZone_ != nullptr && isFocusedFXParamMappingEnabled_)
            focusedFXParamZone_->RequestUpdate(usedWidgets_);

        for(auto zone : focusedFXZones_)
            zone->RequestUpdate(usedWidgets_);
        
        for(auto zone : selectedTrackFXZones_)
            zone->RequestUpdate(usedWidgets_);
        
        for(auto zone : fxSlotZones_)
            zone->RequestUpdate(usedWidgets_);
        
        if(homeZone_ != nullptr)
            homeZone_->RequestUpdate(usedWidgets_);
        
        // default is to zero unused Widgets -- for an opposite sense device, you can override this by supplying an inverted NoAction context in the Home Zone
        map<string, string> properties;
        
        for(auto &[key, value] : usedWidgets_)
        {
            if(value == false)
            {
                rgba_color color;
                key->UpdateValue(properties, 0.0);
                key->UpdateValue(properties, "");
                key->UpdateColorValue(color);
            }
        }
    }

    void DoAction(shared_ptr<Widget> widget, double value)
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
    
    void DoRelativeAction(shared_ptr<Widget> widget, double delta)
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
    
    void DoRelativeAction(shared_ptr<Widget> widget, int accelerationIndex, double delta)
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
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    shared_ptr<Widget> const widget_;
    
public:
    CSIMessageGenerator(shared_ptr<Widget> widget) : widget_(widget) {}
    virtual ~CSIMessageGenerator() {}
    
    virtual void ProcessMessage(double value)
    {
        widget_->GetZoneManager()->DoAction(widget_, value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class AnyPress_CSIMessageGenerator : public CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    AnyPress_CSIMessageGenerator(shared_ptr<Widget> widget) : CSIMessageGenerator(widget) {}
    virtual ~AnyPress_CSIMessageGenerator() {}
    
    virtual void ProcessMessage(double value) override
    {
        widget_->GetZoneManager()->DoAction(widget_, 1.0);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MotorizedFaderWithoutTouch_CSIMessageGenerator : public CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    MotorizedFaderWithoutTouch_CSIMessageGenerator(shared_ptr<Widget> widget) : CSIMessageGenerator(widget) {}
    virtual ~MotorizedFaderWithoutTouch_CSIMessageGenerator() {}
    
    virtual void ProcessMessage(double value) override
    {
        widget_->SetIncomingMessageTime(DAW::GetCurrentNumberOfMilliseconds());
        widget_->GetZoneManager()->DoAction(widget_, value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Touch_CSIMessageGenerator : public CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    Touch_CSIMessageGenerator(shared_ptr<Widget> widget) : CSIMessageGenerator(widget) {}
    virtual ~Touch_CSIMessageGenerator() {}
    
    virtual void ProcessMessage(double value) override
    {
        widget_->GetZoneManager()->DoTouch(widget_, value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ModifierManager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    shared_ptr<Page> page_ = nullptr;
    shared_ptr<ControlSurface> surface_ = nullptr;
    
    struct Modifier
    {
        bool isEngaged = false;
        double pressedTime = 0.0;
        int value = 0;
    };
    
    enum Modifiers
    {
        Shift = 0,
        Option,
        Control,
        Alt,
        Flip,
        Global,
        Marker,
        Nudge,
        Zoom,
        Scrub
    };
    
    vector<string> modifierNames_ =
    {
        "Shift",
        "Option",
        "Control",
        "Alt",
        "Flip",
        "Global",
        "Marker",
        "Nudge",
        "Zoom",
        "Scrub"
    };

    vector<Modifier> modifiers_;
    vector<int> modifierCombinations_;
    
    vector<vector<int>> GetCombinations(vector<int> &indices)
    {
        vector<vector<int>> combinations;
        
        for (int mask = 0; mask < (1 << indices.size()); mask++)
        {
            vector<int> combination; // Stores a combination
            
            for (int position = 0; position < indices.size(); position++)
                if (mask & (1 << position))
                    combination.push_back(indices[position]);
            
            if(combination.size() > 0)
                combinations.push_back(combination);
        }
        
        return combinations;
    }

    void SetLatchModifier(bool value, Modifiers modifier);

public:
    ModifierManager()
    {
        modifierCombinations_.push_back(0);
        
        for(int i = 0; i < 10; i++)
            modifiers_.push_back(Modifier());
        
        int value = 2;
        
        for(auto &modifier : modifiers_)
            modifier.value = value *= 2;
    }
    
    ModifierManager(shared_ptr<Page> page) : ModifierManager()
    {
        page_ = page;
    }
    
    ModifierManager(shared_ptr<ControlSurface> surface) : ModifierManager()
    {
        surface_ = surface;
    }
    
    void RecalculateModifiers();
    vector<int> &GetModifiers() { return modifierCombinations_; }
    
    bool GetShift() { return modifiers_[Shift].isEngaged; }
    bool GetOption() { return modifiers_[Option].isEngaged; }
    bool GetControl() { return modifiers_[Control].isEngaged; }
    bool GetAlt() { return modifiers_[Alt].isEngaged; }
    bool GetFlip() { return modifiers_[Flip].isEngaged; }
    bool GetGlobal() { return modifiers_[Global].isEngaged; }
    bool GetMarker() { return modifiers_[Marker].isEngaged; }
    bool GetNudge() { return modifiers_[Nudge].isEngaged; }
    bool GetZoom() { return modifiers_[Zoom].isEngaged; }
    bool GetScrub() { return modifiers_[Scrub].isEngaged; }

    int GetModifierValue()
    {
        int modifierValue = 0;
        
        for(auto modifier : modifiers_)
            if(modifier.isEngaged)
                modifierValue += modifier.value;
        
        return modifierValue;
    }
    
    void SetShift(bool value)
    {
        SetLatchModifier(value, Shift);
    }
 
    void SetOption(bool value)
    {
        SetLatchModifier(value, Option);
    }
    
    void SetControl(bool value)
    {
        SetLatchModifier(value, Control);
    }
    
    void SetAlt(bool value)
    {
        SetLatchModifier(value, Alt);
    }
  
    void SetFlip(bool value)
    {
        SetLatchModifier(value, Flip);
    }
  
    void SetGlobal(bool value)
    {
        SetLatchModifier(value, Global);
    }
    
    void SetMarker(bool value)
    {
        modifiers_[Nudge].isEngaged = false;
        modifiers_[Zoom].isEngaged = false;
        modifiers_[Scrub].isEngaged = false;

        SetLatchModifier(value, Marker);
    }
    
    void SetNudge(bool value)
    {
        modifiers_[Marker].isEngaged = false;
        modifiers_[Zoom].isEngaged = false;
        modifiers_[Scrub].isEngaged = false;

        SetLatchModifier(value, Nudge);
    }
  
    void SetZoom(bool value)
    {
        modifiers_[Marker].isEngaged = false;
        modifiers_[Nudge].isEngaged = false;
        modifiers_[Scrub].isEngaged = false;

        SetLatchModifier(value, Zoom);
    }
  
    void SetScrub(bool value)
    {
        modifiers_[Marker].isEngaged = false;
        modifiers_[Nudge].isEngaged = false;
        modifiers_[Zoom].isEngaged = false;

        SetLatchModifier(value, Scrub);
    }
    
    void ClearModifiers()
    {
        for(auto &modifier : modifiers_)
            modifier.isEngaged = false;
        
        RecalculateModifiers();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ControlSurface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{    
private:
    int* scrubModePtr_ = nullptr;
    int configScrubMode_ = 0;

    bool isRewinding_ = false;
    bool isFastForwarding_ = false;
    
    bool isTextLengthRestricted_ = false;
    int restrictedTextLength_ = 6;
    
    vector<shared_ptr<FeedbackProcessor>> trackColorFeedbackProcessors_;
    vector<rgba_color> fixedTrackColors_;

    map<int, bool> channelTouches_;
    map<int, bool> channelToggles_;

protected:
    ControlSurface(shared_ptr<Page> page, const string name, int numChannels, int channelOffset) : page_(page), name_(name), numChannels_(numChannels), channelOffset_(channelOffset)
    {
        int size = 0;
        scrubModePtr_ = (int*)get_config_var("scrubmode", &size);
        
        for(int i = 1 ; i <= numChannels; i++)
        {
            channelTouches_[i] = false;
            channelToggles_[i] = false;
        }
    }

    shared_ptr<Page> const page_;
    string const name_;
    shared_ptr<ZoneManager> zoneManager_ = nullptr;
    shared_ptr<ModifierManager> modifierManager_ = nullptr;

    struct ProtectedTag {}; // to block constructor use by external code
    
    int const numChannels_ = 0;
    int const channelOffset_ = 0;
    
    vector<shared_ptr<Widget>> widgets_;
    map<string, shared_ptr<Widget>> widgetsByName_;
    
    map<string, shared_ptr<CSIMessageGenerator>> CSIMessageGeneratorsByMessage_;
    
    bool speedX5_ = false;
    
    void StopRewinding()
    {
        isRewinding_ = false;
        *scrubModePtr_ = configScrubMode_;
    
        speedX5_ = false;
    }
    
    void StopFastForwarding()
    {
        isFastForwarding_ = false;
        *scrubModePtr_ = configScrubMode_;
    
        speedX5_ = false;
    }
        
    void CancelRewindAndFastForward()
    {
        if(isRewinding_)
            StopRewinding();
        else if(isFastForwarding_)
            StopFastForwarding();
    }
    
    virtual void InitHardwiredWidgets(shared_ptr<ControlSurface> surface)
    {
        // Add the "hardwired" widgets
        AddWidget(make_shared<Widget>(surface, "OnTrackSelection"));
        AddWidget(make_shared<Widget>(surface, "OnPageEnter"));
        AddWidget(make_shared<Widget>(surface, "OnPageLeave"));
        AddWidget(make_shared<Widget>(surface, "OnInitialization"));
        AddWidget(make_shared<Widget>(surface, "OnPlayStart"));
        AddWidget(make_shared<Widget>(surface, "OnPlayStop"));
        AddWidget(make_shared<Widget>(surface, "OnRecordStart"));
        AddWidget(make_shared<Widget>(surface, "OnRecordStop"));
        AddWidget(make_shared<Widget>(surface, "OnZoneActivation"));
        AddWidget(make_shared<Widget>(surface, "OnZoneDeactivation"));
    }
    
public:
    virtual ~ControlSurface() {}
    
    void Stop();
    void Play();
    void Record();
    
    virtual void RequestUpdate();
    void ForceClearTrack(int trackNum);
    void ForceUpdateTrackColors();
    void OnTrackSelection(MediaTrack* track);
    virtual void SetHasMCUMeters(int displayType) {}
    virtual void SendOSCMessage(string zoneName) {}
    virtual void SendOSCMessage(string zoneName, int value) {}
    virtual void SendOSCMessage(string zoneName, double value) {}
    virtual void SendOSCMessage(string zoneName, string value) {}

    virtual void HandleExternalInput() {}
    virtual void UpdateTimeDisplay() {}
    
    virtual void SendMidiSysExMessage(MIDI_event_ex_t* midiMessage) {}
    virtual void SendMidiMessage(int first, int second, int third) {}
    
    shared_ptr<ZoneManager> GetZoneManager() { return zoneManager_; }
    shared_ptr<Page> GetPage() { return page_; }
    string GetName() { return name_; }
    
    int GetNumChannels() { return numChannels_; }
    int GetChannelOffset() { return channelOffset_; }
    vector<rgba_color> GetTrackColors();
    
    bool GetIsRewinding() { return isRewinding_; }
    bool GetIsFastForwarding() { return isFastForwarding_; }

    void TouchChannel(int channelNum, bool isTouched)
    {
        if(channelNum > 0 && channelNum <= numChannels_)
            channelTouches_[channelNum] = isTouched;
    }
    
    bool GetIsChannelTouched(int channelNum)
    {
        if(channelNum > 0 && channelNum <= numChannels_)
            return channelTouches_[channelNum];
        else
            return false;
    }
       
    void ToggleChannel(int channelNum)
    {
        if(channelNum > 0 && channelNum <= numChannels_)
            channelToggles_[channelNum] = ! channelToggles_[channelNum];
    }
    
    void ToggleRestrictTextLength(int length)
    {
        isTextLengthRestricted_ = ! isTextLengthRestricted_;
        restrictedTextLength_ = length;
    }
    
    string GetRestrictedLengthText(string text)
    {
        string restrictedText = text;

        if(isTextLengthRestricted_ && text.length() > restrictedTextLength_)
        {
            string firstLetter = restrictedText.substr(0, 1);
            
            restrictedText = restrictedText.substr(1, restrictedText.length() - 1);
            
            restrictedText = regex_replace(restrictedText, regex("[\\s]"), "");

            if(restrictedText.length() <= restrictedTextLength_ - 1)
                return firstLetter + restrictedText;
            
            restrictedText = regex_replace(restrictedText, regex("[`~!@#$%^&*:()_|=?;:'\",]"), "");

            if(restrictedText.length() <= restrictedTextLength_ - 1)
                return firstLetter + restrictedText;
            
            restrictedText = regex_replace(restrictedText, regex("[aeiou]"), "");

            restrictedText = firstLetter + restrictedText;
            
            if(restrictedText.length() > restrictedTextLength_)
                restrictedText = restrictedText.substr(0, restrictedTextLength_);
        }

        return restrictedText;
    }
    
    bool GetIsChannelToggled(int channelNum)
    {
        if(channelNum > 0 && channelNum <= numChannels_)
            return channelToggles_[channelNum];
        else
            return false;
    }
       
    void AddTrackColorFeedbackProcessor(shared_ptr<FeedbackProcessor> feedbackProcessor)
    {
        trackColorFeedbackProcessors_.push_back(feedbackProcessor);
    }
    
    void SetFixedTrackColors(vector<rgba_color> colors)
    {
        fixedTrackColors_.clear();
        
        for(auto color : colors)
            fixedTrackColors_.push_back(color);
    }
        
    void ForceClear()
    {
        for(auto widget : widgets_)
            widget->ForceClear();
    }
    
    void TrackFXListChanged(MediaTrack* track)
    {
        OnTrackSelection(track);
    }
    
    void HandleStop()
    {
        if(widgetsByName_.count("OnRecordStop") > 0)
            zoneManager_->DoAction(widgetsByName_["OnRecordStop"], 1.0);

        if(widgetsByName_.count("OnPlayStop") > 0)
            zoneManager_->DoAction(widgetsByName_["OnPlayStop"], 1.0);
    }
    
    void HandlePlay()
    {
        if(widgetsByName_.count("OnPlayStart") > 0)
            zoneManager_->DoAction(widgetsByName_["OnPlayStart"], 1.0);
    }
    
    void HandleRecord()
    {
        if(widgetsByName_.count("OnRecordStart") > 0)
            zoneManager_->DoAction(widgetsByName_["OnRecordStart"], 1.0);
    }
        
    void StartRewinding()
    {
        if(isFastForwarding_)
            StopFastForwarding();

        if(isRewinding_) // on 2nd, 3rd, etc. press
        {
            speedX5_ = ! speedX5_;
            return;
        }
        
        int playState = DAW::GetPlayState();
        if(playState == 1 || playState == 2 || playState == 5 || playState == 6) // playing or paused or recording or paused whilst recording
            DAW::SetEditCurPos(DAW::GetPlayPosition(), true, false);

        DAW::CSurf_OnStop();
        
        isRewinding_ = true;
        configScrubMode_ = *scrubModePtr_;
        *scrubModePtr_ = 2;
    }
       
    void StartFastForwarding()
    {
        if(isRewinding_)
            StopRewinding();

        if(isFastForwarding_) // on 2nd, 3rd, etc. press
        {
            speedX5_ = ! speedX5_;
            return;
        }
        
        int playState = DAW::GetPlayState();
        if(playState == 1 || playState == 2 || playState == 5 || playState == 6) // playing or paused or recording or paused whilst recording
            DAW::SetEditCurPos(DAW::GetPlayPosition(), true, false);

        DAW::CSurf_OnStop();
        
        isFastForwarding_ = true;
        configScrubMode_ = *scrubModePtr_;
        *scrubModePtr_ = 2;
    }
           
    void AddWidget(shared_ptr<Widget> widget)
    {
        widgets_.push_back(widget);
        widgetsByName_[widget->GetName()] = widget;
        zoneManager_->AddWidget(widget);
    }
    
    void AddCSIMessageGenerator(shared_ptr<CSIMessageGenerator> messageGenerator, string message)
    {
        CSIMessageGeneratorsByMessage_[message] = messageGenerator;
    }

    shared_ptr<Widget> GetWidgetByName(string name)
    {
        if(widgetsByName_.count(name) > 0)
            return widgetsByName_[name];
        else
            return nullptr;
    }
    
    void OnPageEnter()
    {
        ForceClear();
        
        if(widgetsByName_.count("OnPageEnter") > 0)
            zoneManager_->DoAction(widgetsByName_["OnPageEnter"], 1.0);
    }
    
    void OnPageLeave()
    {
        ForceClear();
        
        if(widgetsByName_.count("OnPageLeave") > 0)
            zoneManager_->DoAction(widgetsByName_["OnPageLeave"], 1.0);
    }
    
    void OnInitialization()
    {
        if(widgetsByName_.count("OnInitialization") > 0)
            zoneManager_->DoAction(widgetsByName_["OnInitialization"], 1.0);
    }
    
    
    bool GetShift();
    bool GetOption();
    bool GetControl();
    bool GetAlt();
    bool GetFlip();
    bool GetGlobal();
    bool GetMarker();
    bool GetNudge();
    bool GetZoom();
    bool GetScrub();

    void SetShift(bool value);
    void SetOption(bool value);
    void SetControl(bool value);
    void SetAlt(bool value);
    void SetFlip(bool value);
    void SetGlobal(bool value);
    void SetMarker(bool value);
    void SetNudge(bool value);
    void SetZoom(bool value);
    void SetScrub(bool value);
    
    vector<int> &GetModifiers();
    void ClearModifiers();
    
    void UpdateCurrentActionContextModifiers()
    {
        if(modifierManager_ == nullptr)
            GetZoneManager()->UpdateCurrentActionContextModifiers();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    double lastDoubleValue_ = 0.0;
    string lastStringValue_ = "";
    rgba_color lastColor_;
    
    shared_ptr<Widget> const widget_ = nullptr;
    
public:
    FeedbackProcessor(shared_ptr<Widget> widget) : widget_(widget) {}
    virtual ~FeedbackProcessor() {}
    virtual string GetName()  { return "FeedbackProcessor"; }
    shared_ptr<Widget> GetWidget() { return widget_; }
    virtual void SetColorValue(rgba_color color) {}
    virtual void SetInitialValues(map<string, string> &properties) {}
    virtual void ForceValue(map<string, string> &properties, double value) {}
    virtual void ForceColorValue(rgba_color color) {}
    virtual void ForceValue(map<string, string> &properties, string value) {}
    virtual void SetColorValues(rgba_color color1, rgba_color color2) {}
    virtual void UpdateTrackColors() {}
    virtual void ForceUpdateTrackColors() {}
    virtual void SetXTouchDisplayColors(string zoneName, string color) {}
    virtual void RestoreXTouchDisplayColors() {}
    virtual int GetMaxCharacters() { return 0; }

    virtual void SetValue(map<string, string> &properties, double value)
    {
        if(lastDoubleValue_ != value)
        {
            lastDoubleValue_ = value;
            ForceValue(properties, value);
        }
    }
    
    virtual void SetValue(map<string, string> &properties, string value)
    {
        if(lastStringValue_ != value)
        {
            lastStringValue_ = value;
            ForceValue(properties, value);
        }
    }

    virtual void ClearCache()
    {
        lastDoubleValue_ = 0.0;
        lastStringValue_ = "";
    }
    
    void Clear()
    {
        map<string, string> properties;
        
        rgba_color color;
        SetValue(properties, 0.0);
        SetValue(properties, "");
        SetColorValue(color);
    }
    
    void ForceClear()
    {
        map<string, string> properties;
        rgba_color color;
        ForceValue(properties, 0.0);
        ForceValue(properties, "");
        ForceColorValue(color);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Midi_CSIMessageGenerator : public CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    Midi_CSIMessageGenerator(shared_ptr<Widget> widget) : CSIMessageGenerator(widget) {}
    
public:
    virtual ~Midi_CSIMessageGenerator() {}
    virtual void ProcessMidiMessage(const MIDI_event_ex_t* midiMessage) {}
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Midi_FeedbackProcessor : public FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    shared_ptr<Midi_ControlSurface> const surface_ = nullptr;
    
    shared_ptr<MIDI_event_ex_t> lastMessageSent_ = make_shared<MIDI_event_ex_t>(0, 0, 0);
    shared_ptr<MIDI_event_ex_t> midiFeedbackMessage1_ = make_shared<MIDI_event_ex_t>(0, 0, 0);
    shared_ptr<MIDI_event_ex_t> midiFeedbackMessage2_ = make_shared<MIDI_event_ex_t>(0, 0, 0);
    
    Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget) : FeedbackProcessor(widget), surface_(surface) {}
    Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> feedback1) : FeedbackProcessor(widget), surface_(surface), midiFeedbackMessage1_(feedback1) {}
    Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> feedback1, shared_ptr<MIDI_event_ex_t> feedback2) : FeedbackProcessor(widget), surface_(surface), midiFeedbackMessage1_(feedback1), midiFeedbackMessage2_(feedback2) {}
    
    void SendMidiSysExMessage(MIDI_event_ex_t* midiMessage);
    void SendMidiMessage(int first, int second, int third);
    void ForceMidiMessage(int first, int second, int third);

public:
    virtual string GetName() override { return "Midi_FeedbackProcessor"; }

    virtual void ClearCache() override
    {
        lastMessageSent_->midi_message[0] = 0;
        lastMessageSent_->midi_message[1] = 0;
        lastMessageSent_->midi_message[2] = 0;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Midi_ControlSurfaceIO
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    string const name_ = "";
    midi_Input* const midiInput_ = nullptr;
    midi_Output* const midiOutput_ = nullptr;
    
public:
    Midi_ControlSurfaceIO(string name, midi_Input* midiInput, midi_Output* midiOutput) : name_(name), midiInput_(midiInput), midiOutput_(midiOutput) {}
    
    void HandleExternalInput(Midi_ControlSurface* surface);
    
    void SendMidiMessage(MIDI_event_ex_t* midiMessage)
    {
        if(midiOutput_)
            midiOutput_->SendMsg(midiMessage, -1);
    }

    void SendMidiMessage(int first, int second, int third)
    {
        if(midiOutput_)
            midiOutput_->Send(first, second, third, -1);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Midi_ControlSurface : public ControlSurface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    string const templateFilename_ = "";
    shared_ptr<Midi_ControlSurfaceIO> surfaceIO_ = nullptr;
    map<int, vector<shared_ptr<Midi_CSIMessageGenerator>>> Midi_CSIMessageGeneratorsByMessage_;

    // special processing for MCU meters
    bool hasMCUMeters_ = false;
    int displayType_ = 0x14;

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
    Midi_ControlSurface(shared_ptr<Page> page, const string name, int numChannels, int channelOffset, string templateFilename, shared_ptr<Midi_ControlSurfaceIO> surfaceIO, ProtectedTag)
    : ControlSurface(page, name, numChannels, channelOffset), templateFilename_(templateFilename), surfaceIO_(surfaceIO) {}
    
    static shared_ptr<Midi_ControlSurface> GetInstance(bool useLocalmodifiers, shared_ptr<Page> page, const string name, int numChannels, int channelOffset, string templateFilename, string zoneFolder, string fxZoneFolder, shared_ptr<Midi_ControlSurfaceIO> surfaceIO);

    virtual ~Midi_ControlSurface() {}
    
    void ProcessMidiMessage(const MIDI_event_ex_t* evt);
    virtual void SendMidiSysExMessage(MIDI_event_ex_t* midiMessage) override;
    virtual void SendMidiMessage(int first, int second, int third) override;

    virtual void SetHasMCUMeters(int displayType) override
    {
        hasMCUMeters_ = true;
        displayType_ = displayType;
    }
    
    virtual void HandleExternalInput() override
    {
        surfaceIO_->HandleExternalInput(this);
    }
        
    void AddCSIMessageGenerator(shared_ptr<Midi_CSIMessageGenerator> messageGenerator, int message)
    {
        Midi_CSIMessageGeneratorsByMessage_[message].push_back(messageGenerator);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_FeedbackProcessor : public FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    shared_ptr<OSC_ControlSurface> const surface_ = nullptr;
    string oscAddress_ = "";
    
public:
    OSC_FeedbackProcessor(shared_ptr<OSC_ControlSurface> surface, shared_ptr<Widget> widget, string oscAddress) : FeedbackProcessor(widget), surface_(surface), oscAddress_(oscAddress) {}
    ~OSC_FeedbackProcessor() {}

    virtual string GetName() override { return "OSC_FeedbackProcessor"; }

    virtual void SetColorValue(rgba_color color) override;
    virtual void X32SetColorValue(rgba_color color);
    virtual void ForceValue(map<string, string> &properties, double value) override;
    virtual void ForceValue(map<string, string> &properties, string value) override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_IntFeedbackProcessor : public OSC_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    OSC_IntFeedbackProcessor(shared_ptr<OSC_ControlSurface> surface, shared_ptr<Widget> widget, string oscAddress) : OSC_FeedbackProcessor(surface, widget, oscAddress) {}
    ~OSC_IntFeedbackProcessor() {}

    virtual string GetName() override { return "OSC_IntFeedbackProcessor"; }

    virtual void ForceValue(map<string, string> &properties, double value) override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_ControlSurfaceIO
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    string const name_ = "";
    shared_ptr<oscpkt::UdpSocket> inSocket_ = nullptr;
    shared_ptr<oscpkt::UdpSocket> outSocket_ = nullptr;
    oscpkt::PacketReader packetReader_;
    oscpkt::PacketWriter packetWriter_;
    const double X32HeartBeatRefreshInterval_ = 5000; // must be less than 10000
    double X32HeartBeatLastRefreshTime_ = 0.0;
    
public:
    OSC_ControlSurfaceIO(string name, string receiveOnPort, string transmitToPort, string transmitToIpAddress);

    void HandleExternalInput(OSC_ControlSurface* surface);
    
    void SendOSCMessage(string oscAddress, double value)
    {
        if(outSocket_ != nullptr && outSocket_->isOk())
        {
            oscpkt::Message message;
            message.init(oscAddress).pushFloat((float)value);
            packetWriter_.init().addMessage(message);
            outSocket_->sendPacket(packetWriter_.packetData(), packetWriter_.packetSize());
        }
    }
    
    void SendOSCMessage(string oscAddress, int value)
    {
        if(outSocket_ != nullptr && outSocket_->isOk())
        {
            oscpkt::Message message;
            message.init(oscAddress).pushInt32(value);
            packetWriter_.init().addMessage(message);
            outSocket_->sendPacket(packetWriter_.packetData(), packetWriter_.packetSize());
        }
    }
    
    void SendOSCMessage( string oscAddress, string value)
    {
        if(outSocket_ != nullptr && outSocket_->isOk())
        {
            oscpkt::Message message;
            message.init(oscAddress).pushStr(value);
            packetWriter_.init().addMessage(message);
            outSocket_->sendPacket(packetWriter_.packetData(), packetWriter_.packetSize());
        }
    }
    
    void SendOSCMessage(string value)
    {
        if(outSocket_ != nullptr && outSocket_->isOk())
        {
            oscpkt::Message message;
            message.init(value);
            packetWriter_.init().addMessage(message);
            outSocket_->sendPacket(packetWriter_.packetData(), packetWriter_.packetSize());
        }
    }
    
    void SendX32HeartBeat()
    {
        double currentTime = DAW::GetCurrentNumberOfMilliseconds();

        if(currentTime - X32HeartBeatLastRefreshTime_ > X32HeartBeatRefreshInterval_)
        {
            X32HeartBeatLastRefreshTime_ = currentTime;
            SendOSCMessage("/xremote");
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_ControlSurface : public ControlSurface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    string const templateFilename_ = "";
    shared_ptr<OSC_ControlSurfaceIO> const surfaceIO_ = nullptr;

public:
    OSC_ControlSurface(shared_ptr<Page> page, const string name, int numChannels, int channelOffset, string templateFilename, shared_ptr<OSC_ControlSurfaceIO> surfaceIO, ProtectedTag)
    : ControlSurface(page, name, numChannels, channelOffset), templateFilename_(templateFilename), surfaceIO_(surfaceIO) {}
    
    static shared_ptr<OSC_ControlSurface> GetInstance(bool useLocalmodifiers, shared_ptr<Page> page, const string name, int numChannels, int channelOffset, string templateFilename, string zoneFolder, string fxZoneFolder, shared_ptr<OSC_ControlSurfaceIO> surfaceIO);

    virtual ~OSC_ControlSurface() {}
    
    void ProcessOSCMessage(string message, double value);
    void SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, double value);
    void SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, int value);
    void SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, string value);
    virtual void SendOSCMessage(string zoneName) override;
    virtual void SendOSCMessage(string zoneName, int value) override;
    virtual void SendOSCMessage(string zoneName, double value) override;
    virtual void SendOSCMessage(string zoneName, string value) override;

    bool IsX32()
    {
        return GetName().find("X32") != string::npos || GetName().find("x32") != string::npos;
    }
    
    virtual void RequestUpdate() override
    {
        ControlSurface::RequestUpdate();

        if (IsX32())
            surfaceIO_->SendX32HeartBeat();
    }

    virtual void HandleExternalInput() override
    {
        surfaceIO_->HandleExternalInput(this);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TrackNavigationManager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    shared_ptr<Page> const page_ = nullptr;
    shared_ptr<TrackNavigationManager> sharedThisPtr_ = nullptr;
    bool followMCP_ = true;
    bool synchPages_ = true;
    bool isScrollLinkEnabled_ = false;
    bool isScrollSynchEnabled_ = false;
    int currentTrackVCAFolderMode_ = 0;
    int targetScrollLinkChannel_ = 0;
    int trackOffset_ = 0;
    int vcaTrackOffset_ = 0;
    int folderTrackOffset_ = 0;
    int selectedTracksOffset_ = 0;
    vector<MediaTrack*> tracks_;
    vector<MediaTrack*> selectedTracks_;
    
    vector<MediaTrack*> vcaTopLeadTracks_;
    MediaTrack*         vcaLeadTrack_ = nullptr;
    vector<MediaTrack*> vcaLeadTracks_;
    vector<MediaTrack*> vcaSpillTracks_;
    
    vector<MediaTrack*> folderTopParentTracks_;
    MediaTrack*         folderParentTrack_ = nullptr;
    vector<MediaTrack*> folderParentTracks_;
    vector<MediaTrack*> folderSpillTracks_;
    map<MediaTrack*,    vector<MediaTrack*>> folderDictionary_;

    map<int, shared_ptr<Navigator> > trackNavigators_;
    shared_ptr<Navigator> const masterTrackNavigator_ = nullptr;
    shared_ptr<Navigator> selectedTrackNavigator_ = nullptr;
    shared_ptr<Navigator> focusedFXNavigator_ = nullptr;
    
    vector<string> autoModeDisplayNames__ = { "Trim", "Read", "Touch", "Write", "Latch", "LtchPre" };
    
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
                {
                    trackOffset_ = i - 1;
                    break;
                }
            
            trackOffset_ -= targetScrollLinkChannel_;
            
            if(trackOffset_ <  0)
                trackOffset_ =  0;
            
            int top = GetNumTracks() - trackNavigators_.size();
            
            if(trackOffset_ >  top)
                trackOffset_ = top;
        }
    }
    
public:
    TrackNavigationManager(shared_ptr<Page> page, bool followMCP,  bool synchPages, bool isScrollLinkEnabled, bool isScrollSynchEnabled) :
    page_(page),
    followMCP_(followMCP),
    synchPages_(synchPages),
    isScrollLinkEnabled_(isScrollLinkEnabled),
    isScrollSynchEnabled_(isScrollSynchEnabled),
    masterTrackNavigator_(make_shared<MasterTrackNavigator>(page_)),
    selectedTrackNavigator_(make_shared<SelectedTrackNavigator>(page_)),
    focusedFXNavigator_(make_shared<FocusedFXNavigator>(page_))
    {}
       
    void SetSharedThisPtr(shared_ptr<TrackNavigationManager> thisPtr) { sharedThisPtr_ = thisPtr; }
    
    void RebuildTracks();
    void RebuildSelectedTracks();
    void AdjustSelectedTrackBank(int amount);
    bool GetSynchPages() { return synchPages_; }
    bool GetScrollLink() { return isScrollLinkEnabled_; }
    int  GetNumTracks() { return DAW::CSurf_NumTracks(followMCP_); }
    shared_ptr<Navigator> GetMasterTrackNavigator() { return masterTrackNavigator_; }
    shared_ptr<Navigator> GetSelectedTrackNavigator() { return selectedTrackNavigator_; }
    shared_ptr<Navigator> GetFocusedFXNavigator() { return focusedFXNavigator_; }
    
    bool GetIsTrackVisible(MediaTrack* track)
    {
        return DAW::IsTrackVisible(track, followMCP_);
    }
    
    void VCAModeActivated()
    {
        currentTrackVCAFolderMode_ = 1;
    }
    
    void FolderModeActivated()
    {
        currentTrackVCAFolderMode_ = 2;
    }
    
    void SelectedTracksModeActivated()
    {
        currentTrackVCAFolderMode_ = 3;
    }
    
    void VCAModeDeactivated()
    {
        if(currentTrackVCAFolderMode_ == 1)
            currentTrackVCAFolderMode_ = 0;
    }
    
    void FolderModeDeactivated()
    {
        if(currentTrackVCAFolderMode_ == 2)
            currentTrackVCAFolderMode_ = 0;
    }
    
    void SelectedTracksModeDeactivated()
    {
        if(currentTrackVCAFolderMode_ == 3)
            currentTrackVCAFolderMode_ = 0;
    }
    
    string GetCurrentTrackVCAFolderModeDisplay()
    {
        if(currentTrackVCAFolderMode_ == 0)
            return "Track";
        else if(currentTrackVCAFolderMode_ == 1)
            return "VCA";
        else if(currentTrackVCAFolderMode_ == 2)
            return "Folder";
        else if(currentTrackVCAFolderMode_ == 3)
            return "SelectedTracks";
        else
            return "";
    }

    string GetAutoModeDisplayName(int modeIndex)
    {
        int globalOverride = DAW::GetGlobalAutomationOverride();

        if(globalOverride > -1) // -1=no override, 0=trim/read, 1=read, 2=touch, 3=write, 4=latch, 5=bypass
            return autoModeDisplayNames__[globalOverride];
        else
            return autoModeDisplayNames__[modeIndex];
    }

    string GetGlobalAutoModeDisplayName()
    {
        int globalOverride = DAW::GetGlobalAutomationOverride();

        if(globalOverride == -1)
            return "NoOverride";
        else if(globalOverride > -1) // -1=no override, 0=trim/read, 1=read, 2=touch, 3=write, 4=latch, 5=bypass
            return autoModeDisplayNames__[globalOverride];
        else
            return "";
    }

    void NextInputMonitorMode(MediaTrack* track)
    {
        // I_RECMON : int * : record monitor (0=off, 1=normal, 2=not when playing (tapestyle))
        int recMonitorMode = DAW::GetMediaTrackInfo_Value(track,"I_RECMON");

        // I_RECMONITEMS : int * : monitor items while recording (0=off, 1=on)
        int recMonitorItemMode = DAW::GetMediaTrackInfo_Value(track,"I_RECMONITEMS");

        if(recMonitorMode == 0)
        {
            recMonitorMode = 1;
            recMonitorItemMode = 0;
        }
        else if(recMonitorMode == 1 && recMonitorItemMode == 0)
        {
            recMonitorMode = 2;
            recMonitorItemMode = 0;
        }
        else if(recMonitorMode == 2 && recMonitorItemMode == 0)
        {
            recMonitorMode = 1;
            recMonitorItemMode = 1;
        }
        else if(recMonitorMode == 1 && recMonitorItemMode == 1)
        {
            recMonitorMode = 2;
            recMonitorItemMode = 1;
        }
        else if(recMonitorMode == 2 && recMonitorItemMode == 1)
        {
            recMonitorMode = 0;
            recMonitorItemMode = 0;
        }

        DAW::GetSetMediaTrackInfo(track, "I_RECMON", &recMonitorMode);
        DAW::GetSetMediaTrackInfo(track, "I_RECMONITEMS", &recMonitorItemMode);
    }
    
    string GetCurrentInputMonitorMode(MediaTrack* track)
    {
        // I_RECMON : int * : record monitor (0=off, 1=normal, 2=not when playing (tapestyle))
        int recMonitorMode = DAW::GetMediaTrackInfo_Value(track,"I_RECMON");

        // I_RECMONITEMS : int * : monitor items while recording (0=off, 1=on)
        int recMonitorItemMode = DAW::GetMediaTrackInfo_Value(track,"I_RECMONITEMS");

        if(recMonitorMode == 0)
            return "Off";
        else if(recMonitorMode == 1 && recMonitorItemMode == 0)
            return "Input";
        else if(recMonitorMode == 2 && recMonitorItemMode == 0)
            return "Auto";
        else if(recMonitorMode == 1 && recMonitorItemMode == 1)
            return "Input+";
        else if(recMonitorMode == 2 && recMonitorItemMode == 1)
            return "Auto+";
        else
            return "";
    }
    
    vector<MediaTrack*> &GetSelectedTracks()
    {
        selectedTracks_.clear();
        
        for(int i = 0; i < DAW::CountSelectedTracks(); i++)
            selectedTracks_.push_back(DAW::GetSelectedTrack(i));
        
        return selectedTracks_;
    }

    void SetTrackOffset(int trackOffset)
    {
        if(isScrollSynchEnabled_)
            trackOffset_ = trackOffset;
    }
    
    void AdjustTrackBank(int amount)
    {
        if(currentTrackVCAFolderMode_ != 0)
            return;

        int numTracks = tracks_.size();
        
        if(numTracks <= trackNavigators_.size())
            return;
       
        trackOffset_ += amount;
        
        if(trackOffset_ <  0)
            trackOffset_ =  0;
        
        int top = numTracks - trackNavigators_.size();
        
        if(trackOffset_ >  top)
            trackOffset_ = top;
        
        if(isScrollSynchEnabled_)
        {
            int offset = trackOffset_;
            
            offset++;
            
            if(MediaTrack* leftmostTrack = DAW::GetTrack(offset))
                DAW::SetMixerScroll(leftmostTrack);
        }
    }
    
    void AdjustVCABank(int amount)
    {
        if(currentTrackVCAFolderMode_ != 1)
            return;

        int numTracks = vcaSpillTracks_.size();
            
        if(numTracks <= trackNavigators_.size())
            return;
       
        vcaTrackOffset_ += amount;
        
        if(vcaTrackOffset_ <  0)
            vcaTrackOffset_ =  0;
        
        int top = numTracks - trackNavigators_.size();
        
        if(vcaTrackOffset_ >  top)
            vcaTrackOffset_ = top;
    }
    
    void AdjustFolderBank(int amount)
    {
        if(currentTrackVCAFolderMode_ != 2)
            return;

        int numTracks = folderSpillTracks_.size();
        
        if(numTracks <= trackNavigators_.size())
            return;
       
        folderTrackOffset_ += amount;
        
        if(folderTrackOffset_ <  0)
            folderTrackOffset_ =  0;
        
        int top = numTracks - trackNavigators_.size();
        
        if(folderTrackOffset_ >  top)
            folderTrackOffset_ = top;
    }
    
    void AdjustSelectedTracksBank(int amount)
    {
        if(currentTrackVCAFolderMode_ != 3)
            return;

        int numTracks = selectedTracks_.size();
       
        if(numTracks <= trackNavigators_.size())
            return;
        
        selectedTracksOffset_ += amount;
        
        if(selectedTracksOffset_ < 0)
            selectedTracksOffset_ = 0;
        
        int top = numTracks - trackNavigators_.size();
        
        if(selectedTracksOffset_ > top)
            selectedTracksOffset_ = top;
    }
    
    shared_ptr<Navigator> GetNavigatorForChannel(int channelNum)
    {
        if(sharedThisPtr_ != nullptr)
        {
            if(trackNavigators_.count(channelNum) < 1)
                trackNavigators_[channelNum] = make_shared<TrackNavigator>(page_, sharedThisPtr_, channelNum);
            
            return trackNavigators_[channelNum];
        }
        else
            return nullptr;
    }
    
    MediaTrack* GetTrackFromChannel(int channelNumber)
    {       
        if(currentTrackVCAFolderMode_ == 0)
        {
            if(channelNumber + trackOffset_ < GetNumTracks() && channelNumber + trackOffset_ < tracks_.size())
                return tracks_[channelNumber + trackOffset_];
            else
                return nullptr;
        }
        else if(currentTrackVCAFolderMode_ == 1)
        {
            if(vcaLeadTrack_ == nullptr)
            {
                if(channelNumber < vcaTopLeadTracks_.size() && DAW::ValidateTrackPtr(vcaTopLeadTracks_[channelNumber]))
                    return vcaTopLeadTracks_[channelNumber];
                else
                    return nullptr;
            }
            else
            {
                if(channelNumber == 0 && vcaSpillTracks_.size() > 0 && DAW::ValidateTrackPtr(vcaSpillTracks_[channelNumber]))
                    return vcaSpillTracks_[channelNumber];
                else if(vcaTrackOffset_ == 0 && channelNumber < vcaSpillTracks_.size() && DAW::ValidateTrackPtr(vcaSpillTracks_[channelNumber]))
                    return vcaSpillTracks_[channelNumber];
                else
                {
                    channelNumber += vcaTrackOffset_;
                    
                    if(channelNumber < vcaSpillTracks_.size() && DAW::ValidateTrackPtr(vcaSpillTracks_[channelNumber]))
                        return vcaSpillTracks_[channelNumber];
                }
            }
        }
        else if(currentTrackVCAFolderMode_ == 2)
        {
            if(folderParentTrack_ == nullptr)
            {
                if(channelNumber < folderTopParentTracks_.size() && DAW::ValidateTrackPtr(folderTopParentTracks_[channelNumber]))
                    return folderTopParentTracks_[channelNumber];
                else
                    return nullptr;
            }
            else
            {
                if(channelNumber == 0 && folderSpillTracks_.size() > 0 && DAW::ValidateTrackPtr(folderSpillTracks_[channelNumber]))
                    return folderSpillTracks_[channelNumber];
                else if(folderTrackOffset_ == 0 && channelNumber < folderSpillTracks_.size() && DAW::ValidateTrackPtr(folderSpillTracks_[channelNumber]))
                    return folderSpillTracks_[channelNumber];
                else
                {
                    channelNumber += folderTrackOffset_;
                    
                    if(channelNumber < folderSpillTracks_.size() && DAW::ValidateTrackPtr(folderSpillTracks_[channelNumber]))
                        return folderSpillTracks_[channelNumber];
                }
            }
        }
        else if(currentTrackVCAFolderMode_ == 3)
        {
            if(channelNumber + selectedTracksOffset_ >= selectedTracks_.size())
                return nullptr;
            else
                return selectedTracks_[channelNumber + selectedTracksOffset_];
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
    
    bool GetIsVCASpilled(MediaTrack* track)
    {
        if(vcaLeadTrack_ == nullptr && (DAW::GetTrackGroupMembership(track, "VOLUME_VCA_LEAD") != 0 || DAW::GetTrackGroupMembershipHigh(track, "VOLUME_VCA_LEAD") != 0))
            return true;
        else if(vcaLeadTrack_ == track)
            return true;
        else
            return false;
    }
    
    void ToggleVCASpill(MediaTrack* track)
    {
        if(currentTrackVCAFolderMode_ != 1)
            return;
        
        if(DAW::GetTrackGroupMembership(track, "VOLUME_VCA_LEAD") == 0 && DAW::GetTrackGroupMembershipHigh(track, "VOLUME_VCA_LEAD") == 0)
            return;

        if(vcaLeadTrack_ == track)
        {
            if(vcaLeadTracks_.size() > 0)
            {
                vcaLeadTrack_ = vcaLeadTracks_.back();
                vcaLeadTracks_.pop_back();
            }
            else
                vcaLeadTrack_ = nullptr;
        }
        else if(vcaLeadTrack_ != nullptr)
        {
            vcaLeadTracks_.push_back(vcaLeadTrack_);
            vcaLeadTrack_ = track;
        }
        else
            vcaLeadTrack_ = track;
       
        vcaTrackOffset_ = 0;
    }

    bool GetIsFolderSpilled(MediaTrack* track)
    {
        if(find(folderTopParentTracks_.begin(), folderTopParentTracks_.end(), track) != folderTopParentTracks_.end())
            return true;
        else if(DAW::GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH") == 1)
            return true;
        else
            return false;
    }

    void ToggleFolderSpill(MediaTrack* track)
    {
        if(currentTrackVCAFolderMode_ != 2)
            return;
        
        if(folderTopParentTracks_.size() == 0)
            return;

        else if(DAW::GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH") != 1)
            return;
        
        if(folderParentTrack_ == track)
        {
            if(folderParentTracks_.size() > 0)
            {
                folderParentTrack_ = folderParentTracks_.back();
                folderParentTracks_.pop_back();
            }
            else
                folderParentTrack_ = nullptr;
        }
        else if(folderParentTrack_ != nullptr)
        {
            folderParentTracks_.push_back(folderParentTrack_);
            folderParentTrack_ = track;
        }
        else
            folderParentTrack_ = track;
       
        folderTrackOffset_ = 0;
    }
    
    void ToggleSynchPages()
    {
        synchPages_ = ! synchPages_;
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
        if(isScrollLinkEnabled_ && tracks_.size() > trackNavigators_.size())
            ForceScrollLink();
    }
    
    void OnTrackListChange()
    {
        if(isScrollLinkEnabled_ && tracks_.size() > trackNavigators_.size())
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
    
    bool GetIsNavigatorTouched(shared_ptr<Navigator> navigator,  int touchedControl)
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
        if(currentTrackVCAFolderMode_ != 1)
            return;
    
        vcaTopLeadTracks_.clear();
        vcaSpillTracks_.clear();
        
        bitset<32> leadTrackVCALeaderGroup;
        bitset<32> leadTrackVCALeaderGroupHigh;
        
        if(vcaLeadTrack_ != nullptr)
        {
            leadTrackVCALeaderGroup = DAW::GetTrackGroupMembership(vcaLeadTrack_, "VOLUME_VCA_LEAD");
            leadTrackVCALeaderGroupHigh = DAW::GetTrackGroupMembershipHigh(vcaLeadTrack_, "VOLUME_VCA_LEAD");
            vcaSpillTracks_.push_back(vcaLeadTrack_);
        }
        
        // Get Visible Tracks
        for (int i = 1; i <= GetNumTracks(); i++)
        {
            MediaTrack* track = DAW::CSurf_TrackFromID(i, followMCP_);
            
            if(DAW::GetTrackGroupMembership(track, "VOLUME_VCA_LEAD") != 0 && DAW::GetTrackGroupMembership(track, "VOLUME_VCA_FOLLOW") == 0)
                vcaTopLeadTracks_.push_back(track);
            
            if(DAW::GetTrackGroupMembershipHigh(track, "VOLUME_VCA_LEAD") != 0 && DAW::GetTrackGroupMembershipHigh(track, "VOLUME_VCA_FOLLOW") == 0)
                vcaTopLeadTracks_.push_back(track);
            
            if(vcaLeadTrack_ != nullptr)
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
        if(currentTrackVCAFolderMode_ != 2)
            return;
        
        folderTopParentTracks_.clear();
        folderDictionary_.clear();
        folderSpillTracks_.clear();
       
        vector<vector<MediaTrack*>*> currentDepthTracks;
        
        for (int i = 1; i <= GetNumTracks(); i++)
        {
            MediaTrack* track = DAW::CSurf_TrackFromID(i, followMCP_);

            if(DAW::GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH") == 1)
            {
                if(currentDepthTracks.size() == 0)
                    folderTopParentTracks_.push_back(track);
                else
                    currentDepthTracks.back()->push_back(track);
                
                folderDictionary_[track].push_back(track);
                
                currentDepthTracks.push_back(&folderDictionary_[track]);
            }
            else if(DAW::GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH") == 0 && currentDepthTracks.size() > 0)
            {
                currentDepthTracks.back()->push_back(track);
            }
            else if(DAW::GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH") < 0 && currentDepthTracks.size() > 0)
            {
                currentDepthTracks.back()->push_back(track);
                
                int folderBackTrack = -DAW::GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH");
                
                for(int i = 0; i < folderBackTrack && currentDepthTracks.size() > 0; i++)
                    currentDepthTracks.pop_back();
            }
        }
        
        if(folderParentTrack_ != nullptr)
            for(auto track : folderDictionary_[folderParentTrack_])
                folderSpillTracks_.push_back(track);
    }
    
    void EnterPage()
    {
        /*
         if(colorTracks_)
         {
         // capture track colors
         for(auto* navigator : trackNavigators_)
         if(MediaTrack* track = DAW::GetTrackFromGUID(navigator->GetTrackGUID(), followMCP_))
         trackColors_[navigator->GetTrackGUID()] = DAW::GetTrackColor(track);
         }
         */
    }
    
    void LeavePage()
    {
        /*
         if(colorTracks_)
         {
         DAW::PreventUIRefresh(1);
         // reset track colors
         for(auto* navigator : trackNavigators_)
         if(MediaTrack* track = DAW::GetTrackFromGUID(navigator->GetTrackGUID(), followMCP_))
         if(trackColors_.count(navigator->GetTrackGUID()) > 0)
         DAW::GetSetMediaTrackInfo(track, "I_CUSTOMCOLOR", &trackColors_[navigator->GetTrackGUID()]);
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
    string const name_ = "";
    shared_ptr<TrackNavigationManager> trackNavigationManager_ = nullptr;
    shared_ptr<ModifierManager> modifierManager_ = nullptr;
    vector<shared_ptr<ControlSurface>> surfaces_;
    
    struct PrivateTag {}; // to block constructor use by external code
    
public:
    Page(string name, PrivateTag) : name_(name) {}

    static shared_ptr<Page> GetInstance(string name, bool followMCP,  bool synchPages, bool isScrollLinkEnabled, bool isScrollSynchEnabled)
    {
        shared_ptr<Page> instance = make_shared<Page>(name, PrivateTag{});

        instance->trackNavigationManager_ = make_shared<TrackNavigationManager>(instance, followMCP, synchPages, isScrollLinkEnabled, isScrollSynchEnabled);
        instance->trackNavigationManager_->SetSharedThisPtr(instance->trackNavigationManager_);
        
        instance->modifierManager_ = make_shared<ModifierManager>(instance);
        
        return instance;
    }
    
    string GetName() { return name_; }
    
    shared_ptr<ModifierManager> GetModifierManager() { return modifierManager_; }
    
    void AddSurface(shared_ptr<ControlSurface> surface)
    {
        return surfaces_.push_back(surface);
    }
    
    void UpdateCurrentActionContextModifiers()
    {
        for(auto surface : surfaces_)
            surface->UpdateCurrentActionContextModifiers();
    }
    
    void ForceClear()
    {
        for(auto surface : surfaces_)
            surface->ForceClear();
    }
    
    void ForceClearTrack(int trackNum)
    {
        for(auto surface : surfaces_)
            surface->ForceClearTrack(trackNum);
    }
    
    void ForceUpdateTrackColors()
    {
        for(auto surface : surfaces_)
            surface->ForceUpdateTrackColors();
    }
        
    bool GetTouchState(MediaTrack* track, int touchedControl)
    {
        return trackNavigationManager_->GetIsControlTouched(track, touchedControl);
    }
    
    void OnTrackSelection(MediaTrack* track)
    {
        trackNavigationManager_->OnTrackSelection();
        
        for(auto surface : surfaces_)
            surface->OnTrackSelection(track);
    }
    
    void OnTrackListChange()
    {
        trackNavigationManager_->OnTrackListChange();
    }
    
    void OnTrackSelectionBySurface(MediaTrack* track)
    {
        trackNavigationManager_->OnTrackSelectionBySurface(track);
        
        for(auto surface : surfaces_)
            surface->OnTrackSelection(track);
    }
    
    void TrackFXListChanged(MediaTrack* track)
    {
        for(auto surface : surfaces_)
            surface->TrackFXListChanged(track);
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
    
    void SignalStop()
    {
        for(auto surface : surfaces_)
            surface->HandleStop();
    }
    
    void SignalPlay()
    {
        for(auto surface : surfaces_)
            surface->HandlePlay();
    }
    
    void SignalRecord()
    {
        for(auto surface : surfaces_)
            surface->HandleRecord();
    }
    
    void GoTrackFXSlot(MediaTrack* track, shared_ptr<Navigator> navigator, int fxSlot)
    {
        for(auto surface : surfaces_)
            surface->GetZoneManager()->GoTrackFXSlot(track, navigator, fxSlot);
    }
    
    void ClearFocusedFXParam()
    {
        for(auto surface : surfaces_)
            surface->GetZoneManager()->ClearFocusedFXParam();
    }

    void ClearFocusedFX()
    {
        for(auto surface : surfaces_)
            surface->GetZoneManager()->ClearFocusedFX();
    }

    void ClearSelectedTrackFX()
    {
        for(auto surface : surfaces_)
            surface->GetZoneManager()->ClearSelectedTrackFX();
    }

    void ClearFXSlot(shared_ptr<Zone> zone)
    {
        for(auto surface : surfaces_)
            surface->GetZoneManager()->ClearFXSlot(zone);
    }

    void GoHome()
    {
        for(auto surface : surfaces_)
            surface->GetZoneManager()->GoHome();
    }
    
    void GoAssociatedZone(string zoneName)
    {
        for(auto surface : surfaces_)
            surface->GetZoneManager()->GoAssociatedZone(zoneName);
    }
    
    void AdjustBank(string zoneName, int amount)
    {
        if(zoneName == "Track")
            trackNavigationManager_->AdjustTrackBank(amount);
        else if(zoneName == "VCA")
            trackNavigationManager_->AdjustVCABank(amount);
        else if(zoneName == "Folder")
            trackNavigationManager_->AdjustFolderBank(amount);
        else if(zoneName == "SelectedTracks")
            trackNavigationManager_->AdjustSelectedTracksBank(amount);
        else if(zoneName == "SelectedTrack")
            trackNavigationManager_->AdjustSelectedTrackBank(amount);
        else
            for(auto surface : surfaces_)
                surface->GetZoneManager()->AdjustBank(zoneName, amount);
    }
    
    void AddZoneFilePath(shared_ptr<ControlSurface> originatingSurface, string zoneFolder, string name, struct CSIZoneInfo info)
    {
        for(auto surface : surfaces_)
            if(surface != originatingSurface)
                surface->GetZoneManager()->AddZoneFilePath(zoneFolder, name, info);
    }
    
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Page facade for TrackNavigationManager
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    bool GetSynchPages() { return trackNavigationManager_->GetSynchPages(); }
    bool GetScrollLink() { return trackNavigationManager_->GetScrollLink(); }
    int  GetNumTracks() { return trackNavigationManager_->GetNumTracks(); }
    shared_ptr<Navigator> GetMasterTrackNavigator() { return trackNavigationManager_->GetMasterTrackNavigator(); }
    shared_ptr<Navigator>  GetSelectedTrackNavigator() { return trackNavigationManager_->GetSelectedTrackNavigator(); }
    shared_ptr<Navigator>  GetFocusedFXNavigator() { return trackNavigationManager_->GetFocusedFXNavigator(); }
    void VCAModeActivated() { trackNavigationManager_->VCAModeActivated(); }
    void VCAModeDeactivated() { trackNavigationManager_->VCAModeDeactivated(); }
    void FolderModeActivated() { trackNavigationManager_->FolderModeActivated(); }
    void FolderModeDeactivated() { trackNavigationManager_->FolderModeDeactivated(); }
    void SelectedTracksModeActivated() { trackNavigationManager_->SelectedTracksModeActivated(); }
    void SelectedTracksModeDeactivated() { trackNavigationManager_->SelectedTracksModeDeactivated(); }
    shared_ptr<Navigator>  GetNavigatorForChannel(int channelNum) { return trackNavigationManager_->GetNavigatorForChannel(channelNum); }
    MediaTrack* GetTrackFromId(int trackNumber) { return trackNavigationManager_->GetTrackFromId(trackNumber); }
    int GetIdFromTrack(MediaTrack* track) { return trackNavigationManager_->GetIdFromTrack(track); }
    bool GetIsTrackVisible(MediaTrack* track) { return trackNavigationManager_->GetIsTrackVisible(track); }
    bool GetIsVCASpilled(MediaTrack* track) { return trackNavigationManager_->GetIsVCASpilled(track); }
    void ToggleVCASpill(MediaTrack* track) { trackNavigationManager_->ToggleVCASpill(track); }
    bool GetIsFolderSpilled(MediaTrack* track) { return trackNavigationManager_->GetIsFolderSpilled(track); }
    void ToggleFolderSpill(MediaTrack* track) { trackNavigationManager_->ToggleFolderSpill(track); }
    void ToggleScrollLink(int targetChannel) { trackNavigationManager_->ToggleScrollLink(targetChannel); }
    void ToggleSynchPages() { trackNavigationManager_->ToggleSynchPages(); }
    void SetTrackOffset(int offset) { trackNavigationManager_->SetTrackOffset(offset); }
    MediaTrack* GetSelectedTrack() { return trackNavigationManager_->GetSelectedTrack(); }
    void NextInputMonitorMode(MediaTrack* track) { trackNavigationManager_->NextInputMonitorMode(track); }
    string GetAutoModeDisplayName(int modeIndex) { return trackNavigationManager_->GetAutoModeDisplayName(modeIndex); }
    string GetGlobalAutoModeDisplayName() { return trackNavigationManager_->GetGlobalAutoModeDisplayName(); }
    string GetCurrentInputMonitorMode(MediaTrack* track) { return trackNavigationManager_->GetCurrentInputMonitorMode(track); }
    vector<MediaTrack*> &GetSelectedTracks() { return trackNavigationManager_->GetSelectedTracks(); }
    
    
    /*
    int repeats = 0;
    
    void Run()
    {
        int start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        trackNavigationManager_->RebuildTracks();
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
             trackNavigationManager_->RebuildTracks();
             trackNavigationManager_->RebuildVCASpill();
             trackNavigationManager_->RebuildFolderTracks();
             int duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - start;
             totalDuration += duration;
             ShowDuration("Rebuild Track/VCA/Folder List", duration);
             
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
        trackNavigationManager_->RebuildTracks();
        trackNavigationManager_->RebuildVCASpill();
        trackNavigationManager_->RebuildFolderTracks();
        trackNavigationManager_->RebuildSelectedTracks();
        
        for(auto surface : surfaces_)
            surface->HandleExternalInput();
        
        for(auto surface : surfaces_)
            surface->RequestUpdate();
    }
//*/
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Manager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    map<string, shared_ptr<Action>> actions_;

    vector <shared_ptr<Page>> pages_;
    
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
    
    double *projectMetronomePrimaryVolumePtr_ = nullptr;
    double *projectMetronomeSecondaryVolumePtr_ = nullptr;
    
    map<string, map<int, string>> fxParamAliases_;
    map<string, map<int, int>> fxParamSteppedValueCounts_;
    
    map<int, int> baseTickCounts_ ;
    
    void InitActionsDictionary();

    void InitFXParamAliases();
    void InitFXParamStepValues();
    
    void WriteFXParamAliases();

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
        
        projectMetronomePrimaryVolumePtr_ = (double *)get_config_var("projmetrov1", &size);
        projectMetronomeSecondaryVolumePtr_ = (double *)get_config_var("projmetrov2", &size);
        
        vector<int> stepSizes  = { 2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25 };
        vector<int> tickCounts = { 250, 235, 220, 205, 190, 175, 160, 145, 130, 115, 100, 90, 80, 70, 60, 50, 45, 40, 35, 30, 25, 20, 20, 20 };
        
        for(int i = 0; i < stepSizes.size(); i++)
            baseTickCounts_[stepSizes[i]] = tickCounts[i];
        
        //GenerateX32SurfaceFile();
    }

    void Shutdown()
    {
        WriteFXParamAliases();
        
        fxParamsDisplay_ = false;
        surfaceInDisplay_ = false;
        surfaceOutDisplay_ = false;
       
        // GAW -- IMPORTANT
        // We want to stop polling, save state, and and zero out all Widgets before shutting down
        shouldRun_ = false;
        
        if(pages_.size() > 0)
            pages_[currentPageIndex_]->ForceClear();
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
   
    double *GetMetronomePrimaryVolumePtr() { return projectMetronomePrimaryVolumePtr_; }
    double *GetMetronomeSecondaryVolumePtr() { return projectMetronomeSecondaryVolumePtr_; }
    
    int GetBaseTickCount(int stepCount)
    {
        if(baseTickCounts_.count(stepCount) > 0)
            return baseTickCounts_[stepCount];
        else
            return baseTickCounts_[baseTickCounts_.size() - 1];
    }
    
    void Speak(string phrase)
    {
        const void (*osara_outputMessage)(const char* message) = nullptr;
    
        osara_outputMessage = (decltype(osara_outputMessage))plugin_getapi("osara_outputMessage");

        if (osara_outputMessage)
            osara_outputMessage(phrase.c_str());
    }
    
    shared_ptr<ActionContext> GetActionContext(string actionName, shared_ptr<Widget> widget, shared_ptr<Zone> zone, vector<string> params)
    {
        if(actions_.count(actionName) > 0)
            return make_shared<ActionContext>(actions_[actionName], widget, zone, params);
        else
            return make_shared<ActionContext>(actions_["NoAction"], widget, zone, params);
    }

    
    shared_ptr<ActionContext> GetActionContext(string actionName, shared_ptr<Widget> widget, shared_ptr<Zone> zone, int paramIndex)
    {
        if(actions_.count(actionName) > 0)
            return make_shared<ActionContext>(actions_[actionName], widget, zone, paramIndex);
        else
            return make_shared<ActionContext>(actions_["NoAction"], widget, zone, paramIndex);
    }

    shared_ptr<ActionContext> GetActionContext(string actionName, shared_ptr<Widget> widget, shared_ptr<Zone> zone, string stringParam)
    {
        if(actions_.count(actionName) > 0)
            return make_shared<ActionContext>(actions_[actionName], widget, zone, stringParam);
        else
            return make_shared<ActionContext>(actions_["NoAction"], widget, zone, stringParam);
    }

    void OnTrackSelection(MediaTrack *track)
    {
        if(pages_.size() > 0)
            pages_[currentPageIndex_]->OnTrackSelection(track);
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
    }
    
    void SetTrackOffset(int offset)
    {
        if(pages_.size() > 0)
            pages_[currentPageIndex_]->SetTrackOffset(offset);
    }
    
    void AdjustBank(shared_ptr<Page> sendingPage, string zoneName, int amount)
    {
        if(! sendingPage->GetSynchPages())
            sendingPage->AdjustBank(zoneName, amount);
        else
            for(auto page: pages_)
                if(page->GetSynchPages())
                    page->AdjustBank(zoneName, amount);
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
    
    string GetTCPFXParamName(MediaTrack* track, int fxIndex, int paramIndex)
    {
        char fxName[BUFSZ];
        DAW::TrackFX_GetNamedConfigParm(track, fxIndex, "fx_name", fxName, sizeof(fxName));

        char fxParamName[BUFSZ];
        DAW::TrackFX_GetParamName(track, fxIndex, paramIndex, fxParamName, sizeof(fxParamName));
        
        fxParamAliases_[fxName][paramIndex] = fxParamName;

        return fxParamAliases_[fxName][paramIndex];
    }
    
    string GetFXParamName(MediaTrack* track, int fxIndex, int paramIndex)
    {
        if(! track)
            return "";
        
        char fxName[BUFSZ];
        DAW::TrackFX_GetNamedConfigParm(track, fxIndex, "fx_name", fxName, sizeof(fxName));

        if(fxParamAliases_.count(fxName) > 0 && fxParamAliases_[fxName].count(paramIndex) > 0)
            return fxParamAliases_[fxName][paramIndex];
        else
        {
            char fxParamName[BUFSZ];
            DAW::TrackFX_GetParamName(track, fxIndex, paramIndex, fxParamName, sizeof(fxParamName));
            return fxParamName;
        }
    }

    void SetSteppedValueCount(string fxName, int paramIndex, int steppedValuecount)
    {
        fxParamSteppedValueCounts_[fxName][paramIndex] = steppedValuecount;
    }
    
    bool HaveFXSteppedValuesBeenCalculated(string fxName)
    {
        if(fxParamSteppedValueCounts_.count(fxName) > 0)
            return true;
        else
            return false;
    }
    
    int GetSteppedValueCount(string fxName, int paramIndex)
    {
        if(fxParamSteppedValueCounts_.count(fxName) > 0 && fxParamSteppedValueCounts_[fxName].count(paramIndex) > 0)
            return fxParamSteppedValueCounts_[fxName][paramIndex];
        else
            return  0;
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
    
    void GenerateX32SurfaceFile()
    {
        vector<vector<string>> generalWidgets = {   {"MasterFader", "/main/st/mix/fader"},
                                                    {"OtherWidget", "other/widget/"} };
        
        vector<vector<string>> channelWidgets = {   {"Fader", "/ch/|/mix/fader"},
                                                    {"Mute", "/ch/|/mix/mute"},
                                                    {"Solo", "/ch/|/mix/solo"},
                                                    {"Select", "/ch/|/mix/select"} };
       
        ofstream X32File(string(DAW::GetResourcePath()) + "/CSI/Zones/ZoneRawFXFiles/" + "X32.ost");

        if(X32File.is_open())
        {
            for(int i = 0; i < generalWidgets.size(); i++)
            {
                X32File << "Widget " + generalWidgets[i][0]  + GetLineEnding();
                
                X32File << "\tControl " + generalWidgets[i][1] + GetLineEnding();
                
                X32File << "\tFB_Processor " + generalWidgets[i][1] + GetLineEnding();

                X32File << "WidgetEnd" + GetLineEnding() + GetLineEnding();
            }

            for(int i = 0; i < channelWidgets.size(); i++)
            {
                for(int j = 0; j < 32; j++)
                {
                    string numStr = to_string(j + 1);
             
                    X32File << "Widget " + channelWidgets[i][0] + numStr + GetLineEnding();
                    
                    if(numStr.length() < 2)
                       numStr = "0" + numStr;
                    
                    string OSCMessage = regex_replace(channelWidgets[i][1], regex("[|]"), numStr);

                    X32File << "\tControl " + OSCMessage + GetLineEnding();

                    X32File << "\tFB_Processor " + OSCMessage + GetLineEnding();

                    X32File << "WidgetEnd" + GetLineEnding() + GetLineEnding();
                }
            }
        }
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
