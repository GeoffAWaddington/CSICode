//
//  control_surface_integrator_ui.cpp
//  reaper_csurf_integrator
//
//

#include "control_surface_integrator.h"
#include "../WDL/dirscan.h"
#include "resource.h"

extern void TrimLine(string &line);
extern void GetParamStepsString(string &outputString, int numSteps);

extern int g_minNumParamSteps;
extern int g_maxNumParamSteps;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Learn FX
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct FXRowLayout
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    string suffix;
    string modifiers;
    int modifier;
    
    FXRowLayout()
    {
        modifier = 0;
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct FXCellParams
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    int paramNum;
    string paramName;
    double deltaValue;
    ptrvector<double> acceleratedDeltaValues;
    double rangeMinimum;
    double rangeMaximum;
    ptrvector<double> steppedValues;
    ptrvector<int> acceleratedTickValues;
    string controlParams;
    string nameDisplayParams;
    string valueDisplayParams;
    PropertyList controlProperties;
    PropertyList nameDisplayProperties;
    PropertyList valueDisplayProperties;

    FXCellParams()
    {
        paramNum = 0;
        deltaValue = 0.001;
        rangeMinimum = 0.0;
        rangeMaximum = 1.0;
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct FXCellWidgets
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    int modifier;
    int paramNum;
    char paramName[128];
    char paramWidget[128];
    char paramNameWidget[128];
    char paramValueWidget[128];
    
    FXCellWidgets()
    {
        modifier = 0;
        paramNum = -1;
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct FXParamInfo
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    int row;
    int column;
    int paramNum;
    int modifier;
    char paramName[128];
    char paramWidget[128];
    char paramNameWidget[128];
    char paramValueWidget[128];

    FXParamInfo()
    {
        row = 0;
        column = 0;
        paramNum = -1;
        modifier = 0;
    }
};

static HWND hwndLearnDlg = NULL;
static int s_dlgResult = IDCANCEL;

static ModifierManager s_modifierManager(NULL);

static ZoneManager *s_zoneManager;

static int s_lastTouchedParamNum = -1;
static MediaTrack *s_focusedTrack = NULL;
static int s_fxSlot = 0;
static char s_fxName[BUFSZ];
static char s_fxAlias[BUFSZ];

static int s_numColumns;
static ptrvector<FXRowLayout> s_fxRowLayouts;
ptrvector<FXParamInfo> s_fxParamInfo;
static ptrvector<FXCellWidgets> cellWidgets;

// t = template
string_list s_t_paramWidgets;
string_list s_t_displayRows;
string_list s_t_ringStyles;
string_list s_t_fonts;
static bool s_t_hasColor;
static string s_t_paramWidget;
static string s_t_nameWidget;
static string s_t_valueWidget;
static string s_t_paramWidgetParams;
static string s_t_nameWidgetParams;
static string s_t_valueWidgetParams;

static const int s_baseControls[] =
{
    IDC_PickRingStyle,
    
    IDC_FXParamNameEdit,
    IDC_FixedTextDisplayPickRow,
    
    IDC_FXParamValueDisplayPickRow,
    
    IDC_PickSteps,
    IDC_EditSteps,
    IDC_StepsPromptGroup,
    
    IDC_GroupFXControl,
    IDC_GroupFixedTextDisplay,
    IDC_GroupFXParamValueDisplay,
};

static const int s_fontControls[] =
{
    IDC_FixedTextDisplayFontLabel,
    IDC_FixedTextDisplayPickFont,
    IDC_FixedTextDisplayTopLabel,
    IDC_Edit_FixedTextDisplayTop,
    IDC_FixedTextDisplayBottomLabel,
    IDC_Edit_FixedTextDisplayBottom,
    IDC_FXParamValueDisplayFontLabel,
    IDC_FXParamValueDisplayPickFont,
    IDC_ParamValueDisplayTopLabel,
    IDC_Edit_ParamValueDisplayTop,
    IDC_ParamValueDisplayBottomLabel,
    IDC_Edit_ParamValueDisplayBottom
};

static const int s_colorControls[] =
{
    IDC_FXParamRingColorBox,
    IDC_FXParamRingColor,
    IDC_FXParamIndicatorColorBox,
    IDC_FXParamIndicatorColor,
    IDC_FixedTextDisplayForegroundColor,
    IDC_FXFixedTextDisplayForegroundColorBox,
    IDC_FixedTextDisplayBackgroundColor,
    IDC_FXFixedTextDisplayBackgroundColorBox,
    IDC_FXParamDisplayForegroundColor,
    IDC_FXParamValueDisplayForegroundColorBox,
    IDC_FXParamDisplayBackgroundColor,
    IDC_FXParamValueDisplayBackgroundColorBox
};

static void ShowBaseControls(HWND hwndDlg, bool show)
{
    for (int i = 0; i < NUM_ELEM(s_baseControls); ++i)
            ShowWindow(GetDlgItem(hwndDlg, s_baseControls[i]), show);
}

static void ShowFontControls(HWND hwndDlg, bool show)
{
    for (int i = 0; i < NUM_ELEM(s_fontControls); ++i)
            ShowWindow(GetDlgItem(hwndDlg, s_fontControls[i]), show);
}

static void ShowColorControls(HWND hwndDlg, bool show)
{
    for (int i = 0; i < NUM_ELEM(s_colorControls); ++i)
            ShowWindow(GetDlgItem(hwndDlg, s_colorControls[i]), show);
}

static unsigned int s_buttonColors[][3] =
{
    { IDC_FXParamRingColor, IDC_FXParamRingColorBox, 0xffffffff },
    { IDC_FXParamIndicatorColor, IDC_FXParamIndicatorColorBox, 0xffffffff },
    { IDC_FixedTextDisplayForegroundColor, IDC_FXFixedTextDisplayForegroundColorBox, 0xffffffff },
    { IDC_FixedTextDisplayBackgroundColor, IDC_FXFixedTextDisplayBackgroundColorBox, 0xffffffff },
    { IDC_FXParamDisplayForegroundColor, IDC_FXParamValueDisplayForegroundColorBox, 0xffffffff },
    { IDC_FXParamDisplayBackgroundColor, IDC_FXParamValueDisplayBackgroundColorBox, 0xffffffff },
};

static unsigned int &GetButtonColorForID(unsigned int id)
{
    for (int x = 0; x < NUM_ELEM(s_buttonColors); x ++)
        if (s_buttonColors[x][0] == id) return s_buttonColors[x][2];
    WDL_ASSERT(false);
    return s_buttonColors[0][2];
}

static WDL_DLGRET dlgProcEditFXAlias(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            SetDlgItemText(hwndDlg, IDC_EDIT_FXAlias, s_fxAlias);
        }
            break;
            
        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case IDOK:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        GetDlgItemText(hwndDlg, IDC_EDIT_FXAlias, s_fxAlias, sizeof(s_fxAlias));
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

static bool DeleteZone()
{
    char buf[2048], buf2[2048];
    snprintf(buf, sizeof(buf), __LOCALIZE_VERFMT("This will permanently delete\n\n%s\n\nAre you sure you want to permanently delete this Zone from disk?","csi_mbox"), s_fxName);
    snprintf(buf2, sizeof(buf2), __LOCALIZE_VERFMT("Delete %s","csi_mbox"), s_fxAlias);
    if (MessageBox(GetMainHwnd(), buf, buf2, MB_YESNO) == IDNO)
       return false;
    
    s_zoneManager->RemoveZone(s_fxName);
    
    return true;
}

static void SaveZone()
{
    if (s_zoneManager == NULL || s_focusedTrack == NULL || s_fxName[0] == 0)
        return;
    
    char path[BUFSIZ];
    snprintf(path, sizeof(path), "%s/CSI/Zones/%s/AutoGeneratedFXZones", GetResourcePath(), s_zoneManager->GetFXZoneFolder().c_str());

    try
    {
        RecursiveCreateDirectory(path,0);
        
        string trimmedFXName = s_fxName;
        ReplaceAllWith(trimmedFXName, s_BadFileChars, "_");
        
        snprintf(path, sizeof(path), "%s/%s.zon", path, trimmedFXName.c_str());
        
        FILE *fxZone = fopenUTF8(path,"wb");
        
        if (fxZone)
        {
            fprintf(fxZone, "Zone \"%s\" \"%s\"\n", s_fxName, s_fxAlias);
            
            if (s_zoneManager->GetZoneFilePaths().Exists("FXPrologue"))
            {
                fpistream file(s_zoneManager->GetZoneFilePaths().Get("FXPrologue")->filePath.c_str());
                    
                for (string line; getline(file, line) ; )
                    if (line.find("Zone") != 0)
                        fprintf(fxZone, "%s\n", line.c_str());
            }
            
            fprintf(fxZone, "\n%s\n\n", s_BeginAutoSection);
            
            
            
            
            
            fprintf(fxZone, "\n%s\n\n", s_EndAutoSection);

            
            if (s_zoneManager->GetZoneFilePaths().Exists("FXEpilogue"))
            {
                fpistream file(s_zoneManager->GetZoneFilePaths().Get("FXEpilogue")->filePath.c_str());
                    
                for (string line; getline(file, line) ; )
                    if (line.find("Zone") != 0)
                        fprintf(fxZone, "%s\n", line.c_str());
            }

            fclose(fxZone);
        }
    }
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble saving %s\n", path);
        ShowConsoleMsg(buffer);
    }
}

static void LoadTemplates()
{
    s_numColumns = s_zoneManager->GetSurface()->GetNumChannels();
    s_fxRowLayouts.clear();
    s_t_paramWidget = "";
    s_t_nameWidget = "";
    s_t_valueWidget = "";

    s_t_paramWidgetParams = "";
    s_t_nameWidgetParams = "";
    s_t_valueWidgetParams = "";

    s_t_paramWidgets.clear();
    s_t_displayRows.clear();
    s_t_ringStyles.clear();
    s_t_fonts.clear();
    
    s_t_hasColor = false;

    const WDL_StringKeyedArray<CSIZoneInfo*> &zonePaths = s_zoneManager->GetZoneFilePaths();
    
    if (s_zoneManager == NULL || ! zonePaths.Exists("FXRowLayout") || ! zonePaths.Exists("FXWidgetLayout"))
        return;

    try
    {
        fpistream file(zonePaths.Get("FXRowLayout")->filePath.c_str());
        
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            if (line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
            
            if (line.find("Zone") == string::npos)
            {
                string_list tokens;
                GetTokens(tokens, line.c_str());
                
                if (tokens.size() == 2)
                {
                    FXRowLayout t = FXRowLayout();
                    
                    t.suffix = tokens[1];
                    t.modifiers = tokens[0];
                    t.modifier = s_zoneManager->GetSurface()->GetModifierManager()->GetModifierValue(tokens[0]);
                    s_fxRowLayouts.push_back(t);
                }
            }
        }
    }
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s\n", zonePaths.Get("FXRowLayout")->filePath.c_str());
        ShowConsoleMsg(buffer);
    }

    try
    {
        fpistream file(zonePaths.Get("FXWidgetLayout")->filePath.c_str());

        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            if (line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
            
            if (line.find("Zone") == string::npos)
            {
                string_list tokens;
                GetTokens(tokens, line.c_str());
                
                
                if (line.find("Zone") == string::npos)
                {
                    if (tokens[0][0] == '#')
                    {
                        if (tokens[0] == "#WidgetTypes")
                        {
                            for (int i = 1; i < tokens.size(); ++i)
                                s_t_paramWidgets.push_back(tokens[i]);
                        }
                        else if (tokens[0] == "#DisplayRows")
                        {
                            for (int i = 1; i < tokens.size(); ++i)
                                s_t_displayRows.push_back(tokens[i]);
                        }
                        
                        else if (tokens[0] == "#RingStyles")
                        {
                            for (int i = 1; i < tokens.size(); ++i)
                                s_t_ringStyles.push_back(tokens[i]);
                        }
                        
                        else if (tokens[0] == "#DisplayFonts")
                        {
                            for (int i = 1; i < tokens.size(); ++i)
                                s_t_fonts.push_back(tokens[i]);
                        }
                        else if (tokens[0] == "#SupportsColor")
                        {
                            s_t_hasColor = true;
                        }
                    }
                    else
                    {
                        if (tokens.size() > 1 && tokens[1] == "FXParam")
                        {
                            s_t_paramWidget = tokens[0];
                            
                            if (tokens.size() > 2)
                                s_t_paramWidgetParams = line.substr(line.find(tokens[2]), line.length() - 1);
                        }
                        if (tokens.size() > 1 && tokens[1] == "FixedTextDisplay")
                        {
                            s_t_nameWidget = tokens[0];
                            
                            if (tokens.size() > 2)
                                s_t_nameWidgetParams = line.substr(line.find(tokens[2]), line.length() - 1);
                        }
                        if (tokens.size() > 1 && tokens[1] == "FXParamValueDisplay")
                        {
                            s_t_valueWidget = tokens[0];
                            
                            if (tokens.size() > 2)
                                s_t_valueWidgetParams = line.substr(line.find(tokens[2]), line.length() - 1);
                        }
                    }
                }
            }
        }
    }
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s\n", zonePaths.Get("FXWidgetLayout")->filePath.c_str());
        ShowConsoleMsg(buffer);
    }
}

static void WriteBoilerPlate(FILE *fxFile, string &fxBoilerplatePath)
{
    int lineNumber = 0;
    
    try
    {
        fpistream file(fxBoilerplatePath.c_str());
        
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            lineNumber++;
            
            if (line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
                continue;

            if (line.find("Zone") == 0)
                continue;
            
            fprintf(fxFile, "\t%s\n", line.c_str());
        }
    }
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", fxBoilerplatePath.c_str(), lineNumber);
        ShowConsoleMsg(buffer);
    }
}

static void AutoMapFX(HWND hwndDlg,  MediaTrack *track, int fxSlot, const char *fxName, const char *fxAlias)
{
    char fxFilePath[BUFSIZ];
    snprintf(fxFilePath, sizeof(fxFilePath), "%s/CSI/Zones/%s/AutoGeneratedFXZones", GetResourcePath(), s_zoneManager->GetFXZoneFolder().c_str());
    RecursiveCreateDirectory(fxFilePath, 0);

    string fxFileName = s_fxName;
    ReplaceAllWith(fxFileName, s_BadFileChars, "_");

    char fxFullFilePath[BUFSIZ];
    snprintf(fxFullFilePath, sizeof(fxFullFilePath), "%s/%s.zon", fxFilePath, fxFileName.c_str());

    FILE *fxFile = fopenUTF8(fxFullFilePath, "wb");

    if (fxFile)
    {
        fprintf(fxFile, "%s \"%s\" \"%s\"\n", "Zone", s_fxName, s_fxAlias);
        
        if (s_zoneManager->GetZoneFilePaths().Exists("FXPrologue"))
            WriteBoilerPlate(fxFile, s_zoneManager->GetZoneFilePaths().Get("FXPrologue")->filePath);

        fprintf(fxFile, "\n%s\n", s_BeginAutoSection);
        
        int numParams = TrackFX_GetNumParams(track, fxSlot);

        int currentParam = 0;
        
        for (int row = 0; row < s_fxRowLayouts.size(); ++row)
        {
            char modifiers[BUFSIZ];
            modifiers[0] = 0;
            if (s_fxRowLayouts[row].modifiers != "")
                snprintf(modifiers, sizeof(modifiers), "%s+", s_fxRowLayouts[row].modifiers.c_str());
            
            for (int column = 0; column < s_numColumns; ++column)
            {
                if (currentParam < numParams)
                {
                    string steps;
                    s_zoneManager->GetSteppedValuesForParam(steps, fxName, track, fxSlot, currentParam);
                    
                    char paramName[BUFSIZ];
                    paramName[0] = 0;
                    TrackFX_GetParamName(s_focusedTrack, s_fxSlot, currentParam, paramName, sizeof(paramName));
                    
                    fprintf(fxFile, "\t%s%s%d FXParam %d %s%s\n", modifiers, s_t_paramWidget.c_str(), column + 1, currentParam, steps.c_str(), s_t_paramWidgetParams.c_str());
                    fprintf(fxFile, "\t%s%s%d FixedTextDisplay \"%s\" %s\n", modifiers, s_t_nameWidget.c_str(), column + 1, paramName, s_t_nameWidgetParams.c_str());
                    fprintf(fxFile, "\t%s%s%d FXParamValueDisplay %d %s\n\n", modifiers, s_t_valueWidget.c_str(), column + 1, currentParam, s_t_valueWidgetParams.c_str());
                        
                    currentParam++;
                }
                else
                {
                    fprintf(fxFile, "\t%s%s%d NoAction\n", modifiers, s_t_paramWidget.c_str(), column + 1);
                    fprintf(fxFile, "\t%s%s%d NoAction\n", modifiers, s_t_nameWidget.c_str(), column + 1);
                    fprintf(fxFile, "\t%s%s%d NoAction\n\n", modifiers, s_t_valueWidget.c_str(), column + 1);
                }
                
                for (int cell = 1; cell < s_t_paramWidgets.size(); ++cell)
                {
                    fprintf(fxFile, "\t%s%s%d NoAction\n", modifiers, s_t_paramWidgets[cell].c_str(), column + 1);
                    fprintf(fxFile, "\t%s%s%d NoAction NoFeedback\n", modifiers, s_t_nameWidget.c_str(), column + 1);
                    fprintf(fxFile, "\t%s%s%d NoAction NoFeedback\n\n", modifiers, s_t_valueWidget.c_str(), column + 1);
                }
            }
        }

        fprintf(fxFile, "%s\n\n", s_EndAutoSection);

        if (s_zoneManager->GetZoneFilePaths().Exists("FXEpilogue"))
            WriteBoilerPlate(fxFile, s_zoneManager->GetZoneFilePaths().Get("FXEpilogue")->filePath);
        
        fprintf(fxFile, "%s\n", "ZoneEnd");

        fclose(fxFile);
    }
    
    EnableWindow(GetDlgItem(hwndDlg, IDC_AutoMap), false);
}

static void HandleInitDialog(HWND hwndDlg)
{
    for (int i = 0; i < s_t_ringStyles.size(); ++i)
        SendDlgItemMessage(hwndDlg, IDC_PickRingStyle, CB_ADDSTRING, 0, (LPARAM)s_t_ringStyles[i].c_str());

    WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, IDC_FixedTextDisplayPickRow));
    SendDlgItemMessage(hwndDlg, IDC_FixedTextDisplayPickRow, CB_ADDSTRING, 0, (LPARAM)__LOCALIZE("No Display","csi_fxsteps"));
    
    WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, IDC_FixedTextDisplayPickRow));
    SendDlgItemMessage(hwndDlg, IDC_FXParamValueDisplayPickRow, CB_ADDSTRING, 0, (LPARAM)__LOCALIZE("No Display","csi_fxsteps"));

    for (int i = 0; i < s_t_fonts.size(); ++i)
    {
        SendDlgItemMessage(hwndDlg, IDC_FixedTextDisplayPickFont, CB_ADDSTRING, 0, (LPARAM)s_t_fonts[i].c_str());
        SendDlgItemMessage(hwndDlg, IDC_FXParamValueDisplayPickFont, CB_ADDSTRING, 0, (LPARAM)s_t_fonts[i].c_str());
    }

    WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, IDC_PickSteps));
    SendDlgItemMessage(hwndDlg, IDC_PickSteps, CB_ADDSTRING, 0, (LPARAM)__LOCALIZE("Custom","csi_fxsteps"));
    
    for (int j = g_minNumParamSteps; j <= g_maxNumParamSteps; j++)
        SendDlgItemMessage(hwndDlg, IDC_PickSteps, CB_ADDSTRING, 0, (LPARAM)int_to_string(j).c_str());
}

static void FillAllParamsLists(HWND hwndDlg)
{
    char buf[BUFSIZ];

    SendMessage(GetDlgItem(hwndDlg, IDC_AllParams), LB_RESETCONTENT, 0, 0);

    for (int i = 0; i < TrackFX_GetNumParams(s_focusedTrack, s_fxSlot); ++i)
    {
        TrackFX_GetParamName(s_focusedTrack, s_fxSlot, i, buf, sizeof(buf));

        SendDlgItemMessage(hwndDlg, IDC_AllParams, LB_ADDSTRING, 0, (LPARAM)buf);
    }
}

static void FillFXCellWidgets()
{
    cellWidgets.clear();
    
    int lineNumber = 0;
    
    if ( ! s_zoneManager->GetZoneFilePaths().Exists(s_fxName))
        return;
    try
    {
        
        fpistream file(s_zoneManager->GetZoneFilePaths().Get(s_fxName)->filePath.c_str());
        
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            if (line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
            
            string_list tokens;
            
            GetTokens(tokens, line.c_str());
            
            if (tokens.size() > 2 && !strcmp(tokens[1], "FXParam"))
            {
                FXCellWidgets widgets;
                string_list subTokens;
                GetSubTokens(subTokens, tokens[0], '+');
                widgets.modifier = s_modifierManager.GetModifierValue(subTokens);
                strcpy(widgets.paramWidget, subTokens[subTokens.size() - 1]);
                widgets.paramNum = atoi(tokens[2]);
                
                tokens.clear();
                subTokens.clear();
                getline(file, line);
                GetTokens(tokens, line.c_str());
                
                if (tokens.size() > 2 && !strcmp(tokens[1], "FixedTextDisplay"))
                {
                    GetSubTokens(subTokens, tokens[0], '+');
                    strcpy(widgets.paramNameWidget, subTokens[subTokens.size() - 1]);
                    strcpy(widgets.paramName, tokens[2]);
                }
                
                tokens.clear();
                subTokens.clear();
                getline(file, line);
                GetTokens(tokens, line.c_str());
                
                if (tokens.size() > 0)
                {
                    GetSubTokens(subTokens, tokens[0], '+');
                    strcpy(widgets.paramValueWidget, subTokens[subTokens.size() - 1]);
                }
                
                cellWidgets.push_back(widgets);
            }
        }
    }
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", s_zoneManager->GetZoneFilePaths().Get(s_fxName)->filePath.c_str(), lineNumber);
        ShowConsoleMsg(buffer);
    }
}

static void FillParams(HWND hwndDlg)
{
    FillFXCellWidgets();
    
    HWND hwndParamList = GetDlgItem(hwndDlg, IDC_PARAM_LIST);
    
    int numColumns = Header_GetItemCount(ListView_GetHeader(hwndParamList));
       
    for (int i = numColumns - 1; i >= 0; --i)
        ListView_DeleteColumn(hwndParamList, i);
    
    ListView_DeleteAllItems(hwndParamList);
    
    int firstColumnSize = 200;
    int columnSize  = 73;

#ifdef WIN32
    firstColumnSize = 244;
    columnSize  = 124;
#endif
    
    LVCOLUMN columnDescriptor = { LVCF_TEXT | LVCF_WIDTH, LVCFMT_RIGHT, 0, (char*)"" };
    columnDescriptor.cx = firstColumnSize;
    
    ListView_InsertColumn(hwndParamList, 0, &columnDescriptor);
    
    for (int i = 1; i <= s_numColumns; ++i)
    {
        char caption[20];
        snprintf(caption, sizeof(caption), "%d", i);
        columnDescriptor.pszText = caption;
        columnDescriptor.cx = columnSize;
        columnDescriptor.fmt = LVCFMT_CENTER;
        ListView_InsertColumn(hwndParamList, i, &columnDescriptor);
    }

    const WDL_PointerKeyedArray<Widget*, WDL_IntKeyedArray<WDL_PtrList<ActionContext> *> *> *contexts = s_zoneManager->GetLearnFocusedFXActionContextDictionary();
    
    if (contexts ==  NULL)
        return;
    
    s_fxParamInfo.clear();
    
    for (int row = 0; row < s_fxRowLayouts.size(); ++row)
    {
        for (int cell= 0; cell < s_t_paramWidgets.size(); ++cell)
        {
            int rowIdx = row * s_t_paramWidgets.size() + cell;
            
            char undecoratedWidgetName[128];
            
            if(s_fxRowLayouts[row].modifier)
                snprintf(undecoratedWidgetName, sizeof(undecoratedWidgetName), "%s+%s%s", s_fxRowLayouts[row].modifiers.c_str(), s_t_paramWidgets[cell].c_str(), s_fxRowLayouts[row].suffix.c_str());
            else
                snprintf(undecoratedWidgetName, sizeof(undecoratedWidgetName), "%s%s", s_t_paramWidgets[cell].c_str(), s_fxRowLayouts[row].suffix.c_str());

            LVITEM item;
            memset(&item, 0, sizeof(item));
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = rowIdx;
            item.cchTextMax = 20;
            item.pszText = undecoratedWidgetName;
            
            ListView_InsertItem(hwndParamList, &item);
            
            for (int columnIdx = 0; columnIdx < s_numColumns; ++columnIdx)
            {
                char paramName[BUFSIZ];
                paramName[0] = 0;
                
                char widgetName[BUFSIZ];
                snprintf(widgetName, sizeof(widgetName), "%s%s%d", s_t_paramWidgets[cell].c_str(), s_fxRowLayouts[row].suffix.c_str(), columnIdx + 1);
                
                char pszTextBuf[128];
                pszTextBuf[0] = 0;
                
                LVITEM item;
                memset(&item, 0, sizeof(item));
                item.mask = LVIF_TEXT;
                item.iItem = rowIdx;
                item.iSubItem = columnIdx + 1;
                item.cchTextMax = 20;
                item.pszText = pszTextBuf;
                
                FXParamInfo info;
                info.row = rowIdx;
                info.column = columnIdx;
                info.paramName[0] = 0;
                info.paramWidget[0] = 0;
                info.paramNameWidget[0] = 0;
                info.paramValueWidget[0] = 0;
                info.modifier = s_fxRowLayouts[row].modifier;
                
                for (int cellWidget = 0; cellWidget < cellWidgets.size(); ++cellWidget)
                {
                    FXCellWidgets &widgets = cellWidgets[cellWidget];
                    
                    if(!strcmp(widgets.paramWidget, widgetName) && info.modifier == widgets.modifier)
                    {
                        info.paramNum = widgets.paramNum;
                        strcpy(info.paramName, widgets.paramName);
                        item.pszText = info.paramName;
                        strcpy(info.paramWidget, widgets.paramWidget);
                        strcpy(info.paramNameWidget, widgets.paramNameWidget);
                        strcpy(info.paramValueWidget, widgets.paramValueWidget);

                        break;
                    }
                }
                  
                ListView_SetItem(hwndParamList, &item);
                s_fxParamInfo.push_back(info);
            }
        }
    }
}

static void FillFXParamNumParams(HWND hwndDlg, int paramIdx)
{
    const WDL_PointerKeyedArray<Widget*, WDL_IntKeyedArray<WDL_PtrList<ActionContext> *> *> *zoneContexts = s_zoneManager->GetLearnFocusedFXActionContextDictionary();
    
    if (zoneContexts == NULL)
        return;

    rgba_color defaultColor;
    defaultColor.r = 237;
    defaultColor.g = 237;
    defaultColor.b = 237;

    for (int infoIdx = 0; infoIdx < s_fxParamInfo.size(); ++infoIdx)
    {
        int modifier = s_fxParamInfo[infoIdx].modifier;
        
        char buf[BUFSIZ];
        
        if (s_fxParamInfo[infoIdx].paramNum == paramIdx)
        {
            SendMessage(GetDlgItem(hwndDlg, IDC_FixedTextDisplayPickRow), CB_RESETCONTENT, 0, 0);
            SendMessage(GetDlgItem(hwndDlg, IDC_FXParamValueDisplayPickRow), CB_RESETCONTENT, 0, 0);

            SendDlgItemMessage(hwndDlg, IDC_FixedTextDisplayPickRow, CB_ADDSTRING, 0, (LPARAM)"No Display");
            SendDlgItemMessage(hwndDlg, IDC_FXParamValueDisplayPickRow, CB_ADDSTRING, 0, (LPARAM)"No Display");

            for (int i = 0; i < s_t_displayRows.size(); ++i)
            {
                snprintf(buf, sizeof(buf), "%s%d", s_t_displayRows[i].c_str(), s_fxParamInfo[infoIdx].column + 1);
                SendDlgItemMessage(hwndDlg, IDC_FixedTextDisplayPickRow, CB_ADDSTRING, 0, (LPARAM)buf);
                SendDlgItemMessage(hwndDlg, IDC_FXParamValueDisplayPickRow, CB_ADDSTRING, 0, (LPARAM)buf);
            }
                        
            ListView_SetItemState(GetDlgItem(hwndDlg, IDC_PARAM_LIST), s_fxParamInfo[infoIdx].row, LVIS_SELECTED, LVIS_SELECTED);
            
            SetWindowText(GetDlgItem(hwndDlg, IDC_GroupFXControl), s_fxParamInfo[infoIdx].paramWidget);
            
            Widget *widget = s_zoneManager->GetSurface()->GetWidgetByName(s_fxParamInfo[infoIdx].paramWidget);
            
            if (widget != NULL)
            {
                if(zoneContexts->Exists(widget) && zoneContexts->Get(widget)->Exists(modifier))
                {
                    ListView_SetItemState(GetDlgItem(hwndDlg, IDC_PARAM_LIST), s_fxParamInfo[infoIdx].row, LVIS_SELECTED, LVIS_SELECTED);
                    
                    WDL_PtrList<ActionContext> *contexts = zoneContexts->Get(widget)->Get(modifier);
                    
                    if(contexts->GetSize() > 0)
                    {
                        ActionContext *context = contexts->Get(0);
                        
                        const char *ringstyle = context->GetWidgetProperties().get_prop(PropertyType_RingStyle);
                        if (ringstyle)
                            SetDlgItemText(hwndDlg, IDC_PickRingStyle, ringstyle);
                        else
                            SetDlgItemText(hwndDlg, IDC_PickRingStyle, "");
                        
                        snprintf(buf, sizeof(buf), "%0.2f", context->GetDeltaValue());
                        SetDlgItemText(hwndDlg, IDC_EDIT_Delta, buf);
                        
                        snprintf(buf, sizeof(buf), "%0.2f", context->GetRangeMinimum());
                        SetDlgItemText(hwndDlg, IDC_EDIT_RangeMin, buf);
                        
                        snprintf(buf, sizeof(buf), "%0.2f", context->GetRangeMaximum());
                        SetDlgItemText(hwndDlg, IDC_EDIT_RangeMax, buf);
                        
                        int numSteps = context->GetNumberOfSteppedValues();
                        if (numSteps)
                        {
                            snprintf(buf, sizeof(buf), "%d", numSteps);
                            SetDlgItemText(hwndDlg, IDC_PickSteps, buf);
                        }
                        else
                            SetDlgItemText(hwndDlg, IDC_PickSteps, "");
                        
                        char tmp[BUFSZ];
                        const vector<double> &steppedValues = context->GetSteppedValues();
                        string steps;
                        
                        for (int i = 0; i < steppedValues.size(); ++i)
                        {
                            steps += format_number(steppedValues[i], tmp, sizeof(tmp));
                            steps += "  ";
                        }
                        
                        SetDlgItemText(hwndDlg, IDC_EditSteps, steps.c_str());
                        
                        
                        const vector<double> &acceleratedDeltaValues = context->GetAcceleratedDeltaValues();
                        string deltas;
                        
                        for (int i = 0; i < (int)acceleratedDeltaValues.size(); ++i)
                        {
                            deltas += format_number(acceleratedDeltaValues[i], tmp, sizeof(tmp));
                            deltas += " ";
                        }
                        
                        SetDlgItemText(hwndDlg, IDC_EDIT_DeltaValues, deltas.c_str());
                        
                        
                        const vector<int> &acceleratedTickCounts = context->GetAcceleratedTickCounts();
                        string ticks;
                        
                        for (int i = 0; i < (int)acceleratedTickCounts.size(); ++i)
                            ticks += int_to_string(acceleratedTickCounts[i]) + " ";
                        
                        SetDlgItemText(hwndDlg, IDC_EDIT_TickValues, ticks.c_str());
                        
                        const char *ringcolor = context->GetWidgetProperties().get_prop(PropertyType_LEDRingColor);
                        if (ringcolor)
                        {
                            rgba_color color;
                            GetColorValue(ringcolor, color);
                            GetButtonColorForID(IDC_FXParamRingColor) = ColorToNative(color.r, color.g, color.b);
                        }
                        else
                            GetButtonColorForID(IDC_FXParamRingColor) = ColorToNative(defaultColor.r, defaultColor.g, defaultColor.b);
                        
                        
                        const char *pushcolor = context->GetWidgetProperties().get_prop(PropertyType_PushColor);
                        if (pushcolor)
                        {
                            rgba_color color;
                            GetColorValue(pushcolor, color);
                            GetButtonColorForID(IDC_FXParamIndicatorColor) = ColorToNative(color.r, color.g, color.b);
                        }
                        else
                            GetButtonColorForID(IDC_FXParamIndicatorColor) = ColorToNative(defaultColor.r, defaultColor.g, defaultColor.b);
                        
                    }
                }
            }
            
            widget = s_zoneManager->GetSurface()->GetWidgetByName(s_fxParamInfo[infoIdx].paramNameWidget);
            
            if (widget != NULL)
            {
                if(zoneContexts->Exists(widget) && zoneContexts->Get(widget)->Exists(modifier))
                {
                    WDL_PtrList<ActionContext> *contexts = zoneContexts->Get(widget)->Get(modifier);
                    
                    if(contexts->GetSize() > 0)
                    {
                        ActionContext *context = contexts->Get(0);
                        
                        SetWindowText(GetDlgItem(hwndDlg, IDC_FXParamNameEdit), context->GetStringParam());

                        string pnw = s_fxParamInfo[infoIdx].paramNameWidget;
                        
                        int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_FixedTextDisplayPickRow), CB_FINDSTRINGEXACT, -1, (LPARAM)s_fxParamInfo[infoIdx].paramNameWidget);
                        if (index >= 0)
                            SendMessage(GetDlgItem(hwndDlg, IDC_FixedTextDisplayPickRow), CB_SETCURSEL, index, 0);

                        const char *property = context->GetWidgetProperties().get_prop(PropertyType_Font);
                        if (property)
                            SetDlgItemText(hwndDlg, IDC_FixedTextDisplayPickFont, property);
                        else
                            SetDlgItemText(hwndDlg, IDC_FixedTextDisplayPickFont, "");
                        
                        property = context->GetWidgetProperties().get_prop(PropertyType_TopMargin);
                        if (property)
                            SetDlgItemText(hwndDlg, IDC_Edit_FixedTextDisplayTop, property);
                        else
                            SetDlgItemText(hwndDlg, IDC_Edit_FixedTextDisplayTop, "");
                        
                        property = context->GetWidgetProperties().get_prop(PropertyType_BottomMargin);
                        if (property)
                            SetDlgItemText(hwndDlg, IDC_Edit_FixedTextDisplayBottom, property);
                        else
                            SetDlgItemText(hwndDlg, IDC_Edit_FixedTextDisplayBottom, "");
                        
                        const char *foreground = context->GetWidgetProperties().get_prop(PropertyType_TextColor);
                        if (foreground)
                        {
                            rgba_color color;
                            GetColorValue(foreground, color);
                            GetButtonColorForID(IDC_FixedTextDisplayForegroundColor) = ColorToNative(color.r, color.g, color.b);
                        }
                        else
                            GetButtonColorForID(IDC_FixedTextDisplayForegroundColor) = ColorToNative(defaultColor.r, defaultColor.g, defaultColor.b);
                        
                        const char *background = context->GetWidgetProperties().get_prop(PropertyType_BackgroundColor);
                        if (background)
                        {
                            rgba_color color;
                            GetColorValue(background, color);
                            GetButtonColorForID(IDC_FixedTextDisplayBackgroundColor) = ColorToNative(color.r, color.g, color.b);
                        }
                        else
                            GetButtonColorForID(IDC_FixedTextDisplayBackgroundColor) = ColorToNative(defaultColor.r, defaultColor.g, defaultColor.b);
                    }
                }
            }
            
            widget = s_zoneManager->GetSurface()->GetWidgetByName(s_fxParamInfo[infoIdx].paramValueWidget);
            
            if (widget != NULL)
            {
                if(zoneContexts->Exists(widget) && zoneContexts->Get(widget)->Exists(modifier))
                {
                    WDL_PtrList<ActionContext> *contexts = zoneContexts->Get(widget)->Get(modifier);
                    
                    if(contexts->GetSize() > 0)
                    {
                        ActionContext *context = contexts->Get(0);

                        int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_FXParamValueDisplayPickRow), CB_FINDSTRINGEXACT, -1, (LPARAM)s_fxParamInfo[infoIdx].paramValueWidget);
                        if (index >= 0)
                            SendMessage(GetDlgItem(hwndDlg, IDC_FXParamValueDisplayPickRow), CB_SETCURSEL, index, 0);

                        const char *property = context->GetWidgetProperties().get_prop(PropertyType_Font);
                        if (property)
                            SetDlgItemText(hwndDlg, IDC_FXParamValueDisplayPickFont, property);
                        else
                            SetDlgItemText(hwndDlg, IDC_FXParamValueDisplayPickFont, "");
                        
                        property = context->GetWidgetProperties().get_prop(PropertyType_TopMargin);
                        if (property)
                            SetDlgItemText(hwndDlg, IDC_Edit_ParamValueDisplayTop, property);
                        else
                            SetDlgItemText(hwndDlg, IDC_Edit_ParamValueDisplayTop, "");
                        
                        property = context->GetWidgetProperties().get_prop(PropertyType_BottomMargin);
                        if (property)
                            SetDlgItemText(hwndDlg, IDC_Edit_ParamValueDisplayBottom, property);
                        else
                            SetDlgItemText(hwndDlg, IDC_Edit_ParamValueDisplayBottom, "");
                        
                        const char *foreground = context->GetWidgetProperties().get_prop(PropertyType_TextColor);
                        if (foreground)
                        {
                            rgba_color color;
                            GetColorValue(foreground, color);
                            GetButtonColorForID(IDC_FXParamDisplayForegroundColor) = ColorToNative(color.r, color.g, color.b);
                        }
                        else
                            GetButtonColorForID(IDC_FXParamDisplayForegroundColor) = ColorToNative(defaultColor.r, defaultColor.g, defaultColor.b);
                        
                        const char *background = context->GetWidgetProperties().get_prop(PropertyType_BackgroundColor);
                        if (background)
                        {
                            rgba_color color;
                            GetColorValue(background, color);
                            GetButtonColorForID(IDC_FXParamDisplayBackgroundColor) = ColorToNative(color.r, color.g, color.b);
                        }
                        else
                            GetButtonColorForID(IDC_FXParamDisplayBackgroundColor) = ColorToNative(defaultColor.r, defaultColor.g, defaultColor.b);
                    }
                }
            }
        }
        
    } 
    
    RECT rect;
    GetClientRect(hwndDlg, &rect);
    InvalidateRect(hwndDlg, &rect, 0);
}

static void ApplyChanges(HWND hwndDlg, int paramIdx)
{
    const WDL_PointerKeyedArray<Widget*, WDL_IntKeyedArray<WDL_PtrList<ActionContext> *> *> *zoneContexts = s_zoneManager->GetLearnFocusedFXActionContextDictionary();

    char buf[BUFSIZ];

    GetDlgItemText(hwndDlg, IDC_EDIT_Delta, buf, sizeof(buf));

    double deltaVal = atof(buf);
    
    for (int infoIdx = 0; infoIdx < s_fxParamInfo.size(); ++infoIdx)
    {
        int modifier = s_fxParamInfo[infoIdx].modifier;
        
        if (s_fxParamInfo[infoIdx].paramNum == paramIdx)
        {
            Widget *widget = s_zoneManager->GetSurface()->GetWidgetByName(s_fxParamInfo[infoIdx].paramWidget);
            
            if (widget != NULL)
            {
                if(zoneContexts->Exists(widget) && zoneContexts->Get(widget)->Exists(modifier))
                {
                    WDL_PtrList<ActionContext> *contexts = zoneContexts->Get(widget)->Get(modifier);
                    
                    if(contexts->GetSize() > 0) for (int cIdx = 0; cIdx < contexts->GetSize(); ++cIdx)
                    {
                        ActionContext *context = contexts->Get(cIdx);
                        
                        context->SetDeltaValue(deltaVal);
                        
                        
                        
                        
                    }
                }
            }
        }
    }
}

static void HandleInitialize(HWND hwndDlg)
{
    s_lastTouchedParamNum = -1;
            
    TrackFX_GetFXName(s_focusedTrack, s_fxSlot, s_fxName, sizeof(s_fxName));
                
    char buf[BUFSZ];
    snprintf(buf, sizeof(buf), "%s    %s    %s", s_zoneManager->GetSurface()->GetName(), s_fxAlias, s_fxName);

    SetWindowText(hwndDlg, buf);
    
    if (s_zoneManager->GetZoneFilePaths().Exists(s_fxName))
    {
        // PopulateListView
        s_zoneManager->LoadLearnFocusedFXZone(s_fxName, s_fxSlot);
        EnableWindow(GetDlgItem(hwndDlg, IDC_AutoMap), false);
    }
    else
    {
        s_zoneManager->LoadLearnFocusedFXZone(s_fxName, s_fxSlot);
        EnableWindow(GetDlgItem(hwndDlg, IDC_AutoMap), true);
    }
    
    FillParams(hwndDlg);
    
    if (s_t_fonts.size())
        ShowFontControls(hwndDlg, true);
    else
        ShowFontControls(hwndDlg, false);

    if (s_t_hasColor)
    {
        ShowColorControls(hwndDlg, true);
        InvalidateRect(hwndDlg, NULL, true);
    }
    else
        ShowColorControls(hwndDlg, false);
    
    FillAllParamsLists(hwndDlg);
}

static WDL_DLGRET dlgProcLearnFX(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    { 
        case WM_USER + 1024: // initialize
            HandleInitialize(hwndDlg);
            break;
           
        case WM_INITDIALOG: // initialize
            HandleInitDialog(hwndDlg);
            break;
           
        case WM_PAINT:
        {
            if (s_t_hasColor)
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwndDlg, &ps);
                
                for (int x = 0; x < NUM_ELEM(s_buttonColors); x ++)
                {
                    const int colorPickerBox = s_buttonColors[x][1];
                    const int colorValue = s_buttonColors[x][2];

                    HBRUSH brush = CreateSolidBrush(colorValue);
                    
                    RECT clientRect, windowRect;
                    POINT p;
                    GetClientRect(GetDlgItem(hwndDlg, colorPickerBox), &clientRect);
                    GetWindowRect(GetDlgItem(hwndDlg, colorPickerBox), &windowRect);
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
            
        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case IDC_FXParamRingColor:
                case IDC_FXParamIndicatorColor:
                case IDC_FixedTextDisplayForegroundColor:
                case IDC_FixedTextDisplayBackgroundColor:
                case IDC_FXParamDisplayForegroundColor:
                case IDC_FXParamDisplayBackgroundColor:
                    {
                        GR_SelectColor(hwndDlg, (int *)&GetButtonColorForID(LOWORD(wParam)));
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_AllParams:
                {
                    switch (HIWORD(wParam))
                    {
                        case LBN_SELCHANGE:
                        {
                            int index = (int)SendDlgItemMessage(hwndDlg, IDC_AllParams, LB_GETCURSEL, 0, 0);

                            if (index >= 0)
                                 FillFXParamNumParams(hwndDlg, index);
                        }
                    }
                }
                    break;
          
                case IDC_Delete:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        if (DeleteZone())
                            EnableWindow(GetDlgItem(hwndDlg, IDC_AutoMap), true);
                    }
                    break ;
                    
                case IDC_EraseControl:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        
                    }
                    break ;

                case IDC_Apply:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int index = (int)SendDlgItemMessage(hwndDlg, IDC_AllParams, LB_GETCURSEL, 0, 0);

                        if (index >= 0)
                            ApplyChanges(hwndDlg, index);
                    }
                    break ;

                case IDC_FXAlias:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_EditFXAlias), g_hwnd, dlgProcEditFXAlias);
                        
                        if (s_dlgResult == IDOK)
                        {
                            char buf[BUFSZ];
                            snprintf(buf, sizeof(buf), "%s    %s    %s", s_zoneManager->GetSurface()->GetName(), s_fxAlias, s_fxName);
                            SetWindowText(hwndDlg, buf);
                        }
                    }
                    break;
                                       
                case IDC_AutoMap:
                    if (HIWORD(wParam) == BN_CLICKED)
                        AutoMapFX(hwndDlg, s_focusedTrack, s_fxSlot, s_fxName, s_fxAlias);
                    break ;
                                        
                case IDCANCEL:
                    if (HIWORD(wParam) == BN_CLICKED)
                        CloseFocusedFXDialog();
                    break ;
            }
        }
    }
    
    return 0;
}

static void LearnFocusedFXDialog()
{
    if (hwndLearnDlg == NULL)
    {
        // initialize
        LoadTemplates();
        hwndLearnDlg = CreateDialog(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_LearnFX), g_hwnd, dlgProcLearnFX);
        
        if (hwndLearnDlg != NULL)
            ListView_SetExtendedListViewStyleEx(GetDlgItem(hwndLearnDlg, IDC_PARAM_LIST), LVS_EX_GRIDLINES, LVS_EX_GRIDLINES);
    }
    
    if (hwndLearnDlg == NULL)
        return;
    
    SendMessage(hwndLearnDlg, WM_USER + 1024, 0, 0);
        
    ShowWindow(hwndLearnDlg, SW_SHOW);
}

void LaunchLearnFocusedFXDialog(ZoneManager *zoneManager)
{
    TrackFX_GetFXName(s_focusedTrack, s_fxSlot, s_fxName, sizeof(s_fxName));
    
    const WDL_StringKeyedArray<CSIZoneInfo*> &zonePaths = s_zoneManager->GetZoneFilePaths();

    if (zonePaths.Exists(s_fxName))
    {
        lstrcpyn_safe(s_fxAlias, zonePaths.Get(s_fxName)->alias.c_str(), sizeof(s_fxAlias));
        LearnFocusedFXDialog();
    }
    else
    {
        zoneManager->GetAlias(s_fxName, s_fxAlias, sizeof(s_fxAlias));
        LearnFocusedFXDialog();
    }
}

void CheckLearnFocusedFXState(ZoneManager *zoneManager)
{
    if ((hwndLearnDlg != NULL && ! IsWindowVisible(hwndLearnDlg)) || s_zoneManager == NULL || zoneManager != s_zoneManager) // not the current control surface
        return;
    
    int trackNumber = 0;
    int fxSlot = 0;
    int itemNumber = 0;
    int takeNumber = 0;
    int paramIndex = 0;
        
    int retVal = GetTouchedOrFocusedFX(1, &trackNumber, &itemNumber, &takeNumber, &fxSlot, &paramIndex);

    MediaTrack *focusedTrack = NULL;
    
    trackNumber++;
    
    if (retVal && ! (paramIndex & 0x01))
    {
        if (trackNumber > 0)
            focusedTrack = DAW::GetTrack(trackNumber);
        else if (trackNumber == 0)
            focusedTrack = GetMasterTrack(NULL);
    }

    if (focusedTrack != NULL && (focusedTrack != s_focusedTrack || (focusedTrack == s_focusedTrack && fxSlot != s_fxSlot)))
    {
        s_focusedTrack = focusedTrack;
        s_fxSlot = fxSlot;
        
        LaunchLearnFocusedFXDialog(zoneManager);
    }
}

void LearnFocusedFXDialog(ZoneManager *zoneManager)
{
    if (s_zoneManager != NULL && zoneManager != s_zoneManager) // not the current control surface
        return;
    
    if (hwndLearnDlg != NULL && IsWindowVisible(hwndLearnDlg))
    {
        CloseFocusedFXDialog();
        return;
    }
    
    int trackNumber = 0;
    int fxSlot = 0;
    int itemNumber = 0;
    int takeNumber = 0;
    int paramIndex = 0;
        
    int retVal = GetTouchedOrFocusedFX(1, &trackNumber, &itemNumber, &takeNumber, &fxSlot, &paramIndex);

    MediaTrack *focusedTrack = NULL;
    
    trackNumber++;
    
    if (retVal && ! (paramIndex & 0x01))
    {
        if (trackNumber > 0)
            focusedTrack = DAW::GetTrack(trackNumber);
        else if (trackNumber == 0)
            focusedTrack = GetMasterTrack(NULL);
    }

    if (focusedTrack == NULL)
        return;
    
    s_zoneManager = zoneManager;
    s_focusedTrack = focusedTrack;
    s_fxSlot = fxSlot;

    LaunchLearnFocusedFXDialog(zoneManager);
}

void CloseFocusedFXDialog()
{
    // GAW TBD -- Save here
    
    s_zoneManager->DeactivateLearnFXZone();
    
    s_zoneManager =  NULL;
    s_focusedTrack = NULL;
    s_fxSlot = 0;

    if (hwndLearnDlg != NULL)
        ShowWindow(hwndLearnDlg, SW_HIDE);
}

void UpdateLearnWindow()
{
    if (hwndLearnDlg == NULL || ! IsWindowVisible(hwndLearnDlg))
        return;

    int tracknumberOut;
    int fxnumberOut;
    int paramnumberOut;
    
    if (GetLastTouchedFX(&tracknumberOut, &fxnumberOut, &paramnumberOut))
    {
        if (s_lastTouchedParamNum != paramnumberOut)
        {
            s_lastTouchedParamNum = paramnumberOut;
            SendMessage(GetDlgItem(hwndLearnDlg, IDC_AllParams), LB_SETCURSEL, s_lastTouchedParamNum, 0);
#ifdef WIN32
            FillFXParamNumParams(hwndLearnDlg, s_lastTouchedParamNum);
#endif
        }
    }
}

void UpdateLearnWindow(int paramNumber)
{
    if (hwndLearnDlg == NULL || ! IsWindowVisible(hwndLearnDlg))
        return;

    SendMessage(GetDlgItem(hwndLearnDlg, IDC_AllParams), LB_SETCURSEL, paramNumber, 0);
#ifdef WIN32
    FillFXParamNumParams(hwndLearnDlg, paramNumber);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FileSystem
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    static string_list GetDirectoryFilenames(const string &path)
    {
        string_list filenames;

        WDL_DirScan scan;
        
        if (!scan.First(path.c_str()))
            do {
              const char *ext = WDL_get_fileext(scan.GetCurrentFN());
              if (scan.GetCurrentFN()[0] != '.' && !scan.GetCurrentIsDirectory() && (!stricmp(ext,".mst") || !stricmp(ext,".ost")))
                filenames.push_back(std::string(scan.GetCurrentFN()));
            } while (!scan.Next());
        
        return filenames;
    }
    
    static string_list GetDirectoryFolderNames(const string &path)
    {
        string_list folderNames;

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

static bool s_editMode = false;

static char *s_genericOSCSurface = (char *)"Generic OSC Surface";
static char *s_BehringerX32Surface = (char *)"Behringer X32 Surface";

static string s_surfaceName;
static string s_surfaceType;
static int s_surfaceInPort = 0;
static int s_surfaceOutPort = 0;
static int s_surfaceRefreshRate = 0;
static int s_surfaceDefaultRefreshRate = 15;
static int s_surfaceMaxPacketsPerRun = 0;
static int s_surfaceDefaultMaxPacketsPerRun = 0;  // No restriction, send all queued packets
static string s_surfaceRemoteDeviceIP;

static int s_pageIndex = 0;
static bool s_followMCP = false;
static bool s_synchPages = true;
static bool s_isScrollLinkEnabled = false;
static bool s_scrollSynch = false;

static string s_pageSurfaceName;
static int s_numChannels = 0;
static int s_channelOffset = 0;
static string s_templateFilename;
static string s_zoneTemplateFolder;
static string s_fxZoneTemplateFolder;

////////////////////////////////////////////////////////////////////////////////////////////////////////
// structs
////////////////////////////////////////////////////////////////////////////////////////////////////////
struct SurfaceLine
{
    string type ;
    string name;
    int inPort;
    int outPort;
    int surfaceRefreshRate;
    int surfaceMaxPacketsPerRun;
    string remoteDeviceIP;
    
    SurfaceLine()
    {
        inPort = 0;
        outPort = 0;
        surfaceRefreshRate = s_surfaceDefaultRefreshRate;
        surfaceMaxPacketsPerRun = s_surfaceDefaultMaxPacketsPerRun;
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
        numChannels = 0;
        channelOffset = 0;
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
    bool modifiers;
    
    Listener()
    {
        goHome = false;
        sends = false;
        receives = false;
        focusedFX = false;
        focusedFXParam = false;
        fxMenu = false;
        localFXSlot = false;
        selectedTrackFX = false;
        modifiers = false;
    }
};

struct Broadcaster
{
    string name;
    WDL_PtrList<Listener> listeners;    
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
            
            destinationBroadcaster->listeners.Add(destinationListener);
        }
        
        destination.Add(destinationBroadcaster);
    }
}

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
                        
                        s_synchPages = !! IsDlgButtonChecked(hwndDlg, IDC_CHECK_SynchPages);
                        s_isScrollLinkEnabled = !! IsDlgButtonChecked(hwndDlg, IDC_CHECK_ScrollLink);
                        s_scrollSynch = !! IsDlgButtonChecked(hwndDlg, IDC_CHECK_ScrollSynch);
                        
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

static void FillSurfaceTemplateCombo(HWND hwndDlg, const string &resourcePath)
{
    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate), CB_RESETCONTENT, 0, 0);
    
    char buf[BUFSZ];
    
    GetDlgItemText(hwndDlg, IDC_COMBO_PageSurface, buf, sizeof(buf));
    
    for (int i = 0; i < s_surfaces.GetSize(); ++i)
    {
        if (s_surfaces.Get(i)->name == string(buf))
        {
            string type = s_surfaces.Get(i)->type;
            
            if (type == s_MidiSurfaceToken)
                for (int j = 0; j < (int)FileSystem::GetDirectoryFilenames(resourcePath + "/CSI/Surfaces/Midi/").size(); ++j)
                    AddComboEntry(hwndDlg, 0, (char*)FileSystem::GetDirectoryFilenames(resourcePath + "/CSI/Surfaces/Midi/")[j].c_str(), IDC_COMBO_SurfaceTemplate);

            if (type == s_OSCSurfaceToken || type == s_OSCX32SurfaceToken)
                for (int j = 0; j < (int)FileSystem::GetDirectoryFilenames(resourcePath + "/CSI/Surfaces/OSC/").size(); ++j)
                    AddComboEntry(hwndDlg, 0, (char*)FileSystem::GetDirectoryFilenames(resourcePath + "/CSI/Surfaces/OSC/")[j].c_str(), IDC_COMBO_SurfaceTemplate);
            
            break;
        }
    }
}

static WDL_DLGRET dlgProcPageSurface(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            const string resourcePath(GetResourcePath());
            WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate));
            WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, IDC_COMBO_PageSurface));
            WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates));
            WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, IDC_COMBO_FXZoneTemplates));
            if (s_editMode)
            {
                AddComboEntry(hwndDlg, 0, (char *)s_pageSurfaceName.c_str(), IDC_COMBO_PageSurface);
                SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_PageSurface), CB_SETCURSEL, 0, 0);

                SetDlgItemInt(hwndDlg, IDC_EDIT_NumChannels, s_numChannels, false);
                SetDlgItemInt(hwndDlg, IDC_EDIT_ChannelOffset, s_channelOffset, false);
               
                FillSurfaceTemplateCombo(hwndDlg, resourcePath);
                
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
                
                FillSurfaceTemplateCombo(hwndDlg, resourcePath);
                            
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
                            const string resourcePath(GetResourcePath());
                            FillSurfaceTemplateCombo(hwndDlg, resourcePath);
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
                                
                                index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_FXZoneTemplates), CB_FINDSTRINGEXACT, -1, (LPARAM)buffer);
                                if (index >= 0)
                                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_FXZoneTemplates), CB_SETCURSEL, index, 0);
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
            WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, IDC_COMBO_MidiIn));
            WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, IDC_COMBO_MidiOut));
            
            for (int i = 0; i < GetNumMIDIInputs(); i++)
                if (GetMIDIInputName(i, buf, sizeof(buf)))
                {
                    AddComboEntry(hwndDlg, i, buf, IDC_COMBO_MidiIn);
                    if (s_editMode && s_surfaceInPort == i)
                        SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_MidiIn), CB_SETCURSEL, currentIndex, 0);
                    currentIndex++;
                }
            
            currentIndex = 0;
            
            for (int i = 0; i < GetNumMIDIOutputs(); i++)
                if (GetMIDIOutputName(i, buf, sizeof(buf)))
                {
                    AddComboEntry(hwndDlg, i, buf, IDC_COMBO_MidiOut);
                    if (s_editMode && s_surfaceOutPort == i)
                        SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_MidiOut), CB_SETCURSEL, currentIndex, 0);
                    currentIndex++;
                }
            
            if (s_editMode)
            {
                SetDlgItemText(hwndDlg, IDC_EDIT_MidiSurfaceName, s_surfaceName.c_str());
                SetDlgItemInt(hwndDlg, IDC_EDIT_MidiSurfaceRefreshRate, s_surfaceRefreshRate, true);
            }
            else
            {
                SetDlgItemInt(hwndDlg, IDC_EDIT_MidiSurfaceRefreshRate, s_surfaceDefaultRefreshRate, true);
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
                case IDOK:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        char buf[BUFSZ];
                        GetDlgItemText(hwndDlg, IDC_EDIT_MidiSurfaceName, buf, sizeof(buf));
                        s_surfaceName = buf;
                        
                        BOOL translated;
                        s_surfaceRefreshRate = GetDlgItemInt(hwndDlg, IDC_EDIT_MidiSurfaceRefreshRate, &translated, true);

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
            WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, IDC_COMBO_Type));
            AddComboEntry(hwndDlg, 0, s_genericOSCSurface, IDC_COMBO_Type);
            AddComboEntry(hwndDlg, 1, s_BehringerX32Surface, IDC_COMBO_Type);

            if (s_editMode)
            {
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCSurfaceName, s_surfaceName.c_str());

                if (s_surfaceType == s_OSCSurfaceToken)
                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_Type), CB_SETCURSEL, 0, 0);
                else
                    SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_Type), CB_SETCURSEL, 1, 0);

                SetDlgItemText(hwndDlg, IDC_EDIT_OSCRemoteDeviceIP, s_surfaceRemoteDeviceIP.c_str());
                SetDlgItemInt(hwndDlg, IDC_EDIT_OSCInPort, s_surfaceInPort, false);
                SetDlgItemInt(hwndDlg, IDC_EDIT_OSCOutPort, s_surfaceOutPort, false);
                SetDlgItemInt(hwndDlg, IDC_EDIT_MaxPackets, s_surfaceMaxPacketsPerRun, false);
            }
            else
            {
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCSurfaceName, "");
                SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_Type), CB_SETCURSEL, 0, 0);
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCRemoteDeviceIP, "");
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCInPort, "");
                SetDlgItemText(hwndDlg, IDC_EDIT_OSCOutPort, "");
                SetDlgItemInt(hwndDlg, IDC_EDIT_MaxPackets, s_surfaceDefaultMaxPacketsPerRun, false);
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
                                            
                        GetDlgItemText(hwndDlg, IDC_EDIT_OSCSurfaceName, buf, sizeof(buf));
                        s_surfaceName = buf;
                        
                        int index = (int)SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_Type), CB_GETCURSEL, 0, 0);
                        if (index >= 0)
                        {
                            if (index == 0)
                                s_surfaceType = s_OSCSurfaceToken;
                            else
                                s_surfaceType = s_OSCX32SurfaceToken;
                        }
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_OSCRemoteDeviceIP, buf, sizeof(buf));
                        s_surfaceRemoteDeviceIP = buf;
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_OSCInPort, buf, sizeof(buf));
                        s_surfaceInPort = atoi(buf);
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_OSCOutPort, buf, sizeof(buf));
                        s_surfaceOutPort = atoi(buf);
                        
                        GetDlgItemText(hwndDlg, IDC_EDIT_MaxPackets, buf, sizeof(buf));
                        s_surfaceMaxPacketsPerRun = atoi(buf);

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
    if(listener == NULL)
        return;
    
    char tmp[BUFSZ];
    snprintf(tmp,sizeof(tmp), __LOCALIZE_VERFMT("%s Listens to","csi_osc"), listener->name.c_str());
    SetDlgItemText(hwndDlg, IDC_ListenCheckboxes, tmp);

    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_GoHome), BM_SETCHECK, listener->goHome ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Sends), BM_SETCHECK, listener->sends ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Receives), BM_SETCHECK, listener->receives ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FocusedFX), BM_SETCHECK, listener->focusedFX ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FocusedFXParam), BM_SETCHECK, listener->focusedFXParam ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FXMenu), BM_SETCHECK, listener->fxMenu ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_LocalFXSlot), BM_SETCHECK, listener->localFXSlot ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Modifiers), BM_SETCHECK, listener->modifiers ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_SelectedTrackFX), BM_SETCHECK, listener->selectedTrackFX ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void ClearCheckBoxes(HWND hwndDlg)
{
    SetDlgItemText(hwndDlg, IDC_ListenCheckboxes, __LOCALIZE("Surface Listens to", "csi_osc"));

    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_GoHome), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Sends), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Receives), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FocusedFX), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FocusedFXParam), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_FXMenu), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_LocalFXSlot), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Modifiers), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_SelectedTrackFX), BM_SETCHECK, BST_UNCHECKED, 0);
}

static WDL_DLGRET dlgProcBroadcast(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, IDC_AddBroadcaster));
            WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, IDC_AddListener));
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
            
            CheckDlgButton(hwndDlg, IDC_CHECK_ShowRawInput, g_surfaceRawInDisplay);
            CheckDlgButton(hwndDlg, IDC_CHECK_ShowInput, g_surfaceInDisplay);
            CheckDlgButton(hwndDlg, IDC_CHECK_ShowOutput, g_surfaceOutDisplay);
            CheckDlgButton(hwndDlg, IDC_CHECK_WriteFXParams, g_fxParamsWrite);
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
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Listeners), LB_RESETCONTENT, 0, 0);
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

                                char tmp[BUFSZ];
                                snprintf(tmp, sizeof(tmp), __LOCALIZE_VERFMT("%s Listens to","csi_osc"), 
                                   s_broadcasters.Get(broadcasterIndex)->listeners.Get(s_broadcasters.Get(broadcasterIndex)->listeners.GetSize() - 1)->name.c_str());
                                SetDlgItemText(hwndDlg, IDC_ListenCheckboxes, tmp);
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
                        g_surfaceRawInDisplay = IsDlgButtonChecked(hwndDlg, IDC_CHECK_ShowRawInput) != 0;
                        g_surfaceInDisplay = IsDlgButtonChecked(hwndDlg, IDC_CHECK_ShowInput) != 0;
                        g_surfaceOutDisplay = IsDlgButtonChecked(hwndDlg, IDC_CHECK_ShowOutput) != 0;
                        g_fxParamsWrite = IsDlgButtonChecked(hwndDlg, IDC_CHECK_WriteFXParams) != 0;
                        
                        TransferBroadcasters(s_broadcasters, s_pages.Get(s_pageIndex)->broadcasters);

                        EndDialog(hwndDlg, 0);
                    }
                    break;
            }
        }
    }
    
    return 0;
}

WDL_DLGRET dlgProcMainConfig(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
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
                        break ;
                        
                    case IDC_BUTTON_AddOSCSurface:
                        if (HIWORD(wParam) == BN_CLICKED)
                        {
                            s_dlgResult = false;
                            s_editMode = false;
                            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_OSCSurface), hwndDlg, dlgProcOSCSurface);
                            if (s_dlgResult == IDOK)
                            {
                                SurfaceLine *surface = new SurfaceLine();
                            
                                surface->name = s_surfaceName;
                                surface->type = s_surfaceType;
                                surface->remoteDeviceIP = s_surfaceRemoteDeviceIP;
                                surface->inPort = s_surfaceInPort;
                                surface->outPort = s_surfaceOutPort;
                                surface->surfaceMaxPacketsPerRun = s_surfaceMaxPacketsPerRun;

                                s_surfaces.Add(surface);
                                
                                AddListEntry(hwndDlg, s_surfaceName.c_str(), IDC_LIST_Surfaces);
                                SendMessage(GetDlgItem(hwndDlg, IDC_LIST_Surfaces), LB_SETCURSEL, s_surfaces.GetSize() - 1, 0);
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
                                s_surfaceType = s_surfaces.Get(index)->type;
                                s_surfaceInPort = s_surfaces.Get(index)->inPort;
                                s_surfaceOutPort = s_surfaces.Get(index)->outPort;
                                s_surfaceRemoteDeviceIP = s_surfaces.Get(index)->remoteDeviceIP;
                                s_surfaceRefreshRate = s_surfaces.Get(index)->surfaceRefreshRate;
                                s_surfaceMaxPacketsPerRun = s_surfaces.Get(index)->surfaceMaxPacketsPerRun;

                                s_dlgResult = false;
                                s_editMode = true;
                                
                                string type = s_surfaces.Get(index)->type;
                                
                                if (type == s_MidiSurfaceToken)
                                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_MidiSurface), hwndDlg, dlgProcMidiSurface);
                                else if (type == s_OSCSurfaceToken || type == s_OSCX32SurfaceToken)
                                    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_OSCSurface), hwndDlg, dlgProcOSCSurface);
                                                               
                                if (s_dlgResult == IDOK)
                                {
                                    s_surfaces.Get(index)->name = s_surfaceName;
                                    s_surfaces.Get(index)->type = s_surfaceType;
                                    s_surfaces.Get(index)->remoteDeviceIP = s_surfaceRemoteDeviceIP;
                                    s_surfaces.Get(index)->inPort = s_surfaceInPort;
                                    s_surfaces.Get(index)->outPort = s_surfaceOutPort;
                                    s_surfaces.Get(index)->surfaceRefreshRate = s_surfaceRefreshRate;
                                    s_surfaces.Get(index)->surfaceMaxPacketsPerRun = s_surfaceMaxPacketsPerRun;
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
            string iniFilePath = string(GetResourcePath()) + "/CSI/CSI.ini";
            
            fpistream iniFile(iniFilePath.c_str());
            
            int lineNumber = 0;
            
            for (string line; getline(iniFile, line) ; )
            {
                TrimLine(line);
                
                lineNumber++;
                
                if (lineNumber == 1)
                {
                    if (line != s_MajorVersionToken)
                    {
                        char tmp[BUFSZ];
                        snprintf(tmp, sizeof(tmp), __LOCALIZE_VERFMT("Version mismatch -- Your CSI.ini file is not %s.","csi_mbox"), s_MajorVersionToken);
                        MessageBox(g_hwnd, tmp, __LOCALIZE("CSI.ini version mismatch","csi_mbox"), MB_OK);
                        iniFile.close();
                        break;
                    }
                    else
                        continue;
                }
                
                if (line[0] != '\r' && line[0] != '/' && line != "") // ignore comment lines and blank lines
                {
                    string_list tokens;
                    GetTokens(tokens, line.c_str());
                    
                    if (tokens[0] == s_MidiSurfaceToken || tokens[0] == s_OSCSurfaceToken || tokens[0] == s_OSCX32SurfaceToken)
                    {
                        SurfaceLine *surface = new SurfaceLine();
                        
                        surface->type = tokens[0];
                        surface->name = tokens[1];
                        
                        if ((surface->type == s_MidiSurfaceToken || surface->type == s_OSCSurfaceToken || surface->type == s_OSCX32SurfaceToken) && (tokens.size() == 4 || tokens.size() == 5 || tokens.size() == 6))
                        {
                            surface->inPort = atoi(tokens[2].c_str());
                            surface->outPort = atoi(tokens[3].c_str());
                            if ((surface->type == s_OSCSurfaceToken || surface->type == s_OSCX32SurfaceToken) && tokens.size() > 4)
                                surface->remoteDeviceIP = tokens[4];
                            
                            if (surface->type == s_MidiSurfaceToken)
                            {
                                if (tokens.size() == 5)
                                    surface->surfaceRefreshRate = atoi(tokens[4]);
                                else
                                    surface->surfaceRefreshRate = s_surfaceDefaultRefreshRate;
                            }
                            else if (surface->type == s_OSCSurfaceToken || surface->type == s_OSCX32SurfaceToken)
                            {
                                if (tokens.size() == 6)
                                    surface->surfaceMaxPacketsPerRun = atoi(tokens[5]);
                                else
                                    surface->surfaceMaxPacketsPerRun = s_surfaceDefaultMaxPacketsPerRun;
                            }
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

                        string_list categoryTokens;
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
          
            if (s_pages.GetSize() == 0)
            {
                PageLine *page = new PageLine();
                page->name = "Home";
                page->followMCP = false;
                page->synchPages = false;
                page->isScrollLinkEnabled = false;
                page->scrollSynch = false;

                s_pages.Add(page);
                
                AddListEntry(hwndDlg, page->name, IDC_LIST_Pages);
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
            FILE *iniFile = fopenUTF8((string(GetResourcePath()) + "/CSI/CSI.ini").c_str(), "wb");

            if (iniFile)
            {
                fprintf(iniFile, "%s\n", s_MajorVersionToken);
                
                fprintf(iniFile, "\n");
                
                for (int i = 0; i < s_surfaces.GetSize(); ++i)
                {
                    fprintf(iniFile, "%s \"%s\" %d %d ", 
                        s_surfaces.Get(i)->type.c_str(),
                        s_surfaces.Get(i)->name.c_str(),
                        s_surfaces.Get(i)->inPort,
                        s_surfaces.Get(i)->outPort);

                    string type = s_surfaces.Get(i)->type;
                    
                    if (type == s_MidiSurfaceToken)
                    {
                        int refreshRate = s_surfaces.Get(i)->surfaceRefreshRate < 1 ? s_surfaceDefaultRefreshRate : s_surfaces.Get(i)->surfaceRefreshRate;
                        fprintf(iniFile, "%d ", refreshRate);
                    }
                    
                    else if (type == s_OSCSurfaceToken || type == s_OSCX32SurfaceToken)
                    {
                        fprintf(iniFile, "%s ", s_surfaces.Get(i)->remoteDeviceIP.c_str());
                        
                        int maxPacketsPerRun = s_surfaces.Get(i)->surfaceMaxPacketsPerRun < 0 ? s_surfaceDefaultMaxPacketsPerRun : s_surfaces.Get(i)->surfaceMaxPacketsPerRun;
                        
                        fprintf(iniFile, "%d ", maxPacketsPerRun);
                    }

                    fprintf(iniFile, "\n");
                }
                
                fprintf(iniFile, "\n");
                
                for (int i = 0; i < s_pages.GetSize(); ++i)
                {
                    fprintf(iniFile, "%s \"%s\"", s_PageToken, s_pages.Get(i)->name.c_str());
                    
                    if (s_pages.Get(i)->followMCP == false)
                        fprintf(iniFile, " FollowTCP");
                                        
                    if (s_pages.Get(i)->synchPages == false)
                        fprintf(iniFile, " NoSynchPages");
                    
                    if (s_pages.Get(i)->isScrollLinkEnabled == true)
                        fprintf(iniFile, " UseScrollLink");
                    
                    if (s_pages.Get(i)->scrollSynch == true)
                        fprintf(iniFile, " UseScrollSynch");

                    fprintf(iniFile, "\n");

                    for (int j = 0; j < s_pages.Get(i)->surfaces.GetSize(); ++j)
                    {
                        fprintf(iniFile, "\t\"%s\" %d %d \"%s\" \"%s\" \"%s\"\n",
                            s_pages.Get(i)->surfaces.Get(j)->pageSurfaceName.c_str(),
                            s_pages.Get(i)->surfaces.Get(j)->numChannels,
                            s_pages.Get(i)->surfaces.Get(j)->channelOffset,
                            s_pages.Get(i)->surfaces.Get(j)->templateFilename.c_str(),
                            s_pages.Get(i)->surfaces.Get(j)->zoneTemplateFolder.c_str(),
                            s_pages.Get(i)->surfaces.Get(j)->fxZoneTemplateFolder.c_str());
                    }
                    
                    fprintf(iniFile, "\n");
                    
                    for (int j = 0; j < s_pages.Get(i)->broadcasters.GetSize(); ++j)
                    {
                        if (s_pages.Get(i)->broadcasters.Get(j)->listeners.GetSize() == 0)
                            continue;
                        
                        fprintf(iniFile, "\tBroadcaster \"%s\"\n", s_pages.Get(i)->broadcasters.Get(j)->name.c_str());
                        
                        for (int k = 0; k < s_pages.Get(i)->broadcasters.Get(j)->listeners.GetSize(); ++k)
                        {
                            fprintf(iniFile, "\t\tListener \"%s\" \"", s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->name.c_str());
                            
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->goHome)
                                fprintf(iniFile, "GoHome ");
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->sends)
                                fprintf(iniFile, "Sends ");
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->receives)
                                fprintf(iniFile, "Receives ");
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->focusedFX)
                                fprintf(iniFile, "FocusedFX ");
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->focusedFXParam)
                                fprintf(iniFile, "FocusedFXParam ");
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->fxMenu)
                                fprintf(iniFile, "FXMenu ");
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->localFXSlot)
                                fprintf(iniFile, "LocalFXSlot ");
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->modifiers)
                                fprintf(iniFile, "Modifiers ");
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->selectedTrackFX)
                                fprintf(iniFile, "SelectedTrackFX ");

                            fprintf(iniFile, "\"\n");
                        }
                        
                        fprintf(iniFile, "\n");
                    }
                }

                fclose(iniFile);
            }
        }
        break;
    }
    
    return 0;
}
