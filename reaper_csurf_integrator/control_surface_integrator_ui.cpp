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
static vector<vector<string>> surfaceLayout;
static vector<vector<string>> surfaceLayoutTemplate;
int numGroups = 0;

struct FXParamLayoutTemplate
{
    string modifiers = "";
    string suffix = "";
    string widgetAction = "";
    string aliasDisplayAction = "";
    string valueDisplayAction = "";
};

static vector<FXParamLayoutTemplate> layoutTemplates;

struct FXParamDefinition
{
    string widget = "";
    string paramNumber = "";
    map<string, string> widgetProperties;

    string aliasDisplayWidget = "";
    string alias = "";
    map<string, string> aliasDisplayWidgetProperties;
    
    string valueDisplayWidget = "";
    map<string, string> valueDisplayWidgetProperties;

    string delta = "";
    vector<string> deltas;
    string rangeMinimum = "";
    string rangeMaximum = "";
    vector<string> steps;
    vector<string> ticks;
};

struct FXParamDefinitions
{
    vector<FXParamDefinition> definitions;
};

static void GetWidgetName(string line, int listSlotIndex, string &widgetName)
{
    istringstream modifiersAndWidgetName(line);
    vector<string> modifiersAndWidgetNameTokens;
    string modifiersAndWidgetNameToken;
    
    while (getline(modifiersAndWidgetName, modifiersAndWidgetNameToken, '+'))
        modifiersAndWidgetNameTokens.push_back(modifiersAndWidgetNameToken);
    
    widgetName = modifiersAndWidgetNameTokens[modifiersAndWidgetNameTokens.size() - 1];

    widgetName = widgetName.substr(0, widgetName.length() - layoutTemplates[listSlotIndex].suffix.length());
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

static void GetSteppedValues(vector<string> params, string &deltaValue, vector<string> &acceleratedDeltaValues, string &rangeMinimum, string &rangeMaximum, vector<string> &steppedValues, vector<string> &acceleratedTickValues)
{
    auto openSquareBrace = find(params.begin(), params.end(), "[");
    auto closeSquareBrace = find(params.begin(), params.end(), "]");
    
    if(openSquareBrace != params.end() && closeSquareBrace != params.end())
    {
        for(auto it = openSquareBrace + 1; it != closeSquareBrace; ++it)
        {
            string strVal = *(it);
            
            if(regex_match(strVal, regex("-?[0-9]+[.][0-9]+")) || regex_match(strVal, regex("-?[0-9]+")))
                steppedValues.push_back(strVal);
            else if(regex_match(strVal, regex("[(]-?[0-9]+[.][0-9]+[)]")))
                deltaValue = stod(strVal);
            else if(regex_match(strVal, regex("[(]-?[0-9]+[)]")))
                acceleratedTickValues.push_back(strVal);
            else if(regex_match(strVal, regex("[(](-?[0-9]+[.][0-9]+[,])+-?[0-9]+[.][0-9]+[)]")))
            {
                istringstream acceleratedDeltaValueStream(strVal);
                string deltaValue;
                
                while (getline(acceleratedDeltaValueStream, deltaValue, ','))
                    acceleratedDeltaValues.push_back(deltaValue);
            }
            else if(regex_match(strVal, regex("[(](-?[0-9]+[,])+-?[0-9]+[)]")))
            {
                istringstream acceleratedTickValueStream(strVal.substr( 1, strVal.length() - 2 ));
                string tickValue;
                
                while (getline(acceleratedTickValueStream, tickValue, ','))
                    acceleratedTickValues.push_back(tickValue);
            }
            else if(regex_match(strVal, regex("-?[0-9]+[.][0-9]+[>]-?[0-9]+[.][0-9]+")) || regex_match(strVal, regex("[0-9]+[-][0-9]+")))
            {
                istringstream range(strVal);
                vector<string> range_tokens;
                string range_token;
                
                while (getline(range, range_token, '>'))
                    range_tokens.push_back(range_token);
                
                if(range_tokens.size() == 2)
                {
                    string firstValue = range_tokens[0];
                    string lastValue = range_tokens[1];
                    
                    if(lastValue > firstValue)
                    {
                        rangeMinimum = firstValue;
                        rangeMaximum = lastValue;
                    }
                    else
                    {
                        rangeMinimum = lastValue;
                        rangeMaximum = firstValue;
                    }
                }
            }
        }
    }
}

static vector<FXParamDefinitions> paramDefs;

static vector<string> prologue;
static vector<string> epilogue;
static vector<string> rawParams;
static map<string, string> rawParamsDictionary;

static string fxName = "";
static string fxAlias = "";

void UnpackZone(string fullPath)
{
    paramDefs.clear();
    prologue.clear();
    epilogue.clear();
    rawParams.clear();
    rawParamsDictionary.clear();

    fxName = "";
    fxAlias = "";

    bool inZone = false;
    bool inAutoZone = false;
    bool pastAutoZone = false;
    
    ifstream autoFXFile(fullPath);
    
    int listSlotIndex = 0;
    
    FXParamDefinitions definitions;
    paramDefs.push_back(definitions);
    
    for (string line; getline(autoFXFile, line) ; )
    {
        line = regex_replace(line, regex(CRLFChars), "");

        if(inAutoZone && ! pastAutoZone)
        {
            // Trim leading and trailing spaces
            line = regex_replace(line, regex("^\\s+|\\s+$"), "", regex_constants::format_default);
            
            if(line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
        }

        vector<string> tokens = GetTokens(line);
        
        if(line.substr(0, 5) == "Zone ")
        {
            inZone = true;
            
            if(tokens.size() > 1)
                fxName = tokens[1];
            if(tokens.size() > 2)
                fxAlias = tokens[2];
            
            continue;
        }
        else if(line == BeginAutoSection)
        {
            inAutoZone = true;
            continue;
        }
        else if(inZone && ! inAutoZone)
        {
            prologue.push_back(line);
            continue;
        }
        else if(line == EndAutoSection)
        {
            pastAutoZone = true;
            continue;
        }
        else if(line == "ZoneEnd")
        {
            inZone = false;
            continue;
        }
        else if(inZone && pastAutoZone)
        {
            epilogue.push_back(line);
            continue;
        }
        else if(! inZone && pastAutoZone)
        {
            if(line != "")
                rawParams.push_back(line);
            continue;
        }
        else
        {
            tokens = GetTokens(line);
            
            if(tokens[0].find(layoutTemplates[listSlotIndex].suffix) == string::npos)
            {
                listSlotIndex++;
                FXParamDefinitions definitions;
                paramDefs.push_back(definitions);
            }
            
            FXParamDefinition def;
            
            GetWidgetName(tokens[0], listSlotIndex, def.widget);
            
            if(tokens.size() > 2)
                def.paramNumber = tokens[2];
            
            if(tokens.size() > 4 && tokens[3] == "[")
            {
                vector<string> params;

                for(int i = 3; i < tokens.size() && tokens[i] != "]"; i++)
                    params.push_back(tokens[i]);
                
                params.push_back("]");
                
                GetSteppedValues(params, def.delta, def.deltas, def.rangeMinimum, def.rangeMaximum, def.steps, def.ticks);
            }
            
            int propertiesOffset = 3;
            
            if (def.steps.size() != 0)
                propertiesOffset = def.steps.size() + 5;
            
            if(tokens.size() > propertiesOffset)
                GetProperties(propertiesOffset,  tokens.size(), tokens, def.widgetProperties);
            
            if(getline(autoFXFile, line))
            {
                tokens = GetTokens(line);

                if(tokens.size() > 2)
                {
                    vector<string> modifers;
                    
                    GetWidgetName(tokens[0], listSlotIndex, def.aliasDisplayWidget);
                    
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
                    
                    GetWidgetName(tokens[0], listSlotIndex, def.valueDisplayWidget);
                    
                    if(tokens.size() > 3)
                        GetProperties(3, tokens.size(), tokens, def.valueDisplayWidgetProperties);
                }
            }
            else
                continue;
           
            paramDefs.back().definitions.push_back(def);
        }
    }
}

static vector<int> paramNumEditControls = { IDC_FXParamNumEdit1, IDC_FXParamNumEdit2, IDC_FXParamNumEdit3 };
static vector<int> widgetTypePickers = { IDC_PickWidgetType1, IDC_PickWidgetType2, IDC_PickWidgetType3 };
static vector<int> ringStylePickers = { IDC_PickRingStyle1, IDC_PickRingStyle2, IDC_PickRingStyle3 };
static vector<int> fixedTextEditControls = { IDC_FXParamNameEdit1, IDC_FXParamNameEdit2, IDC_FXParamNameEdit3 };
static vector<int> fixedTextDisplayRowPickers = { IDC_FixedTextDisplayPickRow1 , IDC_FixedTextDisplayPickRow2, IDC_FixedTextDisplayPickRow3 };
static vector<int> fixedTextDisplayFontLabels = { IDC_FixedTextDisplayFontLabel1, IDC_FixedTextDisplayFontLabel2, IDC_FixedTextDisplayFontLabel3 };
static vector<int> fixedTextDisplayFontPickers = { IDC_FixedTextDisplayPickFont1, IDC_FixedTextDisplayPickFont2, IDC_FixedTextDisplayPickFont3 };
static vector<int> paramValueDisplayRowPickers = { IDC_FXParamValueDisplayPickRow1 , IDC_FXParamValueDisplayPickRow2, IDC_FXParamValueDisplayPickRow3 };
static vector<int> paramValueDisplayFontLabels = { IDC_FXParamValueDisplayFontLabel1, IDC_FXParamValueDisplayFontLabel2, IDC_FXParamValueDisplayFontLabel3 };
static vector<int> paramValueDisplayFontPickers = { IDC_FXParamValueDisplayPickFont1, IDC_FXParamValueDisplayPickFont2, IDC_FXParamValueDisplayPickFont3 };

static vector<int> stepPickers = { IDC_PickSteps1, IDC_PickSteps2, IDC_PickSteps3 };
static vector<int> stepEditControls = { IDC_EditSteps1, IDC_EditSteps2, IDC_EditSteps3 };
static vector<int> stepPrompts = { IDC_StepsPromptGroup1, IDC_StepsPromptGroup2, IDC_StepsPromptGroup3 };

static vector<int> widgetRingColorBoxes = { IDC_FXParamRingColorBox1, IDC_FXParamRingColorBox2, IDC_FXParamRingColorBox3 };
static vector<int> widgetRingColors = { IDC_FXParamRingColor1, IDC_FXParamRingColor2, IDC_FXParamRingColor3 };
static vector<int> widgetRingIndicatorColorBoxes = { IDC_FXParamIndicatorColorBox1, IDC_FXParamIndicatorColorBox2, IDC_FXParamIndicatorColorBox3 };
static vector<int> widgetRingIndicators = { IDC_FXParamIndicatorColor1, IDC_FXParamIndicatorColor2, IDC_FXParamIndicatorColor3 };
static vector<int> fixedTextDisplayForegroundColors = { IDC_FixedTextDisplayForegroundColor1, IDC_FixedTextDisplayForegroundColor2, IDC_FixedTextDisplayForegroundColor3 };
static vector<int> fixedTextDisplayForegroundColorBoxes = { IDC_FXFixedTextDisplayForegroundColorBox1, IDC_FXFixedTextDisplayForegroundColorBox2, IDC_FXFixedTextDisplayForegroundColorBox3 };
static vector<int> fixedTextDisplayBackgroundColors = { IDC_FixedTextDisplayBackgroundColor1, IDC_FixedTextDisplayBackgroundColor2, IDC_FixedTextDisplayBackgroundColor3 };
static vector<int> fixedTextDisplayBackgroundColorBoxes = { IDC_FXFixedTextDisplayBackgroundColorBox1, IDC_FXFixedTextDisplayBackgroundColorBox2, IDC_FXFixedTextDisplayBackgroundColorBox3 };
static vector<int> fxParamDisplayForegroundColors = { IDC_FXParamDisplayForegroundColor1, IDC_FXParamDisplayForegroundColor2, IDC_FXParamDisplayForegroundColor3 };
static vector<int> fxParamDisplayForegroundColorBoxes = { IDC_FXParamValueDisplayForegroundColorBox1, IDC_FXParamValueDisplayForegroundColorBox2, IDC_FXParamValueDisplayForegroundColorBox3 };
static vector<int> fxParamDisplayBackgroundColors = { IDC_FXParamDisplayBackgroundColor1, IDC_FXParamDisplayBackgroundColor2, IDC_FXParamDisplayBackgroundColor3 };
static vector<int> fxParamDisplayBackgroundColorBoxes = { IDC_FXParamValueDisplayBackgroundColorBox1, IDC_FXParamValueDisplayBackgroundColorBox2, IDC_FXParamValueDisplayBackgroundColorBox3 };


// for show / hide
static vector<int> groupBoxes = { IDC_Group1, IDC_Group2, IDC_Group3 };
static vector<int> fxParamGroupBoxes = { IDC_GroupFXParam1, IDC_GroupFXParam2, IDC_GroupFXParam3 };
static vector<int> fixedTextDisplayGroupBoxes = { IDC_GroupFixedTextDisplay1 , IDC_GroupFixedTextDisplay2, IDC_GroupFixedTextDisplay3 };
static vector<int> fxParamDisplayGroupBoxes = { IDC_GroupFXParamValueDisplay1 , IDC_GroupFXParamValueDisplay2, IDC_GroupFXParamValueDisplay3 };
static vector<int> advancedButtons = { IDC_AdvancedGroup1 , IDC_AdvancedGroup2, IDC_AdvancedGroup3 };

static vector<vector<int>> baseControls =
{
    paramNumEditControls,
    widgetTypePickers,
    ringStylePickers,
    
    fixedTextEditControls,
    fixedTextDisplayRowPickers,
    
    paramValueDisplayRowPickers,
    
    stepPickers,
    stepEditControls,
    stepPrompts,
    
    groupBoxes,
    fxParamGroupBoxes,
    fixedTextDisplayGroupBoxes,
    fxParamDisplayGroupBoxes,
    advancedButtons
};

static vector<vector<int>> fontControls =
{
    fixedTextDisplayFontLabels,
    fixedTextDisplayFontPickers,
    paramValueDisplayFontLabels,
    paramValueDisplayFontPickers
};

static vector<vector<int>> colorControls =
{
    widgetRingColorBoxes,
    widgetRingColors,
    widgetRingIndicatorColorBoxes,
    widgetRingIndicators,
    fixedTextDisplayForegroundColors,
    fixedTextDisplayForegroundColorBoxes,
    fixedTextDisplayBackgroundColors,
    fixedTextDisplayBackgroundColorBoxes,
    fxParamDisplayForegroundColors,
    fxParamDisplayForegroundColorBoxes,
    fxParamDisplayBackgroundColors,
    fxParamDisplayBackgroundColorBoxes,
};

static void ShowBaseControls(HWND hwndDlg, int startIndex, int endIndex, bool show)
{
    for(auto controls : baseControls)
        for(int i = startIndex; i < endIndex; i++)
            ShowWindow(GetDlgItem(hwndDlg, controls[i]), show);
}

static void ShowFontControls(HWND hwndDlg, int startIndex, int endIndex, bool show)
{
    for(auto controls : fontControls)
        for(int i = startIndex; i < endIndex; i++)
            ShowWindow(GetDlgItem(hwndDlg, controls[i]), show);
}

static void ShowColorControls(HWND hwndDlg, int startIndex, int endIndex, bool show)
{
    for(auto controls : colorControls)
        for(int i = startIndex; i < endIndex; i++)
            ShowWindow(GetDlgItem(hwndDlg, controls[i]), show);
}

static map<int, int> buttonColors =
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

static map<int, int> buttonColorBoxes =
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

    for(int i = 0; i < rawParams.size(); i++)
    {
        char buf[BUFSZ];
        
        sprintf(buf, rawParams[i].c_str());
        
        lvi.iItem = i;
        lvi.pszText = buf;
        
        ListView_InsertItem(hwndParamList, &lvi);
        
        
        int spaceBreak = rawParams[i].find( " ");
          
        if(spaceBreak != -1)
        {
            string key = rawParams[i].substr(0, spaceBreak);
            string value = rawParams[i].substr(spaceBreak + 1, rawParams[i].length() - spaceBreak - 1);
            
            rawParamsDictionary[key] = value;
        }
    }
}

static int dlgResult = IDCANCEL;

static int fxListIndex = 0;

static bool hasFonts = false;
static bool hasColors = false;

static WDL_DLGRET dlgProcEditFXParam(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
            
        case WM_PAINT:
        {
            if(hasColors)
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwndDlg, &ps);
                
                for(auto [ colorPicker, colorValue ] :  buttonColors)
                {
                    HBRUSH brush = CreateSolidBrush(colorValue);
                    
                    RECT clientRect, windowRect;
                    POINT p;
                    GetClientRect(GetDlgItem(hwndDlg, buttonColorBoxes[colorPicker]), &clientRect);
                    GetWindowRect(GetDlgItem(hwndDlg, buttonColorBoxes[colorPicker]), &windowRect);
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
            dlgResult = IDCANCEL;
            
            hasFonts = false;
            hasColors = false;
            
            ShowBaseControls(hwndDlg, 0, groupBoxes.size(), false );
            ShowFontControls(hwndDlg, 0, groupBoxes.size(), false);
            ShowColorControls(hwndDlg, 0, groupBoxes.size(), false);

            SetWindowText(hwndDlg, (fxAlias + "   " + layoutTemplates[fxListIndex].modifiers + layoutTemplates[fxListIndex].suffix).c_str());

            for(int i = 0; i < stepPickers.size(); i++)
            {
                SendDlgItemMessage(hwndDlg, stepPickers[i], CB_ADDSTRING, 0, (LPARAM)"Custom");
                
                for(auto [key, value] : SteppedValueDictionary)
                    SendDlgItemMessage(hwndDlg, stepPickers[i], CB_ADDSTRING, 0, (LPARAM)to_string(key).c_str());
            }
                                      
            PopulateParamListView(GetDlgItem(hwndDlg, IDC_AllParams));
            
            for(auto layout : surfaceLayoutTemplate)
            {
                if(layout.size() > 0 )
                {
                    if(layout[0] == "WidgetTypes")
                    {
                        for(int i = 0; i < widgetTypePickers.size(); i++)
                            for(int j = 1; j < layout.size(); j++)
                                SendDlgItemMessage(hwndDlg, widgetTypePickers[i], CB_ADDSTRING, 0, (LPARAM)layout[j].c_str());
                    }
                    else if(layout[0] == "RingStyles")
                    {
                        for(int i = 0; i < ringStylePickers.size(); i++)
                            for(int j = 1; j < layout.size(); j++)
                                SendDlgItemMessage(hwndDlg, ringStylePickers[i], CB_ADDSTRING, 0, (LPARAM)layout[j].c_str());
                    }
                    else if(layout[0] == "DisplayRows")
                    {
                        for(int i = 0; i < fixedTextDisplayRowPickers.size(); i++)
                        {
                            SendDlgItemMessage(hwndDlg, fixedTextDisplayRowPickers[i], CB_ADDSTRING, 0, (LPARAM)"");

                            for(int j = 1; j < layout.size(); j++)
                                SendDlgItemMessage(hwndDlg, fixedTextDisplayRowPickers[i], CB_ADDSTRING, 0, (LPARAM)layout[j].c_str());
                        }
                        
                        for(int i = 0; i < paramValueDisplayRowPickers.size(); i++)
                        {
                            SendDlgItemMessage(hwndDlg, paramValueDisplayRowPickers[i], CB_ADDSTRING, 0, (LPARAM)"");

                            for(int j = 1; j < layout.size(); j++)
                                SendDlgItemMessage(hwndDlg, paramValueDisplayRowPickers[i], CB_ADDSTRING, 0, (LPARAM)layout[j].c_str());
                        }
                    }
                    else if(layout[0] == "DisplayFonts")
                    {
                        hasFonts = true;
                        
                        for(int i = 0; i < fixedTextDisplayFontPickers.size(); i++)
                            for(int j = 1; j < layout.size(); j++)
                                SendDlgItemMessage(hwndDlg, fixedTextDisplayFontPickers[i], CB_ADDSTRING, 0, (LPARAM)layout[j].c_str());
                        
                        for(int i = 0; i < paramValueDisplayFontPickers.size(); i++)
                            for(int j = 1; j < layout.size(); j++)
                                SendDlgItemMessage(hwndDlg, paramValueDisplayFontPickers[i], CB_ADDSTRING, 0, (LPARAM)layout[j].c_str());
                    }
                }
            }

            for(int i = 0; i < paramDefs[fxListIndex].definitions.size() && i < paramNumEditControls.size(); i++)
            {
                SetDlgItemText(hwndDlg, paramNumEditControls[i], paramDefs[fxListIndex].definitions[i].paramNumber.c_str());
                SetDlgItemText(hwndDlg, fixedTextEditControls[i], paramDefs[fxListIndex].definitions[i].alias.c_str());

                SetDlgItemText(hwndDlg, widgetTypePickers[i], paramDefs[fxListIndex].definitions[i].widget.c_str());
                SetDlgItemText(hwndDlg, fixedTextDisplayRowPickers[i], paramDefs[fxListIndex].definitions[i].aliasDisplayWidget.c_str());
                SetDlgItemText(hwndDlg, paramValueDisplayRowPickers[i], paramDefs[fxListIndex].definitions[i].valueDisplayWidget.c_str());

                if(paramDefs[fxListIndex].definitions[i].widgetProperties.count("RingStyle") > 0)
                    SetDlgItemText(hwndDlg, ringStylePickers[i], paramDefs[fxListIndex].definitions[i].widgetProperties["RingStyle"].c_str());
                
                if(paramDefs[fxListIndex].definitions[i].aliasDisplayWidgetProperties.count("Font") > 0)
                    SetDlgItemText(hwndDlg, fixedTextDisplayFontPickers[i], paramDefs[fxListIndex].definitions[i].aliasDisplayWidgetProperties["Font"].c_str());

                if(paramDefs[fxListIndex].definitions[i].valueDisplayWidgetProperties.count("Font") > 0)
                    SetDlgItemText(hwndDlg, paramValueDisplayFontPickers[i], paramDefs[fxListIndex].definitions[i].valueDisplayWidgetProperties["Font"].c_str());

                if(paramDefs[fxListIndex].definitions[i].widgetProperties.count("LEDRingColor") > 0)
                {
                    hasColors = true;
                    rgba_color color = GetColorValue(paramDefs[fxListIndex].definitions[i].widgetProperties["LEDRingColor"]);
                    buttonColors[widgetRingColors[i]] = DAW::ColorToNative(color.r, color.g, color.b);
                }

                if(paramDefs[fxListIndex].definitions[i].widgetProperties.count("PushColor") > 0)
                {
                    hasColors = true;
                    rgba_color color = GetColorValue(paramDefs[fxListIndex].definitions[i].widgetProperties["PushColor"]);
                    buttonColors[widgetRingIndicators[i]] = DAW::ColorToNative(color.r, color.g, color.b);
                }

                if(paramDefs[fxListIndex].definitions[i].aliasDisplayWidgetProperties.count("Foreground") > 0)
                {
                    hasColors = true;
                    rgba_color color = GetColorValue(paramDefs[fxListIndex].definitions[i].aliasDisplayWidgetProperties["Foreground"]);
                    buttonColors[fixedTextDisplayForegroundColors[i]] = DAW::ColorToNative(color.r, color.g, color.b);
                }

                if(paramDefs[fxListIndex].definitions[i].aliasDisplayWidgetProperties.count("Background") > 0)
                {
                    hasColors = true;
                    rgba_color color = GetColorValue(paramDefs[fxListIndex].definitions[i].aliasDisplayWidgetProperties["Background"]);
                    buttonColors[fixedTextDisplayBackgroundColors[i]] = DAW::ColorToNative(color.r, color.g, color.b);
                }

                if(paramDefs[fxListIndex].definitions[i].valueDisplayWidgetProperties.count("Foreground") > 0)
                {
                    hasColors = true;
                    rgba_color color = GetColorValue(paramDefs[fxListIndex].definitions[i].valueDisplayWidgetProperties["Foreground"]);
                    buttonColors[fxParamDisplayForegroundColors[i]] = DAW::ColorToNative(color.r, color.g, color.b);
                }

                if(paramDefs[fxListIndex].definitions[i].valueDisplayWidgetProperties.count("Background") > 0)
                {
                    hasColors = true;
                    rgba_color color = GetColorValue(paramDefs[fxListIndex].definitions[i].valueDisplayWidgetProperties["Background"]);
                    buttonColors[fxParamDisplayBackgroundColors[i]] = DAW::ColorToNative(color.r, color.g, color.b);
                }

                string steps = "";
                
                for(auto step : paramDefs[fxListIndex].definitions[i].steps)
                    steps += step + "  ";
                
                SetDlgItemText(hwndDlg, stepEditControls[i], steps.c_str());
            }
            
            ShowBaseControls(hwndDlg, 0, paramDefs[fxListIndex].definitions.size(), true);
            
            if(hasFonts)
                ShowFontControls(hwndDlg, 0, paramDefs[fxListIndex].definitions.size(), true);
            
            if(hasColors)
            {
                ShowColorControls(hwndDlg, 0, paramDefs[fxListIndex].definitions.size(), true);
                InvalidateRect(hwndDlg, NULL, true);
            }
            
            break;
        }
            
        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case IDC_FXParamRingColor1:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FXParamRingColor1]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamRingColor2:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FXParamRingColor2]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamRingColor3:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FXParamRingColor3]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamIndicatorColor1:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FXParamIndicatorColor1]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamIndicatorColor2:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FXParamIndicatorColor2]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamIndicatorColor3:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FXParamIndicatorColor3]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FixedTextDisplayForegroundColor1:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FixedTextDisplayForegroundColor1]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FixedTextDisplayForegroundColor2:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FixedTextDisplayForegroundColor2]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FixedTextDisplayForegroundColor3:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FixedTextDisplayForegroundColor3]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FixedTextDisplayBackgroundColor1:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FixedTextDisplayBackgroundColor1]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FixedTextDisplayBackgroundColor2:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FixedTextDisplayBackgroundColor2]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FixedTextDisplayBackgroundColor3:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FixedTextDisplayBackgroundColor3]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamDisplayForegroundColor1:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FXParamDisplayForegroundColor1]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamDisplayForegroundColor2:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FXParamDisplayForegroundColor2]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamDisplayForegroundColor3:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FXParamDisplayForegroundColor3]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamDisplayBackgroundColor1:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FXParamDisplayBackgroundColor1]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamDisplayBackgroundColor2:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FXParamDisplayBackgroundColor2]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                    
                case IDC_FXParamDisplayBackgroundColor3:
                    {
                        DAW::GR_SelectColor(hwndDlg, &buttonColors[IDC_FXParamDisplayBackgroundColor3]);
                        InvalidateRect(hwndDlg, NULL, true);
                    }
                        break;
                                     
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
                        for(int i = 0; i < paramDefs[fxListIndex].definitions.size(); i++)
                        {
                            char buf[BUFSZ];
                            
                            GetDlgItemText(hwndDlg, paramNumEditControls[i], buf, sizeof(buf));
                            paramDefs[fxListIndex].definitions[i].paramNumber = buf;

                            GetDlgItemText(hwndDlg, widgetTypePickers[i], buf, sizeof(buf));
                            paramDefs[fxListIndex].definitions[i].widget = buf;
                            
                            GetDlgItemText(hwndDlg, ringStylePickers[i], buf, sizeof(buf));
                            paramDefs[fxListIndex].definitions[i].widgetProperties["RingStyle"] = buf;
                            
                            GetDlgItemText(hwndDlg, fixedTextEditControls[i], buf, sizeof(buf));
                            paramDefs[fxListIndex].definitions[i].alias = buf;

                            GetDlgItemText(hwndDlg, fixedTextDisplayRowPickers[i], buf, sizeof(buf));
                            paramDefs[fxListIndex].definitions[i].aliasDisplayWidget = buf;

                            GetDlgItemText(hwndDlg, paramValueDisplayRowPickers[i], buf, sizeof(buf));
                            paramDefs[fxListIndex].definitions[i].valueDisplayWidget = buf;

                            GetDlgItemText(hwndDlg, stepEditControls[i], buf, sizeof(buf));
                            
                            if(string(buf) != "")
                            {
                                paramDefs[fxListIndex].definitions[i].steps.clear();
                                for(auto step : GetTokens(buf))
                                    paramDefs[fxListIndex].definitions[i].steps.push_back(step);
                            }

                            if(hasFonts)
                            {
                                GetDlgItemText(hwndDlg, fixedTextDisplayFontPickers[i], buf, sizeof(buf));
                                paramDefs[fxListIndex].definitions[i].aliasDisplayWidgetProperties["Font"] = buf;
                                
                                GetDlgItemText(hwndDlg, paramValueDisplayFontPickers[i], buf, sizeof(buf));
                                paramDefs[fxListIndex].definitions[i].valueDisplayWidgetProperties["Font"] = buf;
                            }
                            
                            if(hasColors)
                            {
                                rgba_color color;
                                
                                DAW::ColorFromNative(buttonColors[widgetRingColors[i]], &color.r, &color.g, &color.b);
                                paramDefs[fxListIndex].definitions[i].widgetProperties["LEDRingColor"] = color.to_string();
                                
                                DAW::ColorFromNative(buttonColors[widgetRingIndicators[i]], &color.r, &color.g, &color.b);
                                paramDefs[fxListIndex].definitions[i].widgetProperties["PushColor"] = color.to_string();

                                DAW::ColorFromNative(buttonColors[fixedTextDisplayForegroundColors[i]], &color.r, &color.g, &color.b);
                                paramDefs[fxListIndex].definitions[i].aliasDisplayWidgetProperties["Foreground"] = color.to_string();

                                DAW::ColorFromNative(buttonColors[fixedTextDisplayBackgroundColors[i]], &color.r, &color.g, &color.b);
                                paramDefs[fxListIndex].definitions[i].aliasDisplayWidgetProperties["Background"] = color.to_string();

                                DAW::ColorFromNative(buttonColors[fxParamDisplayForegroundColors[i]], &color.r, &color.g, &color.b);
                                paramDefs[fxListIndex].definitions[i].valueDisplayWidgetProperties["Foreground"] = color.to_string();

                                DAW::ColorFromNative(buttonColors[fxParamDisplayBackgroundColors[i]], &color.r, &color.g, &color.b);
                                paramDefs[fxListIndex].definitions[i].valueDisplayWidgetProperties["Background"] = color.to_string();
                            }
                        }
                       
                        dlgResult = IDOK;
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
                                    
                                    for(auto step : SteppedValueDictionary[index + 1])
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
                                    
                                    for(auto step : SteppedValueDictionary[index + 1])
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
                                    
                                    for(auto step : SteppedValueDictionary[index + 1])
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
    
    components.push_back(layoutTemplates[index].modifiers + layoutTemplates[index].suffix);
    
    for(auto paramDef :  paramDefs[index].definitions)
    {
        string widgetName = paramDef.widget;
        
        if(widgetName == "RotaryPush")
            widgetName = "Push";
        
        components.push_back(widgetName);
        
        string alias = paramDef.alias;

        if(paramDef.alias == "" && paramDef.paramNumber != "" && rawParamsDictionary.count(paramDef.paramNumber) > 0)
            alias = rawParamsDictionary[paramDef.paramNumber];
        else if(paramDef.paramNumber == "")
            alias = "NoAction";
        
        components.push_back(alias);
    }
        
    return components;
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
        vector<string> components = GetLineComponents(i);
        
        string preamble = components[0];
        
#ifdef _WIN32
        preamble += "                                                       ";
#endif
        
        sprintf(buf, preamble.c_str());
                       
        lvi.iItem = i;
        lvi.pszText = buf;
        
        ListView_InsertItem(hwndParamList, &lvi);
        
        for(int j = 1; j < components.size(); j++)
        {
            lvi.iSubItem = j;
            sprintf(buf, components[j].c_str());
            lvi.pszText = buf;

            ListView_SetItem(hwndParamList, &lvi);
        }

        lvi.iSubItem = 0;
    }
}

static void MoveUp(HWND hwndParamList)
{
    int index = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
    if(index > 0)
    {
        FXParamDefinitions itemToMove;
        
        for(auto def : paramDefs[index].definitions)
            itemToMove.definitions.push_back(def);
        
        paramDefs.erase(paramDefs.begin() + index);
        paramDefs.insert(paramDefs.begin() + index - 1, itemToMove);
        
        PopulateListView(hwndParamList);
        
        ListView_SetItemState(hwndParamList, index - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

static void MoveDown(HWND hwndParamList)
{
    int index = ListView_GetNextItem(hwndParamList, -1, LVNI_SELECTED);
    if(index >= 0 && index < paramDefs.size() - 1)
    {
        FXParamDefinitions itemToMove;
        
        for(auto def : paramDefs[index].definitions)
            itemToMove.definitions.push_back(def);
        
        paramDefs.erase(paramDefs.begin() + index);
        paramDefs.insert(paramDefs.begin() + index + 1, itemToMove);

        PopulateListView(hwndParamList);

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
                        PopulateListView(hwndParamList);
                }
            }
        }
            break;
            
        case WM_INITDIALOG:
        {
            HWND paramList = GetDlgItem(hwndDlg, IDC_PARAM_LIST);
            
            vector<int> columnSizes = { 160 };
            
            for(int i = 1; i <= numGroups; i++)
            {
                columnSizes.push_back(80);
                columnSizes.push_back(150);
            }
            
            LVCOLUMN columnDescriptor = { LVCF_TEXT | LVCF_WIDTH, 0, 0, (char*)"" };
            columnDescriptor.cx = columnSizes[0];
            
            ListView_InsertColumn(paramList, 0, &columnDescriptor);
            
            for(int i = 1; i <= numGroups * 2; i++)
            {
                columnDescriptor.cx = columnSizes[i];
                ListView_InsertColumn(paramList, i, &columnDescriptor);
            }

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
                
                FXParamDefinitions itemToMove;
                
                for(auto def : paramDefs[oldPosition].definitions)
                    itemToMove.definitions.push_back(def);
                
                paramDefs.erase(paramDefs.begin() + oldPosition);
                paramDefs.insert(paramDefs.begin() + lvhti.iItem, itemToMove);
                
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
                                PopulateListView(hwndParamList);
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
                        PopulateListView(hwndParamList);                }
            }
        }
            break;
            
        case WM_INITDIALOG:
        {
            HWND paramList = GetDlgItem(hwndDlg, IDC_PARAM_LIST);
            
            vector<int> columnSizes = { 65 };
            
            for(int i = 1; i <= numGroups; i++)
            {
                columnSizes.push_back(38);
                columnSizes.push_back(75);
            }
            
            LVCOLUMN columnDescriptor = { LVCF_TEXT | LVCF_WIDTH, LVCFMT_RIGHT, 0, (char*)"" };
            columnDescriptor.cx = columnSizes[0];
            
            ListView_InsertColumn(paramList, 0, &columnDescriptor);
            
            for(int i = 1; i <= numGroups * 2; i++)
            {
                columnDescriptor.cx = columnSizes[i];
                ListView_InsertColumn(paramList, i, &columnDescriptor);
            }
            
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
                        char buf[BUFSZ];
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
                                PopulateListView(hwndParamList);
                            
                            dlgResult = IDCANCEL;
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
    layoutTemplates.clear();
    
    surfaceLayout = zoneManager->GetSurfaceFXLayout();
    surfaceLayoutTemplate = zoneManager->GetSurfaceFXLayoutTemplate();

    numGroups = 0;
    
    for(auto layout : surfaceLayoutTemplate)
    {
        if(layout.size() > 0 && layout[0] == "WidgetTypes")
        {
            numGroups = layout.size() - 1;
            break;
        }
    }
    
    string widgetAction = "";
    string aliasDisplayAction = "";
    string valueDisplayAction = "";
    
    for(auto row : surfaceLayoutTemplate)
    {
        if(row.size() > 1)
        {
            if(row[0] == "WidgetAction")
                widgetAction = row[1];
            else if(row[0] == "AliasDisplayAction")
                aliasDisplayAction = row[1];
            else if(row[0] == "ValueDisplayAction")
                valueDisplayAction = row[1];
        }
    }
    
    for(auto layout : zoneManager->GetFXLayouts())
    {
        for(int i = 0; i < layout.channelCount; i++)
        {
            string modifiers = "";
            if(layout.modifiers != "")
                modifiers = layout.modifiers + "+";
            
            FXParamLayoutTemplate layoutTemplate;
            
            layoutTemplate.modifiers = modifiers;
            layoutTemplate.suffix = layout.suffix + to_string(i + 1);
            
            layoutTemplate.widgetAction = widgetAction;
            layoutTemplate.aliasDisplayAction = aliasDisplayAction;
            layoutTemplate.valueDisplayAction = valueDisplayAction;
            
            layoutTemplates.push_back(layoutTemplate);
        }
    }

    UnpackZone(fullPath);
    
    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG_RemapAutoFX), g_hwnd, dlgProcRemapFXAutoZone);
    
    if(dlgResult == IDSAVE)
    {
        ofstream fxFile(fullPath);

        if(fxFile.is_open())
        {
            fxFile << "Zone \"" + fxName + "\" \"" + fxAlias + "\"" + GetLineEnding();

            for(auto line : prologue)
                fxFile << line + GetLineEnding();
            
            fxFile << BeginAutoSection + GetLineEnding() + GetLineEnding();

            for(int i = 0; i < paramDefs.size(); i++)
            {
                for(int j = 0; j < paramDefs[i].definitions.size(); j++)
                {
                    fxFile << "\t" + layoutTemplates[i].modifiers + paramDefs[i].definitions[j].widget + layoutTemplates[i].suffix + "\t";
                    
                    if(paramDefs[i].definitions[j].paramNumber == "")
                    {
                        fxFile << "NoAction" + GetLineEnding();
                    }
                    else
                    {
                        fxFile << layoutTemplates[i].widgetAction + " " + paramDefs[i].definitions[j].paramNumber + " ";
                        
                        if(paramDefs[i].definitions[j].steps.size() > 0)
                        {
                            fxFile << "[ ";
                            
                            for(auto step : paramDefs[i].definitions[j].steps)
                                fxFile << step + " " ;
                                
                            fxFile << "]";
                        }
                        
                        for(auto [ key, value ] : paramDefs[i].definitions[j].widgetProperties)
                            fxFile << " " + key + "=" + value ;
                        
                        fxFile << GetLineEnding();
                    }
                    
                    if(paramDefs[i].definitions[j].paramNumber == "" || paramDefs[i].definitions[j].aliasDisplayWidget == "")
                    {
                        fxFile << "\tNullDisplay\tNoAction" + GetLineEnding();
                    }
                    else
                    {
                        fxFile << "\t" + layoutTemplates[i].modifiers + paramDefs[i].definitions[j].aliasDisplayWidget + layoutTemplates[i].suffix + "\t";
                        
                        fxFile << layoutTemplates[i].aliasDisplayAction + " \"" + paramDefs[i].definitions[j].alias + "\" ";
                        
                        for(auto [ key, value ] : paramDefs[i].definitions[j].aliasDisplayWidgetProperties)
                            fxFile << " " + key + "=" + value;
                        
                        fxFile << GetLineEnding();
                    }
                    
                    if(paramDefs[i].definitions[j].paramNumber == "" || paramDefs[i].definitions[j].valueDisplayWidget == "")
                    {
                        fxFile << "\tNullDisplay\tNoAction" + GetLineEnding();
                    }
                    else
                    {
                        fxFile << "\t" + layoutTemplates[i].modifiers + paramDefs[i].definitions[j].valueDisplayWidget + layoutTemplates[i].suffix + "\t";
                        
                        fxFile << layoutTemplates[i].valueDisplayAction + " " + paramDefs[i].definitions[j].paramNumber;
                        
                        for(auto [ key, value ] : paramDefs[i].definitions[j].valueDisplayWidgetProperties)
                            fxFile << " " + key + "=" + value;
                        
                        fxFile << GetLineEnding();
                    }

                    fxFile << GetLineEnding();
                }
                
                fxFile << GetLineEnding();
            }
            
            fxFile <<  EndAutoSection + GetLineEnding();

            for(auto line : epilogue)
                fxFile << line + GetLineEnding();

            fxFile << "ZoneEnd" + GetLineEnding();

            for(auto line : rawParams)
                fxFile << line + GetLineEnding();
            
            fxFile.close();
        }

        
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
