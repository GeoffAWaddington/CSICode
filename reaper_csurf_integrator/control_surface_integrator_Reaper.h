//
//  control_surface_integrator_Reaper.h
//  reaper_csurf_integrator
//
//

#ifndef control_surface_integrator_Reaper_h
#define control_surface_integrator_Reaper_h

#define REAPERAPI_WANT_TrackFX_GetParamNormalized
#define REAPERAPI_WANT_TrackFX_SetParamNormalized
#include "reaper_plugin_functions.h"

#include <string>
#include <vector>

#include "handy_functions.h" //for LogToConsole()

using namespace std;

extern HWND g_hwnd;

const int MEDBUF = 512;
const int SMLBUF = 256;

struct rgba_color
{
    int r;
    int g;
    int b;
    int a;

    bool operator == (const rgba_color &other) const
    {
        return r == other.r && g == other.g && b == other.b && a == other.a ;
    }

    bool operator != (const rgba_color &other) const
    {
        return r != other.r || g != other.g || b != other.b || a != other.a;
    }

    const char *rgba_to_string(char *buf) const // buf must be at least 10 bytes
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

static bool GetColorValue(const char *hexColor, rgba_color &colorValue)
{
    if (strlen(hexColor) == 7)
    {
        return sscanf(hexColor, "#%2x%2x%2x", &colorValue.r, &colorValue.g, &colorValue.b) == 3;
    }
    if (strlen(hexColor) == 9)
    {
        return sscanf(hexColor, "#%2x%2x%2x%2x", &colorValue.r, &colorValue.g, &colorValue.b, &colorValue.a) == 4;
    }
    return false;
}

struct MIDI_event_ex_t : MIDI_event_t
{
    MIDI_event_ex_t()
    {
        frame_offset = 0;
        size = 3;
        midi_message[0] = 0x00;
        midi_message[1] = 0x00;
        midi_message[2] = 0x00;
        midi_message[3] = 0x00;
    };

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
private:
    enum Modes { e_all = 0, e_folder, e_tracks, e_spill }; //static Modes items; // this is legal
    
    inline static bool spillMode_ = false;
    inline static int startSpill_ = 1;

    struct Params
    {
        bool showfolders;
        bool showchild;
        int start;
        int end;
        int folderheight;
        int childheight;
    };

public:

    DAW()
    {
    }

    static void SendCommandMessage(WPARAM wparam) { ::SendMessage(g_hwnd, WM_COMMAND, wparam, 0); }

    static void SendCommandMessage(const char* cmd)
    {
        int commandId = NamedCommandLookup(cmd);
        SendCommandMessage(commandId);
    }

    static MediaTrack *GetSelectedTrack(int seltrackidx) { return ::GetSelectedTrack(NULL, seltrackidx); }

    static bool ValidateTrackPtr(MediaTrack *track) { return ::ValidatePtr(track, "MediaTrack*"); }

    static bool CanUndo()
    {
        if (::Undo_CanUndo2(NULL))
            return true;
        else
            return false;
    }

    static bool CanRedo()
    {
        if (::Undo_CanRedo2(NULL))
            return true;
        else
            return false;
    }

    static void Undo()
    {
        if (CanUndo())
            ::Undo_DoUndo2(NULL);
    }

    static void Redo()
    {
        if (CanRedo())
            ::Undo_DoRedo2(NULL);
    }

    static MediaTrack *GetTrack(int trackidx)
    {
        trackidx--;

        if (trackidx < 0)
            trackidx = 0;

        return ::GetTrack(NULL, trackidx) ;
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

    static const char* GetCommandName(int cmdID)
    {
        const char* actionName = ::kbd_getTextFromCmd(cmdID, ::SectionFromUniqueID(1));
        if (actionName)
            return actionName;
        else
            return "NOT FOUND!";
    }

    //====================== My DAW Sync Functions ==============================
    static void SetHomeView() //control_surface_integrator.h class ZoneManager->GoHome()
    {
        SendCommandMessage("_SWSTL_SHOWALLTCP");
        SendCommandMessage("_d5b9a4565d3e7340b1b1a17142c42dab"); //Custom: HOME
        SendCommandMessage("_ed94f234ca542041b239da13df07fcca"); //Custom : SetHeights
        //TOADD Global automation override: All automation in trim/read mode
    }

    static void UpdateTracks(Params p)  //WIP
    {
        int show;
        int folderdepth;
        int numTracks = GetNumTracks();

        int height;
        bool start = false;

        for (int i = 1; i <= numTracks; ++i)
        {
            if (MediaTrack* track = CSurf_TrackFromID(i, true))
            {
                if (IsTrackVisible(track, true))
                {
                    show = 0; //start as hide on each pass in the loop

                    folderdepth = (int)GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH");

                    if (p.start == i)  start = true;

                    if (folderdepth == 1)
                    {
                        height = p.folderheight;
                        if (start && p.showfolders)
                            show = 1;
                    }
                    else
                    {
                        height = p.childheight;
                        if (start && p.showchild)
                            show = 1;
                    }

                    GetSetMediaTrackInfo(track, "I_HEIGHTOVERRIDE", &height);
                    //GetSetMediaTrackInfo(track, "I_AUTOMODE", &automode);
                    GetSetMediaTrackInfo(track, "B_SHOWINTCP", &show);

                    if (folderdepth == p.end) start = false; //end of spill
                }
            }
        }
        TrackList_AdjustWindows(true); //replaced SendCommandMessage("_SWS_SAVEVIEW"); and SendCommandMessage("_SWS_RESTOREVIEW");
    }

    static void Folder_Spill_View(MediaTrack* track)
    {
        Params p;
        if (spillMode_ == false) //folder
        {
            p = { true, false,  1, 2, 80, 80 };
            UpdateTracks(p);
        }
        else                    //spill
        {
            if(track != NULL)
                startSpill_ = (int)GetMediaTrackInfo_Value(track, "IP_TRACKNUMBER");
            p = { true, true, startSpill_, -1,  22, 80 };
            UpdateTracks(p);
        }
    }

    static void UpdateView(const char* name, bool status, MediaTrack* track) //CALLED: control_surface_manager_actions.h class GoZone->Do() DAW::UpdateView(name);
    {
        Params p;
        if (!strcmp(name, "Folder") && status == true)
        {
            Folder_Spill_View(NULL);
        }
       else if (!strcmp(name, "SelectedTracks") && status == true)
       {
            p = { true, true, 1, 2, 80, 600}; //         params[0] = { 80, 600, 1 }; //add new param show selected tracks
           UpdateTracks(p);
           SendCommandMessage("_SWSTL_SHOWTCPEX"); //SWS: Show selected track(s) in TCP, hide others
           SendCommandMessage(40888);              //Envelope: Show all active envelopes for tracks
           SendCommandMessage(40421);              //Item : Select all items in track
           SendCommandMessage(40847);              //Item: Open item inline editors
       }
       else if (!strcmp(name, "ToggleSpill") && status == true)
        {
            spillMode_ = !spillMode_;
            Folder_Spill_View(track);
        }
       else
       {
           p = {true, true, 1, 2,  22, 80};
           UpdateTracks(p);
           SendCommandMessage(41887);  //Item: Close item inline editors
           SendCommandMessage(41150);  //Envelope : Hide all envelopes for all tracks
           SendCommandMessage(41887);  //Item: Close item inline editors
       }
    }

    static void ShowInfo() //FOR DEBUGGING
    {
        int numTracks = GetNumTracks();
        for (int i = 1; i <= numTracks; ++i)
        {
            if (MediaTrack* track = CSurf_TrackFromID(i, true))
                if (IsTrackVisible(track, true))
                {
                    char buf[MEDBUF];

                    GetTrackName(track, buf, sizeof(buf));
                    int h = (int)GetMediaTrackInfo_Value(track, "I_TCPH");
                    int d = (int)GetMediaTrackInfo_Value(track, "I_FOLDERDEPTH");

                    LogToConsole(512, "[KEV]  Name:%s Height:%d Depth:%d\n", buf, h, d);
                }
        }
    }
};

#endif /* control_surface_integrator_Reaper_h */
