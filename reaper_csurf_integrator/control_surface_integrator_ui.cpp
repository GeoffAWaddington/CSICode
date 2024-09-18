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
extern void GetParamStepsValues(vector<double> &outputVector, int numSteps);

extern int g_minNumParamSteps;
extern int g_maxNumParamSteps;

static bool s_isClearingAdvancedParameters = false;

static Widget *s_currentWidget = NULL;
static int s_currentModifier = 0;

// t = template
static string_list s_t_paramWidgets;
static string_list s_t_paramWidgetParams;
static string_list s_t_displayRows;
static string_list s_t_displayRowParams;
static string_list s_t_ringStyles;
static string_list s_t_fonts;
static bool s_t_hasColor = false;
static char s_t_paramWidget[SMLBUF];
static char s_t_nameWidget[SMLBUF];
static char s_t_valueWidget[SMLBUF];

static HWND s_hwndLearnDlg = NULL;
static HWND s_hwndLearnFXAdvancedDlg = NULL;
static HWND s_hwndLearnFXPropertiesDlg = NULL;
static int s_dlgResult = IDCANCEL;
static bool s_isAdvanceShown = false;
static bool s_isPropertiesShown = false;

static ModifierManager s_modifierManager(NULL);

static ZoneManager *s_zoneManager = NULL;
static int s_lastTouchedParamNum = -1;
static double s_lastTouchedParamValue = -1.0;
static MediaTrack *s_focusedTrack = NULL;
static int s_fxSlot = 0;
static char s_fxName[MEDBUF];
static char s_fxAlias[MEDBUF];

static int s_numChannels = 0;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct FXRowLayout
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    char suffix[SMLBUF];
    char modifiers[SMLBUF];
    int modifier;
    
    FXRowLayout()
    {
        suffix[0] = 0;
        modifiers[0] = 0;
        modifier = 0;
    }
};

static ptrvector<FXRowLayout> s_fxRowLayouts;

static ActionContext *GetContext(Widget *widget, int modifier)
{
    if (widget == NULL)
        return NULL;
    
    const WDL_PointerKeyedArray<Widget*, WDL_IntKeyedArray<WDL_PtrList<ActionContext> *> *> *contexts = s_zoneManager->GetLearnFocusedFXActionContextDictionary();
    
    if (contexts == NULL)
        return NULL;
    
    if (contexts->Exists(widget) && contexts->Get(widget)->Exists(modifier) && contexts->Get(widget)->Get(modifier)->GetSize() > 0)
        return contexts->Get(widget)->Get(modifier)->Get(0);
    else
        return NULL;
}

struct FXCell
{
    WDL_PtrList<Widget> controlWidgets;
    WDL_PtrList<Widget> displayWidgets;

    string suffix;
    int modifier;
    
    int channel;
    
    FXCell()
    {
        modifier = 0;
        channel = 0;
    }
        
    ActionContext *GetNameContext(Widget *widget)
    {
        if (widget == NULL)
            return NULL;
        
        for (int i = 0; i < displayWidgets.GetSize(); ++i)
        {
            ActionContext *paramContext = GetContext(widget, modifier);
            ActionContext *nameContext = GetContext(displayWidgets.Get(i), modifier);
            
            if (nameContext != NULL && ! strcmp(nameContext->GetAction()->GetName(), "FixedTextDisplay"))
            {
                if (paramContext != NULL && nameContext->GetParamIndex() == paramContext->GetParamIndex())
                    return nameContext;
            }
        }

        return NULL;
    }
        
    ActionContext *GetValueContext(Widget *widget)
    {
        if (widget == NULL)
            return NULL;
        
        for (int i = 0; i < displayWidgets.GetSize(); ++i)
        {
            ActionContext *paramContext = GetContext(widget, modifier);
            ActionContext *valueContext = GetContext(displayWidgets.Get(i), modifier);
            
            if (valueContext != NULL && ! strcmp(valueContext->GetAction()->GetName(), "FXParamValueDisplay"))
            {
                if (paramContext != NULL && valueContext->GetParamIndex() == paramContext->GetParamIndex())
                    return valueContext;
            }
        }

        return NULL;
    }
        
    Widget *GetNameWidget(Widget *widget)
    {
        for (int i = 0; i < displayWidgets.GetSize(); ++i)
        {
            ActionContext *paramContext = GetContext(widget, modifier);
            ActionContext *nameContext = GetContext(displayWidgets.Get(i), modifier);
            
            if (nameContext != NULL && ! strcmp(nameContext->GetAction()->GetName(), "FixedTextDisplay"))
            {
                if (paramContext != NULL && nameContext->GetParamIndex() == paramContext->GetParamIndex())
                    return displayWidgets.Get(i);
            }
        }

        return NULL;
    }
    
    Widget *GetValueWidget(Widget *widget)
    {
        for (int i = 0; i < displayWidgets.GetSize(); ++i)
        {
            ActionContext *paramContext = GetContext(widget, modifier);
            ActionContext *valueContext = GetContext(displayWidgets.Get(i), modifier);
            
            if (valueContext != NULL && ! strcmp(valueContext->GetAction()->GetName(), "FXParamValueDisplay"))
            {
                if (paramContext != NULL && valueContext->GetParamIndex() == paramContext->GetParamIndex())
                    return displayWidgets.Get(i);
            }
        }
        
        return NULL;
    }
    
    void SetNameWidget(const char *displayWidgetName, const char *paramName)
    {
        for (int i = 0; i < displayWidgets.GetSize(); ++i)
        {
            if( ! strcmp (displayWidgets.Get(i)->GetName(), displayWidgetName))
            {
                ActionContext *paramContext = GetContext(s_currentWidget, modifier);
                ActionContext *nameContext = GetContext(displayWidgets.Get(i), modifier);

                if (nameContext != NULL && paramContext != NULL)
                {
                    nameContext->SetAction(s_zoneManager->GetCSI()->GetFixedTextDisplayAction());
                    nameContext->SetParamIndex(paramContext->GetParamIndex());
                    nameContext->SetStringParam(paramName);
                }
                
                break;
            }
        }
    }
    
    void SetValueWidget(const char *displayWidgetName)
    {
        for (int i = 0; i < displayWidgets.GetSize(); ++i)
        {
            if( ! strcmp (displayWidgets.Get(i)->GetName(), displayWidgetName))
            {
                ActionContext *paramContext = GetContext(s_currentWidget, modifier);
                ActionContext *nameContext = GetContext(displayWidgets.Get(i), modifier);

                if (nameContext != NULL && paramContext != NULL)
                {
                    nameContext->SetAction(s_zoneManager->GetCSI()->GetFXParamValueDisplayAction());
                    nameContext->SetParamIndex(paramContext->GetParamIndex());
                    nameContext->SetStringParam("");
                }
                
                break;
            }
        }
    }
    
    void ClearNameDisplayWidget()
    {
        ActionContext *paramContext = GetContext(s_currentWidget, modifier);
        if (paramContext == NULL)
            return;

        for (int i = 0; i < displayWidgets.GetSize(); ++i)
        {
            ActionContext *nameContext = GetContext(displayWidgets.Get(i), modifier);
            
            if (nameContext != NULL && nameContext->GetParamIndex() == paramContext->GetParamIndex() && ! strcmp (nameContext->GetAction()->GetName(), "FixedTextDisplay"))
            {
                nameContext->SetAction(s_zoneManager->GetCSI()->GetNoActionAction());
                nameContext->SetParamIndex(0);
                nameContext->SetStringParam("");
                
                break;
            }
        }
    }
    
    void ClearValueDisplayWidget()
    {
        ActionContext *paramContext = GetContext(s_currentWidget, modifier);
        if (paramContext == NULL)
            return;
        
        for (int i = 0; i < displayWidgets.GetSize(); ++i)
        {
            ActionContext *valueContext = GetContext(displayWidgets.Get(i), modifier);
            
            if (valueContext != NULL && valueContext->GetParamIndex() == paramContext->GetParamIndex() && ! strcmp (valueContext->GetAction()->GetName(), "FXParamValueDisplay"))
            {
                valueContext->SetAction(s_zoneManager->GetCSI()->GetNoActionAction());
                valueContext->SetParamIndex(0);
                valueContext->SetStringParam("");
                
                break;
            }
        }
    }
};

static void destroyFXParamWidgetCellContextList(WDL_IntKeyedArray<FXCell *> *l) { l->Delete(true); delete l; }
WDL_PointerKeyedArray<Widget*, WDL_IntKeyedArray<FXCell *> *> s_cellMap(destroyFXParamWidgetCellContextList);

WDL_PtrList<FXCell> s_cells;

static FXCell *GetCell(Widget *widget, int modifier)
{
    if (widget == NULL)
        return NULL;

    if (s_cellMap.Exists(widget) && s_cellMap.Get(widget)->Exists(modifier))
        return s_cellMap.Get(widget)->Get(modifier);
    else
        return NULL;
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

static unsigned int s_paramButtonColors[][3] =
{
    { IDC_FXParamRingColor, IDC_FXParamRingColorBox, 0xffffffff },
    { IDC_FXParamIndicatorColor, IDC_FXParamIndicatorColorBox, 0xffffffff },
};

static unsigned int &GetParamButtonColorForID(unsigned int id)
{
    for (int x = 0; x < NUM_ELEM(s_paramButtonColors); ++x)
        if (s_paramButtonColors[x][0] == id) return s_paramButtonColors[x][2];
    WDL_ASSERT(false);
    return s_paramButtonColors[0][2];
}

static unsigned int s_nameDisplayColors[][3] =
{
    { IDC_FixedTextDisplayForegroundColor, IDC_FXFixedTextDisplayForegroundColorBox, 0xffffffff },
    { IDC_FixedTextDisplayBackgroundColor, IDC_FXFixedTextDisplayBackgroundColorBox, 0xffffffff },
};

static unsigned int &GetNameDisplayButtonColorForID(unsigned int id)
{
    for (int x = 0; x < NUM_ELEM(s_nameDisplayColors); ++x)
        if (s_paramButtonColors[x][0] == id) return s_nameDisplayColors[x][2];
    WDL_ASSERT(false);
    return s_nameDisplayColors[0][2];
}

static unsigned int s_valueDisplayColors[][3] =
{
    { IDC_FXParamDisplayForegroundColor, IDC_FXParamValueDisplayForegroundColorBox, 0xffffffff },
    { IDC_FXParamDisplayBackgroundColor, IDC_FXParamValueDisplayBackgroundColorBox, 0xffffffff },
};

static unsigned int &GetValueDisplayButtonColorForID(unsigned int id)
{
    for (int x = 0; x < NUM_ELEM(s_valueDisplayColors); ++x)
        if (s_paramButtonColors[x][0] == id) return s_valueDisplayColors[x][2];
    WDL_ASSERT(false);
    return s_nameDisplayColors[0][2];
}

static unsigned int &GetButtonColorForID(unsigned int id)
{
    for (int x = 0; x < NUM_ELEM(s_buttonColors); ++x)
        if (s_buttonColors[x][0] == id) return s_buttonColors[x][2];
    WDL_ASSERT(false);
    return s_buttonColors[0][2];
}

static ActionContext *context = NULL;

static WDL_DLGRET dlgProcEditAdvanced(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    char buf[MEDBUF];
    
    int modifier = 0;
    
    if (s_zoneManager)
    {
        const WDL_TypedBuf<int> &modifiers = s_zoneManager->GetSurface()->GetModifiers();
        
        if (modifiers.GetSize() > 0)
            modifier = modifiers.Get()[0];
    }
        
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            ActionContext *context = GetContext(s_currentWidget, modifier);
            
            if (context == NULL)
                break;

            char titleBuf[MEDBUF];
            titleBuf[0] = 0;
            
            char modifierBuf[SMLBUF];
            s_modifierManager.GetModifierString(modifier, modifierBuf, sizeof(modifierBuf));

            char paramName[MEDBUF];
            GetDlgItemText(s_hwndLearnDlg, IDC_FXParamNameEdit, paramName, sizeof(paramName));
            
            snprintf(titleBuf, sizeof(titleBuf), "%s%s - %s", modifierBuf, s_currentWidget->GetName(), paramName);
            
            SetWindowText(hwndDlg, titleBuf);
            
            snprintf(buf, sizeof(buf), "%0.2f", context->GetDeltaValue());
            SetDlgItemText(hwndDlg, IDC_EDIT_Delta, buf);
            
            snprintf(buf, sizeof(buf), "%0.2f", context->GetRangeMinimum());
            SetDlgItemText(hwndDlg, IDC_EDIT_RangeMin, buf);
            
            snprintf(buf, sizeof(buf), "%0.2f", context->GetRangeMaximum());
            SetDlgItemText(hwndDlg, IDC_EDIT_RangeMax, buf);

            
            char tmp[MEDBUF];
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
            WDL_FastString ticks;
            ticks.Set("");
            
            for (int i = 0; i < (int)acceleratedTickCounts.size(); ++i)
            {
                snprintf(buf, sizeof(buf), "%d ", acceleratedTickCounts[i]);
                ticks.Append(buf);
            }
            SetDlgItemText(hwndDlg, IDC_EDIT_TickValues, ticks.Get());
        }
            break;
            
        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case IDOK:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        if (context == NULL)
                        {
                            s_dlgResult = IDCANCEL;
                            EndDialog(hwndDlg, 0);
                            return 0;
                        }

                        GetDlgItemText(hwndDlg, IDC_EDIT_Delta, buf, sizeof(buf));
                        context->SetDeltaValue(atof(buf));

                        GetDlgItemText(hwndDlg, IDC_EDIT_RangeMin, buf, sizeof(buf));
                        context->SetRangeMinimum(atof(buf));

                        GetDlgItemText(hwndDlg, IDC_EDIT_RangeMax, buf, sizeof(buf));
                        context->SetRangeMaximum(atof(buf));

                        GetDlgItemText(hwndDlg, IDC_EDIT_DeltaValues, buf, sizeof(buf));
                        string_list tokens;
                        GetTokens(tokens, buf);
                        vector<double> deltas;
                        for (int i = 0; i < tokens.size(); ++i)
                            deltas.push_back(atof(tokens[i]));
                        context->SetAccelerationValues(deltas);
                     
                        GetDlgItemText(hwndDlg, IDC_EDIT_TickValues, buf, sizeof(buf));
                        tokens.clear();
                        GetTokens(tokens, buf);
                        vector<int> ticks;
                        for (int i = 0; i < tokens.size(); ++i)
                            ticks.push_back(atoi(tokens[i]));
                        context->SetTickCounts(ticks);
                       
                        GetDlgItemText(hwndDlg, IDC_EditSteps, buf, sizeof(buf));
                        tokens.clear();
                        GetTokens(tokens, buf);
                        vector<double> steps;
                        for (int i = 0; i < tokens.size(); ++i)
                            steps.push_back(atof(tokens[i]));
                        context->SetStepValues(steps);

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

static void LoadTemplates()
{
    s_fxRowLayouts.clear();
    s_t_paramWidget[0] = 0;
    s_t_nameWidget[0] = 0;
    s_t_valueWidget[0] = 0;

    s_t_paramWidgets.clear();
    s_t_displayRows.clear();
    s_t_ringStyles.clear();
    s_t_fonts.clear();
    
    s_t_hasColor = false;

    const WDL_StringKeyedArray<CSIZoneInfo*> &zoneInfo = s_zoneManager->GetZoneInfo();
    
    if (s_zoneManager == NULL || ! zoneInfo.Exists("FXRowLayout") || ! zoneInfo.Exists("FXWidgetLayout"))
        return;

    try
    {
        fpistream file(zoneInfo.Get("FXRowLayout")->filePath.c_str());
        
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
                    
                    strcpy(t.suffix, tokens[1]);
                    strcpy(t.modifiers, tokens[0]);
                    t.modifier = s_zoneManager->GetSurface()->GetModifierManager()->GetModifierValue(tokens[0]);
                    s_fxRowLayouts.push_back(t);
                }
            }
        }
    }
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s\n", zoneInfo.Get("FXRowLayout")->filePath.c_str());
        ShowConsoleMsg(buffer);
    }

    try
    {
        fpistream file(zoneInfo.Get("FXWidgetLayout")->filePath.c_str());

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
                        if (tokens[0] == "#WidgetType" && tokens.size() > 1)
                        {
                            s_t_paramWidgets.push_back(tokens[1]);

                            if (tokens.size() > 2)
                                s_t_paramWidgetParams.push_back(line.substr(line.find(tokens[2]), line.length() - 1).c_str());
                        }
                        else if (tokens[0] == "#DisplayRow" && tokens.size() > 1)
                        {
                            s_t_displayRows.push_back(tokens[1]);

                            if (tokens.size() > 2)
                                s_t_displayRowParams.push_back(line.substr(line.find(tokens[2]), line.length() - 1).c_str());
                        }
                        else if (tokens[0] == "#RingStyle" && tokens.size() > 1)
                            s_t_ringStyles.push_back(tokens[1]);
                        else if (tokens[0] == "#DisplayFont" && tokens.size() > 1)
                            s_t_fonts.push_back(tokens[1]);
                        else if (tokens[0] == "#SupportsColor")
                            s_t_hasColor = true;
                    }
                    else
                    {
                        if (tokens.size() > 1 && tokens[1] == "FXParam")
                            strcpy(s_t_paramWidget, tokens[0]);
                        if (tokens.size() > 1 && tokens[1] == "FixedTextDisplay")
                            strcpy(s_t_nameWidget, tokens[0]);
                        if (tokens.size() > 1 && tokens[1] == "FXParamValueDisplay")
                            strcpy(s_t_valueWidget, tokens[0]);
                    }
                }
            }
        }
    }
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s\n", zoneInfo.Get("FXWidgetLayout")->filePath.c_str());
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

static void SaveZone()
{
    if (s_zoneManager == NULL || s_focusedTrack == NULL || s_fxName[0] == 0)
        return;
    
    char buf[MEDBUF];
    buf[0] = 0;
    
    char path[BUFSIZ];
    snprintf(path, sizeof(path), "%s/CSI/Zones/%s/AutoGeneratedFXZones", GetResourcePath(), s_zoneManager->GetFXZoneFolder());

    try
    {
        RecursiveCreateDirectory(path,0);
        
        string trimmedFXName = s_fxName;
        ReplaceAllWith(trimmedFXName, s_BadFileChars, "_");
        
        snprintf(path, sizeof(path), "%s/%s.zon", path, trimmedFXName.c_str());
        
        FILE *fxFile = fopenUTF8(path,"wb");
        
        if (fxFile)
        {
            fprintf(fxFile, "Zone \"%s\" \"%s\"\n", s_fxName, s_fxAlias);
            
            if (s_zoneManager->GetZoneInfo().Exists("FXPrologue"))
            {
                fpistream file(s_zoneManager->GetZoneInfo().Get("FXPrologue")->filePath.c_str());
                    
                for (string line; getline(file, line) ; )
                    if (line.find("Zone") != 0)
                        fprintf(fxFile, "%s\n", line.c_str());
            }
            
            fprintf(fxFile, "\n%s\n\n", s_BeginAutoSection);
                        
            int previousChannel = 1;
            
            for (int cellIdx = 0; cellIdx < s_cells.GetSize(); ++cellIdx)
            {
                FXCell *cell = s_cells.Get(cellIdx);
               
                if (previousChannel > cell->channel)
                {
                    fprintf(fxFile, "\n\n");
                    previousChannel = 1;
                }
                else
                    previousChannel++;

                int modifier = cell->modifier;
                s_modifierManager.GetModifierString(modifier, buf, sizeof(buf));
                
                for (int controlIdx = 0; controlIdx < cell->controlWidgets.GetSize(); ++controlIdx)
                {
                    Widget *widget = cell->controlWidgets.Get(controlIdx);
                    
                    fprintf(fxFile, "\t%s%s ", buf, widget->GetName());

                    if (ActionContext *context = GetContext(widget, modifier))
                    {
                        char actionName[SMLBUF];
                        snprintf(actionName, sizeof(actionName), "%s", context->GetAction()->GetName());
                        
                        fprintf(fxFile, "%s ", actionName);

                        if (strcmp(actionName, "NoAction"))
                        {
                            fprintf(fxFile, "%d ", context->GetParamIndex());
                            
                            context->GetWidgetProperties().save_list(fxFile);
                            
                            fprintf(fxFile, "[ %0.2f>%0.2f ", context->GetRangeMinimum(), context->GetRangeMaximum());
                            
                            fprintf(fxFile, "(");
                            
                            if (context->GetAcceleratedDeltaValues().size() > 0)
                            {
                                for (int i = 0; i < context->GetAcceleratedDeltaValues().size(); ++i)
                                {
                                    if ( i < context->GetAcceleratedDeltaValues().size() - 1)
                                        fprintf(fxFile, "%0.2f,", context->GetAcceleratedDeltaValues()[i]);
                                    else
                                        fprintf(fxFile, "%0.2f", context->GetAcceleratedDeltaValues()[i]);
                                }
                            }
                            else
                                fprintf(fxFile, "%0.2f", context->GetDeltaValue());
                                
                            fprintf(fxFile, ") ");

                            fprintf(fxFile, "(");
                            
                            if (context->GetAcceleratedTickCounts().size() > 0)
                            {
                                for (int i = 0; i < context->GetAcceleratedTickCounts().size(); ++i)
                                {
                                    if ( i < context->GetAcceleratedTickCounts().size() - 1)
                                        fprintf(fxFile, "%d,", context->GetAcceleratedTickCounts()[i]);
                                    else
                                        fprintf(fxFile, "%d", context->GetAcceleratedTickCounts()[i]);
                                }
                            }
                                
                            fprintf(fxFile, ") ");
                            
                            if (context->GetSteppedValues().size() > 0)
                            {
                                for (int i = 0; i < context->GetSteppedValues().size(); ++i)
                                    fprintf(fxFile, "%0.2f ", context->GetSteppedValues()[i]);
                            }
                            
                            fprintf(fxFile, " ]");
                        }
                    }
                    
                    fprintf(fxFile, "\n");
                }
                
                for (int displayIdx = 0; displayIdx < cell->displayWidgets.GetSize(); ++displayIdx)
                {
                    Widget *widget = cell->displayWidgets.Get(displayIdx);
                    
                    fprintf(fxFile, "\t%s%s ", buf, widget->GetName());

                    if (ActionContext *context = GetContext(widget, modifier))
                    {
                        char actionName[SMLBUF];
                        snprintf(actionName, sizeof(actionName), "%s", context->GetAction()->GetName());
                        
                        fprintf(fxFile, "%s ", actionName);

                        if (strcmp(actionName, "NoAction"))
                        {
                            if ( ! strcmp(actionName, "FixedTextDisplay"))
                                fprintf(fxFile, "\"%s\" %d ", context->GetStringParam(), context->GetParamIndex());
                            else if ( ! strcmp(actionName, "FXParamValueDisplay"))
                                fprintf(fxFile, "%d ", context->GetParamIndex());
                            
                            context->GetWidgetProperties().save_list(fxFile);
                        }
                        
                        fprintf(fxFile, "\n");
                    }
                }

                fprintf(fxFile, "\n");
            }
            
            fprintf(fxFile, "\n%s\n\n", s_EndAutoSection);

            if (s_zoneManager->GetZoneInfo().Exists("FXEpilogue"))
            {
                fpistream file(s_zoneManager->GetZoneInfo().Get("FXEpilogue")->filePath.c_str());
                    
                for (string line; getline(file, line) ; )
                    if (line.find("Zone") != 0)
                        fprintf(fxFile, "%s\n", line.c_str());
            }

            fclose(fxFile);
        }
    }
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble saving %s\n", path);
        ShowConsoleMsg(buffer);
    }
}

static void CreateContextMap()
{
    s_cellMap.DeleteAll(true);
    s_cells.Empty();
    
    const WDL_PointerKeyedArray<Widget*, WDL_IntKeyedArray<WDL_PtrList<ActionContext> *> *> *zoneContexts = s_zoneManager->GetLearnFocusedFXActionContextDictionary();

    if (zoneContexts == NULL)
        return;
    
    char widgetName[SMLBUF];
    
    for (int rowLayoutIdx = 0; rowLayoutIdx < s_fxRowLayouts.size(); ++rowLayoutIdx)
    {
        int modifier = s_fxRowLayouts[rowLayoutIdx].modifier;
        
        for (int channel = 1; channel <= s_numChannels; ++channel)
        {
            FXCell *cell = new FXCell();
            cell->suffix = s_fxRowLayouts[rowLayoutIdx].suffix;
            cell->modifier = modifier;
            cell->channel = channel;
            s_cells.Add(cell);

            for (int widgetTypesIdx = 0; widgetTypesIdx < s_t_paramWidgets.size(); ++widgetTypesIdx)
            {
                snprintf(widgetName, sizeof(widgetName), "%s%s%d", s_t_paramWidgets[widgetTypesIdx].c_str(), s_fxRowLayouts[rowLayoutIdx].suffix, channel);
                if (Widget *widget = s_zoneManager->GetSurface()->GetWidgetByName(widgetName))
                    cell->controlWidgets.Add(widget);
            }
            
            for (int widgetTypesIdx = 0; widgetTypesIdx < s_t_displayRows.size(); ++widgetTypesIdx)
            {
                snprintf(widgetName, sizeof(widgetName), "%s%s%d", s_t_displayRows[widgetTypesIdx].c_str(), s_fxRowLayouts[rowLayoutIdx].suffix, channel);
                if (Widget *widget = s_zoneManager->GetSurface()->GetWidgetByName(widgetName))
                    cell->displayWidgets.Add(widget);
            }
            
            for (int i = 0; i < cell->controlWidgets.GetSize(); ++i)
            {
                Widget *widget = cell->controlWidgets.Get(i);
                
                if (! s_cellMap.Exists(widget))
                {
                    WDL_IntKeyedArray<FXCell *> *m = new WDL_IntKeyedArray<FXCell *>();
                    s_cellMap.Insert(widget, m);
                }

                s_cellMap.Get(widget)->Insert(modifier, cell);
            }
        }
    }
}

void InitBlankLearnFocusedFXZone()
{
    Zone *zone = s_zoneManager->GetLearnedFocusedFXZone();
    if (zone == NULL)
        return;
    
    if (s_zoneManager->GetZoneInfo().Exists("FXPrologue"))
        s_zoneManager->LoadZoneFile(zone, s_zoneManager->GetZoneInfo().Get("FXPrologue")->filePath.c_str(), "");
      
    char widgetName[MEDBUF];

    string_list blankParams;
    
    for (int rowLayoutIdx = 0; rowLayoutIdx < s_fxRowLayouts.size(); ++rowLayoutIdx)
    {
        int modifier = s_fxRowLayouts[rowLayoutIdx].modifier;
        
        for (int channel = 1; channel <= s_numChannels; ++channel)
        {
            for (int widgetTypesIdx = 0; widgetTypesIdx < s_t_paramWidgets.size(); ++widgetTypesIdx)
            {
                snprintf(widgetName, sizeof(widgetName), "%s%s%d", s_t_paramWidgets[widgetTypesIdx].c_str(), s_fxRowLayouts[rowLayoutIdx].suffix, channel);
                if (Widget *widget = s_zoneManager->GetSurface()->GetWidgetByName(widgetName))
                {
                    zone->AddWidget(widget);
                    ActionContext *context = s_zoneManager->GetCSI()->GetActionContext("NoAction", widget, zone, blankParams);
                    zone->AddActionContext(widget, modifier, context);
                }
            }
            
            for (int widgetTypesIdx = 0; widgetTypesIdx < s_t_displayRows.size(); ++widgetTypesIdx)
            {
                snprintf(widgetName, sizeof(widgetName), "%s%s%d", s_t_displayRows[widgetTypesIdx].c_str(), s_fxRowLayouts[rowLayoutIdx].suffix, channel);
                if (Widget *widget = s_zoneManager->GetSurface()->GetWidgetByName(widgetName))
                {
                    zone->AddWidget(widget);
                    ActionContext *context = s_zoneManager->GetCSI()->GetActionContext("NoAction", widget, zone, blankParams);
                    zone->AddActionContext(widget, modifier, context);
                }
            }
        }
    }
    
    if (s_zoneManager->GetZoneInfo().Exists("FXEpilogue"))
        s_zoneManager->LoadZoneFile(zone, s_zoneManager->GetZoneInfo().Get("FXEpilogue")->filePath.c_str(), "");
    
    CreateContextMap();
}

static void HandleInitAdvancedLearnFXDialog()
{
    SendDlgItemMessage(s_hwndLearnFXAdvancedDlg, IDC_PickRingStyle, CB_RESETCONTENT, 0, 0);

    for (int i = 0; i < s_t_ringStyles.size(); ++i)
        SendDlgItemMessage(s_hwndLearnFXAdvancedDlg, IDC_PickRingStyle, CB_ADDSTRING, 0, (LPARAM)s_t_ringStyles[i].c_str());
    
    SendDlgItemMessage(s_hwndLearnFXAdvancedDlg, IDC_PickSteps, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessage(s_hwndLearnFXAdvancedDlg, IDC_PickSteps, CB_ADDSTRING, 0, (LPARAM)"0");
    
    for (int step = g_minNumParamSteps; step <= g_maxNumParamSteps; ++step)
    {
        char buf[SMLBUF];
        snprintf(buf, sizeof(buf), "%d", step);
        SendDlgItemMessage(s_hwndLearnFXAdvancedDlg, IDC_PickSteps, CB_ADDSTRING, 0, (LPARAM)buf);
    }

    SendDlgItemMessage(s_hwndLearnFXPropertiesDlg, IDC_FixedTextDisplayPickFont, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessage(s_hwndLearnFXPropertiesDlg, IDC_FXParamValueDisplayPickFont, CB_RESETCONTENT, 0, 0);

    for (int i = 0; i < s_t_fonts.size(); ++i)
    {
        SendDlgItemMessage(s_hwndLearnFXPropertiesDlg, IDC_FixedTextDisplayPickFont, CB_ADDSTRING, 0, (LPARAM)s_t_fonts[i].c_str());
        SendDlgItemMessage(s_hwndLearnFXPropertiesDlg, IDC_FXParamValueDisplayPickFont, CB_ADDSTRING, 0, (LPARAM)s_t_fonts[i].c_str());
    }
}

static void ClearAdvancedParams()
{
    s_isClearingAdvancedParameters = true;
    
    HWND hwndDlg = s_hwndLearnFXAdvancedDlg;
    
    if (hwndDlg == NULL)
        return;
 
    SetWindowText(hwndDlg, "Advanced");
    SetWindowText(GetDlgItem(hwndDlg, IDC_GroupFXControl), "Control");
    SetDlgItemText(hwndDlg, IDC_PickRingStyle, "");
    SetDlgItemText(hwndDlg, IDC_PickSteps, "");
    SetWindowText(GetDlgItem(hwndDlg, IDC_FXParamNameEdit), "");
    SetDlgItemText(hwndDlg, IDC_COMBO_PickNameDisplay, "");
    SetDlgItemText(hwndDlg, IDC_COMBO_PickValueDisplay, "");
    
    hwndDlg = s_hwndLearnFXPropertiesDlg;
    
    if (hwndDlg == NULL)
        return;
    
    SetDlgItemText(hwndDlg, IDC_FixedTextDisplayPickFont, "");
    SetWindowText(GetDlgItem(hwndDlg, IDC_Edit_FixedTextDisplayTop), "");
    SetWindowText(GetDlgItem(hwndDlg, IDC_Edit_FixedTextDisplayBottom), "");

    SetDlgItemText(hwndDlg, IDC_FXParamValueDisplayPickFont, "");
    SetWindowText(GetDlgItem(hwndDlg, IDC_Edit_ParamValueDisplayTop), "");
    SetWindowText(GetDlgItem(hwndDlg, IDC_Edit_ParamValueDisplayBottom), "");

    for (int i = 0; i < NUM_ELEM(s_buttonColors); ++i)
        s_buttonColors[i][2] = 0xedededff;
    
    RECT rect;
    GetClientRect(hwndDlg, &rect);
    InvalidateRect(hwndDlg, &rect, 0);

    s_isClearingAdvancedParameters = false;
}

static void GetFullWidgetName(Widget* widget, int modifier, char *widgetNamBuf, int bufSize)
{
    if (widget == NULL)
        return;
    
    char modifierBuf[SMLBUF];
    s_modifierManager.GetModifierString(modifier, modifierBuf, sizeof(modifierBuf));
    snprintf(widgetNamBuf, bufSize, "%s%s", modifierBuf, widget->GetName());
}

static void FillPropertiesParams()
{
    if (s_currentWidget == NULL)
        return;
    
    FXCell *cell = GetCell(s_currentWidget, s_currentModifier);
    
    if ( cell == NULL)
        return;
    
    if (cell->displayWidgets.GetSize() < 2)
        return;
    
    ActionContext *paramContext = GetContext(s_currentWidget, s_currentModifier);
    ActionContext *nameContext = GetContext(cell->GetNameWidget(s_currentWidget), s_currentModifier);
    ActionContext *valueContext = GetContext(cell->GetValueWidget(s_currentWidget), s_currentModifier);
    
    if (paramContext == NULL)
        return;

    char buf[MEDBUF];
    buf[0] = 0;

    rgba_color defaultColor;
    defaultColor.r = 237;
    defaultColor.g = 237;
    defaultColor.b = 237;
    
    const char *ringColor = paramContext->GetWidgetProperties().get_prop(PropertyType_LEDRingColor);
    if (ringColor)
    {
        rgba_color color;
        GetColorValue(ringColor, color);
        GetButtonColorForID(IDC_FXParamRingColor) = ColorToNative(color.r, color.g, color.b);
    }
    else
        GetButtonColorForID(IDC_FXParamRingColor) = ColorToNative(defaultColor.r, defaultColor.g, defaultColor.b);
    
    const char *pushColor = paramContext->GetWidgetProperties().get_prop(PropertyType_PushColor);
    if (pushColor)
    {
        rgba_color color;
        GetColorValue(pushColor, color);
        GetButtonColorForID(IDC_FXParamIndicatorColor) = ColorToNative(color.r, color.g, color.b);
    }
    else
        GetButtonColorForID(IDC_FXParamIndicatorColor) = ColorToNative(defaultColor.r, defaultColor.g, defaultColor.b);
    
    const char *property;
    const char *foreground;
    const char *background;
    
    if (nameContext)
    {
        property = nameContext->GetWidgetProperties().get_prop(PropertyType_Font);
        if (property)
            SetDlgItemText(s_hwndLearnFXPropertiesDlg, IDC_FixedTextDisplayPickFont, property);
        else
            SetDlgItemText(s_hwndLearnFXPropertiesDlg, IDC_FixedTextDisplayPickFont, "");
        
        property = nameContext->GetWidgetProperties().get_prop(PropertyType_TopMargin);
        if (property)
            SetDlgItemText(s_hwndLearnFXPropertiesDlg, IDC_Edit_FixedTextDisplayTop, property);
        else
            SetDlgItemText(s_hwndLearnFXPropertiesDlg, IDC_Edit_FixedTextDisplayTop, "");
        
        property = nameContext->GetWidgetProperties().get_prop(PropertyType_BottomMargin);
        if (property)
            SetDlgItemText(s_hwndLearnFXPropertiesDlg, IDC_Edit_FixedTextDisplayBottom, property);
        else
            SetDlgItemText(s_hwndLearnFXPropertiesDlg, IDC_Edit_FixedTextDisplayBottom, "");
        
        foreground = nameContext->GetWidgetProperties().get_prop(PropertyType_TextColor);
        if (foreground)
        {
            rgba_color color;
            GetColorValue(foreground, color);
            GetButtonColorForID(IDC_FixedTextDisplayForegroundColor) = ColorToNative(color.r, color.g, color.b);
        }
        else
            GetButtonColorForID(IDC_FixedTextDisplayForegroundColor) = ColorToNative(defaultColor.r, defaultColor.g, defaultColor.b);
        
        background = nameContext->GetWidgetProperties().get_prop(PropertyType_BackgroundColor);
        if (background)
        {
            rgba_color color;
            GetColorValue(background, color);
            GetButtonColorForID(IDC_FixedTextDisplayBackgroundColor) = ColorToNative(color.r, color.g, color.b);
        }
        else
            GetButtonColorForID(IDC_FixedTextDisplayBackgroundColor) = ColorToNative(defaultColor.r, defaultColor.g, defaultColor.b);
    }
    
    if (valueContext)
    {
        SendDlgItemMessage(s_hwndLearnFXPropertiesDlg, IDC_COMBO_PickValueDisplay, CB_RESETCONTENT, 0, 0);
        SendDlgItemMessage(s_hwndLearnFXPropertiesDlg, IDC_COMBO_PickValueDisplay, CB_ADDSTRING, 0, (LPARAM)"None");
        for (int i = 0; i < s_t_displayRows.size(); ++i)
        {
            /////////////////////////////////////////////////
            //snprintf(buf, sizeof(buf), "%s%d", s_t_displayRows[i].c_str(), channel);
            SendDlgItemMessage(s_hwndLearnFXPropertiesDlg, IDC_COMBO_PickValueDisplay, CB_ADDSTRING, 0, (LPARAM)buf);
        }

        property = valueContext->GetWidgetProperties().get_prop(PropertyType_Font);
        if (property)
            SetDlgItemText(s_hwndLearnFXPropertiesDlg, IDC_FXParamValueDisplayPickFont, property);
        else
            SetDlgItemText(s_hwndLearnFXPropertiesDlg, IDC_FXParamValueDisplayPickFont, "");
        
        property = valueContext->GetWidgetProperties().get_prop(PropertyType_TopMargin);
        if (property)
            SetDlgItemText(s_hwndLearnFXPropertiesDlg, IDC_Edit_ParamValueDisplayTop, property);
        else
            SetDlgItemText(s_hwndLearnFXPropertiesDlg, IDC_Edit_ParamValueDisplayTop, "");
        
        property = valueContext->GetWidgetProperties().get_prop(PropertyType_BottomMargin);
        if (property)
            SetDlgItemText(s_hwndLearnFXPropertiesDlg, IDC_Edit_ParamValueDisplayBottom, property);
        else
            SetDlgItemText(s_hwndLearnFXPropertiesDlg, IDC_Edit_ParamValueDisplayBottom, "");
        
        foreground = valueContext->GetWidgetProperties().get_prop(PropertyType_TextColor);
        if (foreground)
        {
            rgba_color color;
            GetColorValue(foreground, color);
            GetButtonColorForID(IDC_FXParamDisplayForegroundColor) = ColorToNative(color.r, color.g, color.b);
        }
        else
            GetButtonColorForID(IDC_FXParamDisplayForegroundColor) = ColorToNative(defaultColor.r, defaultColor.g, defaultColor.b);
        
        background = valueContext->GetWidgetProperties().get_prop(PropertyType_BackgroundColor);
        if (background)
        {
            rgba_color color;
            GetColorValue(background, color);
            GetButtonColorForID(IDC_FXParamDisplayBackgroundColor) = ColorToNative(color.r, color.g, color.b);
        }
        else
            GetButtonColorForID(IDC_FXParamDisplayBackgroundColor) = ColorToNative(defaultColor.r, defaultColor.g, defaultColor.b);
    }
    
    RECT rect;
    GetClientRect(s_hwndLearnFXPropertiesDlg, &rect);
    InvalidateRect(s_hwndLearnFXPropertiesDlg, &rect, 0);
}

static void FillAdvancedParams()
{
    if (s_currentWidget == NULL)
        return;
    
    FXCell *cell = GetCell(s_currentWidget, s_currentModifier);
    
    if ( cell == NULL)
        return;
    
    if (cell->displayWidgets.GetSize() < 2)
        return;
    
    ActionContext *paramContext = GetContext(s_currentWidget, s_currentModifier);
    
    if (paramContext == NULL)
        return;

    char buf[MEDBUF];
    buf[0] = 0;
    
    const char *ringstyle = paramContext->GetWidgetProperties().get_prop(PropertyType_RingStyle);
    if (ringstyle)
        SetDlgItemText(s_hwndLearnFXAdvancedDlg, IDC_PickRingStyle, ringstyle);
    else
        SendMessage(GetDlgItem(s_hwndLearnFXAdvancedDlg, IDC_PickRingStyle), CB_SETCURSEL, 0, 0);

    int numSteps = paramContext->GetNumberOfSteppedValues();
    if (numSteps)
    {
        snprintf(buf, sizeof(buf), "%d", numSteps);
        SetDlgItemText(s_hwndLearnFXAdvancedDlg, IDC_PickSteps, buf);
    }
    else
        SetDlgItemText(s_hwndLearnFXAdvancedDlg, IDC_PickSteps, "0");

    if (ActionContext *nameContext = cell->GetNameContext(s_currentWidget))
        SetWindowText(GetDlgItem(s_hwndLearnFXAdvancedDlg, IDC_FXParamNameEdit), nameContext->GetStringParam());
    else
        SetWindowText(GetDlgItem(s_hwndLearnFXAdvancedDlg, IDC_FXParamNameEdit), "");

    if (s_cellMap.Exists(s_currentWidget) && s_cellMap.Get(s_currentWidget)->Exists(s_currentModifier))
    {
        FXCell *cell = s_cellMap.Get(s_currentWidget)->Get(s_currentModifier);
        
        SendDlgItemMessage(s_hwndLearnFXAdvancedDlg, IDC_COMBO_PickNameDisplay, CB_RESETCONTENT, 0, 0);
        SendDlgItemMessage(s_hwndLearnFXAdvancedDlg, IDC_COMBO_PickNameDisplay, CB_ADDSTRING, 0, (LPARAM)"");
        SendDlgItemMessage(s_hwndLearnFXAdvancedDlg, IDC_COMBO_PickValueDisplay, CB_RESETCONTENT, 0, 0);
        SendDlgItemMessage(s_hwndLearnFXAdvancedDlg, IDC_COMBO_PickValueDisplay, CB_ADDSTRING, 0, (LPARAM)"");

        for (int i = 0; i < cell->displayWidgets.GetSize(); ++i)
        {
            SendDlgItemMessage(s_hwndLearnFXAdvancedDlg, IDC_COMBO_PickNameDisplay, CB_ADDSTRING, 0, (LPARAM)cell->displayWidgets.Get(i)->GetName());
            SendDlgItemMessage(s_hwndLearnFXAdvancedDlg, IDC_COMBO_PickValueDisplay, CB_ADDSTRING, 0, (LPARAM)cell->displayWidgets.Get(i)->GetName());
        }
        
        if (cell->GetNameWidget(s_currentWidget))
        {
            int index = (int)SendMessage(GetDlgItem(s_hwndLearnFXAdvancedDlg, IDC_COMBO_PickNameDisplay), CB_FINDSTRINGEXACT, -1, (LPARAM)cell->GetNameWidget(s_currentWidget)->GetName());
            if (index >= 0)
                SendMessage(GetDlgItem(s_hwndLearnFXAdvancedDlg, IDC_COMBO_PickNameDisplay), CB_SETCURSEL, index, 0);
            else
                SendMessage(GetDlgItem(s_hwndLearnFXAdvancedDlg, IDC_COMBO_PickNameDisplay), CB_SETCURSEL, 0, 0);
        }
        else
            SendMessage(GetDlgItem(s_hwndLearnFXAdvancedDlg, IDC_COMBO_PickNameDisplay), CB_SETCURSEL, 0, 0);

        if (cell->GetValueWidget(s_currentWidget))
        {
            int index = (int)SendMessage(GetDlgItem(s_hwndLearnFXAdvancedDlg, IDC_COMBO_PickValueDisplay), CB_FINDSTRINGEXACT, -1, (LPARAM)cell->GetValueWidget(s_currentWidget)->GetName());
            if (index >= 0)
                SendMessage(GetDlgItem(s_hwndLearnFXAdvancedDlg, IDC_COMBO_PickValueDisplay), CB_SETCURSEL, index, 0);
            else
                SendMessage(GetDlgItem(s_hwndLearnFXAdvancedDlg, IDC_COMBO_PickValueDisplay), CB_SETCURSEL, 0, 0);
        }
        else
            SendMessage(GetDlgItem(s_hwndLearnFXAdvancedDlg, IDC_COMBO_PickValueDisplay), CB_SETCURSEL, 0, 0);
    }
    
    FillPropertiesParams();

    paramContext->GetWidget()->Configure(s_zoneManager->GetLearnedFocusedFXZone()->GetActionContexts(s_currentWidget));
}

static void FillParams()
{
    if (s_currentWidget == NULL)
        return;
    
    char buf[MEDBUF];
    GetFullWidgetName(s_currentWidget, s_currentModifier, buf, sizeof(buf));
    SetDlgItemText(s_hwndLearnDlg, IDC_WidgetName, buf);

    FXCell *cell = GetCell(s_currentWidget, s_currentModifier);
    
    if ( cell == NULL)
        return;
    
    if (cell->displayWidgets.GetSize() < 2)
        return;

    ActionContext *paramContext = GetContext(s_currentWidget, s_currentModifier);
 
    char modifierBuf[SMLBUF];
    s_modifierManager.GetModifierString(s_currentModifier, modifierBuf, sizeof(modifierBuf));

    buf[0] = 0;
    
    if (paramContext == NULL)
    {
        ClearAdvancedParams();
        return;
    }
        
    if ( ! strcmp (paramContext->GetAction()->GetName(), "NoAction"))
    {
        SetWindowText(s_hwndLearnFXAdvancedDlg, "Advanced");
        SetDlgItemText(s_hwndLearnFXAdvancedDlg, IDC_GroupFXControl, "Control");
        ClearAdvancedParams();
    }
    else
    {
        TrackFX_GetParamName(s_focusedTrack, s_fxSlot, s_lastTouchedParamNum, buf, sizeof(buf));
        SetDlgItemText(s_hwndLearnDlg, IDC_ParamName, buf);

        char fullName[MEDBUF];
        snprintf(fullName, sizeof(fullName), "%s%s", modifierBuf, s_currentWidget->GetName());
        SetWindowText(s_hwndLearnFXAdvancedDlg, fullName);
        SetWindowText(s_hwndLearnFXPropertiesDlg, fullName);
        SetWindowText(GetDlgItem(s_hwndLearnFXAdvancedDlg, IDC_GroupFXControl), fullName);
        SetWindowText(GetDlgItem(s_hwndLearnFXPropertiesDlg, IDC_GroupFXControl), fullName);
        FillAdvancedParams();
    }
}

static void HandlePopulateBasicLearnFXDialog()
{
    s_lastTouchedParamNum = -1;
            
    TrackFX_GetFXName(s_focusedTrack, s_fxSlot, s_fxName, sizeof(s_fxName));
                
    char buf[MEDBUF];
    snprintf(buf, sizeof(buf), "%s    %s", s_zoneManager->GetSurface()->GetName(), s_fxName);

    SetWindowText(s_hwndLearnDlg, buf);
    
    s_zoneManager->LoadLearnFocusedFXZone(s_focusedTrack, s_fxName, s_fxSlot);

    SetDlgItemText(s_hwndLearnFXAdvancedDlg, IDC_EditFXAlias, s_fxAlias);
    
    CreateContextMap();
}

void WidgetMoved(Widget *widget, int modifier)
{
    if (s_focusedTrack == NULL)
        return;
    
    if (s_zoneManager == NULL)
        return;
  
    if (s_zoneManager->GetLearnedFocusedFXZone() == NULL)
        return;
    
    if (s_zoneManager->GetLearnedFocusedFXZone()->GetWidgets().Find(widget) < 0)
        return;
    
    if (s_hwndLearnDlg == NULL)
        return;
    
    if (s_hwndLearnFXAdvancedDlg == NULL)
        return;
    
    if (! IsWindowVisible(s_hwndLearnDlg))
        return;
        
    if (widget == s_currentWidget)
        return;
    
    s_currentWidget = widget;
    s_currentModifier = modifier;
    
    char buf[MEDBUF];
    GetFullWidgetName(widget, modifier, buf, sizeof(buf));
    SetDlgItemText(s_hwndLearnDlg, IDC_WidgetName, buf);
    
    if (ActionContext *context = GetContext(widget, modifier))
    {
        if (! strcmp(context->GetAction()->GetName(), "NoAction"))
        {
            buf[0] = 0;
            SetDlgItemText(s_hwndLearnDlg, IDC_ParamName, buf);
            ClearAdvancedParams();
        }
        else if (FXCell *cell = GetCell(widget, modifier))
        {
            if (ActionContext *context = cell->GetNameContext(widget))
            {
                SetDlgItemText(s_hwndLearnDlg, IDC_ParamName, context->GetStringParam());
                FillAdvancedParams();
            }
        }
    }
}

static void SetWidgetProperties(ActionContext *context, const char *params)
{
    context->GetWidgetProperties().delete_props();
    
    string_list tokens;
    GetTokens(tokens, params);
    
    for (int i = 0; i < tokens.size(); ++i)
    {
        string_list kvps;
        GetSubTokens(kvps, tokens.get(i), '=');
        
        if (kvps.size() == 2)
            context->GetWidgetProperties().set_prop(PropertyList::prop_from_string(kvps.get(0)), kvps.get(1));
    }
}

static void HandleAssigment(int modifier, int paramIdx, bool shouldAssign)
{
    if (s_hwndLearnFXAdvancedDlg == NULL)
        return;

    if (s_currentWidget == NULL)
        return;

    if (paramIdx < 0)
        return;
    
    if (s_fxSlot < 0)
        return;
    
    FXCell *cell = NULL;
    
    if (s_cellMap.Exists(s_currentWidget) && s_cellMap.Get(s_currentWidget)->Exists(modifier))
        cell = s_cellMap.Get(s_currentWidget)->Get(modifier);
    
    if (cell == NULL)
        return;
    
    char buf[MEDBUF];
    buf[0] = 0;
    
    int index = -1;
    
    for (int i = 0; i < cell->controlWidgets.GetSize(); ++i)
    {
        if (cell->controlWidgets.Get(i) == s_currentWidget)
        {
            index = i;
            break;
        }
    }
    
    if (index < 0)
        return;
    
    ActionContext *paramContext = GetContext(s_currentWidget, modifier);
    
    if (paramContext == NULL)
        return;
    
    if ( ! shouldAssign)
    {
        if (ActionContext *nameContext = cell->GetNameContext(s_currentWidget))
        {
            nameContext->SetAction(s_zoneManager->GetCSI()->GetNoActionAction());
            nameContext->SetParamIndex(0);
            nameContext->SetStringParam("");
            nameContext->GetWidgetProperties().delete_props();
        }

        if (ActionContext *valueContext = cell->GetValueContext(s_currentWidget))
        {
            valueContext->SetAction(s_zoneManager->GetCSI()->GetNoActionAction());
            valueContext->SetParamIndex(0);
            valueContext->SetStringParam("");
            valueContext->GetWidgetProperties().delete_props();
        }
        
        paramContext->SetAction(s_zoneManager->GetCSI()->GetNoActionAction());
        paramContext->SetParamIndex(0);
        paramContext->SetStringParam("");
        paramContext->GetWidgetProperties().delete_props();
        
        ClearAdvancedParams();
        SetDlgItemText(s_hwndLearnDlg, IDC_ParamName, "");
    }
    else if (strcmp(paramContext->GetAction()->GetName(), "FXParam"))
    {
        paramContext->SetAction(s_zoneManager->GetCSI()->GetFXParamAction());
        paramContext->SetParamIndex(paramIdx);
        paramContext->SetStringParam("");

        char suffix[SMLBUF];
        snprintf(suffix, sizeof(suffix), "%s%d", cell->suffix.c_str(), s_currentWidget->GetChannelNumber());
        char rawWidgetName[SMLBUF];
        snprintf(rawWidgetName, strlen(s_currentWidget->GetName()) - strlen(suffix) + 1, "%s", s_currentWidget->GetName());
        
        for (int i = 0; i < s_t_paramWidgetParams.size() && i < s_t_paramWidgets.size(); ++i)
        {
            if ( ! strcmp(s_t_paramWidgets[i], rawWidgetName))
            {
                SetWidgetProperties(paramContext, s_t_paramWidgetParams[i]);
                break;
            }
        }
        
        TrackFX_GetParamName(s_focusedTrack, s_fxSlot, paramIdx, buf, sizeof(buf));
        
        char fullWidgetName[MEDBUF];
        snprintf(fullWidgetName, sizeof(fullWidgetName), "%s%s%d",  s_t_nameWidget, cell->suffix.c_str(), cell->channel);
        cell->SetNameWidget(fullWidgetName, buf);
        
        snprintf(fullWidgetName, sizeof(fullWidgetName), "%s%s%d",  s_t_valueWidget, cell->suffix.c_str(), cell->channel);
        cell->SetValueWidget(fullWidgetName);

        if (ActionContext *context = cell->GetNameContext(s_currentWidget))
        {
            if (Widget *widget = cell->GetNameWidget(s_currentWidget))
            {
                snprintf(rawWidgetName, strlen(widget->GetName()) - strlen(suffix) + 1, "%s", widget->GetName());

                for (int i = 0; i < s_t_displayRowParams.size() && i < s_t_displayRows.size(); ++i)
                {
                    if ( ! strcmp(s_t_displayRows[i], rawWidgetName))
                    {
                        SetWidgetProperties(context, s_t_displayRowParams[i]);
                        break;
                    }
                }
            }
        }
        
        if (ActionContext *context = cell->GetValueContext(s_currentWidget))
        {
            if (Widget *widget = cell->GetValueWidget(s_currentWidget))
            {
                snprintf(rawWidgetName, strlen(widget->GetName()) - strlen(suffix) + 1, "%s", widget->GetName());

                for (int i = 0; i < s_t_displayRowParams.size() && i < s_t_displayRows.size(); ++i)
                {
                    if ( ! strcmp(s_t_displayRows[i], rawWidgetName))
                    {
                        SetWidgetProperties(context, s_t_displayRowParams[i]);
                        break;
                    }
                }
            }
        }

        s_zoneManager->GetCSI()->CalculateSteppedValues(s_fxName, s_focusedTrack, s_fxSlot);
        
        int stepCount = s_zoneManager->GetCSI()->GetSteppedValueCount(s_fxName, paramIdx);

        vector<double> steps;

        if (stepCount > 0)
        {
            GetParamStepsValues(steps, stepCount);
            paramContext->SetStepValues(steps);
        }
        else if (s_currentWidget->GetIsTwoState())
        {
            steps.push_back(0.0);
            steps.push_back(1.0);
            paramContext->SetStepValues(steps);
        }
        
        HandleInitAdvancedLearnFXDialog();
        FillParams();
    }
}

static void UpdateLearnWindowParams()
{
    char paramName[SMLBUF];
    TrackFX_GetParamName(s_focusedTrack, s_fxSlot, s_lastTouchedParamNum, paramName, sizeof(paramName));
    SetDlgItemText(s_hwndLearnDlg, IDC_ParamName, paramName);
        
    if (s_currentWidget != NULL)
    {
        HandleAssigment(s_currentModifier, s_lastTouchedParamNum, true);
        return;
    }

    Zone *zone = s_zoneManager->GetLearnedFocusedFXZone();
    
    if (zone == NULL)
        return;

    for (int i = 0; i < zone->GetWidgets().GetSize(); ++i)
    {
        Widget *widget = zone->GetWidgets().Get(i);
        
        for (int j = 0; j < s_fxRowLayouts.size(); ++j)
        {
            int modifier = s_fxRowLayouts[j].modifier;
            
            ActionContext *context = GetContext(widget, modifier);
            
            if ( context != NULL && ! strcmp(context->GetAction()->GetName(), "FXParam") && context->GetParamIndex() == s_lastTouchedParamNum)
            {
                s_currentModifier = modifier;
                s_zoneManager->GetSurface()->SetModifierValue(modifier);
                s_currentWidget = widget;
                
                FillParams();
                
                return;
            }
        }
    }
}

static void ApplyToAll(HWND hwndDlg, Widget *widget, int modifier, ActionContext *sourceParamContext, ActionContext *sourceNameContext, ActionContext *sourceValueContext)
{
    for (int cell = 0; cell < s_cells.GetSize(); ++cell)
    {
        for (int i = 0; i < s_cells.Get(cell)->controlWidgets.GetSize(); ++i)
        {
            if (sourceParamContext != NULL)
            {
                if (ActionContext *context = GetContext(s_cells.Get(cell)->controlWidgets.Get(i), modifier))
                {
                    if (const char *sourceRingColor = sourceParamContext->GetWidgetProperties().get_prop(PropertyType_LEDRingColor))
                        if (context->GetWidgetProperties().get_prop(PropertyType_LEDRingColor))
                            context->GetWidgetProperties().set_prop(PropertyType_LEDRingColor, sourceRingColor);
 
                    if (const char *sourcePushColor = sourceParamContext->GetWidgetProperties().get_prop(PropertyType_PushColor))
                        if (context->GetWidgetProperties().get_prop(PropertyType_PushColor))
                            context->GetWidgetProperties().set_prop(PropertyType_PushColor, sourcePushColor);
                    
                    context->GetWidget()->Configure(s_zoneManager->GetLearnedFocusedFXZone()->GetActionContexts(s_currentWidget));
                }
            }
            
            if (sourceNameContext != NULL)
            {
                if (ActionContext *context = s_cells.Get(cell)->GetNameContext(s_cells.Get(cell)->controlWidgets.Get(i)))
                {
                    if (const char *sourceTextColor = sourceNameContext->GetWidgetProperties().get_prop(PropertyType_TextColor))
                        if (context->GetWidgetProperties().get_prop(PropertyType_TextColor))
                            context->GetWidgetProperties().set_prop(PropertyType_TextColor, sourceTextColor);
 
                    if (const char *sourceBackgroundColor = sourceNameContext->GetWidgetProperties().get_prop(PropertyType_BackgroundColor))
                        if (context->GetWidgetProperties().get_prop(PropertyType_BackgroundColor))
                            context->GetWidgetProperties().set_prop(PropertyType_BackgroundColor, sourceBackgroundColor);

                    if (const char *sourceTopMargin = sourceNameContext->GetWidgetProperties().get_prop(PropertyType_TopMargin))
                        if (context->GetWidgetProperties().get_prop(PropertyType_TopMargin))
                            context->GetWidgetProperties().set_prop(PropertyType_TopMargin, sourceTopMargin);

                    if (const char *sourceBottomMargin = sourceNameContext->GetWidgetProperties().get_prop(PropertyType_BottomMargin))
                        if (context->GetWidgetProperties().get_prop(PropertyType_BottomMargin))
                            context->GetWidgetProperties().set_prop(PropertyType_BottomMargin, sourceBottomMargin);

                    if (const char *sourceFont = sourceNameContext->GetWidgetProperties().get_prop(PropertyType_Font))
                        if (context->GetWidgetProperties().get_prop(PropertyType_Font))
                            context->GetWidgetProperties().set_prop(PropertyType_Font, sourceFont);
                    
                    context->ForceWidgetValue(context->GetStringParam());
                }
            }
            
            if (sourceValueContext != NULL)
            {
                if (ActionContext *context = s_cells.Get(cell)->GetValueContext(s_cells.Get(cell)->controlWidgets.Get(i)))
                {
                    if (const char *sourceTextColor = sourceValueContext->GetWidgetProperties().get_prop(PropertyType_TextColor))
                        if (const char *textColor = context->GetWidgetProperties().get_prop(PropertyType_TextColor))
                            context->GetWidgetProperties().set_prop(PropertyType_TextColor, sourceTextColor);
 
                    if (const char *sourceBackgroundColor = sourceValueContext->GetWidgetProperties().get_prop(PropertyType_BackgroundColor))
                        if (const char *backgroundColor = context->GetWidgetProperties().get_prop(PropertyType_BackgroundColor))
                            context->GetWidgetProperties().set_prop(PropertyType_BackgroundColor, sourceBackgroundColor);

                    if (const char *sourceTopMargin = sourceValueContext->GetWidgetProperties().get_prop(PropertyType_TopMargin))
                        if (context->GetWidgetProperties().get_prop(PropertyType_TopMargin))
                            context->GetWidgetProperties().set_prop(PropertyType_TopMargin, sourceTopMargin);

                    if (const char *sourceBottomMargin = sourceValueContext->GetWidgetProperties().get_prop(PropertyType_BottomMargin))
                        if (context->GetWidgetProperties().get_prop(PropertyType_BottomMargin))
                            context->GetWidgetProperties().set_prop(PropertyType_BottomMargin, sourceBottomMargin);

                    if (const char *sourceFont = sourceValueContext->GetWidgetProperties().get_prop(PropertyType_Font))
                        if (context->GetWidgetProperties().get_prop(PropertyType_Font))
                            context->GetWidgetProperties().set_prop(PropertyType_Font, sourceFont);
                    
                    context->ForceWidgetValue(context->GetStringParam());
                }
            }
        }
    }
}

static WDL_DLGRET dlgProcLearnFXProperties(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    char buf[MEDBUF];
    
    rgba_color color;
    char colorBuf[32];

    switch(uMsg)
    {
        case WM_CLOSE:
            ShowWindow(hwndDlg, SW_HIDE);
            s_isPropertiesShown = false;
            break;
            
        case WM_INITDIALOG: // initialize
        {
            RECT parentRect;
            RECT dlgRect;
            GetWindowRect(s_hwndLearnFXAdvancedDlg, &parentRect);
            GetWindowRect(hwndDlg, &dlgRect);
            int offset = parentRect.right - parentRect.left + 1;
            SetWindowPos(hwndDlg, 0, parentRect.left + offset, dlgRect.top - 42, dlgRect.right - dlgRect.left, dlgRect.bottom - dlgRect.top, 0);
        }
            break;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwndDlg, &ps);
            
            for (int x = 0; x < NUM_ELEM(s_buttonColors); ++x)
            {
                if (! IsWindowVisible(GetDlgItem(hwndDlg, s_buttonColors[x][0]) ))
                    continue;
                
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
            break;
                        
        case WM_COMMAND:
        {
            int modifier = 0;
            
            if (s_zoneManager)
            {
                const WDL_TypedBuf<int> &modifiers = s_zoneManager->GetSurface()->GetModifiers();
                
                if (modifiers.GetSize() > 0)
                    modifier = modifiers.Get()[0];
            }
                        
            ActionContext *paramContext = GetContext(s_currentWidget, modifier);
            ActionContext *nameContext = NULL;
            ActionContext *valueContext = NULL;
                        
            FXCell *cell = GetCell(s_currentWidget, modifier);
            
            if (cell)
            {
                nameContext = cell->GetNameContext(s_currentWidget);
                valueContext = cell->GetValueContext(s_currentWidget);
            }

            switch(LOWORD(wParam))
            {
                case IDC_ApplyToAll:
                    if (HIWORD(wParam) == BN_CLICKED)
                        ApplyToAll(hwndDlg, s_currentWidget, modifier, paramContext, nameContext, valueContext);
                    break;
                    
                case IDC_FXParamRingColor:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        GR_SelectColor(hwndDlg, (int *)&GetButtonColorForID(LOWORD(wParam)));
                        
                        ColorFromNative(GetButtonColorForID(IDC_FXParamRingColor), &color.r, &color.g, &color.b);
                        
                        if (paramContext)
                        {
                            paramContext->GetWidgetProperties().set_prop(PropertyType_LEDRingColor, color.rgba_to_string(colorBuf));
                            paramContext->GetWidget()->Configure(s_zoneManager->GetLearnedFocusedFXZone()->GetActionContexts(s_currentWidget));
                        }
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                    break;
                    
                case IDC_FXParamIndicatorColor:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        GR_SelectColor(hwndDlg, (int *)&GetButtonColorForID(LOWORD(wParam)));
                        
                        ColorFromNative(GetButtonColorForID(IDC_FXParamIndicatorColor), &color.r, &color.g, &color.b);
                        
                        if (paramContext)
                        {
                            paramContext->GetWidgetProperties().set_prop(PropertyType_PushColor, color.rgba_to_string(colorBuf));
                            paramContext->GetWidget()->Configure(s_zoneManager->GetLearnedFocusedFXZone()->GetActionContexts(s_currentWidget));
                        }
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                    break;

                case IDC_FixedTextDisplayForegroundColor:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        GR_SelectColor(hwndDlg, (int *)&GetButtonColorForID(LOWORD(wParam)));
                        
                        ColorFromNative(GetButtonColorForID(IDC_FixedTextDisplayForegroundColor), &color.r, &color.g, &color.b);
                        
                        if (nameContext)
                        {
                            nameContext->GetWidgetProperties().set_prop(PropertyType_TextColor, color.rgba_to_string(colorBuf));
                            nameContext->GetWidget()->UpdateColorValue(color);
                            nameContext->ForceWidgetValue(nameContext->GetStringParam());
                        }
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                    break;

                case IDC_FixedTextDisplayBackgroundColor:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        GR_SelectColor(hwndDlg, (int *)&GetButtonColorForID(LOWORD(wParam)));
                        
                        ColorFromNative(GetButtonColorForID(IDC_FixedTextDisplayBackgroundColor), &color.r, &color.g, &color.b);
                        
                        if (nameContext)
                        {
                            nameContext->GetWidgetProperties().set_prop(PropertyType_BackgroundColor, color.rgba_to_string(colorBuf));
                            nameContext->ForceWidgetValue(nameContext->GetStringParam());
                        }
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                    break;

                case IDC_FXParamDisplayForegroundColor:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        GR_SelectColor(hwndDlg, (int *)&GetButtonColorForID(LOWORD(wParam)));
                        
                        ColorFromNative(GetButtonColorForID(IDC_FXParamDisplayForegroundColor), &color.r, &color.g, &color.b);
                        
                        if (valueContext)
                        {
                            valueContext->GetWidgetProperties().set_prop(PropertyType_TextColor, color.rgba_to_string(colorBuf));
                            valueContext->ForceWidgetValue(nameContext->GetStringParam());
                        }

                        InvalidateRect(hwndDlg, NULL, true);
                    }
                    break;

                case IDC_FXParamDisplayBackgroundColor:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        GR_SelectColor(hwndDlg, (int *)&GetButtonColorForID(LOWORD(wParam)));
                        
                        ColorFromNative(GetButtonColorForID(IDC_FXParamDisplayBackgroundColor), &color.r, &color.g, &color.b);
                        
                        if (valueContext)
                        {
                            valueContext->GetWidgetProperties().set_prop(PropertyType_BackgroundColor, color.rgba_to_string(colorBuf));
                            valueContext->ForceWidgetValue(nameContext->GetStringParam());
                        }

                        InvalidateRect(hwndDlg, NULL, true);
                    }
                    break;

                case IDC_FixedTextDisplayPickFont:
                    if (HIWORD(wParam) == CBN_SELCHANGE)
                    {
                        int index = SendDlgItemMessage(hwndDlg, IDC_FixedTextDisplayPickFont, CB_GETCURSEL, 0, 0);
                        if (index >= 0)
                        {
                            SendDlgItemMessage(hwndDlg,IDC_FixedTextDisplayPickFont, CB_GETLBTEXT, index, (LPARAM)buf);
                            if (nameContext)
                            {
                                nameContext->GetWidgetProperties().set_prop(PropertyType_Font, buf);
                                nameContext->ForceWidgetValue(nameContext->GetStringParam());
                            }
                        }
                    }
                    break;

                case IDC_Edit_FixedTextDisplayTop:
                    if (HIWORD(wParam) == EN_CHANGE && ! s_isClearingAdvancedParameters)
                    {
                        buf[0] = 0;
                        
                        GetDlgItemText(hwndDlg, IDC_Edit_FixedTextDisplayTop, buf, sizeof(buf));
                        if (nameContext)
                        {
                            if (buf[0] != 0)
                                nameContext->GetWidgetProperties().set_prop(PropertyType_TopMargin, buf);
                            nameContext->ForceWidgetValue(nameContext->GetStringParam());
                        }
                    }
                    break;

                case IDC_Edit_FixedTextDisplayBottom:
                    if (HIWORD(wParam) == EN_CHANGE && ! s_isClearingAdvancedParameters)
                    {
                        buf[0] = 0;
                        
                        GetDlgItemText(hwndDlg, IDC_Edit_FixedTextDisplayBottom, buf, sizeof(buf));
                        if (nameContext)
                        {
                            if (buf[0] != 0)
                                nameContext->GetWidgetProperties().set_prop(PropertyType_BottomMargin, buf);
                            nameContext->ForceWidgetValue(nameContext->GetStringParam());
                        }
                    }
                    break;

                case IDC_FXParamValueDisplayPickFont:
                    if (HIWORD(wParam) == CBN_SELCHANGE)
                    {
                        int index = SendDlgItemMessage(hwndDlg, IDC_FXParamValueDisplayPickFont, CB_GETCURSEL, 0, 0);
                        if (index >= 0)
                        {
                            SendDlgItemMessage(hwndDlg, IDC_FXParamValueDisplayPickFont, CB_GETLBTEXT, index, (LPARAM)buf);
                            if (valueContext)
                            {
                                valueContext->GetWidgetProperties().set_prop(PropertyType_Font, buf);
                                valueContext->ForceWidgetValue(nameContext->GetStringParam());
                            }
                        }
                    }
                    break;

                case IDC_Edit_ParamValueDisplayTop:
                    if (HIWORD(wParam) == EN_CHANGE && ! s_isClearingAdvancedParameters)
                    {
                        buf[0] = 0;
                        
                        GetDlgItemText(hwndDlg, IDC_Edit_ParamValueDisplayTop, buf, sizeof(buf));
                        if (valueContext)
                        {
                            if (buf[0] != 0)
                                valueContext->GetWidgetProperties().set_prop(PropertyType_TopMargin, buf);
                            valueContext->ForceWidgetValue(nameContext->GetStringParam());
                        }
                    }
                    break;

                case IDC_Edit_ParamValueDisplayBottom:
                    if (HIWORD(wParam) == EN_CHANGE && ! s_isClearingAdvancedParameters)
                    {
                        buf[0] = 0;

                        GetDlgItemText(hwndDlg, IDC_Edit_ParamValueDisplayBottom, buf, sizeof(buf));
                        if (valueContext)
                        {
                            if (buf[0] != 0)
                                valueContext->GetWidgetProperties().set_prop(PropertyType_BottomMargin, buf);
                            valueContext->ForceWidgetValue(nameContext->GetStringParam());
                        }
                    }
                    break;
            }
        }
            break;
    }

    return 0;
}

static WDL_DLGRET dlgProcLearnFXAdvanced(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    char buf[MEDBUF];
    
    switch(uMsg)
    {
        case WM_CLOSE:
            ShowWindow(hwndDlg, SW_HIDE);
            s_isAdvanceShown = false;
            ShowWindow(s_hwndLearnFXPropertiesDlg, SW_HIDE);
            s_isPropertiesShown = false;
            break;
            
        case WM_INITDIALOG: // initialize
        {
            RECT parentRect;
            RECT dlgRect;
            GetWindowRect(s_hwndLearnDlg, &parentRect);
            GetWindowRect(hwndDlg, &dlgRect);
            int offset = parentRect.right - parentRect.left + 1;
            SetWindowPos(hwndDlg, 0, dlgRect.left + offset, dlgRect.top, dlgRect.right - dlgRect.left, dlgRect.bottom - dlgRect.top, 0);
        }
            break;
            
        case WM_COMMAND:
        {
            int modifier = 0;
            
            if (s_zoneManager)
            {
                const WDL_TypedBuf<int> &modifiers = s_zoneManager->GetSurface()->GetModifiers();
                
                if (modifiers.GetSize() > 0)
                    modifier = modifiers.Get()[0];
            }
                        
            ActionContext *paramContext = GetContext(s_currentWidget, modifier);
            ActionContext *nameContext = NULL;
            ActionContext *valueContext = NULL;
                        
            FXCell *cell = GetCell(s_currentWidget, modifier);
            
            if (cell)
            {
                nameContext = cell->GetNameContext(s_currentWidget);
                valueContext = cell->GetValueContext(s_currentWidget);
            }

            switch(LOWORD(wParam))
            {
                case IDC_Params:
                    if (HIWORD(wParam) == BN_CLICKED)
                        DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_EditAdvanced), g_hwnd, dlgProcEditAdvanced);
                    break;

                case IDC_EditFXAlias:
                    if (HIWORD(wParam) == EN_CHANGE)
                    {
                        GetDlgItemText(hwndDlg, IDC_EditFXAlias, s_fxAlias, sizeof(s_fxAlias));
                    }
                    break;
                    
                case IDC_PickRingStyle:
                    if (HIWORD(wParam) == CBN_SELCHANGE)
                    {
                        int index = SendDlgItemMessage(hwndDlg, IDC_PickRingStyle, CB_GETCURSEL, 0, 0);
                        if (index >= 0)
                        {
                            SendDlgItemMessage(hwndDlg,IDC_PickRingStyle, CB_GETLBTEXT, index, (LPARAM)buf);
                            if (paramContext)
                                paramContext->GetWidgetProperties().set_prop(PropertyType_RingStyle, buf);
                        }
                    }
                    break;
                   
                case IDC_FXParamNameEdit:
                    if (HIWORD(wParam) == EN_CHANGE && s_currentWidget != NULL)
                    {
                        GetDlgItemText(hwndDlg, IDC_FXParamNameEdit, buf, sizeof(buf));
                        if (nameContext)
                            nameContext->SetStringParam(buf);
                        
                        char modifierBuf[SMLBUF];
                        s_modifierManager.GetModifierString(modifier, modifierBuf, sizeof(modifierBuf));
                        char titleBuf[MEDBUF];
                        snprintf(titleBuf, sizeof(titleBuf), "%s%s - %s", modifierBuf, s_currentWidget->GetName(), buf);
                        SetWindowText(hwndDlg, titleBuf);
                    }
                    break;

                    
                case IDC_PickSteps:
                   if (HIWORD(wParam) == CBN_SELCHANGE)
                   {
                       int index = (int)SendDlgItemMessage(hwndDlg, IDC_PickSteps, CB_GETCURSEL, 0, 0);
                       if (index >= 0)
                       {
                           string outputString;
                           GetParamStepsString(outputString, index + 1);
                           SetDlgItemText(hwndDlg, IDC_EditSteps, outputString.c_str());
                           string_list tokens;
                           GetTokens(tokens, outputString.c_str());
                           vector<double> steps;
                           for (int i = 0; i < tokens.size(); ++i)
                               steps.push_back(atof(tokens[i]));
                           
                           if (paramContext)
                               paramContext->SetStepValues(steps);
                       }
                   }
                   break;
                    
                case IDC_COMBO_PickNameDisplay:
                   if (HIWORD(wParam) == CBN_SELCHANGE && cell != NULL)
                   {
                       int index = (int)SendDlgItemMessage(hwndDlg, IDC_COMBO_PickNameDisplay, CB_GETCURSEL, 0, 0);
                       if (index >= 0)
                       {
                           char displayWidgetName[MEDBUF];
                           SendDlgItemMessage(hwndDlg,IDC_COMBO_PickNameDisplay, CB_GETLBTEXT, index, (LPARAM)displayWidgetName);
                           
                           if ( ! strcmp(displayWidgetName, ""))
                               cell->ClearNameDisplayWidget();
                           else
                           {
                               char paramName[MEDBUF];
                               GetDlgItemText(hwndDlg, IDC_FXParamNameEdit, paramName, sizeof(paramName));

                               cell->SetNameWidget(displayWidgetName, paramName);
                           }
                       }
                   }
                   break;
                    
                case IDC_COMBO_PickValueDisplay:
                   if (HIWORD(wParam) == CBN_SELCHANGE && cell != NULL)
                   {
                       int index = (int)SendDlgItemMessage(hwndDlg, IDC_COMBO_PickValueDisplay, CB_GETCURSEL, 0, 0);
                       if (index >= 0)
                       {
                           char valueWidgetName[MEDBUF];
                           SendDlgItemMessage(hwndDlg,IDC_COMBO_PickValueDisplay, CB_GETLBTEXT, index, (LPARAM)valueWidgetName);

                           if ( ! strcmp(valueWidgetName, ""))
                               cell->ClearValueDisplayWidget();
                           else
                               cell->SetValueWidget(valueWidgetName);
                       }
                   }
                   break;
                    
                case IDC_ShowProperties:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        ShowWindow(s_hwndLearnFXPropertiesDlg, SW_SHOW);
                        s_isPropertiesShown = true;
                    }
                    break;
            }
        }
            break;
    }

    return 0;
}

static WDL_DLGRET dlgProcLearnFX(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_CLOSE:
            CloseFocusedFXDialog();
            break;
            
        case WM_USER + 1024: // initialize
            HandlePopulateBasicLearnFXDialog();
            break;
                   
        case WM_COMMAND:
        {
            int modifier = 0;
            
            if (s_zoneManager)
            {
                const WDL_TypedBuf<int> &modifiers = s_zoneManager->GetSurface()->GetModifiers();
                
                if (modifiers.GetSize() > 0)
                    modifier = modifiers.Get()[0];
            }

            switch(LOWORD(wParam))
            {
                case IDC_Unassign:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        int paramNum = s_lastTouchedParamNum;
                        if (paramNum < 0)
                            break;

                        if (s_currentWidget == NULL)
                            break;
                        
                        HandleAssigment(modifier, paramNum, false);
                    }
                    break;

                case IDC_Advanced:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        ShowWindow(s_hwndLearnFXAdvancedDlg, SW_SHOW);
                        s_isAdvanceShown = true;
                    }
                    break;
                    
                case IDC_Save:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        SaveZone();
                        CloseFocusedFXDialog();
                    }
                    break ;                                        
            }
        }
    }
    
    return 0;
}

static void LearnFocusedFXDialog()
{
    if (s_hwndLearnDlg == NULL && s_hwndLearnFXAdvancedDlg == NULL)
    {
        // initialize
        LoadTemplates();
        s_hwndLearnDlg = CreateDialog(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_LearnFX), g_hwnd, dlgProcLearnFX);
        s_hwndLearnFXAdvancedDlg = CreateDialog(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_LearnAdvanced), g_hwnd, dlgProcLearnFXAdvanced);
        s_hwndLearnFXPropertiesDlg = CreateDialog(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_LearnProperties), g_hwnd, dlgProcLearnFXProperties);
    }
    
    if (s_hwndLearnDlg == NULL)
        return;
    
    if (s_hwndLearnFXAdvancedDlg == NULL)
        return;
    
    if (s_hwndLearnFXPropertiesDlg == NULL)
        return;
    
    s_numChannels = s_zoneManager->GetSurface()->GetNumChannels();
    
    SendMessage(s_hwndLearnDlg, WM_USER + 1024, 0, 0);

    ShowWindow(s_hwndLearnDlg, SW_SHOW);
    
    if (s_isAdvanceShown)
        ShowWindow(s_hwndLearnFXAdvancedDlg, SW_SHOW);
    
    if (s_isPropertiesShown)
        ShowWindow(s_hwndLearnFXPropertiesDlg, SW_SHOW);
}

void LaunchLearnFocusedFXDialog(ZoneManager *zoneManager)
{
    TrackFX_GetFXName(s_focusedTrack, s_fxSlot, s_fxName, sizeof(s_fxName));
    
    const WDL_StringKeyedArray<CSIZoneInfo*> &zoneInfo = s_zoneManager->GetZoneInfo();

    if (zoneInfo.Exists(s_fxName))
        lstrcpyn_safe(s_fxAlias, zoneInfo.Get(s_fxName)->alias.c_str(), sizeof(s_fxAlias));
    else
        zoneManager->GetAlias(s_fxName, s_fxAlias, sizeof(s_fxAlias));
    
    LearnFocusedFXDialog();
}

void LearnFocusedFXDialog(ZoneManager *zoneManager)
{
    if (s_zoneManager != NULL && zoneManager != s_zoneManager) // not the current control surface
        return;
    
    if (s_hwndLearnDlg != NULL && IsWindowVisible(s_hwndLearnDlg))
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
    s_zoneManager =  NULL;
    s_focusedTrack = NULL;
    s_fxSlot = -1;
    s_lastTouchedParamNum = -1;
    s_currentWidget = NULL;
    s_currentModifier = -1;

    if(s_hwndLearnFXPropertiesDlg != NULL)
        ShowWindow(s_hwndLearnFXPropertiesDlg, SW_HIDE);

    if(s_hwndLearnFXAdvancedDlg != NULL)
        ShowWindow(s_hwndLearnFXAdvancedDlg, SW_HIDE);

    if (s_hwndLearnDlg != NULL)
        ShowWindow(s_hwndLearnDlg, SW_HIDE);
}

void UpdateLearnWindow()
{
    if (s_hwndLearnDlg == NULL || ! IsWindowVisible(s_hwndLearnDlg))
        return;

    int trackNumberOut;
    int fxNumberOut;
    int paramNumberOut;
    
    if (GetLastTouchedFX(&trackNumberOut, &fxNumberOut, &paramNumberOut))
    {
        double minvalOut = 0.0;
        double maxvalOut = 0.0;
        double currentParamValue = TrackFX_GetParam(DAW::GetTrack(trackNumberOut), fxNumberOut, paramNumberOut, &minvalOut, &maxvalOut);
        
        if (s_lastTouchedParamNum != paramNumberOut || s_lastTouchedParamValue != currentParamValue)
        {
            s_lastTouchedParamNum = paramNumberOut;
            s_lastTouchedParamValue = currentParamValue;
            UpdateLearnWindowParams();
        }
    }
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
static int s_surfaceMaxSysExMessagesPerRun = 0;
static int s_surfaceDefaultMaxSysExMessagesPerRun = 200;
static string s_surfaceRemoteDeviceIP;
static int s_pageIndex = 0;
static bool s_followMCP = false;
static bool s_synchPages = true;
static bool s_isScrollLinkEnabled = false;
static bool s_scrollSynch = false;

static string s_pageSurfaceName;
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
    int surfaceMaxSysExMessagesPerRun;
    string remoteDeviceIP;
    
    SurfaceLine()
    {
        inPort = 0;
        outPort = 0;
        surfaceRefreshRate = s_surfaceDefaultRefreshRate;
        surfaceMaxPacketsPerRun = s_surfaceDefaultMaxPacketsPerRun;
        surfaceMaxSysExMessagesPerRun = s_surfaceMaxSysExMessagesPerRun;
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
                        char buf[MEDBUF];
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
    
    char buf[MEDBUF];
    
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
                                char buffer[MEDBUF];
                                
                                GetDlgItemText(hwndDlg, IDC_COMBO_SurfaceTemplate, buffer, sizeof(buffer));
                                
                                for (int i = 0; i < sizeof(buffer); ++i)
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
                                char buffer[MEDBUF];
                                
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
                        char buf[MEDBUF];

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
            char buf[MEDBUF];
            int currentIndex = 0;
            WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, IDC_COMBO_MidiIn));
            WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, IDC_COMBO_MidiOut));
            
            for (int i = 0; i < GetNumMIDIInputs(); ++i)
                if (GetMIDIInputName(i, buf, sizeof(buf)))
                {
                    AddComboEntry(hwndDlg, i, buf, IDC_COMBO_MidiIn);
                    if (s_editMode && s_surfaceInPort == i)
                        SendMessage(GetDlgItem(hwndDlg, IDC_COMBO_MidiIn), CB_SETCURSEL, currentIndex, 0);
                    currentIndex++;
                }
            
            currentIndex = 0;
            
            for (int i = 0; i < GetNumMIDIOutputs(); ++i)
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
                SetDlgItemInt(hwndDlg, IDC_EDIT_MidiSurfaceMaxSysExMessagesPerRun, s_surfaceMaxSysExMessagesPerRun, true);
            }
            else
            {
                SetDlgItemInt(hwndDlg, IDC_EDIT_MidiSurfaceRefreshRate, s_surfaceDefaultRefreshRate, true);
                SetDlgItemInt(hwndDlg, IDC_EDIT_MidiSurfaceMaxSysExMessagesPerRun, s_surfaceDefaultMaxSysExMessagesPerRun, true);
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
                        char buf[MEDBUF];
                        GetDlgItemText(hwndDlg, IDC_EDIT_MidiSurfaceName, buf, sizeof(buf));
                        s_surfaceName = buf;
                        
                        BOOL translated;
                        s_surfaceRefreshRate = GetDlgItemInt(hwndDlg, IDC_EDIT_MidiSurfaceRefreshRate, &translated, true);

                        s_surfaceMaxSysExMessagesPerRun = GetDlgItemInt(hwndDlg, IDC_EDIT_MidiSurfaceMaxSysExMessagesPerRun, &translated, true);
                        
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
                        char buf[MEDBUF];
                                            
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
    
    char tmp[MEDBUF];
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
                            char broadcasterName[MEDBUF];
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
                            char listenerName[MEDBUF];
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

                                char tmp[MEDBUF];
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
                                s_surfaceMaxSysExMessagesPerRun = s_surfaces.Get(index)->surfaceMaxSysExMessagesPerRun;

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
                                    s_surfaces.Get(index)->surfaceMaxSysExMessagesPerRun = s_surfaceMaxSysExMessagesPerRun;
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
                        char tmp[MEDBUF];
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
                        
                        if ((surface->type == s_MidiSurfaceToken || surface->type == s_OSCSurfaceToken || surface->type == s_OSCX32SurfaceToken) && tokens.size() == 6)
                        {
                            surface->inPort = atoi(tokens[2].c_str());
                            surface->outPort = atoi(tokens[3].c_str());
                            if ((surface->type == s_OSCSurfaceToken || surface->type == s_OSCX32SurfaceToken) && tokens.size() > 4)
                                surface->remoteDeviceIP = tokens[4];
                            
                            if (surface->type == s_MidiSurfaceToken)
                            {
                                surface->surfaceRefreshRate = atoi(tokens[4]);
                                surface->surfaceMaxSysExMessagesPerRun = atoi(tokens[5]);
                            }
                            else if (surface->type == s_OSCSurfaceToken || surface->type == s_OSCX32SurfaceToken)
                                surface->surfaceMaxPacketsPerRun = atoi(tokens[5]);
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

                        for (int i = 2; i < tokens.size(); ++i)
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
                        
                        int maxSysExMessagesPerRun = s_surfaces.Get(i)->surfaceMaxSysExMessagesPerRun < 1 ? s_surfaceDefaultMaxSysExMessagesPerRun : s_surfaces.Get(i)->surfaceMaxSysExMessagesPerRun;
                        fprintf(iniFile, "%d ", maxSysExMessagesPerRun);
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
