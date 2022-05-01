//
//  control_surface_integrator_ui.cpp
//  reaper_csurf_integrator
//
//

#include "control_surface_integrator.h"
#include "control_surface_integrator_ui.h"
#include <memory>

Manager* TheManager;
extern string GetLineEnding();

const string Control_Surface_Integrator = "Control Surface Integrator";

extern int g_registered_command_toggle_show_raw_surface_input;
extern int g_registered_command_toggle_show_surface_input;
extern int g_registered_command_toggle_show_surface_output;
extern int g_registered_command_toggle_show_FX_params;
extern int g_registered_command_toggle_write_FX_params;

bool hookCommandProc(int command, int flag)
{
    if(TheManager != nullptr)
    {
        if (command == g_registered_command_toggle_show_raw_surface_input)
        {
            TheManager->ToggleSurfaceRawInDisplay();
            return true;
        }
        else if (command == g_registered_command_toggle_show_surface_input)
        {
            TheManager->ToggleSurfaceInDisplay();
            return true;
        }
        else if (command == g_registered_command_toggle_show_surface_output)
        {
            TheManager->ToggleSurfaceOutDisplay();
            return true;
        }
        else if (command == g_registered_command_toggle_show_FX_params)
        {
            TheManager->ToggleFXParamsDisplay();
            return true;
        }
        else if (command == g_registered_command_toggle_write_FX_params)
        {
            TheManager->ToggleFXParamsWrite();
            return true;
        }
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// CSurfIntegrator
////////////////////////////////////////////////////////////////////////////////////////////////////////
CSurfIntegrator::CSurfIntegrator()
{
    TheManager = new Manager();
}

CSurfIntegrator::~CSurfIntegrator()
{
    if(TheManager)
    {
        TheManager->Shutdown();
        delete TheManager;
        TheManager= nullptr;
    }
}

void CSurfIntegrator::OnTrackSelection(MediaTrack *trackid)
{
    if(TheManager)
        TheManager->OnTrackSelection(trackid);
}

void CSurfIntegrator::SetTrackListChange()
{
    if(TheManager)
        TheManager->OnTrackListChange();
}

int CSurfIntegrator::Extended(int call, void *parm1, void *parm2, void *parm3)
{
    if(call == CSURF_EXT_SUPPORTS_EXTENDED_TOUCH)
    {
        return 1;
    }
    
    if(call == CSURF_EXT_RESET)
    {
       if(TheManager)
           TheManager->Init();
    }
    
    if(call == CSURF_EXT_SETFXCHANGE)
    {
        // parm1=(MediaTrack*)track, whenever FX are added, deleted, or change order
        if(TheManager)
            TheManager->TrackFXListChanged((MediaTrack*)parm1);
    }
        
    return 1;
}

bool CSurfIntegrator::GetTouchState(MediaTrack *track, int touchedControl)
{
    return TheManager->GetTouchState(track, touchedControl);
}

void CSurfIntegrator::Run()
{
    if(TheManager)
        TheManager->Run();
}

const char *CSurfIntegrator::GetTypeString()
{
    return "CSI";
}

const char *CSurfIntegrator::GetDescString()
{
    descspace.Set(Control_Surface_Integrator.c_str());
    return descspace.Get();
}

const char *CSurfIntegrator::GetConfigString() // string of configuration data
{
    snprintf(configtmp, sizeof(configtmp),"0 0");
    return configtmp;
}

static IReaperControlSurface *createFunc(const char *type_string, const char *configString, int *errStats)
{
    return new CSurfIntegrator();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FileSystem
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    static vector<string> GetDirectoryFilenames(const string& dir)
    {
        vector<string> filenames;
        shared_ptr<DIR> directory_ptr(opendir(dir.c_str()), [](DIR* dir){ dir && closedir(dir); });
        struct dirent *dirent_ptr;
        
        if(directory_ptr == nullptr)
            return filenames;
        
        while ((dirent_ptr = readdir(directory_ptr.get())) != nullptr)
            filenames.push_back(string(dirent_ptr->d_name));
        
        return filenames;
    }
    
    static vector<string> GetDirectoryFolderNames(const string& dir)
    {
        vector<string> folderNames;
        shared_ptr<DIR> directory_ptr(opendir(dir.c_str()), [](DIR* dir){ dir && closedir(dir); });
        struct dirent *dirent_ptr;
        
        if(directory_ptr == nullptr)
            return folderNames;
        
        while ((dirent_ptr = readdir(directory_ptr.get())) != nullptr)
            if(dirent_ptr->d_type == DT_DIR)
                folderNames.push_back(string(dirent_ptr->d_name));
        
        return folderNames;
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////
// structs
////////////////////////////////////////////////////////////////////////////////////////////////////////
struct SurfaceLine
{
    string type = "";
    string name = "";
    int inPort = 0;
    int outPort = 0;
    string templateFilename = "";
    string zoneTemplateFolder = "";
    int numChannels = 0;
    int channelOffset = 0;
    
    // for OSC
    string remoteDeviceIP = "";

};

struct PageLine
{
    string name = "";
    bool followMCP = true;
    bool synchPages = false;
    bool useScrollLink = false;
    //bool trackColouring = false;
    //int green = 0;
    //int blue = 0;
    vector<shared_ptr<SurfaceLine>> surfaces;
};

// Scratch pad to get in and out of dialogs easily
static bool editMode = false;
static int dlgResult = 0;

static int pageIndex = 0;

static string type = "";
static char name[BUFSZ];
static int inPort = 0;
static int outPort = 0;
static char remoteDeviceIP[BUFSZ];
static char templateFilename[BUFSZ];
static char zoneTemplateFolder[BUFSZ];

int numChannels = 0;
int channelOffset = 0;

static bool followMCP = true;
static bool synchPages = false;
//static bool trackColouring = false;
static bool useScrollLink = false;

static vector<shared_ptr<PageLine>> pages;

void AddComboEntry(HWND hwndDlg, int x, char * buf, int comboId)
{
    int a=SendDlgItemMessage(hwndDlg,comboId,CB_ADDSTRING,0,(LPARAM)buf);
    SendDlgItemMessage(hwndDlg,comboId,CB_SETITEMDATA,a,x);
}

void AddListEntry(HWND hwndDlg, string buf, int comboId)
{
    SendDlgItemMessage(hwndDlg, comboId, LB_ADDSTRING, 0, (LPARAM)buf.c_str());
}

static WDL_DLGRET dlgProcPage(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            if(editMode)
            {
                SetDlgItemText(hwndDlg, IDC_EDIT_PageName, name);
                
                if(followMCP)
                    CheckDlgButton(hwndDlg, IDC_RADIO_MCP, BST_CHECKED);
                else
                    CheckDlgButton(hwndDlg, IDC_RADIO_TCP, BST_CHECKED);
                
                if(synchPages)
                    CheckDlgButton(hwndDlg, IDC_CHECK_SynchPages, BST_CHECKED);
                else
                    CheckDlgButton(hwndDlg, IDC_CHECK_SynchPages, BST_UNCHECKED);
                
                if(useScrollLink)
                    CheckDlgButton(hwndDlg, IDC_CHECK_ScrollLink, BST_CHECKED);
                else
                    CheckDlgButton(hwndDlg, IDC_CHECK_ScrollLink, BST_UNCHECKED);
            }
            else
            {
                CheckDlgButton(hwndDlg, IDC_RADIO_TCP, BST_CHECKED); // default is to follow Track Control Panel
            }
        }
            break;
            
        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case IDC_RADIO_MCP:
                    CheckDlgButton(hwndDlg, IDC_RADIO_TCP, BST_UNCHECKED);
                    break;
                    
                case IDC_RADIO_TCP:
                    CheckDlgButton(hwndDlg, IDC_RADIO_MCP, BST_UNCHECKED);
                    break;
                    
                case IDOK:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        GetDlgItemText(hwndDlg, IDC_EDIT_PageName , name, sizeof(name));
                        
                        if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_MCP))
                            followMCP = true;
                        else
                            followMCP = false;
                        
                        if (IsDlgButtonChecked(hwndDlg, IDC_CHECK_SynchPages))
                            synchPages = true;
                        else
                            synchPages = false;
                        
                        if (IsDlgButtonChecked(hwndDlg, IDC_CHECK_ScrollLink))
                            useScrollLink = true;
                        else
                            useScrollLink = false;

                        dlgResult = IDOK;
                        EndDialog(hwndDlg, 0);
                    }
                    break ;
                    
                case IDCANCEL:
                    if (HIWORD(wParam) == BN_CLICKED)
                        EndDialog(hwndDlg, 0);
                    break ;
            }
        }
            break ;
            
        case WM_CLOSE:
            DestroyWindow(hwndDlg) ;
            break ;
            
        case WM_DESTROY:
            EndDialog(hwndDlg, 0);
            break;
    }
    
    return 0 ;
}

static WDL_DLGRET dlgProcMidiSurface(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            char buf[BUFSZ];
            int currentIndex = 0;
            
            for (int i = 0; i < GetNumMIDIInputs(); i++)
                if (GetMIDIInputName(i, buf, sizeof(buf)))
                {
                    AddComboEntry(hwndDlg, i, buf, IDC_COMBO_MidiIn);
                    if(editMode && inPort == i)
                        SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_MidiIn), CB_SETCURSEL, currentIndex, 0);
                    currentIndex++;
                }
            
            currentIndex = 0;
            
            for (int i = 0; i < GetNumMIDIOutputs(); i++)
                if (GetMIDIOutputName(i, buf, sizeof(buf)))
                {
                    AddComboEntry(hwndDlg, i, buf, IDC_COMBO_MidiOut);
                    if(editMode && outPort == i)
                        SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_MidiOut), CB_SETCURSEL, currentIndex, 0);
                    currentIndex++;
                }
            
            string resourcePath(DAW::GetResourcePath());
            
            int i = 0;
            for(auto filename : FileSystem::GetDirectoryFilenames(resourcePath + "/CSI/Surfaces/Midi/"))
            {
                int length = filename.length();
                if(length > 4 && filename[0] != '.' && filename[length - 4] == '.' && filename[length - 3] == 'm' && filename[length - 2] == 's' &&filename[length - 1] == 't')
                    AddComboEntry(hwndDlg, i++, (char*)filename.c_str(), IDC_COMBO_SurfaceTemplate);
            }
            
            for(auto foldername : FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/"))
                if(foldername[0] != '.')
                    AddComboEntry(hwndDlg, 0, (char *)foldername.c_str(), IDC_COMBO_ZoneTemplates);
            
            if(editMode)
            {
                SetDlgItemText(hwndDlg, IDC_EDIT_MidiSurfaceName, name);
                SetDlgItemText(hwndDlg, IDC_EDIT_NumChannels, to_string(numChannels).c_str());
                SetDlgItemText(hwndDlg, IDC_EDIT_ChannelOffset, to_string(channelOffset).c_str());

                int index = SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_FINDSTRINGEXACT, -1, (LPARAM)templateFilename);
                if(index >= 0)
                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_SETCURSEL, index, 0);
                
                index = SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)zoneTemplateFolder);
                if(index >= 0)
                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_SETCURSEL, index, 0);
            }
            else
            {
                SetDlgItemText(hwndDlg, IDC_EDIT_MidiSurfaceName, "");
                SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_MidiIn), CB_SETCURSEL, 0, 0);
                SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_MidiOut), CB_SETCURSEL, 0, 0);
                SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_SETCURSEL, 0, 0);
                SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_SETCURSEL, 0, 0);
                SetDlgItemText(hwndDlg, IDC_EDIT_NumChannels, "0");
                SetDlgItemText(hwndDlg, IDC_EDIT_ChannelOffset, "0");
            }
        }
            break;
            
        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case IDC_COMBO_SurfaceTemplate:
                {
                    switch (HIWORD(wParam))
                    {
                        case CBN_SELCHANGE:
                        {
                            int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_GETCURSEL, 0, 0);
                            if(index >= 0 && !editMode)
                            {
                                char buffer[BUFSZ];
                                
                                GetDlgItemText(hwndDlg, IDC_COMBO_SurfaceTemplate, buffer, sizeof(buffer));

                                for(int i = 0; i < sizeof(buffer); i++)
                                {
                                    if(buffer[i] == '.')
                                    {
                                        buffer[i] = 0;
                                        break;
                                    }
                                }
                                
                                int index = SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)buffer);
                                if(index >= 0)
                                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_SETCURSEL, index, 0);
                            }
                        }
                    }
                    
                    break;
                }

                case IDOK:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        char buf[BUFSZ];
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_NumChannels, buf, sizeof(buf));
                        numChannels = atoi(buf);
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_ChannelOffset, buf, sizeof(buf));
                        channelOffset = atoi(buf);
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_MidiSurfaceName, name, sizeof(name));
                        
                        int currentSelection = SendDlgItemMessage(hwndDlg, IDC_COMBO_MidiIn, CB_GETCURSEL, 0, 0);
                        if (currentSelection >= 0)
                            inPort = SendDlgItemMessage(hwndDlg, IDC_COMBO_MidiIn, CB_GETITEMDATA, currentSelection, 0);
                        currentSelection = SendDlgItemMessage(hwndDlg, IDC_COMBO_MidiOut, CB_GETCURSEL, 0, 0);
                        if (currentSelection >= 0)
                            outPort = SendDlgItemMessage(hwndDlg, IDC_COMBO_MidiOut, CB_GETITEMDATA, currentSelection, 0);
                        
                        GetDlgItemText(hwndDlg, IDC_COMBO_SurfaceTemplate, templateFilename, sizeof(templateFilename));
                        GetDlgItemText(hwndDlg, IDC_COMBO_ZoneTemplates, zoneTemplateFolder, sizeof(zoneTemplateFolder));
                        
                        dlgResult = IDOK;
                        EndDialog(hwndDlg, 0);
                    }
                    break ;
                    
                case IDCANCEL:
                    if (HIWORD(wParam) == BN_CLICKED)
                        EndDialog(hwndDlg, 0);
                    break ;
            }
        }
            break ;
            
        case WM_CLOSE:
            DestroyWindow(hwndDlg) ;
            break ;
            
        case WM_DESTROY:
            EndDialog(hwndDlg, 0);
            break;
    }
    
    return 0 ;
}

static WDL_DLGRET dlgProcOSCSurface(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            string resourcePath(DAW::GetResourcePath());
            
            int i = 0;
            for(auto filename : FileSystem::GetDirectoryFilenames(resourcePath + "/CSI/Surfaces/OSC/"))
            {
                int length = filename.length();
                if(length > 4 && filename[0] != '.' && filename[length - 4] == '.' && filename[length - 3] == 'o' && filename[length - 2] == 's' &&filename[length - 1] == 't')
                    AddComboEntry(hwndDlg, i++, (char*)filename.c_str(), IDC_COMBO_SurfaceTemplate);
            }
            
            for(auto foldername : FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/"))
                if(foldername[0] != '.')
                    AddComboEntry(hwndDlg, 0, (char *)foldername.c_str(), IDC_COMBO_ZoneTemplates);
            
            if(editMode)
            {
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCSurfaceName, name);
                SetDlgItemText(hwndDlg, IDC_EDIT_NumChannels, to_string(numChannels).c_str());
                SetDlgItemText(hwndDlg, IDC_EDIT_ChannelOffset, to_string(channelOffset).c_str());
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCRemoteDeviceIP, remoteDeviceIP);
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCInPort, to_string(inPort).c_str());
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCOutPort, to_string(outPort).c_str());
                
                int index = SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_FINDSTRINGEXACT, -1, (LPARAM)templateFilename);
                if(index >= 0)
                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_SETCURSEL, index, 0);
                
                index = SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)zoneTemplateFolder);
                if(index >= 0)
                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_SETCURSEL, index, 0);
            }
            else
            {
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCSurfaceName, "");
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCRemoteDeviceIP, "");
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCInPort, "");
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCOutPort, "");
                SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_SETCURSEL, 0, 0);
                SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_SETCURSEL, 0, 0);
                SetDlgItemText(hwndDlg, IDC_EDIT_NumChannels, "0");
                SetDlgItemText(hwndDlg, IDC_EDIT_ChannelOffset, "0");
            }
        }
            break;
            
        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case IDC_COMBO_SurfaceTemplate:
                {
                    switch (HIWORD(wParam))
                    {
                        case CBN_SELCHANGE:
                        {
                            int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_GETCURSEL, 0, 0);
                            if(index >= 0 && !editMode)
                            {
                                char buffer[BUFSZ];
                                
                                GetDlgItemText(hwndDlg, IDC_COMBO_SurfaceTemplate, buffer, sizeof(buffer));
                                
                                for(int i = 0; i < sizeof(buffer); i++)
                                {
                                    if(buffer[i] == '.')
                                    {
                                        buffer[i] = 0;
                                        break;
                                    }
                                }
                                
                                int index = SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)buffer);
                                if(index >= 0)
                                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_SETCURSEL, index, 0);
                            }
                        }
                    }
                    
                    break;
                }
                    
                case IDOK:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        char buf[BUFSZ];
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_NumChannels, buf, sizeof(buf));
                        numChannels = atoi(buf);
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_ChannelOffset, buf, sizeof(buf));
                        channelOffset = atoi(buf);
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_OSCSurfaceName, name, sizeof(name));
                        GetDlgItemText(hwndDlg, IDC_EDIT_OSCRemoteDeviceIP, remoteDeviceIP, sizeof(remoteDeviceIP));
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_OSCInPort, buf, sizeof(buf));
                        inPort = atoi(buf);
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_OSCOutPort, buf, sizeof(buf));
                        outPort = atoi(buf);
                        
                        GetDlgItemText(hwndDlg, IDC_COMBO_SurfaceTemplate, templateFilename, sizeof(templateFilename));
                        GetDlgItemText(hwndDlg, IDC_COMBO_ZoneTemplates, zoneTemplateFolder, sizeof(zoneTemplateFolder));
                        
                        dlgResult = IDOK;
                        EndDialog(hwndDlg, 0);
                    }
                    break ;
                    
                case IDCANCEL:
                    if (HIWORD(wParam) == BN_CLICKED)
                        EndDialog(hwndDlg, 0);
                    break ;
            }
        }
            break ;
            
        case WM_CLOSE:
            DestroyWindow(hwndDlg) ;
            break ;
            
        case WM_DESTROY:
            EndDialog(hwndDlg, 0);
            break;
    }
    
    return 0 ;
}

static WDL_DLGRET dlgProcMainConfig(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_COMMAND:
            {
                switch(LOWORD(wParam))
                {
                    case IDC_LIST_Pages:
                        if (HIWORD(wParam) == LBN_DBLCLK)
                        {
#ifdef WIN32
                            // pretend we clicked the Edit button
                            SendMessage(GetDlgItem(hwndDlg, IDC_BUTTON_EditPage), BM_CLICK, 0, 0);
#endif
                        }                       
                        else if (HIWORD(wParam) == LBN_SELCHANGE)
                        {
                            int index = SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                            if (index >= 0)
                            {
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_RESETCONTENT, 0, 0);

                                pageIndex = index;

                                for (auto surface : pages[index]->surfaces)
                                    AddListEntry(hwndDlg, surface->name, IDC_LIST_Surfaces);
                            }
                            else
                            {
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_RESETCONTENT, 0, 0);
                            }
                        }
                        break;

                    case IDC_LIST_Surfaces:
                        if (HIWORD(wParam) == LBN_DBLCLK)
                        {
#ifdef WIN32
                            // pretend we clicked the Edit button
                            SendMessage(GetDlgItem(hwndDlg, IDC_BUTTON_EditSurface), BM_CLICK, 0, 0);
#endif
                        }
                        break;

                    case IDC_BUTTON_AddPage:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            dlgResult = false;
                            followMCP = true;
                            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_Page), hwndDlg, dlgProcPage);
                            if(dlgResult == IDOK)
                            {
                                shared_ptr<PageLine> page = make_shared<PageLine>();
                                page->name = name;
                                page->followMCP = followMCP;
                                page->synchPages = synchPages;
                                page->useScrollLink = useScrollLink;
                                //page->trackColouring = trackColouring;
                                pages.push_back(page);
                                AddListEntry(hwndDlg, name, IDC_LIST_Pages);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_SETCURSEL, pages.size() - 1, 0);
                            }
                        }
                        break ;
                        
                    case IDC_BUTTON_AddMidiSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                            if (index >= 0)
                            {
                                dlgResult = false;
                                DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_MidiSurface), hwndDlg, dlgProcMidiSurface);
                                if(dlgResult == IDOK)
                                {
                                    shared_ptr<SurfaceLine> surface = make_shared<SurfaceLine>();
                                    surface->type = MidiSurfaceToken;
                                    surface->name = name;
                                    surface->inPort = inPort;
                                    surface->outPort = outPort;
                                    surface->templateFilename = templateFilename;
                                    surface->zoneTemplateFolder = zoneTemplateFolder;
                                    surface->numChannels = numChannels;
                                    surface->channelOffset = channelOffset;

                                    pages[pageIndex]->surfaces.push_back(surface);
                                    
                                    AddListEntry(hwndDlg, name, IDC_LIST_Surfaces);
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL,  pages[pageIndex]->surfaces.size() - 1, 0);
                                }
                            }
                        }
                        break ;
                        
                    case IDC_BUTTON_AddOSCSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                            if (index >= 0)
                            {
                                dlgResult = false;
                                DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_MidiSurface1), hwndDlg, dlgProcOSCSurface);
                                if(dlgResult == IDOK)
                                {
                                    shared_ptr<SurfaceLine> surface = make_shared<SurfaceLine>();
                                    surface->type = OSCSurfaceToken;
                                    surface->name = name;
                                    surface->remoteDeviceIP = remoteDeviceIP;
                                    surface->inPort = inPort;
                                    surface->outPort = outPort;
                                    surface->templateFilename = templateFilename;
                                    surface->zoneTemplateFolder = zoneTemplateFolder;
                                    surface->numChannels = numChannels;
                                    surface->channelOffset = channelOffset;

                                    pages[pageIndex]->surfaces.push_back(surface);
                                    
                                    AddListEntry(hwndDlg, name, IDC_LIST_Surfaces);
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL,  pages[pageIndex]->surfaces.size() - 1, 0);
                                }
                            }
                        }
                        break ;
                        
                    case IDC_BUTTON_EditPage:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                            if(index >= 0)
                            {
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_GETTEXT, index, (LPARAM)(LPCTSTR)(name));
                                followMCP = pages[index]->followMCP;
                                synchPages = pages[index]->synchPages;
                                useScrollLink = pages[index]->useScrollLink;

                                dlgResult = false;
                                editMode = true;
                                
                                DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_Page), hwndDlg, dlgProcPage);
                                if(dlgResult == IDOK)
                                {
                                    pages[index]->name = name;
                                    pages[index]->followMCP = followMCP;
                                    pages[index]->synchPages = synchPages;
                                    pages[index]->useScrollLink = useScrollLink;
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_RESETCONTENT, 0, 0);
                                    for(auto page :  pages)
                                        AddListEntry(hwndDlg, page->name, IDC_LIST_Pages);
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_SETCURSEL, index, 0);
                                }
                                
                                editMode = false;
                            }
                        }
                        break ;

                    case IDC_BUTTON_EditSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = SendDlgItemMessage(hwndDlg, IDC_LIST_Surfaces, LB_GETCURSEL, 0, 0);
                            if(index >= 0)
                            {
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_GETTEXT, index, (LPARAM)(LPCTSTR)(name));
                                inPort = pages[pageIndex]->surfaces[index]->inPort;
                                outPort = pages[pageIndex]->surfaces[index]->outPort;
                                strcpy(remoteDeviceIP, pages[pageIndex]->surfaces[index]->remoteDeviceIP.c_str());
                                strcpy(templateFilename, pages[pageIndex]->surfaces[index]->templateFilename.c_str());
                                strcpy(zoneTemplateFolder, pages[pageIndex]->surfaces[index]->zoneTemplateFolder.c_str());
                                numChannels = pages[pageIndex]->surfaces[index]->numChannels;
                                channelOffset = pages[pageIndex]->surfaces[index]->channelOffset;

                                dlgResult = false;
                                editMode = true;
                                
                                if(pages[pageIndex]->surfaces[index]->type == MidiSurfaceToken)
                                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_MidiSurface), hwndDlg, dlgProcMidiSurface);
                                else if(pages[pageIndex]->surfaces[index]->type == OSCSurfaceToken)
                                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_MidiSurface1), hwndDlg, dlgProcOSCSurface);
                                                               
                                if(dlgResult == IDOK)
                                {
                                    pages[pageIndex]->surfaces[index]->name = name;
                                    pages[pageIndex]->surfaces[index]->remoteDeviceIP = remoteDeviceIP;
                                    pages[pageIndex]->surfaces[index]->inPort = inPort;
                                    pages[pageIndex]->surfaces[index]->outPort = outPort;
                                    pages[pageIndex]->surfaces[index]->templateFilename = templateFilename;
                                    pages[pageIndex]->surfaces[index]->zoneTemplateFolder = zoneTemplateFolder;
                                    pages[pageIndex]->surfaces[index]->numChannels = numChannels;
                                    pages[pageIndex]->surfaces[index]->channelOffset = channelOffset;
                                }
                                
                                editMode = false;
                            }
                        }
                        break ;
                    
                    case IDC_BUTTON_RemovePage:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                            if(index >= 0)
                            {
                                pages.erase(pages.begin() + index);
                                
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_RESETCONTENT, 0, 0);
                                
                                for(auto page: pages)
                                    AddListEntry(hwndDlg, page->name, IDC_LIST_Pages);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_SETCURSEL, index, 0);
                            }
                        }
                        break ;

                    case IDC_BUTTON_RemoveSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = SendDlgItemMessage(hwndDlg, IDC_LIST_Surfaces, LB_GETCURSEL, 0, 0);
                            if(index >= 0)
                            {
                                pages[pageIndex]->surfaces.erase(pages[pageIndex]->surfaces.begin() + index);
                                
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_RESETCONTENT, 0, 0);
                                for(auto surface: pages[pageIndex]->surfaces)
                                    AddListEntry(hwndDlg, surface->name, IDC_LIST_Surfaces);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL, index, 0);
                            }
                        }
                        break ;
                }
            }
            break ;
            
        case WM_INITDIALOG:
        {
            pages.clear();
            
            ifstream iniFile(string(DAW::GetResourcePath()) + "/CSI/CSI.ini");
            
            int numChannels = 0;
            
            for (string line; getline(iniFile, line) ; )
            {
                line = regex_replace(line, regex(TabChars), " ");
                line = regex_replace(line, regex(CRLFChars), "");
                
                vector<string> tokens(GetTokens(line));
                
                if(tokens.size() > 4) // ignore comment lines and blank lines
                {
                    if(tokens[0] == MidiSurfaceToken && tokens.size() == 14)
                    {
                        if(atoi(tokens[6].c_str()) + atoi(tokens[9].c_str()) > numChannels )
                            numChannels = atoi(tokens[6].c_str()) + atoi(tokens[9].c_str());
                    }
                    else if(tokens[0] == OSCSurfaceToken && tokens.size() == 15)
                    {
                        if(atoi(tokens[6].c_str()) + atoi(tokens[9].c_str()) > numChannels )
                            numChannels = atoi(tokens[6].c_str()) + atoi(tokens[9].c_str());
                    }
                }
            }
            
            iniFile.clear();
            iniFile.seekg(0);
            
            for (string line; getline(iniFile, line) ; )
            {
                line = regex_replace(line, regex(TabChars), " ");
                line = regex_replace(line, regex(CRLFChars), "");
                                     
                if(line[0] != '\r' && line[0] != '/' && line != "") // ignore comment lines and blank lines
                {
                    istringstream iss(line);
                    vector<string> tokens;
                    string token;
                    
                    while (iss >> quoted(token))
                        tokens.push_back(token);
                    
                    if(tokens[0] == PageToken)
                    {
                        if(tokens.size() != 5)
                            continue;
 
                        shared_ptr<PageLine> page = make_shared<PageLine>();
                        page->name = tokens[1];
                        
                        if(tokens[2] == "FollowMCP")
                            page->followMCP = true;
                        else
                            page->followMCP = false;
                        
                        if(tokens[3] == "SynchPages")
                            page->synchPages = true;
                        else
                            page->synchPages = false;
                        
                        if(tokens[4] == "UseScrollLink")
                            page->useScrollLink = true;
                        else
                            page->useScrollLink = false;
                        
                        pages.push_back(page);
                        
                        AddListEntry(hwndDlg, page->name, IDC_LIST_Pages);
                    }
                    
                    else if(tokens[0] == MidiSurfaceToken || tokens[0] == OSCSurfaceToken)
                    {
                        shared_ptr<SurfaceLine> surface = make_shared<SurfaceLine>();
                        surface->type = tokens[0];
                        surface->name = tokens[1];
                        
                        if((surface->type == MidiSurfaceToken || surface->type == OSCSurfaceToken) && (tokens.size() == 8 || tokens.size() == 9))
                        {
                            surface->inPort = atoi(tokens[2].c_str());
                            surface->outPort = atoi(tokens[3].c_str());
                            surface->templateFilename = tokens[4];
                            surface->zoneTemplateFolder = tokens[5];
                            surface->numChannels = atoi(tokens[6].c_str());
                            surface->channelOffset = atoi(tokens[7].c_str());

                            if(tokens[0] == OSCSurfaceToken && tokens.size() == 9)
                                surface->remoteDeviceIP = tokens[8];
                        }
                        
                        if(pages.size() > 0)
                            pages[pages.size() - 1]->surfaces.push_back(surface);
                    }
                }
            }
            
            SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_SETCURSEL, 0, 0);
            SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL, 0, 0);

            // the messages above don't trigger the user-initiated code, so pretend the user selected them
            SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_LIST_Pages, LBN_SELCHANGE), 0);
        }
        break;
        
        case WM_USER+1024:
        {
            ofstream iniFile(string(DAW::GetResourcePath()) + "/CSI/CSI.ini");

            if(iniFile.is_open())
            {
                iniFile << "Version 2.0" + GetLineEnding();
                
                string line = "";
                
                for(auto page : pages)
                {
                    line = PageToken + " ";
                    line += "\"" + page->name + "\" ";
                    
                    if(page->followMCP)
                        line += "FollowMCP ";
                    else
                        line += "FollowTCP ";
                    
                    if(page->synchPages)
                        line += "SynchPages ";
                    else
                        line += "NoSynchPages ";
                    
                    if(page->useScrollLink)
                        line += "UseScrollLink ";
                    else
                        line += "NoScrollLink ";
                    
                    line += GetLineEnding();
                    
                    iniFile << line;

                    for(auto surface : page->surfaces)
                    {
                        line = surface->type + " ";
                        line += "\"" + surface->name + "\" ";
                        
                        if(surface->type == MidiSurfaceToken || surface->type == OSCSurfaceToken)
                        {
                            line += to_string(surface->inPort) + " " ;
                            line += to_string(surface->outPort) + " " ;
                            line += "\"" + surface->templateFilename + "\" ";
                            line += "\"" + surface->zoneTemplateFolder + "\" ";
                            line += to_string(surface->numChannels) + " " ;
                            line += to_string(surface->channelOffset) + " " ;
                            
                            if(surface->type == OSCSurfaceToken)
                                line += " " + surface->remoteDeviceIP + " ";
                        }

                        line += GetLineEnding();
                        
                        iniFile << line;
                    }
                    
                    iniFile << GetLineEnding();
                }

                iniFile.close();
            }
        }
        break;
    }
    
    return 0;
}

static HWND configFunc(const char *type_string, HWND parent, const char *initConfigString)
{
    return CreateDialogParam(g_hInst,MAKEINTRESOURCE(IDD_SURFACEEDIT_CSI),parent,dlgProcMainConfig,(LPARAM)initConfigString);
}

reaper_csurf_reg_t csurf_integrator_reg =
{
    "CSI",
    Control_Surface_Integrator.c_str(),
    createFunc,
    configFunc,
};
