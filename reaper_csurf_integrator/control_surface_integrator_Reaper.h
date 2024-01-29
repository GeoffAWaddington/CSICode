//
//  control_surface_integrator_Reaper.h
//  reaper_csurf_integrator
//
//

#ifndef control_surface_integrator_Reaper_h
#define control_surface_integrator_Reaper_h

#include "reaper_plugin_functions.h"
#include "WDL/mutex.h"
#include "ReportLoggingEtc.h"

using namespace std;

extern HWND g_hwnd;

const int BUFSZ = 512;

struct rgba_color
{
    int r;
    int g;
    int b;
    int a;
        
    bool operator == (rgba_color &other)
    {
        return r == other.r && g == other.g && b == other.b && a == other.a ;
    }
    
    bool operator != (rgba_color &other)
    {
        return r != other.r || g != other.g || b != other.b || a != other.a;
    }
    
    const char *to_string(char *buf) // buf must be at least 10 bytes
    {
      snprintf(buf,10,"#%02x%02x%02x%02x",r,g,b,a);
      return buf;
    }
    
    rgba_color()
    {
        r = 0;
        g = 0;
        b = 0;
        a = 255;
    }
};

static rgba_color GetColorValue(const string &hexColor)
{
    rgba_color colorValue;
    
    if (hexColor.length() == 7)
    {
        regex pattern("#([0-9a-fA-F]{6})");
        smatch match;
        if (regex_match(hexColor, match, pattern))
            sscanf(match.str(1).c_str(), "%2x%2x%2x", &colorValue.r, &colorValue.g, &colorValue.b);
    }
    else if (hexColor.length() == 9)
    {
        regex pattern("#([0-9a-fA-F]{8})");
        smatch match;
        if (regex_match(hexColor, match, pattern))
            sscanf(match.str(1).c_str(), "%2x%2x%2x%2x", &colorValue.r, &colorValue.g, &colorValue.b, &colorValue.a);
    }
 
    return colorValue;
}

struct MIDI_event_ex_t : MIDI_event_t
{
    MIDI_event_ex_t() {};
    
    MIDI_event_ex_t(const unsigned char first, const unsigned char second, const unsigned char third)
    {
        size = 3;
        midi_message[0] = first;
        midi_message[1] = second;
        midi_message[2] = third;
        midi_message[3] = 0x00;
    };
    
    bool IsEqualTo(const MIDI_event_ex_t *other) const
    {
        if (this->size != other->size)
            return false;
        
        for (int i = 0; i < size; ++i)
            if (this->midi_message[i] != other->midi_message[i])
                return false;
        
        return true;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class DAW
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    static void SwapBufsPrecise(midi_Input *midiInput)
    {
    #ifndef timeGetTime
            midiInput->SwapBufsPrecise(GetTickCount(), GetTickCount());
    #else
            midiInput->SwapBufsPrecise(timeGetTime(), timeGetTime());
    #endif
    }
    
    static double GetCurrentNumberOfMilliseconds()
    {
    #ifndef timeGetTime
            return GetTickCount();
    #else
            return timeGetTime();
    #endif
    }
    
    static void MarkProjectDirty(ReaProject *proj) { ::MarkProjectDirty(proj); }
    
    static int IsProjectDirty() { return ::IsProjectDirty(nullptr); }
    
    static void SaveProject() { ::Main_SaveProject(nullptr, false); }
    
    static const char *get_ini_file() { return ::get_ini_file(); }

    static DWORD GetPrivateProfileString(const char *appname, const char *keyname, const char *def, char *ret, int retsize, const char *fn) { return ::GetPrivateProfileString(appname, keyname, def, ret, retsize, fn); }

    static int SetProjExtState(ReaProject *proj, const char *extname, const char *key, const char *value)
    {
        int retval = ::SetProjExtState(proj, extname, key, value);
        MarkProjectDirty(0);
        return retval;
    }

    static int GetProjExtState(ReaProject *proj, const char *extname, const char *key, char *valOutNeedBig, int valOutNeedBig_sz) { return ::GetProjExtState(proj, extname, key, valOutNeedBig, valOutNeedBig_sz); }
    
    static const char *GetResourcePath() { return ::GetResourcePath(); }
    
    static int NamedCommandLookup(const char *command_name) { return ::NamedCommandLookup(command_name);  }

    static void SendCommandMessage(WPARAM wparam) { ::SendMessage(g_hwnd, WM_COMMAND, wparam, 0); }
        
    static int GetToggleCommandState(int commandId) { return ::GetToggleCommandState(commandId); }
    
    static void ShowConsoleMsg(const char *msg) { ::ShowConsoleMsg(msg); }
    
    static midi_Input *CreateMIDIInput(int dev) {  return ::CreateMIDIInput(dev); }
    
    static midi_Output *CreateMIDIOutput(int dev, bool streamMode, int *msoffset100) {  return ::CreateMIDIOutput(dev, streamMode, msoffset100); }
   
    static int GetNumMIDIInputs() { return ::GetNumMIDIInputs(); }
    
    static int GetNumMIDIOutputs() { return ::GetNumMIDIOutputs(); }
    
    static bool GetMIDIInputName(int dev, char *nameout, int nameout_sz) { return ::GetMIDIInputName(dev, nameout, nameout_sz); }
    
    static bool GetMIDIOutputName(int dev, char *nameout, int nameout_sz) { return ::GetMIDIOutputName(dev, nameout, nameout_sz); }
    
    static bool AnyTrackSolo(ReaProject *proj) { return ::AnyTrackSolo(proj); }
    
    static void SoloAllTracks(int solo) { ::SoloAllTracks(solo); }

    static void SetAutomationMode(int mode, bool onlySel) { ::SetAutomationMode(mode, onlySel); }

    static int GetGlobalAutomationOverride() { return ::GetGlobalAutomationOverride(); }

    static void SetGlobalAutomationOverride(int mode) { ::SetGlobalAutomationOverride(mode); }

    static int GetFocusedFX2(int *tracknumberOut, int *itemnumberOut, int *fxnumberOut) { return ::GetFocusedFX2(tracknumberOut, itemnumberOut, fxnumberOut); }
    
    static bool GetLastTouchedFX(int *tracknumberOut, int *fxnumberOut, int *paramnumberOut) {  return ::GetLastTouchedFX(tracknumberOut, fxnumberOut, paramnumberOut); }
    
    static void CSurf_OnArrow(int whichdir, bool wantzoom) { ::CSurf_OnArrow(whichdir, wantzoom); }
    
    static double GetPlayPosition() { return ::GetPlayPosition(); }
    
    static void format_timestr_pos(double tpos, char *buf, int buf_sz, int modeoverride) { ::format_timestr_pos(tpos, buf, buf_sz, modeoverride); }

    static void SetEditCurPos(double time, bool moveview, bool seekplay) { ::SetEditCurPos(time, moveview, seekplay);  }

    static double GetCursorPosition() { return ::GetCursorPosition(); }

    static double GetProjectLength(ReaProject *proj) { return ::GetProjectLength(proj); }

    static void CSurf_OnRew(int seekplay) { ::CSurf_OnRew(seekplay); }
    
    static void CSurf_OnFwd(int seekplay) { ::CSurf_OnFwd(seekplay); }
    
    static void CSurf_OnStop() { ::CSurf_OnStop(); }
    
    static void CSurf_OnPlay() { ::CSurf_OnPlay(); }
    
    static void CSurf_OnRecord() { ::CSurf_OnRecord(); }
    
    static int GetPlayState() { return ::GetPlayState(); }
    
    static int CSurf_NumTracks(bool mcpView) { return ::CSurf_NumTracks(mcpView); };
    
    static MediaTrack *CSurf_TrackFromID(int idx, bool mcpView) { return ::CSurf_TrackFromID(idx, mcpView); }
    
    static int GetSetRepeatEx(ReaProject *proj, int val) { return ::GetSetRepeatEx(proj, val); }
    
    static MediaTrack *GetMasterTrack() { return ::GetMasterTrack(NULL); };
    
    static int CountSelectedTracks() { return ::CountSelectedTracks2(NULL, false); }
    
    static MediaTrack *GetSelectedTrack(int seltrackidx) { return ::GetSelectedTrack(NULL, seltrackidx); }
    
    // Runs the system color chooser dialog.  Returns 0 if the user cancels the dialog.
    static int GR_SelectColor(HWND hwnd, int *colorOut) { return ::GR_SelectColor(hwnd, colorOut); }
        
    static int ColorToNative(int r, int g, int b) { return ::ColorToNative(r, g, b); }

    static void ColorFromNative(int color, int *r, int *g, int *b) { ::ColorFromNative(color, r, g, b); }

    static bool ValidateTrackPtr(MediaTrack *track) { return ::ValidatePtr(track, "MediaTrack*"); }
    
    static double TimeMap2_timeToBeats(ReaProject *proj, double tpos, int *measuresOutOptional, int *cmlOutOptional, double *fullbeatsOutOptional, int *cdenomOutOptional)
    {
        return ::TimeMap2_timeToBeats(proj, tpos, measuresOutOptional, cmlOutOptional, fullbeatsOutOptional, cdenomOutOptional);
    }
    
    static bool CanUndo()
    {
        if (::Undo_CanUndo2(nullptr))
           return true;
        else
            return false;
    }
    
    static bool CanRedo()
    {
        if (::Undo_CanRedo2(nullptr))
           return true;
        else
            return false;
    }
    
    static void Undo()
    {
        if (CanUndo())
            ::Undo_DoUndo2(nullptr);
    }
    
    static void Redo()
    {
        if (CanRedo())
            ::Undo_DoRedo2(nullptr);
    }
    
    static void Undo_BeginBlock()
    {
        ::Undo_BeginBlock();
    }
    
    static void Undo_EndBlock()
    {
        ::Undo_EndBlock("", 0);
    }
    
    static MediaTrack *GetTrack(int trackidx)
    {
        trackidx--;
        
        if (trackidx < 0)
            trackidx = 0;
        
        return ::GetTrack(NULL, trackidx) ;
    }
    
    static int TrackFX_GetCount(MediaTrack *track)
    {
        if (ValidateTrackPtr(track))
            return ::TrackFX_GetCount(track);
        else
            return 0;
    }
    
    static int CountTCPFXParms(MediaTrack *track)
    {
        if (ValidateTrackPtr(track))
            return ::CountTCPFXParms(nullptr, track);
        else
            return 0;
    }   
    
    static bool GetTCPFXParm(MediaTrack *track, int index, int *fxindexOut, int *parmidxOut)
    {
        if (ValidateTrackPtr(track))
            return ::GetTCPFXParm(nullptr, track, index, fxindexOut, parmidxOut);
        else
            return false;
    }
    
    static bool TrackFX_GetFXName(MediaTrack *track, int fx, char *buf, int buf_sz)
    {
        if (ValidateTrackPtr(track))
            return ::TrackFX_GetFXName(track, fx, buf, buf_sz);
        else
        {
            if (buf_sz > 0)
                buf[0] = 0;
            return false;
        }
    }
    
    static bool TrackFX_GetNamedConfigParm(MediaTrack *track, int fx, const char *parmname, char *buf, int buf_sz)
    {
        if (ValidateTrackPtr(track))
            return ::TrackFX_GetNamedConfigParm(track, fx, parmname, buf, buf_sz);
        else
        {
            if (buf_sz > 0)
                buf[0] = 0;
            return false;
        }
    }

    static int TrackFX_GetNumParams(MediaTrack *track, int fx)
    {
        if (ValidateTrackPtr(track))
            return ::TrackFX_GetNumParams(track, fx);
        else
            return 0;
    }
    
    static string TrackFX_GetParamName(MediaTrack *track, int fxIndex, int paramIndex)
    {
        if (ValidateTrackPtr(track))
        {
            char fxParamName[BUFSZ];
            ::TrackFX_GetParamName(track, fxIndex, paramIndex, fxParamName, sizeof(fxParamName));
            return string(fxParamName);
        }
        else
            return "";
    }
    
    static bool TrackFX_GetFormattedParamValue(MediaTrack *track, int fx, int param, char *buf, int buf_sz)
    {
        if (ValidateTrackPtr(track))
            return ::TrackFX_GetFormattedParamValue(track, fx, param, buf, buf_sz);
        else
        {
            if (buf_sz > 0)
                buf[0] = 0;
            return false;
        }
    }
    
    static bool TrackFX_GetParameterStepSizes(MediaTrack *track, int fx, int param, double *stepOut, double *smallstepOut, double *largestepOut, bool *istoggleOut)
    {
        if (ValidateTrackPtr(track))
            return ::TrackFX_GetParameterStepSizes(track, fx, param, stepOut, smallstepOut, largestepOut, istoggleOut);
        else
            return false;
    }

    static double TrackFX_GetParam(MediaTrack *track, int fx, int param, double *minvalOut, double *maxvalOut)
    {
        if (ValidateTrackPtr(track))
            return ::TrackFX_GetParam(track, fx, param, minvalOut, maxvalOut);
        else
            return 0.0;
    }
    
    static bool TrackFX_SetParam(MediaTrack *track, int fx, int param, double val)
    {
        if (ValidateTrackPtr(track))
            return ::TrackFX_SetParam(track, fx, param, val);
        else
            return false;
    }
    
    static bool TrackFX_EndParamEdit(MediaTrack *track, int fx, int param)
    {
        if (ValidateTrackPtr(track))
            return ::TrackFX_EndParamEdit(track, fx, param);
        else
            return false;
    }
    
    static bool TrackFX_GetEnabled(MediaTrack *track, int fx)
    {
        if (ValidateTrackPtr(track))
            return ::TrackFX_GetEnabled(track, fx);
        else
            return false;
    }
    
    static void TrackFX_SetEnabled(MediaTrack *track, int fx, bool enabled)
    {
        if (ValidateTrackPtr(track))
            ::TrackFX_SetEnabled(track, fx, enabled);
    }
    
    static bool TrackFX_GetOffline(MediaTrack *track, int fx)
    {
        if (ValidateTrackPtr(track))
            return ::TrackFX_GetOffline(track, fx);
        else
            return false;
    }
    
    static void TrackFX_SetOffline(MediaTrack *track, int fx, bool offline)
    {
        if (ValidateTrackPtr(track))
            ::TrackFX_SetOffline(track, fx, offline);
    }

    static void TrackFX_SetOpen(MediaTrack *track, int fx, bool open)
    {
        if (ValidateTrackPtr(track))
            ::TrackFX_SetOpen(track, fx, open);
    }

    static bool GetTrackName(MediaTrack *track, char *buf, int buf_sz)
    {
        if (ValidateTrackPtr(track))
            return ::GetTrackName(track, buf, buf_sz);
        else
        {
            if (buf_sz > 0)
                buf[0] = 0;
            return false;
        }
    }
    
    static rgba_color GetTrackColor(MediaTrack *track)
    {
        rgba_color color;
        
        if (ValidateTrackPtr(track))
            ::ColorFromNative(::GetTrackColor(track), &color.r, &color.g, &color.b);
        
        if (color.r == 0 && color.g == 0 && color.b == 0)
        {
            color.r = 64;
            color.g = 64;
            color.b = 64;
        }
        
        return color;
    }
    
    static int CSurf_TrackToID(MediaTrack *track, bool mcpView)
    {
        if (ValidateTrackPtr(track))
            return ::CSurf_TrackToID(track, mcpView);
        else
            return 1;
    }
    
    static double GetMediaTrackInfo_Value(MediaTrack *track, const char *parmname)
    {
        if (ValidateTrackPtr(track))
            return ::GetMediaTrackInfo_Value(track, parmname);
        else
            return 0.0;
    }

    static double GetTrackSendInfo_Value(MediaTrack *track, int category, int send_index, const char *parmname)
    {
        if (ValidateTrackPtr(track))
            return ::GetTrackSendInfo_Value(track, category, send_index, parmname);
        else
            return 0.0;
    }

    static void *GetSetTrackSendInfo(MediaTrack *track, int category, int send_index, const char *parmname, void *setNewValue)
    {
        if (ValidateTrackPtr(track))
            return ::GetSetTrackSendInfo(track, category, send_index, parmname, setNewValue);
        else
            return nullptr;
    }
    
    static void *GetSetMediaTrackInfo(MediaTrack *track, const char *parmname, void *setNewValue)
    {
        if (ValidateTrackPtr(track))
            return ::GetSetMediaTrackInfo(track, parmname, setNewValue);
        else
            return nullptr;
    }
    
    static unsigned int GetTrackGroupMembership(MediaTrack *track, const char *groupname)
    {
        if (ValidateTrackPtr(track))
            return ::GetSetTrackGroupMembership(track, groupname, 0, 0);
        else
            return 0;
    }

    static unsigned int GetTrackGroupMembershipHigh(MediaTrack *track, const char *groupname)
    {
        if (ValidateTrackPtr(track))
            return ::GetSetTrackGroupMembershipHigh(track, groupname, 0, 0);
        else
            return 0;
    }

    static double CSurf_OnVolumeChange(MediaTrack *track, double volume, bool relative)
    {
        if (ValidateTrackPtr(track))
            return ::CSurf_OnVolumeChange(track, volume, relative);
        else
            return 0.0;
    }
    
    static double CSurf_OnPanChange(MediaTrack *track, double pan, bool relative)
    {
        if (ValidateTrackPtr(track))
            return ::CSurf_OnPanChange(track, pan, relative);
        else
            return 0.0;
    }

    static bool CSurf_OnMuteChange(MediaTrack *track, int mute)
    {
        if (ValidateTrackPtr(track))
            return ::CSurf_OnMuteChange(track, mute);
        else
            return false;
    }

    static bool GetTrackUIMute(MediaTrack *track, bool *muteOut)
    {
        if (ValidateTrackPtr(track))
            return ::GetTrackUIMute(track, muteOut);
        else
            return false;
    }
    
    static bool GetTrackUIVolPan(MediaTrack *track, double *volumeOut, double *panOut)
    {
        if (ValidateTrackPtr(track))
            return ::GetTrackUIVolPan(track, volumeOut, panOut);
        else
            return false;
    }
    
    static bool GetTrackUIPan(MediaTrack *track, double *pan1Out, double *pan2Out, int *panmodeOut)
    {
        if (ValidateTrackPtr(track))
            return ::GetTrackUIPan(track, pan1Out, pan2Out, panmodeOut);
        else
            return false;
    }

    static void CSurf_SetSurfaceVolume(MediaTrack *track, double volume, IReaperControlSurface *ignoresurf)
    {
        if (ValidateTrackPtr(track))
            ::CSurf_SetSurfaceVolume(track, volume, ignoresurf);
    }
    
    static bool SetTrackSendUIPan(MediaTrack *track, int send_idx, double pan, int isend)
    {
        if (ValidateTrackPtr(track))
            return ::SetTrackSendUIPan(track, send_idx, pan, isend);
        else
            return false;
    }
    
    static bool SetTrackSendUIVol(MediaTrack *track, int send_idx, double vol, int isend)
    {
        if (ValidateTrackPtr(track))
            return ::SetTrackSendUIVol(track, send_idx, vol, isend);
        else
            return false;
    }
    
    static int GetTrackNumSends(MediaTrack *track, int category)
    {
        if (ValidateTrackPtr(track))
            return ::GetTrackNumSends(track, category);
        else
            return 0;
    }
    
    static bool GetTrackSendUIMute(MediaTrack *track, int send_index, bool *muteOut)
    {
        if (ValidateTrackPtr(track))
            return ::GetTrackSendUIMute(track, send_index, muteOut);
        else
            return false;
    }
    
    static bool ToggleTrackSendUIMute(MediaTrack *track, int send_index)
    {
        if (ValidateTrackPtr(track))
            return ::ToggleTrackSendUIMute(track, send_index);
        else
            return false;
    }

    static bool GetTrackSendUIVolPan(MediaTrack *track, int send_index, double *volumeOut, double *panOut)
    {
        if (ValidateTrackPtr(track))
            return ::GetTrackSendUIVolPan(track, send_index, volumeOut, panOut);
        else
            return false;
    }
    
    static bool GetTrackReceiveUIMute(MediaTrack *track, int send_index, bool *muteOut)
    {
        if (ValidateTrackPtr(track))
            return ::GetTrackReceiveUIMute(track, send_index, muteOut);
        else
            return false;
    }
    
    static bool GetTrackReceiveUIVolPan(MediaTrack *track, int send_index, double *volumeOut, double *panOut)
    {
        if (ValidateTrackPtr(track))
            return ::GetTrackReceiveUIVolPan(track, send_index, volumeOut, panOut);
        else
            return false;
    }
    
    static double Track_GetPeakInfo(MediaTrack *track, int channel)
    {
        if (ValidateTrackPtr(track))
            return ::Track_GetPeakInfo(track, channel);
        else
            return 0.0;
    }

    static void CSurf_SetSurfacePan(MediaTrack *track, double pan, IReaperControlSurface *ignoresurf)
    {
        if (ValidateTrackPtr(track))
            ::CSurf_SetSurfacePan(track, pan, ignoresurf);
    }

    static void CSurf_SetSurfaceMute(MediaTrack *track, bool mute, IReaperControlSurface *ignoresurf)
    {
        if (ValidateTrackPtr(track))
            ::CSurf_SetSurfaceMute(track, mute, ignoresurf);
    }

    static double CSurf_OnWidthChange(MediaTrack *track, double width, bool relative)
    {
        if (ValidateTrackPtr(track))
            return ::CSurf_OnWidthChange(track, width, relative);
        else
            return 0.0;
    }
    
    static bool CSurf_OnSelectedChange(MediaTrack *track, int selected)
    {
        if (ValidateTrackPtr(track))
            return ::CSurf_OnSelectedChange(track, selected);
        else
            return false;
    }

    static void CSurf_SetSurfaceSelected(MediaTrack *track, bool selected, IReaperControlSurface *ignoresurf)
    {
        if (ValidateTrackPtr(track))
            ::CSurf_SetSurfaceSelected(track, selected, ignoresurf);
    }
    
    static void SetOnlyTrackSelected(MediaTrack *track)
    {
        if (ValidateTrackPtr(track))
            ::SetOnlyTrackSelected(track);
    }
    
    static bool CSurf_OnRecArmChange(MediaTrack *track, int recarm)
    {
        if (ValidateTrackPtr(track))
            return ::CSurf_OnRecArmChange(track, recarm);
        else
            return false;
    }

    static void CSurf_SetSurfaceRecArm(MediaTrack *track, bool recarm, IReaperControlSurface *ignoresurf)
    {
        if (ValidateTrackPtr(track))
            ::CSurf_SetSurfaceRecArm(track, recarm, ignoresurf);
    }

    static bool CSurf_OnSoloChange(MediaTrack *track, int solo)
    {
        if (ValidateTrackPtr(track))
            return ::CSurf_OnSoloChange(track, solo);
        else
            return false;
    }

    static void CSurf_SetSurfaceSolo(MediaTrack *track, bool solo, IReaperControlSurface *ignoresurf)
    {
        if (ValidateTrackPtr(track))
            ::CSurf_SetSurfaceSolo(track, solo, ignoresurf);
    }
    
    static bool IsTrackVisible(MediaTrack *track, bool mixer)
    {
        if (ValidateTrackPtr(track))
            return ::IsTrackVisible(track, mixer);
        else
            return false;
    }
    
    static MediaTrack *SetMixerScroll(MediaTrack *leftmosttrack)
    {
        if (ValidateTrackPtr(leftmosttrack))
            return ::SetMixerScroll(leftmosttrack);
        else
            return nullptr;
    }
};

#endif /* control_surface_integrator_Reaper_h */
