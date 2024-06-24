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

#if __cplusplus < 201100
#if !defined(_MSC_VER) || _MSC_VER < 1400
#define override
#endif
#endif

#ifdef _WIN32
#define _CRT_NONSTDC_NO_DEPRECATE // for Visual Studio versions that want _strdup instead of strdup
#if _MSC_VER <= 1400
#define _CRT_SECURE_NO_DEPRECATE
#endif
#if _MSC_VER >= 1800
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#endif

#include <sstream>
#include <iomanip>
#include <math.h>

#ifdef _WIN32
#include "oscpkt.hh"
#include "udp.hh"
#endif

#include "../WDL/win32_utf8.h"
#include "../WDL/ptrlist.h"
#include "../WDL/assocarray.h"
#include "../WDL/wdlstring.h"
#include "../WDL/queue.h"

#include "control_surface_integrator_Reaper.h"

#ifdef INCLUDE_LOCALIZE_IMPORT_H
#define LOCALIZE_IMPORT_PREFIX "csi_"
#include "../WDL/localize/localize-import.h"
#endif
#include "../WDL/localize/localize.h"

#ifdef _WIN32
#include "commctrl.h"
#else
#include "oscpkt.hh"
#include "udp.hh"
#endif

#define NUM_ELEM(array) (int(sizeof(array)/sizeof(array[0])))

class CSurfIntegrator;
class ZoneManager;
class Widget;

extern void TrimLine(string &line);
extern void ReplaceAllWith(string &output, const char *replaceAny, const char *replacement);
extern int strToHex(const char *valueStr);

extern void LearnFocusedFXDialog(ZoneManager *zoneManager);
extern void CheckLearnFocusedFXState(ZoneManager *zoneManager);
extern void CloseFocusedFXDialog();
extern void UpdateLearnWindow();
extern void UpdateLearnWindow(int paramNumber);
extern void InitBlankLearnFocusedFXZone();

extern bool g_surfaceRawInDisplay;
extern bool g_surfaceInDisplay;
extern bool g_surfaceOutDisplay;
extern bool g_fxParamsWrite;

extern REAPER_PLUGIN_HINSTANCE g_hInst;

static const char * const s_CSIName = "CSI";
static const char * const s_CSIVersionDisplay = "v7.0";
static const char * const s_MajorVersionToken = "Version 7.0";
static const char * const s_PageToken = "Page";
static const char * const s_MidiSurfaceToken = "MidiSurface";
static const char * const s_OSCSurfaceToken = "OSCSurface";
static const char * const s_OSCX32SurfaceToken = "OSCX32Surface";

static const char * const s_BadFileChars = " \\:*?<>|.,()/";
static const char * const s_BeginAutoSection = "#Begin auto generated section";
static const char * const s_EndAutoSection = "#End auto generated section";

class fpistream
{
  public:
    FILE *handle;

    fpistream(const char *fn) { handle = fopenUTF8(fn, "rb"); }
    ~fpistream() { close(); }
    void close() { if (handle) { fclose(handle); handle = NULL; } }
};

bool getline(fpistream &fp, string &str); // mimic C++ getline()

static char *format_number(double v, char *buf, int bufsz)
{
  snprintf(buf,bufsz,"%.12f", v);
  WDL_remove_trailing_decimal_zeros(buf,2); // trim 1.0000 down to 1.0
  return buf;
}

class string_list
{
  public:

     void clear();
     char *add_raw(const char *str, size_t len); // may pass NULL, adds all 0s of len (use trim_last after)
     void push_back(const char *str) { add_raw(str, strlen(str)); }
     void push_back(const string &str) { push_back(str.c_str()); }
     void trim_last(); // removes any extra trailing 0 bytes on last item

     int size() const { return offsets_.GetSize(); }

     int begin() const { return 0; }
     void erase(int idx) { offsets_.Delete(idx); }
     void update(int idx, const char *value);

     const char *get(int idx) const;

     struct string_ref {
         string_ref(const char *v) : v_(v) { }
         const char *v_;

         const char *c_str() const { return v_; }
         operator const char *() const { return v_; }

         size_t find(const char *s, size_t pos = 0) const
         {
             const char *sp = v_;
             while (pos-- != 0 && *sp) sp++;
             const char *f = strstr(sp,s);
             return f ? (f - v_) : string::npos;
         }
         size_t find(const string &s, size_t pos = 0) const { return find(s.c_str(), pos); }

         bool operator==(const char *str) const { return !strcmp(v_,str); }
         bool operator!=(const char *str) const { return !!strcmp(v_,str); }
         bool operator==(const string &str) const { return !strcmp(v_,str.c_str()); }
         bool operator!=(const string &str) const { return !!strcmp(v_,str.c_str()); }
     };

     const string_ref operator[](const int idx) const { return string_ref(get(idx)); }

  private:
      WDL_TypedBuf<int> offsets_;
      WDL_TypedBuf<char> buf_;
};

extern void GetTokens(string_list &tokens, const char *line);
extern void GetSubTokens(string_list &tokens, const char *line, char delim);

template<class T> class ptrvector : public WDL_PtrList<T>
{
  public:
    ptrvector() { }
    ptrvector(const ptrvector &cp)
    {
        for (int x = 0; x < cp.GetSize(); x ++)
            push_back(cp[x]);
    }
    ~ptrvector() { WDL_PtrList<T>::Empty(true); }

    ptrvector &operator=(const ptrvector &cp)
    {
        WDL_PtrList<T>::Empty(true);
        for (int x = 0; x < cp.GetSize(); x ++)
            push_back(cp[x]);
        return *this;
    }

    void clear() { WDL_PtrList<T>::Empty(true); }
    int size() const { return WDL_PtrList<T>::GetSize(); }
    const T &operator[](int idx) const {
        const T *p = WDL_PtrList<T>::Get(idx);
        if (WDL_NORMALLY(p)) return *p;
        static T tmp;
        return tmp;
    }
    T &operator[](int idx) {
        T *p = WDL_PtrList<T>::Get(idx);
        if (WDL_NORMALLY(p)) return *p;
        static T tmp;
        return tmp;
    }

    void push_back(const T &list)
    {
        WDL_PtrList<T>::Add(new T(list));
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
enum PropertyType {
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define DECLARE_PROPERTY_TYPES(D) \
  D(Row) \
  D(Font) \
  D(TopMargin) \
  D(BottomMargin) \
  D(BackgroundColorOff) \
  D(TextColorOff) \
  D(BackgroundColorOn) \
  D(TextColorOn) \
  D(DisplayText) \
  D(BackgroundColor) \
  D(TextColor) \
  D(RingStyle) \
  D(Push) \
  D(PushColor) \
  D(LEDRingColor) \
  D(LEDRingColors) \
  D(BarStyle) \
  D(TextAlign) \
  D(TextInvert) \
  D(Mode) \
  D(OffColor) \
  D(OnColor) \
  D(Background) \
  D(Foreground) \
  D(Feedback) \

  PropertyType_Unknown = 0, // in this case, string is type=value pair
#define DEFPT(x) PropertyType_##x ,
  DECLARE_PROPERTY_TYPES(DEFPT)
#undef DEFPT
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class PropertyList
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    enum { MAX_PROP=24, RECLEN=10 };
    int nprops_;
    PropertyType props_[MAX_PROP];
    char vals_[MAX_PROP][RECLEN]; // if last byte is nonzero, pointer, otherwise, string

    static char *get_item_ptr(char *vp) // returns a strdup'd string
    {
        if (!vp[RECLEN-1]) return NULL;
        char *ret;
        memcpy(&ret,vp,sizeof(char *));
        return ret;
    }

  public:
    PropertyList() : nprops_(0) { }
    ~PropertyList()
    {
        for (int x = 0; x < nprops_; x ++) free(get_item_ptr(&vals_[x][0]));
    }

    void set_prop(PropertyType prop, const char *val)
    {
        int x;
        if (prop == PropertyType_Unknown)
            x = nprops_;
        else
            for (x = 0; x < nprops_ && props_[x] != prop; x ++);

        if (WDL_NOT_NORMALLY(x >= MAX_PROP)) return;

        char *rec = &vals_[x][0];
        if (x == nprops_)
        {
            nprops_++;
            props_[x] = prop;
        }
        else
        {
            free(get_item_ptr(rec));
        }

        if (strlen(val) < RECLEN)
        {
          lstrcpyn_safe(rec, val, RECLEN);
          rec[RECLEN-1] = 0;
        }
        else
        {
          char *v = strdup(val);
          memcpy(rec, &v, sizeof(v));
          rec[RECLEN-1] = 1;
        }
    }
    void set_prop_int(PropertyType prop, int v) { char tmp[64]; snprintf(tmp,sizeof(tmp),"%d",v); set_prop(prop,tmp); }

    const char *get_prop(PropertyType prop) const
    {
        for (int x = 0; x < nprops_; x ++)
            if (props_[x] == prop)
            {
                char *p = get_item_ptr((char *) (&vals_[x][0]));
                return p ? p : &vals_[x][0];
            }
        return NULL;
    }

    const char *enum_props(int x, PropertyType &type) const
    {
        if (x<0 || x >= nprops_) return NULL;
        type = props_[x];
        return get_item_ptr((char*)&vals_[x][0]);
    }

    static PropertyType prop_from_string(const char *str)
    {
#define CHK(x) if (!strcmp(str,#x)) return PropertyType_##x;
        DECLARE_PROPERTY_TYPES(CHK)
#undef CHK
        return PropertyType_Unknown;
    }
    static const char *string_from_prop(PropertyType type)
    {
#define CHK(x) if (type == PropertyType_##x) return #x;
        DECLARE_PROPERTY_TYPES(CHK)
#undef CHK
        return NULL;
    }

    void save_list(FILE *fxFile) const
    {
        for (int x = 0; x < nprops_; x ++)
        {
            //const char *value = get_item_ptr((char *)&vals_[x][0]), *key = string_from_prop(props_[x]);
            const char *value = get_prop(props_[x]);
            const char *key = string_from_prop(props_[x]);
            
            if (key && value)
                fprintf(fxFile, " %s=%s", key, value);
            //else if (WDL_NORMALLY(props_[x] == PropertyType_Unknown && value && strstr(value,"=")))
                //fprintf(fxFile, " %s", value);
        }
        fprintf(fxFile,"\n");
    }

    void print_to_buf(char * buf, int buf_size, PropertyType prop)
    {
        const char *key = string_from_prop(prop);
        const char *value = get_prop(prop);

        if (key && value)
            snprintf_append(buf, buf_size, "%s=%s ", key, value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Page;
class ControlSurface;
class Midi_ControlSurface;
class OSC_ControlSurface;
class TrackNavigationManager;
class FeedbackProcessor;
class Zone;
class ActionContext;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Action
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual ~Action() {}
    
    virtual const char *GetName() { return "Action"; }

    virtual void Touch(ActionContext *context, double value) {}
    virtual void RequestUpdate(ActionContext *context) {}
    virtual void Do(ActionContext *context, double value) {}
    virtual double GetCurrentNormalizedValue(ActionContext *context) { return 0.0; }
    virtual double GetCurrentDBValue(ActionContext *context) { return 0.0; }

    int GetPanMode(MediaTrack *track)
    {
        double pan1, pan2 = 0.0;
        int panMode = 0;
        GetTrackUIPan(track, &pan1, &pan2, &panMode);
        return panMode;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    CSurfIntegrator *const csi_;
    Page *const page_;
    bool isVolumeTouched_;
    bool isPanTouched_;
    bool isPanWidthTouched_;
    bool isPanLeftTouched_;
    bool isPanRightTouched_;
    bool isMCUTrackPanWidth_;

    Navigator(CSurfIntegrator *const csi, Page * page) : csi_(csi), page_(page)
    {
        // protected:
        isVolumeTouched_ = false;
        isPanTouched_ = false;
        isPanWidthTouched_ = false;
        isPanLeftTouched_ = false;
        isPanRightTouched_ = false;
        isMCUTrackPanWidth_ = false;
    }

public:
    virtual ~Navigator() {}
    
    virtual const char *GetName() { return "Navigator"; }
    virtual MediaTrack *GetTrack() { return NULL; }
    virtual int GetChannelNum() { return 0; }

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
    int const channelNum_;
    
protected:
    TrackNavigationManager *const trackNavigationManager_;

public:
    TrackNavigator(CSurfIntegrator *const csi, Page *page, TrackNavigationManager *trackNavigationManager, int channelNum) : Navigator(csi, page), trackNavigationManager_(trackNavigationManager), channelNum_(channelNum) {}
    virtual ~TrackNavigator() {}
    
    virtual const char *GetName() override { return "TrackNavigator"; }
   
    virtual MediaTrack *GetTrack() override;
    
    virtual int GetChannelNum() override { return channelNum_; }

};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MasterTrackNavigator : public Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    MasterTrackNavigator(CSurfIntegrator *const csi, Page * page) : Navigator(csi, page) {}
    virtual ~MasterTrackNavigator() {}
    
    virtual const char *GetName() override { return "MasterTrackNavigator"; }
    
    virtual MediaTrack *GetTrack() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SelectedTrackNavigator : public Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    SelectedTrackNavigator(CSurfIntegrator *const csi, Page * page) : Navigator(csi, page) {}
    virtual ~SelectedTrackNavigator() {}
    
    virtual const char *GetName() override { return "SelectedTrackNavigator"; }
    
    virtual MediaTrack *GetTrack() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FocusedFXNavigator : public Navigator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    FocusedFXNavigator(CSurfIntegrator *const csi, Page * page) : Navigator(csi, page) {}
    virtual ~FocusedFXNavigator() {}
    
    virtual const char *GetName() override { return "FocusedFXNavigator"; }
    
    virtual MediaTrack *GetTrack() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ActionContext
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    CSurfIntegrator *const csi_;
    Action *action_;
    Widget  *const widget_;
    Zone  *const zone_;

    int intParam_;
    
    string stringParam_;
    
    int paramIndex_;
    string fxParamDisplayName_;
    
    int commandId_;
    
    double rangeMinimum_;
    double rangeMaximum_;
    
    vector<double> steppedValues_;
    int steppedValuesIndex_;
    
    double deltaValue_;
    vector<double> acceleratedDeltaValues_;
    vector<int> acceleratedTickValues_;
    int accumulatedIncTicks_;
    int accumulatedDecTicks_;
    
    bool isValueInverted_;
    bool isFeedbackInverted_;
    DWORD holdDelayAmount_;
    DWORD delayStartTime_;
    bool delayStartTimeValid_;
    double deferredValue_;
    
    bool supportsColor_;
    vector<rgba_color> colorValues_;
    int currentColorIndex_;
    
    bool supportsTrackColor_;
        
    bool provideFeedback_;

    PropertyList widgetProperties_;
        
    void UpdateTrackColor();
    void GetSteppedValues(Widget *widget, Action *action,  Zone *zone, int paramNumber, const string_list &params, const PropertyList &widgetProperties, double &deltaValue, vector<double> &acceleratedDeltaValues, double &rangeMinimum, double &rangeMaximum, vector<double> &steppedValues, vector<int> &acceleratedTickValues);
    void SetColor(const string_list &params, bool &supportsColor, bool &supportsTrackColor, vector<rgba_color> &colorValues);
    void GetColorValues(vector<rgba_color> &colorValues, const string_list &colors);
public:
    ActionContext(CSurfIntegrator *const csi, Action *action, Widget *widget, Zone *zone, int paramIndex, const string_list *params, const string *stringParam);

    virtual ~ActionContext() {}
    
    CSurfIntegrator *GetCSI() { return csi_; }
    
    Action *GetAction() { return action_; }
    Widget *GetWidget() { return widget_; }
    Zone *GetZone() { return zone_; }
    int GetSlotIndex();
    const char *GetName();

    int GetIntParam() { return intParam_; }
    int GetCommandId() { return commandId_; }
    
    const char *GetFXParamDisplayName() { return fxParamDisplayName_.c_str(); }
    
    MediaTrack *GetTrack();
    
    void DoRangeBoundAction(double value);
    void DoSteppedValueAction(double value);
    void DoAcceleratedSteppedValueAction(int accelerationIndex, double value);
    void DoAcceleratedDeltaValueAction(int accelerationIndex, double value);
    
    Page *GetPage();
    ControlSurface *GetSurface();
    int GetParamIndex() { return paramIndex_; }
    void SetParamIndex(int paramIndex) { paramIndex_ = paramIndex; }
      
    PropertyList &GetWidgetProperties() { return widgetProperties_; }
    
    void SetIsValueInverted() { isValueInverted_ = true; }
    void SetIsFeedbackInverted() { isFeedbackInverted_ = true; }
    void SetHoldDelayAmount(double holdDelayAmount) { holdDelayAmount_ = (DWORD) (holdDelayAmount * 1000.0 + 0.5); } // holdDelayAmount is specified in seconds, holdDelayAmount_ is in milliseconds
    
    void SetAction(Action *action) { action_ = action; }
    void DoAction(double value);
    void DoRelativeAction(double value);
    void DoRelativeAction(int accelerationIndex, double value);
    
    void RequestUpdate();
    void RunDeferredActions();
    void ClearWidget();
    void UpdateWidgetValue(double value); // note: if passing the constant 0, must be 0.0 to avoid ambiguous type vs pointer
    void UpdateWidgetValue(const char *value);
    void UpdateJSFXWidgetSteppedValue(double value);
    void UpdateColorValue(double value);

    const char *GetStringParam() { return stringParam_.c_str(); }
    void    SetStringParam(const char *stringParam) { stringParam_ = stringParam; }
    const   vector<double> &GetAcceleratedDeltaValues() { return acceleratedDeltaValues_; }
    void    SetAccelerationValues(const vector<double> &acceleratedDeltaValues) { acceleratedDeltaValues_ = acceleratedDeltaValues; }
    const   vector<int> &GetAcceleratedTickCounts() { return acceleratedTickValues_; }
    void    SetTickCounts(const vector<int> &acceleratedTickValues) { acceleratedTickValues_ = acceleratedTickValues; }
    int     GetNumberOfSteppedValues() { return (int)steppedValues_.size(); }
    const   vector<double> &GetSteppedValues() { return steppedValues_; }
    void    SetStepValues(const vector<double> &steppedValues) { steppedValues_ = steppedValues; }
    double  GetDeltaValue() { return deltaValue_; }
    void    SetDeltaValue(double deltaValue) { deltaValue_ = deltaValue; }
    double  GetRangeMinimum() const { return rangeMinimum_; }
    void    SetRangeMinimum(double rangeMinimum) { rangeMinimum_ = rangeMinimum; }
    double  GetRangeMaximum() const { return rangeMaximum_; }
    void    SetRangeMaximum(double rangeMaximum) { rangeMaximum_ = rangeMaximum; }
    bool    GetProvideFeedback() { return provideFeedback_; }
    
    void    SetProvideFeedback(bool provideFeedback)
    {
        provideFeedback_ = provideFeedback;

        if (provideFeedback)
            RequestUpdate();
        else
            ClearWidget();
    }
    
    void DoTouch(double value)
    {
        action_->Touch(this, value);
    }

    void SetRange(const vector<double> &range)
    {
        if (range.size() != 2)
            return;
        
        rangeMinimum_ = range[0];
        rangeMaximum_ = range[1];
    }
        
    void SetSteppedValueIndex(double value)
    {
        int index = 0;
        double delta = 100000000.0;
        
        for (int i = 0; i < steppedValues_.size(); i++)
            if (fabs(steppedValues_[i] - value) < delta)
            {
                delta = fabs(steppedValues_[i] - value);
                index = i;
            }
        
        steppedValuesIndex_ = index;
    }

    char *GetPanValueString(double panVal, const char *dualPan, char *buf, int bufsz) const
    {
        bool left = false;
        
        if (panVal < 0)
        {
            left = true;
            panVal = -panVal;
        }
        
        int panIntVal = int(panVal  *100.0);
        
        if (left)
        {
            const char *prefix;
            if (panIntVal == 100)
                prefix = "";
            else if (panIntVal < 100 && panIntVal > 9)
                prefix = " ";
            else
                prefix = "  ";
            
            snprintf(buf, bufsz, "<%s%d%s", prefix, panIntVal, dualPan ? dualPan : "");
        }
        else
        {
            const char *suffix;
            
            if (panIntVal == 100)
                suffix = "";
            else if (panIntVal < 100 && panIntVal > 9)
                suffix = " ";
            else
                suffix = "  ";

            snprintf(buf, bufsz, "  %s%d%s>", dualPan && *dualPan ? dualPan : " ", panIntVal, suffix);
        }
        
        if (panIntVal == 0)
        {
            if (!dualPan || !dualPan[0])
                lstrcpyn_safe(buf, "  <C>  ", bufsz);
            else if (!strcmp(dualPan, "L"))
                lstrcpyn_safe(buf, " L<C>  ", bufsz);
            else if (!strcmp(dualPan, "R"))
                lstrcpyn_safe(buf, " <C>R  ", bufsz);
        }

        return buf;
    }
    
    char *GetPanWidthValueString(double widthVal, char *buf, int bufsz) const
    {
        bool reversed = false;
        
        if (widthVal < 0)
        {
            reversed = true;
            widthVal = -widthVal;
        }
        
        int widthIntVal = int(widthVal  *100.0);
        
        if (widthIntVal == 0)
            lstrcpyn_safe(buf, "<Mono> ", bufsz);
        else
            snprintf(buf, bufsz, "%s %d", reversed ? "Rev" : "Wid", widthIntVal);
        
        return buf;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Zone
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    ZoneManager  *const zoneManager_;
    CSurfIntegrator *const csi_;
    Navigator *const navigator_;
    int slotIndex_;
    string const name_;
    string const alias_;
    string const sourceFilePath_;
    
    bool isActive_;
    
    // these do not own the widgets, ultimately the ControlSurface contains the list of widgets
    WDL_PtrList<Widget> widgets_;
    WDL_PointerKeyedArray<Widget*, int> currentActionContextModifiers_;

    static void destroyActionContextListArray(WDL_IntKeyedArray<WDL_PtrList<ActionContext> *> *list) { delete list; }
    static void destroyActionContextList(WDL_PtrList<ActionContext> *l) { l->Empty(true); delete l; }
    WDL_PointerKeyedArray<Widget*, WDL_IntKeyedArray<WDL_PtrList<ActionContext> *> *> actionContextDictionary_;
        
    ptrvector<Zone *> includedZones_;

    ptrvector<Zone *> subZones_;

    void UpdateCurrentActionContextModifier(Widget *widget);
        
public:   
    Zone(CSurfIntegrator *const csi, ZoneManager  *const zoneManager, Navigator *navigator, int slotIndex, const string &name, const string &alias, const string &sourceFilePath): csi_(csi), zoneManager_(zoneManager), navigator_(navigator), slotIndex_(slotIndex), name_(name), alias_(alias), sourceFilePath_(sourceFilePath), actionContextDictionary_(destroyActionContextListArray)
    {
        isActive_ = false;
    }

    virtual ~Zone()
    {
        includedZones_.clear();
        subZones_.clear();
    }
    
    void InitSubZones(const string_list &subZones, const char *widgetSuffix);
    int GetSlotIndex();
    void SetXTouchDisplayColors(const char *colors);
    void RestoreXTouchDisplayColors();
    void UpdateCurrentActionContextModifiers();
    
    const WDL_PointerKeyedArray<Widget*, WDL_IntKeyedArray<WDL_PtrList<ActionContext> *> *> &GetActionContextDictionary() { return actionContextDictionary_; }
    const WDL_PtrList<ActionContext> &GetActionContexts(Widget *widget);
    void AddWidget(Widget *widget);
    void Activate();
    void Deactivate();
    void DoAction(Widget *widget, bool &isUsed, double value);
    void DoRelativeAction(Widget *widget, bool &isUsed, double delta);
    void DoRelativeAction(Widget *widget, bool &isUsed, int accelerationIndex, double delta);
    void DoTouch(Widget *widget, const char *widgetName, bool &isUsed, double value);
    void RequestUpdate();
    const WDL_PtrList<Widget> &GetWidgets() { return widgets_; }

    const char *GetSourceFilePath() { return sourceFilePath_.c_str(); }
    ptrvector<Zone *> &GetIncludedZones() { return includedZones_; }

    virtual const char *GetType() { return "Zone"; }
    
    Navigator *GetNavigator() { return navigator_; }
    void SetSlotIndex(int index) { slotIndex_ = index; }
    bool GetIsActive() { return isActive_; }
        
    void Toggle()
    {
        if (isActive_)
            Deactivate();
        else
            Activate();
    }

    const char *GetName()
    {
        return name_.c_str();
    }
    
    const char *GetAlias()
    {
        if (alias_.size() > 0)
            return alias_.c_str();
        else
            return name_.c_str();
    }
            
    void AddActionContext(Widget *widget, int modifier, ActionContext *actionContext)
    {
        WDL_IntKeyedArray<WDL_PtrList<ActionContext> *> *m = actionContextDictionary_.Get(widget);
        
        if (m)
        {
            WDL_PtrList<ActionContext> *l = m->Get(modifier);
            if (l)
            {
                l->Add(actionContext);
                return;
            }
        }
        
        if (!m)
        {
            m = new WDL_IntKeyedArray<WDL_PtrList<ActionContext> *>(destroyActionContextList);
            actionContextDictionary_.Insert(widget,m);
        }
        
        WDL_PtrList<ActionContext> *l = m->Get(modifier);
        if (!l)
        {
            l = new WDL_PtrList<ActionContext>;
            m->Insert(modifier,l);
        }

        l->Add(actionContext);
    }

    const WDL_PtrList<ActionContext> &GetActionContexts(Widget *widget, int modifier)
    {
        WDL_IntKeyedArray<WDL_PtrList<ActionContext> *> *m = actionContextDictionary_.Get(widget);
        if (m)
        {
            WDL_PtrList<ActionContext> *list = m->Get(modifier);
            if (list) return *list;
        }
        static const WDL_PtrList<ActionContext> empty;
        return empty;
    }
    
    void OnTrackDeselection()
    {
        isActive_ = true;
        
        for (int i = 0; i < includedZones_.size(); ++i)
            includedZones_[i]->Activate();
    }

    void RequestUpdateWidget(Widget *widget)
    {
        for (int i = 0; i < GetActionContexts(widget).GetSize(); ++i)
        {
            GetActionContexts(widget).Get(i)->RunDeferredActions();
            GetActionContexts(widget).Get(i)->RequestUpdate();
        }
    }
    
    virtual void GoSubZone(const char *subZoneName)
    {
        for (int i = 0; i < subZones_.size(); ++i)
        {
            if ( ! strcmp(subZones_[i]->GetName(), subZoneName))
            {
                subZones_[i]->SetSlotIndex(GetSlotIndex());
                subZones_[i]->Activate();
            }
            else
                subZones_[i]->Deactivate();
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SubZone : public Zone
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    Zone  *const enclosingZone_;
    
public:
    SubZone(CSurfIntegrator *const csi, ZoneManager  *const zoneManager, Navigator *navigator, int slotIndex, const string &name, const string &alias, const string &sourceFilePath, Zone *enclosingZone) : Zone(csi, zoneManager, navigator, slotIndex, name, alias, sourceFilePath), enclosingZone_(enclosingZone) {}

    virtual ~SubZone() {}
    
    virtual const char *GetType() override { return "SubZone"; }

    virtual void GoSubZone(const char *subZoneName) override
    {
        enclosingZone_->GoSubZone(subZoneName);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Widget
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    CSurfIntegrator *const csi_;
    ControlSurface *const surface_;
    string const name_;
    WDL_PtrList<FeedbackProcessor> feedbackProcessors_; // owns the objects
    int channelNumber_;
    int lastIncomingMessageTime_;
    double lastIncomingDelta_;
    
    double stepSize_;
    vector<double> accelerationValues_;
    
    bool hasBeenUsedByUpdate_;
    
public:
    // all Widgets are owned by their ControlSurface!
    Widget(CSurfIntegrator *const csi,  ControlSurface *surface, const char *name) : csi_(csi), surface_(surface), name_(name)
    {
        // private:
        channelNumber_ = 0;
        lastIncomingMessageTime_ = GetTickCount()-30000;
        lastIncomingDelta_ = 0.0;
        stepSize_ = 0.0;
        hasBeenUsedByUpdate_ = false;

        int index = (int)strlen(name) - 1;
        if (isdigit(name[index]))
        {
            while (index>=0 && isdigit(name[index]))
                index--;
               
            index++;
            
            channelNumber_ = atoi(name + index);
        }
    }
    
    ~Widget()
    {
      feedbackProcessors_.Empty(true);
    }
    
    void ClearHasBeenUsedByUpdate() { hasBeenUsedByUpdate_ = false; }
    void SetHasBeenUsedByUpdate() { hasBeenUsedByUpdate_ = true; }
    bool GetHasBeenUsedByUpdate() { return hasBeenUsedByUpdate_; }
    
    const char *GetName() { return name_.c_str(); }
    ControlSurface *GetSurface() { return surface_; }
    ZoneManager *GetZoneManager();
    int GetChannelNumber() { return channelNumber_; }
    
    void SetStepSize(double stepSize) { stepSize_ = stepSize; }
    double GetStepSize() { return stepSize_; }
    
    void SetAccelerationValues(const vector<double> *accelerationValues) { accelerationValues_ = *accelerationValues; }
    const vector<double> &GetAccelerationValues() { return accelerationValues_; }
    
    void SetIncomingMessageTime(int lastIncomingMessageTime) { lastIncomingMessageTime_ = lastIncomingMessageTime; }
    int GetLastIncomingMessageTime() { return lastIncomingMessageTime_; }
    
    void SetLastIncomingDelta(double delta) { lastIncomingDelta_ = delta; }
    double GetLastIncomingDelta() { return lastIncomingDelta_; }

    void Configure(const WDL_PtrList<ActionContext> &contexts);
    void UpdateValue(const PropertyList &properties, double value);
    void UpdateValue(const PropertyList &properties, const char * const &value);
    void RunDeferredActions();
    void UpdateColorValue(const rgba_color &color);
    void SetXTouchDisplayColors(const char *colors);
    void RestoreXTouchDisplayColors();
    void ForceClear();
    void LogInput(double value);
    
    void AddFeedbackProcessor(FeedbackProcessor *feedbackProcessor) // takes ownership of feedbackProcessor
    {
        feedbackProcessors_.Add(feedbackProcessor);
    }
};

////////////////////////////
// For Zone Manager
////////////////////////////

void GetSteppedValues(const string_list &params, int start_idx, double &deltaValue, vector<double> &acceleratedDeltaValues, double &rangeMinimum, double &rangeMaximum, vector<double> &steppedValues, vector<int> &acceleratedTickValues);

void GetPropertiesFromTokens(int start, int finish, const string_list &tokens, PropertyList &properties);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct CSIZoneInfo
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    string filePath;
    string alias;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ZoneManager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    CSurfIntegrator *const csi_;
    ControlSurface *const surface_;
    string const zoneFolder_;
    string const fxZoneFolder_;
   
    WDL_StringKeyedArray<CSIZoneInfo*> zoneInfo_;
    static void disposeAction(CSIZoneInfo *zoneInfo) { delete zoneInfo; }
        
    double holdDelayAmount_;
    
    Zone *learnFocusedFXZone_;
    
    Zone *homeZone_;

    ptrvector<Zone *> goZones_;
    
    ptrvector<ZoneManager *> listeners_;
    
    WDL_PtrList<Zone> zonesToBeDeleted_;
    
    bool listensToGoHome_;
    bool listensToSends_;
    bool listensToReceives_;
    bool listensToFocusedFX_;
    bool listensToFocusedFXParam_;
    bool listensToFXMenu_;
    bool listensToLocalFXSlot_;
    bool listensToSelectedTrackFX_;

    Zone *focusedFXParamZone_;
    bool isFocusedFXParamMappingEnabled_;
    
    Zone *focusedFXZone_;
    bool isFocusedFXMappingEnabled_;
    
    ptrvector<Zone *> selectedTrackFXZones_;
    Zone *fxSlotZone_;
    
    int trackSendOffset_;
    int trackReceiveOffset_;
    int trackFXMenuOffset_;
    int selectedTrackSendOffset_;
    int selectedTrackReceiveOffset_;
    int selectedTrackFXMenuOffset_;
    int masterTrackFXMenuOffset_;
    
    string_list paramList_;

    void GoFXSlot(MediaTrack *track, Navigator *navigator, int fxSlot);
    void GoSelectedTrackFX();
    void GetWidgetNameAndModifiers(const char *line, string &baseWidgetName, int &modifier, bool &isValueInverted, bool &isFeedbackInverted, double &holdDelayAmount,
                                   bool &isDecrease, bool &isIncrease);
    void Initialize();
    void GetNavigatorsForZone(const char *zoneName, const char *navigatorName, ptrvector<Navigator *> &navigators);
    void LoadZones(ptrvector<Zone *> &zones, string_list &zoneList);
         
    void DoAction(Widget *widget, double value, bool &isUsed);
    void DoRelativeAction(Widget *widget, double delta, bool &isUsed);
    void DoRelativeAction(Widget *widget, int accelerationIndex, double delta, bool &isUsed);
    void DoTouch(Widget *widget, double value, bool &isUsed);
    
    void LoadZoneMetadata(const char *filePath, string_list &metadata)
    {
        int lineNumber = 0;
        
        try
        {
            fpistream file(filePath);
            
            for (string line; getline(file, line) ; )
            {
                TrimLine(line);
                
                lineNumber++;
                
                if (line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                    continue;
                
                string_list tokens;
                GetTokens(tokens, line.c_str());
                
                if (tokens[0] == "Zone" || tokens[0] == "ZoneEnd")
                    continue;
                
                metadata.push_back(line);
            }
        }
        catch (exception)
        {
            char buffer[250];
            snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath, lineNumber);
            ShowConsoleMsg(buffer);
        }
    }
    
    void AdjustBank(int &bankOffset, int amount)
    {
        bankOffset += amount;
            
        if (bankOffset < 0)
            bankOffset = 0;
    }
    
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
   
    bool GetIsListener()
    {
        return listensToGoHome_ || listensToSends_ || listensToReceives_ || listensToFocusedFX_ || listensToFocusedFXParam_ || listensToFXMenu_ || listensToLocalFXSlot_ || listensToSelectedTrackFX_;
    }
    
    void ListenToGoZone(const char *zoneName)
    {
        if (!strcmp("SelectedTrackSend", zoneName) && listensToSends_)
            for (int i = 0; i < listeners_.size(); ++i)
                listeners_[i]->GoZone(zoneName);
        else if (!strcmp("SelectedTrackReceive", zoneName) && listensToReceives_)
            for (int i = 0; i < listeners_.size(); ++i)
                listeners_[i]->GoZone(zoneName);
        else if (!strcmp("SelectedTrackFX", zoneName) && listensToSelectedTrackFX_)
            for (int i = 0; i < listeners_.size(); ++i)
                listeners_[i]->GoZone(zoneName);
        else if (!strcmp("SelectedTrackFXMenu", zoneName) && listensToFXMenu_)
            for (int i = 0; i < listeners_.size(); ++i)
                listeners_[i]->GoZone(zoneName);
        else
            GoZone(zoneName);
    }
    
    void ListenToClearFXZone(const char *zoneToClear)
    {
        if (!strcmp("FocusedFXParam", zoneToClear) && listensToFocusedFXParam_)
            for (int i = 0; i < listeners_.size(); ++i)
                listeners_[i]->ClearFocusedFXParam();
        else if (!strcmp("FocusedFX", zoneToClear) && listensToFocusedFX_)
            for (int i = 0; i < listeners_.size(); ++i)
                listeners_[i]->ClearFocusedFX();
        else if (!strcmp("SelectedTrackFX", zoneToClear) && listensToSelectedTrackFX_)
            for (int i = 0; i < listeners_.size(); ++i)
                listeners_[i]->ClearSelectedTrackFX();
        else if (!strcmp("FXSlot", zoneToClear) && listensToFXMenu_)
            for (int i = 0; i < listeners_.size(); ++i)
                listeners_[i]->ClearFXSlot();
    }
    
    void ListenToGoHome()
    {
        if (listensToGoHome_)
            GoHome();
    }
    
    void ListenToGoFXSlot(MediaTrack *track, Navigator *navigator, int fxSlot)
    {
        if (listensToFXMenu_)
            GoFXSlot(track, navigator, fxSlot);
    }

    void ListenToToggleEnableFocusedFXParamMapping()
    {
        if (listensToFocusedFXParam_)
            ToggleEnableFocusedFXParamMapping();
    }

    void ToggleEnableFocusedFXParamMapping()
    {
        isFocusedFXParamMappingEnabled_ = ! isFocusedFXParamMappingEnabled_;
        
        if (focusedFXParamZone_ != NULL)
        {
            if (isFocusedFXParamMappingEnabled_)
                focusedFXParamZone_->Activate();
            else
                focusedFXParamZone_->Deactivate();
        }
    }

    void ListenToToggleEnableFocusedFXMapping()
    {
        if (listensToFocusedFX_)
            ToggleEnableFocusedFXMapping();
    }

    void ToggleEnableFocusedFXMapping()
    {
        isFocusedFXMappingEnabled_ = ! isFocusedFXMappingEnabled_;
    }

    void ClearFocusedFXParam()
    {
        if (focusedFXParamZone_ != NULL)
            focusedFXParamZone_->Deactivate();
    }
    
    void ClearLearnFocusedFXZone()
    {
        if (learnFocusedFXZone_ != NULL)
        {
            learnFocusedFXZone_->Deactivate();
            if (zonesToBeDeleted_.Find(learnFocusedFXZone_) == -1)
                zonesToBeDeleted_.Add(learnFocusedFXZone_);
            learnFocusedFXZone_ = NULL;
        }
    }
    
    void ClearFocusedFX()
    {
        if (focusedFXZone_ != NULL)
        {
            focusedFXZone_->Deactivate();
            if (zonesToBeDeleted_.Find(focusedFXZone_) == -1)
                zonesToBeDeleted_.Add(focusedFXZone_);
            focusedFXZone_ = NULL;
        }
    }
        
    void ClearSelectedTrackFX()
    {
        for (int i = 0; i < (int)selectedTrackFXZones_.size(); ++i)
        {
            selectedTrackFXZones_[i]->Deactivate();
            if (zonesToBeDeleted_.Find(selectedTrackFXZones_[i]) == -1)
                zonesToBeDeleted_.Add(selectedTrackFXZones_[i]);
        }
        
        selectedTrackFXZones_.Empty();
    }
    
    void ClearFXSlot()
    {
        if (fxSlotZone_ != NULL)
        {
            fxSlotZone_->Deactivate();
            if (zonesToBeDeleted_.Find(fxSlotZone_) == -1)
                zonesToBeDeleted_.Add(fxSlotZone_);
            fxSlotZone_ = NULL;
            ReactivateFXMenuZone();
        }
    }

    void ReactivateFXMenuZone()
    {
        for (int i = 0; i < goZones_.size(); ++i)
            if (!strcmp(goZones_[i]->GetName(), "TrackFXMenu") || !strcmp(goZones_[i]->GetName(), "SelectedTrackFXMenu"))
                if (goZones_[i]->GetIsActive())
                    goZones_[i]->Activate();
    }

public:
    ZoneManager(CSurfIntegrator *const csi, ControlSurface *surface, const string &zoneFolder, const string &fxZoneFolder);

    ~ZoneManager()
    {
        if (homeZone_ != NULL)
        {
            delete homeZone_;
            homeZone_ = NULL;
        }
            
        if (focusedFXZone_ != NULL)
        {
            delete focusedFXZone_;
            focusedFXZone_ = NULL;
        }
            
        if (focusedFXParamZone_ != NULL)
        {
            delete focusedFXParamZone_;
            focusedFXParamZone_ = NULL;
        }
            
        if (fxSlotZone_ != NULL)
        {
            delete fxSlotZone_;
            fxSlotZone_ = NULL;
        }
            
        if (learnFocusedFXZone_ != NULL)
        {
            delete learnFocusedFXZone_;
            learnFocusedFXZone_ = NULL;
        }
            
        goZones_.clear();

        selectedTrackFXZones_.Empty(true);
    }
      
    void SetHoldDelayAmount(double value) { holdDelayAmount_ = value; }

    Navigator *GetMasterTrackNavigator();
    Navigator *GetSelectedTrackNavigator();
    Navigator *GetFocusedFXNavigator();
    
    bool GetIsBroadcaster() { return  ! (listeners_.size() == 0); }
    void AddListener(ControlSurface *surface);
    void SetListenerCategories(const char *categoryList);
    const ptrvector<ZoneManager *> &GetListeners() { return listeners_; }
    
    int  GetNumChannels();
    
    void CalculateSteppedValues(const string &fxName, MediaTrack *track, int fxIndex);
    void GetSteppedValuesForParam(string &output, const char *fxName, MediaTrack *track, int fxIndex, int paramIndex);

    void PreProcessZones();
    void PreProcessZoneFile(const char *filePath);
    void LoadZoneFile(Zone *zone, const char *widgetSuffix);
    void LoadZoneFile(Zone *zone, const char *filePath, const char *widgetSuffix);

    void UpdateCurrentActionContextModifiers();
    void CheckFocusedFXState();

    void DoAction(Widget *widget, double value);
    void DoRelativeAction(Widget *widget, double delta);
    void DoRelativeAction(Widget *widget, int accelerationIndex, double delta);
    void DoTouch(Widget *widget, double value);
    
    const char *GetFXZoneFolder() { return fxZoneFolder_.c_str(); }
    const WDL_StringKeyedArray<CSIZoneInfo*> &GetZoneInfo() { return zoneInfo_; }

    CSurfIntegrator *GetCSI() { return csi_; }
    ControlSurface *GetSurface() { return surface_; }
    
    int GetTrackSendOffset() { return trackSendOffset_; }
    int GetTrackReceiveOffset() { return trackReceiveOffset_; }
    int GetTrackFXMenuOffset() { return trackFXMenuOffset_; }
    int GetSelectedTrackSendOffset() { return selectedTrackSendOffset_; }
    int GetSelectedTrackReceiveOffset() { return selectedTrackReceiveOffset_; }
    int GetSelectedTrackFXMenuOffset() { return selectedTrackFXMenuOffset_; }
    int GetMasterTrackFXMenuOffset() { return masterTrackFXMenuOffset_; }

    bool GetIsFocusedFXMappingEnabled() { return isFocusedFXMappingEnabled_; }
    bool GetIsFocusedFXParamMappingEnabled() { return isFocusedFXParamMappingEnabled_; }

    Zone *GetLearnedFocusedFXZone() { return  learnFocusedFXZone_;  }
    
    const WDL_PointerKeyedArray<Widget*, WDL_IntKeyedArray<WDL_PtrList<ActionContext> *> *> *GetLearnFocusedFXActionContextDictionary()
    {
        if (learnFocusedFXZone_ != NULL)
            return &learnFocusedFXZone_->GetActionContextDictionary();
        else return NULL;
    }

    void GetAlias(const char *fxName, char *alias, int aliassz)
    {
        static const char  *const prefixes[] =
        {
            "AU: Tube-Tech ",
            "AU: AU ",
            "AU: UAD UA ",
            "AU: UAD Pultec ",
            "AU: UAD Tube-Tech ",
            "AU: UAD Softube ",
            "AU: UAD Teletronix ",
            "AU: UADx ",
            "AU: UAD ",
            "AU: ",
            "AUi: ",
            "VST: TDR ",
            "VST: UAD UA ",
            "VST: UAD Pultec ",
            "VST: UAD Tube-Tech ",
            "VST: UAD Softube ",
            "VST: UAD Teletronix ",
            "VST: UAD ",
            "VST3: UADx ",
            "VST3i: UADx ",
            "VST: ",
            "VSTi: ",
            "VST3: ",
            "VST3i: ",
            "JS: ",
            "Rewire: ",
            "CLAP: ",
            "CLAPi: ",
        };
        
        // skip over known prefixes
        for (int i = 0; i < NUM_ELEM(prefixes); ++i)
        {
            const int l = (int) strlen(prefixes[i]);
            if (!strncmp(fxName, prefixes[i], l))
            {
                fxName += l;
                break;
            }
        }

        lstrcpyn_safe(alias, fxName, aliassz);
        char *p = strstr(alias," (");
        if (p) *p = 0;
    }
    
    void LoadAndActivateFocusedFXZone(const char *fxName, int fxSlot)
    {
        if(zoneInfo_.Exists(fxName))
        {
            focusedFXZone_ = new Zone(csi_, this, GetFocusedFXNavigator(), fxSlot, fxName, zoneInfo_.Get(fxName)->alias, zoneInfo_.Get(fxName)->filePath.c_str());
            LoadZoneFile(focusedFXParamZone_, "");
            focusedFXZone_->Activate();
        }
    }

    void LoadLearnFocusedFXZone(const char *fxName, int fxIndex)
    {
        if (learnFocusedFXZone_ != NULL)
        {
            // GAW TBD -- save here and add to zoneInfo_
            
            ClearLearnFocusedFXZone();
        }

        if(zoneInfo_.Exists(fxName))
        {
            learnFocusedFXZone_ = new Zone(csi_, this, GetFocusedFXNavigator(), fxIndex, fxName, zoneInfo_.Get(fxName)->alias, zoneInfo_.Get(fxName)->filePath);
            LoadZoneFile(learnFocusedFXZone_, "");
            
            learnFocusedFXZone_->Activate();
        }
        else
        {
            char alias[BUFSIZ];
            GetAlias(fxName, alias, sizeof(alias));

            CSIZoneInfo info;
            info.alias = alias;
            
            char fxFilePath[BUFSIZ];
            snprintf(fxFilePath, sizeof(fxFilePath), "%s/CSI/Zones/%s/AutoGeneratedFXZones", GetResourcePath(), GetFXZoneFolder());
            RecursiveCreateDirectory(fxFilePath, 0);
            string fxFileName = fxName;
            ReplaceAllWith(fxFileName, s_BadFileChars, "_");
            char fxFullFilePath[BUFSIZ];
            snprintf(fxFullFilePath, sizeof(fxFullFilePath), "%s/%s.zon", fxFilePath, fxFileName.c_str());
            info.filePath = fxFullFilePath;
            
            learnFocusedFXZone_ = new Zone(csi_, this, GetFocusedFXNavigator(), fxIndex, fxName, info.alias, info.filePath);
            InitBlankLearnFocusedFXZone();
            learnFocusedFXZone_->Activate();
        }
    }
                
    void DeclareGoZone(const char *zoneName)
    {
        if (! GetIsBroadcaster() && ! GetIsListener()) // No Broadcasters/Listeners relationships defined
            GoZone(zoneName);
        else
            for (int i = 0; i < listeners_.size(); ++i)
                listeners_[i]->ListenToGoZone(zoneName);
    }
    
    void GoZone(const char *zoneName)
    {
        ClearFXMapping();
        ResetOffsets();

        for (int i = 0; i < goZones_.size(); ++i)
        {
            if (!strcmp(zoneName, goZones_[i]->GetName()))
            {
                if (goZones_[i]->GetIsActive())
                {
                    for (int j = i; j < goZones_.size(); ++j)
                        if (!strcmp(zoneName, goZones_[j]->GetName()))
                            goZones_[j]->Deactivate();
                    
                    return;
                }
            }
        }
        
        for (int i = 0; i < goZones_.size(); ++i)
            if (strcmp(zoneName, goZones_[i]->GetName()))
                goZones_[i]->Deactivate();
        
        for (int i = 0; i < goZones_.size(); ++i)
            if (!strcmp(zoneName, goZones_[i]->GetName()))
               goZones_[i]->Activate();
    }
    
    void DeclareClearFXZone(const char *zoneName)
    {
        if (! GetIsBroadcaster() && ! GetIsListener()) // No Broadcasters/Listeners relationships defined
        {
            if (!strcmp("FocusedFXParam", zoneName))
                ClearFocusedFXParam();
            else if (!strcmp("FocusedFX", zoneName))
                ClearFocusedFX();
            else if (!strcmp("SelectedTrackFX", zoneName))
                ClearSelectedTrackFX();
            else if (!strcmp("FXSlot", zoneName))
                ClearFXSlot();
        }
        else
            for (int i = 0; i < listeners_.size(); ++i)
                listeners_[i]->ListenToClearFXZone(zoneName);
    }
    
    void DeclareGoFXSlot(MediaTrack *track, Navigator *navigator, int fxSlot)
    {
        if (listensToLocalFXSlot_ || (! GetIsBroadcaster() && ! GetIsListener())) // No Broadcasters/Listeners relationships defined
            GoFXSlot(track, navigator, fxSlot);
        else
            for (int i = 0; i < listeners_.size(); ++i)
                listeners_[i]->ListenToGoFXSlot(track, navigator, fxSlot);
    }
                
    void RemoveZone(const char *zoneName)
    {
        if (zoneInfo_.Exists(zoneName))
        {
            remove(zoneInfo_.Get(zoneName)->filePath.c_str());
            zoneInfo_.Delete(zoneName);
        }
    }
      
    void GetName(MediaTrack *track, int fxIndex, char *name, int namesz)
    {
        char fxName[MEDBUF];
        TrackFX_GetFXName(track, fxIndex, fxName, sizeof(fxName));

        if (zoneInfo_.Exists(fxName))
            lstrcpyn_safe(name, zoneInfo_.Get(fxName)->alias.c_str(), namesz);
        else
            GetAlias(fxName, name, namesz);
    }
        
    void ClearLearnedFXParams()
    {
        if (learnFocusedFXZone_ != NULL)
        {
            // GAW TDB -- clear UI vars
            
        }
    }
          
    void HideAllFXWindows()
    {
        for (int i = -1; i < GetNumTracks(); i++)
        {
            MediaTrack *tr = i < 0 ? GetMasterTrack(NULL) : GetTrack(NULL, i);
            if (WDL_NOT_NORMALLY(tr == NULL)) continue;

            for (int j = TrackFX_GetCount(tr) - 1; j >= -1; j --)
                TrackFX_Show(tr, j, j < 0 ? 0 : 2);

            for (int j = CountTrackMediaItems(tr) - 1; j >= 0; j --)
            {
                MediaItem *item = GetTrackMediaItem(tr, j);
                for (int k = CountTakes(item)-1; k >= 0; k --)
                {
                    MediaItem_Take *take = GetMediaItemTake(item, k);
                    if (take)
                    {
                        for (int l = TakeFX_GetCount(take) - 1; l >= -1; l --)
                            TakeFX_Show(take, l, l < 0 ? 0 : 2);
                    }
                }
            }
        }
    }

    void GoHome()
    {
        HideAllFXWindows();
        ClearLearnedFXParams();
        ClearFXMapping();
        ResetOffsets();

        for (int i = 0; i < goZones_.size(); ++i)
            goZones_[i]->Deactivate();
        
        homeZone_->Activate();
    }
    
    void DeclareGoHome()
    {
        if (! GetIsBroadcaster() && ! GetIsListener()) // No Broadcasters/Listeners relationships defined
            GoHome();
        else
            for (int i = 0; i < listeners_.size(); ++i)
                listeners_[i]->ListenToGoHome();
    }
        
    void OnTrackSelection()
    {
        if (fxSlotZone_ != NULL)
            ClearFXSlot();
    }

    void OnTrackDeselection()
    {
        ResetSelectedTrackOffsets();
        
        selectedTrackFXZones_.Empty(true);
        
        for (int i = 0; i < goZones_.size(); ++i)
        {
            if (!strcmp(goZones_[i]->GetName(), "SelectedTrack") ||
                !strcmp(goZones_[i]->GetName(), "SelectedTrackSend") ||
                !strcmp(goZones_[i]->GetName(), "SelectedTrackReceive") ||
                !strcmp(goZones_[i]->GetName(), "SelectedTrackFXMenu"))
            {
                goZones_[i]->Deactivate();
            }
        }
        
        homeZone_->OnTrackDeselection();
    }
    
    void DeclareToggleEnableFocusedFXParamMapping()
    {
        if (! GetIsBroadcaster() && ! GetIsListener()) // No Broadcasters/Listeners relationships defined
            ToggleEnableFocusedFXParamMapping();
        else
            for (int i = 0; i < listeners_.size(); ++i)
                listeners_[i]->ListenToToggleEnableFocusedFXParamMapping();
    }

    void DeclareToggleEnableFocusedFXMapping()
    {
        if (! GetIsBroadcaster() && ! GetIsListener()) // No Broadcasters/Listeners relationships defined
            ToggleEnableFocusedFXMapping();
        else
            for (int i = 0; i < listeners_.size(); ++i)
                listeners_[i]->ListenToToggleEnableFocusedFXMapping();
    }
    
    bool GetIsGoZoneActive(const char *zoneName)
    {
        for (int i = 0; i < goZones_.size(); ++i)
            if (!strcmp(zoneName, goZones_[i]->GetName()))
                return goZones_[i]->GetIsActive();
        
        return false;
    }
    
    bool GetIsHomeZoneOnlyActive()
    {
        for (int i = 0; i < goZones_.size(); ++i)
            if (goZones_[i]->GetIsActive())
                return false;

        return true;
    }

    void ClearFXMapping()
    {
        ClearLearnFocusedFXZone();
        ClearFocusedFX();
        ClearSelectedTrackFX();
        ClearFXSlot();
    }
        
    void AdjustBank(const char *zoneName, int amount)
    {
        if (!strcmp(zoneName, "TrackSend"))
            AdjustBank(trackSendOffset_, amount);
        else if (!strcmp(zoneName, "TrackReceive"))
            AdjustBank(trackReceiveOffset_, amount);
        else if (!strcmp(zoneName, "TrackFXMenu"))
            AdjustBank(trackFXMenuOffset_, amount);
        else if (!strcmp(zoneName, "SelectedTrackSend"))
            AdjustBank(selectedTrackSendOffset_, amount);
        else if (!strcmp(zoneName, "SelectedTrackReceive"))
            AdjustBank(selectedTrackReceiveOffset_, amount);
        else if (!strcmp(zoneName, "SelectedTrackFXMenu"))
            AdjustBank(selectedTrackFXMenuOffset_, amount);
        else if (!strcmp(zoneName, "MasterTrackFXMenu"))
            AdjustBank(masterTrackFXMenuOffset_, amount);
    }
                
    void AddZoneFilePath(const char *name, CSIZoneInfo *info)
    {
        if (name && *name)
            zoneInfo_.Insert(name, info);
    }
    
    void AddZoneFilePath(const char *fxZoneFolder, const char *name, CSIZoneInfo *info)
    {
        if (!strcmp(fxZoneFolder, fxZoneFolder_.c_str()))
            AddZoneFilePath(name, info);
    }
               
    void RequestUpdate()
    {
        CheckFocusedFXState();
          
        if (learnFocusedFXZone_ != NULL)
        {
            UpdateLearnWindow();
            learnFocusedFXZone_->RequestUpdate();
        }

        if (focusedFXParamZone_ != NULL && isFocusedFXParamMappingEnabled_)
            focusedFXParamZone_->RequestUpdate();

        if (focusedFXZone_ != NULL)
            focusedFXZone_->RequestUpdate();
        
        for (int i = 0; i < selectedTrackFXZones_.size(); ++i)
            selectedTrackFXZones_[i]->RequestUpdate();
        
        if (fxSlotZone_ != NULL)
            fxSlotZone_->RequestUpdate();
        
        for (int i = 0; i < goZones_.size(); ++i)
            goZones_[i]->RequestUpdate();

        if (homeZone_ != NULL)
            homeZone_->RequestUpdate();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    CSurfIntegrator *const csi_;
    Widget  *const widget_;
    
public:
    CSIMessageGenerator(CSurfIntegrator *const csi, Widget *widget) : csi_(csi), widget_(widget) {}
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
    AnyPress_CSIMessageGenerator(CSurfIntegrator *const csi, Widget *widget) : CSIMessageGenerator(csi, widget) {}
    virtual ~AnyPress_CSIMessageGenerator() {}
    
    virtual void ProcessMessage(double value) override
    {
        widget_->GetZoneManager()->DoAction(widget_, 1.0);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Touch_CSIMessageGenerator : public CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    Touch_CSIMessageGenerator(CSurfIntegrator *const csi, Widget *widget) : CSIMessageGenerator(csi, widget) {}
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
    CSurfIntegrator *const csi_;
    Page *page_;
    ControlSurface *surface_;
    
    int latchTime_;
    
    enum Modifiers
    {
        ErrorModifier = -1,
        Shift = 0,
        Option,
        Control,
        Alt,
        Flip,
        Global,
        Marker,
        Nudge,
        Zoom,
        Scrub,
        MaxModifiers
    };
    
    static int maskFromModifier(Modifiers m)
    {
      if (WDL_NOT_NORMALLY(m == ErrorModifier)) return 0;
      return 4 << (int) m;
    }
    
    static Modifiers modifierFromString(const char *s)
    {
         if (!strcmp(s,"Shift")) return Shift;
         if (!strcmp(s,"Option")) return Option;
         if (!strcmp(s,"Control")) return Control;
         if (!strcmp(s,"Alt")) return Alt;
         if (!strcmp(s,"Flip")) return Flip;
         if (!strcmp(s,"Global")) return Global;
         if (!strcmp(s,"Marker")) return Marker;
         if (!strcmp(s,"Nudge")) return Nudge;
         if (!strcmp(s,"Zoom")) return Zoom;
         if (!strcmp(s,"Scrub")) return Scrub;
         return ErrorModifier;
    }

    static const char *stringFromModifier(Modifiers mod)
    {
      switch (mod)
      {
        case Shift: return "Shift";
        case Option: return "Option";
        case Control: return "Control";
        case Alt: return "Alt";
        case Flip: return "Flip";
        case Global: return "Global";
        case Marker: return "Marker";
        case Nudge: return "Nudge";
        case Zoom: return "Zoom";
        case Scrub: return "Scrub";
        default:
          WDL_ASSERT(false);
        return "";
      }
    }

    struct ModifierState
    {
        bool isEngaged;
        DWORD pressedTime;
    };
    ModifierState modifiers_[MaxModifiers];
    WDL_TypedBuf<int> modifierCombinations_;
    static int intcmp_rev(const void *a, const void *b) { return *(const int *)a > *(const int *)b ? -1 : *(const int *)a < *(const int *)b ? 1 : 0; }
    
    void GetCombinations(const Modifiers *indices, int num_indices, WDL_TypedBuf<int> &combinations)
    {
        for (int mask = 0; mask < (1 << num_indices); mask++)
        {
            int combination = 0;
            
            for (int position = 0; position < num_indices; position++)
                if (mask & (1 << position))
                    combination |= maskFromModifier(indices[position]);
            
            if (combination != 0)
                combinations.Add(combination);
        }
    }

    void SetLatchModifier(bool value, Modifiers modifier, int latchTime);

public:
    ModifierManager(CSurfIntegrator *const csi, Page *page = NULL, ControlSurface *surface = NULL) : csi_(csi)
    {
        // private:
        page_ = page;
        surface_ = surface;
        latchTime_ = 100;

        int *p = modifierCombinations_.ResizeOK(1);
        if (WDL_NORMALLY(p)) p[0]=0;

        memset(modifiers_,0,sizeof(modifiers_));
    }
    
    void RecalculateModifiers();
    const WDL_TypedBuf<int> &GetModifiers() { return modifierCombinations_; }
    
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

    void ClearModifier(const char *modifierString)
    {
        Modifiers m = modifierFromString(modifierString);
        if (m != ErrorModifier)
        {
            modifiers_[m].isEngaged = false;
            RecalculateModifiers();
        }
    }

    static char *GetModifierString(int modifierValue, char *buf, int bufsz)
    {
        buf[0]=0;
        for (int x = 0; x < MaxModifiers; x++)
            if (modifierValue & maskFromModifier((Modifiers)x))
                snprintf_append(buf, bufsz, "%s+",stringFromModifier((Modifiers)x));

        return buf;
    }

    int GetModifierValue(const char *modifierString)
    {
        string_list modifierTokens;
        GetSubTokens(modifierTokens, modifierString, '+');
        return GetModifierValue(modifierTokens);
    }
    
    int GetModifierValue(const string_list &tokens)
    {
        int modifierValue = 0;
        for (int i = 0; i < tokens.size(); i++)
        {
            Modifiers m = modifierFromString(tokens[i].c_str());
            if (m != ErrorModifier)
                modifierValue |= maskFromModifier(m);
        }
        
        return modifierValue;
    }

    void SetShift(bool value, int latchTime)
    {
        SetLatchModifier(value, Shift, latchTime);
    }

    void SetOption(bool value, int latchTime)
    {
        SetLatchModifier(value, Option, latchTime);
    }
    
    void SetControl(bool value, int latchTime)
    {
        SetLatchModifier(value, Control, latchTime);
    }
    
    void SetAlt(bool value, int latchTime)
    {
        SetLatchModifier(value, Alt, latchTime);
    }
  
    void SetFlip(bool value, int latchTime)
    {
        SetLatchModifier(value, Flip, latchTime);
    }
  
    void SetGlobal(bool value, int latchTime)
    {
        SetLatchModifier(value, Global, latchTime);
    }
    
    void SetMarker(bool value, int latchTime)
    {
        modifiers_[Nudge].isEngaged = false;
        modifiers_[Zoom].isEngaged = false;
        modifiers_[Scrub].isEngaged = false;

        SetLatchModifier(value, Marker, latchTime);
    }
    
    void SetNudge(bool value, int latchTime)
    {
        modifiers_[Marker].isEngaged = false;
        modifiers_[Zoom].isEngaged = false;
        modifiers_[Scrub].isEngaged = false;

        SetLatchModifier(value, Nudge, latchTime);
    }
  
    void SetZoom(bool value, int latchTime)
    {
        modifiers_[Marker].isEngaged = false;
        modifiers_[Nudge].isEngaged = false;
        modifiers_[Scrub].isEngaged = false;

        SetLatchModifier(value, Zoom, latchTime);
    }
  
    void SetScrub(bool value, int latchTime)
    {
        modifiers_[Marker].isEngaged = false;
        modifiers_[Nudge].isEngaged = false;
        modifiers_[Zoom].isEngaged = false;

        SetLatchModifier(value, Scrub, latchTime);
    }
    
    void ClearModifiers()
    {
        for (int i = 0; i < MaxModifiers; ++i)
            modifiers_[i].isEngaged = false;
        
        RecalculateModifiers();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ChannelTouch
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    int channelNum;
    bool isTouched;
    
    ChannelTouch()
    {
        channelNum = 0;
        isTouched = false;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ChannelToggle
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    int channelNum;
    bool isToggled;
    
    ChannelToggle()
    {
        channelNum = 0;
        isToggled = false;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ControlSurface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{    
private:
    int *scrubModePtr_;
    int configScrubMode_;
    
    bool isRewinding_;
    bool isFastForwarding_;
    
    bool isTextLengthRestricted_;
    int restrictedTextLength_;
    
    bool usesLocalModifiers_;
    bool listensToModifiers_;
    
    int latchTime_;
        
    WDL_PtrList<FeedbackProcessor> trackColorFeedbackProcessors_; // does not own pointers
    
    WDL_TypedBuf<ChannelTouch> channelTouches_;
    WDL_TypedBuf<ChannelToggle> channelToggles_;

protected:
    WDL_StringKeyedArray<double> stepSize_;
    WDL_StringKeyedArray<WDL_IntKeyedArray<int>* > accelerationValuesForDecrement_;
    WDL_StringKeyedArray<WDL_IntKeyedArray<int>* > accelerationValuesForIncrement_;
    static void disposeIncDecAccelValues(WDL_IntKeyedArray<int> *accelValues) { delete  accelValues; }
    WDL_StringKeyedArray<vector<double>* > accelerationValues_;
    vector<double> emptyAccelerationValues_;
    
    static void disposeAccelValues(vector<double> *accelValues) { delete  accelValues; }
    
    void ProcessValues(const ptrvector<string_list> &lines);
    
    CSurfIntegrator *const csi_;
    Page *const page_;
    string const name_;
    ZoneManager *zoneManager_;
    ModifierManager *modifierManager_;
    
    int const numChannels_;
    int const channelOffset_;
    
    WDL_PtrList<Widget> widgets_; // owns list
    WDL_StringKeyedArray<Widget*> widgetsByName_;
    
    WDL_StringKeyedArray<CSIMessageGenerator*> CSIMessageGeneratorsByMessage_;
    static void disposeAction(CSIMessageGenerator *messageGenerator) { delete messageGenerator; }

    bool speedX5_;

    ControlSurface(CSurfIntegrator *const csi, Page *page, const string &name, int numChannels, int channelOffset) : csi_(csi), page_(page), name_(name), numChannels_(numChannels), channelOffset_(channelOffset), CSIMessageGeneratorsByMessage_(true, disposeAction), accelerationValues_(true, disposeAccelValues),
        accelerationValuesForDecrement_(true, disposeIncDecAccelValues), accelerationValuesForIncrement_(true, disposeIncDecAccelValues)
    {
        //private:
        scrubModePtr_ = NULL;
        configScrubMode_ = 0;

        isRewinding_ = false;
        isFastForwarding_ = false;
        
        isTextLengthRestricted_ = false;
        restrictedTextLength_ = 6;
        
        usesLocalModifiers_ = false;
        listensToModifiers_ = false;
        
        latchTime_ = 100;
        
        // protected
        zoneManager_ = NULL;
        modifierManager_ = new ModifierManager(csi_, NULL, this);
        speedX5_ = false;
        
        int size = 0;
        scrubModePtr_ = (int*)get_config_var("scrubmode", &size);
        
        for (int i = 1 ; i <= numChannels; i++)
        {
            ChannelTouch channelTouch;
            channelTouch.channelNum = i;
            channelTouches_.Add(channelTouch);
            
            ChannelToggle channelToggle;
            channelToggle.channelNum = i;
            channelToggles_.Add(channelToggle);
        }
    }
    
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
        if (isRewinding_)
            StopRewinding();
        else if (isFastForwarding_)
            StopFastForwarding();
    }
    
    virtual void InitHardwiredWidgets(ControlSurface *surface)
    {
        // Add the "hardwired" widgets
        AddWidget(new Widget(csi_, surface, "OnTrackSelection"));
        AddWidget(new Widget(csi_, surface, "OnPageEnter"));
        AddWidget(new Widget(csi_, surface, "OnPageLeave"));
        AddWidget(new Widget(csi_, surface, "OnInitialization"));
        AddWidget(new Widget(csi_, surface, "OnPlayStart"));
        AddWidget(new Widget(csi_, surface, "OnPlayStop"));
        AddWidget(new Widget(csi_, surface, "OnRecordStart"));
        AddWidget(new Widget(csi_, surface, "OnRecordStop"));
        AddWidget(new Widget(csi_, surface, "OnZoneActivation"));
        AddWidget(new Widget(csi_, surface, "OnZoneDeactivation"));
    }
    
public:
    virtual ~ControlSurface()
    {
        widgets_.Empty(true);
        delete zoneManager_;
        delete modifierManager_;
    }
    
    void Stop();
    void Play();
    void Record();
        
    virtual void RequestUpdate();
    void ForceClearTrack(int trackNum);
    void ForceUpdateTrackColors();
    void OnTrackSelection(MediaTrack *track);
    virtual void SetHasMCUMeters(int displayType) {}
    virtual void SendOSCMessage(const char *zoneName) {}
    virtual void SendOSCMessage(const char *zoneName, int value) {}
    virtual void SendOSCMessage(const char *zoneName, double value) {}
    virtual void SendOSCMessage(const char *zoneName, const char *value) {}

    virtual void HandleExternalInput() {}
    virtual void UpdateTimeDisplay() {}
    
    virtual void SendMidiSysExMessage(MIDI_event_ex_t *midiMessage) {}
    virtual void SendMidiMessage(int first, int second, int third) {}
    
    ModifierManager *GetModifierManager() { return modifierManager_; }
    ZoneManager *GetZoneManager() { return zoneManager_; }
    Page *GetPage() { return page_; }
    const char *GetName() { return name_.c_str(); }
    
    int GetNumChannels() { return numChannels_; }
    int GetChannelOffset() { return channelOffset_; }
    rgba_color GetTrackColorForChannel(int channel);

    bool GetIsRewinding() { return isRewinding_; }
    bool GetIsFastForwarding() { return isFastForwarding_; }

    bool GetUsesLocalModifiers() { return usesLocalModifiers_; }
    void ToggleUseLocalModifiers() { usesLocalModifiers_ = ! usesLocalModifiers_; }
    bool GetListensToModifiers() { return listensToModifiers_; }
    void SetListensToModifiers() { listensToModifiers_ = true; }

    void SetLatchTime(int latchTime) { latchTime_ = latchTime; }
    int GetLatchTime() { return latchTime_; }

    double GetStepSize(const char * const widgetClass)
    {
        if (stepSize_.Exists(widgetClass))
            return stepSize_.Get(widgetClass);
        else
            return 0;
    }

    const vector<double> *GetAccelerationValues(const char * const  widgetClass)
    {
        if (accelerationValues_.Exists(widgetClass))
            return accelerationValues_.Get(widgetClass);
        else
            return &emptyAccelerationValues_;
    }

    WDL_IntKeyedArray<int> *GetAccelerationValuesForDecrement(const char * const  widgetClass)
    {
         if (accelerationValuesForDecrement_.Exists(widgetClass))
             return accelerationValuesForDecrement_.Get(widgetClass);
        else
            return NULL;
    }
    
    WDL_IntKeyedArray<int> *GetAccelerationValuesForIncrement(const char * const  widgetClass)
    {
        if (accelerationValuesForIncrement_.Exists(widgetClass))
            return accelerationValuesForIncrement_.Get(widgetClass);
       else
           return NULL;
    }
    
    void TouchChannel(int channelNum, bool isTouched)
    {
        for (int i = 0; i < channelTouches_.GetSize(); ++i)
            if (channelTouches_.Get()[i].channelNum == channelNum)
            {
                channelTouches_.Get()[i].isTouched = isTouched;
                break;
            }
    }
    
    bool GetIsChannelTouched(int channelNum)
    {
        for (int i = 0; i < channelTouches_.GetSize(); ++i)
            if (channelTouches_.Get()[i].channelNum == channelNum)
                return channelTouches_.Get()[i].isTouched;

        return false;
    }
       
    void ToggleChannel(int channelNum)
    {
        for (int i = 0; i < channelToggles_.GetSize(); ++i)
            if (channelToggles_.Get()[i].channelNum == channelNum)
            {
                channelToggles_.Get()[i].isToggled = ! channelToggles_.Get()[i].isToggled;
                break;
            }
    }
    
    bool GetIsChannelToggled(int channelNum)
    {
        for (int i = 0; i < channelToggles_.GetSize(); ++i)
            if (channelToggles_.Get()[i].channelNum == channelNum)
                return channelToggles_.Get()[i].isToggled;

        return false;
    }

    void ToggleRestrictTextLength(int length)
    {
        isTextLengthRestricted_ = ! isTextLengthRestricted_;
        restrictedTextLength_ = length;
    }
    
    const char *GetRestrictedLengthText(const char *textc, char *buf, int bufsz) // may return textc if not restricted
    {
        if (isTextLengthRestricted_ && strlen(textc) > restrictedTextLength_ && WDL_NORMALLY(restrictedTextLength_ >= 0))
        {
            static const char * const filter_lists[3] = {
              " \t\r\n",
              " \t\r\n`~!@#$%^&*:()_|=?;:'\",",
              " \t\r\n`~!@#$%^&*:()_|=?;:'\",aeiou"
            };

            for (int pass = 0; pass < 3; pass ++)
            {
                const char *rd = textc;
                int l = 0;
                while (*rd && l < bufsz-1 && l <= restrictedTextLength_)
                {
                     if (!l || !strchr(filter_lists[pass], *rd))
                         buf[l++] = *rd;
                     rd++;
                }
                if (pass < 2 && l > restrictedTextLength_) continue; // keep filtering

                buf[wdl_min(restrictedTextLength_, l)] = 0;
                return buf;
            }
        }
        return textc;

    }
           
    void AddTrackColorFeedbackProcessor(FeedbackProcessor *feedbackProcessor) // does not own this pointer
    {
        if (WDL_NOT_NORMALLY(!feedbackProcessor)) { return; }
        trackColorFeedbackProcessors_.Add(feedbackProcessor);
    }
        
    void ForceClear()
    {
        for (int i = 0; i < widgets_.GetSize(); ++i)
            widgets_.Get(i)->ForceClear();
    }
           
    void TrackFXListChanged(MediaTrack *track)
    {
        OnTrackSelection(track);
    }

    void DoWidgetAction(const char *name, double v=1.0)
    {
        Widget *w = widgetsByName_.Get(name);
        if (w) zoneManager_->DoAction(w, 1.0);
    }
    
    void HandleStop()
    {
        DoWidgetAction("OnRecordStop");
        DoWidgetAction("OnPlayStop");
    }
    
    void HandlePlay()
    {
        DoWidgetAction("OnPlayStart");
    }
    
    void HandleRecord()
    {
        DoWidgetAction("OnRecordStart");
    }
        
    void StartRewinding()
    {
        if (isFastForwarding_)
            StopFastForwarding();

        if (isRewinding_) // on 2nd, 3rd, etc. press
        {
            speedX5_ = ! speedX5_;
            return;
        }
        
        int playState = GetPlayState();
        if (playState == 1 || playState == 2 || playState == 5 || playState == 6) // playing or paused or recording or paused whilst recording
            SetEditCurPos(GetPlayPosition(), true, false);

        CSurf_OnStop();
        
        isRewinding_ = true;
        configScrubMode_ = *scrubModePtr_;
        *scrubModePtr_ = 2;
    }
       
    void StartFastForwarding()
    {
        if (isRewinding_)
            StopRewinding();

        if (isFastForwarding_) // on 2nd, 3rd, etc. press
        {
            speedX5_ = ! speedX5_;
            return;
        }
        
        int playState = GetPlayState();
        if (playState == 1 || playState == 2 || playState == 5 || playState == 6) // playing or paused or recording or paused whilst recording
            SetEditCurPos(GetPlayPosition(), true, false);

        CSurf_OnStop();
        
        isFastForwarding_ = true;
        configScrubMode_ = *scrubModePtr_;
        *scrubModePtr_ = 2;
    }
           
    void AddWidget(Widget *widget)
    {
        if (WDL_NOT_NORMALLY(!widget)) return;
        widgets_.Add(widget);
        widgetsByName_.Insert(widget->GetName(),widget);
    }
    
    void AddCSIMessageGenerator(const char *message, CSIMessageGenerator *messageGenerator)
    {
        if (WDL_NOT_NORMALLY(!messageGenerator)) { return; }
        CSIMessageGeneratorsByMessage_.Insert(message, messageGenerator);
    }

    Widget *GetWidgetByName(const char *name)
    {
      return widgetsByName_.Get(name);
    }
    
    void OnPageEnter()
    {
        ForceClear();
        
        DoWidgetAction("OnPageEnter");
    }
    
    void OnPageLeave()
    {
        ForceClear();
        
        DoWidgetAction("OnPageLeave");
    }
    
    void OnInitialization()
    {
        DoWidgetAction("OnInitialization");
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
    
    const WDL_TypedBuf<int> &GetModifiers();
    void ClearModifiers();
    void ClearModifier(const char *modifier);

    void UpdateCurrentActionContextModifiers()
    {
        if (! usesLocalModifiers_)
            GetZoneManager()->UpdateCurrentActionContextModifiers();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    CSurfIntegrator *const csi_;
    double lastDoubleValue_;
    string lastStringValue_;
    rgba_color lastColor_;
    
    Widget  *const widget_;
    
public:
    FeedbackProcessor(CSurfIntegrator *const csi, Widget *widget) : csi_(csi), widget_(widget)
    {
        lastDoubleValue_ = 0.0;
    }
    virtual ~FeedbackProcessor() {}
    virtual const char *GetName()  { return "FeedbackProcessor"; }
    Widget *GetWidget() { return widget_; }
    virtual void Configure(const WDL_PtrList<ActionContext> &contexts) {}
    virtual void ForceValue(const PropertyList &properties, double value) {}
    virtual void ForceValue(const PropertyList &properties, const char * const &value) {}
    virtual void ForceColorValue(const rgba_color &color) {}
    virtual void ForceUpdateTrackColors() {}
    virtual void RunDeferredActions() {}
    virtual void ForceClear() {}
    
    virtual void UpdateTrackColors() {}
    virtual void SetXTouchDisplayColors(const char *colors) {}
    virtual void RestoreXTouchDisplayColors() {}

    virtual void SetColorValue(const rgba_color &color) {}

    virtual void SetValue(const PropertyList &properties, double value)
    {
        if (lastDoubleValue_ != value)
        {
            lastDoubleValue_ = value;
            ForceValue(properties, value);
        }
    }
    
    virtual void SetValue(const PropertyList &properties, const char * const & value)
    {
        if (lastStringValue_ != value)
        {
            lastStringValue_ = value;
            ForceValue(properties, value);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Midi_CSIMessageGenerator : public CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    Midi_CSIMessageGenerator(CSurfIntegrator *const csi, Widget *widget) : CSIMessageGenerator(csi, widget) {}
    
public:
    virtual ~Midi_CSIMessageGenerator() {}
    virtual void ProcessMidiMessage(const MIDI_event_ex_t *midiMessage) {}
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Midi_FeedbackProcessor : public FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    Midi_ControlSurface *const surface_;
    
    MIDI_event_ex_t *lastMessageSent_;
    MIDI_event_ex_t *midiFeedbackMessage1_;
    MIDI_event_ex_t *midiFeedbackMessage2_;
    
    Midi_FeedbackProcessor(CSurfIntegrator *const csi, Midi_ControlSurface *surface, Widget *widget, MIDI_event_ex_t *feedback1 = NULL, MIDI_event_ex_t *feedback2 = NULL) : FeedbackProcessor(csi, widget), surface_(surface)
    {
        lastMessageSent_ = new MIDI_event_ex_t(0, 0, 0);
        midiFeedbackMessage1_ = feedback1 ? feedback1 : new MIDI_event_ex_t(0, 0, 0);
        midiFeedbackMessage2_ = feedback2 ? feedback2 : new MIDI_event_ex_t(0, 0, 0);
    }
    
    void SendMidiSysExMessage(MIDI_event_ex_t *midiMessage);
    void SendMidiMessage(int first, int second, int third);
    void ForceMidiMessage(int first, int second, int third);

public:
    ~Midi_FeedbackProcessor()
    {
        if (lastMessageSent_ != NULL)
            delete lastMessageSent_;
        
        if (midiFeedbackMessage1_ != NULL)
            delete midiFeedbackMessage1_;
        
        if (midiFeedbackMessage2_ != NULL)
            delete midiFeedbackMessage2_;
    }
    
    virtual const char *GetName() override { return "Midi_FeedbackProcessor"; }
};

void ReleaseMidiInput(midi_Input *input);
void ReleaseMidiOutput(midi_Output *output);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Midi_ControlSurfaceIO
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    CSurfIntegrator *const csi_;
    string const name_;
    midi_Input *const midiInput_;
    midi_Output *const midiOutput_;
    
public:
    Midi_ControlSurfaceIO(CSurfIntegrator *csi, const char *name, midi_Input *midiInput, midi_Output *midiOutput, int surfaceRefreshRate) : csi_(csi), name_(name), midiInput_(midiInput), midiOutput_(midiOutput), surfaceRefreshRate_(surfaceRefreshRate) {}

    ~Midi_ControlSurfaceIO()
    {
        if (midiInput_) ReleaseMidiInput(midiInput_);
        if (midiOutput_) ReleaseMidiOutput(midiOutput_);
    }
    
    int surfaceRefreshRate_;

    
    const char *GetName() { return name_.c_str(); }
    
    void HandleExternalInput(Midi_ControlSurface *surface);
    
    void SendMidiMessage(MIDI_event_ex_t *midiMessage)
    {
        if (midiOutput_)
            midiOutput_->SendMsg(midiMessage, -1);
    }

    void SendMidiMessage(int first, int second, int third)
    {
        if (midiOutput_)
            midiOutput_->Send(first, second, third, -1);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Midi_ControlSurface : public ControlSurface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    Midi_ControlSurfaceIO *const surfaceIO_;
    WDL_IntKeyedArray<Midi_CSIMessageGenerator*> Midi_CSIMessageGeneratorsByMessage_;
    static void disposeAction(Midi_CSIMessageGenerator *messageGenerator) { delete messageGenerator; }
    
    DWORD lastRun_;

    void ProcessMidiWidget(int &lineNumber, fpistream &surfaceTemplateFile, const string_list &in_tokens);
    
    void ProcessMIDIWidgetFile(const string &filePath, Midi_ControlSurface *surface);
    
    // special processing for MCU meters
    bool hasMCUMeters_;
    int displayType_;

    void InitializeMCU();
    void InitializeMCUXT();
    
    virtual void InitializeMeters()
    {
        if (hasMCUMeters_)
        {
            if (displayType_ == 0x14)
                InitializeMCU();
            else
                InitializeMCUXT();
        }
    }

    void SendSysexInitData(int line[], int numElem);
    
public:
    Midi_ControlSurface(CSurfIntegrator *const csi, Page *page, const char *name, int numChannels, int channelOffset, const char *templateFilename, const char *zoneFolder, const char *fxZoneFolder, Midi_ControlSurfaceIO *surfaceIO);

    virtual ~Midi_ControlSurface() {}
    
    void ProcessMidiMessage(const MIDI_event_ex_t *evt);
    virtual void SendMidiSysExMessage(MIDI_event_ex_t *midiMessage) override;
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
        
    void AddCSIMessageGenerator(int messageKey, Midi_CSIMessageGenerator *messageGenerator)
    {
        if (WDL_NOT_NORMALLY(!messageGenerator)) return;
        Midi_CSIMessageGeneratorsByMessage_.Insert(messageKey, messageGenerator);
    }
    
    virtual void RequestUpdate() override
    {
        const DWORD now = GetTickCount();
        if ((now - lastRun_) < (1000/max((surfaceIO_->surfaceRefreshRate_),1))) return;
        lastRun_=now;

        ControlSurface::RequestUpdate();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_FeedbackProcessor : public FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    OSC_ControlSurface *const surface_;
    string const oscAddress_;
    
public:
    OSC_FeedbackProcessor(CSurfIntegrator *const csi, OSC_ControlSurface *surface, Widget *widget, const char *oscAddress) : FeedbackProcessor(csi, widget), surface_(surface), oscAddress_(oscAddress) {}
    ~OSC_FeedbackProcessor() {}

    virtual const char *GetName() override { return "OSC_FeedbackProcessor"; }

    virtual void SetColorValue(const rgba_color &color) override;
    virtual void ForceValue(const PropertyList &properties, double value) override;
    virtual void ForceValue(const PropertyList &properties, const char * const &value) override;
    virtual void ForceClear() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_IntFeedbackProcessor : public OSC_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    OSC_IntFeedbackProcessor(CSurfIntegrator *const csi, OSC_ControlSurface *surface, Widget *widget, const char *oscAddress) : OSC_FeedbackProcessor(csi, surface, widget, oscAddress) {}
    ~OSC_IntFeedbackProcessor() {}

    virtual const char *GetName() override { return "OSC_IntFeedbackProcessor"; }

    virtual void ForceValue(const PropertyList &properties, double value) override;
    virtual void ForceClear() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_ControlSurfaceIO
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    CSurfIntegrator *const csi_;
    string const name_;
    oscpkt::UdpSocket *inSocket_;
    oscpkt::UdpSocket *outSocket_;
    oscpkt::PacketReader packetReader_;
    oscpkt::PacketWriter packetWriter_;
    oscpkt::Storage storageTmp_;
    int maxBundleSize_; // 0 = no bundles (would only be useful if the destination doesn't support bundles)
    int maxPacketsPerRun_; // 0 = no limit
    int sentPacketCount_; // count of packets sent this Run() slice, after maxPacketsPerRun_ packtees go into packetQueue_
    WDL_Queue packetQueue_;
    
public:
    OSC_ControlSurfaceIO(CSurfIntegrator *const csi, const char *name, const char *receiveOnPort, const char *transmitToPort, const char *transmitToIpAddress, int maxPacketsPerRun);
    virtual ~OSC_ControlSurfaceIO();

    const char *GetName() { return name_.c_str(); }

    virtual void HandleExternalInput(OSC_ControlSurface *surface);

    void QueuePacket(const void *p, int sz)
    {
        if (WDL_NOT_NORMALLY(!outSocket_)) return;
        if (WDL_NOT_NORMALLY(!p || sz < 1)) return;
        if (WDL_NOT_NORMALLY(packetQueue_.GetSize() > 32*1024*1024)) return; // drop packets after 32MB queued
        if (maxPacketsPerRun_ != 0 && sentPacketCount_ >= maxPacketsPerRun_)
        {
            void *wr = packetQueue_.Add(NULL,sz + sizeof(int));
            if (WDL_NORMALLY(wr != NULL))
            {
                memcpy(wr, &sz, sizeof(int));
                memcpy((char *)wr + sizeof(int), p, sz);
            }
        }
        else
        {
            outSocket_->sendPacket(p, sz);
            sentPacketCount_++;
        }
    }

    void QueueOSCMessage(oscpkt::Message *message) // NULL message flushes any latent bundles
    {
        if (outSocket_ != NULL && outSocket_->isOk())
        {
            if (maxBundleSize_ > 0 && packetWriter_.packetSize() > 0)
            {
                bool send_bundle;
                if (message)
                {
                    // oscpkt lacks the ability to calculate the size of a Message?
                    storageTmp_.clear();
                    message->packMessage(storageTmp_, true);
                    send_bundle = (packetWriter_.packetSize() + storageTmp_.size() > maxBundleSize_);
                }
                else
                {
                    send_bundle = true;
                }

                if (send_bundle)
                {
                    packetWriter_.endBundle();
                    QueuePacket(packetWriter_.packetData(), packetWriter_.packetSize());
                    packetWriter_.init();
                }
            }

            if (message)
            {
                if (maxBundleSize_ > 0 && packetWriter_.packetSize() == 0)
                {
                    packetWriter_.startBundle();
                }

                packetWriter_.addMessage(*message);

                if (maxBundleSize_ <= 0)
                {
                    QueuePacket(packetWriter_.packetData(), packetWriter_.packetSize());
                    packetWriter_.init();
                }
            }
        }
    }
    
    void SendOSCMessage(const char *oscAddress, double value)
    {
        if (outSocket_ != NULL && outSocket_->isOk())
        {
            oscpkt::Message message;
            message.init(oscAddress).pushFloat((float)value);
            QueueOSCMessage(&message);
        }
    }
    
    void SendOSCMessage(const char *oscAddress, int value)
    {
        if (outSocket_ != NULL && outSocket_->isOk())
        {
            oscpkt::Message message;
            message.init(oscAddress).pushInt32(value);
            QueueOSCMessage(&message);
        }
    }
    
    void SendOSCMessage(const char *oscAddress, const char *value)
    {
        if (outSocket_ != NULL && outSocket_->isOk())
        {
            oscpkt::Message message;
            message.init(oscAddress).pushStr(value);
            QueueOSCMessage(&message);
        }
    }
    
    void SendOSCMessage(const char *value)
    {
        if (outSocket_ != NULL && outSocket_->isOk())
        {
            oscpkt::Message message;
            message.init(value);
            QueueOSCMessage(&message);
        }
    }
    
    void BeginRun()
    {
        sentPacketCount_ = 0;
        // send any latent packets first
        while (packetQueue_.GetSize()>=sizeof(int))
        {
            int sza;
            if (maxPacketsPerRun_ != 0 && sentPacketCount_ >= maxPacketsPerRun_) break;

            memcpy(&sza, packetQueue_.Get(), sizeof(int));
            packetQueue_.Advance(sizeof(int));
            if (WDL_NOT_NORMALLY(sza < 0 || packetQueue_.GetSize() < sza))
            {
                packetQueue_.Clear();
            }
            else
            {
                if (WDL_NORMALLY(outSocket_ != NULL))
                {
                    outSocket_->sendPacket(packetQueue_.Get(), sza);
                }
                packetQueue_.Advance(sza);
                sentPacketCount_++;
            }
        }
        packetQueue_.Compact();
    }

    virtual void Run()
    {
        QueueOSCMessage(NULL); // flush any latent bundles
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_X32ControlSurfaceIO :public OSC_ControlSurfaceIO
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    DWORD X32HeartBeatRefreshInterval_;
    DWORD X32HeartBeatLastRefreshTime_;
    
public:
    OSC_X32ControlSurfaceIO(CSurfIntegrator *const csi, const char *name, const char *receiveOnPort, const char *transmitToPort, const char *transmitToIpAddress, int maxPacketsPerRun);
    virtual ~OSC_X32ControlSurfaceIO() {}

    virtual void HandleExternalInput(OSC_ControlSurface *surface) override;

    void Run() override
    {
        DWORD currentTime = GetTickCount();

        if ((currentTime - X32HeartBeatLastRefreshTime_) > X32HeartBeatRefreshInterval_)
        {
            X32HeartBeatLastRefreshTime_ = currentTime;
            SendOSCMessage("/xremote");
        }
        
        OSC_ControlSurfaceIO::Run();
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_ControlSurface : public ControlSurface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    OSC_ControlSurfaceIO *const surfaceIO_;
    void ProcessOSCWidget(int &lineNumber, fpistream &surfaceTemplateFile, const string_list &in_tokens);
    void ProcessOSCWidgetFile(const string &filePath);
public:
    OSC_ControlSurface(CSurfIntegrator *const csi, Page *page, const char *name, int numChannels, int channelOffset, const char *templateFilename, const char *zoneFolder, const char *fxZoneFolder, OSC_ControlSurfaceIO *surfaceIO);

    virtual ~OSC_ControlSurface() {}
    
    void ProcessOSCMessage(const char *message, double value);
    void SendOSCMessage(OSC_FeedbackProcessor *feedbackProcessor, const char *oscAddress, double value);
    void SendOSCMessage(OSC_FeedbackProcessor *feedbackProcessor, const char *oscAddress, int value);
    void SendOSCMessage(OSC_FeedbackProcessor *feedbackProcessor, const char *oscAddress, const char *value);
    virtual void SendOSCMessage(const char *zoneName) override;
    virtual void SendOSCMessage(const char *zoneName, int value) override;
    virtual void SendOSCMessage(const char *zoneName, double value) override;
    virtual void SendOSCMessage(const char *zoneName, const char *value) override;

    virtual void RequestUpdate() override
    {
        surfaceIO_->BeginRun();
        ControlSurface::RequestUpdate();
        surfaceIO_->Run();
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
protected:
    CSurfIntegrator *const csi_;
    Page *const page_;
    bool followMCP_;
    bool synchPages_;
    bool isScrollLinkEnabled_;
    bool isScrollSynchEnabled_;
    int currentTrackVCAFolderMode_;
    int targetScrollLinkChannel_;
    int trackOffset_;
    int vcaTrackOffset_;
    int folderTrackOffset_;
    int selectedTracksOffset_;
    WDL_PtrList<MediaTrack> tracks_;
    WDL_PtrList<MediaTrack> selectedTracks_;
    
    WDL_PtrList<MediaTrack> vcaTopLeadTracks_;
    MediaTrack *            vcaLeadTrack_;
    WDL_PtrList<MediaTrack> vcaLeadTracks_;
    WDL_PtrList<MediaTrack> vcaSpillTracks_;
    
    WDL_PtrList<MediaTrack> folderTopParentTracks_;
    MediaTrack *            folderParentTrack_;
    WDL_PtrList<MediaTrack> folderParentTracks_;
    WDL_PtrList<MediaTrack> folderSpillTracks_;
    WDL_PointerKeyedArray<MediaTrack*, WDL_PtrList<MediaTrack>* > folderDictionary_;
    static void disposeFolderParents(WDL_PtrList<MediaTrack> *parent) { delete parent;  }
 
    WDL_PtrList<Navigator> trackNavigators_;
    Navigator *const masterTrackNavigator_;
    Navigator *selectedTrackNavigator_;
    Navigator *focusedFXNavigator_;
    
    void ForceScrollLink()
    {
        // Make sure selected track is visble on the control surface
        MediaTrack *selectedTrack = GetSelectedTrack();
        
        if (selectedTrack != NULL)
        {
            for (int i = 0; i < trackNavigators_.GetSize(); ++i)
                if (selectedTrack == trackNavigators_.Get(i)->GetTrack())
                    return;
            
            for (int i = 1; i <= GetNumTracks(); i++)
            {
                if (selectedTrack == GetTrackFromId(i))
                {
                    trackOffset_ = i - 1;
                    break;
                }
            }
            
            trackOffset_ -= targetScrollLinkChannel_;
            
            if (trackOffset_ <  0)
                trackOffset_ =  0;
            
            int top = GetNumTracks() - trackNavigators_.GetSize();
            
            if (trackOffset_ >  top)
                trackOffset_ = top;
        }
    }
    
public:
    TrackNavigationManager(CSurfIntegrator *const csi, Page *page, bool followMCP,  bool synchPages, bool isScrollLinkEnabled, bool isScrollSynchEnabled) :
    csi_(csi),
    page_(page),
    followMCP_(followMCP),
    synchPages_(synchPages),
    isScrollLinkEnabled_(isScrollLinkEnabled),
    isScrollSynchEnabled_(isScrollSynchEnabled),
    masterTrackNavigator_(new MasterTrackNavigator(csi_, page_)),
    selectedTrackNavigator_(new SelectedTrackNavigator(csi_, page_)),
    focusedFXNavigator_(new FocusedFXNavigator(csi_, page_)),
    folderDictionary_(disposeFolderParents)
    {
        //private:
        currentTrackVCAFolderMode_ = 0;
        targetScrollLinkChannel_ = 0;
        trackOffset_ = 0;
        vcaTrackOffset_ = 0;
        folderTrackOffset_ = 0;
        selectedTracksOffset_ = 0;
        vcaLeadTrack_ = NULL;
        folderParentTrack_ = NULL;
    }
    ~TrackNavigationManager()
    {
        delete masterTrackNavigator_;
        delete selectedTrackNavigator_;
        delete focusedFXNavigator_;
        
        trackNavigators_.Empty(true);
    }
    
    void RebuildTracks();
    void RebuildSelectedTracks();
    void AdjustSelectedTrackBank(int amount);
    bool GetSynchPages() { return synchPages_; }
    bool GetScrollLink() { return isScrollLinkEnabled_; }
    int  GetNumTracks() { return CSurf_NumTracks(followMCP_); }
    Navigator *GetMasterTrackNavigator() { return masterTrackNavigator_; }
    Navigator *GetSelectedTrackNavigator() { return selectedTrackNavigator_; }
    Navigator *GetFocusedFXNavigator() { return focusedFXNavigator_; }
    
    bool GetIsTrackVisible(MediaTrack *track)
    {
        return IsTrackVisible(track, followMCP_);
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
        if (currentTrackVCAFolderMode_ == 1)
            currentTrackVCAFolderMode_ = 0;
    }
    
    void FolderModeDeactivated()
    {
        if (currentTrackVCAFolderMode_ == 2)
            currentTrackVCAFolderMode_ = 0;
    }
    
    void SelectedTracksModeDeactivated()
    {
        if (currentTrackVCAFolderMode_ == 3)
            currentTrackVCAFolderMode_ = 0;
    }
    
    string GetCurrentTrackVCAFolderModeDisplay()
    {
        if (currentTrackVCAFolderMode_ == 0)
            return "Track";
        else if (currentTrackVCAFolderMode_ == 1)
            return "VCA";
        else if (currentTrackVCAFolderMode_ == 2)
            return "Folder";
        else if (currentTrackVCAFolderMode_ == 3)
            return "SelectedTracks";
        else
            return "";
    }
    static const char *GetAutoModeDisplayNameNoOverride(int modeIndex)
    {
        switch (modeIndex)
        {
            case 0: return "Trim";
            case 1: return "Read";
            case 2: return "Touch";
            case 3: return "Write";
            case 4: return "Latch";
            case 5: return "LtchPre";
            default: WDL_ASSERT(false); return "?";
        }
    }

    const char *GetAutoModeDisplayName(int modeIndex)
    {
        int globalOverride = GetGlobalAutomationOverride();

        if (globalOverride > -1) // -1=no override, 0=trim/read, 1=read, 2=touch, 3=write, 4=latch, 5=bypass
            return GetAutoModeDisplayNameNoOverride(globalOverride);
        else
            return GetAutoModeDisplayNameNoOverride(modeIndex);
    }

    const char *GetGlobalAutoModeDisplayName()
    {
        int globalOverride = GetGlobalAutomationOverride();

        if (globalOverride == -1)
            return "NoOverride";
        else if (globalOverride > -1) // -1=no override, 0=trim/read, 1=read, 2=touch, 3=write, 4=latch, 5=bypass
            return GetAutoModeDisplayNameNoOverride(globalOverride);
        else
            return "";
    }

    void NextInputMonitorMode(MediaTrack *track)
    {
        // I_RECMON : int  *: record monitor (0=off, 1=normal, 2=not when playing (tapestyle))
        int recMonitorMode = (int)GetMediaTrackInfo_Value(track,"I_RECMON");

        // I_RECMONITEMS : int  *: monitor items while recording (0=off, 1=on)
        int recMonitorItemMode = (int)GetMediaTrackInfo_Value(track,"I_RECMONITEMS");

        if (recMonitorMode == 0)
        {
            recMonitorMode = 1;
            recMonitorItemMode = 0;
        }
        else if (recMonitorMode == 1 && recMonitorItemMode == 0)
        {
            recMonitorMode = 2;
            recMonitorItemMode = 0;
        }
        else if (recMonitorMode == 2 && recMonitorItemMode == 0)
        {
            recMonitorMode = 1;
            recMonitorItemMode = 1;
        }
        else if (recMonitorMode == 1 && recMonitorItemMode == 1)
        {
            recMonitorMode = 2;
            recMonitorItemMode = 1;
        }
        else if (recMonitorMode == 2 && recMonitorItemMode == 1)
        {
            recMonitorMode = 0;
            recMonitorItemMode = 0;
        }

        GetSetMediaTrackInfo(track, "I_RECMON", &recMonitorMode);
        GetSetMediaTrackInfo(track, "I_RECMONITEMS", &recMonitorItemMode);
    }
    
    const char *GetCurrentInputMonitorMode(MediaTrack *track)
    {
        // I_RECMON : int  *: record monitor (0=off, 1=normal, 2=not when playing (tapestyle))
        int recMonitorMode = (int)GetMediaTrackInfo_Value(track,"I_RECMON");

        // I_RECMONITEMS : int  *: monitor items while recording (0=off, 1=on)
        int recMonitorItemMode = (int)GetMediaTrackInfo_Value(track,"I_RECMONITEMS");

        if (recMonitorMode == 0)
            return "Off";
        else if (recMonitorMode == 1 && recMonitorItemMode == 0)
            return "Input";
        else if (recMonitorMode == 2 && recMonitorItemMode == 0)
            return "Auto";
        else if (recMonitorMode == 1 && recMonitorItemMode == 1)
            return "Input+";
        else if (recMonitorMode == 2 && recMonitorItemMode == 1)
            return "Auto+";
        else
            return "";
    }
    
    const WDL_PtrList<MediaTrack> &GetSelectedTracks()
    {
        selectedTracks_.Empty();
        
        for (int i = 0; i < CountSelectedTracks2(NULL, false); i++)
            selectedTracks_.Add(DAW::GetSelectedTrack(i));
        
        return selectedTracks_;
    }

    void SetTrackOffset(int trackOffset)
    {
        if (isScrollSynchEnabled_)
            trackOffset_ = trackOffset;
    }
    
    void AdjustTrackBank(int amount)
    {
        if (currentTrackVCAFolderMode_ != 0)
            return;

        int numTracks = tracks_.GetSize();
        
        if (numTracks <= trackNavigators_.GetSize())
            return;
       
        trackOffset_ += amount;
        
        if (trackOffset_ <  0)
            trackOffset_ =  0;
        
        int top = numTracks - trackNavigators_.GetSize();
        
        if (trackOffset_ >  top)
            trackOffset_ = top;
        
        if (isScrollSynchEnabled_)
        {
            int offset = trackOffset_;
            
            offset++;
            
            if (MediaTrack *leftmostTrack = DAW::GetTrack(offset))
                SetMixerScroll(leftmostTrack);
        }
    }
    
    void AdjustVCABank(int amount)
    {
        if (currentTrackVCAFolderMode_ != 1)
            return;

        int numTracks = vcaSpillTracks_.GetSize();
            
        if (numTracks <= trackNavigators_.GetSize())
            return;
       
        vcaTrackOffset_ += amount;
        
        if (vcaTrackOffset_ <  0)
            vcaTrackOffset_ =  0;
        
        int top = numTracks - trackNavigators_.GetSize();
        
        if (vcaTrackOffset_ >  top)
            vcaTrackOffset_ = top;
    }
    
    void AdjustFolderBank(int amount)
    {
        if (currentTrackVCAFolderMode_ != 2)
            return;

        int numTracks = folderSpillTracks_.GetSize();
        
        if (numTracks <= trackNavigators_.GetSize())
            return;
       
        folderTrackOffset_ += amount;
        
        if (folderTrackOffset_ <  0)
            folderTrackOffset_ =  0;
        
        int top = numTracks - trackNavigators_.GetSize();
        
        if (folderTrackOffset_ >  top)
            folderTrackOffset_ = top;
    }
    
    void AdjustSelectedTracksBank(int amount)
    {
        if (currentTrackVCAFolderMode_ != 3)
            return;

        int numTracks = selectedTracks_.GetSize();
       
        if (numTracks <= trackNavigators_.GetSize())
            return;
        
        selectedTracksOffset_ += amount;
        
        if (selectedTracksOffset_ < 0)
            selectedTracksOffset_ = 0;
        
        int top = numTracks - trackNavigators_.GetSize();
        
        if (selectedTracksOffset_ > top)
            selectedTracksOffset_ = top;
    }
    
    Navigator *GetNavigatorForChannel(int channelNum)
    {
        for (int i = 0; i < trackNavigators_.GetSize(); ++i)
            if (trackNavigators_.Get(i)->GetChannelNum() == channelNum)
                return trackNavigators_.Get(i);
          
        TrackNavigator *newNavigator = new TrackNavigator(csi_, page_, this, channelNum);
        
        trackNavigators_.Add(newNavigator);
            
        return newNavigator;
    }
    
    MediaTrack *GetTrackFromChannel(int channelNumber)
    {       
        if (currentTrackVCAFolderMode_ == 0)
        {
            if (channelNumber + trackOffset_ < GetNumTracks() && channelNumber + trackOffset_ < tracks_.GetSize())
                return tracks_.Get(channelNumber + trackOffset_);
            else
                return NULL;
        }
        else if (currentTrackVCAFolderMode_ == 1)
        {
            if (vcaLeadTrack_ == NULL)
            {
                if (channelNumber < vcaTopLeadTracks_.GetSize() && DAW::ValidateTrackPtr(vcaTopLeadTracks_.Get(channelNumber)))
                    return vcaTopLeadTracks_.Get(channelNumber);
                else
                    return NULL;
            }
            else
            {
                if (channelNumber == 0 && vcaSpillTracks_.GetSize() > 0 && DAW::ValidateTrackPtr(vcaSpillTracks_.Get(channelNumber)))
                    return vcaSpillTracks_.Get(channelNumber);
                else if (vcaTrackOffset_ == 0 && channelNumber < vcaSpillTracks_.GetSize() && DAW::ValidateTrackPtr(vcaSpillTracks_.Get(channelNumber)))
                    return vcaSpillTracks_.Get(channelNumber);
                else
                {
                    channelNumber += vcaTrackOffset_;
                    
                    if (channelNumber < vcaSpillTracks_.GetSize() && DAW::ValidateTrackPtr(vcaSpillTracks_.Get(channelNumber)))
                        return vcaSpillTracks_.Get(channelNumber);
                }
            }
        }
        else if (currentTrackVCAFolderMode_ == 2)
        {
            if (folderParentTrack_ == NULL)
            {
                if (channelNumber < folderTopParentTracks_.GetSize() && DAW::ValidateTrackPtr(folderTopParentTracks_.Get(channelNumber)))
                    return folderTopParentTracks_.Get(channelNumber);
                else
                    return NULL;
            }
            else
            {
                if (channelNumber == 0 && folderSpillTracks_.GetSize() > 0 && DAW::ValidateTrackPtr(folderSpillTracks_.Get(channelNumber)))
                    return folderSpillTracks_.Get(channelNumber);
                else if (folderTrackOffset_ == 0 && channelNumber < folderSpillTracks_.GetSize() && DAW::ValidateTrackPtr(folderSpillTracks_.Get(channelNumber)))
                    return folderSpillTracks_.Get(channelNumber);
                else
                {
                    channelNumber += folderTrackOffset_;
                    
                    if (channelNumber < folderSpillTracks_.GetSize() && DAW::ValidateTrackPtr(folderSpillTracks_.Get(channelNumber)))
                        return folderSpillTracks_.Get(channelNumber);
                }
            }
        }
        else if (currentTrackVCAFolderMode_ == 3)
        {
            if (channelNumber + selectedTracksOffset_ >= selectedTracks_.GetSize())
                return NULL;
            else
                return selectedTracks_.Get(channelNumber + selectedTracksOffset_);
        }
        
        return NULL;
    }
    
    MediaTrack *GetTrackFromId(int trackNumber)
    {
        if (trackNumber <= GetNumTracks())
            return CSurf_TrackFromID(trackNumber, followMCP_);
        else
            return NULL;
    }
    
    int GetIdFromTrack(MediaTrack *track)
    {
        return CSurf_TrackToID(track, followMCP_);
    }
    
    bool GetIsVCASpilled(MediaTrack *track)
    {
        if (vcaLeadTrack_ == NULL && (DAW::GetTrackGroupMembership(track, "VOLUME_VCA_LEAD") != 0 || DAW::GetTrackGroupMembershipHigh(track, "VOLUME_VCA_LEAD") != 0))
            return true;
        else if (vcaLeadTrack_ == track)
            return true;
        else
            return false;
    }
    
    void ToggleVCASpill(MediaTrack *track)
    {
        if (currentTrackVCAFolderMode_ != 1)
            return;
        
        if (DAW::GetTrackGroupMembership(track, "VOLUME_VCA_LEAD") == 0 && DAW::GetTrackGroupMembershipHigh(track, "VOLUME_VCA_LEAD") == 0)
            return;

        if (vcaLeadTrack_ == track)
        {
            if (vcaLeadTracks_.GetSize() > 0)
            {
                vcaLeadTrack_ = vcaLeadTracks_.Get(vcaLeadTracks_.GetSize() - 1);
                vcaLeadTracks_.Delete(vcaLeadTracks_.GetSize() - 1);
            }
            else
                vcaLeadTrack_ = NULL;
        }
        else if (vcaLeadTrack_ != NULL)
        {
            vcaLeadTracks_.Add(vcaLeadTrack_);
            vcaLeadTrack_ = track;
        }
        else
            vcaLeadTrack_ = track;
       
        vcaTrackOffset_ = 0;
    }

    bool GetIsFolderSpilled(MediaTrack *track)
    {
        if (folderTopParentTracks_.Find(track) >= 0)
            return true;
        else if (GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH") == 1)
            return true;
        else
            return false;
    }

    void ToggleFolderSpill(MediaTrack *track)
    {
        if (currentTrackVCAFolderMode_ != 2)
            return;
        
        if (folderTopParentTracks_.GetSize() == 0)
            return;

        else if (GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH") != 1)
            return;
        
        if (folderParentTrack_ == track)
        {
            if (folderParentTracks_.GetSize() > 0)
            {
                folderParentTrack_ = folderParentTracks_.Get(folderParentTracks_.GetSize() - 1);
                folderParentTracks_.Delete(folderParentTracks_.GetSize() - 1);
            }
            else
                folderParentTrack_ = NULL;
        }
        else if (folderParentTrack_ != NULL)
        {
            folderParentTracks_.Add(folderParentTrack_);
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
    
    MediaTrack *GetSelectedTrack()
    {
        if (CountSelectedTracks2(NULL, false) != 1)
            return NULL;
        else
            return DAW::GetSelectedTrack(0);
    }
    
//  Page only uses the following:
       
    void OnTrackSelection()
    {
        if (isScrollLinkEnabled_ && tracks_.GetSize() > trackNavigators_.GetSize())
            ForceScrollLink();
    }
    
    void OnTrackListChange()
    {
        if (isScrollLinkEnabled_ && tracks_.GetSize() > trackNavigators_.GetSize())
            ForceScrollLink();
    }

    void OnTrackSelectionBySurface(MediaTrack *track)
    {
        if (isScrollLinkEnabled_)
        {
            if (IsTrackVisible(track, true))
                SetMixerScroll(track); // scroll selected MCP tracks into view
            
            if (IsTrackVisible(track, false))
                DAW::SendCommandMessage(40913); // scroll selected TCP tracks into view
        }
    }

    bool GetIsControlTouched(MediaTrack *track, int touchedControl)
    {
        if (track == GetMasterTrackNavigator()->GetTrack())
            return GetIsNavigatorTouched(GetMasterTrackNavigator(), touchedControl);
        
        for (int i = 0; i < trackNavigators_.GetSize(); ++i)
            if (track == trackNavigators_.Get(i)->GetTrack())
                return GetIsNavigatorTouched(trackNavigators_.Get(i), touchedControl);
 
        if (MediaTrack *selectedTrack = GetSelectedTrack())
             if (track == selectedTrack)
                return GetIsNavigatorTouched(GetSelectedTrackNavigator(), touchedControl);
        
        if (MediaTrack *focusedFXTrack = GetFocusedFXNavigator()->GetTrack())
            if (track == focusedFXTrack)
                return GetIsNavigatorTouched(GetFocusedFXNavigator(), touchedControl);

        return false;
    }
    
    bool GetIsNavigatorTouched(Navigator *navigator,  int touchedControl)
    {
        if (touchedControl == 0)
            return navigator->GetIsVolumeTouched();
        else if (touchedControl == 1)
        {
            if (navigator->GetIsPanTouched() || navigator->GetIsPanLeftTouched())
                return true;
        }
        else if (touchedControl == 2)
        {
            if (navigator->GetIsPanWidthTouched() || navigator->GetIsPanRightTouched())
                return true;
        }

        return false;
    }
    
    void RebuildVCASpill()
    {   
        if (currentTrackVCAFolderMode_ != 1)
            return;
    
        vcaTopLeadTracks_.Empty();
        vcaSpillTracks_.Empty();
        
        unsigned int leadTrackVCALeaderGroup = 0;
        unsigned int leadTrackVCALeaderGroupHigh = 0;
        
        if (vcaLeadTrack_ != NULL)
        {
            leadTrackVCALeaderGroup = DAW::GetTrackGroupMembership(vcaLeadTrack_, "VOLUME_VCA_LEAD");
            leadTrackVCALeaderGroupHigh = DAW::GetTrackGroupMembershipHigh(vcaLeadTrack_, "VOLUME_VCA_LEAD");
            vcaSpillTracks_.Add(vcaLeadTrack_);
        }
        
        // Get Visible Tracks
        for (int tidx = 1; tidx <= GetNumTracks(); tidx++)
        {
            MediaTrack *track = CSurf_TrackFromID(tidx, followMCP_);
            
            if (DAW::GetTrackGroupMembership(track, "VOLUME_VCA_LEAD") != 0 && DAW::GetTrackGroupMembership(track, "VOLUME_VCA_FOLLOW") == 0)
                vcaTopLeadTracks_.Add(track);
            
            if (DAW::GetTrackGroupMembershipHigh(track, "VOLUME_VCA_LEAD") != 0 && DAW::GetTrackGroupMembershipHigh(track, "VOLUME_VCA_FOLLOW") == 0)
                vcaTopLeadTracks_.Add(track);
            
            if (vcaLeadTrack_ != NULL)
            {
                bool isFollower = false;
                
                unsigned int leadTrackVCAFollowerGroup = DAW::GetTrackGroupMembership(track, "VOLUME_VCA_FOLLOW");
                unsigned int leadTrackVCAFollowerGroupHigh = DAW::GetTrackGroupMembershipHigh(track, "VOLUME_VCA_FOLLOW");

                if ((leadTrackVCALeaderGroup     & leadTrackVCAFollowerGroup) ||
                    (leadTrackVCALeaderGroupHigh & leadTrackVCAFollowerGroupHigh))
                {
                    isFollower = true;
                }
                
                if (isFollower)
                    vcaSpillTracks_.Add(track);
            }
        }
    }
    
    void RebuildFolderTracks()
    {
        if (currentTrackVCAFolderMode_ != 2)
            return;
        
        folderTopParentTracks_.Empty();
        folderDictionary_.DeleteAll();

        folderSpillTracks_.Empty();
       
        vector<WDL_PtrList<MediaTrack>*> currentDepthTracks;
        
        for (int i = 1; i <= GetNumTracks(); i++)
        {
            MediaTrack *track = CSurf_TrackFromID(i, followMCP_);

            if (GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH") == 1)
            {
                if (currentDepthTracks.size() == 0)
                    folderTopParentTracks_.Add(track);
                else
                    currentDepthTracks.back()->Add(track);
                
                if ( ! folderDictionary_.Exists(track))
                    folderDictionary_.Insert(track, new WDL_PtrList<MediaTrack>());
                
                if (folderDictionary_.Exists(track))
                    folderDictionary_.Get(track)->Add(track);
                
                currentDepthTracks.push_back(folderDictionary_.Get(track));
            }
            else if (GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH") == 0 && currentDepthTracks.size() > 0)
            {
                currentDepthTracks.back()->Add(track);
            }
            else if (GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH") < 0 && currentDepthTracks.size() > 0)
            {
                currentDepthTracks.back()->Add(track);
                
                int folderBackTrack = (int)-GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH");
                
                for (int t = 0; t < folderBackTrack && currentDepthTracks.size() > 0; t++)
                    currentDepthTracks.pop_back();
            }
        }
        
        if (folderParentTrack_ != NULL)
            for (int i = 0; i < folderDictionary_.Get(folderParentTrack_)->GetSize(); ++i)
                folderSpillTracks_.Add(folderDictionary_.Get(folderParentTrack_)->Get(i));
    }
    
    void EnterPage()
    {
        /*
         if (colorTracks_)
         {
         // capture track colors
         for (auto *navigator : trackNavigators_)
         if (MediaTrack *track = DAW::GetTrackFromGUID(navigator->GetTrackGUID(), followMCP_))
         trackColors_[navigator->GetTrackGUID()] = DAW::GetTrackColor(track);
         }
         */
    }
    
    void LeavePage()
    {
        /*
         if (colorTracks_)
         {
         DAW::PreventUIRefresh(1);
         // reset track colors
         for (auto *navigator : trackNavigators_)
         if (MediaTrack *track = DAW::GetTrackFromGUID(navigator->GetTrackGUID(), followMCP_))
         if (trackColors_.count(navigator->GetTrackGUID()) > 0)
         GetSetMediaTrackInfo(track, "I_CUSTOMCOLOR", &trackColors_[navigator->GetTrackGUID()]);
         DAW::PreventUIRefresh(-1);
         }
         */
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Page
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
protected:
    CSurfIntegrator *const csi_;
    string const name_;
    TrackNavigationManager *trackNavigationManager_;
    ModifierManager *modifierManager_;
    WDL_PtrList<ControlSurface> surfaces_;
    
public:
    Page(CSurfIntegrator *const csi, const char *name, bool followMCP,  bool synchPages, bool isScrollLinkEnabled, bool isScrollSynchEnabled) : csi_(csi), name_(name)
    {
        trackNavigationManager_ = new TrackNavigationManager(csi_, this, followMCP, synchPages, isScrollLinkEnabled, isScrollSynchEnabled);
        modifierManager_ = new ModifierManager(csi_, this, NULL);
    }

    ~Page()
    {
        delete trackNavigationManager_;
        delete modifierManager_;
        surfaces_.Empty(true);
    }
        
    const char *GetName() { return name_.c_str(); }
    
    ModifierManager *GetModifierManager() { return modifierManager_; }
    
    const WDL_PtrList<ControlSurface> &GetSurfaces() { return surfaces_; }
    
    void AddSurface(ControlSurface *surface)
    {
        if (WDL_NOT_NORMALLY(!surface)) { return; }
        surfaces_.Add(surface);
    }
    
    void UpdateCurrentActionContextModifiers()
    {
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->UpdateCurrentActionContextModifiers();
    }
    
    void ForceClear()
    {
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->ForceClear();
    }
    
    void ForceClearTrack(int trackNum)
    {
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->ForceClearTrack(trackNum);
    }
    
    void ForceUpdateTrackColors()
    {
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->ForceUpdateTrackColors();
    }
      
    bool GetTouchState(MediaTrack *track, int touchedControl)
    {
        return trackNavigationManager_->GetIsControlTouched(track, touchedControl);
    }
    
    void OnTrackSelection(MediaTrack *track)
    {
        trackNavigationManager_->OnTrackSelection();
        
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->OnTrackSelection(track);
    }
    
    void OnTrackListChange()
    {
        trackNavigationManager_->OnTrackListChange();
    }
    
    void OnTrackSelectionBySurface(MediaTrack *track)
    {
        trackNavigationManager_->OnTrackSelectionBySurface(track);
        
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->OnTrackSelection(track);
    }
    
    void TrackFXListChanged(MediaTrack *track)
    {
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->TrackFXListChanged(track);
    }
    
    void EnterPage()
    {
        trackNavigationManager_->EnterPage();
        
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->OnPageEnter();
    }
    
    void LeavePage()
    {
        trackNavigationManager_->LeavePage();
        
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->OnPageLeave();
    }
    
    void OnInitialization()
    {
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->OnInitialization();
    }
    
    void SignalStop()
    {
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->HandleStop();
    }
    
    void SignalPlay()
    {
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->HandlePlay();
    }
    
    void SignalRecord()
    {
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->HandleRecord();
    }
            
    void GoHome()
    {
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->GetZoneManager()->GoHome();
    }
    
    void GoZone(const char *name)
    {
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->GetZoneManager()->GoZone(name);
    }
    
    void AdjustBank(const char *zoneName, int amount)
    {
        if (!strcmp(zoneName, "Track"))
            trackNavigationManager_->AdjustTrackBank(amount);
        else if (!strcmp(zoneName, "VCA"))
            trackNavigationManager_->AdjustVCABank(amount);
        else if (!strcmp(zoneName, "Folder"))
            trackNavigationManager_->AdjustFolderBank(amount);
        else if (!strcmp(zoneName, "SelectedTracks"))
            trackNavigationManager_->AdjustSelectedTracksBank(amount);
        else if (!strcmp(zoneName, "SelectedTrack"))
            trackNavigationManager_->AdjustSelectedTrackBank(amount);
        else
            for (int i = 0; i < (int)surfaces_.GetSize(); ++i)
                surfaces_.Get(i)->GetZoneManager()->AdjustBank(zoneName, amount);
    }
    
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Page facade for TrackNavigationManager
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    bool GetSynchPages() { return trackNavigationManager_->GetSynchPages(); }
    bool GetScrollLink() { return trackNavigationManager_->GetScrollLink(); }
    int  GetNumTracks() { return trackNavigationManager_->GetNumTracks(); }
    Navigator *GetMasterTrackNavigator() { return trackNavigationManager_->GetMasterTrackNavigator(); }
    Navigator * GetSelectedTrackNavigator() { return trackNavigationManager_->GetSelectedTrackNavigator(); }
    Navigator * GetFocusedFXNavigator() { return trackNavigationManager_->GetFocusedFXNavigator(); }
    void VCAModeActivated() { trackNavigationManager_->VCAModeActivated(); }
    void VCAModeDeactivated() { trackNavigationManager_->VCAModeDeactivated(); }
    void FolderModeActivated() { trackNavigationManager_->FolderModeActivated(); }
    void FolderModeDeactivated() { trackNavigationManager_->FolderModeDeactivated(); }
    void SelectedTracksModeActivated() { trackNavigationManager_->SelectedTracksModeActivated(); }
    void SelectedTracksModeDeactivated() { trackNavigationManager_->SelectedTracksModeDeactivated(); }
    Navigator * GetNavigatorForChannel(int channelNum) { return trackNavigationManager_->GetNavigatorForChannel(channelNum); }
    MediaTrack *GetTrackFromId(int trackNumber) { return trackNavigationManager_->GetTrackFromId(trackNumber); }
    int GetIdFromTrack(MediaTrack *track) { return trackNavigationManager_->GetIdFromTrack(track); }
    bool GetIsTrackVisible(MediaTrack *track) { return trackNavigationManager_->GetIsTrackVisible(track); }
    bool GetIsVCASpilled(MediaTrack *track) { return trackNavigationManager_->GetIsVCASpilled(track); }
    void ToggleVCASpill(MediaTrack *track) { trackNavigationManager_->ToggleVCASpill(track); }
    bool GetIsFolderSpilled(MediaTrack *track) { return trackNavigationManager_->GetIsFolderSpilled(track); }
    void ToggleFolderSpill(MediaTrack *track) { trackNavigationManager_->ToggleFolderSpill(track); }
    void ToggleScrollLink(int targetChannel) { trackNavigationManager_->ToggleScrollLink(targetChannel); }
    void ToggleSynchPages() { trackNavigationManager_->ToggleSynchPages(); }
    void SetTrackOffset(int offset) { trackNavigationManager_->SetTrackOffset(offset); }
    MediaTrack *GetSelectedTrack() { return trackNavigationManager_->GetSelectedTrack(); }
    void NextInputMonitorMode(MediaTrack *track) { trackNavigationManager_->NextInputMonitorMode(track); }
    const char *GetAutoModeDisplayName(int modeIndex) { return trackNavigationManager_->GetAutoModeDisplayName(modeIndex); }
    const char *GetGlobalAutoModeDisplayName() { return trackNavigationManager_->GetGlobalAutoModeDisplayName(); }
    const char *GetCurrentInputMonitorMode(MediaTrack *track) { return trackNavigationManager_->GetCurrentInputMonitorMode(track); }
    const WDL_PtrList<MediaTrack> &GetSelectedTracks() { return trackNavigationManager_->GetSelectedTracks(); }
    
    
    /*
    int repeats = 0;
    
    void Run()
    {
        int start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
                
        repeats++;
         
        if (repeats > 50)
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
            
            for (int i = 0; i < surfaces_.GetSize(); ++i)
            {
                start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
                surfaces_.Get(i)->HandleExternalInput();
                duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - start;
                totalDuration += duration;
                ShowDuration(surfaces_.Get(i)->GetName(), "HandleExternalInput", duration);
            }
            
            for (int i = 0; i < surfaces_.GetSize(); ++i)
            {
                start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
                surfaces_.Get(i)->RequestUpdate();
                duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - start;
                totalDuration += duration;
                ShowDuration(surfaces_.Get(i)->GetName(), "Request Update", duration);
            }
            
            char msgBuffer[250];
            
            snprintf(msgBuffer, sizeof(msgBuffer), "Total duration = %d\n\n\n", totalDuration);
            ShowConsoleMsg(msgBuffer);
        }
    }
    
    
    void ShowDuration(string item, int duration)
    {
        char msgBuffer[250];
        
        snprintf(msgBuffer, sizeof(msgBuffer), "%s - %d microseconds\n", item.c_str(), duration);
        ShowConsoleMsg(msgBuffer);
    }
    
    void ShowDuration(string surface, string item, int duration)
    {
        char msgBuffer[250];
        
        snprintf(msgBuffer, sizeof(msgBuffer), "%s - %s - %d microseconds\n", surface.c_str(), item.c_str(), duration);
        ShowConsoleMsg(msgBuffer);
    }
   */

//*
    void Run()
    {
        trackNavigationManager_->RebuildTracks();
        trackNavigationManager_->RebuildVCASpill();
        trackNavigationManager_->RebuildFolderTracks();
        trackNavigationManager_->RebuildSelectedTracks();
        
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->HandleExternalInput();
        
        for (int i = 0; i < surfaces_.GetSize(); ++i)
            surfaces_.Get(i)->RequestUpdate();
    }
//*/
};

static const int s_stepSizes_[]  = { 2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25 };
static const int s_tickCounts_[] = { 250, 235, 220, 205, 190, 175, 160, 145, 130, 115, 100, 90, 80, 70, 60, 50, 45, 40, 35, 30, 25, 20, 20, 20 };

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CSurfIntegrator : public IReaperControlSurface
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    char configtmp[1024];

    WDL_PtrList<Midi_ControlSurfaceIO> midiSurfacesIO_;
    WDL_PtrList<OSC_ControlSurfaceIO> oscSurfacesIO_;

    WDL_StringKeyedArray<Action*> actions_;
    static void disposeAction(Action *action) { delete action; }

    WDL_PtrList<Page> pages_;

    int currentPageIndex_;
    
    bool shouldRun_;
    
    // these are offsets to be passed to projectconfig_var_addr() when needed in order to get the actual pointers
    int timeModeOffs_;
    int timeMode2Offs_;
    int measOffsOffs_;
    int timeOffsOffs_; // for a double
    int projectPanModeOffs_;
    
    int projectMetronomePrimaryVolumeOffs_; // for double -- if invalid, use fallbacks
    int projectMetronomeSecondaryVolumeOffs_; // for double -- if invalid, use fallbacks
    
    WDL_StringKeyedArray<WDL_IntKeyedArray<int>* > fxParamSteppedValueCounts_;
    static void disposeCounts(WDL_IntKeyedArray<int> *counts) { delete counts; }
    
    void InitActionsDictionary();
    
    void InitFXParamStepValues();

    double GetPrivateProfileDouble(const char *key)
    {
        char tmp[512];
        memset(tmp, 0, sizeof(tmp));
        
        GetPrivateProfileString("REAPER", key, "", tmp, sizeof(tmp), get_ini_file());
        
        return strtod (tmp, NULL);
    }
    
public:
    CSurfIntegrator();
    
    ~CSurfIntegrator();
    
    virtual int Extended(int call, void *parm1, void *parm2, void *parm3) override;
    const char *GetTypeString() override;
    const char *GetDescString() override;
    const char *GetConfigString() override; // string of configuration data

    void Shutdown()
    {
        // GAW -- IMPORTANT
        
        // We want to stop polling
        shouldRun_ = false;
        
        // Zero out all Widgets before shutting down
        if (pages_.Get(currentPageIndex_))
            pages_.Get(currentPageIndex_)->ForceClear();
    }
    
    void Init();

    double GetFaderMaxDB() { return GetPrivateProfileDouble("slidermaxv"); }
    double GetFaderMinDB() { return GetPrivateProfileDouble("sliderminv"); }
    double GetVUMaxDB() { return GetPrivateProfileDouble("vumaxvol"); }
    double GetVUMinDB() { return GetPrivateProfileDouble("vuminvol"); }
    
    int *GetTimeModePtr() { return (int *) projectconfig_var_addr(NULL,timeModeOffs_); }
    int *GetTimeMode2Ptr() { return (int *) projectconfig_var_addr(NULL,timeMode2Offs_); }
    int *GetMeasOffsPtr() { return (int *) projectconfig_var_addr(NULL,measOffsOffs_); }
    double *GetTimeOffsPtr() { return (double *) projectconfig_var_addr(NULL,timeOffsOffs_); }
    int GetProjectPanMode() { int *p = (int *) projectconfig_var_addr(NULL,projectPanModeOffs_); return p ? *p : 0; }
   
    double *GetMetronomePrimaryVolumePtr()
    {
      void *ret = projectconfig_var_addr(NULL,projectMetronomePrimaryVolumeOffs_);
      if (ret) return (double *)ret;
      // REAPER 7.09 and earlier require this:
      int size=0;
      ret = get_config_var("projmetrov1", &size);
      if (size==8) return (double *)ret;
      return NULL;
    }
    
    double *GetMetronomeSecondaryVolumePtr()
    { 
      void *ret = projectconfig_var_addr(NULL,projectMetronomeSecondaryVolumeOffs_);
      if (ret) return (double *)ret;
      // REAPER 7.09 and earlier require this:
      int size=0;
      ret = get_config_var("projmetrov2", &size);
      if (size==8) return (double *)ret;
      return NULL;
    }
    
    int GetBaseTickCount(int stepCount)
    {
        if (NUM_ELEM(s_tickCounts_) < stepCount)
            return s_tickCounts_[stepCount];
        else
            return s_tickCounts_[NUM_ELEM(s_tickCounts_) - 1];
    }
    
    void Speak(const char *phrase)
    {
        static void (*osara_outputMessage)(const char *message);
        static bool chk;
    
        if (!chk)
        {
            *(void **)&osara_outputMessage = plugin_getapi("osara_outputMessage");
            chk = true;
        }

        if (osara_outputMessage)
            osara_outputMessage(phrase);
    }
    
    // These direct calls are used by Learn to change Actions in the dynamic learnFXZone -- it allows for realtime editing, with results immediately visible in the hardware
    Action *GetNoActionAction()
    {
        return actions_.Get("NoAction");
    }
    
    Action *GetFXParamAction()
    {
        return actions_.Get("FXParam");
    }
    
    Action *GetFixedTextDisplayAction()
    {
        return actions_.Get("FixedTextDisplay");
    }
    
    Action *GetFXParamValueDisplayAction()
    {
        return actions_.Get("FXParamValueDisplay");
    }
    // End direct calls
    
    ActionContext *GetActionContext(const char *actionName, Widget *widget, Zone *zone, const string_list &params)
    {
        if (actions_.Exists(actionName))
            return new ActionContext(this, actions_.Get(actionName), widget, zone, 0, &params, NULL);
        else
            return new ActionContext(this, actions_.Get("NoAction"), widget, zone, 0, &params, NULL);
    }

    void OnTrackSelection(MediaTrack *track) override
    {
        if (pages_.Get(currentPageIndex_))
            pages_.Get(currentPageIndex_)->OnTrackSelection(track);
    }
    
    void SetTrackListChange() override
    {
        if (pages_.Get(currentPageIndex_))
            pages_.Get(currentPageIndex_)->OnTrackListChange();
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
        if (pages_.Get(currentPageIndex_))
            pages_.Get(currentPageIndex_)->SetTrackOffset(offset);
    }
    
    void AdjustBank(Page *sendingPage, const char *zoneName, int amount)
    {
        if (! sendingPage->GetSynchPages())
            sendingPage->AdjustBank(zoneName, amount);
        else
            for (int i = 0; i < pages_.GetSize(); ++i)
                if (pages_.Get(i)->GetSynchPages())
                    pages_.Get(i)->AdjustBank(zoneName, amount);
    }
       
    void NextPage()
    {
        if (pages_.Get(currentPageIndex_))
        {
            pages_.Get(currentPageIndex_)->LeavePage();
            currentPageIndex_ = currentPageIndex_ == pages_.GetSize() - 1 ? 0 : (currentPageIndex_+1);
            //DAW::SetProjExtState(0, "CSI", "PageIndex", int_to_string(currentPageIndex_).c_str());
            if (WDL_NORMALLY(pages_.Get(currentPageIndex_)))
                pages_.Get(currentPageIndex_)->EnterPage();
        }
    }
    
    void GoToPage(const char *pageName)
    {
        for (int i = 0; i < pages_.GetSize(); i++)
        {
            if (pages_.Get(i)->GetName() == pageName)
            {
                if (WDL_NORMALLY(pages_.Get(currentPageIndex_)))
                    pages_.Get(currentPageIndex_)->LeavePage();
                currentPageIndex_ = i;
                if (WDL_NORMALLY(pages_.Get(currentPageIndex_)))
                {
                    //DAW::SetProjExtState(0, "CSI", "PageIndex", int_to_string(currentPageIndex_).c_str());
                    pages_.Get(currentPageIndex_)->EnterPage();
                }
                break;
            }
        }
    }
    
    bool GetTouchState(MediaTrack *track, int touchedControl) override
    {
        if (pages_.Get(currentPageIndex_))
            return pages_.Get(currentPageIndex_)->GetTouchState(track, touchedControl);
        else
            return false;
    }
    
    void TrackFXListChanged(MediaTrack *track)
    {
        for (int i = 0; i < pages_.GetSize(); ++i)
            pages_.Get(i)->TrackFXListChanged(track);
        
        if (g_fxParamsWrite)
        {
            char fxName[MEDBUF];
            
            for (int i = 0; i < TrackFX_GetCount(track); i++)
            {
                TrackFX_GetFXName(track, i, fxName, sizeof(fxName));
                FILE *fxFile = NULL;
                                
                if (g_fxParamsWrite)
                {
                    string fxNameNoBadChars(fxName);
                    ReplaceAllWith(fxNameNoBadChars, s_BadFileChars, "_");

                    fxFile = fopenUTF8((string(GetResourcePath()) + "/CSI/Zones/ZoneRawFXFiles/" + fxNameNoBadChars + ".txt").c_str(), "wb");
                    
                    if (fxFile)
                        fprintf(fxFile, "Zone \"%s\"\n", fxName);
                }

                for (int j = 0; j < TrackFX_GetNumParams(track, i); j++)
                {
                    char fxParamName[MEDBUF];
                    TrackFX_GetParamName(track, i, j, fxParamName, sizeof(fxParamName));
 
                    if (fxFile)
                        fprintf(fxFile, "\tFXParam %d \"%s\"\n", j, fxParamName);
                        
                    /* step sizes
                    double stepOut = 0;
                    double smallstepOut = 0;
                    double largestepOut = 0;
                    bool istoggleOut = false;
                    TrackFX_GetParameterStepSizes(track, i, j, &stepOut, &smallstepOut, &largestepOut, &istoggleOut);

                    ShowConsoleMsg(("\n\n" + to_string(j) + " - \"" + string(fxParamName) + "\"\t\t\t\t Step = " +  to_string(stepOut) + " Small Step = " + to_string(smallstepOut)  + " LargeStep = " + to_string(largestepOut)  + " Toggle Out = " + (istoggleOut == 0 ? "false" : "true")).c_str());
                    */
                }
                
                if (fxFile)
                {
                    fprintf(fxFile,"ZoneEnd"); // do we want a newline on this? probably
                    fclose(fxFile);
                }
            }
        }
    }
    
    const char *GetTCPFXParamName(MediaTrack *track, int fxIndex, int paramIndex, char *buf, int bufsz)
    {
        buf[0]=0;
        TrackFX_GetParamName(track, fxIndex, paramIndex, buf, bufsz);
        return buf;
    }
    
    void SetSteppedValueCount(const char *fxName, int paramIndex, int steppedValueCount)
    {
        if ( ! fxParamSteppedValueCounts_.Exists(fxName))
            fxParamSteppedValueCounts_.Insert(fxName, new WDL_IntKeyedArray<int>());
        
        if (fxParamSteppedValueCounts_.Exists(fxName))
            fxParamSteppedValueCounts_.Get(fxName)->Insert(paramIndex, steppedValueCount);
    }
    
    bool HaveFXSteppedValuesBeenCalculated(const char *fxName)
    {
        if (fxParamSteppedValueCounts_.Exists(fxName))
            return true;
        else
            return false;
    }
    
    int GetSteppedValueCount(const char *fxName, int paramIndex)
    {
        WDL_IntKeyedArray<int> *r = fxParamSteppedValueCounts_.Get(fxName);
        return r ? r->Get(paramIndex, 0) : 0;
    }
    
    //int repeats = 0;
    
    void Run() override
    {
        //int start = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        if (shouldRun_ && pages_.Get(currentPageIndex_))
            pages_.Get(currentPageIndex_)->Run();
        /*
         repeats++;
         
         if (repeats > 50)
         {
         repeats = 0;
         
         int duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() - start;
         
         char msgBuffer[250];
         
         snprintf(msgBuffer, sizeof(msgBuffer), "%d microseconds\n", duration);
         ShowConsoleMsg(msgBuffer);
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
 
 snprintf(msgBuffer, sizeof(msgBuffer), "%d microseconds\n", duration);
 ShowConsoleMsg(msgBuffer);
 
 */

#endif /* control_surface_integrator.h */
