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
extern void TrimLine(string &line);

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
static shared_ptr<ZoneManager> s_zoneManager;
static vector<vector<string>> s_surfaceLayoutTemplate;
static int s_numGroups = 0;
static AutoZoneDefinition s_zoneDef;
static vector<FXParamLayoutTemplate> s_layoutTemplates;

static int s_dlgResult = IDCANCEL;

static int s_fxListIndex = 0;
static int s_groupIndex = 0;

static WDL_DLGRET dlgProcEditAdvanced(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            SetWindowText(hwndDlg, ("Advanced Edit Group " + to_string(s_groupIndex + 1)).c_str());

            s_dlgResult = IDCANCEL;
               
            if(s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].delta != "")
                SetDlgItemText(hwndDlg, IDC_EDIT_Delta, s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].delta.c_str());

            if(s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].rangeMinimum != "")
                SetDlgItemText(hwndDlg, IDC_EDIT_RangeMin, s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].rangeMinimum.c_str());

            if(s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].rangeMaximum != "")
                SetDlgItemText(hwndDlg, IDC_EDIT_RangeMax, s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].rangeMaximum.c_str());

            if(s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].deltas.size() > 0)
            {
                string deltas = "";
                
                for(auto delta : s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].deltas)
                    deltas += delta + " ";
                
                SetDlgItemText(hwndDlg, IDC_EDIT_DeltaValues, deltas.c_str());
            }

            if(s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].ticks.size() > 0)
            {
                string ticks = "";
                
                for(auto tick : s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].ticks)
                    ticks += tick + " ";
                
                SetDlgItemText(hwndDlg, IDC_EDIT_TickValues, ticks.c_str());
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
                        char buf[BUFSZ];
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_Delta, buf, sizeof(buf));
                        if(string(buf) != "")
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].delta = buf;

                        GetDlgItemText(hwndDlg, IDC_EDIT_RangeMin, buf, sizeof(buf));
                        if(string(buf) != "")
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].rangeMinimum = buf;

                        GetDlgItemText(hwndDlg, IDC_EDIT_RangeMax, buf, sizeof(buf));
                        if(string(buf) != "")
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].rangeMaximum = buf;

                        GetDlgItemText(hwndDlg, IDC_EDIT_DeltaValues, buf, sizeof(buf));
                        if(string(buf) != "")
                        {
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].deltas.clear();
                            vector<string> deltas;
                            GetTokens(deltas, buf);
                            for(auto delta : deltas)
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].deltas.push_back(delta);
                        }

                        GetDlgItemText(hwndDlg, IDC_EDIT_TickValues, buf, sizeof(buf));
                        if(string(buf) != "")
                        {
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].ticks.clear();
                            vector<string> ticks;
                            GetTokens(ticks, buf);
                            for(auto tick : ticks)
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].ticks.push_back(tick);
                        }

                        s_dlgResult = IDOK;
                        EndDialog(hwndDlg, 0);
                    }
                    break ;
                    
                case IDCANCEL:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        s_dlgResult = IDCANCEL;
                        EndDialog(hwndDlg, 0);
                    }
                    break ;
            }
        }
    }
    
    return 0;
}

static vector<int> s_paramNumEditControls = { IDC_FXParamNumEdit1, IDC_FXParamNumEdit2, IDC_FXParamNumEdit3 };
static vector<int> s_widgetTypePickers = { IDC_PickWidgetType1, IDC_PickWidgetType2, IDC_PickWidgetType3 };
static vector<int> s_ringStylePickers = { IDC_PickRingStyle1, IDC_PickRingStyle2, IDC_PickRingStyle3 };
static vector<int> s_fixedTextEditControls = { IDC_FXParamNameEdit1, IDC_FXParamNameEdit2, IDC_FXParamNameEdit3 };
static vector<int> s_fixedTextDisplayRowPickers = { IDC_FixedTextDisplayPickRow1 , IDC_FixedTextDisplayPickRow2, IDC_FixedTextDisplayPickRow3 };
static vector<int> s_fixedTextDisplayFontLabels = { IDC_FixedTextDisplayFontLabel1, IDC_FixedTextDisplayFontLabel2, IDC_FixedTextDisplayFontLabel3 };
static vector<int> s_fixedTextDisplayFontPickers = { IDC_FixedTextDisplayPickFont1, IDC_FixedTextDisplayPickFont2, IDC_FixedTextDisplayPickFont3 };
static vector<int> s_paramValueDisplayRowPickers = { IDC_FXParamValueDisplayPickRow1 , IDC_FXParamValueDisplayPickRow2, IDC_FXParamValueDisplayPickRow3 };
static vector<int> s_paramValueDisplayFontLabels = { IDC_FXParamValueDisplayFontLabel1, IDC_FXParamValueDisplayFontLabel2, IDC_FXParamValueDisplayFontLabel3 };
static vector<int> s_paramValueDisplayFontPickers = { IDC_FXParamValueDisplayPickFont1, IDC_FXParamValueDisplayPickFont2, IDC_FXParamValueDisplayPickFont3 };

static vector<int> s_stepPickers = { IDC_PickSteps1, IDC_PickSteps2, IDC_PickSteps3 };
static vector<int> s_stepEditControls = { IDC_EditSteps1, IDC_EditSteps2, IDC_EditSteps3 };
static vector<int> s_stepPrompts = { IDC_StepsPromptGroup1, IDC_StepsPromptGroup2, IDC_StepsPromptGroup3 };

static vector<int> s_widgetRingColorBoxes = { IDC_FXParamRingColorBox1, IDC_FXParamRingColorBox2, IDC_FXParamRingColorBox3 };
static vector<int> s_widgetRingColors = { IDC_FXParamRingColor1, IDC_FXParamRingColor2, IDC_FXParamRingColor3 };
static vector<int> s_widgetRingIndicatorColorBoxes = { IDC_FXParamIndicatorColorBox1, IDC_FXParamIndicatorColorBox2, IDC_FXParamIndicatorColorBox3 };
static vector<int> s_widgetRingIndicators = { IDC_FXParamIndicatorColor1, IDC_FXParamIndicatorColor2, IDC_FXParamIndicatorColor3 };
static vector<int> s_fixedTextDisplayForegroundColors = { IDC_FixedTextDisplayForegroundColor1, IDC_FixedTextDisplayForegroundColor2, IDC_FixedTextDisplayForegroundColor3 };
static vector<int> s_fixedTextDisplayForegroundColorBoxes = { IDC_FXFixedTextDisplayForegroundColorBox1, IDC_FXFixedTextDisplayForegroundColorBox2, IDC_FXFixedTextDisplayForegroundColorBox3 };
static vector<int> s_fixedTextDisplayBackgroundColors = { IDC_FixedTextDisplayBackgroundColor1, IDC_FixedTextDisplayBackgroundColor2, IDC_FixedTextDisplayBackgroundColor3 };
static vector<int> s_fixedTextDisplayBackgroundColorBoxes = { IDC_FXFixedTextDisplayBackgroundColorBox1, IDC_FXFixedTextDisplayBackgroundColorBox2, IDC_FXFixedTextDisplayBackgroundColorBox3 };
static vector<int> s_fxParamDisplayForegroundColors = { IDC_FXParamDisplayForegroundColor1, IDC_FXParamDisplayForegroundColor2, IDC_FXParamDisplayForegroundColor3 };
static vector<int> s_fxParamDisplayForegroundColorBoxes = { IDC_FXParamValueDisplayForegroundColorBox1, IDC_FXParamValueDisplayForegroundColorBox2, IDC_FXParamValueDisplayForegroundColorBox3 };
static vector<int> s_fxParamDisplayBackgroundColors = { IDC_FXParamDisplayBackgroundColor1, IDC_FXParamDisplayBackgroundColor2, IDC_FXParamDisplayBackgroundColor3 };
static vector<int> s_fxParamDisplayBackgroundColorBoxes = { IDC_FXParamValueDisplayBackgroundColorBox1, IDC_FXParamValueDisplayBackgroundColorBox2, IDC_FXParamValueDisplayBackgroundColorBox3 };


// for show / hide
static vector<int> s_groupBoxes = { IDC_Group1, IDC_Group2, IDC_Group3 };
static vector<int> s_fxParamGroupBoxes = { IDC_GroupFXParam1, IDC_GroupFXParam2, IDC_GroupFXParam3 };
static vector<int> s_fixedTextDisplayGroupBoxes = { IDC_GroupFixedTextDisplay1 , IDC_GroupFixedTextDisplay2, IDC_GroupFixedTextDisplay3 };
static vector<int> s_fxParamDisplayGroupBoxes = { IDC_GroupFXParamValueDisplay1 , IDC_GroupFXParamValueDisplay2, IDC_GroupFXParamValueDisplay3 };
static vector<int> s_advancedButtons = { IDC_AdvancedGroup1 , IDC_AdvancedGroup2, IDC_AdvancedGroup3 };

static vector<vector<int>> s_baseControls =
{
    s_paramNumEditControls,
    s_widgetTypePickers,
    s_ringStylePickers,
    
    s_fixedTextEditControls,
    s_fixedTextDisplayRowPickers,
    
    s_paramValueDisplayRowPickers,
    
    s_stepPickers,
    s_stepEditControls,
    s_stepPrompts,
    
    s_groupBoxes,
    s_fxParamGroupBoxes,
    s_fixedTextDisplayGroupBoxes,
    s_fxParamDisplayGroupBoxes,
    s_advancedButtons
};

static vector<vector<int>> s_fontControls =
{
    s_fixedTextDisplayFontLabels,
    s_fixedTextDisplayFontPickers,
    s_paramValueDisplayFontLabels,
    s_paramValueDisplayFontPickers
};

static vector<vector<int>> s_colorControls =
{
    s_widgetRingColorBoxes,
    s_widgetRingColors,
    s_widgetRingIndicatorColorBoxes,
    s_widgetRingIndicators,
    s_fixedTextDisplayForegroundColors,
    s_fixedTextDisplayForegroundColorBoxes,
    s_fixedTextDisplayBackgroundColors,
    s_fixedTextDisplayBackgroundColorBoxes,
    s_fxParamDisplayForegroundColors,
    s_fxParamDisplayForegroundColorBoxes,
    s_fxParamDisplayBackgroundColors,
    s_fxParamDisplayBackgroundColorBoxes,
};

static void ShowBaseControls(HWND hwndDlg, int startIndex, int endIndex, bool show)
{
    for(auto controls : s_baseControls)
        for(int i = startIndex; i < endIndex; i++)
            ShowWindow(GetDlgItem(hwndDlg, controls[i]), show);
}

static void ShowFontControls(HWND hwndDlg, int startIndex, int endIndex, bool show)
{
    for(auto controls : s_fontControls)
        for(int i = startIndex; i < endIndex; i++)
            ShowWindow(GetDlgItem(hwndDlg, controls[i]), show);
}

static void ShowColorControls(HWND hwndDlg, int startIndex, int endIndex, bool show)
{
    for(auto controls : s_colorControls)
        for(int i = startIndex; i < endIndex; i++)
            ShowWindow(GetDlgItem(hwndDlg, controls[i]), show);
}

static map<int, int> s_buttonColors =
{
    { IDC_FXParamRingColor1, 0xffffffff },
    { IDC_FXParamRingColor2, 0xffffffff },
    { IDC_FXParamRingColor3, 0xffffffff },
    { IDC_FXParamIndicatorColor1, 0xffffffff },
    { IDC_FXParamIndicatorColor2, 0xffffffff },
    { IDC_FXParamIndicatorColor3, 0xffffffff },
    { IDC_FixedTextDisplayForegroundColor1, 0xffffffff },
    { IDC_FixedTextDisplayForegroundColor2, 0xffffffff },
    { IDC_FixedTextDisplayForegroundColor3, 0xffffffff },
    { IDC_FixedTextDisplayBackgroundColor1, 0xffffffff },
    { IDC_FixedTextDisplayBackgroundColor2, 0xffffffff },
    { IDC_FixedTextDisplayBackgroundColor3, 0xffffffff },
    { IDC_FXParamDisplayForegroundColor1, 0xffffffff },
    { IDC_FXParamDisplayForegroundColor2, 0xffffffff },
    { IDC_FXParamDisplayForegroundColor3, 0xffffffff },
    { IDC_FXParamDisplayBackgroundColor1, 0xffffffff },
    { IDC_FXParamDisplayBackgroundColor2, 0xffffffff },
    { IDC_FXParamDisplayBackgroundColor3, 0xffffffff }
};

static map<int, int> s_buttonColorBoxes =
{
    { IDC_FXParamRingColor1, IDC_FXParamRingColorBox1 },
    { IDC_FXParamRingColor2, IDC_FXParamRingColorBox2 },
    { IDC_FXParamRingColor3, IDC_FXParamRingColorBox3 },
    { IDC_FXParamIndicatorColor1, IDC_FXParamIndicatorColorBox1 },
    { IDC_FXParamIndicatorColor2, IDC_FXParamIndicatorColorBox2 },
    { IDC_FXParamIndicatorColor3, IDC_FXParamIndicatorColorBox3 },
    { IDC_FixedTextDisplayForegroundColor1, IDC_FXFixedTextDisplayForegroundColorBox1 },
    { IDC_FixedTextDisplayForegroundColor2, IDC_FXFixedTextDisplayForegroundColorBox2 },
    { IDC_FixedTextDisplayForegroundColor3, IDC_FXFixedTextDisplayForegroundColorBox3 },
    { IDC_FixedTextDisplayBackgroundColor1, IDC_FXFixedTextDisplayBackgroundColorBox1 },
    { IDC_FixedTextDisplayBackgroundColor2, IDC_FXFixedTextDisplayBackgroundColorBox2 },
    { IDC_FixedTextDisplayBackgroundColor3, IDC_FXFixedTextDisplayBackgroundColorBox3 },
    { IDC_FXParamDisplayForegroundColor1, IDC_FXParamValueDisplayForegroundColorBox1 },
    { IDC_FXParamDisplayForegroundColor2, IDC_FXParamValueDisplayForegroundColorBox2 },
    { IDC_FXParamDisplayForegroundColor3, IDC_FXParamValueDisplayForegroundColorBox3 },
    { IDC_FXParamDisplayBackgroundColor1, IDC_FXParamValueDisplayBackgroundColorBox1 },
    { IDC_FXParamDisplayBackgroundColor2, IDC_FXParamValueDisplayBackgroundColorBox2 },
    { IDC_FXParamDisplayBackgroundColor3, IDC_FXParamValueDisplayBackgroundColorBox3 }
};

static void PopulateParamListView(HWND hwndParamList)
{
    ListView_DeleteAllItems(hwndParamList);
        
    LVITEM lvi;
    lvi.mask      = LVIF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvi.stateMask = 0;
    lvi.iSubItem  = 0;
    lvi.state     = 0;

    for(int i = 0; i < s_zoneDef.rawParams.size(); i++)
    {
        lvi.iItem = i;
        lvi.pszText = (char *)s_zoneDef.rawParams[i].c_str();
        
        ListView_InsertItem(hwndParamList, &lvi);
        
        
        int spaceBreak = (int)s_zoneDef.rawParams[i].find( " ");
          
        if(spaceBreak != -1)
        {
            string key = s_zoneDef.rawParams[i].substr(0, spaceBreak);
            string value = s_zoneDef.rawParams[i].substr(spaceBreak + 1, s_zoneDef.rawParams[i].length() - spaceBreak - 1);
            
            s_zoneDef.rawParamsDictionary[key] = value;
        }
    }
}

static bool s_hasFonts = false;
static bool s_hasColors = false;

static WDL_DLGRET dlgProcEditFXParam(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
            
        case WM_PAINT:
        {
            if(s_hasColors)
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwndDlg, &ps);
                
                for(auto [ colorPicker, colorValue ] :  s_buttonColors)
                {
                    // GAW TBD -- think of a more elegant way to do this :)
                    if(s_numGroups < 3 && ( colorPicker == s_widgetRingColors[2] ||
                                            colorPicker == s_widgetRingIndicators[2] ||
                                            colorPicker == s_fixedTextDisplayForegroundColors[2] ||
                                            colorPicker == s_fixedTextDisplayBackgroundColors[2] ||
                                            colorPicker == s_fxParamDisplayForegroundColors[2] ||
                                            colorPicker == s_fxParamDisplayBackgroundColors[2] ))
                            continue;

                    if(s_numGroups < 2 && ( colorPicker == s_widgetRingColors[1] ||
                                            colorPicker == s_widgetRingIndicators[1] ||
                                            colorPicker == s_fixedTextDisplayForegroundColors[1] ||
                                            colorPicker == s_fixedTextDisplayBackgroundColors[1] ||
                                            colorPicker == s_fxParamDisplayForegroundColors[1] ||
                                            colorPicker == s_fxParamDisplayBackgroundColors[1] ))
                            continue;


                    HBRUSH brush = CreateSolidBrush(colorValue);
                    
                    RECT clientRect, windowRect;
                    POINT p;
                    GetClientRect(GetDlgItem(hwndDlg, s_buttonColorBoxes[colorPicker]), &clientRect);
                    GetWindowRect(GetDlgItem(hwndDlg, s_buttonColorBoxes[colorPicker]), &windowRect);
                    p.x = windowRect.left;
                    p.y = windowRect.top;
                    ScreenToClient(hwndDlg, &p);
                    
                    windowRect.left = p.x;
                    windowRect.right = windowRect.left + clientRect.right;
                    windowRect.top = p.y;
                    windowRect.bottom = windowRect.top + clientRect.bottom;
                    
                    FillRect(hdc, &windowRect, brush);
                    DeleteObject(brush);
                }
                
                EndPaint(hwndDlg, &ps);
            }
        }
            break;
            
        case WM_INITDIALOG:
        {
            s_dlgResult = IDCANCEL;
            
            s_hasFonts = false;
            s_hasColors = false;
            
            ShowBaseControls(hwndDlg, 0, (int)s_groupBoxes.size(), false );
            ShowFontControls(hwndDlg, 0, (int)s_groupBoxes.size(), false);
            ShowColorControls(hwndDlg, 0, (int)s_groupBoxes.size(), false);

            SetWindowText(hwndDlg, (s_zoneDef.fxAlias + "   " + s_layoutTemplates[s_fxListIndex].modifiers + s_layoutTemplates[s_fxListIndex].suffix).c_str());

            for(int i = 0; i < s_stepPickers.size(); i++)
            {
                SendDlgItemMessage(hwndDlg, s_stepPickers[i], CB_ADDSTRING, 0, (LPARAM)"Custom");
                
                for(auto [key, value] : s_SteppedValueDictionary)
                    SendDlgItemMessage(hwndDlg, s_stepPickers[i], CB_ADDSTRING, 0, (LPARAM)to_string(key).c_str());
            }
                                      
            PopulateParamListView(GetDlgItem(hwndDlg, IDC_AllParams));
            
            for(auto layout : s_surfaceLayoutTemplate)
            {
                if(layout.size() > 0 )
                {
                    if(layout[0] == "WidgetTypes")
                    {
                        for(int i = 0; i < s_widgetTypePickers.size(); i++)
                            for(int j = 1; j < layout.size(); j++)
                                SendDlgItemMessage(hwndDlg, s_widgetTypePickers[i], CB_ADDSTRING, 0, (LPARAM)layout[j].c_str());
                    }
                    else if(layout[0] == "RingStyles")
                    {
                        for(int i = 0; i < s_ringStylePickers.size(); i++)
                            for(int j = 1; j < layout.size(); j++)
                                SendDlgItemMessage(hwndDlg, s_ringStylePickers[i], CB_ADDSTRING, 0, (LPARAM)layout[j].c_str());
                    }
                    else if(layout[0] == "DisplayRows")
                    {
                        for(int i = 0; i < s_fixedTextDisplayRowPickers.size(); i++)
                        {
                            SendDlgItemMessage(hwndDlg, s_fixedTextDisplayRowPickers[i], CB_ADDSTRING, 0, (LPARAM)"");

                            for(int j = 1; j < layout.size(); j++)
                                SendDlgItemMessage(hwndDlg, s_fixedTextDisplayRowPickers[i], CB_ADDSTRING, 0, (LPARAM)layout[j].c_str());
                        }
                        
                        for(int i = 0; i < s_paramValueDisplayRowPickers.size(); i++)
                        {
                            SendDlgItemMessage(hwndDlg, s_paramValueDisplayRowPickers[i], CB_ADDSTRING, 0, (LPARAM)"");

                            for(int j = 1; j < layout.size(); j++)
                                SendDlgItemMessage(hwndDlg, s_paramValueDisplayRowPickers[i], CB_ADDSTRING, 0, (LPARAM)layout[j].c_str());
                        }
                    }
                    else if(layout[0] == "DisplayFonts")
                    {
                        s_hasFonts = true;
                        
                        for(int i = 0; i < s_fixedTextDisplayFontPickers.size(); i++)
                            for(int j = 1; j < layout.size(); j++)
                                SendDlgItemMessage(hwndDlg, s_fixedTextDisplayFontPickers[i], CB_ADDSTRING, 0, (LPARAM)layout[j].c_str());
                        
                        for(int i = 0; i < s_paramValueDisplayFontPickers.size(); i++)
                            for(int j = 1; j < layout.size(); j++)
                                SendDlgItemMessage(hwndDlg, s_paramValueDisplayFontPickers[i], CB_ADDSTRING, 0, (LPARAM)layout[j].c_str());
                    }
                }
            }

            for(int i = 0; i < s_zoneDef.paramDefs[s_fxListIndex].definitions.size() && i < s_paramNumEditControls.size(); i++)
            {
                SetDlgItemText(hwndDlg, s_paramNumEditControls[i], s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNumber.c_str());
                SetDlgItemText(hwndDlg, s_fixedTextEditControls[i], s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramName.c_str());

                SetDlgItemText(hwndDlg, s_widgetTypePickers[i], s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidget.c_str());
                SetDlgItemText(hwndDlg, s_fixedTextDisplayRowPickers[i], s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidget.c_str());
                SetDlgItemText(hwndDlg, s_paramValueDisplayRowPickers[i], s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidget.c_str());

                if(s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidgetProperties.count("RingStyle") > 0)
                    SetDlgItemText(hwndDlg, s_ringStylePickers[i], s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidgetProperties["RingStyle"].c_str());
                
                if(s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidgetProperties.count("Font") > 0)
                    SetDlgItemText(hwndDlg, s_fixedTextDisplayFontPickers[i], s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidgetProperties["Font"].c_str());

                if(s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidgetProperties.count("Font") > 0)
                    SetDlgItemText(hwndDlg, s_paramValueDisplayFontPickers[i], s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidgetProperties["Font"].c_str());

                if(s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidgetProperties.count("LEDRingColor") > 0)
                {
                    s_hasColors = true;
                    rgba_color color = GetColorValue(s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidgetProperties["LEDRingColor"]);
                    s_buttonColors[s_widgetRingColors[i]] = DAW::ColorToNative(color.r, color.g, color.b);
                }

                if(s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidgetProperties.count("PushColor") > 0)
                {
                    s_hasColors = true;
                    rgba_color color = GetColorValue(s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidgetProperties["PushColor"]);
                    s_buttonColors[s_widgetRingIndicators[i]] = DAW::ColorToNative(color.r, color.g, color.b);
                }

                if(s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidgetProperties.count("Foreground") > 0)
                {
                    s_hasColors = true;
                    rgba_color color = GetColorValue(s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidgetProperties["Foreground"]);
                    s_buttonColors[s_fixedTextDisplayForegroundColors[i]] = DAW::ColorToNative(color.r, color.g, color.b);
                }

                if(s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidgetProperties.count("Background") > 0)
                {
                    s_hasColors = true;
                    rgba_color color = GetColorValue(s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidgetProperties["Background"]);
                    s_buttonColors[s_fixedTextDisplayBackgroundColors[i]] = DAW::ColorToNative(color.r, color.g, color.b);
                }

                if(s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidgetProperties.count("Foreground") > 0)
                {
                    s_hasColors = true;
                    rgba_color color = GetColorValue(s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidgetProperties["Foreground"]);
                    s_buttonColors[s_fxParamDisplayForegroundColors[i]] = DAW::ColorToNative(color.r, color.g, color.b);
                }

                if(s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidgetProperties.count("Background") > 0)
                {
                    s_hasColors = true;
                    rgba_color color = GetColorValue(s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidgetProperties["Background"]);
                    s_buttonColors[s_fxParamDisplayBackgroundColors[i]] = DAW::ColorToNative(color.r, color.g, color.b);
                }

                string steps = "";
                
                for(auto step : s_zoneDef.paramDefs[s_fxListIndex].definitions[i].steps)
                    steps += step + "  ";
                
                SetDlgItemText(hwndDlg, s_stepEditControls[i], steps.c_str());
                
                char buf[BUFSZ];
                
                GetDlgItemText(hwndDlg, s_widgetTypePickers[i], buf, sizeof(buf));
                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidget = buf;

                if(string(buf) == "RotaryPush" && steps == "")
                {
                    SetDlgItemText(hwndDlg, s_stepEditControls[i], "0  1");
                    SetDlgItemText(hwndDlg, s_stepPickers[i], "2");
                }
            }
            
            ShowBaseControls(hwndDlg, 0, s_numGroups, true);
            
            if(s_hasFonts)
                ShowFontControls(hwndDlg, 0, s_numGroups, true);
            
            if(s_hasColors)
            {
                ShowColorControls(hwndDlg, 0, s_numGroups, true);
                InvalidateRect(hwndDlg, NULL, true);
            }
            
        }
            break;

        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case IDC_FXParamRingColor1:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FXParamRingColor1]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamRingColor2:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FXParamRingColor2]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamRingColor3:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FXParamRingColor3]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamIndicatorColor1:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FXParamIndicatorColor1]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamIndicatorColor2:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FXParamIndicatorColor2]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamIndicatorColor3:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FXParamIndicatorColor3]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FixedTextDisplayForegroundColor1:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FixedTextDisplayForegroundColor1]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FixedTextDisplayForegroundColor2:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FixedTextDisplayForegroundColor2]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FixedTextDisplayForegroundColor3:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FixedTextDisplayForegroundColor3]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FixedTextDisplayBackgroundColor1:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FixedTextDisplayBackgroundColor1]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FixedTextDisplayBackgroundColor2:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FixedTextDisplayBackgroundColor2]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FixedTextDisplayBackgroundColor3:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FixedTextDisplayBackgroundColor3]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamDisplayForegroundColor1:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FXParamDisplayForegroundColor1]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamDisplayForegroundColor2:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FXParamDisplayForegroundColor2]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamDisplayForegroundColor3:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FXParamDisplayForegroundColor3]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamDisplayBackgroundColor1:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FXParamDisplayBackgroundColor1]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamDisplayBackgroundColor2:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FXParamDisplayBackgroundColor2]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamDisplayBackgroundColor3:
                    {
                        DAW::GR_SelectColor(hwndDlg, &s_buttonColors[IDC_FXParamDisplayBackgroundColor3]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                          
                case IDC_AdvancedGroup1:
                {
                    s_groupIndex = 0;
                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_Advanced), g_hwnd, dlgProcEditAdvanced);
                    s_dlgResult = IDCANCEL;
                }
                    break;
                    
                case IDC_AdvancedGroup2:
                {
                    s_groupIndex = 1;
                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_Advanced), g_hwnd, dlgProcEditAdvanced);
                    s_dlgResult = IDCANCEL;
                }
                    break;
                    
                case IDC_AdvancedGroup3:
                {
                    s_groupIndex = 2;
                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_Advanced), g_hwnd, dlgProcEditAdvanced);
                    s_dlgResult = IDCANCEL;
                }
                    break;
                    
                case IDCANCEL:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        s_dlgResult = IDCANCEL;
                        EndDialog(hwndDlg, 0);
                    }
                    break ;

                case IDOK:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        for(int i = 0; i < s_zoneDef.paramDefs[s_fxListIndex].definitions.size(); i++)
                        {
                            char buf[BUFSZ];
                            
                            GetDlgItemText(hwndDlg, s_paramNumEditControls[i], buf, sizeof(buf));
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNumber = buf;

                            GetDlgItemText(hwndDlg, s_widgetTypePickers[i], buf, sizeof(buf));
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidget = buf;
                            
                            GetDlgItemText(hwndDlg, s_ringStylePickers[i], buf, sizeof(buf));
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidgetProperties["RingStyle"] = buf;
                            
                            GetDlgItemText(hwndDlg, s_fixedTextEditControls[i], buf, sizeof(buf));
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramName = buf;

                            GetDlgItemText(hwndDlg, s_fixedTextDisplayRowPickers[i], buf, sizeof(buf));
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidget = buf;

                            GetDlgItemText(hwndDlg, s_paramValueDisplayRowPickers[i], buf, sizeof(buf));
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidget = buf;

                            GetDlgItemText(hwndDlg, s_stepEditControls[i], buf, sizeof(buf));
                            
                            if(string(buf) != "")
                            {
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].steps.clear();
                                vector<string> steps;
                                GetTokens(steps, buf);
                                for(auto step : steps)
                                    s_zoneDef.paramDefs[s_fxListIndex].definitions[i].steps.push_back(step);
                            }

                            if(s_hasFonts)
                            {
                                GetDlgItemText(hwndDlg, s_fixedTextDisplayFontPickers[i], buf, sizeof(buf));
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidgetProperties["Font"] = buf;
                                
                                GetDlgItemText(hwndDlg, s_paramValueDisplayFontPickers[i], buf, sizeof(buf));
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidgetProperties["Font"] = buf;
                            }
                            
                            if(s_hasColors)
                            {
                                rgba_color color;
                                
                                DAW::ColorFromNative(s_buttonColors[s_widgetRingColors[i]], &color.r, &color.g, &color.b);
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidgetProperties["LEDRingColor"] = color.to_string();
                                
                                DAW::ColorFromNative(s_buttonColors[s_widgetRingIndicators[i]], &color.r, &color.g, &color.b);
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidgetProperties["PushColor"] = color.to_string();

                                DAW::ColorFromNative(s_buttonColors[s_fixedTextDisplayForegroundColors[i]], &color.r, &color.g, &color.b);
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidgetProperties["Foreground"] = color.to_string();

                                DAW::ColorFromNative(s_buttonColors[s_fixedTextDisplayBackgroundColors[i]], &color.r, &color.g, &color.b);
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidgetProperties["Background"] = color.to_string();

                                DAW::ColorFromNative(s_buttonColors[s_fxParamDisplayForegroundColors[i]], &color.r, &color.g, &color.b);
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidgetProperties["Foreground"] = color.to_string();

                                DAW::ColorFromNative(s_buttonColors[s_fxParamDisplayBackgroundColors[i]], &color.r, &color.g, &color.b);
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidgetProperties["Background"] = color.to_string();
                            }
                        }
                       
                        s_dlgResult = IDOK;
                        EndDialog(hwndDlg, 0);
                    }
                    break ;
                    
                case IDC_PickSteps1:
                {
                    switch (HIWORD(wParam))
                    {
                        case CBN_SELCHANGE:
                        {
                            int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_PickSteps1), CB_GETCURSEL, 0, 0);
                            if(index >= 0)
                            {
                                if(index == 0)
                                    SetDlgItemText(hwndDlg, IDC_EditSteps1, "");
                                else
                                {
                                    ostringstream stepStr;
                                    
                                    for(auto step : s_SteppedValueDictionary[index + 1])
                                    {
                                        stepStr << std::setprecision(2) << step;
                                        stepStr <<  "  ";
                                    }
                                        
                                    SetDlgItemText(hwndDlg, IDC_EditSteps1, (stepStr.str()).c_str());
                                }
                            }
                        }
                    }
                }
                    break;
                
                case IDC_PickSteps2:
                {
                    switch (HIWORD(wParam))
                    {
                        case CBN_SELCHANGE:
                        {
                            int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_PickSteps2), CB_GETCURSEL, 0, 0);
                            if(index >= 0)
                            {
                                if(index == 0)
                                    SetDlgItemText(hwndDlg, IDC_EditSteps2, "");
                                else
                                {
                                    ostringstream stepStr;
                                    
                                    for(auto step : s_SteppedValueDictionary[index + 1])
                                    {
                                        stepStr << std::setprecision(2) << step;
                                        stepStr <<  "  ";
                                    }
                                        
                                    SetDlgItemText(hwndDlg, IDC_EditSteps2, (stepStr.str()).c_str());
                                }
                            }
                        }
                    }
                }
                    break;
                
                case IDC_PickSteps3:
                {
                    switch (HIWORD(wParam))
                    {
                        case CBN_SELCHANGE:
                        {
                            int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_PickSteps3), CB_GETCURSEL, 0, 0);
                            if(index >= 0)
                            {
                                if(index == 0)
                                    SetDlgItemText(hwndDlg, IDC_EditSteps3, "");
                                else
                                {
                                    ostringstream stepStr;
                                    
                                    for(auto step : s_SteppedValueDictionary[index + 1])
                                    {
                                        stepStr << std::setprecision(2) << step;
                                        stepStr <<  "  ";
                                    }
                                        
                                    SetDlgItemText(hwndDlg, IDC_EditSteps3, (stepStr.str()).c_str());
                                }
                            }
                        }
                    }
                }
                    break;
            }
        }
    }
    
    return 0;
}

vector<string> GetLineComponents(int index)
{
    vector<string> components;
    
    components.push_back(s_layoutTemplates[index].modifiers + s_layoutTemplates[index].suffix);
    
    for(auto paramDef :  s_zoneDef.paramDefs[index].definitions)
    {
        string widgetName = paramDef.paramWidget;
        
        if(widgetName == "RotaryPush")
            widgetName = "Push";
        
        components.push_back(widgetName);
        
        string alias = paramDef.paramName;

        if(paramDef.paramName == "" && paramDef.paramNumber != "" && s_zoneDef.rawParamsDictionary.count(paramDef.paramNumber) > 0)
            alias = s_zoneDef.rawParamsDictionary[paramDef.paramNumber];
        else if(paramDef.paramNumber == "")
            alias = "NoAction";
        
        components.push_back(alias);
    }
        
    return components;
}

static void SetListViewItem(HWND hwndParamList, int index, bool shouldInsert)
{
    LVITEM lvi;
    lvi.mask      = LVIF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvi.stateMask = 0;
    lvi.iSubItem  = 0;
    lvi.state     = 0;

    vector<string> components = GetLineComponents(index);
    
    string preamble = components[0];
    
#ifdef _WIN32
    preamble += "                                                       ";
#endif
                   
    lvi.iItem = index;
    lvi.pszText = (char *)preamble.c_str();
    
    if(shouldInsert)
        ListView_InsertItem(hwndParamList, &lvi);
    else
        ListView_SetItem(hwndParamList, &lvi);
    
    for(int i = 1; i < components.size(); i++)
    {
        lvi.iSubItem = i;
        lvi.pszText = (char *)components[i].c_str();

        ListView_SetItem(hwndParamList, &lvi);
    }
}

static void PopulateListView(HWND hwndParamList)
{
    ListView_DeleteAllItems(hwndParamList);
        
    for(int i = 0; i < s_zoneDef.paramDefs.size(); i++)
        SetListViewItem(hwndParamList, i, true);
}

static void MoveUp(HWND hwndParamList)
{
    int index = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
    if(index > 0)
    {
        FXParamDefinitions itemToMove;
        
        for(auto def : s_zoneDef.paramDefs[index].definitions)
            itemToMove.definitions.push_back(def);
        
        s_zoneDef.paramDefs.erase(s_zoneDef.paramDefs.begin() + index);
        s_zoneDef.paramDefs.insert(s_zoneDef.paramDefs.begin() + index - 1, itemToMove);
        
        SetListViewItem(hwndParamList, index, false);
        SetListViewItem(hwndParamList, index - 1, false);

        ListView_SetItemState(hwndParamList, index - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

static void MoveDown(HWND hwndParamList)
{
    int index = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
    if(index >= 0 && index < s_zoneDef.paramDefs.size() - 1)
    {
        FXParamDefinitions itemToMove;
        
        for(auto def : s_zoneDef.paramDefs[index].definitions)
            itemToMove.definitions.push_back(def);
        
        s_zoneDef.paramDefs.erase(s_zoneDef.paramDefs.begin() + index);
        s_zoneDef.paramDefs.insert(s_zoneDef.paramDefs.begin() + index + 1, itemToMove);

        SetListViewItem(hwndParamList, index, false);
        SetListViewItem(hwndParamList, index + 1, false);

        ListView_SetItemState(hwndParamList, index + 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

static void EditItem(HWND hwndParamList)
{
    int index = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
    
    if(index >= 0)
    {
        s_fxListIndex = index;
        DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_EditFXParam), g_hwnd, dlgProcEditFXParam);
        
        if(s_dlgResult == IDOK)
            SetListViewItem(hwndParamList, index, false);
        
        s_dlgResult = IDCANCEL;
    }
}

static bool DeleteZone()
{
    if(MessageBox(NULL, (string("This will permanently delete\n\n") + s_zoneDef.fxName + string(".zon\n\n Are you sure you want to permanently delete this file from disk? \n\nIf you delerte the file the RemapAutoZone dialog will close.")).c_str(), string("Delete " + s_zoneDef.fxAlias).c_str(), MB_YESNO) == IDNO)
       return false;
    
    s_zoneManager->RemoveZone(s_zoneDef.fxName);
    
    return true;
}

static bool s_isDragging = false;

#ifdef _WIN32

#pragma comment(lib, "comctl32.lib")

static HIMAGELIST   s_hDragImageList;
static int          s_oldPosition;

static WDL_DLGRET dlgProcRemapFXAutoZone(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_NOTIFY:
        {
            if(((LPNMHDR)lParam)->code == NM_DBLCLK)
                EditItem(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
            else if(((LPNMHDR)lParam)->code == LVN_BEGINDRAG)
            {
                HWND hwndParamList = GetDlgItem(hwndDlg, IDC_PARAM_LIST);
               
                POINT p;
                p.x = 8;
                p.y = 8;

                int iPos = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
                if(iPos != -1)
                {
                    s_oldPosition = iPos;

                    s_isDragging = TRUE;

                    s_hDragImageList = ListView_CreateDragImage(hwndParamList, iPos, &p);
                    ImageList_BeginDrag(s_hDragImageList, 0, 0, 0);

                    POINT pt = ((NM_LISTVIEW*) ((LPNMHDR)lParam))->ptAction;
                    ClientToScreen(hwndParamList, &pt);

                    ImageList_DragEnter(GetDesktopWindow(), pt.x, pt.y);

                    SetCapture(hwndDlg);
                }
            }
        }
            break;
            
        case WM_INITDIALOG:
        {
            SetWindowText(hwndDlg, "Remap Auto Zone");

            HWND paramList = GetDlgItem(hwndDlg, IDC_PARAM_LIST);
            
            vector<int> columnSizes = { 160 };
            
            for(int i = 1; i <= s_numGroups; i++)
            {
                columnSizes.push_back(80);
                columnSizes.push_back(150);
            }
            
            LVCOLUMN columnDescriptor = { LVCF_TEXT | LVCF_WIDTH, 0, 0, (char*)"" };
            columnDescriptor.cx = columnSizes[0];
            
            ListView_InsertColumn(paramList, 0, &columnDescriptor);
            
            for(int i = 1; i <= s_numGroups * 2; i++)
            {
                columnDescriptor.cx = columnSizes[i];
                ListView_InsertColumn(paramList, i, &columnDescriptor);
            }

            s_dlgResult = IDCANCEL;
            
            SetDlgItemText(hwndDlg, IDC_FXNAME, s_zoneDef.fxName.c_str());
            SetDlgItemText(hwndDlg, IDC_EDIT_FXAlias, s_zoneDef.fxAlias.c_str());
            
            PopulateListView(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
            
            break;
        }
            
        case WM_LBUTTONUP:
        {
            if(s_isDragging)
            {
                s_isDragging = FALSE;
                
                HWND hwndParamList = GetDlgItem(hwndDlg, IDC_PARAM_LIST);
                
                ImageList_DragLeave(hwndParamList);
                ImageList_EndDrag();
                ImageList_Destroy(s_hDragImageList);

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
                if (((lvhti.flags & LVHT_ONITEMLABEL) == 0) &&
                          ((lvhti.flags & LVHT_ONITEMSTATEICON) == 0))
                    break;
                
                FXParamDefinitions itemToMove;
                
                for(auto def : s_zoneDef.paramDefs[s_oldPosition].definitions)
                    itemToMove.definitions.push_back(def);
                
                s_zoneDef.paramDefs.erase(s_zoneDef.paramDefs.begin() + s_oldPosition);
                s_zoneDef.paramDefs.insert(s_zoneDef.paramDefs.begin() + lvhti.iItem, itemToMove);
                
                PopulateListView(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
                
                ListView_EnsureVisible(GetDlgItem(hwndDlg, IDC_PARAM_LIST), lvhti.iItem, false);
                
                ListView_SetItemState(hwndParamList, lvhti.iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            break;
        }

        case WM_MOUSEMOVE:
        {
            if(s_isDragging)
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
                        s_zoneDef.fxAlias = buf;
                        
                        s_dlgResult = IDSAVE;
                        EndDialog(hwndDlg, 0);
                    }
                    break ;
                   
                case IDEDIT:
                    if (HIWORD(wParam) == BN_CLICKED)
                        EditItem(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
                    break ;
                    
                case IDC_Delete:
                    if (HIWORD(wParam) == BN_CLICKED)
                         if(DeleteZone())
                             EndDialog(hwndDlg, 0);
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

static POINT lastCursorPosition;

static WDL_DLGRET dlgProcRemapFXAutoZone(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_NOTIFY:
        {
            if(((LPNMHDR)lParam)->code == NM_DBLCLK)
                EditItem(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
            else if(((LPNMHDR)lParam)->code == LVN_BEGINDRAG)
            {
                s_isDragging = true;
                GetCursorPos(&lastCursorPosition);
                SetCapture(hwndDlg);
            }
        }
            break;
            
        case WM_INITDIALOG:
        {
            SetWindowText(hwndDlg, "Remap Auto Zone");

            HWND paramList = GetDlgItem(hwndDlg, IDC_PARAM_LIST);
            
            vector<int> columnSizes = { 65 }; // modifiers
            
            for(int i = 1; i <= s_numGroups; i++)
            {
                columnSizes.push_back(38); // widget
                columnSizes.push_back(75); // param name
            }
            
            LVCOLUMN columnDescriptor = { LVCF_TEXT | LVCF_WIDTH, LVCFMT_RIGHT, 0, (char*)"" };
            columnDescriptor.cx = columnSizes[0];
            
            ListView_InsertColumn(paramList, 0, &columnDescriptor);
            
            for(int i = 1; i <= s_numGroups * 2; i++)
            {
                columnDescriptor.cx = columnSizes[i];
                ListView_InsertColumn(paramList, i, &columnDescriptor);
            }
            
            s_dlgResult = IDCANCEL;
            
            SetDlgItemText(hwndDlg, IDC_FXNAME, s_zoneDef.fxName.c_str());
            SetDlgItemText(hwndDlg, IDC_EDIT_FXAlias, s_zoneDef.fxAlias.c_str());
            
            PopulateListView(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
            
            break;
        }
            
        case WM_LBUTTONUP:
        {
            if(s_isDragging)
            {
                s_isDragging = false;
                ReleaseCapture();
            }
            break;
        }

        case WM_MOUSEMOVE:
        {
            if(s_isDragging)
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
                        char buf[BUFSZ];
                        GetDlgItemText(hwndDlg, IDC_EDIT_FXAlias, buf, sizeof(buf));
                        s_zoneDef.fxAlias = buf;
                        
                        s_dlgResult = IDSAVE;
                        EndDialog(hwndDlg, 0);
                    }
                    break ;
                   
                case IDEDIT:
                    if (HIWORD(wParam) == BN_CLICKED)
                         EditItem(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
                    break ;
                    
                case IDC_Delete:
                    if (HIWORD(wParam) == BN_CLICKED)
                        if(DeleteZone())
                            EndDialog(hwndDlg, 0);
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

bool RemapAutoZoneDialog(shared_ptr<ZoneManager> aZoneManager, string fullFilePath)
{
    s_zoneDef.Clear();
    s_zoneManager = aZoneManager;
    s_zoneDef.fullPath = fullFilePath;
    s_numGroups = s_zoneManager->GetNumGroups();
    s_layoutTemplates = s_zoneManager->GetFXLayoutTemplates();
    s_surfaceLayoutTemplate = s_zoneManager->GetSurfaceFXLayoutTemplate();
    
    s_zoneManager->UnpackZone(s_zoneDef, s_layoutTemplates);
    
    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_RemapAutoFX), g_hwnd, dlgProcRemapFXAutoZone);
    
    if(s_dlgResult == IDSAVE)
    {
        s_zoneManager->SaveAutoZone(s_zoneDef, s_layoutTemplates);
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

static vector<shared_ptr<SurfaceLine>> s_surfaces;

struct PageSurfaceLine
{
    string pageSurfaceName = "";
    int numChannels = 0;
    int channelOffset = 0;
    string templateFilename = "";
    string zoneTemplateFolder = "";
    string fxZoneTemplateFolder = "";
};

// Broadcast/Listen
struct Listener
{
    string name = "";
    bool goHome = false;
    bool sends = false;
    bool receives = false;
    bool focusedFX = false;
    bool focusedFXParam = false;
    bool fxMenu = false;
    bool localFXSlot = false;
    bool selectedTrackFX = false;
    bool custom = false;
    bool modifiers = false;
};

struct Broadcaster
{
    string name = "";
    vector<shared_ptr<Listener>> listeners;
};

struct PageLine
{
    string name = "";
    bool followMCP = true;
    bool synchPages = true;
    bool isScrollLinkEnabled = false;
    bool scrollSynch = false;
    vector<shared_ptr<PageSurfaceLine>> surfaces;
    vector<shared_ptr<Broadcaster>> broadcasters;
};

// Scratch pad to get in and out of dialogs easily
static vector<shared_ptr<Broadcaster>> s_broadcasters;

static void TransferBroadcasters(vector<shared_ptr<Broadcaster>> &source, vector<shared_ptr<Broadcaster>> &destination)
{
    destination.clear();
    
    for(auto sourceBrodacaster : source)
    {
        shared_ptr<Broadcaster> destinationBroadcaster = make_shared<Broadcaster>();
        
        destinationBroadcaster->name = sourceBrodacaster->name;
        
        for(auto sourceListener : sourceBrodacaster->listeners)
        {
            shared_ptr<Listener> destinationListener = make_shared<Listener>();
            
            destinationListener->name = sourceListener->name;
            
            destinationListener->goHome = sourceListener->goHome;
            destinationListener->sends = sourceListener->sends;
            destinationListener->receives = sourceListener->receives;
            destinationListener->focusedFX = sourceListener->focusedFX;
            destinationListener->focusedFXParam = sourceListener->focusedFXParam;
            destinationListener->fxMenu = sourceListener->fxMenu;
            destinationListener->localFXSlot = sourceListener->localFXSlot;
            destinationListener->modifiers = sourceListener->modifiers;
            destinationListener->selectedTrackFX = sourceListener->selectedTrackFX;
            destinationListener->custom = sourceListener->custom;
            
            destinationBroadcaster->listeners.push_back(destinationListener);
        }
        
        destination.push_back(destinationBroadcaster);
    }
}

static bool s_editMode = false;

static string s_surfaceName = "";
static int s_surfaceInPort = 0;
static int s_surfaceOutPort = 0;
static string s_surfaceRemoteDeviceIP = "";

static int s_pageIndex = 0;
static bool s_followMCP = false;
static bool s_synchPages = true;
static bool s_isScrollLinkEnabled = false;
static bool s_scrollSynch = false;

static string s_pageSurfaceName = "";
static int s_numChannels = 0;
static int s_channelOffset = 0;
static string s_templateFilename = "";
static string s_zoneTemplateFolder = "";
static string s_fxZoneTemplateFolder = "";

static vector<shared_ptr<PageLine>> s_pages;

void AddComboEntry(HWND hwndDlg, int x, char * buf, int comboId)
{
    int a = (int)SendDlgItemMessage(hwndDlg,comboId,CB_ADDSTRING,0,(LPARAM)buf);
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
            if(s_editMode)
            {
                SetDlgItemText(hwndDlg, IDC_EDIT_PageName, s_surfaceName.c_str());
                
                CheckDlgButton(hwndDlg, IDC_RADIO_TCP, ! s_followMCP);
                CheckDlgButton(hwndDlg, IDC_RADIO_MCP, s_followMCP);

                CheckDlgButton(hwndDlg, IDC_CHECK_SynchPages, s_synchPages);
                CheckDlgButton(hwndDlg, IDC_CHECK_ScrollLink, s_isScrollLinkEnabled);
                CheckDlgButton(hwndDlg, IDC_CHECK_ScrollSynch, s_scrollSynch);
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
                        char buf[BUFSZ];
                        GetDlgItemText(hwndDlg, IDC_EDIT_PageName, buf, sizeof(buf));
                        s_surfaceName = buf;
                        
                        if(IsDlgButtonChecked(hwndDlg, IDC_RADIO_TCP))
                           s_followMCP = false;
                        else if(IsDlgButtonChecked(hwndDlg, IDC_RADIO_MCP))
                           s_followMCP = true;
                        
                        s_synchPages = IsDlgButtonChecked(hwndDlg, IDC_CHECK_SynchPages);
                        s_isScrollLinkEnabled = IsDlgButtonChecked(hwndDlg, IDC_CHECK_ScrollLink);
                        s_scrollSynch = IsDlgButtonChecked(hwndDlg, IDC_CHECK_ScrollSynch);
                        
                        s_dlgResult = IDOK;
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
    
    for(auto surface : s_surfaces)
    {
        if(surface->name == string(buf))
        {
            if(surface->type == s_MidiSurfaceToken)
                for(auto filename : FileSystem::GetDirectoryFilenames(resourcePath + "/CSI/Surfaces/Midi/"))
                    AddComboEntry(hwndDlg, 0, (char*)filename.c_str(), IDC_COMBO_SurfaceTemplate);

            if(surface->type == s_OSCSurfaceToken)
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
            if(s_editMode)
            {
                AddComboEntry(hwndDlg, 0, (char *)s_pageSurfaceName.c_str(), IDC_COMBO_PageSurface);
                SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_PageSurface), CB_SETCURSEL, 0, 0);

                SetDlgItemInt(hwndDlg, IDC_EDIT_NumChannels, s_numChannels, false);
                SetDlgItemInt(hwndDlg, IDC_EDIT_ChannelOffset, s_channelOffset, false);
               
                PopulateSurfaceTemplateCombo(hwndDlg, resourcePath);
                
                for(auto foldername : FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/"))
                    AddComboEntry(hwndDlg, 0, (char *)foldername.c_str(), IDC_COMBO_ZoneTemplates);

                for(auto foldername : FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/"))
                    AddComboEntry(hwndDlg, 0, (char *)foldername.c_str(), IDC_COMBO_FXZoneTemplates);

                int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_FINDSTRINGEXACT, -1, (LPARAM)s_templateFilename.c_str());
                if(index >= 0)
                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_SETCURSEL, index, 0);
                
                index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)s_zoneTemplateFolder.c_str());
                if(index >= 0)
                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_SETCURSEL, index, 0);
                
                index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_FXZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)s_fxZoneTemplateFolder.c_str());
                if(index >= 0)
                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_FXZoneTemplates), CB_SETCURSEL, index, 0);
            }
            else
            {
                for(auto surface : s_surfaces)
                    AddComboEntry(hwndDlg, 0, (char *)surface->name.c_str(), IDC_COMBO_PageSurface);
                
                SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_PageSurface), CB_SETCURSEL, 0, 0);
                
                SetDlgItemText(hwndDlg, IDC_EDIT_NumChannels, "0");
                SetDlgItemText(hwndDlg, IDC_EDIT_ChannelOffset, "0");
                
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
                            if(index >= 0 && ! s_editMode)
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
                                
                                int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)buffer);
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
                            if(index >= 0 && ! s_editMode)
                            {
                                char buffer[BUFSZ];
                                
                                GetDlgItemText(hwndDlg, IDC_COMBO_ZoneTemplates, buffer, sizeof(buffer));
                                
                                int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_FXZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)buffer);
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
                        char buf[BUFSZ];

                        GetDlgItemText(hwndDlg, IDC_COMBO_PageSurface, buf, sizeof(buf));
                        s_pageSurfaceName = buf;
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_NumChannels, buf, sizeof(buf));
                        s_numChannels = atoi(buf);
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_ChannelOffset, buf, sizeof(buf));
                        s_channelOffset = atoi(buf);
                                                
                        GetDlgItemText(hwndDlg, IDC_COMBO_SurfaceTemplate, buf, sizeof(buf));
                        s_templateFilename = buf;
                        
                        GetDlgItemText(hwndDlg, IDC_COMBO_ZoneTemplates, buf, sizeof(buf));
                        s_zoneTemplateFolder = buf;
                        
                        GetDlgItemText(hwndDlg, IDC_COMBO_FXZoneTemplates, buf, sizeof(buf));
                        s_fxZoneTemplateFolder = buf;

                        s_dlgResult = IDOK;
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
            
            for (int i = 0; i < DAW::GetNumMIDIInputs(); i++)
                if (DAW::GetMIDIInputName(i, buf, sizeof(buf)))
                {
                    AddComboEntry(hwndDlg, i, buf, IDC_COMBO_MidiIn);
                    if(s_editMode && s_surfaceInPort == i)
                        SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_MidiIn), CB_SETCURSEL, currentIndex, 0);
                    currentIndex++;
                }
            
            currentIndex = 0;
            
            for (int i = 0; i < DAW::GetNumMIDIOutputs(); i++)
                if (DAW::GetMIDIOutputName(i, buf, sizeof(buf)))
                {
                    AddComboEntry(hwndDlg, i, buf, IDC_COMBO_MidiOut);
                    if(s_editMode && s_surfaceOutPort == i)
                        SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_MidiOut), CB_SETCURSEL, currentIndex, 0);
                    currentIndex++;
                }
            
            string resourcePath(DAW::GetResourcePath());
            
            if(s_editMode)
            {
                SetDlgItemText(hwndDlg, IDC_EDIT_MidiSurfaceName, s_surfaceName.c_str());
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
                            if(index >= 0 && !s_editMode)
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
                                
                                int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)buffer);
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
                        GetDlgItemText(hwndDlg, IDC_EDIT_MidiSurfaceName, buf, sizeof(buf));
                        s_surfaceName = buf;
                        
                        int currentSelection = (int)SendDlgItemMessage(hwndDlg, IDC_COMBO_MidiIn, CB_GETCURSEL, 0, 0);
                        if (currentSelection >= 0)
                            s_surfaceInPort = (int)SendDlgItemMessage(hwndDlg, IDC_COMBO_MidiIn, CB_GETITEMDATA, currentSelection, 0);
                        currentSelection = (int)SendDlgItemMessage(hwndDlg, IDC_COMBO_MidiOut, CB_GETCURSEL, 0, 0);
                        if (currentSelection >= 0)
                            s_surfaceOutPort = (int)SendDlgItemMessage(hwndDlg, IDC_COMBO_MidiOut, CB_GETITEMDATA, currentSelection, 0);
                        
                        s_dlgResult = IDOK;
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
            
            if(s_editMode)
            {
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCSurfaceName, s_surfaceName.c_str());
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCRemoteDeviceIP, s_surfaceRemoteDeviceIP.c_str());
                SetDlgItemInt(hwndDlg, IDC_EDIT_OSCInPort, s_surfaceInPort, false);
                SetDlgItemInt(hwndDlg, IDC_EDIT_OSCOutPort, s_surfaceOutPort, false);
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
                            if(index >= 0 && !s_editMode)
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
                                
                                int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)buffer);
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
                                            
                        GetDlgItemText(hwndDlg, IDC_EDIT_OSCSurfaceName, buf, sizeof(buf));
                        s_surfaceName = buf;
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_OSCRemoteDeviceIP, buf, sizeof(buf));
                        s_surfaceRemoteDeviceIP = buf;
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_OSCInPort, buf, sizeof(buf));
                        s_surfaceInPort = atoi(buf);
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_OSCOutPort, buf, sizeof(buf));
                        s_surfaceOutPort = atoi(buf);
                        
                        s_dlgResult = IDOK;
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

static void SetCheckBoxes(HWND hwndDlg, shared_ptr<Listener> listener)
{
    SetWindowText(GetDlgItem(hwndDlg, IDC_ListenCheckboxes), string(listener->name + " Listens to").c_str());

    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_GoHome), BM_SETCHECK, listener->goHome ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Sends), BM_SETCHECK, listener->sends ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Receives), BM_SETCHECK, listener->receives ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FocusedFX), BM_SETCHECK, listener->focusedFX ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FocusedFXParam), BM_SETCHECK, listener->focusedFXParam ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FXMenu), BM_SETCHECK, listener->fxMenu ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_LocalFXSlot), BM_SETCHECK, listener->localFXSlot ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Modifiers), BM_SETCHECK, listener->modifiers ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_SelectedTrackFX), BM_SETCHECK, listener->selectedTrackFX ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Custom), BM_SETCHECK, listener->custom ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void ClearCheckBoxes(HWND hwndDlg)
{
    SetWindowText(GetDlgItem(hwndDlg, IDC_ListenCheckboxes), "Surface Listens to");

    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_GoHome), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Sends), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Receives), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FocusedFX), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FocusedFXParam), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FXMenu), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_LocalFXSlot), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Modifiers), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_SelectedTrackFX), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Custom), BM_SETCHECK, BST_UNCHECKED, 0);
}

static WDL_DLGRET dlgProcBroadcast(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            for(auto surface : s_surfaces)
                AddComboEntry(hwndDlg, 0, (char *)surface->name.c_str(), IDC_AddBroadcaster);
            SendMessage(GetDlgItem(hwndDlg, IDC_AddBroadcaster), CB_SETCURSEL, 0, 0);

            for(auto surface : s_surfaces)
                AddComboEntry(hwndDlg, 0, (char *)surface->name.c_str(), IDC_AddListener);
            SendMessage(GetDlgItem(hwndDlg, IDC_AddListener), CB_SETCURSEL, 0, 0);
            
            TransferBroadcasters(s_pages[s_pageIndex]->broadcasters, s_broadcasters);
            
            if(s_broadcasters.size() > 0)
            {
                for(auto broadcaster : s_broadcasters)
                    AddListEntry(hwndDlg, broadcaster->name, IDC_LIST_Broadcasters);
                    
                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Broadcasters), LB_SETCURSEL, 0, 0);
            }
        }
            
        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case IDC_LIST_Broadcasters:
                    if (HIWORD(wParam) == LBN_SELCHANGE)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        if (broadcasterIndex >= 0)
                        {
                            SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Listeners), LB_RESETCONTENT, 0, 0);
                           
                            for (auto listener : s_broadcasters[broadcasterIndex]->listeners)
                                AddListEntry(hwndDlg, listener->name, IDC_LIST_Listeners);
                            
                            if(s_broadcasters.size() > 0)
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Listeners), LB_SETCURSEL, 0, 0);
                            
                            if(broadcasterIndex >= 0 && s_broadcasters[broadcasterIndex]->listeners.size() > 0)
                                SetCheckBoxes(hwndDlg, s_broadcasters[broadcasterIndex]->listeners[0]);
                            else
                                ClearCheckBoxes(hwndDlg);
                        }
                        else
                        {
                            SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Listeners), LB_RESETCONTENT, 0, 0);
                        }
                    }
                    break;

                case IDC_LIST_Listeners:
                    if (HIWORD(wParam) == LBN_SELCHANGE)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);
                        
                        if(broadcasterIndex >= 0 && listenerIndex >= 0)
                            SetCheckBoxes(hwndDlg, s_broadcasters[broadcasterIndex]->listeners[listenerIndex]);
                    }
                    break;

                case ID_BUTTON_AddBroadcaster:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_AddBroadcaster, LB_GETCURSEL, 0, 0);
                        if (broadcasterIndex >= 0)
                        {
                            char broadcasterName[BUFSZ];
                            GetDlgItemText(hwndDlg, IDC_AddBroadcaster, broadcasterName, sizeof(broadcasterName));
                            
                            bool foundit = false;
                            for(auto broadcaster : s_broadcasters)
                                if(broadcasterName == broadcaster->name)
                                    foundit = true;
                            if(! foundit)
                            {
                                shared_ptr<Broadcaster> broadcaster = make_shared<Broadcaster>();
                                broadcaster->name = broadcasterName;
                                s_broadcasters.push_back(broadcaster);
                                AddListEntry(hwndDlg, broadcasterName, IDC_LIST_Broadcasters);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Broadcasters), LB_SETCURSEL, s_broadcasters.size() - 1, 0);
                                ClearCheckBoxes(hwndDlg);
                            }
                        }
                    }
                    break;
                    
                case ID_BUTTON_AddListener:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_AddListener, LB_GETCURSEL, 0, 0);
                        if (broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            char listenerName[BUFSZ];
                            GetDlgItemText(hwndDlg, IDC_AddListener, listenerName, sizeof(listenerName));
                            
                            bool foundit = false;
                            for(auto listener : s_broadcasters[broadcasterIndex]->listeners)
                                if(listenerName == listener->name)
                                foundit = true;
                            if(! foundit)
                            {
                                shared_ptr<Listener> listener = make_shared<Listener>();
                                listener->name = listenerName;
                                 s_broadcasters[broadcasterIndex]->listeners.push_back(listener);
                                AddListEntry(hwndDlg, listenerName, IDC_LIST_Listeners);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Listeners), LB_SETCURSEL,  s_broadcasters[broadcasterIndex]->listeners.size() - 1, 0);
                                ClearCheckBoxes(hwndDlg);

                                SetWindowText(GetDlgItem(hwndDlg, IDC_ListenCheckboxes), string( s_broadcasters[broadcasterIndex]->listeners.back()->name + " Listens to").c_str());
                            }
                        }
                    }
                    break;
                 
                case IDC_CHECK_GoHome:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if(broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if(SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_GoHome), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                 s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->goHome = true;
                            else
                                 s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->goHome = false;
                        }
                    }
                    break;
                                        
                case IDC_CHECK_Sends:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if(broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if(SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Sends), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->sends = true;
                            else
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->sends = false;
                        }
                    }
                    break;
                    
                case IDC_CHECK_Receives:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex =(int) SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if(broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if(SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Receives), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->receives = true;
                            else
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->receives = false;
                        }
                    }
                    break;
                    
                case IDC_CHECK_FocusedFX:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if(broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if(SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FocusedFX), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->focusedFX = true;
                            else
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->focusedFX = false;
                        }
                    }
                    break;
                    
                case IDC_CHECK_FocusedFXParam:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if(broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if(SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FocusedFXParam), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->focusedFXParam = true;
                            else
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->focusedFXParam = false;
                        }
                    }
                    break;
                    
                case IDC_CHECK_FXMenu:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if(broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if(SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FXMenu), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->fxMenu = true;
                            else
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->fxMenu = false;
                        }
                    }
                    break;
                 
                case IDC_CHECK_LocalFXSlot:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if(broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if(SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_LocalFXSlot), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->localFXSlot = true;
                            else
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->localFXSlot = false;
                        }
                    }
                    break;
                 
                case IDC_CHECK_Modifiers:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if(broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if(SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Modifiers), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->modifiers = true;
                            else
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->modifiers = false;
                        }
                    }
                    break;
                 
                case IDC_CHECK_SelectedTrackFX:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if(broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if(SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_SelectedTrackFX), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->selectedTrackFX = true;
                            else
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->selectedTrackFX = false;
                        }
                    }
                    break;
                 
                case IDC_CHECK_Custom:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if(broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if(SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Custom), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->custom = true;
                            else
                                s_broadcasters[broadcasterIndex]->listeners[listenerIndex]->custom = false;
                        }
                    }
                    break;
                 
                case ID_RemoveBroadcaster:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);

                        if(broadcasterIndex >= 0)
                        {
                            s_broadcasters.erase(s_broadcasters.begin() + broadcasterIndex);
                            SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Broadcasters), LB_RESETCONTENT, 0, 0);
                            SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Listeners), LB_RESETCONTENT, 0, 0);
                            ClearCheckBoxes(hwndDlg);

                            if(s_broadcasters.size() > 0)
                            {
                                for(auto broadcaster : s_broadcasters)
                                    AddListEntry(hwndDlg, broadcaster->name, IDC_LIST_Broadcasters);
                                    
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Broadcasters), LB_SETCURSEL, s_broadcasters.size() - 1, 0);
                            }
                        }
                    }
                    break;

                case ID_RemoveListener:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if(broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            s_broadcasters[broadcasterIndex]->listeners.erase(s_broadcasters[broadcasterIndex]->listeners.begin() + listenerIndex);
                            SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Listeners), LB_RESETCONTENT, 0, 0);
                            ClearCheckBoxes(hwndDlg);
                            if(s_broadcasters[broadcasterIndex]->listeners.size() > 0)
                            {
                                for(auto listener : s_broadcasters[broadcasterIndex]->listeners)
                                    AddListEntry(hwndDlg, listener->name, IDC_LIST_Listeners);
                                    
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Listeners), LB_SETCURSEL, s_broadcasters[broadcasterIndex]->listeners.size() - 1, 0);
                                
#ifdef WIN32
                                listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);
                                
                                if(listenerIndex >= 0)
                                    SetCheckBoxes(hwndDlg, s_broadcasters[broadcasterIndex]->listeners[listenerIndex]);
#endif
                            }
                        }
                    }
                    break;

                case WM_CLOSE:
                case IDCANCEL:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                         EndDialog(hwndDlg, 0);
                    }
                     break;
                    
                case IDOK:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        TransferBroadcasters(s_broadcasters, s_pages[s_pageIndex]->broadcasters);

                        EndDialog(hwndDlg, 0);
                    }
                    break;
            }
        }
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
                            int index = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                            if (index >= 0)
                            {
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_RESETCONTENT, 0, 0);

                                s_pageIndex = index;

                                for (auto surface : s_pages[index]->surfaces)
                                    AddListEntry(hwndDlg, surface->pageSurfaceName, IDC_LIST_PageSurfaces);
                                
                                if(s_pages[index]->surfaces.size() > 0)
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
                            int index = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                            if (index >= 0)
                            {
                                s_dlgResult = false;
                                s_editMode = false;
                                DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_MidiSurface), hwndDlg, dlgProcMidiSurface);
                                if(s_dlgResult == IDOK)
                                {
                                    shared_ptr<SurfaceLine> surface = make_shared<SurfaceLine>();
                                    surface->type = s_MidiSurfaceToken;
                                    surface->name = s_surfaceName;
                                    surface->inPort = s_surfaceInPort;
                                    surface->outPort = s_surfaceOutPort;

                                    s_surfaces.push_back(surface);
                                    
                                    AddListEntry(hwndDlg, s_surfaceName.c_str(), IDC_LIST_Surfaces);
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL, s_surfaces.size() - 1, 0);
                                }
                            }
                        }
                        break ;
                        
                    case IDC_BUTTON_AddOSCSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                            if (index >= 0)
                            {
                                s_dlgResult = false;
                                s_editMode = false;
                                DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_OSCSurface), hwndDlg, dlgProcOSCSurface);
                                if(s_dlgResult == IDOK)
                                {
                                    shared_ptr<SurfaceLine> surface = make_shared<SurfaceLine>();
                                    surface->type = s_OSCSurfaceToken;
                                    surface->name = s_surfaceName;
                                    surface->remoteDeviceIP = s_surfaceRemoteDeviceIP;
                                    surface->inPort = s_surfaceInPort;
                                    surface->outPort = s_surfaceOutPort;

                                    s_surfaces.push_back(surface);
                                    
                                    AddListEntry(hwndDlg, s_surfaceName.c_str(), IDC_LIST_Surfaces);
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL, s_surfaces.size() - 1, 0);
                                }
                            }
                        }
                        break ;
                     
                    case IDC_BUTTON_AddPage:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            s_dlgResult = false;
                            s_editMode = false;
                            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_Page), hwndDlg, dlgProcPage);
                            if(s_dlgResult == IDOK)
                            {
                                shared_ptr<PageLine> page = make_shared<PageLine>();
                                page->name = s_surfaceName;
                                page->followMCP = s_followMCP;
                                page->synchPages = s_synchPages;
                                page->isScrollLinkEnabled = s_isScrollLinkEnabled;
                                page->scrollSynch = s_scrollSynch;

                                s_pages.push_back(page);
                                AddListEntry(hwndDlg, s_surfaceName.c_str(), IDC_LIST_Pages);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_SETCURSEL, s_pages.size() - 1, 0);
                            }
                        }
                        break ;

                    case IDC_BUTTON_AddPageSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            s_dlgResult = false;
                            s_editMode = false;
                            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_PageSurface), hwndDlg, dlgProcPageSurface);
                            if(s_dlgResult == IDOK)
                            {
                                shared_ptr<PageSurfaceLine> pageSurface = make_shared<PageSurfaceLine>();
                                pageSurface->pageSurfaceName = s_pageSurfaceName;
                                pageSurface->numChannels = s_numChannels;
                                pageSurface->channelOffset = s_channelOffset;
                                pageSurface->templateFilename = s_templateFilename;
                                pageSurface->zoneTemplateFolder = s_zoneTemplateFolder;
                                pageSurface->fxZoneTemplateFolder = s_fxZoneTemplateFolder;

                                int index = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                                if (index >= 0)
                                {
                                    s_pages[index]->surfaces.push_back(pageSurface);
                                    AddListEntry(hwndDlg, s_pageSurfaceName, IDC_LIST_PageSurfaces);
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_SETCURSEL, s_pages[index]->surfaces.size() - 1, 0);
                                }
                            }
                        }
                        break ;

                    case IDC_BUTTON_EditSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Surfaces, LB_GETCURSEL, 0, 0);
                            if(index >= 0)
                            {
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_GETTEXT, index, (LPARAM)(LPCTSTR)(s_surfaceName.c_str()));
                                s_surfaceInPort = s_surfaces[index]->inPort;
                                s_surfaceOutPort = s_surfaces[index]->outPort;
                                s_surfaceRemoteDeviceIP = s_surfaces[index]->remoteDeviceIP;

                                s_dlgResult = false;
                                s_editMode = true;
                                
                                if(s_surfaces[index]->type == s_MidiSurfaceToken)
                                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_MidiSurface), hwndDlg, dlgProcMidiSurface);
                                else if(s_surfaces[index]->type == s_OSCSurfaceToken)
                                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_OSCSurface), hwndDlg, dlgProcOSCSurface);
                                                               
                                if(s_dlgResult == IDOK)
                                {
                                    s_surfaces[index]->name = s_surfaceName;
                                    s_surfaces[index]->remoteDeviceIP = s_surfaceRemoteDeviceIP;
                                    s_surfaces[index]->inPort = s_surfaceInPort;
                                    s_surfaces[index]->outPort = s_surfaceOutPort;
                                }
                                
                                s_editMode = false;
                            }
                        }
                        break ;
                    
                    case IDC_BUTTON_EditPage:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                            if(index >= 0)
                            {
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_GETTEXT, index, (LPARAM)(LPCTSTR)(s_surfaceName.c_str()));

                                s_dlgResult = false;
                                s_editMode = true;
                                
                                s_followMCP = s_pages[index]->followMCP;
                                s_synchPages = s_pages[index]->synchPages;
                                s_isScrollLinkEnabled = s_pages[index]->isScrollLinkEnabled;
                                s_scrollSynch = s_pages[index]->scrollSynch;
                                
                                DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_Page), hwndDlg, dlgProcPage);
                                if(s_dlgResult == IDOK)
                                {
                                    s_pages[index]->name = s_surfaceName;
                                    s_pages[index]->followMCP = s_followMCP;
                                    s_pages[index]->synchPages = s_synchPages;
                                    s_pages[index]->isScrollLinkEnabled = s_isScrollLinkEnabled;
                                    s_pages[index]->scrollSynch = s_scrollSynch;

                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_RESETCONTENT, 0, 0);
                                    for(auto page :  s_pages)
                                        AddListEntry(hwndDlg, page->name, IDC_LIST_Pages);
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_SETCURSEL, index, 0);
                                }
                                
                                s_editMode = false;
                            }
                        }
                        break ;
                        
                    case IDC_BUTTON_Advanced:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_Broadcast), hwndDlg, dlgProcBroadcast);
                        }
                        break;
                        
                    case IDC_BUTTON_EditPageSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_PageSurfaces, LB_GETCURSEL, 0, 0);
                            if(index >= 0)
                            {
                                s_dlgResult = false;
                                s_editMode = true;
                                
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_GETTEXT, index, (LPARAM)(LPCTSTR)(s_pageSurfaceName.c_str()));

                                int pageIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                                if(pageIndex >= 0)
                                {
                                    s_numChannels = s_pages[pageIndex]->surfaces[index]->numChannels;
                                    s_channelOffset = s_pages[pageIndex]->surfaces[index]->channelOffset;
                                    
                                    s_templateFilename = s_pages[pageIndex]->surfaces[index]->templateFilename;
                                    s_zoneTemplateFolder  = s_pages[pageIndex]->surfaces[index]->zoneTemplateFolder;
                                    s_fxZoneTemplateFolder = s_pages[pageIndex]->surfaces[index]->fxZoneTemplateFolder;

                                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_PageSurface), hwndDlg, dlgProcPageSurface);
                                    
                                    if(s_dlgResult == IDOK)
                                    {
                                        s_pages[pageIndex]->surfaces[index]->numChannels = s_numChannels;
                                        s_pages[pageIndex]->surfaces[index]->channelOffset = s_channelOffset;
                                        s_pages[pageIndex]->surfaces[index]->templateFilename = s_templateFilename;
                                        s_pages[pageIndex]->surfaces[index]->zoneTemplateFolder = s_zoneTemplateFolder;
                                        s_pages[pageIndex]->surfaces[index]->fxZoneTemplateFolder = s_fxZoneTemplateFolder;
                                    }
                                }
                                
                                s_editMode = false;
                            }
                        }
                        break ;
                        

                    case IDC_BUTTON_RemoveSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Surfaces, LB_GETCURSEL, 0, 0);
                            if(index >= 0)
                            {
                                s_surfaces.erase(s_surfaces.begin() + index);
                                
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_RESETCONTENT, 0, 0);
                                for(auto surface: s_surfaces)
                                    AddListEntry(hwndDlg, surface->name, IDC_LIST_Surfaces);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL, index, 0);
                            }
                        }
                        break ;
                        
                    case IDC_BUTTON_RemovePage:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                            if(index >= 0)
                            {
                                s_pages.erase(s_pages.begin() + index);
                                
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_RESETCONTENT, 0, 0);
#ifdef WIN32
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_RESETCONTENT, 0, 0);
#endif
                                for(auto page: s_pages)
                                    AddListEntry(hwndDlg, page->name, IDC_LIST_Pages);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_SETCURSEL, index, 0);
                            }
                        }
                        break ;

                    case IDC_BUTTON_RemovePageSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int pageIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                            if(pageIndex >= 0)
                            {
                                int index = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_PageSurfaces, LB_GETCURSEL, 0, 0);
                                if(index >= 0)
                                {
                                    s_pages[pageIndex]->surfaces.erase(s_pages[pageIndex]->surfaces.begin() + index);
                                    
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_RESETCONTENT, 0, 0);

                                    for(auto surface : s_pages[pageIndex]->surfaces)
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
            s_surfaces.clear();
            s_pages.clear();
            
            string iniFilePath = string(DAW::GetResourcePath()) + "/CSI/CSI.ini";
            
            ifstream iniFile(iniFilePath);
            
            int lineNumber = 0;
            
            for (string line; getline(iniFile, line) ; )
            {
                TrimLine(line);
                
                lineNumber++;
                
                if(lineNumber == 1)
                {
                    if(line != s_MajorVersionToken)
                    {
                        MessageBox(g_hwnd, ("Version mismatch -- Your CSI.ini file is not " + s_MajorVersionToken).c_str(), ("This is CSI " + s_MajorVersionToken).c_str(), MB_OK);
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
                    
                    if(tokens[0] == s_MidiSurfaceToken || tokens[0] == s_OSCSurfaceToken)
                    {
                        shared_ptr<SurfaceLine> surface = make_shared<SurfaceLine>();
                        
                        surface->type = tokens[0];
                        surface->name = tokens[1];
                        
                        if((surface->type == s_MidiSurfaceToken || surface->type == s_OSCSurfaceToken) && (tokens.size() == 4 || tokens.size() == 5))
                        {
                            surface->inPort = atoi(tokens[2].c_str());
                            surface->outPort = atoi(tokens[3].c_str());
                            if(surface->type == s_OSCSurfaceToken && tokens.size() == 5)
                                surface->remoteDeviceIP = tokens[4];
                        }
                        
                        s_surfaces.push_back(surface);
                        
                        AddListEntry(hwndDlg, surface->name, IDC_LIST_Surfaces);
                    }
                    else if(tokens[0] == s_PageToken && tokens.size() > 1)
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
 
                        s_pages.push_back(page);
                        
                        AddListEntry(hwndDlg, page->name, IDC_LIST_Pages);
                    }
                    else if(tokens[0] == "Broadcaster" && tokens.size() == 2 && s_pages.size() > 0)
                    {
                        shared_ptr<Broadcaster> broadcaster = make_shared<Broadcaster>();
                        broadcaster->name = tokens[1];
                        s_pages.back()->broadcasters.push_back(broadcaster);
                    }
                    else if(tokens[0] == "Listener" && tokens.size() == 3 && s_pages.size() > 0 && s_pages.back()->broadcasters.size() > 0)
                    {
                        shared_ptr<Listener> listener = make_shared<Listener>();
                        listener->name = tokens[1];

                        vector<string> categoryTokens;
                        GetTokens(categoryTokens, tokens[2]);
                        
                        for(auto categoryToken : categoryTokens)
                        {
                            if(categoryToken == "GoHome")
                                listener->goHome = true;
                            if(categoryToken == "Sends")
                                listener->sends = true;
                            if(categoryToken == "Receives")
                                listener->receives = true;
                            if(categoryToken == "FocusedFX")
                                listener->focusedFX = true;
                            if(categoryToken == "FocusedFXParam")
                                listener->focusedFXParam = true;
                            if(categoryToken == "FXMenu")
                                listener->fxMenu = true;
                            if(categoryToken == "LocalFXSlot")
                                listener->localFXSlot = true;
                            if(categoryToken == "Modifiers")
                                listener->modifiers = true;
                            if(categoryToken == "SelectedTrackFX")
                                listener->selectedTrackFX = true;
                            if(categoryToken == "Custom")
                                listener->custom = true;
                        }
                        
                        s_pages.back()->broadcasters.back()->listeners.push_back(listener);
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
                        
                        if (! s_pages.empty())
                        {
                            s_pages.back()->surfaces.push_back(surface);
                            
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
          
            if(s_surfaces.size() > 0)
                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL, 0, 0);
            
            if(s_pages.size() > 0)
            {
                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_SETCURSEL, 0, 0);

                // the messages above don't trigger the user-initiated code, so pretend the user selected them
                SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_LIST_Pages, LBN_SELCHANGE), 0);
                
                if(s_pages[0]->surfaces.size() > 0)
                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_SETCURSEL, 0, 0);
            }
        }
        break;
        
        case WM_USER+1024:
        {
            ofstream iniFile(string(DAW::GetResourcePath()) + "/CSI/CSI.ini");

            if(iniFile.is_open())
            {
                iniFile << s_MajorVersionToken + GetLineEnding();
                
                iniFile << GetLineEnding();
                
                string line = "";
                
                for(auto surface : s_surfaces)
                {
                    line = surface->type + " ";
                    line += "\"" + surface->name + "\" ";
                    line += to_string(surface->inPort) + " ";
                    line += to_string(surface->outPort) + " ";

                    if(surface->type == s_OSCSurfaceToken)
                        line += surface->remoteDeviceIP;
                    
                    iniFile << line + GetLineEnding();
                }
                
                iniFile << GetLineEnding();
                
                for(auto page : s_pages)
                {
                    line = s_PageToken + " ";
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
                        line = "\t";
                        line += "\"" + surface->pageSurfaceName + "\" ";
                        line += to_string(surface->numChannels) + " " ;
                        line += to_string(surface->channelOffset) + " " ;
                        line += "\"" + surface->templateFilename + "\" ";
                        line += "\"" + surface->zoneTemplateFolder + "\" ";
                        line += "\"" + surface->fxZoneTemplateFolder + "\" ";

                        iniFile << line + GetLineEnding();
                    }
                    
                    iniFile << GetLineEnding();
                    
                    for(auto broadcaster : page->broadcasters)
                    {
                        if(broadcaster->listeners.size() == 0)
                            continue;
                        
                        iniFile << string("\tBroadcaster ") + "\"" + broadcaster->name + "\"" + GetLineEnding();
                        
                        for(auto listener : broadcaster->listeners)
                        {
                            string listenerCategories = "";
                            
                            if(listener->goHome)
                                listenerCategories += "GoHome ";
                            if(listener->sends)
                                listenerCategories += "Sends ";
                            if(listener->receives)
                                listenerCategories += "Receives ";
                            if(listener->focusedFX)
                                listenerCategories += "FocusedFX ";
                            if(listener->focusedFXParam)
                                listenerCategories += "FocusedFXParam ";
                            if(listener->fxMenu)
                                listenerCategories += "FXMenu ";
                            if(listener->localFXSlot)
                                listenerCategories += "LocalFXSlot ";
                            if(listener->modifiers)
                                listenerCategories += "Modifiers ";
                            if(listener->selectedTrackFX)
                                listenerCategories += "SelectedTrackFX ";
                            if(listener->custom)
                                listenerCategories += "Custom ";

                            iniFile << string("\t\tListener ") + "\"" + listener->name + "\" \"" + listenerCategories + "\"" + GetLineEnding();
                        }
                        
                        iniFile << GetLineEnding();
                    }
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
