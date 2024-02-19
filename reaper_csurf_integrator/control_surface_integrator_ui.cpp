//
//  control_surface_integrator_ui.cpp
//  reaper_csurf_integrator
//
//

#include "control_surface_integrator.h"
#include "../WDL/dirscan.h"
#include "resource.h"

extern CSurfIntegrator *g_csiForGui;

extern void TrimLine(string &line);
extern void GetParamStepsString(string &outputString, int numSteps);

extern int g_minNumParamSteps;
extern int g_maxNumParamSteps;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Remap Auto FX
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static ZoneManager *s_zoneManager;
static int s_numGroups = 0;
static AutoZoneDefinition s_zoneDef;

static int s_dlgResult = IDCANCEL;

static int s_fxListIndex = 0;
static int s_groupIndex = 0;

static WDL_DLGRET dlgProcEditAdvanced(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            SetWindowText(hwndDlg, (__LOCALIZE("Advanced Edit Group ","csi_adv") + int_to_string(s_groupIndex + 1)).c_str());

            s_dlgResult = IDCANCEL;

            FXParamTemplate &t = s_zoneDef.cells[s_fxListIndex].paramTemplates[s_groupIndex];

            char tmp[BUFSZ];
            if (t.deltaValue != 0.0)
                SetDlgItemText(hwndDlg, IDC_EDIT_Delta, format_number(t.deltaValue, tmp, sizeof(tmp)));

            if (t.rangeMinimum != 1.0 ||
                t.rangeMaximum != 0.0)
            {
                SetDlgItemText(hwndDlg, IDC_EDIT_RangeMin, format_number(t.rangeMinimum, tmp, sizeof(tmp)));
                SetDlgItemText(hwndDlg, IDC_EDIT_RangeMax, format_number(t.rangeMaximum, tmp, sizeof(tmp)));
            }

            if (t.acceleratedDeltaValues.size() > 0)
            {
                string deltas;
                
                for (int i = 0; i < (int)t.acceleratedDeltaValues.size(); ++i)
                {
                    deltas += format_number(t.acceleratedDeltaValues[i], tmp, sizeof(tmp));
                    deltas += " ";
                }
                
                SetDlgItemText(hwndDlg, IDC_EDIT_DeltaValues, deltas.c_str());
            }

            if (t.acceleratedTickValues.size() > 0)
            {
                string ticks;
                
                for (int i = 0; i < (int)t.acceleratedTickValues.size(); ++i)
                    ticks += int_to_string(t.acceleratedTickValues[i]) + " ";
                
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
                        FXParamTemplate &t = s_zoneDef.cells[s_fxListIndex].paramTemplates[s_groupIndex];

                        char buf[BUFSZ];

                        GetDlgItemText(hwndDlg, IDC_EDIT_Delta, buf, sizeof(buf));
                        if (buf[0])
                            t.deltaValue = atof(buf);

                        GetDlgItemText(hwndDlg, IDC_EDIT_RangeMin, buf, sizeof(buf));
                        if (buf[0])
                            t.rangeMinimum = atof(buf);

                        GetDlgItemText(hwndDlg, IDC_EDIT_RangeMax, buf, sizeof(buf));
                        if (buf[0])
                            t.rangeMaximum = atof(buf);

                        GetDlgItemText(hwndDlg, IDC_EDIT_DeltaValues, buf, sizeof(buf));
                        if (buf[0])
                        {
                            t.acceleratedDeltaValues.clear();
                            string_list deltas;
                            GetTokens(deltas, buf);
                            for (int i = 0; i < (int)deltas.size(); ++i)
                                t.acceleratedDeltaValues.push_back(atof(deltas[i].c_str()));
                        }

                        GetDlgItemText(hwndDlg, IDC_EDIT_TickValues, buf, sizeof(buf));
                        if (buf[0])
                        {
                            t.acceleratedTickValues.clear();
                            string_list ticks;
                            GetTokens(ticks, buf);
                            for (int i = 0; i < (int)ticks.size(); ++i)
                                t.acceleratedTickValues.push_back(atoi(ticks[i].c_str()));
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
static const int s_fixedTextDisplayTopMarginLabels[] = { IDC_FixedTextDisplayTopLabel1, IDC_FixedTextDisplayTopLabel2, IDC_FixedTextDisplayTopLabel3 };
static const int s_fixedTextDisplayTopMarginEditControls[] = { IDC_Edit_FixedTextDisplayTop1, IDC_Edit_FixedTextDisplayTop2, IDC_Edit_FixedTextDisplayTop3 };
static const int s_fixedTextDisplayBottomMarginLabels[] = { IDC_FixedTextDisplayBottomLabel1, IDC_FixedTextDisplayBottomLabel2, IDC_FixedTextDisplayBottomLabel3 };
static const int s_fixedTextDisplayBottomMarginEditControls[] = { IDC_Edit_FixedTextDisplayBottom1, IDC_Edit_FixedTextDisplayBottom2, IDC_Edit_FixedTextDisplayBottom3 };
static const int s_paramValueDisplayRowPickers[] = { IDC_FXParamValueDisplayPickRow1 , IDC_FXParamValueDisplayPickRow2, IDC_FXParamValueDisplayPickRow3 };
static const int s_paramValueDisplayFontLabels[] = { IDC_FXParamValueDisplayFontLabel1, IDC_FXParamValueDisplayFontLabel2, IDC_FXParamValueDisplayFontLabel3 };
static const int s_paramValueDisplayFontPickers[] = { IDC_FXParamValueDisplayPickFont1, IDC_FXParamValueDisplayPickFont2, IDC_FXParamValueDisplayPickFont3 };
static const int s_paramValueDisplayTopMarginLabels[] = { IDC_ParamValueDisplayTopLabel1, IDC_ParamValueDisplayTopLabel2, IDC_ParamValueDisplayTopLabel3 };
static const int s_paramValueDisplayTopMarginEditControls[] = { IDC_Edit_ParamValueDisplayTop1, IDC_Edit_ParamValueDisplayTop2, IDC_Edit_ParamValueDisplayTop3 };
static const int s_paramValueDisplayBottomMarginLabels[] = { IDC_ParamValueDisplayBottomLabel1, IDC_ParamValueDisplayBottomLabel2, IDC_ParamValueDisplayBottomLabel3 };
static const int s_paramValueDisplayBottomMarginEditControls[] = { IDC_Edit_ParamValueDisplayBottom1, IDC_Edit_ParamValueDisplayBottom2, IDC_Edit_ParamValueDisplayBottom3 };
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
static const int s_fixedTextDisplayGroupBoxes[] = { IDC_GroupFixedTextDisplay1, IDC_GroupFixedTextDisplay2, IDC_GroupFixedTextDisplay3 };
static const int s_fxParamDisplayGroupBoxes[] = { IDC_GroupFXParamValueDisplay1, IDC_GroupFXParamValueDisplay2, IDC_GroupFXParamValueDisplay3 };
static const int s_advancedButtons[] = { IDC_AdvancedGroup1, IDC_AdvancedGroup2, IDC_AdvancedGroup3 };

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
    s_fixedTextDisplayTopMarginLabels,
    s_fixedTextDisplayTopMarginEditControls,
    s_fixedTextDisplayBottomMarginLabels,
    s_fixedTextDisplayBottomMarginEditControls,
    s_paramValueDisplayFontLabels,
    s_paramValueDisplayFontPickers,
    s_paramValueDisplayTopMarginLabels,
    s_paramValueDisplayTopMarginEditControls,
    s_paramValueDisplayBottomMarginLabels,
    s_paramValueDisplayBottomMarginEditControls
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
    s_fxParamDisplayBackgroundColorBoxes
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
        const char *str = s_zoneDef.rawParams[i].c_str();
        lvi.pszText = (char *)str;
        
        ListView_InsertItem(hwndParamList, &lvi);        
    }
}

static WDL_DLGRET dlgProcEditFXParam(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
            
        case WM_PAINT:
        {
            if (s_zoneManager->hasColor_)
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
                        
            WDL_UTF8_HookListView(GetDlgItem(hwndDlg, IDC_AllParams));

            ShowBaseControls(hwndDlg, 0, NUM_ELEM(s_groupBoxes), false );
            ShowFontControls(hwndDlg, 0, NUM_ELEM(s_groupBoxes), false);
            ShowColorControls(hwndDlg, 0, NUM_ELEM(s_groupBoxes), false);

            SurfaceCell &cell = s_zoneDef.cells[s_fxListIndex];
            
            SetWindowText(hwndDlg, (s_zoneDef.fxAlias + "   " + cell.modifiers + cell.address).c_str());

            for (int i = 0; i <NUM_ELEM( s_stepPickers); i++)
            {
                WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, s_stepPickers[i]));
                SendDlgItemMessage(hwndDlg, s_stepPickers[i], CB_ADDSTRING, 0, (LPARAM)__LOCALIZE("Custom","csi_fxparm"));
                
                for (int j = g_minNumParamSteps; j <= g_maxNumParamSteps; j++)
                    SendDlgItemMessage(hwndDlg, s_stepPickers[i], CB_ADDSTRING, 0, (LPARAM)int_to_string(j).c_str());
            }
                                      
            PopulateParamListView(GetDlgItem(hwndDlg, IDC_AllParams));

            for (int i = 0; i < NUM_ELEM(s_widgetTypePickers); i++)
            {
                WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, s_widgetTypePickers[i]));
                SendDlgItemMessage(hwndDlg, s_widgetTypePickers[i], CB_ADDSTRING, 0, (LPARAM)__LOCALIZE("None","csi_fxparm"));

                for (int j = 0; j < s_zoneManager->paramWidgets_.size(); j++)
                    SendDlgItemMessage(hwndDlg, s_widgetTypePickers[i], CB_ADDSTRING, 0, (LPARAM)s_zoneManager->paramWidgets_[j].c_str());
            }
            
            for (int i = 0; i < NUM_ELEM(s_ringStylePickers); i++)
            {
                WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, s_ringStylePickers[i]));
                for (int j = 0; j < s_zoneManager->ringStyles_.size(); j++)
                    SendDlgItemMessage(hwndDlg, s_ringStylePickers[i], CB_ADDSTRING, 0, (LPARAM)s_zoneManager->ringStyles_[j].c_str());
            }
            
            for (int i = 0; i < NUM_ELEM(s_fixedTextDisplayRowPickers); i++)
            {
                WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, s_fixedTextDisplayRowPickers[i]));
                SendDlgItemMessage(hwndDlg, s_fixedTextDisplayRowPickers[i], CB_ADDSTRING, 0, (LPARAM)__LOCALIZE("NullDisplay","csi_fxparm"));

                for (int j = 0; j < s_zoneManager->displayRows_.size(); j++)
                    SendDlgItemMessage(hwndDlg, s_fixedTextDisplayRowPickers[i], CB_ADDSTRING, 0, (LPARAM)s_zoneManager->displayRows_[j].c_str());
            }

            for (int i = 0; i < NUM_ELEM(s_paramValueDisplayRowPickers); i++)
            {
                WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, s_paramValueDisplayRowPickers[i]));
                SendDlgItemMessage(hwndDlg, s_paramValueDisplayRowPickers[i], CB_ADDSTRING, 0, (LPARAM)__LOCALIZE("NullDisplay","csi_fxparm"));

                for (int j = 0; j < s_zoneManager->displayRows_.size(); j++)
                    SendDlgItemMessage(hwndDlg, s_paramValueDisplayRowPickers[i], CB_ADDSTRING, 0, (LPARAM)s_zoneManager->displayRows_[j].c_str());
            }

            for (int i = 0; i < NUM_ELEM(s_fixedTextDisplayFontPickers); i++)
            {
                SendDlgItemMessage(hwndDlg, s_fixedTextDisplayFontPickers[i], CB_ADDSTRING, 0, (LPARAM)"");

                for (int j = 0; j < s_zoneManager->fonts_.size(); j++)
                    SendDlgItemMessage(hwndDlg, s_fixedTextDisplayFontPickers[i], CB_ADDSTRING, 0, (LPARAM)s_zoneManager->fonts_[j].c_str());
            }

            for (int i = 0; i < NUM_ELEM(s_paramValueDisplayFontPickers); i++)
            {
                SendDlgItemMessage(hwndDlg, s_paramValueDisplayFontPickers[i], CB_ADDSTRING, 0, (LPARAM)"");

                for (int j = 0; j < s_zoneManager->fonts_.size(); j++)
                    SendDlgItemMessage(hwndDlg, s_paramValueDisplayFontPickers[i], CB_ADDSTRING, 0, (LPARAM)s_zoneManager->fonts_[j].c_str());
            }

            for (int i = 0; i < s_zoneDef.cells[s_fxListIndex].paramTemplates.size() && i < NUM_ELEM(s_paramNumEditControls); i++)
            {
                FXParamTemplate &t = s_zoneDef.cells[s_fxListIndex].paramTemplates[i];
                
                SetDlgItemText(hwndDlg, s_paramNumEditControls[i], t.paramNum.c_str());
                SetDlgItemText(hwndDlg, s_fixedTextEditControls[i], t.paramName.c_str());

                SetDlgItemText(hwndDlg, s_widgetTypePickers[i], t.control.c_str());
                SetDlgItemText(hwndDlg, s_fixedTextDisplayRowPickers[i], t.nameDisplay.c_str());
                SetDlgItemText(hwndDlg, s_paramValueDisplayRowPickers[i], t.valueDisplay.c_str());
                
                const char *ringstyle = t.controlProperties.get_prop(PropertyType_RingStyle);
                if (ringstyle)
                    SetDlgItemText(hwndDlg, s_ringStylePickers[i], ringstyle);

                const char *font = t.nameDisplayProperties.get_prop(PropertyType_Font);
                if (font)
                    SetDlgItemText(hwndDlg, s_fixedTextDisplayFontPickers[i], font);

                const char *topMargin = t.nameDisplayProperties.get_prop(PropertyType_TopMargin);
                if (topMargin)
                    SetDlgItemText(hwndDlg, s_fixedTextDisplayTopMarginEditControls[i], topMargin);

                const char *bottomMargin = t.nameDisplayProperties.get_prop(PropertyType_BottomMargin);
                if (bottomMargin)
                    SetDlgItemText(hwndDlg, s_fixedTextDisplayBottomMarginEditControls[i], bottomMargin);

                font = t.valueDisplayProperties.get_prop(PropertyType_Font);
                if (font)
                    SetDlgItemText(hwndDlg, s_paramValueDisplayFontPickers[i], font);

                topMargin = t.valueDisplayProperties.get_prop(PropertyType_TopMargin);
                if (topMargin)
                    SetDlgItemText(hwndDlg, s_paramValueDisplayTopMarginEditControls[i], topMargin);

                bottomMargin = t.valueDisplayProperties.get_prop(PropertyType_BottomMargin);
                if (bottomMargin)
                    SetDlgItemText(hwndDlg, s_paramValueDisplayBottomMarginEditControls[i], bottomMargin);

                const char *ringcolor = t.controlProperties.get_prop(PropertyType_LEDRingColor);
                if (ringcolor)
                {
                    rgba_color color;
                    GetColorValue(ringcolor, color);
                    GetButtonColorForID(s_widgetRingColors[i]) = ColorToNative(color.r, color.g, color.b);
                }
                
                const char *pushcolor = t.controlProperties.get_prop(PropertyType_PushColor);
                if (pushcolor)
                {
                    rgba_color color;
                    GetColorValue(pushcolor, color);
                    GetButtonColorForID(s_widgetRingIndicators[i]) = ColorToNative(color.r, color.g, color.b);
                }

                const char *foreground = t.nameDisplayProperties.get_prop(PropertyType_TextColor);
                if (foreground)
                {
                    rgba_color color;
                    GetColorValue(foreground, color);
                    GetButtonColorForID(s_fixedTextDisplayForegroundColors[i]) = ColorToNative(color.r, color.g, color.b);
                }
                
                const char *background = t.nameDisplayProperties.get_prop(PropertyType_BackgroundColor);
                if (background)
                {
                    rgba_color color;
                    GetColorValue(background, color);
                    GetButtonColorForID(s_fixedTextDisplayBackgroundColors[i]) = ColorToNative(color.r, color.g, color.b);
                }

                foreground = t.valueDisplayProperties.get_prop(PropertyType_TextColor);
                if (foreground)
                {
                    rgba_color color;
                    GetColorValue(foreground, color);
                    GetButtonColorForID(s_fxParamDisplayForegroundColors[i]) = ColorToNative(color.r, color.g, color.b);
                }

                background = t.valueDisplayProperties.get_prop(PropertyType_BackgroundColor);
                if (background)
                {
                    rgba_color color;
                    GetColorValue(background, color);
                    GetButtonColorForID(s_fxParamDisplayBackgroundColors[i]) = ColorToNative(color.r, color.g, color.b);
                }
                
                string steps;
                
                char buf[BUFSZ];
                for (int j = 0; j < (int)t.steppedValues.size(); ++j)
                {
                    steps += format_number(t.steppedValues[j], buf, sizeof(buf));
                    steps += "  ";
                }
                
                SetDlgItemText(hwndDlg, s_stepEditControls[i], steps.c_str());
                
                if (!strcmp(buf,"RotaryPush") && steps == "")
                {
                    SetDlgItemText(hwndDlg, s_stepEditControls[i], "0  1");
                    SetDlgItemText(hwndDlg, s_stepPickers[i], "2");
                }
            }
            
            ShowBaseControls(hwndDlg, 0, s_numGroups, true);
            
            if (s_zoneManager->fonts_.size())
                ShowFontControls(hwndDlg, 0, s_numGroups, true);
            
            if (s_zoneManager->hasColor_)
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
                        GR_SelectColor(hwndDlg, (int *)&GetButtonColorForID(LOWORD(wParam)));
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
                        for (int i = 0; i < s_zoneDef.cells[s_fxListIndex].paramTemplates.size(); i++)
                        {
                            FXParamTemplate &t = s_zoneDef.cells[s_fxListIndex].paramTemplates[i];
  
                            char buf[BUFSZ];
                            
                            GetDlgItemText(hwndDlg, s_paramNumEditControls[i], buf, sizeof(buf));
                            t.paramNum = buf;

                            GetDlgItemText(hwndDlg, s_widgetTypePickers[i], buf, sizeof(buf));
                            t.control = buf;
                                                        
                            GetDlgItemText(hwndDlg, s_fixedTextEditControls[i], buf, sizeof(buf));
                            t.paramName = buf;

                            GetDlgItemText(hwndDlg, s_fixedTextDisplayRowPickers[i], buf, sizeof(buf));
                            t.nameDisplay = buf;

                            GetDlgItemText(hwndDlg, s_paramValueDisplayRowPickers[i], buf, sizeof(buf));
                            t.valueDisplay = buf;

                            char propBuf[4096];
                            propBuf[0] = 0;

                            GetDlgItemText(hwndDlg, s_ringStylePickers[i], buf, sizeof(buf));
                            t.controlProperties.set_prop(PropertyType_RingStyle, buf);
                            t.controlProperties.print_to_buf(propBuf, sizeof(propBuf), PropertyType_RingStyle);

                            if (IsWindowVisible(GetDlgItem(hwndDlg,s_widgetRingColors[i])))
                            {
                                rgba_color color;
                                char tmp[32];
                                
                                ColorFromNative(GetButtonColorForID(s_widgetRingColors[i]), &color.r, &color.g, &color.b);
                                t.controlProperties.set_prop(PropertyType_LEDRingColor, color.rgba_to_string(tmp));
                                t.controlProperties.print_to_buf(propBuf, sizeof(propBuf), PropertyType_LEDRingColor);
                                
                                ColorFromNative(GetButtonColorForID(s_widgetRingIndicators[i]), &color.r, &color.g, &color.b);
                                t.controlProperties.set_prop(PropertyType_PushColor, color.rgba_to_string(tmp));
                                t.controlProperties.print_to_buf(propBuf, sizeof(propBuf), PropertyType_PushColor);
                            }
                            
                            snprintf_append(propBuf, sizeof(propBuf), "[ ");

                            if (t.rangeMinimum < t.rangeMaximum)
                                snprintf_append(propBuf, sizeof(propBuf), "%.2f>%.2f ", t.rangeMinimum, t.rangeMaximum);
                            
                            if (t.acceleratedDeltaValues.size())
                            {
                                snprintf_append(propBuf, sizeof(propBuf), "(");
                                for (int j = 0; j < t.acceleratedDeltaValues.size(); ++j)
                                {
                                    if (j == t.acceleratedDeltaValues.size() - 1)
                                        snprintf_append(propBuf, sizeof(propBuf), "%.3f) ", t.acceleratedDeltaValues[j]);
                                    else
                                        snprintf_append(propBuf, sizeof(propBuf), "%.3f,", t.acceleratedDeltaValues[j]);
                                }
                            }
                            else
                                snprintf_append(propBuf, sizeof(propBuf), "(%.3f) ", t.deltaValue);

                            if (t.acceleratedTickValues.size() > 1)
                            {
                                snprintf_append(propBuf, sizeof(propBuf), "(");
                                for (int j = 0; j < t.acceleratedTickValues.size(); ++j)
                                {
                                    if (j == t.acceleratedTickValues.size() - 1)
                                        snprintf_append(propBuf, sizeof(propBuf), "%d) ", t.acceleratedTickValues[j]);
                                    else
                                        snprintf_append(propBuf, sizeof(propBuf), "%d,", t.acceleratedTickValues[j]);
                                }
                            }
                            else if (t.acceleratedTickValues.size() > 0)
                                snprintf_append(propBuf, sizeof(propBuf), "(%d) ", t.acceleratedTickValues[0]);
                            
                            
                            GetDlgItemText(hwndDlg, s_stepEditControls[i], buf, sizeof(buf));
                            
                            if (string(buf) != "")
                            {
                                t.steppedValues.clear();
                                string_list steps;
                                GetTokens(steps, buf);
                                for (int j = 0; j < (int)steps.size(); ++j)
                                {
                                    t.steppedValues.push_back(atoi(steps[j].c_str()));
                                    snprintf_append(propBuf, sizeof(propBuf), "%s ", steps[j].c_str());
                                }
                                t.controlParams += buf;
                            }
                            
                            snprintf_append(propBuf, sizeof(propBuf), "]");
                            t.controlParams = propBuf;
                            propBuf[0] = 0;
                        
                            if (IsWindowVisible(GetDlgItem(hwndDlg,s_fixedTextDisplayFontPickers[i])))
                            {
                                GetDlgItemText(hwndDlg, s_fixedTextDisplayFontPickers[i], buf, sizeof(buf));
                                t.nameDisplayProperties.set_prop(PropertyType_Font, buf);
                                t.nameDisplayProperties.print_to_buf(propBuf, sizeof(propBuf), PropertyType_Font);
   
                                GetDlgItemText(hwndDlg, s_fixedTextDisplayTopMarginEditControls[i], buf, sizeof(buf));
                                t.nameDisplayProperties.set_prop(PropertyType_TopMargin, buf);
                                t.nameDisplayProperties.print_to_buf(propBuf, sizeof(propBuf), PropertyType_TopMargin);
  
                                GetDlgItemText(hwndDlg, s_fixedTextDisplayBottomMarginEditControls[i], buf, sizeof(buf));
                                t.nameDisplayProperties.set_prop(PropertyType_BottomMargin, buf);
                                t.nameDisplayProperties.print_to_buf(propBuf, sizeof(propBuf), PropertyType_BottomMargin);
                            }
                            
                            if (IsWindowVisible(GetDlgItem(hwndDlg,s_fixedTextDisplayForegroundColors[i])))
                            {
                                rgba_color color;
                                char tmp[32];
                                
                                ColorFromNative(GetButtonColorForID(s_fixedTextDisplayForegroundColors[i]), &color.r, &color.g, &color.b);
                                t.nameDisplayProperties.set_prop(PropertyType_TextColor, color.rgba_to_string(tmp));
                                t.nameDisplayProperties.print_to_buf(propBuf, sizeof(propBuf), PropertyType_TextColor);

                                ColorFromNative(GetButtonColorForID(s_fixedTextDisplayBackgroundColors[i]), &color.r, &color.g, &color.b);
                                t.nameDisplayProperties.set_prop(PropertyType_BackgroundColor, color.rgba_to_string(tmp));
                                t.nameDisplayProperties.print_to_buf(propBuf, sizeof(propBuf), PropertyType_BackgroundColor);
                            }
                            
                            t.nameDisplayParams = propBuf;
                            propBuf[0] = 0;

                            if (IsWindowVisible(GetDlgItem(hwndDlg,s_paramValueDisplayFontPickers[i])))
                            {
                                GetDlgItemText(hwndDlg, s_paramValueDisplayFontPickers[i], buf, sizeof(buf));
                                t.valueDisplayProperties.set_prop(PropertyType_Font, buf);
                                t.valueDisplayProperties.print_to_buf(propBuf, sizeof(propBuf), PropertyType_Font);
  
                                GetDlgItemText(hwndDlg, s_paramValueDisplayTopMarginEditControls[i], buf, sizeof(buf));
                                t.valueDisplayProperties.set_prop(PropertyType_TopMargin, buf);
                                t.valueDisplayProperties.print_to_buf(propBuf, sizeof(propBuf), PropertyType_TopMargin);
  
                                GetDlgItemText(hwndDlg, s_paramValueDisplayBottomMarginEditControls[i], buf, sizeof(buf));
                                t.valueDisplayProperties.set_prop(PropertyType_BottomMargin, buf);
                                t.valueDisplayProperties.print_to_buf(propBuf, sizeof(propBuf), PropertyType_BottomMargin);
                            }
                            
                            if (IsWindowVisible(GetDlgItem(hwndDlg,s_fxParamDisplayForegroundColors[i])))
                            {
                                rgba_color color;
                                char tmp[32];
                                
                                ColorFromNative(GetButtonColorForID(s_fxParamDisplayForegroundColors[i]), &color.r, &color.g, &color.b);
                                t.valueDisplayProperties.set_prop(PropertyType_TextColor, color.rgba_to_string(tmp));
                                t.valueDisplayProperties.print_to_buf(propBuf, sizeof(propBuf), PropertyType_TextColor);

                                
                                ColorFromNative(GetButtonColorForID(s_fxParamDisplayBackgroundColors[i]), &color.r, &color.g, &color.b);
                                t.valueDisplayProperties.set_prop(PropertyType_BackgroundColor, color.rgba_to_string(tmp));
                                t.valueDisplayProperties.print_to_buf(propBuf, sizeof(propBuf), PropertyType_BackgroundColor);
                            }
                            
                            t.valueDisplayParams = propBuf;
                            propBuf[0] = 0;
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

string_list GetLineComponents(int index)
{
    string_list components;
    
    SurfaceCell &cell = s_zoneDef.cells[index];
    
    components.push_back(cell.modifiers + cell.address);

    for (int i = 0; i < cell.paramTemplates.size(); ++i)
    {
        FXParamTemplate &t = cell.paramTemplates[i];
        
        string_list tokens;
        GetSubTokens(tokens, t.control.c_str(), '+');
        
        string controlName = string(tokens[tokens.size() - 1]);
        
        if (controlName == "RotaryPush")
            controlName = "Push";
        
        components.push_back(controlName);
        
        if (t.paramName != "")
            components.push_back(t.paramName);
        else
            components.push_back("NoAction");
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

    string_list components = GetLineComponents(index);
    
    string preamble = string(components[0]);
    
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
        
    for (int i = 0; i < s_zoneDef.cells.size(); i++)
        SetListViewItem(hwndParamList, i, true);
}

static void MoveUp(HWND hwndParamList)
{
    int index = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
    
    if (index > 0)
    {
        s_zoneDef.cells[index].ExchangeParamTemplates(s_zoneDef.cells[index - 1]);

        SetListViewItem(hwndParamList, index, false);
        SetListViewItem(hwndParamList, index - 1, false);

        ListView_SetItemState(hwndParamList, index - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

static void MoveDown(HWND hwndParamList)
{
    int index = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
    
    if (index >= 0 && index < s_zoneDef.cells.size() - 1)
    {
        s_zoneDef.cells[index].ExchangeParamTemplates(s_zoneDef.cells[index + 1]);

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
    char buf[2048], buf2[2048];
    snprintf(buf, sizeof(buf), __LOCALIZE_VERFMT("This will permanently delete\r\n\r\n%s.zon\r\n\r\nAre you sure you want to permanently delete this file from disk?\n\nIf you delete the file the RemapAutoZone dialog will close.","csi_mbox"), s_zoneDef.fxName.c_str());
    snprintf(buf2, sizeof(buf2), __LOCALIZE_VERFMT("Delete %s","csi_mbox"), s_zoneDef.fxAlias.c_str());
    if (MessageBox(GetMainHwnd(), buf, buf2, MB_YESNO) == IDNO)
       return false;
    
    s_zoneManager->RemoveZone(s_zoneDef.fxName.c_str());
    
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
            HWND paramList = GetDlgItem(hwndDlg, IDC_PARAM_LIST);
            WDL_UTF8_HookListView(paramList);
            
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
                
                s_zoneDef.cells[s_oldPosition].ExchangeParamTemplates(s_zoneDef.cells[lvhti.iItem]);
                
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
            ShowWindow(GetDlgItem(hwndDlg, IDC_MacHint), true);
            ShowWindow(GetDlgItem(hwndDlg, IDC_WindowsHint), false);
            ShowWindow(GetDlgItem(hwndDlg, IDC_BUTTONUP), false);
            ShowWindow(GetDlgItem(hwndDlg, IDC_BUTTONDOWN), false);

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
            
            for (int i = 1; i <= s_numGroups * 2; i++)
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

bool RemapAutoZoneDialog(ZoneManager *zoneManager, string fullFilePath)
{
    s_zoneDef.Clear();
    s_zoneManager = zoneManager;
    s_zoneDef.fullPath = fullFilePath;
    s_numGroups = s_zoneManager->paramWidgets_.size();
    
    s_zoneManager->UnpackZone(s_zoneDef);
    
    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_RemapAutoFX), g_hwnd, dlgProcRemapFXAutoZone);
    
    if (s_dlgResult == IDSAVE)
    {
        s_zoneManager->SaveAutoZone(s_zoneDef);
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
    string remoteDeviceIP;
    
    SurfaceLine()
    {
        inPort = 0;
        outPort = 0;
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
    bool custom;
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
        custom = false;
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
            destinationListener->custom = source.Get(i)->listeners.Get(j)->custom;
            
            destinationBroadcaster->listeners.Add(destinationListener);
        }
        
        destination.Add(destinationBroadcaster);
    }
}

static bool s_editMode = false;

static string s_surfaceName;
static int s_surfaceInPort = 0;
static int s_surfaceOutPort = 0;
static int s_surfaceRefreshRate = 0;
static int s_surfaceDefaultRefreshRate = 15;
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

static void PopulateSurfaceTemplateCombo(HWND hwndDlg, const string &resourcePath)
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
                            const string resourcePath(GetResourcePath());
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
            
            const string resourcePath(GetResourcePath());
            
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
                        
                        bool translated;
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
            string resourcePath(GetResourcePath());
            WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, IDC_COMBO_SurfaceTemplate));
            WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg, IDC_COMBO_ZoneTemplates));
            
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
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Custom), BM_SETCHECK, listener->custom ? BST_CHECKED : BST_UNCHECKED, 0);
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
    SendMessage(GetDlgItem(hwndDlg, IDC_CHECK_Custom), BM_SETCHECK, BST_UNCHECKED, 0);
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
            
            if (g_csiForGui)
            {
                CheckDlgButton(hwndDlg, IDC_CHECK_ShowRawInput, g_csiForGui->GetSurfaceRawInDisplay());
                CheckDlgButton(hwndDlg, IDC_CHECK_ShowInput, g_csiForGui->GetSurfaceInDisplay());
                CheckDlgButton(hwndDlg, IDC_CHECK_ShowOutput, g_csiForGui->GetSurfaceOutDisplay());
                CheckDlgButton(hwndDlg, IDC_CHECK_WriteFXParams, g_csiForGui->GetFXParamsWrite());
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
                        if (g_csiForGui)
                        {
                            g_csiForGui->SetSurfaceRawInDisplay(!! IsDlgButtonChecked(hwndDlg, IDC_CHECK_ShowRawInput));
                            g_csiForGui->SetSurfaceInDisplay(!! IsDlgButtonChecked(hwndDlg, IDC_CHECK_ShowInput));
                            g_csiForGui->SetSurfaceOutDisplay(!! IsDlgButtonChecked(hwndDlg, IDC_CHECK_ShowOutput));
                            g_csiForGui->SetFXParamsWrite(!! IsDlgButtonChecked(hwndDlg, IDC_CHECK_WriteFXParams));
                        }
                        
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
                                s_surfaceRefreshRate = s_surfaces.Get(index)->surfaceRefreshRate;
                                
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
                                    s_surfaces.Get(index)->surfaceRefreshRate = s_surfaceRefreshRate;
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
                            
                            if (surface->type == s_MidiSurfaceToken)
                            {
                                if (tokens.size() == 5)
                                    surface->surfaceRefreshRate = atoi(tokens[4]);
                                else
                                    surface->surfaceRefreshRate = s_surfaceDefaultRefreshRate;
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

                    if (s_surfaces.Get(i)->type == s_OSCSurfaceToken)
                        fprintf(iniFile, "%s ", s_surfaces.Get(i)->remoteDeviceIP.c_str());
                    
                    int refreshRate = s_surfaces.Get(i)->surfaceRefreshRate < 1 ? s_surfaceDefaultRefreshRate : s_surfaces.Get(i)->surfaceRefreshRate;
                    
                    if (s_surfaces.Get(i)->type == s_MidiSurfaceToken)
                        fprintf(iniFile, "%d ", refreshRate);
                    
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
                            if (s_pages.Get(i)->broadcasters.Get(j)->listeners.Get(k)->custom)
                                fprintf(iniFile, "Custom ");

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
