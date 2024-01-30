//
//  control_surface_integrator_ui.cpp
//  reaper_csurf_integrator
//
//

#include "control_surface_integrator.h"
#include "control_surface_integrator_ui.h"
#include <memory>
#include "WDL/dirscan.h"
#include "WDL/wdlcstring.h"

void ShutdownOSCIO();
CSIManager *csiManager;
extern void TrimLine(string &line);
extern void GetParamStepsString(string &outputString, int numSteps);

extern int g_minNumParamSteps;
extern int g_maxNumParamSteps;
const char * const Control_Surface_Integrator = "Control Surface Integrator";
extern int g_registered_command_toggle_show_raw_surface_input;
extern int g_registered_command_toggle_show_surface_input;
extern int g_registered_command_toggle_show_surface_output;
extern int g_registered_command_toggle_show_FX_params;
extern int g_registered_command_toggle_write_FX_params;

bool hookCommandProc(int command, int flag)
{
    if (csiManager != nullptr)
    {
        if (command == g_registered_command_toggle_show_raw_surface_input)
        {
            csiManager->ToggleSurfaceRawInDisplay();
            return true;
        }
        else if (command == g_registered_command_toggle_show_surface_input)
        {
            csiManager->ToggleSurfaceInDisplay();
            return true;
        }
        else if (command == g_registered_command_toggle_show_surface_output)
        {
            csiManager->ToggleSurfaceOutDisplay();
            return true;
        }
        else if (command == g_registered_command_toggle_show_FX_params)
        {
            csiManager->ToggleFXParamsDisplay();
            return true;
        }
        else if (command == g_registered_command_toggle_write_FX_params)
        {
            csiManager->ToggleFXParamsWrite();
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
    // JF - CSIManager should be a member of CSurfIntegrator?
    if (!csiManager) csiManager = new CSIManager;
    csiManager->csurf_refcnt_++;
}

CSurfIntegrator::~CSurfIntegrator()
{
    if (csiManager && !--csiManager->csurf_refcnt_)
    {
        csiManager->Shutdown();
        delete csiManager;
        csiManager = NULL;
        ShutdownOSCIO();
    }
}

void CSurfIntegrator::OnTrackSelection(MediaTrack *trackid)
{
    if (csiManager)
        csiManager->OnTrackSelection(trackid);
}

void CSurfIntegrator::SetTrackListChange()
{
    if (csiManager)
        csiManager->OnTrackListChange();
}

int CSurfIntegrator::Extended(int call, void *parm1, void *parm2, void *parm3)
{
    if (call == CSURF_EXT_SUPPORTS_EXTENDED_TOUCH)
    {
        return 1;
    }
    
    if (call == CSURF_EXT_RESET)
    {
       if (csiManager)
           csiManager->Init();
    }
    
    if (call == CSURF_EXT_SETFXCHANGE)
    {
        // parm1=(MediaTrack*)track, whenever FX are added, deleted, or change order
        if (csiManager)
            csiManager->TrackFXListChanged((MediaTrack*)parm1);
    }
        
    if (call == CSURF_EXT_SETMIXERSCROLL)
    {
        if (csiManager)
        {
            MediaTrack *leftPtr = (MediaTrack *)parm1;
            
            int offset = DAW::CSurf_TrackToID(leftPtr, true);
            
            offset--;
            
            if (offset < 0)
                offset = 0;
                
            csiManager->SetTrackOffset(offset);
        }
    }
        
    return 1;
}

bool CSurfIntegrator::GetTouchState(MediaTrack *track, int touchedControl)
{
    return csiManager->GetTouchState(track, touchedControl);
}

void CSurfIntegrator::Run()
{
    if (csiManager)
        csiManager->Run();
}

const char *CSurfIntegrator::GetTypeString()
{
    return "CSI";
}

const char *CSurfIntegrator::GetDescString()
{
    return Control_Surface_Integrator;
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
static ZoneManager *s_zoneManager;
static vector<vector<string> > s_surfaceLayoutTemplate;
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
               
            if (s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].delta != "")
                SetDlgItemText(hwndDlg, IDC_EDIT_Delta, s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].delta.c_str());

            if (s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].rangeMinimum != "")
                SetDlgItemText(hwndDlg, IDC_EDIT_RangeMin, s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].rangeMinimum.c_str());

            if (s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].rangeMaximum != "")
                SetDlgItemText(hwndDlg, IDC_EDIT_RangeMax, s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].rangeMaximum.c_str());

            if (s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].deltas.size() > 0)
            {
                string deltas = "";
                
                for (int i = 0; i < (int)s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].deltas.size(); ++i)
                    deltas += s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].deltas[i] + " ";
                
                SetDlgItemText(hwndDlg, IDC_EDIT_DeltaValues, deltas.c_str());
            }

            if (s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].ticks.size() > 0)
            {
                string ticks = "";
                
                for (int i = 0; i < (int)s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].ticks.size(); ++i)
                    ticks += s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].ticks[i] + " ";
                
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
                        if (string(buf) != "")
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].delta = buf;

                        GetDlgItemText(hwndDlg, IDC_EDIT_RangeMin, buf, sizeof(buf));
                        if (string(buf) != "")
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].rangeMinimum = buf;

                        GetDlgItemText(hwndDlg, IDC_EDIT_RangeMax, buf, sizeof(buf));
                        if (string(buf) != "")
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].rangeMaximum = buf;

                        GetDlgItemText(hwndDlg, IDC_EDIT_DeltaValues, buf, sizeof(buf));
                        if (string(buf) != "")
                        {
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].deltas.clear();
                            vector<string> deltas;
                            GetTokens(deltas, buf);
                            for (int i = 0; i < (int)deltas.size(); ++i)
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].deltas.push_back(deltas[i]);
                        }

                        GetDlgItemText(hwndDlg, IDC_EDIT_TickValues, buf, sizeof(buf));
                        if (string(buf) != "")
                        {
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].ticks.clear();
                            vector<string> ticks;
                            GetTokens(ticks, buf);
                            for (int i = 0; i < (int)ticks.size(); ++i)
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[s_groupIndex].ticks.push_back(ticks[i]);
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

static const int s_paramNumEditControls[] = { IDC_FXParamNumEdit1, IDC_FXParamNumEdit2, IDC_FXParamNumEdit3 };
static const int s_widgetTypePickers[] = { IDC_PickWidgetType1, IDC_PickWidgetType2, IDC_PickWidgetType3 };
static const int s_ringStylePickers[] = { IDC_PickRingStyle1, IDC_PickRingStyle2, IDC_PickRingStyle3 };
static const int s_fixedTextEditControls[] = { IDC_FXParamNameEdit1, IDC_FXParamNameEdit2, IDC_FXParamNameEdit3 };
static const int s_fixedTextDisplayRowPickers[] = { IDC_FixedTextDisplayPickRow1 , IDC_FixedTextDisplayPickRow2, IDC_FixedTextDisplayPickRow3 };
static const int s_fixedTextDisplayFontLabels[] = { IDC_FixedTextDisplayFontLabel1, IDC_FixedTextDisplayFontLabel2, IDC_FixedTextDisplayFontLabel3 };
static const int s_fixedTextDisplayFontPickers[] = { IDC_FixedTextDisplayPickFont1, IDC_FixedTextDisplayPickFont2, IDC_FixedTextDisplayPickFont3 };
static const int s_paramValueDisplayRowPickers[] = { IDC_FXParamValueDisplayPickRow1 , IDC_FXParamValueDisplayPickRow2, IDC_FXParamValueDisplayPickRow3 };
static const int s_paramValueDisplayFontLabels[] = { IDC_FXParamValueDisplayFontLabel1, IDC_FXParamValueDisplayFontLabel2, IDC_FXParamValueDisplayFontLabel3 };
static const int s_paramValueDisplayFontPickers[] = { IDC_FXParamValueDisplayPickFont1, IDC_FXParamValueDisplayPickFont2, IDC_FXParamValueDisplayPickFont3 };

static const int s_stepPickers[] = { IDC_PickSteps1, IDC_PickSteps2, IDC_PickSteps3 };
static const int s_stepEditControls[] = { IDC_EditSteps1, IDC_EditSteps2, IDC_EditSteps3 };
static const int s_stepPrompts[] = { IDC_StepsPromptGroup1, IDC_StepsPromptGroup2, IDC_StepsPromptGroup3 };

static const int s_widgetRingColorBoxes[] = { IDC_FXParamRingColorBox1, IDC_FXParamRingColorBox2, IDC_FXParamRingColorBox3 };
static const int s_widgetRingColors[] = { IDC_FXParamRingColor1, IDC_FXParamRingColor2, IDC_FXParamRingColor3 };
static const int s_widgetRingIndicatorColorBoxes[] = { IDC_FXParamIndicatorColorBox1, IDC_FXParamIndicatorColorBox2, IDC_FXParamIndicatorColorBox3 };
static const int s_widgetRingIndicators[] = { IDC_FXParamIndicatorColor1, IDC_FXParamIndicatorColor2, IDC_FXParamIndicatorColor3 };
static const int s_fixedTextDisplayForegroundColors[] = { IDC_FixedTextDisplayForegroundColor1, IDC_FixedTextDisplayForegroundColor2, IDC_FixedTextDisplayForegroundColor3 };
static const int s_fixedTextDisplayForegroundColorBoxes[] = { IDC_FXFixedTextDisplayForegroundColorBox1, IDC_FXFixedTextDisplayForegroundColorBox2, IDC_FXFixedTextDisplayForegroundColorBox3 };
static const int s_fixedTextDisplayBackgroundColors[] = { IDC_FixedTextDisplayBackgroundColor1, IDC_FixedTextDisplayBackgroundColor2, IDC_FixedTextDisplayBackgroundColor3 };
static const int s_fixedTextDisplayBackgroundColorBoxes[] = { IDC_FXFixedTextDisplayBackgroundColorBox1, IDC_FXFixedTextDisplayBackgroundColorBox2, IDC_FXFixedTextDisplayBackgroundColorBox3 };
static const int s_fxParamDisplayForegroundColors[] = { IDC_FXParamDisplayForegroundColor1, IDC_FXParamDisplayForegroundColor2, IDC_FXParamDisplayForegroundColor3 };
static const int s_fxParamDisplayForegroundColorBoxes[] = { IDC_FXParamValueDisplayForegroundColorBox1, IDC_FXParamValueDisplayForegroundColorBox2, IDC_FXParamValueDisplayForegroundColorBox3 };
static const int s_fxParamDisplayBackgroundColors[] = { IDC_FXParamDisplayBackgroundColor1, IDC_FXParamDisplayBackgroundColor2, IDC_FXParamDisplayBackgroundColor3 };
static const int s_fxParamDisplayBackgroundColorBoxes[] = { IDC_FXParamValueDisplayBackgroundColorBox1, IDC_FXParamValueDisplayBackgroundColorBox2, IDC_FXParamValueDisplayBackgroundColorBox3 };


// for hide
static const int s_groupBoxes[] = { IDC_Group1, IDC_Group2, IDC_Group3 };
static const int s_fxParamGroupBoxes[] = { IDC_GroupFXParam1, IDC_GroupFXParam2, IDC_GroupFXParam3 };
static const int s_fixedTextDisplayGroupBoxes[] = { IDC_GroupFixedTextDisplay1 , IDC_GroupFixedTextDisplay2, IDC_GroupFixedTextDisplay3 };
static const int s_fxParamDisplayGroupBoxes[] = { IDC_GroupFXParamValueDisplay1 , IDC_GroupFXParamValueDisplay2, IDC_GroupFXParamValueDisplay3 };
static const int s_advancedButtons[] = { IDC_AdvancedGroup1 , IDC_AdvancedGroup2, IDC_AdvancedGroup3 };

static const int  *const s_baseControls[] =
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

static const int  *const s_fontControls[] =
{
    s_fixedTextDisplayFontLabels,
    s_fixedTextDisplayFontPickers,
    s_paramValueDisplayFontLabels,
    s_paramValueDisplayFontPickers
};

static const int  *const s_colorControls[] =
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
    for (int i = 0; i < NUM_ELEM(s_baseControls); ++i)
        for (int j = startIndex; j < endIndex; j++)
            ShowWindow(GetDlgItem(hwndDlg, s_baseControls[i][j]), show);
}

static void ShowFontControls(HWND hwndDlg, int startIndex, int endIndex, bool show)
{
    for (int i = 0; i < NUM_ELEM(s_fontControls); ++i)
        for (int j = startIndex; j < endIndex; j++)
            ShowWindow(GetDlgItem(hwndDlg, s_fontControls[i][j]), show);
}

static void ShowColorControls(HWND hwndDlg, int startIndex, int endIndex, bool show)
{
    for (int i = 0; i < NUM_ELEM(s_colorControls); ++i)
        for (int j = startIndex; j < endIndex; j++)
            ShowWindow(GetDlgItem(hwndDlg, s_colorControls[i][j]), show);
}

static unsigned int s_buttonColors[][3] =
{
    { IDC_FXParamRingColor1, IDC_FXParamRingColorBox1, 0xffffffff },
    { IDC_FXParamRingColor2, IDC_FXParamRingColorBox2, 0xffffffff },
    { IDC_FXParamRingColor3, IDC_FXParamRingColorBox3, 0xffffffff },
    { IDC_FXParamIndicatorColor1, IDC_FXParamIndicatorColorBox1, 0xffffffff },
    { IDC_FXParamIndicatorColor2, IDC_FXParamIndicatorColorBox2, 0xffffffff },
    { IDC_FXParamIndicatorColor3, IDC_FXParamIndicatorColorBox3, 0xffffffff },
    { IDC_FixedTextDisplayForegroundColor1, IDC_FXFixedTextDisplayForegroundColorBox1, 0xffffffff },
    { IDC_FixedTextDisplayForegroundColor2, IDC_FXFixedTextDisplayForegroundColorBox2, 0xffffffff },
    { IDC_FixedTextDisplayForegroundColor3, IDC_FXFixedTextDisplayForegroundColorBox3, 0xffffffff },
    { IDC_FixedTextDisplayBackgroundColor1, IDC_FXFixedTextDisplayBackgroundColorBox1, 0xffffffff },
    { IDC_FixedTextDisplayBackgroundColor2, IDC_FXFixedTextDisplayBackgroundColorBox2, 0xffffffff },
    { IDC_FixedTextDisplayBackgroundColor3, IDC_FXFixedTextDisplayBackgroundColorBox3, 0xffffffff },
    { IDC_FXParamDisplayForegroundColor1, IDC_FXParamValueDisplayForegroundColorBox1, 0xffffffff },
    { IDC_FXParamDisplayForegroundColor2, IDC_FXParamValueDisplayForegroundColorBox2, 0xffffffff },
    { IDC_FXParamDisplayForegroundColor3, IDC_FXParamValueDisplayForegroundColorBox3, 0xffffffff },
    { IDC_FXParamDisplayBackgroundColor1, IDC_FXParamValueDisplayBackgroundColorBox1, 0xffffffff },
    { IDC_FXParamDisplayBackgroundColor2, IDC_FXParamValueDisplayBackgroundColorBox2, 0xffffffff },
    { IDC_FXParamDisplayBackgroundColor3, IDC_FXParamValueDisplayBackgroundColorBox3, 0xffffffff }
};

static unsigned int &GetButtonColorForID(unsigned int id)
{
    for (int x = 0; x < NUM_ELEM(s_buttonColors); x ++)
        if (s_buttonColors[x][0] == id) return s_buttonColors[x][2];
    WDL_ASSERT(false);
    return s_buttonColors[0][2];
}

static void PopulateParamListView(HWND hwndParamList)
{
    ListView_DeleteAllItems(hwndParamList);
        
    LVITEM lvi;
    lvi.mask      = LVIF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvi.stateMask = 0;
    lvi.iSubItem  = 0;
    lvi.state     = 0;

    for (int i = 0; i < s_zoneDef.rawParams.size(); i++)
    {
        lvi.iItem = i;
        lvi.pszText = (char *)s_zoneDef.rawParams[i].c_str();
        
        ListView_InsertItem(hwndParamList, &lvi);
        
        
        int spaceBreak = (int)s_zoneDef.rawParams[i].find( " ");
          
        if (spaceBreak != -1)
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
            if (s_hasColors)
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwndDlg, &ps);
                
                for (int x = 0; x < NUM_ELEM(s_buttonColors); x ++)
                {
                    const int colorPicker = s_buttonColors[x][0];
                    // GAW TBD -- think of a more elegant way to do this :)
                    if (s_numGroups < 3 && ( colorPicker == s_widgetRingColors[2] ||
                                            colorPicker == s_widgetRingIndicators[2] ||
                                            colorPicker == s_fixedTextDisplayForegroundColors[2] ||
                                            colorPicker == s_fixedTextDisplayBackgroundColors[2] ||
                                            colorPicker == s_fxParamDisplayForegroundColors[2] ||
                                            colorPicker == s_fxParamDisplayBackgroundColors[2] ))
                            continue;

                    if (s_numGroups < 2 && ( colorPicker == s_widgetRingColors[1] ||
                                            colorPicker == s_widgetRingIndicators[1] ||
                                            colorPicker == s_fixedTextDisplayForegroundColors[1] ||
                                            colorPicker == s_fixedTextDisplayBackgroundColors[1] ||
                                            colorPicker == s_fxParamDisplayForegroundColors[1] ||
                                            colorPicker == s_fxParamDisplayBackgroundColors[1] ))
                            continue;

                    const int colorPicker2 = s_buttonColors[x][1];
                    const int colorValue = s_buttonColors[x][2];

                    HBRUSH brush = CreateSolidBrush(colorValue);
                    
                    RECT clientRect, windowRect;
                    POINT p;
                    GetClientRect(GetDlgItem(hwndDlg, colorPicker2), &clientRect);
                    GetWindowRect(GetDlgItem(hwndDlg, colorPicker2), &windowRect);
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
            
            ShowBaseControls(hwndDlg, 0, NUM_ELEM(s_groupBoxes), false );
            ShowFontControls(hwndDlg, 0, NUM_ELEM(s_groupBoxes), false);
            ShowColorControls(hwndDlg, 0, NUM_ELEM(s_groupBoxes), false);

            SetWindowText(hwndDlg, (s_zoneDef.fxAlias + "   " + s_layoutTemplates[s_fxListIndex].modifiers + s_layoutTemplates[s_fxListIndex].suffix).c_str());

            for (int i = 0; i <NUM_ELEM( s_stepPickers); i++)
            {
                SendDlgItemMessage(hwndDlg, s_stepPickers[i], CB_ADDSTRING, 0, (LPARAM)"Custom");
                
                for (int j = g_minNumParamSteps; j <= g_maxNumParamSteps; j++)
                    SendDlgItemMessage(hwndDlg, s_stepPickers[i], CB_ADDSTRING, 0, (LPARAM)to_string(j).c_str());
            }
                                      
            PopulateParamListView(GetDlgItem(hwndDlg, IDC_AllParams));
            
            for (int i = 0; i < (int)s_surfaceLayoutTemplate.size(); ++i)
            {
                if (s_surfaceLayoutTemplate[i].size() > 0 )
                {
                    if (s_surfaceLayoutTemplate[i][0] == "WidgetTypes")
                    {
                        for (int j = 0; j < NUM_ELEM(s_widgetTypePickers); j++)
                            for (int k = 1; k < s_surfaceLayoutTemplate[i].size(); k++)
                                SendDlgItemMessage(hwndDlg, s_widgetTypePickers[j], CB_ADDSTRING, 0, (LPARAM)s_surfaceLayoutTemplate[i][k].c_str());
                    }
                    else if (s_surfaceLayoutTemplate[i][0] == "RingStyles")
                    {
                        for (int j = 0; j < NUM_ELEM(s_ringStylePickers); j++)
                            for (int k = 1; k < s_surfaceLayoutTemplate[i].size(); k++)
                                SendDlgItemMessage(hwndDlg, s_ringStylePickers[j], CB_ADDSTRING, 0, (LPARAM)s_surfaceLayoutTemplate[i][k].c_str());
                    }
                    else if (s_surfaceLayoutTemplate[i][0] == "DisplayRows")
                    {
                        for (int j = 0; j < NUM_ELEM(s_fixedTextDisplayRowPickers); j++)
                        {
                            SendDlgItemMessage(hwndDlg, s_fixedTextDisplayRowPickers[j], CB_ADDSTRING, 0, (LPARAM)"");

                            for (int k = 1; k < s_surfaceLayoutTemplate[i].size(); k++)
                                SendDlgItemMessage(hwndDlg, s_fixedTextDisplayRowPickers[j], CB_ADDSTRING, 0, (LPARAM)s_surfaceLayoutTemplate[i][k].c_str());
                        }
                        
                        for (int j = 0; j < NUM_ELEM(s_paramValueDisplayRowPickers); j++)
                        {
                            SendDlgItemMessage(hwndDlg, s_paramValueDisplayRowPickers[j], CB_ADDSTRING, 0, (LPARAM)"");

                            for (int k = 1; k < s_surfaceLayoutTemplate[i].size(); k++)
                                SendDlgItemMessage(hwndDlg, s_paramValueDisplayRowPickers[j], CB_ADDSTRING, 0, (LPARAM)s_surfaceLayoutTemplate[i][k].c_str());
                        }
                    }
                    else if (s_surfaceLayoutTemplate[i][0] == "DisplayFonts")
                    {
                        s_hasFonts = true;
                        
                        for (int j = 0; j < NUM_ELEM(s_fixedTextDisplayFontPickers); j++)
                            for (int k = 1; k < s_surfaceLayoutTemplate[i].size(); k++)
                                SendDlgItemMessage(hwndDlg, s_fixedTextDisplayFontPickers[j], CB_ADDSTRING, 0, (LPARAM)s_surfaceLayoutTemplate[i][k].c_str());
                        
                        for (int j = 0; j < NUM_ELEM(s_paramValueDisplayFontPickers); j++)
                            for (int k = 1; k < s_surfaceLayoutTemplate[i].size(); k++)
                                SendDlgItemMessage(hwndDlg, s_paramValueDisplayFontPickers[j], CB_ADDSTRING, 0, (LPARAM)s_surfaceLayoutTemplate[i][k].c_str());
                    }
                }
            }

            for (int i = 0; i < s_zoneDef.paramDefs[s_fxListIndex].definitions.size() && i < NUM_ELEM(s_paramNumEditControls); i++)
            {
                SetDlgItemText(hwndDlg, s_paramNumEditControls[i], s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNumber.c_str());
                SetDlgItemText(hwndDlg, s_fixedTextEditControls[i], s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramName.c_str());

                SetDlgItemText(hwndDlg, s_widgetTypePickers[i], s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidget.c_str());
                SetDlgItemText(hwndDlg, s_fixedTextDisplayRowPickers[i], s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidget.c_str());
                SetDlgItemText(hwndDlg, s_paramValueDisplayRowPickers[i], s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidget.c_str());

                const char *ringstyle = s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidgetProperties.get_prop(PropertyType_RingStyle);
                if (ringstyle)
                    SetDlgItemText(hwndDlg, s_ringStylePickers[i], ringstyle);
                
                const char *font = s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidgetProperties.get_prop(PropertyType_Font);
                if (font)
                    SetDlgItemText(hwndDlg, s_fixedTextDisplayFontPickers[i], font);

                font = s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidgetProperties.get_prop(PropertyType_Font);
                if (font)
                    SetDlgItemText(hwndDlg, s_paramValueDisplayFontPickers[i], font);

                const char *ringcolor = s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidgetProperties.get_prop(PropertyType_LEDRingColor);
                if (ringcolor)
                {
                    s_hasColors = true;
                    rgba_color color = GetColorValue(ringcolor);
                    GetButtonColorForID(s_widgetRingColors[i]) = DAW::ColorToNative(color.r, color.g, color.b);
                }

                const char *pushcolor = s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidgetProperties.get_prop(PropertyType_PushColor);
                if (pushcolor)
                {
                    s_hasColors = true;
                    rgba_color color = GetColorValue(pushcolor);
                    GetButtonColorForID(s_widgetRingIndicators[i]) = DAW::ColorToNative(color.r, color.g, color.b);
                }

                const char *foreground = s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidgetProperties.get_prop(PropertyType_Foreground);
                if (foreground)
                {
                    s_hasColors = true;
                    rgba_color color = GetColorValue(foreground);
                    GetButtonColorForID(s_fixedTextDisplayForegroundColors[i]) = DAW::ColorToNative(color.r, color.g, color.b);
                }

                const char *background = s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidgetProperties.get_prop(PropertyType_Background);
                if (background)
                {
                    s_hasColors = true;
                    rgba_color color = GetColorValue(background);
                    GetButtonColorForID(s_fixedTextDisplayBackgroundColors[i]) = DAW::ColorToNative(color.r, color.g, color.b);
                }

                foreground = s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidgetProperties.get_prop(PropertyType_Foreground);
                if (foreground)
                {
                    s_hasColors = true;
                    rgba_color color = GetColorValue(foreground);
                    GetButtonColorForID(s_fxParamDisplayForegroundColors[i]) = DAW::ColorToNative(color.r, color.g, color.b);
                }

                background = s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidgetProperties.get_prop(PropertyType_Background);
                if (background)
                {
                    s_hasColors = true;
                    rgba_color color = GetColorValue(background);
                    GetButtonColorForID(s_fxParamDisplayBackgroundColors[i]) = DAW::ColorToNative(color.r, color.g, color.b);
                }

                string steps = "";
                
                for (int j = 0; j < (int)s_zoneDef.paramDefs[s_fxListIndex].definitions[i].steps.size(); ++i)
                    steps += s_zoneDef.paramDefs[s_fxListIndex].definitions[i].steps[j] + "  ";
                
                SetDlgItemText(hwndDlg, s_stepEditControls[i], steps.c_str());
                
                char buf[BUFSZ];
                
                GetDlgItemText(hwndDlg, s_widgetTypePickers[i], buf, sizeof(buf));
                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidget = buf;

                if (!strcmp(buf,"RotaryPush") && steps == "")
                {
                    SetDlgItemText(hwndDlg, s_stepEditControls[i], "0  1");
                    SetDlgItemText(hwndDlg, s_stepPickers[i], "2");
                }
            }
            
            ShowBaseControls(hwndDlg, 0, s_numGroups, true);
            
            if (s_hasFonts)
                ShowFontControls(hwndDlg, 0, s_numGroups, true);
            
            if (s_hasColors)
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
                case IDC_FXParamRingColor2:
                case IDC_FXParamRingColor3:
                case IDC_FXParamIndicatorColor1:
                case IDC_FXParamIndicatorColor2:
                case IDC_FXParamIndicatorColor3:
                case IDC_FixedTextDisplayForegroundColor1:
                case IDC_FixedTextDisplayForegroundColor2:
                case IDC_FixedTextDisplayForegroundColor3:
                case IDC_FixedTextDisplayBackgroundColor1:
                case IDC_FixedTextDisplayBackgroundColor2:
                case IDC_FixedTextDisplayBackgroundColor3:
                case IDC_FXParamDisplayForegroundColor1:
                case IDC_FXParamDisplayForegroundColor2:
                case IDC_FXParamDisplayForegroundColor3:
                case IDC_FXParamDisplayBackgroundColor1:
                case IDC_FXParamDisplayBackgroundColor2:
                case IDC_FXParamDisplayBackgroundColor3:
                    {
                        DAW::GR_SelectColor(hwndDlg, (int *)&GetButtonColorForID(LOWORD(wParam)));
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
                        for (int i = 0; i < s_zoneDef.paramDefs[s_fxListIndex].definitions.size(); i++)
                        {
                            char buf[BUFSZ];
                            
                            GetDlgItemText(hwndDlg, s_paramNumEditControls[i], buf, sizeof(buf));
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNumber = buf;

                            GetDlgItemText(hwndDlg, s_widgetTypePickers[i], buf, sizeof(buf));
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidget = buf;
                            
                            GetDlgItemText(hwndDlg, s_ringStylePickers[i], buf, sizeof(buf));
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidgetProperties.set_prop(PropertyType_RingStyle, buf);
                            
                            GetDlgItemText(hwndDlg, s_fixedTextEditControls[i], buf, sizeof(buf));
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramName = buf;

                            GetDlgItemText(hwndDlg, s_fixedTextDisplayRowPickers[i], buf, sizeof(buf));
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidget = buf;

                            GetDlgItemText(hwndDlg, s_paramValueDisplayRowPickers[i], buf, sizeof(buf));
                            s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidget = buf;

                            GetDlgItemText(hwndDlg, s_stepEditControls[i], buf, sizeof(buf));
                            
                            if (string(buf) != "")
                            {
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].steps.clear();
                                vector<string> steps;
                                GetTokens(steps, buf);
                                for (int j = 0; j< (int)steps.size(); ++j)
                                    s_zoneDef.paramDefs[s_fxListIndex].definitions[i].steps.push_back(steps[j]);
                            }

                            if (s_hasFonts)
                            {
                                GetDlgItemText(hwndDlg, s_fixedTextDisplayFontPickers[i], buf, sizeof(buf));
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidgetProperties.set_prop(PropertyType_Font, buf);
                                
                                GetDlgItemText(hwndDlg, s_paramValueDisplayFontPickers[i], buf, sizeof(buf));
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidgetProperties.set_prop(PropertyType_Font, buf);
                            }
                            
                            if (s_hasColors)
                            {
                                rgba_color color;
                                char tmp[32];
                                
                                DAW::ColorFromNative(GetButtonColorForID(s_widgetRingColors[i]), &color.r, &color.g, &color.b);
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidgetProperties.set_prop(PropertyType_LEDRingColor, color.to_string(tmp));
                                
                                DAW::ColorFromNative(GetButtonColorForID(s_widgetRingIndicators[i]), &color.r, &color.g, &color.b);
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramWidgetProperties.set_prop(PropertyType_PushColor, color.to_string(tmp));

                                DAW::ColorFromNative(GetButtonColorForID(s_fixedTextDisplayForegroundColors[i]), &color.r, &color.g, &color.b);
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidgetProperties.set_prop(PropertyType_Foreground, color.to_string(tmp));

                                DAW::ColorFromNative(GetButtonColorForID(s_fixedTextDisplayBackgroundColors[i]), &color.r, &color.g, &color.b);
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramNameDisplayWidgetProperties.set_prop(PropertyType_Background, color.to_string(tmp));

                                DAW::ColorFromNative(GetButtonColorForID(s_fxParamDisplayForegroundColors[i]), &color.r, &color.g, &color.b);
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidgetProperties.set_prop(PropertyType_Foreground, color.to_string(tmp));

                                DAW::ColorFromNative(GetButtonColorForID(s_fxParamDisplayBackgroundColors[i]), &color.r, &color.g, &color.b);
                                s_zoneDef.paramDefs[s_fxListIndex].definitions[i].paramValueDisplayWidgetProperties.set_prop(PropertyType_Background, color.to_string(tmp));
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
                            if (index >= 0)
                            {
                                if (index == 0)
                                    SetDlgItemText(hwndDlg, IDC_EditSteps1, "");
                                else
                                {
                                    string steps;
                                    GetParamStepsString(steps, index + 1);
                                    SetDlgItemText(hwndDlg, IDC_EditSteps1, steps.c_str());
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
                            if (index >= 0)
                            {
                                if (index == 0)
                                    SetDlgItemText(hwndDlg, IDC_EditSteps2, "");
                                else
                                {
                                    string steps;
                                    GetParamStepsString(steps, index + 1);
                                    SetDlgItemText(hwndDlg, IDC_EditSteps2, steps.c_str());
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
                            if (index >= 0)
                            {
                                if (index == 0)
                                    SetDlgItemText(hwndDlg, IDC_EditSteps3, "");
                                else
                                {
                                    string steps;
                                    GetParamStepsString(steps, index + 1);
                                    SetDlgItemText(hwndDlg, IDC_EditSteps3, steps.c_str());
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
    
    for (int i = 0; i < (int)s_zoneDef.paramDefs[index].definitions.size(); ++i)
    {
        string widgetName = s_zoneDef.paramDefs[index].definitions[i].paramWidget;
        
        if (widgetName == "RotaryPush")
            widgetName = "Push";
        
        components.push_back(widgetName);
        
        string alias = s_zoneDef.paramDefs[index].definitions[i].paramName;

        if (s_zoneDef.paramDefs[index].definitions[i].paramName == "" && s_zoneDef.paramDefs[index].definitions[i].paramNumber != "" && s_zoneDef.rawParamsDictionary.count(s_zoneDef.paramDefs[index].definitions[i].paramNumber) > 0)
            alias = s_zoneDef.rawParamsDictionary[s_zoneDef.paramDefs[index].definitions[i].paramNumber];
        else if (s_zoneDef.paramDefs[index].definitions[i].paramNumber == "")
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
    
    if (shouldInsert)
        ListView_InsertItem(hwndParamList, &lvi);
    else
        ListView_SetItem(hwndParamList, &lvi);
    
    for (int i = 1; i < components.size(); i++)
    {
        lvi.iSubItem = i;
        lvi.pszText = (char *)components[i].c_str();

        ListView_SetItem(hwndParamList, &lvi);
    }
}

static void PopulateListView(HWND hwndParamList)
{
    ListView_DeleteAllItems(hwndParamList);
        
    for (int i = 0; i < s_zoneDef.paramDefs.size(); i++)
        SetListViewItem(hwndParamList, i, true);
}

static void MoveUp(HWND hwndParamList)
{
    int index = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
    if (index > 0)
    {
        FXParamDefinitions itemToMove;
        
        for (int i = 0; i < (int)s_zoneDef.paramDefs[index].definitions.size(); ++i)
            itemToMove.definitions.push_back(s_zoneDef.paramDefs[index].definitions[i]);
        
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
    if (index >= 0 && index < s_zoneDef.paramDefs.size() - 1)
    {
        FXParamDefinitions itemToMove;
        
        for (int i = 0; i < (int)s_zoneDef.paramDefs[index].definitions.size(); ++i)
            itemToMove.definitions.push_back(s_zoneDef.paramDefs[index].definitions[i]);

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
    
    if (index >= 0)
    {
        s_fxListIndex = index;
        DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_EditFXParam), g_hwnd, dlgProcEditFXParam);
        
        if (s_dlgResult == IDOK)
            SetListViewItem(hwndParamList, index, false);
        
        s_dlgResult = IDCANCEL;
    }
}

static bool DeleteZone()
{
    if (MessageBox(NULL, (string("This will permanently delete\n\n") + s_zoneDef.fxName + string(".zon\n\n Are you sure you want to permanently delete this file from disk? \n\nIf you delerte the file the RemapAutoZone dialog will close.")).c_str(), string("Delete " + s_zoneDef.fxAlias).c_str(), MB_YESNO) == IDNO)
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
            if (((LPNMHDR)lParam)->code == NM_DBLCLK)
                EditItem(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
            else if (((LPNMHDR)lParam)->code == LVN_BEGINDRAG)
            {
                HWND hwndParamList = GetDlgItem(hwndDlg, IDC_PARAM_LIST);
               
                POINT p;
                p.x = 8;
                p.y = 8;

                int iPos = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
                if (iPos != -1)
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
            
            vector<int> columnSizes;
            columnSizes.push_back(160);
            
            for (int i = 1; i <= s_numGroups; i++)
            {
                columnSizes.push_back(80);
                columnSizes.push_back(150);
            }
            
            LVCOLUMN columnDescriptor = { LVCF_TEXT | LVCF_WIDTH, 0, 0, (char*)"" };
            columnDescriptor.cx = columnSizes[0];
            
            ListView_InsertColumn(paramList, 0, &columnDescriptor);
            
            for (int i = 1; i <= s_numGroups  *2; i++)
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
            if (s_isDragging)
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
                
                for (int i = 0; i < (int)s_zoneDef.paramDefs[s_oldPosition].definitions.size(); ++i)
                    itemToMove.definitions.push_back(s_zoneDef.paramDefs[s_oldPosition].definitions[i]);
                
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
            if (s_isDragging)
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
                         if (DeleteZone())
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
            if (((LPNMHDR)lParam)->code == NM_DBLCLK)
                EditItem(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
            else if (((LPNMHDR)lParam)->code == LVN_BEGINDRAG)
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
            
            vector<int> columnSizes;
            columnSizes.push_back(65); // modifiers
            
            for (int i = 1; i <= s_numGroups; i++)
            {
                columnSizes.push_back(38); // widget
                columnSizes.push_back(75); // param name
            }
            
            LVCOLUMN columnDescriptor = { LVCF_TEXT | LVCF_WIDTH, LVCFMT_RIGHT, 0, (char*)"" };
            columnDescriptor.cx = columnSizes[0];
            
            ListView_InsertColumn(paramList, 0, &columnDescriptor);
            
            for (int i = 1; i <= s_numGroups  *2; i++)
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
            if (s_isDragging)
            {
                s_isDragging = false;
                ReleaseCapture();
            }
            break;
        }

        case WM_MOUSEMOVE:
        {
            if (s_isDragging)
            {
                POINT currentCursorPosition;
                GetCursorPos(&currentCursorPosition);
                
                if (lastCursorPosition.y > currentCursorPosition.y && lastCursorPosition.y - currentCursorPosition.y > 21)
                {
                    lastCursorPosition = currentCursorPosition;
                    
                    MoveDown(GetDlgItem(hwndDlg, IDC_PARAM_LIST));
                }
                else if (currentCursorPosition.y > lastCursorPosition.y && currentCursorPosition.y - lastCursorPosition.y > 21)
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
                        if (DeleteZone())
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

bool RemapAutoZoneDialog(ZoneManager *aZoneManager, string fullFilePath)
{
    s_zoneDef.Clear();
    s_zoneManager = aZoneManager;
    s_zoneDef.fullPath = fullFilePath;
    s_numGroups = s_zoneManager->GetNumGroups();
    s_layoutTemplates = s_zoneManager->GetFXLayoutTemplates();
    s_surfaceLayoutTemplate = s_zoneManager->GetSurfaceFXLayoutTemplate();
    
    s_zoneManager->UnpackZone(s_zoneDef, s_layoutTemplates);
    
    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_RemapAutoFX), g_hwnd, dlgProcRemapFXAutoZone);
    
    if (s_dlgResult == IDSAVE)
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
    static vector<string> GetDirectoryFilenames(const string &path)
    {
        vector<string> filenames;

        WDL_DirScan scan;
        
        if (!scan.First(path.c_str()))
            do {
              const char *ext = WDL_get_fileext(scan.GetCurrentFN());
              if (scan.GetCurrentFN()[0] != '.' && !scan.GetCurrentIsDirectory() && (!stricmp(ext,".mst") || !stricmp(ext,".ost")))
                filenames.push_back(std::string(scan.GetCurrentFN()));
            } while (!scan.Next());
        
        return filenames;
    }
    
    static vector<string> GetDirectoryFolderNames(const string &path)
    {
        vector<string> folderNames;

        WDL_DirScan scan;
        
        if (!scan.First(path.c_str()))
        {
            do {
              if (scan.GetCurrentFN()[0] != '.' && scan.GetCurrentIsDirectory())
                folderNames.push_back(std::string(scan.GetCurrentFN()));
            } while (!scan.Next());
        }
        
        return folderNames;
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////
// structs
////////////////////////////////////////////////////////////////////////////////////////////////////////
struct SurfaceLine
{
    string type ;
    string name;
    int inPort;
    int outPort;
    string remoteDeviceIP;
    
    SurfaceLine()
    {
        type = "";
        name = "";
        inPort = 0;
        outPort = 0;
        remoteDeviceIP = "";
    }
};

static WDL_PtrList<SurfaceLine> s_surfaces;

struct PageSurfaceLine
{
    string pageSurfaceName;
    int numChannels;
    int channelOffset;
    string templateFilename;
    string zoneTemplateFolder;
    string fxZoneTemplateFolder;
    
    PageSurfaceLine()
    {
        pageSurfaceName = "";
        numChannels = 0;
        channelOffset = 0;
        templateFilename = "";
        zoneTemplateFolder = "";
        fxZoneTemplateFolder = "";
    }
};

// Broadcast/Listen
struct Listener
{
    string name;
    bool goHome;
    bool sends;
    bool receives;
    bool focusedFX;
    bool focusedFXParam;
    bool fxMenu;
    bool localFXSlot;
    bool selectedTrackFX;
    bool custom;
    bool modifiers;
    
    Listener()
    {
        name = "";
        goHome = false;
        sends = false;
        receives = false;
        focusedFX = false;
        focusedFXParam = false;
        fxMenu = false;
        localFXSlot = false;
        selectedTrackFX = false;
        custom = false;
        modifiers = false;
    }
};

struct Broadcaster
{
    string name;
    WDL_PtrList<Listener> listeners;
    
    Broadcaster()
    {
        name = "";
    }
};

struct PageLine
{
    string name;
    bool followMCP;
    bool synchPages;
    bool isScrollLinkEnabled;
    bool scrollSynch;
    WDL_PtrList<PageSurfaceLine> surfaces;
    WDL_PtrList<Broadcaster> broadcasters;
    
    PageLine()
    {
        name = "";
        followMCP = true;
        synchPages = true;
        isScrollLinkEnabled = false;
        scrollSynch = false;
    }
};

// Scratch pad to get in and out of dialogs easily
static WDL_PtrList<Broadcaster> s_broadcasters;

static void TransferBroadcasters(WDL_PtrList<Broadcaster> &source, WDL_PtrList<Broadcaster> &destination)
{
    destination.Empty(true);
    
    for (int i = 0; i < source.GetSize(); ++i)
    {
        Broadcaster *destinationBroadcaster = new Broadcaster();
        
        destinationBroadcaster->name = source.Get(i)->name;
        
        for (int j = 0; j < source.Get(i)->listeners.GetSize(); ++j)
        {
            Listener *destinationListener = new Listener();
            
            destinationListener->name = source.Get(i)->listeners.Get(j)->name;
            
            destinationListener->goHome = source.Get(i)->listeners.Get(j)->goHome;
            destinationListener->sends = source.Get(i)->listeners.Get(j)->sends;
            destinationListener->receives = source.Get(i)->listeners.Get(j)->receives;
            destinationListener->focusedFX = source.Get(i)->listeners.Get(j)->focusedFX;
            destinationListener->focusedFXParam = source.Get(i)->listeners.Get(j)->focusedFXParam;
            destinationListener->fxMenu = source.Get(i)->listeners.Get(j)->fxMenu;
            destinationListener->localFXSlot = source.Get(i)->listeners.Get(j)->localFXSlot;
            destinationListener->modifiers = source.Get(i)->listeners.Get(j)->modifiers;
            destinationListener->selectedTrackFX = source.Get(i)->listeners.Get(j)->selectedTrackFX;
            destinationListener->custom = source.Get(i)->listeners.Get(j)->custom;
            
            destinationBroadcaster->listeners.Add(destinationListener);
        }
        
        destination.Add(destinationBroadcaster);
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

static WDL_PtrList<PageLine> s_pages;

void AddComboEntry(HWND hwndDlg, int x, char  *buf, int comboId)
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
            if (s_editMode)
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
                        
                        if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_TCP))
                           s_followMCP = false;
                        else if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_MCP))
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
    
    for (int i = 0; i < s_surfaces.GetSize(); ++i)
    {
        if (s_surfaces.Get(i)->name == string(buf))
        {
            if (s_surfaces.Get(i)->type == s_MidiSurfaceToken)
                for (int j = 0; j < (int)FileSystem::GetDirectoryFilenames(resourcePath + "/CSI/Surfaces/Midi/").size(); ++j)
                    AddComboEntry(hwndDlg, 0, (char*)FileSystem::GetDirectoryFilenames(resourcePath + "/CSI/Surfaces/Midi/")[j].c_str(), IDC_COMBO_SurfaceTemplate);

            if (s_surfaces.Get(i)->type == s_OSCSurfaceToken)
                for (int j = 0; j < (int)FileSystem::GetDirectoryFilenames(resourcePath + "/CSI/Surfaces/OSC/").size(); ++j)
                    AddComboEntry(hwndDlg, 0, (char*)FileSystem::GetDirectoryFilenames(resourcePath + "/CSI/Surfaces/OSC/")[j].c_str(), IDC_COMBO_SurfaceTemplate);
            
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
            if (s_editMode)
            {
                AddComboEntry(hwndDlg, 0, (char *)s_pageSurfaceName.c_str(), IDC_COMBO_PageSurface);
                SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_PageSurface), CB_SETCURSEL, 0, 0);

                SetDlgItemInt(hwndDlg, IDC_EDIT_NumChannels, s_numChannels, false);
                SetDlgItemInt(hwndDlg, IDC_EDIT_ChannelOffset, s_channelOffset, false);
               
                PopulateSurfaceTemplateCombo(hwndDlg, resourcePath);
                
                for (int i = 0; i < (int)FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/").size(); ++i)
                    AddComboEntry(hwndDlg, 0, (char *)FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/")[i].c_str(), IDC_COMBO_ZoneTemplates);

                for (int i = 0; i < (int)FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/").size(); ++i)
                    AddComboEntry(hwndDlg, 0, (char *)FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/")[i].c_str(), IDC_COMBO_FXZoneTemplates);

                int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_FINDSTRINGEXACT, -1, (LPARAM)s_templateFilename.c_str());
                if (index >= 0)
                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_SETCURSEL, index, 0);
                
                index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)s_zoneTemplateFolder.c_str());
                if (index >= 0)
                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_SETCURSEL, index, 0);
                
                index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_FXZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)s_fxZoneTemplateFolder.c_str());
                if (index >= 0)
                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_FXZoneTemplates), CB_SETCURSEL, index, 0);
            }
            else
            {
                for (int i = 0; i < s_surfaces.GetSize(); ++i)
                    AddComboEntry(hwndDlg, 0, (char *)s_surfaces.Get(i)->name.c_str(), IDC_COMBO_PageSurface);
                
                SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_PageSurface), CB_SETCURSEL, 0, 0);
                
                SetDlgItemText(hwndDlg, IDC_EDIT_NumChannels, "0");
                SetDlgItemText(hwndDlg, IDC_EDIT_ChannelOffset, "0");
                
                PopulateSurfaceTemplateCombo(hwndDlg, resourcePath);
                            
                for (int i = 0; i < (int)FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/").size(); ++i)
                    AddComboEntry(hwndDlg, 0, (char *)FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/")[i].c_str(), IDC_COMBO_ZoneTemplates);
                
                for (int i = 0; i < (int)FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/").size(); ++i)
                    AddComboEntry(hwndDlg, 0, (char *)FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/")[i].c_str(), IDC_COMBO_FXZoneTemplates);
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
                            if (index >= 0 && ! s_editMode)
                            {
                                char buffer[BUFSZ];
                                
                                GetDlgItemText(hwndDlg, IDC_COMBO_SurfaceTemplate, buffer, sizeof(buffer));
                                
                                for (int i = 0; i < sizeof(buffer); i++)
                                {
                                    if (buffer[i] == '.')
                                    {
                                        buffer[i] = 0;
                                        break;
                                    }
                                }
                                
                                index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)buffer);
                                if (index >= 0)
                                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_SETCURSEL, index, 0);
                            }
                        }
                    }
                    break;
                }
                    
                case IDC_COMBO_ZoneTemplates:
                {
                    switch (HIWORD(wParam))
                    {
                        case CBN_SELCHANGE:
                        {
                            int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_GETCURSEL, 0, 0);
                            if (index >= 0 && ! s_editMode)
                            {
                                char buffer[BUFSZ];
                                
                                GetDlgItemText(hwndDlg, IDC_COMBO_ZoneTemplates, buffer, sizeof(buffer));
                                
                                index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_FXZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)buffer);
                                if (index >= 0)
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
                    if (s_editMode && s_surfaceInPort == i)
                        SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_MidiIn), CB_SETCURSEL, currentIndex, 0);
                    currentIndex++;
                }
            
            currentIndex = 0;
            
            for (int i = 0; i < DAW::GetNumMIDIOutputs(); i++)
                if (DAW::GetMIDIOutputName(i, buf, sizeof(buf)))
                {
                    AddComboEntry(hwndDlg, i, buf, IDC_COMBO_MidiOut);
                    if (s_editMode && s_surfaceOutPort == i)
                        SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_MidiOut), CB_SETCURSEL, currentIndex, 0);
                    currentIndex++;
                }
            
            string resourcePath(DAW::GetResourcePath());
            
            if (s_editMode)
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
                            if (index >= 0 && !s_editMode)
                            {
                                char buffer[BUFSZ];
                                
                                GetDlgItemText(hwndDlg, IDC_COMBO_SurfaceTemplate, buffer, sizeof(buffer));

                                for (int i = 0; i < sizeof(buffer); i++)
                                {
                                    if (buffer[i] == '.')
                                    {
                                        buffer[i] = 0;
                                        break;
                                    }
                                }
                                
                                index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)buffer);
                                if (index >= 0)
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
            for (int j = 0; j < (int)FileSystem::GetDirectoryFilenames(resourcePath + "/CSI/Surfaces/OSC/").size(); ++j)
                AddComboEntry(hwndDlg, i++, (char*)FileSystem::GetDirectoryFilenames(resourcePath + "/CSI/Surfaces/OSC/")[j].c_str(), IDC_COMBO_SurfaceTemplate);
            
            for (int j = 0; j < (int)FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/").size(); ++j)
                AddComboEntry(hwndDlg, 0, (char *)FileSystem::GetDirectoryFolderNames(resourcePath + "/CSI/Zones/")[j].c_str(), IDC_COMBO_ZoneTemplates);
            
            if (s_editMode)
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
                            if (index >= 0 && !s_editMode)
                            {
                                char buffer[BUFSZ];
                                
                                GetDlgItemText(hwndDlg, IDC_COMBO_SurfaceTemplate, buffer, sizeof(buffer));
                                
                                for (int i = 0; i < sizeof(buffer); i++)
                                {
                                    if (buffer[i] == '.')
                                    {
                                        buffer[i] = 0;
                                        break;
                                    }
                                }
                                
                                index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)buffer);
                                if (index >= 0)
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

static void SetCheckBoxes(HWND hwndDlg, Listener *listener)
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
            for (int i = 0; i < s_surfaces.GetSize(); ++i)
                AddComboEntry(hwndDlg, 0, (char *)s_surfaces.Get(i)->name.c_str(), IDC_AddBroadcaster);
            SendMessage(GetDlgItem(hwndDlg, IDC_AddBroadcaster), CB_SETCURSEL, 0, 0);

            for (int i = 0; i < s_surfaces.GetSize(); ++i)
                AddComboEntry(hwndDlg, 0, (char *)s_surfaces.Get(i)->name.c_str(), IDC_AddListener);
            SendMessage(GetDlgItem(hwndDlg, IDC_AddListener), CB_SETCURSEL, 0, 0);
            
            TransferBroadcasters(s_pages.Get(s_pageIndex)->broadcasters, s_broadcasters);
            
            if (s_broadcasters.GetSize() > 0)
            {
                for (int i = 0; i < s_broadcasters.GetSize(); ++i)
                    AddListEntry(hwndDlg, s_broadcasters.Get(i)->name, IDC_LIST_Broadcasters);
                    
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
                           
                            for (int i = 0; i < s_broadcasters.Get(broadcasterIndex)->listeners.GetSize(); ++i)
                                AddListEntry(hwndDlg, s_broadcasters.Get(broadcasterIndex)->listeners.Get(i)->name, IDC_LIST_Listeners);
                            
                            if (s_broadcasters.GetSize() > 0)
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Listeners), LB_SETCURSEL, 0, 0);
                            
                            if (broadcasterIndex >= 0 && s_broadcasters.Get(broadcasterIndex)->listeners.GetSize() > 0)
                                SetCheckBoxes(hwndDlg, s_broadcasters.Get(broadcasterIndex)->listeners.Get(0));
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
                        
                        if (broadcasterIndex >= 0 && listenerIndex >= 0)
                            SetCheckBoxes(hwndDlg, s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex));
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
                            for (int i = 0; i < s_broadcasters.GetSize(); ++i)
                                if (broadcasterName == s_broadcasters.Get(i)->name)
                                    foundit = true;
                            if (! foundit)
                            {
                                Broadcaster *broadcaster = new Broadcaster();
                                broadcaster->name = broadcasterName;
                                s_broadcasters.Add(broadcaster);
                                AddListEntry(hwndDlg, broadcasterName, IDC_LIST_Broadcasters);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Broadcasters), LB_SETCURSEL, s_broadcasters.GetSize() - 1, 0);
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
                            for (int i = 0; i < s_broadcasters.Get(broadcasterIndex)->listeners.GetSize(); ++i)
                                if (listenerName == s_broadcasters.Get(broadcasterIndex)->listeners.Get(i)->name)
                                foundit = true;
                            if (! foundit)
                            {
                                Listener *listener = new Listener();
                                listener->name = listenerName;
                                 s_broadcasters.Get(broadcasterIndex)->listeners.Add(listener);
                                AddListEntry(hwndDlg, listenerName, IDC_LIST_Listeners);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Listeners), LB_SETCURSEL,  s_broadcasters.Get(broadcasterIndex)->listeners.GetSize() - 1, 0);
                                ClearCheckBoxes(hwndDlg);

                                SetWindowText(GetDlgItem(hwndDlg, IDC_ListenCheckboxes), string( s_broadcasters.Get(broadcasterIndex)->listeners.Get(s_broadcasters.Get(broadcasterIndex)->listeners.GetSize() - 1)->name + " Listens to").c_str());
                            }
                        }
                    }
                    break;
                 
                case IDC_CHECK_GoHome:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if (broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if (SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_GoHome), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                 s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->goHome = true;
                            else
                                 s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->goHome = false;
                        }
                    }
                    break;
                                        
                case IDC_CHECK_Sends:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if (broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if (SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Sends), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->sends = true;
                            else
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->sends = false;
                        }
                    }
                    break;
                    
                case IDC_CHECK_Receives:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex =(int) SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if (broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if (SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Receives), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->receives = true;
                            else
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->receives = false;
                        }
                    }
                    break;
                    
                case IDC_CHECK_FocusedFX:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if (broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if (SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FocusedFX), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->focusedFX = true;
                            else
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->focusedFX = false;
                        }
                    }
                    break;
                    
                case IDC_CHECK_FocusedFXParam:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if (broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if (SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FocusedFXParam), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->focusedFXParam = true;
                            else
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->focusedFXParam = false;
                        }
                    }
                    break;
                    
                case IDC_CHECK_FXMenu:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if (broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if (SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FXMenu), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->fxMenu = true;
                            else
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->fxMenu = false;
                        }
                    }
                    break;
                 
                case IDC_CHECK_LocalFXSlot:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if (broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if (SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_LocalFXSlot), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->localFXSlot = true;
                            else
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->localFXSlot = false;
                        }
                    }
                    break;
                 
                case IDC_CHECK_Modifiers:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if (broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if (SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Modifiers), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->modifiers = true;
                            else
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->modifiers = false;
                        }
                    }
                    break;
                 
                case IDC_CHECK_SelectedTrackFX:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if (broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if (SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_SelectedTrackFX), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->selectedTrackFX = true;
                            else
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->selectedTrackFX = false;
                        }
                    }
                    break;
                 
                case IDC_CHECK_Custom:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if (broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            if (SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Custom), BM_GETCHECK, 0, 0) == BST_CHECKED)
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->custom = true;
                            else
                                s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex)->custom = false;
                        }
                    }
                    break;
                 
                case ID_RemoveBroadcaster:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);

                        if (broadcasterIndex >= 0)
                        {
                            s_broadcasters.Delete(broadcasterIndex);
                            SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Broadcasters), LB_RESETCONTENT, 0, 0);
                            SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Listeners), LB_RESETCONTENT, 0, 0);
                            ClearCheckBoxes(hwndDlg);

                            if (s_broadcasters.GetSize() > 0)
                            {
                                for (int i = 0; i < s_broadcasters.GetSize(); ++i)
                                    AddListEntry(hwndDlg, s_broadcasters.Get(i)->name, IDC_LIST_Broadcasters);
                                    
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Broadcasters), LB_SETCURSEL, s_broadcasters.GetSize() - 1, 0);
                            }
                        }
                    }
                    break;

                case ID_RemoveListener:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int broadcasterIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Broadcasters, LB_GETCURSEL, 0, 0);
                        int listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);

                        if (broadcasterIndex >= 0 && listenerIndex >= 0)
                        {
                            s_broadcasters.Get(broadcasterIndex)->listeners.Delete(listenerIndex, true);
                            SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Listeners), LB_RESETCONTENT, 0, 0);
                            ClearCheckBoxes(hwndDlg);
                            if (s_broadcasters.Get(broadcasterIndex)->listeners.GetSize() > 0)
                            {
                                for (int i = 0; i < s_broadcasters.Get(broadcasterIndex)->listeners.GetSize(); ++i)
                                    AddListEntry(hwndDlg, s_broadcasters.Get(broadcasterIndex)->listeners.Get(i)->name, IDC_LIST_Listeners);
                                    
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Listeners), LB_SETCURSEL, s_broadcasters.Get(broadcasterIndex)->listeners.GetSize() - 1, 0);
                                
#ifdef WIN32
                                listenerIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Listeners, LB_GETCURSEL, 0, 0);
                                
                                if (listenerIndex >= 0)
                                    SetCheckBoxes(hwndDlg, s_broadcasters.Get(broadcasterIndex)->listeners.Get(listenerIndex));
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
                        TransferBroadcasters(s_broadcasters, s_pages.Get(s_pageIndex)->broadcasters);

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

                                for (int i = 0; i < s_pages.Get(index)->surfaces.GetSize(); ++i)
                                    AddListEntry(hwndDlg, s_pages.Get(index)->surfaces.Get(i)->pageSurfaceName, IDC_LIST_PageSurfaces);
                                
                                if (s_pages.Get(index)->surfaces.GetSize() > 0)
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
                                if (s_dlgResult == IDOK)
                                {
                                    SurfaceLine *surface = new SurfaceLine();
                                    surface->type = s_MidiSurfaceToken;
                                    surface->name = s_surfaceName;
                                    surface->inPort = s_surfaceInPort;
                                    surface->outPort = s_surfaceOutPort;

                                    s_surfaces.Add(surface);
                                    
                                    AddListEntry(hwndDlg, s_surfaceName.c_str(), IDC_LIST_Surfaces);
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL, s_surfaces.GetSize() - 1, 0);
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
                                if (s_dlgResult == IDOK)
                                {
                                    SurfaceLine *surface = new SurfaceLine();
                                    surface->type = s_OSCSurfaceToken;
                                    surface->name = s_surfaceName;
                                    surface->remoteDeviceIP = s_surfaceRemoteDeviceIP;
                                    surface->inPort = s_surfaceInPort;
                                    surface->outPort = s_surfaceOutPort;

                                    s_surfaces.Add(surface);
                                    
                                    AddListEntry(hwndDlg, s_surfaceName.c_str(), IDC_LIST_Surfaces);
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL, s_surfaces.GetSize() - 1, 0);
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
                            if (s_dlgResult == IDOK)
                            {
                                PageLine *page = new PageLine();
                                page->name = s_surfaceName;
                                page->followMCP = s_followMCP;
                                page->synchPages = s_synchPages;
                                page->isScrollLinkEnabled = s_isScrollLinkEnabled;
                                page->scrollSynch = s_scrollSynch;

                                s_pages.Add(page);
                                AddListEntry(hwndDlg, s_surfaceName.c_str(), IDC_LIST_Pages);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_SETCURSEL, s_pages.GetSize() - 1, 0);
                            }
                        }
                        break ;

                    case IDC_BUTTON_AddPageSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            s_dlgResult = false;
                            s_editMode = false;
                            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_PageSurface), hwndDlg, dlgProcPageSurface);
                            if (s_dlgResult == IDOK)
                            {
                                PageSurfaceLine *pageSurface = new PageSurfaceLine();
                                pageSurface->pageSurfaceName = s_pageSurfaceName;
                                pageSurface->numChannels = s_numChannels;
                                pageSurface->channelOffset = s_channelOffset;
                                pageSurface->templateFilename = s_templateFilename;
                                pageSurface->zoneTemplateFolder = s_zoneTemplateFolder;
                                pageSurface->fxZoneTemplateFolder = s_fxZoneTemplateFolder;

                                int index = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                                if (index >= 0)
                                {
                                    s_pages.Get(index)->surfaces.Add(pageSurface);
                                    AddListEntry(hwndDlg, s_pageSurfaceName, IDC_LIST_PageSurfaces);
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_SETCURSEL, s_pages.Get(index)->surfaces.GetSize() - 1, 0);
                                }
                            }
                        }
                        break ;

                    case IDC_BUTTON_EditSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Surfaces, LB_GETCURSEL, 0, 0);
                            if (index >= 0)
                            {
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_GETTEXT, index, (LPARAM)(LPCTSTR)(s_surfaceName.c_str()));
                                s_surfaceInPort = s_surfaces.Get(index)->inPort;
                                s_surfaceOutPort = s_surfaces.Get(index)->outPort;
                                s_surfaceRemoteDeviceIP = s_surfaces.Get(index)->remoteDeviceIP;

                                s_dlgResult = false;
                                s_editMode = true;
                                
                                if (s_surfaces.Get(index)->type == s_MidiSurfaceToken)
                                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_MidiSurface), hwndDlg, dlgProcMidiSurface);
                                else if (s_surfaces.Get(index)->type == s_OSCSurfaceToken)
                                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_OSCSurface), hwndDlg, dlgProcOSCSurface);
                                                               
                                if (s_dlgResult == IDOK)
                                {
                                    s_surfaces.Get(index)->name = s_surfaceName;
                                    s_surfaces.Get(index)->remoteDeviceIP = s_surfaceRemoteDeviceIP;
                                    s_surfaces.Get(index)->inPort = s_surfaceInPort;
                                    s_surfaces.Get(index)->outPort = s_surfaceOutPort;
                                }
                                
                                s_editMode = false;
                            }
                        }
                        break ;
                    
                    case IDC_BUTTON_EditPage:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                            if (index >= 0)
                            {
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_GETTEXT, index, (LPARAM)(LPCTSTR)(s_surfaceName.c_str()));

                                s_dlgResult = false;
                                s_editMode = true;
                                
                                s_followMCP = s_pages.Get(index)->followMCP;
                                s_synchPages = s_pages.Get(index)->synchPages;
                                s_isScrollLinkEnabled = s_pages.Get(index)->isScrollLinkEnabled;
                                s_scrollSynch = s_pages.Get(index)->scrollSynch;
                                
                                DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_Page), hwndDlg, dlgProcPage);
                                if (s_dlgResult == IDOK)
                                {
                                    s_pages.Get(index)->name = s_surfaceName;
                                    s_pages.Get(index)->followMCP = s_followMCP;
                                    s_pages.Get(index)->synchPages = s_synchPages;
                                    s_pages.Get(index)->isScrollLinkEnabled = s_isScrollLinkEnabled;
                                    s_pages.Get(index)->scrollSynch = s_scrollSynch;

                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_RESETCONTENT, 0, 0);
                                    for (int i = 0; i < s_pages.GetSize(); ++i)
                                        AddListEntry(hwndDlg, s_pages.Get(i)->name, IDC_LIST_Pages);
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
                            if (index >= 0)
                            {
                                s_dlgResult = false;
                                s_editMode = true;
                                
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_GETTEXT, index, (LPARAM)(LPCTSTR)(s_pageSurfaceName.c_str()));

                                int pageIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                                if (pageIndex >= 0)
                                {
                                    s_numChannels = s_pages.Get(pageIndex)->surfaces.Get(index)->numChannels;
                                    s_channelOffset = s_pages.Get(pageIndex)->surfaces.Get(index)->channelOffset;
                                    
                                    s_templateFilename = s_pages.Get(pageIndex)->surfaces.Get(index)->templateFilename;
                                    s_zoneTemplateFolder  = s_pages.Get(pageIndex)->surfaces.Get(index)->zoneTemplateFolder;
                                    s_fxZoneTemplateFolder = s_pages.Get(pageIndex)->surfaces.Get(index)->fxZoneTemplateFolder;

                                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_PageSurface), hwndDlg, dlgProcPageSurface);
                                    
                                    if (s_dlgResult == IDOK)
                                    {
                                        s_pages.Get(pageIndex)->surfaces.Get(index)->numChannels = s_numChannels;
                                        s_pages.Get(pageIndex)->surfaces.Get(index)->channelOffset = s_channelOffset;
                                        s_pages.Get(pageIndex)->surfaces.Get(index)->templateFilename = s_templateFilename;
                                        s_pages.Get(pageIndex)->surfaces.Get(index)->zoneTemplateFolder = s_zoneTemplateFolder;
                                        s_pages.Get(pageIndex)->surfaces.Get(index)->fxZoneTemplateFolder = s_fxZoneTemplateFolder;
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
                            if (index >= 0)
                            {
                                s_surfaces.Delete(index, true);
                                
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_RESETCONTENT, 0, 0);
                                for (int i = 0; i < s_surfaces.GetSize(); ++i)
                                    AddListEntry(hwndDlg, s_surfaces.Get(i)->name, IDC_LIST_Surfaces);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL, index, 0);
                            }
                        }
                        break ;
                        
                    case IDC_BUTTON_RemovePage:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int index = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                            if (index >= 0)
                            {
                                s_pages.Delete(index, true);
                                
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_RESETCONTENT, 0, 0);
#ifdef WIN32
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_RESETCONTENT, 0, 0);
#endif
                                for (int i = 0; i < s_pages.GetSize(); ++i)
                                    AddListEntry(hwndDlg, s_pages.Get(i)->name, IDC_LIST_Pages);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_SETCURSEL, index, 0);
                            }
                        }
                        break ;

                    case IDC_BUTTON_RemovePageSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            int pageIndex = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_Pages, LB_GETCURSEL, 0, 0);
                            if (pageIndex >= 0)
                            {
                                int index = (int)SendDlgItemMessage(hwndDlg, IDC_LIST_PageSurfaces, LB_GETCURSEL, 0, 0);
                                if (index >= 0)
                                {
                                    s_pages.Get(pageIndex)->surfaces.Delete(index, true);
                                    
                                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_RESETCONTENT, 0, 0);

                                    for (int i = 0; i < s_pages.Get(pageIndex)->surfaces.GetSize(); ++i)
                                        AddListEntry(hwndDlg, s_pages.Get(pageIndex)->surfaces.Get(i)->pageSurfaceName, IDC_LIST_PageSurfaces);
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
            string iniFilePath = string(DAW::GetResourcePath()) + "/CSI/CSI.ini";
            
            ifstream iniFile(iniFilePath);
            
            int lineNumber = 0;
            
            for (string line; getline(iniFile, line) ; )
            {
                TrimLine(line);
                
                lineNumber++;
                
                if (lineNumber == 1)
                {
                    if (line != s_MajorVersionToken)
                    {
                        MessageBox(g_hwnd, ("Version mismatch -- Your CSI.ini file is not " + s_MajorVersionToken).c_str(), ("This is CSI " + s_MajorVersionToken).c_str(), MB_OK);
                        iniFile.close();
                        break;
                    }
                    else
                        continue;
                }
                
                if (line[0] != '\r' && line[0] != '/' && line != "") // ignore comment lines and blank lines
                {
                    istringstream iss(line);
                    vector<string> tokens;
                    string token;
                    
                    while (iss >> quoted(token))
                        tokens.push_back(token);
                    
                    if (tokens[0] == s_MidiSurfaceToken || tokens[0] == s_OSCSurfaceToken)
                    {
                        SurfaceLine *surface = new SurfaceLine();
                        
                        surface->type = tokens[0];
                        surface->name = tokens[1];
                        
                        if ((surface->type == s_MidiSurfaceToken || surface->type == s_OSCSurfaceToken) && (tokens.size() == 4 || tokens.size() == 5))
                        {
                            surface->inPort = atoi(tokens[2].c_str());
                            surface->outPort = atoi(tokens[3].c_str());
                            if (surface->type == s_OSCSurfaceToken && tokens.size() == 5)
                                surface->remoteDeviceIP = tokens[4];
                        }
                        
                        s_surfaces.Add(surface);
                        
                        AddListEntry(hwndDlg, surface->name, IDC_LIST_Surfaces);
                    }
                    else if (tokens[0] == s_PageToken && tokens.size() > 1)
                    {
                        bool followMCP = true;
                        bool synchPages = true;
                        bool isScrollLinkEnabled = false;
                        bool scrollSynch = false;

                        for (int i = 2; i < tokens.size(); i++)
                        {
                            if (tokens[i] == "FollowTCP")
                                followMCP = false;
                            else if (tokens[i] == "NoSynchPages")
                                synchPages = false;
                            else if (tokens[i] == "UseScrollLink")
                                isScrollLinkEnabled = true;
                            else if (tokens[i] == "UseScrollSynch")
                                scrollSynch = true;
                        }

                        PageLine *page = new PageLine();
                        page->name = tokens[1];
                        page->followMCP = followMCP;
                        page->synchPages = synchPages;
                        page->isScrollLinkEnabled = isScrollLinkEnabled;
                        page->scrollSynch = scrollSynch;
 
                        s_pages.Add(page);
                        
                        AddListEntry(hwndDlg, page->name, IDC_LIST_Pages);
                    }
                    else if (tokens[0] == "Broadcaster" && tokens.size() == 2 && s_pages.GetSize() > 0)
                    {
                        Broadcaster *broadcaster = new Broadcaster();
                        broadcaster->name = tokens[1];
                        s_pages.Get(s_pages.GetSize() - 1)->broadcasters.Add(broadcaster);
                    }
                    else if (tokens[0] == "Listener" && tokens.size() == 3 && s_pages.GetSize() > 0 && s_pages.Get(s_pages.GetSize() - 1)->broadcasters.GetSize() > 0)
                    {
                        Listener *listener = new Listener();
                        listener->name = tokens[1];

                        vector<string> categoryTokens;
                        GetTokens(categoryTokens, tokens[2]);
                        
                        for (int i = 0; i < (int)categoryTokens.size(); ++i)
                        {
                            if (categoryTokens[i] == "GoHome")
                                listener->goHome = true;
                            if (categoryTokens[i] == "Sends")
                                listener->sends = true;
                            if (categoryTokens[i] == "Receives")
                                listener->receives = true;
                            if (categoryTokens[i] == "FocusedFX")
                                listener->focusedFX = true;
                            if (categoryTokens[i] == "FocusedFXParam")
                                listener->focusedFXParam = true;
                            if (categoryTokens[i] == "FXMenu")
                                listener->fxMenu = true;
                            if (categoryTokens[i] == "LocalFXSlot")
                                listener->localFXSlot = true;
                            if (categoryTokens[i] == "Modifiers")
                                listener->modifiers = true;
                            if (categoryTokens[i] == "SelectedTrackFX")
                                listener->selectedTrackFX = true;
                            if (categoryTokens[i] == "Custom")
                                listener->custom = true;
                        }
                        
                        s_pages.Get(s_pages.GetSize() - 1)->broadcasters.Get(s_pages.Get(s_pages.GetSize() - 1)->broadcasters.GetSize() - 1)->listeners.Add(listener);
                    }
                    else if (tokens.size() == 6 || tokens.size() == 7)
                    {
                        if (tokens[0] == "LocalModifiers")
                        {
                            tokens.erase(tokens.begin()); // pop front
                        }

                        PageSurfaceLine *surface = new PageSurfaceLine();
                        
                        if (s_pages.GetSize() > 0)
                        {
                            s_pages.Get(s_pages.GetSize() - 1)->surfaces.Add(surface);
                            
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
          
            if (s_surfaces.GetSize() > 0)
                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL, 0, 0);
            
            if (s_pages.GetSize() > 0)
            {
                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Pages), LB_SETCURSEL, 0, 0);

                // the messages above don't trigger the user-initiated code, so pretend the user selected them
                SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_LIST_Pages, LBN_SELCHANGE), 0);
                
                if (s_pages.Get(0)->surfaces.GetSize() > 0)
                    SendMessage(GetDlgItem(hwndDlg, IDC_LIST_PageSurfaces), LB_SETCURSEL, 0, 0);
            }
        }
        break;
        
        case WM_DESTROY:
        {
            s_surfaces.Empty(true);
        
            for (int i = 0; i < s_pages.GetSize(); ++i)
            {
                s_pages.Get(i)->surfaces.Empty(true);
                
                for (int j = 0; j < s_pages.Get(i)->broadcasters.GetSize(); ++j)
                    s_pages.Get(i)->broadcasters.Get(j)->listeners.Empty();
                    
                s_pages.Get(i)->broadcasters.Empty(true);
            }
            
            s_pages.Empty(true);
        }
        break;

        case WM_USER+1024:
        {
            ofstream iniFile(string(DAW::GetResourcePath()) + "/CSI/CSI.ini");

            if (iniFile.is_open())
            {
                iniFile << s_MajorVersionToken + "\n";
                
                iniFile << "\n";
                
                string line = "";
                
                for (int i = 0; i < s_surfaces.GetSize(); ++i)
                {
                    line = s_surfaces.Get(i)->type + " ";
                    line += "\"" + s_surfaces.Get(i)->name + "\" ";
                    line += to_string(s_surfaces.Get(i)->inPort) + " ";
                    line += to_string(s_surfaces.Get(i)->outPort) + " ";

                    if (s_surfaces.Get(i)->type == s_OSCSurfaceToken)
                        line += s_surfaces.Get(i)->remoteDeviceIP;
                    
                    iniFile << line + "\n";
                }
                
                iniFile << "\n";
                
                for (int i = 0; i < s_pages.GetSize(); ++i)
                {
                    line = s_PageToken + " ";
                    line += "\"" + s_pages.Get(i)->name + "\"";
                    
                    if (s_pages.Get(i)->followMCP == false)
                        line += " FollowTCP";
                                        
                    if (s_pages.Get(i)->synchPages == false)
                        line += " NoSynchPages";
                    
                    if (s_pages.Get(i)->isScrollLinkEnabled == true)
                        line += " UseScrollLink";
                    
                    if (s_pages.Get(i)->scrollSynch == true)
                        line += " UseScrollSynch";

                    line += "\n";
                    
                    iniFile << line;

                    for (int j = 0; j < s_pages.Get(i)->surfaces.GetSize(); ++j)
                    {
                        line = "\t";
                        line += "\"" + s_pages.Get(i)->surfaces.Get(j)->pageSurfaceName + "\" ";
                        line += to_string(s_pages.Get(i)->surfaces.Get(j)->numChannels) + " " ;
                        line += to_string(s_pages.Get(i)->surfaces.Get(j)->channelOffset) + " " ;
                        line += "\"" + s_pages.Get(i)->surfaces.Get(j)->templateFilename + "\" ";
                        line += "\"" + s_pages.Get(i)->surfaces.Get(j)->zoneTemplateFolder + "\" ";
                        line += "\"" + s_pages.Get(i)->surfaces.Get(j)->fxZoneTemplateFolder + "\" ";

                        iniFile << line + "\n";
                    }
                    
                    iniFile << "\n";
                    
                    for (int j = 0; j < s_pages.Get(i)->broadcasters.GetSize(); ++j)
                    {
                        if (s_pages.Get(i)->broadcasters.Get(j)->listeners.GetSize() == 0)
                            continue;
                        
                        iniFile << string("\tBroadcaster ") + "\"" + s_pages.Get(i)->broadcasters.Get(j)->name + "\"\n";
                        
                        for (int k = 0; k < s_pages.Get(i)->broadcasters.Get(j)->listeners.GetSize(); ++k)
                        {
                            string listenerCategories = "";
                            
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->goHome)
                                listenerCategories += "GoHome ";
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->sends)
                                listenerCategories += "Sends ";
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->receives)
                                listenerCategories += "Receives ";
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->focusedFX)
                                listenerCategories += "FocusedFX ";
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->focusedFXParam)
                                listenerCategories += "FocusedFXParam ";
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->fxMenu)
                                listenerCategories += "FXMenu ";
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->localFXSlot)
                                listenerCategories += "LocalFXSlot ";
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->modifiers)
                                listenerCategories += "Modifiers ";
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->selectedTrackFX)
                                listenerCategories += "SelectedTrackFX ";
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->custom)
                                listenerCategories += "Custom ";

                            iniFile << string("\t\tListener ") + "\"" + s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->name + "\" \"" + listenerCategories + "\"\n";
                        }
                        
                        iniFile << "\n";
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
    Control_Surface_Integrator,
    createFunc,
    configFunc,
};
