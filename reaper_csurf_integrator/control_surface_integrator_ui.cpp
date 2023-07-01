//
//  control_surface_integrator_ui.cpp
//  reaper_csurf_integrator
//
//

#include "control_surface_integrator.h"
#include "control_surface_integrator_ui.h"
#include <memory>

unique_ptr<Manager> TheManager;
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
    TheManager = make_unique<Manager>();
}

CSurfIntegrator::~CSurfIntegrator()
{
    if(TheManager)
        TheManager->Shutdown();
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
        
    if(call == CSURF_EXT_SETMIXERSCROLL)
    {
        if(TheManager)
        {
            MediaTrack* leftPtr = (MediaTrack*)parm1;
            
            int offset = DAW::CSurf_TrackToID(leftPtr, true);
            
            offset--;
            
            if(offset < 0)
                offset = 0;
                
            TheManager->SetTrackOffset(offset);
        }
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
// Remap Auto FX
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
vector<CSILayoutInfo> fxLayouts;
static vector<string> fxParamSlots;
static vector<vector<string>> surfaceLayoutTemplate;

static string fxName = "";
static string fxAlias = "";

struct FXParamDefinition
{
    string paramNumber = "";
    vector<string> modifiers;

    string widget = "";
    string widgetAction = "";
    map<string, string> widgetProperties;

    string aliasDisplayWidget = "";
    string aliasAction = "";
    string alias = "";
    map<string, string> aliasDisplayWidgetProperties;
    
    string valueDisplayWidget = "";
    string valueAction = "";
    map<string, string> valueDisplayWidgetProperties;

    vector<string> steps;
    
    
    
};

static void GetWidgetNameAndModifiers(string line, string &widgetName, vector<string> &modifiers)
{
    istringstream modifiersAndWidgetName(line);
    vector<string> modifiersAndWidgetNameTokens;
    string modifiersAndWidgetNameToken;
    
    while (getline(modifiersAndWidgetName, modifiersAndWidgetNameToken, '+'))
        modifiersAndWidgetNameTokens.push_back(modifiersAndWidgetNameToken);
    
    widgetName = modifiersAndWidgetNameTokens[modifiersAndWidgetNameTokens.size() - 1];

    widgetName = regex_replace(widgetName, regex("[\\d+$]"), "");
    
    for(auto layout : fxLayouts)
    {
        if(layout.suffix != "")
        {
            if(widgetName.substr(widgetName.length() - layout.suffix.length(), layout.suffix.length()) == layout.suffix)
            {
                widgetName = widgetName.substr(0, widgetName.length() - layout.suffix.length());
                break;
            }
        }
        
        //string suffix = widgetName.substr(
    }
    
    if(modifiersAndWidgetNameTokens.size() > 1)
        for(int i = 0; i < modifiersAndWidgetNameTokens.size() - 1; i++)
            modifiers.push_back(modifiersAndWidgetNameTokens[i]);

}

static void GetProperties(int start, int finish, vector<string> &tokens, map<string, string> &properties)
{
    for(int i = start; i < finish; i++)
    {
        if(tokens[i].find("=") != string::npos)
        {
            istringstream property(tokens[i]);
            vector<string> kvp;
            string token;
            
            while(getline(property, token, '='))
                kvp.push_back(token);

            if(kvp.size() == 2)
                properties[kvp[0]] = kvp[1];
        }
    }
}

static vector<FXParamDefinition> paramDefs;

vector<string> allParams;

void UnpackZone(string fullPath)
{
    fxName = "";
    fxAlias = "";

    paramDefs.clear();
    allParams.clear();
    
    bool inZone = false;

    ifstream autoFXFile(fullPath);
    
    for (string line; getline(autoFXFile, line) ; )
    {
        line = regex_replace(line, regex(CRLFChars), "");

        if(line == "")
            continue;
        
        vector<string> tokens = GetTokens(line);
        
        if(line.substr(0, 5) == "Zone ")
        {
            inZone = true;
            
            if(tokens.size() > 1)
                fxName = tokens[1];
            if(tokens.size() > 2)
                fxAlias = tokens[2];
        }
        else if(line == "ZoneEnd")
        {
            inZone = false;
        }
        else if(! inZone)
        {
            line = regex_replace(line, regex("/"), "");
            
            allParams.push_back(line);
        }
        else
        {
            tokens = GetTokens(line);
            
            if(tokens.size() > 2 && tokens[1] == "FXParam")
            {
                FXParamDefinition def;
                
                GetWidgetNameAndModifiers(tokens[0], def.widget, def.modifiers);
                
                def.paramNumber = tokens[2];
                def.widgetAction = tokens[1];
                
                if(tokens.size() > 4 && tokens[3] == "[")
                    for(int i = 4; i < tokens.size() && tokens[i] != "]"; i++)
                        def.steps.push_back(tokens[i]);
                
                if(tokens.size() > def.steps.size() + 5)
                    GetProperties(def.steps.size() + 5,  tokens.size(), tokens, def.widgetProperties);
                
                if(getline(autoFXFile, line))
                {
                    tokens = GetTokens(line);

                    if(tokens.size() > 2)
                    {
                        vector<string> modifers;
                        
                        GetWidgetNameAndModifiers(tokens[0], def.aliasDisplayWidget, modifers);
                        
                        def.aliasAction = tokens[1];
                        def.alias = tokens[2];
                        
                        if(tokens.size() > 3)
                            GetProperties(3, tokens.size(), tokens, def.aliasDisplayWidgetProperties);
                    }
                }
                else
                    continue;
                
                if(getline(autoFXFile, line))
                {
                    tokens = GetTokens(line);

                    if(tokens.size() > 2)
                    {
                        vector<string> modifers;
                        
                        GetWidgetNameAndModifiers(tokens[0], def.valueDisplayWidget, modifers);
                        
                        def.valueAction = tokens[1];
                        
                        if(tokens.size() > 3)
                            GetProperties(3, tokens.size(), tokens, def.valueDisplayWidgetProperties);
                    }
                }
                else
                    continue;
                
                paramDefs.push_back(def);
            }
        }
    }
}








struct FXLayoutStruct
{
    string prefix;
    string suffix;
    string slot;
    
    FXLayoutStruct(string aPrefix, string aSuffix, string aSlot) : prefix(aPrefix), suffix(aSuffix), slot(aSlot) {}
};

struct FXParamStruct
{
    string paramType = "";
    string paramNum = "";
    string displayName = "";
    string steps = "";
    bool usePush = false;
    
    FXParamStruct(string aParamType, string aParamNum, string aDisplayName) : paramType(aParamType), paramNum(aParamNum), displayName(aDisplayName) {}
    FXParamStruct(string aParamType, string aParamNum, string aDisplayName, string numSteps, bool shouldUsePush) : paramType(aParamType), paramNum(aParamNum), displayName(aDisplayName), steps(numSteps), usePush(shouldUsePush) {}
};

vector<string> allLines;
vector<FXLayoutStruct> layouts;
vector<FXParamStruct> params;






int fxListIndex = 0;

static int dlgResult = 0;

static WDL_DLGRET dlgProcEditFXParam(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            SendDlgItemMessage(hwndDlg, IDC_S1PickSteps, CB_ADDSTRING, 0, (LPARAM)"Custom");
            
            for(auto [key, value] : SteppedValueDictionary)
                SendDlgItemMessage(hwndDlg, IDC_S1PickSteps, CB_ADDSTRING, 0, (LPARAM)to_string(key).c_str());
             
            
           
            break;
        }
            
        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case IDCANCEL:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        dlgResult = IDCANCEL;
                        EndDialog(hwndDlg, 0);
                    }
                    break ;

                case IDOK:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                       
                        dlgResult = IDOK;
                        EndDialog(hwndDlg, 0);
                    }
                    break ;
                    
                case IDC_S1PickSteps:
                {
                    switch (HIWORD(wParam))
                    {
                        case CBN_SELCHANGE:
                        {
                            int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_S1PickSteps), CB_GETCURSEL, 0, 0);
                            if(index >= 0)
                            {
                                if(index == 0)
                                    SetDlgItemText(hwndDlg, IDC_S1EditSteps, "");
                                else
                                {
                                    ostringstream stepStr;
                                    
                                    for(auto step : SteppedValueDictionary[index + 1])
                                    {
                                        stepStr << std::setprecision(2) << step;
                                        stepStr <<  "  ";
                                    }
                                        
                                    SetDlgItemText(hwndDlg, IDC_S1EditSteps, (stepStr.str()).c_str());
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    return 0;
}

static string GetParamString(int index)
{
    string widgetName = paramDefs[index].widget;
    
    if(widgetName == "RotaryPush")
        widgetName = "Push";
    
    return fxParamSlots[index] +  " \t" + widgetName + " " + paramDefs[index].paramNumber + " " + paramDefs[index].alias;
}

static void PopulateListView(HWND hwndParamList)
{
    ListView_DeleteAllItems(hwndParamList);
        
    LVITEM lvi;
    lvi.mask      = LVIF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvi.stateMask = 0;
    lvi.iSubItem  = 0;
    lvi.state     = 0;

    for(int i = 0; i < paramDefs.size(); i++)
    {
        char buf[BUFSZ];
        
        sprintf(buf, GetParamString(i).c_str());
                       
        lvi.iItem = i;
        lvi.pszText = buf;
        
        ListView_InsertItem(hwndParamList, &lvi);
    }
}

static void MoveUp(HWND hwndParamList)
{
    int index = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
    if(index > 0)
    {
        FXParamStruct itemToMove(params[index].paramType, params[index].paramNum, params[index].displayName, params[index].steps, params[index].usePush);
        params.erase(params.begin() + index);
        params.insert(params.begin() + index - 1, itemToMove);
        
        ListView_SetItemText(hwndParamList, index, 0, (LPSTR)GetParamString(index).c_str());
        ListView_SetItemText(hwndParamList, index - 1, 0, (LPSTR)GetParamString(index - 1).c_str());
 
#ifdef _WIN32
        PopulateListView(hwndParamList);
#endif
        
        ListView_SetItemState(hwndParamList, index - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

static void MoveDown(HWND hwndParamList)
{
    int index = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
    if(index >= 0 && index < params.size() - 1)
    {
        FXParamStruct itemToMove(params[index].paramType, params[index].paramNum, params[index].displayName, params[index].steps, params[index].usePush);
        params.erase(params.begin() + index);
        params.insert(params.begin() + index + 1, itemToMove);

        ListView_SetItemText(hwndParamList, index, 0, (LPSTR)GetParamString(index).c_str());
        ListView_SetItemText(hwndParamList, index + 1, 0, (LPSTR)GetParamString(index + 1).c_str());

#ifdef _WIN32
        PopulateListView(hwndParamList);
#endif

        ListView_SetItemState(hwndParamList, index + 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

static bool isDragging = false;

#ifdef _WIN32

#pragma comment(lib, "comctl32.lib")

static HIMAGELIST   hDragImageList;
static int          oldPosition;

static WDL_DLGRET dlgProcRemapFXAutoZone(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_NOTIFY:
        {
            if(((LPNMHDR)lParam)->code == LVN_BEGINDRAG)
            {
                HWND hwndParamList = GetDlgItem(hwndDlg, IDC_PARAM_LIST);
               
                POINT p;
                p.x = 8;
                p.y = 8;

                int iPos = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
                if(iPos != -1)
                {
                    oldPosition = iPos;

                    isDragging = TRUE;

                    hDragImageList = ListView_CreateDragImage(hwndParamList, iPos, &p);
                    ImageList_BeginDrag(hDragImageList, 0, 0, 0);

                    POINT pt = ((NM_LISTVIEW*) ((LPNMHDR)lParam))->ptAction;
                    ClientToScreen(hwndParamList, &pt);

                    ImageList_DragEnter(GetDesktopWindow(), pt.x, pt.y);

                    SetCapture(hwndDlg);
                }
            }
            
            else if(((LPNMHDR)lParam)->code == NM_DBLCLK)
            {
                HWND hwndParamList = GetDlgItem(hwndDlg, IDC_PARAM_LIST);
                
                int index = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
                if(index >= 0)
                {
                    fxListIndex = index;
                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_EditFXParam), g_hwnd, dlgProcEditFXParam);
                    
                    if(dlgResult == IDOK)
                        ListView_SetItemText(hwndParamList, index, 0, (LPSTR)GetParamString(index).c_str());
                }
            }
        }
            break;
            
        case WM_INITDIALOG:
        {
            dlgResult = IDCANCEL;
            
            SetDlgItemText(hwndDlg, IDC_FXNAME, fxName.c_str());
            SetDlgItemText(hwndDlg, IDC_EDIT_FXAlias, fxAlias.c_str());
            
            PopulateListView(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
            
            break;
        }
            
        case WM_LBUTTONUP:
        {
            if(isDragging)
            {
                isDragging = FALSE;
                
                HWND hwndParamList = GetDlgItem(hwndDlg, IDC_PARAM_LIST);
                
                ImageList_DragLeave(hwndParamList);
                ImageList_EndDrag();
                ImageList_Destroy(hDragImageList);

                ReleaseCapture();

                LVHITTESTINFO       lvhti;
                lvhti.pt.x = LOWORD(lParam);
                lvhti.pt.y = HIWORD(lParam);
                ClientToScreen(hwndDlg, &lvhti.pt);
                ScreenToClient(hwndParamList, &lvhti.pt);
                ListView_HitTest(hwndParamList, &lvhti);

                // Out of the ListView?
                if (lvhti.iItem == -1)
                    break;
                // Not in an item?
                if ((lvhti.flags & LVHT_ONITEMLABEL == 0) &&
                          (lvhti.flags & LVHT_ONITEMSTATEICON == 0))
                    break;

                FXParamStruct itemToMove(params[oldPosition].paramType, params[oldPosition].paramNum, params[oldPosition].displayName, params[oldPosition].steps, params[oldPosition].usePush);
                params.erase(params.begin() + oldPosition);
                params.insert(params.begin() + lvhti.iItem, itemToMove);
                
                PopulateListView(hwndParamList);
            }
            break;
        }

        case WM_MOUSEMOVE:
        {
            if(isDragging)
            {
                POINT p;
                p.x = LOWORD(lParam);
                p.y = HIWORD(lParam);

                ClientToScreen(hwndDlg, &p);
                ImageList_DragMove(p.x, p.y);
            }
            break;
        }
            
        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case IDC_BUTTONUP:
                    MoveUp(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
                    break;
                    
                case IDC_BUTTONDOWN:
                    MoveDown(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
                    break;
                   
                case IDSAVE:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        char buf[100];
                        GetDlgItemText(hwndDlg, IDC_EDIT_FXAlias, buf, sizeof(buf));
                        fxAlias = buf;
                        
                        dlgResult = IDSAVE;
                        EndDialog(hwndDlg, 0);
                    }
                    break ;
                   
                case IDEDIT:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        HWND hwndParamList = GetDlgItem(hwndDlg, IDC_PARAM_LIST);
                        
                        int index = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
                        if(index >= 0)
                        {
                            fxListIndex = index;
                            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_EditFXParam), g_hwnd, dlgProcEditFXParam);
                            
                            if(dlgResult == IDOK)
                                ListView_SetItemText(hwndParamList, index, 0, (LPSTR)GetParamString(index).c_str());
                        }
                    }
                    break ;
                    
                case IDCANCEL:
                    if (HIWORD(wParam) == BN_CLICKED)
                        EndDialog(hwndDlg, 0);
                    break ;
            }
        }
    }
    
    return 0;
}

#else

POINT lastCursorPosition;

static WDL_DLGRET dlgProcRemapFXAutoZone(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_NOTIFY:
        {
            if(((LPNMHDR)lParam)->code == LVN_BEGINDRAG)
            {
                isDragging = true;
                GetCursorPos(&lastCursorPosition);
                SetCapture(hwndDlg);
            }
            
            else if(((LPNMHDR)lParam)->code == NM_DBLCLK)
            {
                HWND hwndParamList = GetDlgItem(hwndDlg, IDC_PARAM_LIST);
                
                int index = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
                if(index >= 0)
                {
                    fxListIndex = index;
                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_EditFXParam), g_hwnd, dlgProcEditFXParam);
                    
                    if(dlgResult == IDOK)
                        ListView_SetItemText(hwndParamList, index, 0, (LPSTR)GetParamString(index).c_str());
                }
            }
        }
            break;
            
        case WM_INITDIALOG:
        {
            dlgResult = IDCANCEL;
            
            SetDlgItemText(hwndDlg, IDC_FXNAME, fxName.c_str());
            SetDlgItemText(hwndDlg, IDC_EDIT_FXAlias, fxAlias.c_str());
            
            PopulateListView(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
            
            break;
        }
            
        case WM_LBUTTONUP:
        {
            if(isDragging)
            {
                isDragging = false;
                ReleaseCapture();
            }
            break;
        }

        case WM_MOUSEMOVE:
        {
            if(isDragging)
            {
                POINT currentCursorPosition;
                GetCursorPos(&currentCursorPosition);
                
                if(lastCursorPosition.y > currentCursorPosition.y && lastCursorPosition.y - currentCursorPosition.y > 21)
                {
                    lastCursorPosition = currentCursorPosition;
                    
                    MoveDown(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
                }
                else if(currentCursorPosition.y > lastCursorPosition.y && currentCursorPosition.y - lastCursorPosition.y > 21)
                {
                    lastCursorPosition = currentCursorPosition;
                    
                    MoveUp(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
                }
            }
            break;
        }
            
        case WM_KEYDOWN:
            switch (wParam)
            {
                case VK_LEFT:
                    MoveUp(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
                    break;
                    
                case VK_RIGHT:
                    MoveDown(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
                    break;
            }
            break;

        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case IDSAVE:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        char buf[100];
                        GetDlgItemText(hwndDlg, IDC_EDIT_FXAlias, buf, sizeof(buf));
                        fxAlias = buf;
                        
                        dlgResult = IDSAVE;
                        EndDialog(hwndDlg, 0);
                    }
                    break ;
                   
                case IDEDIT:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        HWND hwndParamList = GetDlgItem(hwndDlg, IDC_PARAM_LIST);
                        
                        int index = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
                        if(index >= 0)
                        {
                            fxListIndex = index;
                            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_EditFXParam), g_hwnd, dlgProcEditFXParam);
                            
                            if(dlgResult == IDOK)
                                ListView_SetItemText(hwndParamList, index, 0, (LPSTR)GetParamString(index).c_str());
                        }
                    }
                    break ;
                    
                case IDCANCEL:
                    if (HIWORD(wParam) == BN_CLICKED)
                        EndDialog(hwndDlg, 0);
                    break ;
            }
        }
    }
    
    return 0;
}
#endif

bool RemapAutoZoneDialog(shared_ptr<ZoneManager> zoneManager, string fullPath, vector<string> &fxPrologue,  vector<string> &fxEpilogue)
{
    fxLayouts = zoneManager->GetFXLayouts();
    
    for(auto layout : fxLayouts)
    {
        for(int i = 0; i < layout.channelCount; i++)
        {
            string modifiers = "";
            if(layout.modifiers != "")
                modifiers = layout.modifiers + "+";
            fxParamSlots.push_back(modifiers + layout.suffix + to_string(i + 1));
        }
    }
    
    surfaceLayoutTemplate = zoneManager->GetSurfaceFXLayoutTemplate();

    UnpackZone(fullPath);
    
    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_RemapAutoFX), g_hwnd, dlgProcRemapFXAutoZone);
    
    if(dlgResult == IDSAVE)
    {
        /*
        int layoutIndex = 0;

        ofstream fxFile(fullPath);

        if(fxFile.is_open())
        {
            
            for(int allLinesIndex = 0; allLinesIndex < allLines.size(); allLinesIndex++)
            {
                if(allLinesIndex == 0)
                    fxFile << "Zone \"" + fxName + "\" \"" + fxAlias + "\"" + GetLineEnding();
                else if(allLines[allLinesIndex].find("FXLayout") == string::npos)
                    fxFile << allLines[allLinesIndex] + GetLineEnding();
                else if(layoutIndex < layouts.size())
                {
                    fxFile << "\t" + layouts[layoutIndex].prefix + " \"" + layouts[layoutIndex].suffix + "\" " + layouts[layoutIndex].slot + " " + params[layoutIndex].paramType + " " + params[layoutIndex].paramNum + " \"" + params[layoutIndex].displayName + "\"";
                    if(params[layoutIndex].steps != "")
                        fxFile << " \"" + params[layoutIndex].steps + "\"" + GetLineEnding();
                    else
                        fxFile << GetLineEnding();
                    
                    layoutIndex++;
                }
            }

            fxFile.close();
        }
         */
        
        return true;
    }
    else
        return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FileSystem
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    static vector<string> GetDirectoryFilenames(const string& path)
    {
        vector<string> filenames;
        filesystem::path files {path};
        
        if (std::filesystem::exists(files) && std::filesystem::is_directory(files))
            for (auto& file : std::filesystem::directory_iterator(files))
                if(file.path().extension() == ".mst" || file.path().extension() == ".ost")
                    filenames.push_back(file.path().filename().string());
        
        return filenames;
    }
    
    static vector<string> GetDirectoryFolderNames(const string& path)
    {
        vector<string> folderNames;
        filesystem::path folders {path};
        
        if (std::filesystem::exists(folders) && std::filesystem::is_directory(folders))
            for (auto& folder : std::filesystem::directory_iterator(folders))
                if(folder.is_directory())
                    folderNames.push_back(folder.path().filename().string());
        
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
    string remoteDeviceIP = "";
};

vector<shared_ptr<SurfaceLine>> surfaces;

struct PageSurfaceLine
{
    string pageSurfaceName = "";
    int numChannels = 0;
    int channelOffset = 0;
    bool useLocalmodifiers = false;
    string templateFilename = "";
    string zoneTemplateFolder = "";
    string fxZoneTemplateFolder = "";
};

struct PageLine
{
    string name = "";
    bool followMCP = true;
    bool synchPages = true;
    bool isScrollLinkEnabled = false;
    bool scrollSynch = false;
    vector<shared_ptr<PageSurfaceLine>> surfaces;
};

// Scratch pad to get in and out of dialogs easily
static bool editMode = false;

static string type = "";
static char name[BUFSZ];
static int inPort = 0;
static int outPort = 0;
static char remoteDeviceIP[BUFSZ];

static int pageIndex = 0;
static bool followMCP = false;
static bool synchPages = true;
static bool isScrollLinkEnabled = false;
static bool scrollSynch = false;

static char pageSurfaceName[BUFSZ];
static int numChannels = 0;
static int channelOffset = 0;
static bool useLocalmodifiers = false;
static char templateFilename[BUFSZ];
static char zoneTemplateFolder[BUFSZ];
static char fxZoneTemplateFolder[BUFSZ];

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
                
                CheckDlgButton(hwndDlg, IDC_RADIO_TCP, ! followMCP);
                CheckDlgButton(hwndDlg, IDC_RADIO_MCP, followMCP);

                CheckDlgButton(hwndDlg, IDC_CHECK_SynchPages, synchPages);
                CheckDlgButton(hwndDlg, IDC_CHECK_ScrollLink, isScrollLinkEnabled);
                CheckDlgButton(hwndDlg, IDC_CHECK_ScrollSynch, scrollSynch);
            }
        }
            break;
            
        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case IDOK:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        GetDlgItemText(hwndDlg, IDC_EDIT_PageName , name, sizeof(name));
                        
                        if(IsDlgButtonChecked(hwndDlg, IDC_RADIO_TCP))
                           followMCP = false;
                        else if(IsDlgButtonChecked(hwndDlg, IDC_RADIO_MCP))
                           followMCP = true;
                        
                        synchPages = IsDlgButtonChecked(hwndDlg, IDC_CHECK_SynchPages);
                        isScrollLinkEnabled = IsDlgButtonChecked(hwndDlg, IDC_CHECK_ScrollLink);
                        scrollSynch = IsDlgButtonChecked(hwndDlg, IDC_CHECK_ScrollSynch);
                        
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
    
    return 0;
}

static void PopulateSurfaceTemplateCombo(HWND hwndDlg, string resourcePath)
{
    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_RESETCONTENT, 0, 0);
    
    char buf[BUFSZ];
    
    GetDlgItemText(hwndDlg, IDC_COMBO_PageSurface, buf, sizeof(buf));
    
    for(auto surface : surfaces)
    {
        if(surface->name == string(buf))
        {
            if(surface->type == MidiSurfaceToken)
                for(auto filename : FileSystem::GetDirectoryFilenames(resourcePath + "/CSI/Surfaces/Midi/"))
                    AddComboEntry(hwndDlg, 0, (char*)filename.c_str(), IDC_COMBO_SurfaceTemplate);

            if(surface->type == OSCSurfaceToken)
                for(auto filename : FileSystem::GetDirectoryFilenames(resourcePath + "/CSI/Surfaces/OSC/"))
                    AddComboEntry(hwndDlg, 0, (char*)filename.c_str(), IDC_COMBO_SurfaceTemplate);
            
            break;
        }
    }
}

static WDL_DLGRET dlgProcPageSurface(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    string resourcePath(DAW::GetResourcePath());

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            if(editMode)
            {
                AddComboEntry(hwndDlg, 0, (char *)pageSurfaceName, IDC_COMBO_PageSurface);
                SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_PageSurface), CB_SETCURSEL, 0, 0);

                SetDlgItemText(hwndDlg, IDC_EDIT_NumChannels, to_string(numChannels).c_str());
                SetDlgItemText(hwndDlg, IDC_EDIT_ChannelOffset, to_string(channelOffset).c_str());

                CheckDlgButton(hwndDlg, IDC_CHECK_LocalModifiers, useLocalmodifiers);
                
                PopulateSurfaceTemplateCombo(hwndDlg, resourcePath);
                
                for(auto foldername : FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/"))
                    AddComboEntry(hwndDlg, 0, (char *)foldername.c_str(), IDC_COMBO_ZoneTemplates);

                for(auto foldername : FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/"))
                    AddComboEntry(hwndDlg, 0, (char *)foldername.c_str(), IDC_COMBO_FXZoneTemplates);

                int index = SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_FINDSTRINGEXACT, -1, (LPARAM)templateFilename);
                if(index >= 0)
                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_SETCURSEL, index, 0);
                
                index = SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)zoneTemplateFolder);
                if(index >= 0)
                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_SETCURSEL, index, 0);
                
                index = SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_FXZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)fxZoneTemplateFolder);
                if(index >= 0)
                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_FXZoneTemplates), CB_SETCURSEL, index, 0);
            }
            else
            {
                for(auto surface : surfaces)
                    AddComboEntry(hwndDlg, 0, (char *)surface->name.c_str(), IDC_COMBO_PageSurface);
                
                SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_PageSurface), CB_SETCURSEL, 0, 0);
                
                SetDlgItemText(hwndDlg, IDC_EDIT_NumChannels, "0");
                SetDlgItemText(hwndDlg, IDC_EDIT_ChannelOffset, "0");
                
                CheckDlgButton(hwndDlg, IDC_CHECK_LocalModifiers, useLocalmodifiers);
                
                PopulateSurfaceTemplateCombo(hwndDlg, resourcePath);
                            
                for(auto foldername : FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/"))
                    AddComboEntry(hwndDlg, 0, (char *)foldername.c_str(), IDC_COMBO_ZoneTemplates);
                
                for(auto foldername : FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/"))
                    AddComboEntry(hwndDlg, 0, (char *)foldername.c_str(), IDC_COMBO_FXZoneTemplates);
            }
        }
            break;
            
        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case IDC_COMBO_PageSurface:
                {
                    switch (HIWORD(wParam))
                    {
                        case CBN_SELCHANGE:
                        {
                            PopulateSurfaceTemplateCombo(hwndDlg, resourcePath);
                        }
                    }
                    
                    break;
                }

                case IDC_COMBO_SurfaceTemplate:
                {
                    switch (HIWORD(wParam))
                    {
                        case CBN_SELCHANGE:
                        {
                            int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_GETCURSEL, 0, 0);
                            if(index >= 0 && ! editMode)
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
                }
                    
                case IDC_COMBO_ZoneTemplates:
                {
                    switch (HIWORD(wParam))
                    {
                        case CBN_SELCHANGE:
                        {
                            int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_GETCURSEL, 0, 0);
                            if(index >= 0 && ! editMode)
                            {
                                char buffer[BUFSZ];
                                
                                GetDlgItemText(hwndDlg, IDC_COMBO_ZoneTemplates, buffer, sizeof(buffer));
                                
                                int index = SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_FXZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)buffer);
                                if(index >= 0)
                                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_FXZoneTemplates), CB_SETCURSEL, index, 0);
                            }
                        }
                    }
                    
                    break;
                }

                case IDOK:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        GetDlgItemText(hwndDlg, IDC_COMBO_PageSurface, pageSurfaceName, sizeof(pageSurfaceName));

                        char buf[BUFSZ];
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_NumChannels, buf, sizeof(buf));
                        numChannels = atoi(buf);
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_ChannelOffset, buf, sizeof(buf));
                        channelOffset = atoi(buf);
                        
                        useLocalmodifiers = IsDlgButtonChecked(hwndDlg, IDC_CHECK_LocalModifiers);
                        
                        GetDlgItemText(hwndDlg, IDC_COMBO_SurfaceTemplate, templateFilename, sizeof(templateFilename));
                        GetDlgItemText(hwndDlg, IDC_COMBO_ZoneTemplates, zoneTemplateFolder, sizeof(zoneTemplateFolder));
                        GetDlgItemText(hwndDlg, IDC_COMBO_FXZoneTemplates, fxZoneTemplateFolder, sizeof(fxZoneTemplateFolder));

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
    
    return 0;
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
            
            if(editMode)
            {
                SetDlgItemText(hwndDlg, IDC_EDIT_MidiSurfaceName, name);
            }
            else
            {
                SetDlgItemText(hwndDlg, IDC_EDIT_MidiSurfaceName, "");
                SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_MidiIn), CB_SETCURSEL, 0, 0);
                SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_MidiOut), CB_SETCURSEL, 0, 0);
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
                        GetDlgItemText(hwndDlg, IDC_EDIT_MidiSurfaceName, name, sizeof(name));
                        
                        int currentSelection = SendDlgItemMessage(hwndDlg, IDC_COMBO_MidiIn, CB_GETCURSEL, 0, 0);
                        if (currentSelection >= 0)
                            inPort = SendDlgItemMessage(hwndDlg, IDC_COMBO_MidiIn, CB_GETITEMDATA, currentSelection, 0);
                        currentSelection = SendDlgItemMessage(hwndDlg, IDC_COMBO_MidiOut, CB_GETCURSEL, 0, 0);
                        if (currentSelection >= 0)
                            outPort = SendDlgItemMessage(hwndDlg, IDC_COMBO_MidiOut, CB_GETITEMDATA, currentSelection, 0);
                        
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
    
    return 0;
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
                AddComboEntry(hwndDlg, i++, (char*)filename.c_str(), IDC_COMBO_SurfaceTemplate);
            
            for(auto foldername : FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/"))
                AddComboEntry(hwndDlg, 0, (char *)foldername.c_str(), IDC_COMBO_ZoneTemplates);
            
            if(editMode)
            {
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCSurfaceName, name);
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCRemoteDeviceIP, remoteDeviceIP);
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCInPort, to_string(inPort).c_str());
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCOutPort, to_string(outPort).c_str());
            }
            else
            {
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCSurfaceName, "");
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCRemoteDeviceIP, "");
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCInPort, "");
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCOutPort, "");
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
                                               
                        GetDlgItemText(hwndDlg, IDC_EDIT_OSCSurfaceName, name, sizeof(name));
                        GetDlgItemText(hwndDlg, IDC_EDIT_OSCRemoteDeviceIP, remoteDeviceIP, sizeof(remoteDeviceIP));
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_OSCInPort, buf, sizeof(buf));
                        inPort = atoi(buf);
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_OSCOutPort, buf, sizeof(buf));
                        outPort = atoi(buf);
                        
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
    
    return 0;
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
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_RESETCONTENT, 0, 0);

                                pageIndex = index;

                                for (auto surface : pages[index]->surfaces)
                                    AddListEntry(hwndDlg, surface->pageSurfaceName, IDC_LIST_PageSurfaces);
                                
                                if(pages[index]->surfaces.size() > 0)
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_SETCURSEL, 0, 0);
                            }
                            else
                            {
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_RESETCONTENT, 0, 0);
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
                        
                    case IDC_LIST_PageSurfaces:
                        if (HIWORD(wParam) == LBN_DBLCLK)
                        {
#ifdef WIN32
                            // pretend we clicked the Edit button
                            SendMessage(GetDlgItem(hwndDlg, IDC_BUTTON_EditPageSurface), BM_CLICK, 0, 0);
#endif
                        }
                        break;
                        
                    case IDC_BUTTON_AddMidiSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                            if (index >= 0)
                            {
                                dlgResult = false;
                                editMode = false;
                                DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_MidiSurface), hwndDlg, dlgProcMidiSurface);
                                if(dlgResult == IDOK)
                                {
                                    shared_ptr<SurfaceLine> surface = make_shared<SurfaceLine>();
                                    surface->type = MidiSurfaceToken;
                                    surface->name = name;
                                    surface->inPort = inPort;
                                    surface->outPort = outPort;

                                    surfaces.push_back(surface);
                                    
                                    AddListEntry(hwndDlg, name, IDC_LIST_Surfaces);
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL, surfaces.size() - 1, 0);
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
                                editMode = false;
                                DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_OSCSurface), hwndDlg, dlgProcOSCSurface);
                                if(dlgResult == IDOK)
                                {
                                    shared_ptr<SurfaceLine> surface = make_shared<SurfaceLine>();
                                    surface->type = OSCSurfaceToken;
                                    surface->name = name;
                                    surface->remoteDeviceIP = remoteDeviceIP;
                                    surface->inPort = inPort;
                                    surface->outPort = outPort;

                                    surfaces.push_back(surface);
                                    
                                    AddListEntry(hwndDlg, name, IDC_LIST_Surfaces);
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL, surfaces.size() - 1, 0);
                                }
                            }
                        }
                        break ;
                     
                    case IDC_BUTTON_AddPage:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            dlgResult = false;
                            editMode = false;
                            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_Page), hwndDlg, dlgProcPage);
                            if(dlgResult == IDOK)
                            {
                                shared_ptr<PageLine> page = make_shared<PageLine>();
                                page->name = name;
                                page->followMCP = followMCP;
                                page->synchPages = synchPages;
                                page->isScrollLinkEnabled = isScrollLinkEnabled;
                                page->scrollSynch = scrollSynch;

                                pages.push_back(page);
                                AddListEntry(hwndDlg, name, IDC_LIST_Pages);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_SETCURSEL, pages.size() - 1, 0);
                            }
                        }
                        break ;

                    case IDC_BUTTON_AddPageSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            dlgResult = false;
                            editMode = false;
                            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_PageSurface), hwndDlg, dlgProcPageSurface);
                            if(dlgResult == IDOK)
                            {
                                shared_ptr<PageSurfaceLine> pageSurface = make_shared<PageSurfaceLine>();
                                pageSurface->pageSurfaceName = pageSurfaceName;
                                pageSurface->numChannels = numChannels;
                                pageSurface->channelOffset = channelOffset;
                                pageSurface->useLocalmodifiers = useLocalmodifiers;
                                pageSurface->templateFilename = templateFilename;
                                pageSurface->zoneTemplateFolder = zoneTemplateFolder;
                                pageSurface->fxZoneTemplateFolder = fxZoneTemplateFolder;

                                int index = SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                                if (index >= 0)
                                {
                                    pages[index]->surfaces.push_back(pageSurface);
                                    AddListEntry(hwndDlg, pageSurfaceName, IDC_LIST_PageSurfaces);
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_SETCURSEL, pages[index]->surfaces.size() - 1, 0);
                                }
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
                                inPort = surfaces[index]->inPort;
                                outPort = surfaces[index]->outPort;
                                strcpy(remoteDeviceIP, surfaces[index]->remoteDeviceIP.c_str());

                                dlgResult = false;
                                editMode = true;
                                
                                if(surfaces[index]->type == MidiSurfaceToken)
                                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_MidiSurface), hwndDlg, dlgProcMidiSurface);
                                else if(surfaces[index]->type == OSCSurfaceToken)
                                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_OSCSurface), hwndDlg, dlgProcOSCSurface);
                                                               
                                if(dlgResult == IDOK)
                                {
                                    surfaces[index]->name = name;
                                    surfaces[index]->remoteDeviceIP = remoteDeviceIP;
                                    surfaces[index]->inPort = inPort;
                                    surfaces[index]->outPort = outPort;
                                }
                                
                                editMode = false;
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

                                dlgResult = false;
                                editMode = true;
                                
                                followMCP = pages[index]->followMCP;
                                synchPages = pages[index]->synchPages;
                                isScrollLinkEnabled = pages[index]->isScrollLinkEnabled;
                                scrollSynch = pages[index]->scrollSynch;
                                
                                DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_Page), hwndDlg, dlgProcPage);
                                if(dlgResult == IDOK)
                                {
                                    pages[index]->name = name;
                                    pages[index]->followMCP = followMCP;
                                    pages[index]->synchPages = synchPages;
                                    pages[index]->isScrollLinkEnabled = isScrollLinkEnabled;
                                    pages[index]->scrollSynch = scrollSynch;

                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_RESETCONTENT, 0, 0);
                                    for(auto page :  pages)
                                        AddListEntry(hwndDlg, page->name, IDC_LIST_Pages);
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_SETCURSEL, index, 0);
                                }
                                
                                editMode = false;
                            }
                        }
                        break ;
                        
                    case IDC_BUTTON_EditPageSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = SendDlgItemMessage(hwndDlg, IDC_LIST_PageSurfaces, LB_GETCURSEL, 0, 0);
                            if(index >= 0)
                            {
                                dlgResult = false;
                                editMode = true;
                                
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_GETTEXT, index, (LPARAM)(LPCTSTR)(pageSurfaceName));

                                int pageIndex = SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                                if(pageIndex >= 0)
                                {
                                    numChannels = pages[pageIndex]->surfaces[index]->numChannels;
                                    channelOffset = pages[pageIndex]->surfaces[index]->channelOffset;
                                    useLocalmodifiers = pages[pageIndex]->surfaces[index]->useLocalmodifiers;
                                    
                                    strcpy(templateFilename, pages[pageIndex]->surfaces[index]->templateFilename.c_str());
                                    strcpy(zoneTemplateFolder, pages[pageIndex]->surfaces[index]->zoneTemplateFolder.c_str());
                                    strcpy(fxZoneTemplateFolder, pages[pageIndex]->surfaces[index]->fxZoneTemplateFolder.c_str());

                                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_PageSurface), hwndDlg, dlgProcPageSurface);
                                    
                                    if(dlgResult == IDOK)
                                    {
                                        pages[pageIndex]->surfaces[index]->numChannels = numChannels;
                                        pages[pageIndex]->surfaces[index]->channelOffset = channelOffset;
                                        pages[pageIndex]->surfaces[index]->useLocalmodifiers = useLocalmodifiers;
                                        pages[pageIndex]->surfaces[index]->templateFilename = templateFilename;
                                        pages[pageIndex]->surfaces[index]->zoneTemplateFolder = zoneTemplateFolder;
                                        pages[pageIndex]->surfaces[index]->fxZoneTemplateFolder = fxZoneTemplateFolder;
                                    }
                                }
                                
                                editMode = false;
                            }
                        }
                        break ;
                        

                    case IDC_BUTTON_RemoveSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = SendDlgItemMessage(hwndDlg, IDC_LIST_Surfaces, LB_GETCURSEL, 0, 0);
                            if(index >= 0)
                            {
                                surfaces.erase(surfaces.begin() + index);
                                
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_RESETCONTENT, 0, 0);
                                for(auto surface: surfaces)
                                    AddListEntry(hwndDlg, surface->name, IDC_LIST_Surfaces);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL, index, 0);
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
#ifdef WIN32
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_RESETCONTENT, 0, 0);
#endif
                                for(auto page: pages)
                                    AddListEntry(hwndDlg, page->name, IDC_LIST_Pages);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_SETCURSEL, index, 0);
                            }
                        }
                        break ;

                    case IDC_BUTTON_RemovePageSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int pageIndex = SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                            if(pageIndex >= 0)
                            {
                                int index = SendDlgItemMessage(hwndDlg, IDC_LIST_PageSurfaces, LB_GETCURSEL, 0, 0);
                                if(index >= 0)
                                {
                                    pages[pageIndex]->surfaces.erase(pages[pageIndex]->surfaces.begin() + index);
                                    
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_RESETCONTENT, 0, 0);

                                    for(auto surface : pages[pageIndex]->surfaces)
                                        AddListEntry(hwndDlg, surface->pageSurfaceName, IDC_LIST_PageSurfaces);
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_SETCURSEL, index, 0);
                                }
                            }
                        }
                        break ;
                }
            }
            break ;
            
        case WM_INITDIALOG:
        {
            surfaces.clear();
            pages.clear();
            
            ifstream iniFile(string(DAW::GetResourcePath()) + "/CSI/CSI.ini");
            
            int lineNumber = 0;
            
            for (string line; getline(iniFile, line) ; )
            {
                line = regex_replace(line, regex(TabChars), " ");
                line = regex_replace(line, regex(CRLFChars), "");
             
                lineNumber++;
                
                if(lineNumber == 1)
                {
                    if(line != VersionToken)
                    {
                        MessageBox(g_hwnd, ("Version mismatch -- Your CSI.ini file is not " + VersionToken).c_str(), ("This is CSI " + VersionToken).c_str(), MB_OK);
                        iniFile.close();
                        break;
                    }
                    else
                        continue;
                }
                
                if(line[0] != '\r' && line[0] != '/' && line != "") // ignore comment lines and blank lines
                {
                    istringstream iss(line);
                    vector<string> tokens;
                    string token;
                    
                    while (iss >> quoted(token))
                        tokens.push_back(token);
                    
                    if(tokens[0] == MidiSurfaceToken || tokens[0] == OSCSurfaceToken)
                    {
                        shared_ptr<SurfaceLine> surface = make_shared<SurfaceLine>();
                        
                        surface->type = tokens[0];
                        surface->name = tokens[1];
                        
                        if((surface->type == MidiSurfaceToken || surface->type == OSCSurfaceToken) && (tokens.size() == 4 || tokens.size() == 5))
                        {
                            surface->inPort = atoi(tokens[2].c_str());
                            surface->outPort = atoi(tokens[3].c_str());
                            if(surface->type == OSCSurfaceToken && tokens.size() == 5)
                                surface->remoteDeviceIP = tokens[4];
                        }
                        
                        surfaces.push_back(surface);
                        
                        AddListEntry(hwndDlg, surface->name, IDC_LIST_Surfaces);
                    }
                    else if(tokens[0] == PageToken && tokens.size() > 1)
                    {
                        bool followMCP = true;
                        bool synchPages = true;
                        bool isScrollLinkEnabled = false;
                        bool scrollSynch = false;

                        for(int i = 2; i < tokens.size(); i++)
                        {
                            if(tokens[i] == "FollowTCP")
                                followMCP = false;
                            else if(tokens[i] == "NoSynchPages")
                                synchPages = false;
                            else if(tokens[i] == "UseScrollLink")
                                isScrollLinkEnabled = true;
                            else if(tokens[i] == "UseScrollSynch")
                                scrollSynch = true;
                        }

                        shared_ptr<PageLine> page = make_shared<PageLine>();
                        page->name = tokens[1];
                        page->followMCP = followMCP;
                        page->synchPages = synchPages;
                        page->isScrollLinkEnabled = isScrollLinkEnabled;
                        page->scrollSynch = scrollSynch;
 
                        pages.push_back(page);
                        
                        AddListEntry(hwndDlg, page->name, IDC_LIST_Pages);
                    }
                    else if(tokens.size() == 6 || tokens.size() == 7)
                    {
                        bool useLocalModifiers = false;
                        
                        if(tokens[0] == "LocalModifiers")
                        {
                            useLocalModifiers = true;
                            tokens.erase(tokens.begin()); // pop front
                        }

                        shared_ptr<PageSurfaceLine> surface = make_shared<PageSurfaceLine>();
                        
                        if (! pages.empty())
                        {
                            pages.back()->surfaces.push_back(surface);
                            
                            surface->useLocalmodifiers = useLocalModifiers;
                            surface->pageSurfaceName = tokens[0];
                            surface->numChannels = atoi(tokens[1].c_str());
                            surface->channelOffset = atoi(tokens[2].c_str());
                            surface->templateFilename = tokens[3];
                            surface->zoneTemplateFolder = tokens[4];
                            surface->fxZoneTemplateFolder = tokens[5];
                        }
                    }
                }
            }
          
            if(surfaces.size() > 0)
                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL, 0, 0);
            
            if(pages.size() > 0)
            {
                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_SETCURSEL, 0, 0);

                // the messages above don't trigger the user-initiated code, so pretend the user selected them
                SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_LIST_Pages, LBN_SELCHANGE), 0);
                
                if(pages[0]->surfaces.size() > 0)
                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_SETCURSEL, 0, 0);
            }
        }
        break;
        
        case WM_USER+1024:
        {
            ofstream iniFile(string(DAW::GetResourcePath()) + "/CSI/CSI.ini");

            if(iniFile.is_open())
            {
                iniFile << VersionToken + GetLineEnding();
                
                iniFile << GetLineEnding();
                
                string line = "";
                
                for(auto surface : surfaces)
                {
                    line = surface->type + " ";
                    line += "\"" + surface->name + "\" ";
                    line += to_string(surface->inPort) + " ";
                    line += to_string(surface->outPort) + " ";

                    if(surface->type == OSCSurfaceToken)
                        line += surface->remoteDeviceIP;
                    
                    iniFile << line + GetLineEnding();
                }
                
                iniFile << GetLineEnding();
                
                for(auto page : pages)
                {
                    line = PageToken + " ";
                    line += "\"" + page->name + "\"";
                    
                    if(page->followMCP == false)
                        line += " FollowTCP";
                                        
                    if(page->synchPages == false)
                        line += " NoSynchPages";
                    
                    if(page->isScrollLinkEnabled == true)
                        line += " UseScrollLink";
                    
                    if(page->scrollSynch == true)
                        line += " UseScrollSynch";

                    line += GetLineEnding();
                    
                    iniFile << line;

                    for(auto surface : page->surfaces)
                    {
                        line = "";
                        if(surface->useLocalmodifiers)
                            line += "\"LocalModifiers\" ";
                        line += "\"" + surface->pageSurfaceName + "\" ";
                        line += to_string(surface->numChannels) + " " ;
                        line += to_string(surface->channelOffset) + " " ;
                        line += "\"" + surface->templateFilename + "\" ";
                        line += "\"" + surface->zoneTemplateFolder + "\" ";
                        line += "\"" + surface->fxZoneTemplateFolder + "\" ";

                        iniFile << line + GetLineEnding();
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
