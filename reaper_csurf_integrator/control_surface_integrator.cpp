//
//  control_surface_integrator.cpp
//  reaper_control_surface_integrator
//
//

#include "control_surface_integrator.h"
#include "control_surface_midi_widgets.h"
#include "control_surface_Reaper_actions.h"
#include "control_surface_manager_actions.h"

#include "WDL/dirscan.h"
#include "WDL/ptrlist.h"
#include "WDL/wdlcstring.h"

#include "resource.h"

CSurfIntegrator *g_csiForGui = NULL;

WDL_DLGRET dlgProcMainConfig(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

extern reaper_plugin_info_t *g_reaper_plugin_info;

int g_minNumParamSteps = 2;
int g_maxNumParamSteps = 30;

void GetSteppedValues(vector<string> &params, double &deltaValue, vector<double> &acceleratedDeltaValues, double &rangeMinimum, double &rangeMaximum, vector<double> &steppedValues, vector<int> &acceleratedTickValues)
{
    vector<string>::iterator openSquareBrace = find(params.begin(), params.end(), "[");
    vector<string>::iterator closeSquareBrace = find(params.begin(), params.end(), "]");

    if (openSquareBrace != params.end() && closeSquareBrace != params.end())
    {
        for (vector<string>::iterator it = openSquareBrace + 1; it != closeSquareBrace; ++it)
        {
            const char *str = it->c_str();

            if (str[0] == '(' && str[strlen(str)-1] == ')')
            {
                str++; // skip (

                // (1.0,2.0,3.0) -> acceleratedDeltaValues : mode = 2
                // (1.0) -> deltaValue : mode = 1
                // (1) or (1,2,3) -> acceleratedTickValues : mode = 0
                const int mode = strstr(str, ".") ? strstr(str, ",") ? 2 : 1 : 0;

                while (*str)
                {
                    if (mode == 0)
                    {
                        int v = 0;
                        if (WDL_NOT_NORMALLY(sscanf(str, "%d", &v) != 1)) break;
                        acceleratedTickValues.push_back(v);
                    }
                    else
                    {
                        double v = 0.0;
                        if (WDL_NOT_NORMALLY(sscanf(str,"%lf", &v) != 1)) break;
                        if (mode == 1)
                        {
                            deltaValue = v;
                            break;
                        }
                        acceleratedDeltaValues.push_back(v);
                    }

                    while (*str && *str != ',') str++;
                    if (*str == ',') str++;
                }
            }
            // todo: support 1-3 syntax? else if (!strstr(str,".") && str[0] != '-' && strstr(str,"-"))
            else
            {
                // 1.0>3.0 writes to rangeMinimum/rangeMaximum
                // 1 or 1.0 -> steppedValues
                double a = 0.0, b = 0.0;
                const int nmatch = sscanf(str, "%lf>%lf", &a, &b);

                if (nmatch == 2)
                {
                    rangeMinimum = wdl_min(a,b);
                    rangeMaximum = wdl_max(a,b);
                }
                else if (WDL_NORMALLY(nmatch == 1))
                {
                    steppedValues.push_back(a);
                }
            }
        }
    }
}

static double EnumSteppedValues(int numSteps, int stepNumber)
{
    return floor(stepNumber / (double)(numSteps - 1)  *100.0 + 0.5)  *0.01;
}

void GetParamStepsString(string &outputString, int numSteps)
{
    ostringstream stepStr;
    
    for (int i = 0; i < numSteps; i++)
    {
        stepStr << std::setprecision(2) << EnumSteppedValues(numSteps, i);
        stepStr <<  "  ";
    }

    outputString = stepStr.str();
}

void GetParamStepsValues(vector<double> &outputVector, int numSteps)
{
    outputVector.clear();

    for (int i = 0; i < numSteps; i++)
        outputVector.push_back(EnumSteppedValues(numSteps, i));
}

void TrimLine(string &line)
{
    const string tmp = line;
    const char *p = tmp.c_str();

    // remove leading and trailing spaces
    // condense whitespace to single whitespace
    // stop copying at "//" (comment)
    line.clear();
    for (;;)
    {
        // advance over whitespace
        while (*p > 0 && isspace(*p))
            p++;

        // a single / at the beginning of a line indicates a comment
        if (!*p || p[0] == '/') return;

        if (line.length())
            line.append(" ",1);

        // copy non-whitespace to output
        while (*p && (*p < 0 || !isspace(*p)))
        {
           if (p[0] == '/' && p[1] == '/') return; // existing behavior, maybe not ideal, but a comment can start anywhere
           line.append(p++,1);
        }
    }
}

string ReplaceAllWith(const string &line, string replaceWith, string replacement)
{
    // replace all occurences of
    // any char in replaceWith
    // with replacement string

    string regExStr = "[" + replaceWith + "]";
    
    return regex_replace(line, regex(regExStr), replacement);
    
    
    /*
    const string tmp = line;
    const char *p = tmp.c_str();

    line.clear();
    for (;;)
    {
        // advance over whitespace
        while (*p > 0 && isspace(*p))
            p++;

        // a single / at the beginning of a line indicates a comment
        if (!*p || p[0] == '/') return;

        if (line.length())
            line.append(" ",1);

        // copy non-whitespace to output
        while (*p && (*p < 0 || !isspace(*p)))
        {
           if (p[0] == '/' && p[1] == '/') return; // existing behavior, maybe not ideal, but a comment can start anywhere
           line.append(p++,1);
        }
    }
    */
}

void GetTokens(vector<string> &tokens, const string &line)
{
    const char *rd = line.c_str();
    string token;
    for (;;)
    {
        while (*rd > 0 && isspace(*rd)) rd++;
        if (!*rd) break;

        if (*rd == '\"')
        {
            token.clear();
            rd++;
            while (*rd)
            {
                if (*rd == '\"')
                {
                    rd++;
                    break;
                }
                if (*rd == '\\' && (rd[1] == '\\' || rd[1] == '\"')) // if \\ or \", passthrough second character
                    rd++;
                token.append(rd,1);
                rd++;
            }
        }
        else
        {
            const char *sp = rd;
            while (*rd && (*rd<0 || !isspace(*rd))) rd++;
            if (rd > sp)
                token.assign(sp, (int)(rd-sp));
            else
                token.clear();
        }
        tokens.push_back(token);
    }
}

int strToHex(const string &valueStr)
{
    return strtol(valueStr.c_str(), nullptr, 16);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct MidiPort
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    int port, refcnt;
    void *dev;
    
    MidiPort(int portidx, void *devptr) : port(portidx), refcnt(1), dev(devptr) { };
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi I/O Manager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static WDL_TypedBuf<MidiPort> s_midiInputs, s_midiOutputs;

void ReleaseMidiInput(midi_Input *input)
{
    for (int i = 0; i < s_midiInputs.GetSize(); ++i)
        if (s_midiInputs.Get()[i].dev == (void*)input)
        {
            if (!--s_midiInputs.Get()[i].refcnt)
            {
                input->stop();
                delete input;
                s_midiInputs.Delete(i);
                break;
            }
        }
}

void ReleaseMidiOutput(midi_Output *output)
{
    for (int i = 0; i < s_midiOutputs.GetSize(); ++i)
        if (s_midiOutputs.Get()[i].dev == (void*)output)
        {
            if (!--s_midiOutputs.Get()[i].refcnt)
            {
                delete output;
                s_midiOutputs.Delete(i);
                break;
            }
        }
}

static midi_Input *GetMidiInputForPort(int inputPort)
{
    for (int i = 0; i < s_midiInputs.GetSize(); ++i)
        if (s_midiInputs.Get()[i].port == inputPort)
        {
            s_midiInputs.Get()[i].refcnt++;
            return (midi_Input *)s_midiInputs.Get()[i].dev;
        }
    
    midi_Input *newInput = DAW::CreateMIDIInput(inputPort);
    
    if (newInput)
    {
        newInput->start();
        MidiPort midiInputPort(inputPort, newInput);
        s_midiInputs.Add(midiInputPort);
    }
    
    return newInput;
}

static midi_Output *GetMidiOutputForPort(int outputPort)
{
    for (int i = 0; i < s_midiOutputs.GetSize(); ++i)
        if (s_midiOutputs.Get()[i].port == outputPort)
        {
            s_midiOutputs.Get()[i].refcnt++;
            return (midi_Output *)s_midiOutputs.Get()[i].dev;
        }
    
    midi_Output *newOutput = DAW::CreateMIDIOutput(outputPort, false, NULL);
    
    if (newOutput)
    {
        MidiPort midiOutputPort(outputPort, newOutput);
        s_midiOutputs.Add(midiOutputPort);
    }
    
    return newOutput;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct OSCSurfaceSocket
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    string surfaceName;
    oscpkt::UdpSocket *socket;
    
    OSCSurfaceSocket()
    {
        surfaceName = "";
        socket = nullptr;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OSC I/O Manager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static WDL_TypedBuf<OSCSurfaceSocket> s_inputSockets;
static WDL_TypedBuf<OSCSurfaceSocket> s_outputSockets;

static oscpkt::UdpSocket *GetInputSocketForPort(string surfaceName, int inputPort)
{
    for (int i = 0; i < s_inputSockets.GetSize(); ++i)
        if (s_inputSockets.Get()[i].surfaceName == surfaceName)
            return s_inputSockets.Get()[i].socket; // return existing
    
    // otherwise make new
    oscpkt::UdpSocket *newInputSocket = new oscpkt::UdpSocket();
    
    if (newInputSocket)
    {
        newInputSocket->bindTo(inputPort);
        
        if (! newInputSocket->isOk())
        {
            //cerr << "Error opening port " << PORT_NUM << ": " << inSocket_.errorMessage() << "\n";
            return nullptr;
        }
        
        OSCSurfaceSocket surfaceSocket;
        surfaceSocket.surfaceName = surfaceName;
        surfaceSocket.socket = newInputSocket;
        if (s_inputSockets.Add(surfaceSocket))
            return newInputSocket;
        else
            return nullptr;
    }
    
    return nullptr;
}

static oscpkt::UdpSocket *GetOutputSocketForAddressAndPort(const string &surfaceName, const string &address, int outputPort)
{
    for (int i = 0; i < s_outputSockets.GetSize(); ++i)
        if (s_outputSockets.Get()[i].surfaceName == surfaceName)
            return s_outputSockets.Get()[i].socket; // return existing
    
    // otherwise make new
    oscpkt::UdpSocket *newOutputSocket = new oscpkt::UdpSocket();
    
    if (newOutputSocket)
    {
        if ( ! newOutputSocket->connectTo(address, outputPort))
        {
            //cerr << "Error connecting " << remoteDeviceIP_ << ": " << outSocket_.errorMessage() << "\n";
            return nullptr;
        }
        
        if ( ! newOutputSocket->isOk())
        {
            //cerr << "Error opening port " << outPort_ << ": " << outSocket_.errorMessage() << "\n";
            return nullptr;
        }

        OSCSurfaceSocket surfaceSocket;
        surfaceSocket.surfaceName = surfaceName;
        surfaceSocket.socket = newOutputSocket;
        if (s_outputSockets.Add(surfaceSocket))
            return newOutputSocket;
        else
            return nullptr;
    }
    
    return nullptr;
}

void ShutdownOSCIO()
{
    for (int i = 0; i < s_inputSockets.GetSize(); ++i)
        delete s_inputSockets.Get()[i].socket;
    
    s_inputSockets.Resize(0);
    
    for (int i = 0; i < s_outputSockets.GetSize(); ++i)
        delete s_outputSockets.Get()[i].socket;
    
    s_outputSockets.Resize(0);
}

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
// Parsing
//////////////////////////////////////////////////////////////////////////////////////////////
static void listFilesOfType(const string &path, vector<string> &results, const char *type)
{
    WDL_PtrList<char> stack;
    WDL_FastString tmp;
    stack.Add(strdup(path.c_str()));
    
    while (stack.GetSize()>0)
    {
        const char *curpath = stack.Get(0);
        WDL_DirScan ds;
        if (!ds.First(curpath))
        {
            do
            {
                if (ds.GetCurrentFN()[0] == '.')
                {
                  // ignore dotfiles and ./..
                }
                else if (ds.GetCurrentIsDirectory())
                {
                    ds.GetCurrentFullFN(&tmp);
                    stack.Add(strdup(tmp.Get()));
                }
                else if (!stricmp(type,WDL_get_fileext(ds.GetCurrentFN())))
                {
                    ds.GetCurrentFullFN(&tmp);
                    results.push_back(string(tmp.Get()));
                }
            } while (!ds.Next());
        }
        stack.Delete(0,true,free);
    }
}

static void PreProcessZoneFile(const string &filePath, ZoneManager *zoneManager)
{
    string zoneName = "";
    
    try
    {
        ifstream file(filePath);
        
        CSIZoneInfo *info = new CSIZoneInfo();
        info->filePath = filePath;
                 
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            if (line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
            
            vector<string> tokens;
            GetTokens(tokens, line);

            if (tokens[0] == "Zone" && tokens.size() > 1)
            {
                zoneName = tokens[1];
                info->alias = tokens.size() > 2 ? tokens[2] : zoneName;
                zoneManager->AddZoneFilePath(zoneName, info);
            }

            break;
        }
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath.c_str(), 1);
        DAW::ShowConsoleMsg(buffer);
    }
}

void ZoneManager::GetWidgetNameAndModifiers(const string &line, ActionTemplate *actionTemplate)
{
    istringstream modifiersAndWidgetName(line);
    vector<string> tokens;
    string token;
    
    ModifierManager modifierManager(NULL);
    
    while (getline(modifiersAndWidgetName, token, '+'))
        tokens.push_back(token);
    
    actionTemplate->widgetName = tokens[tokens.size() - 1];
       
    if (tokens.size() > 1)
    {
        for (int i = 0; i < tokens.size() - 1; i++)
        {
            if (tokens[i].find("Touch") != string::npos)
                actionTemplate->modifier += 1;
            else if (tokens[i] == "Toggle")
                actionTemplate->modifier += 2;
                        
            else if (tokens[i] == "Invert")
                actionTemplate->isValueInverted = true;
            else if (tokens[i] == "InvertFB")
                actionTemplate->isFeedbackInverted = true;
            else if (tokens[i] == "Hold")
                actionTemplate->holdDelayAmount = 1.0;
            else if (tokens[i] == "Decrease")
                actionTemplate->isDecrease = true;
            else if (tokens[i] == "Increase")
                actionTemplate->isIncrease = true;
        }
    }
    
    actionTemplate->modifier += modifierManager.GetModifierValue(tokens);
}

void ZoneManager::BuildActionTemplate(const vector<string> &tokens)
{
    string feedbackIndicator = "";
    
    vector<string> params;
    for (int i = 1; i < tokens.size(); i++)
    {
        if (tokens[i] == "Feedback=Yes" || tokens[i] == "Feedback=No")
            feedbackIndicator = tokens[i];
        else
            params.push_back(tokens[i]);
    }

    ActionTemplate *currentActionTemplate = new ActionTemplate();
    
    currentActionTemplate->actionName = tokens[1];
    currentActionTemplate->params = params;
    
    GetWidgetNameAndModifiers(tokens[0], currentActionTemplate);

    WDL_IntKeyedArray<WDL_PtrList<ActionTemplate>* > *wr = actionTemplatesDictionary_.Get(currentActionTemplate->widgetName.c_str());
    if (!wr)
    {
        wr = new WDL_IntKeyedArray<WDL_PtrList<ActionTemplate>* >(disposeActionTemplateList);
        actionTemplatesDictionary_.Insert(currentActionTemplate->widgetName.c_str(), wr);
    }

    WDL_PtrList<ActionTemplate> *ml = wr->Get(currentActionTemplate->modifier);
    if (!ml)
    {
        ml = new WDL_PtrList<ActionTemplate>;
        wr->Insert(currentActionTemplate->modifier, ml);
    }

    ml->Add(currentActionTemplate);
    
    if (ml->GetSize() == 1)
    {
        if (feedbackIndicator == "" || feedbackIndicator == "Feedback=Yes")
            currentActionTemplate->provideFeedback = true;
    }
    else if (feedbackIndicator == "Feedback=Yes")
    {
        for (int i = 0; i < ml->GetSize(); ++i)
            ml->Get(i)->provideFeedback =  (i == ml->GetSize() - 1);
    }
}

void ZoneManager::ProcessSurfaceFXLayout(const string &filePath, vector<vector<string> > &surfaceFXLayout,  vector<vector<string> > &surfaceFXLayoutTemplate)
{
    try
    {
        ifstream file(filePath);
        
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            if (line == "") // ignore blank lines
                continue;
        
            vector<string> tokens;
            GetTokens(tokens, line);
            
            if (tokens[0] != "Zone" && tokens[0] != "ZoneEnd")
            {
                if (tokens[0][0] == '#')
                {
                    tokens[0] = tokens[0].substr(1, tokens[0].length() - 1);
                    surfaceFXLayoutTemplate.push_back(tokens);
                }
                else
                {
                    surfaceFXLayout.push_back(tokens);
                    
                    if (tokens.size() > 1 && tokens[1] == "FXParam")
                    {
                        vector<string> widgetAction;
                        
                        widgetAction.push_back("WidgetAction");
                        widgetAction.push_back(tokens[1]);

                        surfaceFXLayoutTemplate.push_back(widgetAction);
                    }
                    if (tokens.size() > 1 && tokens[1] == "FixedTextDisplay")
                    {
                        vector<string> widgetAction;
                        
                        widgetAction.push_back("AliasDisplayAction");
                        widgetAction.push_back(tokens[1]);

                        surfaceFXLayoutTemplate.push_back(widgetAction);
                    }
                    if (tokens.size() > 1 && tokens[1] == "FXParamValueDisplay")
                    {
                        vector<string> widgetAction;
                        
                        widgetAction.push_back("ValueDisplayAction");
                        widgetAction.push_back(tokens[1]);

                        surfaceFXLayoutTemplate.push_back(widgetAction);
                    }
                }
            }
        }
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath.c_str(), 1);
        DAW::ShowConsoleMsg(buffer);
    }
}

void ZoneManager::ProcessFXLayouts(const string &filePath, vector<CSILayoutInfo> &fxLayouts)
{
    try
    {
        ifstream file(filePath);
        
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            if (line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
        
            if (line.find("Zone") == string::npos)
            {
                vector<string> tokens;
                GetTokens(tokens, line);

                CSILayoutInfo info;

                if (tokens.size() == 3)
                {
                    info.modifiers_ = tokens[0];
                    info.suffix_ = tokens[1];
                    info.channelCount_ = atoi(tokens[2].c_str());
                }

                fxLayouts.push_back(info);
            }
        }
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath.c_str(), 1);
        DAW::ShowConsoleMsg(buffer);
    }
}

void ZoneManager::ProcessFXBoilerplate(const string &filePath, vector<string> &fxBoilerplate)
{
    try
    {
        ifstream file(filePath);
            
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            if (line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
        
            if (line.find("Zone") != 0)
                fxBoilerplate.push_back(line);
        }
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath.c_str(), 1);
        DAW::ShowConsoleMsg(buffer);
    }
}

void Zone::GCTagZone(Zone *zone)
{
    if (!zone || zone->gcState_) return;
    zone->gcState_ = true;

    for (int j = 0; j < zone->associatedZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = zone->associatedZones_.Enumerate(j);
        if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
            GCTagZone(zones->Get(i));
    }

    for (int j = 0; j < zone->subZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = zone->subZones_.Enumerate(j);
        if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
            GCTagZone(zones->Get(i));
    }

    for (int i = 0; i < zone->includedZones_.GetSize(); ++i)
        GCTagZone(zone->includedZones_.Get(i));
}

void ZoneManager::GarbageCollectZones()
{
    if (needGarbageCollect_ == false) return;
    needGarbageCollect_ = false;

    for (int x = 0; x < allZonesNeedFree_.GetSize(); x ++)
    {
        if (allZonesNeedFree_.Get(x)->zoneManager_ != this)
        {
            WDL_ASSERT(false); // better leak than to crash
            allZonesNeedFree_.Delete(x--);
        }
        else
        {
            allZonesNeedFree_.Get(x)->gcState_ = false;
        }
    }

    Zone::GCTagZone(noMapZone_);
    Zone::GCTagZone(homeZone_);
    Zone::GCTagZone(fxLayout_);
    Zone::GCTagZone(focusedFXParamZone_);

    for (int x = 0; x < focusedFXZones_.GetSize(); x ++)
        Zone::GCTagZone(focusedFXZones_.Get(x));
    for (int x = 0; x < selectedTrackFXZones_.GetSize(); x ++)
        Zone::GCTagZone(selectedTrackFXZones_.Get(x));
    for (int x = 0; x < fxSlotZones_.GetSize(); x ++)
        Zone::GCTagZone(fxSlotZones_.Get(x));

    for (int x = allZonesNeedFree_.GetSize()-1; x>=0; x --)
    {
        if (allZonesNeedFree_.Get(x)->zoneManager_ != this)
        {
            WDL_ASSERT(false); // better leak than to crash
            allZonesNeedFree_.Delete(x);
        }
        else if (allZonesNeedFree_.Get(x)->gcState_ == false)
        {
            allZonesNeedFree_.Delete(x,true);
        }
    }
}

void ZoneManager::LoadZoneFile(const string &filePath, const WDL_PtrList<Navigator> &navigators, WDL_PtrList<Zone> &zones, Zone *enclosingZone)
{
    bool isInIncludedZonesSection = false;
    vector<string> includedZones;
    bool isInSubZonesSection = false;
    vector<string> subZones;
    bool isInAssociatedZonesSection = false;
    vector<string> associatedZones;
    
    string zoneName = "";
    string zoneAlias = "";
    int lineNumber = 0;
   
    try
    {
        ifstream file(filePath);
        
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            lineNumber++;
            
            if (line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
            
            if (line == s_BeginAutoSection || line == s_EndAutoSection)
                continue;
            
            vector<string> tokens;
            GetTokens(tokens, line);

            if (tokens.size() > 0)
            {
                if (tokens[0] == "Zone")
                {
                    zoneName = tokens.size() > 1 ? tokens[1] : "";
                    zoneAlias = tokens.size() > 2 ? tokens[2] : "";
                }
                else if (tokens[0] == "ZoneEnd" && zoneName != "")
                {
                    for (int i = 0; i < navigators.GetSize(); i++)
                    {
                        string numStr = to_string(i + 1);
                                                
                        Zone *zone;
                        
                        if (enclosingZone == nullptr)
                            zone = new Zone(csi_, this, navigators.Get(i), i, zoneName, zoneAlias, filePath, includedZones, associatedZones);
                        else
                            zone = new SubZone(csi_, this, navigators.Get(i), i, zoneName, zoneAlias, filePath, includedZones, associatedZones, enclosingZone);

                        if (zoneName == "Home")
                            SetHomeZone(zone);
                                               
                        if (zoneName == "FocusedFXParam")
                            SetFocusedFXParamZone(zone);
                        
                        zones.Add(zone);
                        
                        for (int tde = 0; tde < actionTemplatesDictionary_.GetSize(); tde++)
                        {
                            const char *widgetName = NULL;
                            WDL_IntKeyedArray<WDL_PtrList<ActionTemplate>* > *modifiedActionTemplates = actionTemplatesDictionary_.Enumerate(tde,&widgetName);
                            if (WDL_NOT_NORMALLY(modifiedActionTemplates == NULL || widgetName == NULL)) continue;

                            string surfaceWidgetName = widgetName;
                            
                            if (navigators.GetSize() > 1)
                                surfaceWidgetName = ReplaceAllWith(surfaceWidgetName, "|", to_string(i + 1));
                            
                            if (enclosingZone != nullptr && enclosingZone->GetChannelNumber() != 0)
                                surfaceWidgetName = ReplaceAllWith(surfaceWidgetName, "|", to_string(enclosingZone->GetChannelNumber()));
                            
                            Widget *widget = GetSurface()->GetWidgetByName(surfaceWidgetName);
                                                        
                            if (widget == NULL)
                                continue;
 
                            zone->AddWidget(widget, widget->GetName());
                                                        
                            for (int ti = 0; ti < modifiedActionTemplates->GetSize(); ti ++)
                            {
                                int modifier=0;
                                WDL_PtrList<ActionTemplate> *actionTemplates = modifiedActionTemplates->Enumerate(ti,&modifier);
                                if (WDL_NOT_NORMALLY(actionTemplates == NULL)) continue;
                                for (int j = 0; j < actionTemplates->GetSize(); ++j)
                                {
                                    string actionName = ReplaceAllWith(actionTemplates->Get(j)->actionName, "|", numStr);

                                    vector<string> memberParams;
                                    for (int k = 0; k < actionTemplates->Get(j)->params.size(); k++)
                                        memberParams.push_back(ReplaceAllWith(actionTemplates->Get(j)->params[k], "|", numStr));
                                    
                                    ActionContext *context = csi_->GetActionContext(actionName, widget, zone, memberParams);
                                        
                                    context->SetProvideFeedback(actionTemplates->Get(j)->provideFeedback);
                                    
                                    if (actionTemplates->Get(j)->isValueInverted)
                                        context->SetIsValueInverted();
                                    
                                    if (actionTemplates->Get(j)->isFeedbackInverted)
                                        context->SetIsFeedbackInverted();
                                    
                                    if (actionTemplates->Get(j)->holdDelayAmount != 0.0)
                                        context->SetHoldDelayAmount(actionTemplates->Get(j)->holdDelayAmount);
                                    
                                    vector<double> range;
                                    
                                    if (actionTemplates->Get(j)->isDecrease)
                                    {
                                        range.push_back(-2.0);
                                        range.push_back(1.0);
                                        context->SetRange(range);
                                    }
                                    else if (actionTemplates->Get(j)->isIncrease)
                                    {
                                        range.push_back(0.0);
                                        range.push_back(2.0);
                                        context->SetRange(range);
                                    }
                                   
                                    zone->AddActionContext(widget, modifier, context);
                                }
                            }
                        }
                    
                        if (enclosingZone == nullptr && subZones.size() > 0)
                            zone->InitSubZones(subZones, zone);
                        allZonesNeedFree_.Add(zone);
                    }
                                    
                    includedZones.clear();
                    subZones.clear();
                    associatedZones.clear();
                    actionTemplatesDictionary_.DeleteAll();
                    
                    break;
                }
                                
                else if (tokens[0] == "IncludedZones")
                    isInIncludedZonesSection = true;
                
                else if (tokens[0] == "IncludedZonesEnd")
                    isInIncludedZonesSection = false;
                
                else if (isInIncludedZonesSection)
                    includedZones.push_back(tokens[0]);

                else if (tokens[0] == "SubZones")
                    isInSubZonesSection = true;
                
                else if (tokens[0] == "SubZonesEnd")
                    isInSubZonesSection = false;
                
                else if (isInSubZonesSection)
                    subZones.push_back(tokens[0]);
                 
                else if (tokens[0] == "AssociatedZones")
                    isInAssociatedZonesSection = true;
                
                else if (tokens[0] == "AssociatedZonesEnd")
                    isInAssociatedZonesSection = false;
                
                else if (isInAssociatedZonesSection)
                    associatedZones.push_back(tokens[0]);
                               
                else if (tokens.size() > 1)
                    BuildActionTemplate(tokens);
            }
        }
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath.c_str(), lineNumber);
        DAW::ShowConsoleMsg(buffer);
    }
}

void ActionContext::GetColorValues(vector<rgba_color> &colorValues, const vector<string> &colors)
{
    for (int i = 0; i < (int)colors.size(); ++i)
    {
        rgba_color colorValue;
        
        if(GetColorValue(colors[i], colorValue))
            colorValues.push_back(colorValue);
    }
}

void ActionContext::SetColor(vector<string> &params, bool &supportsColor, bool &supportsTrackColor, vector<rgba_color> &colorValues)
{
    vector<int> rawValues;
    vector<string> hexColors;
    
    vector<string>::iterator openCurlyBrace = find(params.begin(), params.end(), "{");
    vector<string>::iterator closeCurlyBrace = find(params.begin(), params.end(), "}");
    
    if (openCurlyBrace != params.end() && closeCurlyBrace != params.end())
    {
        for (vector<string>::iterator it = openCurlyBrace + 1; it != closeCurlyBrace; ++it)
        {
            string strVal = *(it);
            
            if (strVal.length() > 0 && strVal[0] == '#')
            {
                hexColors.push_back(strVal);
                continue;
            }
            
            if (strVal == "Track")
            {
                supportsTrackColor = true;
                break;
            }
            else
            {
                if (regex_match(strVal, regex("[0-9]+")))
                {
                    int value = stoi(strVal);
                    value = value < 0 ? 0 : value;
                    value = value > 255 ? 255 : value;
                    
                    rawValues.push_back(value);
                }
            }
        }
        
        if (hexColors.size() > 0)
        {
            supportsColor = true;

            GetColorValues(colorValues, hexColors);
        }
        else if (rawValues.size() % 3 == 0 && rawValues.size() > 2)
        {
            supportsColor = true;
            
            for (int i = 0; i < rawValues.size(); i += 3)
            {
                rgba_color color;
                
                color.r = rawValues[i];
                color.g = rawValues[i + 1];
                color.b = rawValues[i + 2];
                
                colorValues.push_back(color);
            }
        }
    }
}

void ActionContext::GetSteppedValues(Widget *widget, Action *action,  Zone *zone, int paramNumber, vector<string> &params, const PropertyList &widgetProperties, double &deltaValue, vector<double> &acceleratedDeltaValues, double &rangeMinimum, double &rangeMaximum, vector<double> &steppedValues, vector<int> &acceleratedTickValues)
{
    ::GetSteppedValues(params, deltaValue, acceleratedDeltaValues, rangeMinimum, rangeMaximum, steppedValues, acceleratedTickValues);
    
    if (deltaValue == 0.0 && widget->GetStepSize() != 0.0)
        deltaValue = widget->GetStepSize();
    
    if (acceleratedDeltaValues.size() == 0 && widget->GetAccelerationValues().size() != 0)
        acceleratedDeltaValues = widget->GetAccelerationValues();
         
    if (steppedValues.size() > 0 && acceleratedTickValues.size() == 0)
    {
        double stepSize = deltaValue;
        
        if (stepSize != 0.0)
        {
            stepSize *= 10000.0;
            int baseTickCount = csi_->GetBaseTickCount((int)steppedValues.size());
            int tickCount = int(baseTickCount / stepSize + 0.5);
            acceleratedTickValues.push_back(tickCount);
        }
    }
}

//////////////////////////////////////////////////////////////////////////////
// Widgets
//////////////////////////////////////////////////////////////////////////////
void Midi_ControlSurface::ProcessMidiWidget(int &lineNumber, ifstream &surfaceTemplateFile, const vector<string> &in_tokens)
{
    if (in_tokens.size() < 2)
        return;
    
    string widgetName = in_tokens[1];
    
    string widgetClass = "";
    
    if (in_tokens.size() > 2)
        widgetClass = in_tokens[2];

    Widget *widget = new Widget(csi_, this, widgetName);
       
    AddWidget(widget);

    vector<vector<string> > tokenLines;
    
    for (string line; getline(surfaceTemplateFile, line) ; )
    {
        TrimLine(line);
        
        lineNumber++;
        
        if (line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
            continue;
        
        vector<string> tokens;
        GetTokens(tokens, line);

        if (tokens[0] == "WidgetEnd")    // finito baybay - Widget list complete
            break;
        
        tokenLines.push_back(tokens);
    }
    
    if (tokenLines.size() < 1)
        return;
    
    for (int i = 0; i < tokenLines.size(); i++)
    {
        int size = (int)tokenLines[i].size();
        
        string widgetType = tokenLines[i][0];

        MIDI_event_ex_t *message1 = nullptr;
        MIDI_event_ex_t *message2 = nullptr;

        int oneByteKey = 0;
        int twoByteKey = 0;
        int threeByteKey = 0;
        int threeByteKeyMsg2 = 0;
        
        if (size > 3)
        {
            message1 = new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3]));
            
            if (message1)
            {
                oneByteKey = message1->midi_message[0] * 0x10000;
                twoByteKey = message1->midi_message[0] * 0x10000 + message1->midi_message[1] * 0x100;
                threeByteKey = message1->midi_message[0] * 0x10000 + message1->midi_message[1] * 0x100 + message1->midi_message[2];
            }
        }
        if (size > 6)
        {
            message2 = new MIDI_event_ex_t(strToHex(tokenLines[i][4]), strToHex(tokenLines[i][5]), strToHex(tokenLines[i][6]));
            
            if (message2)
                threeByteKeyMsg2 = message2->midi_message[0] * 0x10000 + message2->midi_message[1] * 0x100 + message2->midi_message[2];
        }
        // Control Signal Generators
        
        if (widgetType == "AnyPress" && (size == 4 || size == 7) && message1)
            AddCSIMessageGenerator(twoByteKey, new AnyPress_Midi_CSIMessageGenerator(csi_, widget, message1));
        else if (widgetType == "Press" && size == 4 && message1)
            AddCSIMessageGenerator(threeByteKey, new PressRelease_Midi_CSIMessageGenerator(csi_, widget, message1));
        else if (widgetType == "Press" && size == 7 && message1 && message2)
        {
            AddCSIMessageGenerator(threeByteKey, new PressRelease_Midi_CSIMessageGenerator(csi_, widget, message1, message2));
            AddCSIMessageGenerator(threeByteKeyMsg2, new PressRelease_Midi_CSIMessageGenerator(csi_, widget, message1, message2));
        }
        else if (widgetType == "Fader14Bit" && size == 4 && message1)
            AddCSIMessageGenerator(oneByteKey, new Fader14Bit_Midi_CSIMessageGenerator(csi_, widget, message1));
        else if (widgetType == "FaderportClassicFader14Bit" && size == 7 && message1 && message2)
            AddCSIMessageGenerator(oneByteKey, new FaderportClassicFader14Bit_Midi_CSIMessageGenerator(csi_, widget, message1, message2));
        else if (widgetType == "Fader7Bit" && size== 4 && message1)
            AddCSIMessageGenerator(twoByteKey, new Fader7Bit_Midi_CSIMessageGenerator(csi_, widget, message1));
        else if (widgetType == "Encoder" && size == 4 && widgetClass == "RotaryWidgetClass"  && message1)
        {
            if (stepSize_.Exists(widgetClass.c_str()) && accelerationValuesForDecrement_.Exists(widgetClass.c_str()) && accelerationValuesForIncrement_.Exists(widgetClass.c_str()) && accelerationValues_.Exists(widgetClass.c_str()))
                AddCSIMessageGenerator(twoByteKey, new AcceleratedPreconfiguredEncoder_Midi_CSIMessageGenerator(csi_, widget, message1, stepSize_.Get(widgetClass.c_str()), accelerationValuesForDecrement_.Get(widgetClass.c_str()), accelerationValuesForIncrement_.Get(widgetClass.c_str()), accelerationValues_.Get(widgetClass.c_str())));
        }
        else if (widgetType == "Encoder" && size == 4 && message1)
            AddCSIMessageGenerator(twoByteKey, new Encoder_Midi_CSIMessageGenerator(csi_, widget, message1));
        else if (widgetType == "Encoder" && size > 4 && message1)
            AddCSIMessageGenerator(twoByteKey, new AcceleratedEncoder_Midi_CSIMessageGenerator(csi_, widget, message1, tokenLines[i]));
        else if (widgetType == "MFTEncoder" && size > 4 && message1)
            AddCSIMessageGenerator(twoByteKey, new MFT_AcceleratedEncoder_Midi_CSIMessageGenerator(csi_, widget, message1, tokenLines[i]));
        else if (widgetType == "EncoderPlain" && size == 4 && message1)
            AddCSIMessageGenerator(twoByteKey, new EncoderPlain_Midi_CSIMessageGenerator(csi_, widget, message1));
        else if (widgetType == "Encoder7Bit" && size == 4 && message1)
            AddCSIMessageGenerator(twoByteKey, new Encoder7Bit_Midi_CSIMessageGenerator(csi_, widget, message1));
        else if (widgetType == "Touch" && size == 7 && message1  && message2)
        {
            AddCSIMessageGenerator(threeByteKey, new Touch_Midi_CSIMessageGenerator(csi_, widget, message1, message2));
            AddCSIMessageGenerator(threeByteKeyMsg2, new Touch_Midi_CSIMessageGenerator(csi_, widget, message1, message2));
        }
        
        // Feedback Processors
        FeedbackProcessor *feedbackProcessor = NULL;

        if (widgetType == "FB_TwoState" && size == 7 && message1 && message2)
        {
            feedbackProcessor = new TwoState_Midi_FeedbackProcessor(csi_, this, widget, message1, message2);
        }
        else if (widgetType == "FB_NovationLaunchpadMiniRGB7Bit" && size == 4 && message1)
        {
            feedbackProcessor = new NovationLaunchpadMiniRGB7Bit_Midi_FeedbackProcessor(csi_, this, widget, message1);
        }
        else if (widgetType == "FB_MFT_RGB" && size == 4 && message1)
        {
            feedbackProcessor = new MFT_RGB_Midi_FeedbackProcessor(csi_, this, widget, message1);
        }
        else if (widgetType == "FB_AsparionRGB" && size == 4 && message1)
        {
            feedbackProcessor = new AsparionRGB_Midi_FeedbackProcessor(csi_, this, widget, message1);
            
            if (feedbackProcessor)
                AddTrackColorFeedbackProcessor(feedbackProcessor);
        }
        else if (widgetType == "FB_FaderportRGB" && size == 4 && message1)
        {
            feedbackProcessor = new FaderportRGB_Midi_FeedbackProcessor(csi_, this, widget, message1);
        }
        else if (widgetType == "FB_FaderportTwoStateRGB" && size == 4 && message1)
        {
            feedbackProcessor = new FPTwoStateRGB_Midi_FeedbackProcessor(csi_, this, widget, message1);
        }
        else if (widgetType == "FB_FaderportValueBar"  && size == 2)
        {
            feedbackProcessor = new FPValueBar_Midi_FeedbackProcessor(csi_, this, widget, stoi(tokenLines[i][1]));
        }
        else if ((widgetType == "FB_FPVUMeter") && size == 2)
        {
            feedbackProcessor = new FPVUMeter_Midi_FeedbackProcessor(csi_, this, widget, stoi(tokenLines[i][1]));
        }
        else if (widgetType == "FB_Fader14Bit" && size == 4 && message1)
        {
            feedbackProcessor = new Fader14Bit_Midi_FeedbackProcessor(csi_, this, widget, message1);
        }
        else if (widgetType == "FB_FaderportClassicFader14Bit" && size == 7 && message1 && message2)
        {
            feedbackProcessor = new FaderportClassicFader14Bit_Midi_FeedbackProcessor(csi_, this, widget, message1, message2);
        }
        else if (widgetType == "FB_Fader7Bit" && size == 4 && message1)
        {
            feedbackProcessor = new Fader7Bit_Midi_FeedbackProcessor(csi_, this, widget, message1);
        }
        else if (widgetType == "FB_Encoder" && size == 4 && message1)
        {
            feedbackProcessor = new Encoder_Midi_FeedbackProcessor(csi_, this, widget, message1);
        }
        else if (widgetType == "FB_AsparionEncoder" && size == 4 && message1)
        {
            feedbackProcessor = new AsparionEncoder_Midi_FeedbackProcessor(csi_, this, widget, message1);
        }
        else if (widgetType == "FB_ConsoleOneVUMeter" && size == 4 && message1)
        {
            feedbackProcessor = new ConsoleOneVUMeter_Midi_FeedbackProcessor(csi_, this, widget, message1);
        }
        else if (widgetType == "FB_ConsoleOneGainReductionMeter" && size == 4 && message1)
        {
            feedbackProcessor = new ConsoleOneGainReductionMeter_Midi_FeedbackProcessor(csi_, this, widget, message1);
        }
        else if (widgetType == "FB_MCUTimeDisplay" && size == 1)
        {
            feedbackProcessor = new MCU_TimeDisplay_Midi_FeedbackProcessor(csi_, this, widget);
        }
        else if (widgetType == "FB_MCUAssignmentDisplay" && size == 1)
        {
            feedbackProcessor = new FB_MCU_AssignmentDisplay_Midi_FeedbackProcessor(csi_, this, widget);
        }
        else if (widgetType == "FB_QConProXMasterVUMeter" && size == 2)
        {
            feedbackProcessor = new QConProXMasterVUMeter_Midi_FeedbackProcessor(csi_, this, widget, stoi(tokenLines[i][1]));
        }
        else if ((widgetType == "FB_MCUVUMeter" || widgetType == "FB_MCUXTVUMeter") && size == 2)
        {
            int displayType = widgetType == "FB_MCUVUMeter" ? 0x14 : 0x15;
            
            feedbackProcessor = new MCUVUMeter_Midi_FeedbackProcessor(csi_, this, widget, displayType, stoi(tokenLines[i][1]));
            
            SetHasMCUMeters(displayType);
        }
        else if ((widgetType == "FB_AsparionVUMeterL" || widgetType == "FB_AsparionVUMeterR") && size == 2)
        {
            bool isRight = widgetType == "FB_AsparionVUMeterR" ? true : false;
            
            feedbackProcessor = new AsparionVUMeter_Midi_FeedbackProcessor(csi_, this, widget, 0x14, stoi(tokenLines[i][1]), isRight);
            
            SetHasMCUMeters(0x14);
        }
        else if (widgetType == "FB_SCE24LEDButton" && size == 4)
        {
            feedbackProcessor = new SCE24TwoStateLED_Midi_FeedbackProcessor(csi_, this, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]) + 0x60, strToHex(tokenLines[i][3])));
        }
        else if (widgetType == "FB_SCE24OLEDButton" && size == 4)
        {
            feedbackProcessor = new SCE24OLED_Midi_FeedbackProcessor(csi_, this, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]) + 0x60, strToHex(tokenLines[i][3])));
        }
        else if (widgetType == "FB_SCE24Encoder" && size == 4 && message1)
        {
            feedbackProcessor = new SCE24Encoder_Midi_FeedbackProcessor(csi_, this, widget, message1);
        }
        else if (widgetType == "FB_SCE24EncoderText" && size == 4 && message1)
        {
            feedbackProcessor = new SCE24Text_Midi_FeedbackProcessor(csi_, this, widget, message1);
        }
        else if ((widgetType == "FB_MCUDisplayUpper" || widgetType == "FB_MCUDisplayLower" || widgetType == "FB_MCUXTDisplayUpper" || widgetType == "FB_MCUXTDisplayLower") && size == 2)
        {
            if (widgetType == "FB_MCUDisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if (widgetType == "FB_MCUDisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(csi_, this, widget, 1, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if (widgetType == "FB_MCUXTDisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0, 0x15, 0x12, stoi(tokenLines[i][1]));
            else if (widgetType == "FB_MCUXTDisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(csi_, this, widget, 1, 0x15, 0x12, stoi(tokenLines[i][1]));
        }
        else if ((widgetType == "FB_AsparionDisplayUpper" || widgetType == "FB_AsparionDisplayLower" || widgetType == "FB_AsparionDisplayEncoder") && size == 2)
        {
            if (widgetType == "FB_AsparionDisplayUpper")
                feedbackProcessor = new AsparionDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x01, 0x14, 0x1A, stoi(tokenLines[i][1]));
            else if (widgetType == "FB_AsparionDisplayLower")
                feedbackProcessor = new AsparionDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x02, 0x14, 0x1A, stoi(tokenLines[i][1]));
            else if (widgetType == "FB_AsparionDisplayEncoder")
                feedbackProcessor = new AsparionDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x03, 0x14, 0x19, stoi(tokenLines[i][1]));
        }
        else if ((widgetType == "FB_XTouchDisplayUpper" || widgetType == "FB_XTouchDisplayLower" || widgetType == "FB_XTouchXTDisplayUpper" || widgetType == "FB_XTouchXTDisplayLower") && size == 2)
        {
            if (widgetType == "FB_XTouchDisplayUpper")
                feedbackProcessor = new XTouchDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if (widgetType == "FB_XTouchDisplayLower")
                feedbackProcessor = new XTouchDisplay_Midi_FeedbackProcessor(csi_, this, widget, 1, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if (widgetType == "FB_XTouchXTDisplayUpper")
                feedbackProcessor = new XTouchDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0, 0x15, 0x12, stoi(tokenLines[i][1]));
            else if (widgetType == "FB_XTouchXTDisplayLower")
                feedbackProcessor = new XTouchDisplay_Midi_FeedbackProcessor(csi_, this, widget, 1, 0x15, 0x12, stoi(tokenLines[i][1]));
            
            if (feedbackProcessor)
                AddTrackColorFeedbackProcessor(feedbackProcessor);
        }
        else if ((widgetType == "FB_C4DisplayUpper" || widgetType == "FB_C4DisplayLower") && size == 3)
        {
            if (widgetType == "FB_C4DisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0, 0x17, stoi(tokenLines[i][1]) + 0x30, stoi(tokenLines[i][2]));
            else if (widgetType == "FB_C4DisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(csi_, this, widget, 1, 0x17, stoi(tokenLines[i][1]) + 0x30, stoi(tokenLines[i][2]));
        }
        else if ((widgetType == "FB_FP8ScribbleLine1" || widgetType == "FB_FP16ScribbleLine1"
                 || widgetType == "FB_FP8ScribbleLine2" || widgetType == "FB_FP16ScribbleLine2"
                 || widgetType == "FB_FP8ScribbleLine3" || widgetType == "FB_FP16ScribbleLine3"
                 || widgetType == "FB_FP8ScribbleLine4" || widgetType == "FB_FP16ScribbleLine4") && size == 2)
        {
            if (widgetType == "FB_FP8ScribbleLine1")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x02, stoi(tokenLines[i][1]), 0x00);
            else if (widgetType == "FB_FP8ScribbleLine2")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x02, stoi(tokenLines[i][1]), 0x01);
            else if (widgetType == "FB_FP8ScribbleLine3")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x02, stoi(tokenLines[i][1]), 0x02);
            else if (widgetType == "FB_FP8ScribbleLine4")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x02, stoi(tokenLines[i][1]), 0x03);

            else if (widgetType == "FB_FP16ScribbleLine1")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x16, stoi(tokenLines[i][1]), 0x00);
            else if (widgetType == "FB_FP16ScribbleLine2")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x16, stoi(tokenLines[i][1]), 0x01);
            else if (widgetType == "FB_FP16ScribbleLine3")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x16, stoi(tokenLines[i][1]), 0x02);
            else if (widgetType == "FB_FP16ScribbleLine4")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x16, stoi(tokenLines[i][1]), 0x03);
        }
        else if ((widgetType == "FB_FP8ScribbleStripMode" || widgetType == "FB_FP16ScribbleStripMode") && size == 2)
        {
            if (widgetType == "FB_FP8ScribbleStripMode")
                feedbackProcessor = new FPScribbleStripMode_Midi_FeedbackProcessor(csi_, this, widget, 0x02, stoi(tokenLines[i][1]));
            else if (widgetType == "FB_FP16ScribbleStripMode")
                feedbackProcessor = new FPScribbleStripMode_Midi_FeedbackProcessor(csi_, this, widget, 0x16, stoi(tokenLines[i][1]));
        }
        else if ((widgetType == "FB_QConLiteDisplayUpper" || widgetType == "FB_QConLiteDisplayUpperMid" || widgetType == "FB_QConLiteDisplayLowerMid" || widgetType == "FB_QConLiteDisplayLower") && size == 2)
        {
            if (widgetType == "FB_QConLiteDisplayUpper")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if (widgetType == "FB_QConLiteDisplayUpperMid")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(csi_, this, widget, 1, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if (widgetType == "FB_QConLiteDisplayLowerMid")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(csi_, this, widget, 2, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if (widgetType == "FB_QConLiteDisplayLower")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(csi_, this, widget, 3, 0x14, 0x12, stoi(tokenLines[i][1]));
        }

        if (feedbackProcessor != NULL)
            widget->AddFeedbackProcessor(feedbackProcessor);
    }
}

void OSC_ControlSurface::ProcessOSCWidget(int &lineNumber, ifstream &surfaceTemplateFile, const vector<string> &in_tokens)
{
    if (in_tokens.size() < 2)
        return;
    
    Widget *widget = new Widget(csi_, this, in_tokens[1]);
    
    AddWidget(widget);

    vector<vector<string> > tokenLines;

    for (string line; getline(surfaceTemplateFile, line) ; )
    {
        TrimLine(line);
        
        lineNumber++;
        
        if (line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
            continue;
        
        vector<string> tokens;
        GetTokens(tokens, line);

        if (tokens[0] == "WidgetEnd")    // finito baybay - Widget list complete
            break;
        
        tokenLines.push_back(tokens);
    }

    for (int i = 0; i < (int)tokenLines.size(); ++i)
    {
        if (tokenLines[i].size() > 1 && tokenLines[i][0] == "Control")
            AddCSIMessageGenerator(tokenLines[i][1], new CSIMessageGenerator(csi_, widget));
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "AnyPress")
            AddCSIMessageGenerator(tokenLines[i][1], new AnyPress_CSIMessageGenerator(csi_, widget));
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "MotorizedFaderWithoutTouch")
            AddCSIMessageGenerator(tokenLines[i][1], new MotorizedFaderWithoutTouch_CSIMessageGenerator(csi_, widget));
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "Touch")
            AddCSIMessageGenerator(tokenLines[i][1], new Touch_CSIMessageGenerator(csi_, widget));
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "FB_Processor")
            widget->AddFeedbackProcessor(new OSC_FeedbackProcessor(csi_, this, widget, tokenLines[i][1]));
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "FB_IntProcessor")
            widget->AddFeedbackProcessor(new OSC_IntFeedbackProcessor(csi_, this, widget, tokenLines[i][1]));
    }
}

void ControlSurface::ProcessValues(const vector<vector<string> > &lines)
{
    bool inStepSizes = false;
    bool inAccelerationValues = false;
        
    for (int i = 0; i < (int)lines.size(); ++i)
    {
        if (lines[i].size() > 0)
        {
            if (lines[i][0] == "StepSize")
            {
                inStepSizes = true;
                continue;
            }
            else if (lines[i][0] == "StepSizeEnd")
            {
                inStepSizes = false;
                continue;
            }
            else if (lines[i][0] == "AccelerationValues")
            {
                inAccelerationValues = true;
                continue;
            }
            else if (lines[i][0] == "AccelerationValuesEnd")
            {
                inAccelerationValues = false;
                continue;
            }

            if (lines[i].size() > 1)
            {
                const char *widgetClass = lines[i][0].c_str();
                
                if (inStepSizes)
                    stepSize_.Insert(widgetClass, stod(lines[i][1]));
                else if (lines[i].size() > 2 && inAccelerationValues)
                {
                    
                    if (lines[i][1] == "Dec")
                        for (int j = 2; j < lines[i].size(); j++)
                        {
                            if ( ! accelerationValuesForDecrement_.Exists(widgetClass))
                                accelerationValuesForDecrement_.Insert(widgetClass, new WDL_IntKeyedArray<int>());
                            
                            if (accelerationValuesForDecrement_.Exists(widgetClass))
                                accelerationValuesForDecrement_.Get(widgetClass)->Insert(strtol(lines[i][j].c_str(), nullptr, 16), j - 2);
                        }
                    else if (lines[i][1] == "Inc")
                        for (int j = 2; j < lines[i].size(); j++)
                        {
                            if ( ! accelerationValuesForIncrement_.Exists(widgetClass))
                                accelerationValuesForIncrement_.Insert(widgetClass, new WDL_IntKeyedArray<int>());
                            
                            if (accelerationValuesForIncrement_.Exists(widgetClass))
                                accelerationValuesForIncrement_.Get(widgetClass)->Insert(strtol(lines[i][j].c_str(), nullptr, 16), j - 2);
                        }
                    else if (lines[i][1] == "Val")
                        for (int j = 2; j < lines[i].size(); j++)
                        {
                            if ( ! accelerationValues_.Exists(widgetClass))
                                accelerationValues_.Insert(widgetClass, new vector<double>());
                            
                            accelerationValues_.Get(widgetClass)->push_back(stod(lines[i][j]));
                        }
                }
            }
        }
    }
}

void Midi_ControlSurface::ProcessMIDIWidgetFile(const string &filePath, Midi_ControlSurface *surface)
{
    int lineNumber = 0;
    vector<vector<string> > valueLines;
    
    stepSize_.DeleteAll();
    accelerationValuesForDecrement_.DeleteAll();
    accelerationValuesForIncrement_.DeleteAll();
    accelerationValues_.DeleteAll();

    try
    {
        ifstream file(filePath);
        
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            lineNumber++;
            
            if (line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
                continue;
            
            vector<string> tokens;
            GetTokens(tokens, line);

            if (filePath[filePath.length() - 3] == 'm')
            {
                if (tokens.size() > 0 && tokens[0] != "Widget")
                    valueLines.push_back(tokens);
                
                if (tokens.size() > 0 && tokens[0] == "AccelerationValuesEnd")
                    ProcessValues(valueLines);
            }

            if (tokens.size() > 0 && (tokens[0] == "Widget"))
                ProcessMidiWidget(lineNumber, file, tokens);
        }
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath.c_str(), lineNumber);
        DAW::ShowConsoleMsg(buffer);
    }
}

void OSC_ControlSurface::ProcessOSCWidgetFile(const string &filePath)
{
    int lineNumber = 0;
    vector<vector<string> > valueLines;
    
    stepSize_.DeleteAll();
    accelerationValuesForDecrement_.DeleteAll();
    accelerationValuesForIncrement_.DeleteAll();
    accelerationValues_.DeleteAll();

    try
    {
        ifstream file(filePath);
        
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            lineNumber++;
            
            if (line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
                continue;
            
            vector<string> tokens;
            GetTokens(tokens, line);

            if (filePath[filePath.length() - 3] == 'm')
            {
                if (tokens.size() > 0 && tokens[0] != "Widget")
                    valueLines.push_back(tokens);
                
                if (tokens.size() > 0 && tokens[0] == "AccelerationValuesEnd")
                    ProcessValues(valueLines);
            }

            if (tokens.size() > 0 && (tokens[0] == "Widget"))
                ProcessOSCWidget(lineNumber, file, tokens);
        }
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath.c_str(), lineNumber);
        DAW::ShowConsoleMsg(buffer);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Manager
////////////////////////////////////////////////////////////////////////////////////////////////////////
void CSurfIntegrator::InitActionsDictionary()
{
    // actions_.Insert("DumpHex", new DumpHex());
    actions_.Insert("MetronomePrimaryVolumeDisplay", new MetronomePrimaryVolumeDisplay());
    actions_.Insert("MetronomeSecondaryVolumeDisplay", new MetronomeSecondaryVolumeDisplay());
    actions_.Insert("MetronomePrimaryVolume", new MetronomePrimaryVolume());
    actions_.Insert("MetronomeSecondaryVolume", new MetronomeSecondaryVolume());
    actions_.Insert("Speak", new SpeakOSARAMessage());
    actions_.Insert("SendMIDIMessage", new SendMIDIMessage());
    actions_.Insert("SendOSCMessage", new SendOSCMessage());
    actions_.Insert("SaveProject", new SaveProject());
    actions_.Insert("Undo", new Undo());
    actions_.Insert("Redo", new Redo());
    actions_.Insert("TrackAutoMode", new TrackAutoMode());
    actions_.Insert("GlobalAutoMode", new GlobalAutoMode());
    actions_.Insert("TrackAutoModeDisplay", new TrackAutoModeDisplay());
    actions_.Insert("GlobalAutoModeDisplay", new GlobalAutoModeDisplay());
    actions_.Insert("CycleTrackInputMonitor", new CycleTrackInputMonitor());
    actions_.Insert("TrackInputMonitorDisplay", new TrackInputMonitorDisplay());
    actions_.Insert("MCUTimeDisplay", new MCUTimeDisplay());
    actions_.Insert("OSCTimeDisplay", new OSCTimeDisplay());
    actions_.Insert("NoAction", new NoAction());
    actions_.Insert("Reaper", new ReaperAction());
    actions_.Insert("FixedTextDisplay", new FixedTextDisplay());
    actions_.Insert("FixedRGBColorDisplay", new FixedRGBColorDisplay());
    actions_.Insert("Rewind", new Rewind());
    actions_.Insert("FastForward", new FastForward());
    actions_.Insert("Play", new Play());
    actions_.Insert("Stop", new Stop());
    actions_.Insert("Record", new Record());
    actions_.Insert("CycleTimeline", new CycleTimeline());
    actions_.Insert("ToggleSynchPageBanking", new ToggleSynchPageBanking());
    actions_.Insert("ToggleScrollLink", new ToggleScrollLink());
    actions_.Insert("ToggleRestrictTextLength", new ToggleRestrictTextLength());
    actions_.Insert("CSINameDisplay", new CSINameDisplay());
    actions_.Insert("CSIVersionDisplay", new CSIVersionDisplay());
    actions_.Insert("GlobalModeDisplay", new GlobalModeDisplay());
    actions_.Insert("CycleTimeDisplayModes", new CycleTimeDisplayModes());
    actions_.Insert("NextPage", new GoNextPage());
    actions_.Insert("GoPage", new GoPage());
    actions_.Insert("PageNameDisplay", new PageNameDisplay());
    actions_.Insert("GoHome", new GoHome());
    actions_.Insert("AllSurfacesGoHome", new AllSurfacesGoHome());
    actions_.Insert("GoSubZone", new GoSubZone());
    actions_.Insert("LeaveSubZone", new LeaveSubZone());
    actions_.Insert("SetXTouchDisplayColors", new SetXTouchDisplayColors());
    actions_.Insert("RestoreXTouchDisplayColors", new RestoreXTouchDisplayColors());
    actions_.Insert("GoFXSlot", new GoFXSlot());
    actions_.Insert("ShowFXSlot", new ShowFXSlot());
    actions_.Insert("HideFXSlot", new HideFXSlot());
    actions_.Insert("ToggleUseLocalModifiers", new ToggleUseLocalModifiers());
    actions_.Insert("SetLatchTime", new SetLatchTime());
    actions_.Insert("ToggleEnableFocusedFXMapping", new ToggleEnableFocusedFXMapping());
    actions_.Insert("ToggleEnableFocusedFXParamMapping", new ToggleEnableFocusedFXParamMapping());
    actions_.Insert("RemapAutoZone", new RemapAutoZone());
    actions_.Insert("AutoMapSlotFX", new AutoMapSlotFX());
    actions_.Insert("AutoMapFocusedFX", new AutoMapFocusedFX());
    actions_.Insert("GoAssociatedZone", new GoAssociatedZone());
    actions_.Insert("GoFXLayoutZone", new GoFXLayoutZone());
    actions_.Insert("ClearFocusedFXParam", new ClearFocusedFXParam());
    actions_.Insert("ClearFocusedFX", new ClearFocusedFX());
    actions_.Insert("ClearSelectedTrackFX", new ClearSelectedTrackFX());
    actions_.Insert("ClearFXSlot", new ClearFXSlot());
    actions_.Insert("Bank", new Bank());
    actions_.Insert("Shift", new SetShift());
    actions_.Insert("Option", new SetOption());
    actions_.Insert("Control", new SetControl());
    actions_.Insert("Alt", new SetAlt());
    actions_.Insert("Flip", new SetFlip());
    actions_.Insert("Global", new SetGlobal());
    actions_.Insert("Marker", new SetMarker());
    actions_.Insert("Nudge", new SetNudge());
    actions_.Insert("Zoom", new SetZoom());
    actions_.Insert("Scrub", new SetScrub());
    actions_.Insert("ClearModifier", new ClearModifier());
    actions_.Insert("ClearModifiers", new ClearModifiers());
    actions_.Insert("ToggleChannel", new SetToggleChannel());
    actions_.Insert("CycleTrackAutoMode", new CycleTrackAutoMode());
    actions_.Insert("TrackVolume", new TrackVolume());
    actions_.Insert("SoftTakeover7BitTrackVolume", new SoftTakeover7BitTrackVolume());
    actions_.Insert("SoftTakeover14BitTrackVolume", new SoftTakeover14BitTrackVolume());
    actions_.Insert("TrackVolumeDB", new TrackVolumeDB());
    actions_.Insert("TrackToggleVCASpill", new TrackToggleVCASpill());
    actions_.Insert("TrackVCALeaderDisplay", new TrackVCALeaderDisplay());
    actions_.Insert("TrackToggleFolderSpill", new TrackToggleFolderSpill());
    actions_.Insert("TrackFolderParentDisplay", new TrackFolderParentDisplay());
    actions_.Insert("TrackSelect", new TrackSelect());
    actions_.Insert("TrackUniqueSelect", new TrackUniqueSelect());
    actions_.Insert("TrackRangeSelect", new TrackRangeSelect());
    actions_.Insert("TrackRecordArm", new TrackRecordArm());
    actions_.Insert("TrackMute", new TrackMute());
    actions_.Insert("TrackSolo", new TrackSolo());
    actions_.Insert("ClearAllSolo", new ClearAllSolo());
    actions_.Insert("TrackInvertPolarity", new TrackInvertPolarity());
    actions_.Insert("TrackPan", new TrackPan());
    actions_.Insert("TrackPanPercent", new TrackPanPercent());
    actions_.Insert("TrackPanWidth", new TrackPanWidth());
    actions_.Insert("TrackPanWidthPercent", new TrackPanWidthPercent());
    actions_.Insert("TrackPanL", new TrackPanL());
    actions_.Insert("TrackPanLPercent", new TrackPanLPercent());
    actions_.Insert("TrackPanR", new TrackPanR());
    actions_.Insert("TrackPanRPercent", new TrackPanRPercent());
    actions_.Insert("TrackPanAutoLeft", new TrackPanAutoLeft());
    actions_.Insert("TrackPanAutoRight", new TrackPanAutoRight());
    actions_.Insert("TrackNameDisplay", new TrackNameDisplay());
    actions_.Insert("TrackNumberDisplay", new TrackNumberDisplay());
    actions_.Insert("TrackRecordInputDisplay", new TrackRecordInputDisplay());
    actions_.Insert("TrackVolumeDisplay", new TrackVolumeDisplay());
    actions_.Insert("TrackPanDisplay", new TrackPanDisplay());
    actions_.Insert("TrackPanWidthDisplay", new TrackPanWidthDisplay());
    actions_.Insert("TrackPanLeftDisplay", new TrackPanLeftDisplay());
    actions_.Insert("TrackPanRightDisplay", new TrackPanRightDisplay());
    actions_.Insert("TrackPanAutoLeftDisplay", new TrackPanAutoLeftDisplay());
    actions_.Insert("TrackPanAutoRightDisplay", new TrackPanAutoRightDisplay());
    actions_.Insert("TrackOutputMeter", new TrackOutputMeter());
    actions_.Insert("TrackOutputMeterAverageLR", new TrackOutputMeterAverageLR());
    actions_.Insert("TrackVolumeWithMeterAverageLR", new TrackVolumeWithMeterAverageLR());
    actions_.Insert("TrackOutputMeterMaxPeakLR", new TrackOutputMeterMaxPeakLR());
    actions_.Insert("TrackVolumeWithMeterMaxPeakLR", new TrackVolumeWithMeterMaxPeakLR());
    actions_.Insert("FocusedFXParam", new FocusedFXParam());
    actions_.Insert("FXParam", new FXParam());
    actions_.Insert("SaveLearnedFXParams", new SaveLearnedFXParams());
    actions_.Insert("SaveTemplatedFXParams", new SaveTemplatedFXParams());
    actions_.Insert("EraseLastTouchedControl", new EraseLastTouchedControl());
    actions_.Insert("JSFXParam", new JSFXParam());
    actions_.Insert("TCPFXParam", new TCPFXParam());
    actions_.Insert("ToggleFXBypass", new ToggleFXBypass());
    actions_.Insert("FXBypassDisplay", new FXBypassDisplay());
    actions_.Insert("ToggleFXOffline", new ToggleFXOffline());
    actions_.Insert("FXOfflineDisplay", new FXOfflineDisplay());
    actions_.Insert("FXNameDisplay", new FXNameDisplay());
    actions_.Insert("FXMenuNameDisplay", new FXMenuNameDisplay());
    actions_.Insert("SpeakFXMenuName", new SpeakFXMenuName());
    actions_.Insert("FXParamNameDisplay", new FXParamNameDisplay());
    actions_.Insert("TCPFXParamNameDisplay", new TCPFXParamNameDisplay());
    actions_.Insert("FXParamValueDisplay", new FXParamValueDisplay());
    actions_.Insert("TCPFXParamValueDisplay", new TCPFXParamValueDisplay());
    actions_.Insert("FocusedFXParamNameDisplay", new FocusedFXParamNameDisplay());
    actions_.Insert("FocusedFXParamValueDisplay", new FocusedFXParamValueDisplay());
    actions_.Insert("FXGainReductionMeter", new FXGainReductionMeter());
    actions_.Insert("TrackSendVolume", new TrackSendVolume());
    actions_.Insert("TrackSendVolumeDB", new TrackSendVolumeDB());
    actions_.Insert("TrackSendPan", new TrackSendPan());
    actions_.Insert("TrackSendPanPercent", new TrackSendPanPercent());
    actions_.Insert("TrackSendMute", new TrackSendMute());
    actions_.Insert("TrackSendInvertPolarity", new TrackSendInvertPolarity());
    actions_.Insert("TrackSendStereoMonoToggle", new TrackSendStereoMonoToggle());
    actions_.Insert("TrackSendPrePost", new TrackSendPrePost());
    actions_.Insert("TrackSendNameDisplay", new TrackSendNameDisplay());
    actions_.Insert("SpeakTrackSendDestination", new SpeakTrackSendDestination());
    actions_.Insert("TrackSendVolumeDisplay", new TrackSendVolumeDisplay());
    actions_.Insert("TrackSendPanDisplay", new TrackSendPanDisplay());
    actions_.Insert("TrackSendPrePostDisplay", new TrackSendPrePostDisplay());
    actions_.Insert("TrackReceiveVolume", new TrackReceiveVolume());
    actions_.Insert("TrackReceiveVolumeDB", new TrackReceiveVolumeDB());
    actions_.Insert("TrackReceivePan", new TrackReceivePan());
    actions_.Insert("TrackReceivePanPercent", new TrackReceivePanPercent());
    actions_.Insert("TrackReceiveMute", new TrackReceiveMute());
    actions_.Insert("TrackReceiveInvertPolarity", new TrackReceiveInvertPolarity());
    actions_.Insert("TrackReceiveStereoMonoToggle", new TrackReceiveStereoMonoToggle());
    actions_.Insert("TrackReceivePrePost", new TrackReceivePrePost());
    actions_.Insert("TrackReceiveNameDisplay", new TrackReceiveNameDisplay());
    actions_.Insert("SpeakTrackReceiveSource", new SpeakTrackReceiveSource());
    actions_.Insert("TrackReceiveVolumeDisplay", new TrackReceiveVolumeDisplay());
    actions_.Insert("TrackReceivePanDisplay", new TrackReceivePanDisplay());
    actions_.Insert("TrackReceivePrePostDisplay", new TrackReceivePrePostDisplay());
    
    learnFXActions_.Insert("LearnFXParam", new LearnFXParam());
    learnFXActions_.Insert("LearnFXParamNameDisplay", new LearnFXParamNameDisplay());
    learnFXActions_.Insert("LearnFXParamValueDisplay", new LearnFXParamValueDisplay());
}

void CSurfIntegrator::Init()
{
    pages_.Empty(true);
    
    string currentBroadcaster = "";
    
    Page *currentPage = nullptr;
    
    string CSIFolderPath = string(DAW::GetResourcePath()) + "/CSI";
    
    WDL_DirScan ds;
    if (ds.First(CSIFolderPath.c_str()))
    {       
        MessageBox(g_hwnd, ("Please check your installation, cannot find " + CSIFolderPath).c_str(), "Missing CSI Folder", MB_OK);
        
        return;
    }
    
    string iniFilePath = string(DAW::GetResourcePath()) + "/CSI/CSI.ini";
    int lineNumber = 0;
    
    try
    {
        ifstream iniFile(iniFilePath);
               
        for (string line; getline(iniFile, line) ; )
        {
            TrimLine(line);
            
            if (lineNumber == 0)
            {
                if (line != s_MajorVersionToken)
                {
                    MessageBox(g_hwnd, ("Version mismatch -- Your CSI.ini file is not " + string(s_MajorVersionToken)).c_str(), ("This is CSI " + string(s_MajorVersionToken)).c_str(), MB_OK);
                    iniFile.close();
                    return;
                }
                else
                {
                    lineNumber++;
                    continue;
                }
            }
            
            if (line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
                continue;
            
            vector<string> tokens;
            GetTokens(tokens, line);

            if (tokens.size() > 1) // ignore comment lines and blank lines
            {
                if (tokens[0] == s_MidiSurfaceToken && tokens.size() == 4)
                    midiSurfacesIO_.Add(new Midi_ControlSurfaceIO(this, tokens[1], GetMidiInputForPort(atoi(tokens[2].c_str())), GetMidiOutputForPort(atoi(tokens[3].c_str()))));
                else if (tokens[0] == s_OSCSurfaceToken && tokens.size() == 5)
                    oscSurfacesIO_.Add(new OSC_ControlSurfaceIO(this, tokens[1], tokens[2], tokens[3], tokens[4]));
                else if (tokens[0] == s_PageToken)
                {
                    bool followMCP = true;
                    bool synchPages = true;
                    bool isScrollLinkEnabled = false;
                    bool isScrollSynchEnabled = false;

                    currentPage = nullptr;
                    
                    if (tokens.size() > 1)
                    {
                        if (tokens.size() > 2)
                        {
                            for (int i = 2; i < tokens.size(); i++)
                            {
                                if (tokens[i] == "FollowTCP")
                                    followMCP = false;
                                else if (tokens[i] == "NoSynchPages")
                                    synchPages = false;
                                else if (tokens[i] == "UseScrollLink")
                                    isScrollLinkEnabled = true;
                                else if (tokens[i] == "UseScrollSynch")
                                    isScrollSynchEnabled = true;
                            }
                        }
                            
                        currentPage = new Page(this, tokens[1], followMCP, synchPages, isScrollLinkEnabled, isScrollSynchEnabled);
                        pages_.Add(currentPage);
                    }
                }
                else if (currentPage && tokens.size() > 1 && tokens[0] == "Broadcaster")
                {
                    currentBroadcaster = tokens[1];
                }
                else if (currentPage && tokens.size() > 2 && currentBroadcaster != "" && tokens[0] == "Listener")
                {
                    ControlSurface *broadcaster = nullptr;
                    ControlSurface *listener = nullptr;

                    for (int i = 0; i < currentPage->GetSurfaces().GetSize(); ++i)
                    {
                        if (currentPage->GetSurfaces().Get(i)->GetName() == currentBroadcaster)
                            broadcaster = currentPage->GetSurfaces().Get(i);
                        if (currentPage->GetSurfaces().Get(i)->GetName() == tokens[1])
                            listener = currentPage->GetSurfaces().Get(i);
                    }
                    
                    if (broadcaster != nullptr && listener != nullptr)
                    {
                        broadcaster->GetZoneManager()->AddListener(listener);
                        listener->GetZoneManager()->SetListenerCategories(tokens[2]);
                    }
                }
                else
                {
                    if (currentPage && (tokens.size() == 6 || tokens.size() == 7))
                    {
                        string zoneFolder = tokens[4];
                        string fxZoneFolder = tokens[5];
                        
                        // GAW -- there is a potential bug here (if you hand edit CSI.ini for instance), no way to tell if surfacew is MIDI or OSC
                        // maybe prepend the line with s_MidiSurfaceToken or s_OSCSurfaceToken, would break existing installs though
                        bool foundIt = false;
                        
                        for (int i = 0; i < midiSurfacesIO_.GetSize(); ++i)
                        {
                            Midi_ControlSurfaceIO *const io = midiSurfacesIO_.Get(i);
                            
                            if (tokens[0] == io->GetName())
                            {
                                foundIt = true;
                                currentPage->AddSurface(new Midi_ControlSurface(this, currentPage, tokens[0], atoi(tokens[1].c_str()), atoi(tokens[2].c_str()), tokens[3], zoneFolder, fxZoneFolder, io));
                                break;
                            }
                        }
                        
                        if ( ! foundIt)
                        {
                            for (int i = 0; i < oscSurfacesIO_.GetSize(); ++i)
                            {
                                OSC_ControlSurfaceIO *const io = oscSurfacesIO_.Get(i);
                                
                                if (tokens[0] == io->GetName())
                                {
                                    foundIt = true;
                                    currentPage->AddSurface(new OSC_ControlSurface(this, currentPage, tokens[0], atoi(tokens[1].c_str()), atoi(tokens[2].c_str()), tokens[3], zoneFolder, fxZoneFolder, io));
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            
            lineNumber++;
        }
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", iniFilePath.c_str(), lineNumber);
        DAW::ShowConsoleMsg(buffer);
    }
    
    for (int i = 0; i < pages_.GetSize(); ++i)
        pages_.Get(i)->OnInitialization();
}
//////////////////////////////////////////////////////////////////////////////////////////////
// Parsing end
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////
// TrackNavigator
////////////////////////////////////////////////////////////////////////////////////////////////////////
MediaTrack *TrackNavigator::GetTrack()
{
    return trackNavigationManager_->GetTrackFromChannel(channelNum_);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// MasterTrackNavigator
////////////////////////////////////////////////////////////////////////////////////////////////////////
MediaTrack *MasterTrackNavigator::GetTrack()
{
    return DAW::GetMasterTrack();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// SelectedTrackNavigator
////////////////////////////////////////////////////////////////////////////////////////////////////////
MediaTrack *SelectedTrackNavigator::GetTrack()
{
    return page_->GetSelectedTrack();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// FocusedFXNavigator
////////////////////////////////////////////////////////////////////////////////////////////////////////
MediaTrack *FocusedFXNavigator::GetTrack()
{
    int trackNumber = 0;
    int itemNumber = 0;
    int fxIndex = 0;
    
    if (DAW::GetFocusedFX2(&trackNumber, &itemNumber, &fxIndex) == 1) // Track FX
        return DAW::GetTrack(trackNumber);
    else
        return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ActionContext
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ActionContext::ActionContext(CSurfIntegrator *const csi, Action *action, Widget *widget, Zone *zone, int paramIndex, const vector<string> *paramsAndProperties, const string *stringParam): csi_(csi), action_(action), widget_(widget), zone_(zone)
{
    intParam_ = 0;
    supportsColor_ = false;
    supportsTrackColor_ = false;
    provideFeedback_ = false;
    
    if (stringParam != NULL)
        stringParam_ = *stringParam;
    
    paramIndex_ = paramIndex;
    fxParamDisplayName_ = "";
    
    commandId_ = 0;
    
    rangeMinimum_ = 0.0;
    rangeMaximum_ = 1.0;
    
    steppedValuesIndex_ = 0;
    
    deltaValue_ = 0.0;
    accumulatedIncTicks_ = 0;
    accumulatedDecTicks_ = 0;
    
    isValueInverted_ = false;
    isFeedbackInverted_ = false;
    holdDelayAmount_ = 0.0;
    delayStartTime_ = 0.0;
    deferredValue_ = 0.0;
    
    supportsColor_ = false;
    currentColorIndex_ = 0;
    
    supportsTrackColor_ = false;
        
    provideFeedback_ = false;
    
    // For Learn
    cellAddress_.Set("");
    
    vector<string> params;
    
    if (paramsAndProperties != NULL) for (int i = 0; i < (int)(*paramsAndProperties).size(); ++i)
    {
        if ((*paramsAndProperties)[i].find("=") != string::npos)
        {
            istringstream widgetProperty((*paramsAndProperties)[i]);
            vector<string> kvp;
            string token;
            
            while (getline(widgetProperty, token, '='))
                kvp.push_back(token);

            if (kvp.size() == 2)
            {
                PropertyType prop = PropertyList::prop_from_string(kvp[0].c_str());
                if (prop != PropertyType_Unknown)
                {
                    widgetProperties_.set_prop(prop, kvp[1].c_str());;
                }
                else
                {
                    WDL_FastString fs;
                    fs.Set(kvp[0].c_str());
                    fs.Append("=");
                    fs.Append(kvp[1].c_str());
                    widgetProperties_.set_prop(prop, fs.Get());
                    // cerr << "unknown property" << str;
                    //WDL_ASSERT(false);
                }
            }
        }
        else
            params.push_back((*paramsAndProperties)[i]);
    }
    
    for (int i = 1; i < params.size(); i++)
        parameters_.push_back(params[i]);
    
    string actionName = "";
    
    if (params.size() > 0)
        actionName = params[0];
    
    // Action with int param, could include leading minus sign
    if (params.size() > 1 && (isdigit(params[1][0]) ||  params[1][0] == '-'))  // C++ 11 says empty strings can be queried without catastrophe :)
    {
        intParam_= atol(params[1].c_str());
    }
    
    if (actionName == "Bank" && (params.size() > 2 && (isdigit(params[2][0]) ||  params[2][0] == '-')))  // C++ 11 says empty strings can be queried without catastrophe :)
    {
        stringParam_ = params[1];
        intParam_= atol(params[2].c_str());
    }
    
    // Action with param index, must be positive
    if (params.size() > 1 && isdigit(params[1][0]))  // C++ 11 says empty strings can be queried without catastrophe :)
    {
        paramIndex_ = atol(params[1].c_str());
    }
    
    // Action with string param
    if (params.size() > 1)
        stringParam_ = params[1];
    
    if (actionName == "TrackVolumeDB" || actionName == "TrackSendVolumeDB")
    {
        rangeMinimum_ = -144.0;
        rangeMaximum_ = 24.0;
    }
    
    if (actionName == "TrackPanPercent" || actionName == "TrackPanWidthPercent" || actionName == "TrackPanLPercent" || actionName == "TrackPanRPercent")
    {
        rangeMinimum_ = -100.0;
        rangeMaximum_ = 100.0;
    }
   
    if ((actionName == "Reaper" || actionName == "ReaperDec" || actionName == "ReaperInc") && params.size() > 1)
    {
        if (isdigit(params[1][0]))
        {
            commandId_ =  atol(params[1].c_str());
        }
        else // look up by string
        {
            commandId_ = DAW::NamedCommandLookup(params[1].c_str());
            
            if (commandId_ == 0) // can't find it
                commandId_ = 65535; // no-op
        }
    }
        
    if ((actionName == "FXParam" || actionName == "JSFXParam") && params.size() > 1 && isdigit(params[1][0])) // C++ 11 says empty strings can be queried without catastrophe :)
    {
        paramIndex_ = atol(params[1].c_str());
    }
    
    if (actionName == "FXParamValueDisplay" && params.size() > 1 && isdigit(params[1][0]))
    {
        paramIndex_ = atol(params[1].c_str());
    }
    
    if (actionName == "FXParamNameDisplay" && params.size() > 1 && isdigit(params[1][0]))
    {
        paramIndex_ = atol(params[1].c_str());
        
        if (params.size() > 2 && params[2] != "{" && params[2] != "[")
               fxParamDisplayName_ = params[2];
    }
    
    if (params.size() > 0)
        SetColor(params, supportsColor_, supportsTrackColor_, colorValues_);
    
    GetSteppedValues(widget, action_, zone_, paramIndex_, params, widgetProperties_, deltaValue_, acceleratedDeltaValues_, rangeMinimum_, rangeMaximum_, steppedValues_, acceleratedTickValues_);

    if (acceleratedTickValues_.size() < 1)
        acceleratedTickValues_.push_back(10);
}

Page *ActionContext::GetPage()
{
    return widget_->GetSurface()->GetPage();
}

ControlSurface *ActionContext::GetSurface()
{
    return widget_->GetSurface();
}

MediaTrack *ActionContext::GetTrack()
{
    return zone_->GetNavigator()->GetTrack();
}

int ActionContext::GetSlotIndex()
{
    return zone_->GetSlotIndex();
}

const char *ActionContext::GetName()
{
    return zone_->GetNameOrAlias().c_str();
}

void ActionContext::RunDeferredActions()
{
    if (holdDelayAmount_ != 0.0 && delayStartTime_ != 0.0 && DAW::GetCurrentNumberOfMilliseconds() > (delayStartTime_ + holdDelayAmount_))
    {
        if (steppedValues_.size() > 0)
        {
            if (deferredValue_ != 0.0) // ignore release messages
            {
                if (steppedValuesIndex_ == steppedValues_.size() - 1)
                {
                    if (steppedValues_[0] < steppedValues_[steppedValuesIndex_]) // GAW -- only wrap if 1st value is lower
                        steppedValuesIndex_ = 0;
                }
                else
                    steppedValuesIndex_++;
                
                DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
            }
        }
        else
            DoRangeBoundAction(deferredValue_);

        delayStartTime_ = 0.0;
        deferredValue_ = 0.0;
    }
}

void ActionContext::RequestUpdate()
{
    if (provideFeedback_)
        action_->RequestUpdate(this);
}

void ActionContext::RequestUpdate(int paramNum)
{
    if (provideFeedback_)
        action_->RequestUpdate(this, paramNum);
}

void ActionContext::ClearWidget()
{
    UpdateWidgetValue(0.0);
    UpdateWidgetValue("");
}

void ActionContext::UpdateColorValue(double value)
{
    if (supportsColor_)
    {
        currentColorIndex_ = value == 0 ? 0 : 1;
        if (colorValues_.size() > currentColorIndex_)
            widget_->UpdateColorValue(colorValues_[currentColorIndex_]);
    }
}

void ActionContext::UpdateWidgetValue(double value)
{
    if (steppedValues_.size() > 0)
        SetSteppedValueIndex(value);

    value = isFeedbackInverted_ == false ? value : 1.0 - value;
   
    widget_->UpdateValue(widgetProperties_, value);

    UpdateColorValue(value);
    
    if (supportsTrackColor_)
        UpdateTrackColor();
}

void ActionContext::UpdateJSFXWidgetSteppedValue(double value)
{
    if (steppedValues_.size() > 0)
        SetSteppedValueIndex(value);
}

void ActionContext::UpdateTrackColor()
{
    if (MediaTrack *track = zone_->GetNavigator()->GetTrack())
    {
        rgba_color color = DAW::GetTrackColor(track);
        widget_->UpdateColorValue(color);
    }
}

void ActionContext::UpdateWidgetValue(const char *value)
{
    widget_->UpdateValue(widgetProperties_, value ? value : "");
}

void ActionContext::DoAction(double value)
{
    if (holdDelayAmount_ != 0.0)
    {
        if (value == 0.0)
        {
            deferredValue_ = 0.0;
            delayStartTime_ = 0.0;
        }
        else
        {
            deferredValue_ = value;
            delayStartTime_ =  DAW::GetCurrentNumberOfMilliseconds();
        }
    }
    else
    {
        if (steppedValues_.size() > 0)
        {
            if (value != 0.0) // ignore release messages
            {
                if (steppedValuesIndex_ == steppedValues_.size() - 1)
                {
                    if (steppedValues_[0] < steppedValues_[steppedValuesIndex_]) // GAW -- only wrap if 1st value is lower
                        steppedValuesIndex_ = 0;
                }
                else
                    steppedValuesIndex_++;
                
                DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
            }
        }
        else
            DoRangeBoundAction(value);
    }
}

void ActionContext::DoRelativeAction(double delta)
{
    if (steppedValues_.size() > 0)
        DoSteppedValueAction(delta);
    else
        DoRangeBoundAction(action_->GetCurrentNormalizedValue(this) + (deltaValue_ != 0.0 ? (delta > 0 ? deltaValue_ : -deltaValue_) : delta));
}

void ActionContext::DoRelativeAction(int accelerationIndex, double delta)
{
    if (steppedValues_.size() > 0)
        DoAcceleratedSteppedValueAction(accelerationIndex, delta);
    else if (acceleratedDeltaValues_.size() > 0)
        DoAcceleratedDeltaValueAction(accelerationIndex, delta);
    else
        DoRangeBoundAction(action_->GetCurrentNormalizedValue(this) +  (deltaValue_ != 0.0 ? (delta > 0 ? deltaValue_ : -deltaValue_) : delta));
}

void ActionContext::DoRangeBoundAction(double value)
{
    if (value > rangeMaximum_)
        value = rangeMaximum_;
    
    if (value < rangeMinimum_)
        value = rangeMinimum_;
    
    if (isValueInverted_)
        value = 1.0 - value;
    
    widget_->GetZoneManager()->WidgetMoved(this);
    
    action_->Do(this, value);
}

void ActionContext::DoSteppedValueAction(double delta)
{
    if (delta > 0)
    {
        steppedValuesIndex_++;
        
        if (steppedValuesIndex_ > (int)steppedValues_.size() - 1)
            steppedValuesIndex_ = (int)steppedValues_.size() - 1;
        
        DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
    }
    else
    {
        steppedValuesIndex_--;
        
        if (steppedValuesIndex_ < 0 )
            steppedValuesIndex_ = 0;
        
        DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
    }
}

void ActionContext::DoAcceleratedSteppedValueAction(int accelerationIndex, double delta)
{
    if (delta > 0)
    {
        accumulatedIncTicks_++;
        accumulatedDecTicks_ = accumulatedDecTicks_ - 1 < 0 ? 0 : accumulatedDecTicks_ - 1;
    }
    else if (delta < 0)
    {
        accumulatedDecTicks_++;
        accumulatedIncTicks_ = accumulatedIncTicks_ - 1 < 0 ? 0 : accumulatedIncTicks_ - 1;
    }
    
    accelerationIndex = accelerationIndex > (int)acceleratedTickValues_.size() - 1 ? (int)acceleratedTickValues_.size() - 1 : accelerationIndex;
    accelerationIndex = accelerationIndex < 0 ? 0 : accelerationIndex;
    
    if (delta > 0 && accumulatedIncTicks_ >= acceleratedTickValues_[accelerationIndex])
    {
        accumulatedIncTicks_ = 0;
        accumulatedDecTicks_ = 0;
        
        steppedValuesIndex_++;
        
        if (steppedValuesIndex_ > (int)steppedValues_.size() - 1)
            steppedValuesIndex_ = (int)steppedValues_.size() - 1;
        
        DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
    }
    else if (delta < 0 && accumulatedDecTicks_ >= acceleratedTickValues_[accelerationIndex])
    {
        accumulatedIncTicks_ = 0;
        accumulatedDecTicks_ = 0;
        
        steppedValuesIndex_--;
        
        if (steppedValuesIndex_ < 0 )
            steppedValuesIndex_ = 0;
        
        DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
    }
}

void ActionContext::DoAcceleratedDeltaValueAction(int accelerationIndex, double delta)
{
    accelerationIndex = accelerationIndex > (int)acceleratedDeltaValues_.size() - 1 ? (int)acceleratedDeltaValues_.size() - 1 : accelerationIndex;
    accelerationIndex = accelerationIndex < 0 ? 0 : accelerationIndex;
    
    if (delta > 0.0)
        DoRangeBoundAction(action_->GetCurrentNormalizedValue(this) + acceleratedDeltaValues_[accelerationIndex]);
    else
        DoRangeBoundAction(action_->GetCurrentNormalizedValue(this) - acceleratedDeltaValues_[accelerationIndex]);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Zone
////////////////////////////////////////////////////////////////////////////////////////////////////////
Zone::Zone(CSurfIntegrator *const csi, ZoneManager  *const zoneManager, Navigator *navigator, int slotIndex, string name, string alias, string sourceFilePath, vector<string> includedZones, vector<string> associatedZones): csi_(csi), zoneManager_(zoneManager), navigator_(navigator), slotIndex_(slotIndex), name_(name), alias_(alias), sourceFilePath_(sourceFilePath), subZones_(true,disposeZoneRefList), associatedZones_(true,disposeZoneRefList), learnFXCells_(destroyLearnFXCellList), actionContextDictionary_(destroyActionContextListArray)
{
    //protected:
    isActive_ = false;

    if (name == "Home")
    {
        for (int i = 0; i < (int)associatedZones.size(); ++i)
        {
            if (zoneManager_->GetZoneFilePaths().Exists(associatedZones[i].c_str()))
            {
                WDL_PtrList<Navigator> navigators;
                AddNavigatorsForZone(associatedZones[i], navigators);

                WDL_PtrList<Zone> *az = associatedZones_.Get(associatedZones[i].c_str());
                if (WDL_NORMALLY(az == NULL))
                {
                  az = new WDL_PtrList<Zone>;
                  associatedZones_.Insert(associatedZones[i].c_str(), az);
                }
                else
                {
                  // how to handle duplicates?
                  az->Empty();
                }
                
                zoneManager_->LoadZoneFile(zoneManager_->GetZoneFilePaths().Get(associatedZones[i].c_str())->filePath, navigators, *az, nullptr);
            }
        }
    }
    
    for (int i = 0; i < (int)includedZones.size(); ++i)
    {
        if (zoneManager_->GetZoneFilePaths().Exists(includedZones[i].c_str()))
        {
            WDL_PtrList<Navigator> navigators;
            AddNavigatorsForZone(includedZones[i], navigators);
            
            zoneManager_->LoadZoneFile(zoneManager_->GetZoneFilePaths().Get(includedZones[i].c_str())->filePath, navigators, includedZones_, nullptr);
        }
    }
}

void Zone::InitSubZones(const vector<string> &subZones, Zone *enclosingZone)
{
    for (int i = 0; i < (int)subZones.size(); ++i)
    {
        if (zoneManager_->GetZoneFilePaths().Exists(subZones[i].c_str()) > 0)
        {
            WDL_PtrList<Navigator> navigators;
            navigators.Add(GetNavigator());

            WDL_PtrList<Zone> *sz = subZones_.Get(subZones[i].c_str());
            if (WDL_NORMALLY(sz == NULL))
            {
                sz = new WDL_PtrList<Zone>;
                subZones_.Insert(subZones[i].c_str(), sz);
            }
            else
            {
                // how to handle duplicates?
                sz->Empty();
            }
        
            zoneManager_->LoadZoneFile(zoneManager_->GetZoneFilePaths().Get(subZones[i].c_str())->filePath, navigators, *sz, enclosingZone);
        }
    }
}

int Zone::GetSlotIndex()
{
    if (name_ == "TrackSend")
        return zoneManager_->GetTrackSendOffset();
    if (name_ == "TrackReceive")
        return zoneManager_->GetTrackReceiveOffset();
    if (name_ == "TrackFXMenu")
        return zoneManager_->GetTrackFXMenuOffset();
    if (name_ == "SelectedTrack")
        return slotIndex_;
    if (name_ == "SelectedTrackSend")
        return slotIndex_ + zoneManager_->GetSelectedTrackSendOffset();
    if (name_ == "SelectedTrackReceive")
        return slotIndex_ + zoneManager_->GetSelectedTrackReceiveOffset();
    if (name_ == "SelectedTrackFXMenu")
        return slotIndex_ + zoneManager_->GetSelectedTrackFXMenuOffset();
    if (name_ == "MasterTrackFXMenu")
        return slotIndex_ + zoneManager_->GetMasterTrackFXMenuOffset();
    else return slotIndex_;
}

int Zone::GetParamIndex(const string &widgetName)
{
    Widget *w = widgetsByName_.Get(widgetName.c_str());
    if (w)
    {
        const WDL_PtrList<ActionContext> &contexts = GetActionContexts(w);
        
        if (contexts.GetSize() > 0)
            return contexts.Get(0)->GetParamIndex();
    }
    
    return -1;
}

int Zone::GetChannelNumber()
{
    int channelNumber = 0;
    
    for (int i = 0; i < widgets_.GetSize(); i++)
    {
        Widget *widget = NULL;
        if (WDL_NORMALLY(widgets_.EnumeratePtr(i,&widget) && widget) && channelNumber < widget->GetChannelNumber())
            channelNumber = widget->GetChannelNumber();
    }
    
    return channelNumber;
}

void Zone::SetFXParamNum(Widget *widget, int paramIndex)
{
    if (widgets_.Exists(widget))
    {
        for (int i = 0; i < GetActionContexts(widget, currentActionContextModifiers_.Get(widget)).GetSize(); ++i)
            GetActionContexts(widget, currentActionContextModifiers_.Get(widget)).Get(i)->SetParamIndex(paramIndex);
    }
}

void Zone::GoAssociatedZone(const char *zoneName)
{
    if (!strcmp(zoneName, "Track"))
    {
        for (int j = 0; j < associatedZones_.GetSize(); j++)
        {
            WDL_PtrList<Zone> *zones = associatedZones_.Enumerate(j);
            if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
                zones->Get(i)->Deactivate();
        }
        
        return;
    }
    
    WDL_PtrList<Zone> *az = associatedZones_.Get(zoneName);
    if (az != NULL && az->Get(0) != NULL && az->Get(0)->GetIsActive())
    {
        for (int i = 0; i < az->GetSize(); ++i)
            az->Get(i)->Deactivate();
        
        zoneManager_->GoHome();
        
        return;
    }
    
    for (int j = 0; j < associatedZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = associatedZones_.Enumerate(j);
        if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->Deactivate();
    }

    az = associatedZones_.Get(zoneName);
    if (az != NULL)
        for (int i = 0; i < az->GetSize(); ++i)
            az->Get(i)->Activate();
}

void Zone::GoAssociatedZone(const char *zoneName, int slotIndex)
{
    if (!strcmp(zoneName, "Track"))
    {
        for (int j = 0; j < associatedZones_.GetSize(); j++)
        {
            WDL_PtrList<Zone> *zones = associatedZones_.Enumerate(j);
            if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
                zones->Get(i)->Deactivate();
        }

        return;
    }

    WDL_PtrList<Zone> *az = associatedZones_.Get(zoneName);
    if (az != NULL && az->Get(0) != NULL && az->Get(0)->GetIsActive())
    {
        for (int i = 0; i < az->GetSize(); ++i)
            az->Get(i)->Deactivate();
        
        zoneManager_->GoHome();

        return;
    }

    for (int j = 0; j < associatedZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = associatedZones_.Enumerate(j);
        if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->Deactivate();
    }


    az = associatedZones_.Get(zoneName);
    if (az != NULL)
    {
        for (int i = 0; i < az->GetSize(); ++i)
        {
            az->Get(i)->SetSlotIndex(slotIndex);
            az->Get(i)->Activate();
        }
    }
}

void Zone::ReactivateFXMenuZone()
{
    WDL_PtrList<Zone> *fxmenu = associatedZones_.Get("TrackFXMenu");
    WDL_PtrList<Zone> *selfxmenu = associatedZones_.Get("SelectedTrackFXMenu");
    if (fxmenu && fxmenu->Get(0) != NULL && fxmenu->Get(0)->GetIsActive())
        for (int i = 0; i < fxmenu->GetSize(); ++i)
            fxmenu->Get(i)->Activate();
    else if (selfxmenu && selfxmenu->Get(0) && selfxmenu->Get(0)->GetIsActive())
        for (int i = 0; i < selfxmenu->GetSize(); ++i)
            selfxmenu->Get(i)->Activate();
}

void Zone::Activate()
{
    UpdateCurrentActionContextModifiers();
    
    for (int wi = 0; wi < widgets_.GetSize(); wi ++)
    {
        Widget *widget = NULL;
        if (WDL_NOT_NORMALLY(!widgets_.EnumeratePtr(wi,&widget) || !widget)) break;
        if (!strcmp(widget->GetName(), "OnZoneActivation"))
            for (int i = 0; i < GetActionContexts(widget).GetSize(); ++i)
                GetActionContexts(widget).Get(i)->DoAction(1.0);
            
        widget->Configure(GetActionContexts(widget));
    }

    isActive_ = true;
    
    if (!strcmp(GetName(), "VCA"))
        zoneManager_->GetSurface()->GetPage()->VCAModeActivated();
    else if (!strcmp(GetName(), "Folder"))
        zoneManager_->GetSurface()->GetPage()->FolderModeActivated();
    else if (!strcmp(GetName(), "SelectedTracks"))
        zoneManager_->GetSurface()->GetPage()->SelectedTracksModeActivated();

    zoneManager_->GetSurface()->SendOSCMessage(GetName());

    for (int j = 0; j < associatedZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = associatedZones_.Enumerate(j);
        if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->Deactivate();
    }
    
    for (int j = 0; j < subZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = subZones_.Enumerate(j);
        if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->Deactivate();
    }

    for (int i = 0; i < includedZones_.GetSize(); ++i)
        includedZones_.Get(i)->Activate();
}

void Zone::Deactivate()
{    
    for (int wi = 0; wi < widgets_.GetSize(); wi ++)
    {
        Widget *widget = NULL;
        if (WDL_NOT_NORMALLY(!widgets_.EnumeratePtr(wi,&widget) || !widget)) break;
        for (int i = 0; i < GetActionContexts(widget).GetSize(); ++i)
        {
            GetActionContexts(widget).Get(i)->UpdateWidgetValue(0.0);
            GetActionContexts(widget).Get(i)->UpdateWidgetValue("");

            if (!strcmp(widget->GetName(), "OnZoneDeactivation"))
                GetActionContexts(widget).Get(i)->DoAction(1.0);
        }
    }

    isActive_ = false;
    
    if (!strcmp(GetName(), "VCA"))
        zoneManager_->GetSurface()->GetPage()->VCAModeDeactivated();
    else if (!strcmp(GetName(), "Folder"))
        zoneManager_->GetSurface()->GetPage()->FolderModeDeactivated();
    else if (!strcmp(GetName(), "SelectedTracks"))
        zoneManager_->GetSurface()->GetPage()->SelectedTracksModeDeactivated();
    
    for (int i = 0; i < includedZones_.GetSize(); ++i)
        includedZones_.Get(i)->Deactivate();

    for (int j = 0; j < associatedZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = associatedZones_.Enumerate(j);
        if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->Deactivate();
    }
    
    for (int j = 0; j < subZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = subZones_.Enumerate(j);
        if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->Deactivate();
    }

}

void Zone::RequestUpdate()
{
    if (! isActive_)
        return;
    
    for (int j = 0; j < subZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = subZones_.Enumerate(j);
        if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->RequestUpdate();
    }

    for (int j = 0; j < associatedZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = associatedZones_.Enumerate(j);
        if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->RequestUpdate();
    }

    for (int i =  0; i < includedZones_.GetSize(); ++i)
        includedZones_.Get(i)->RequestUpdate();
    
    for (int i = 0; i < thisZoneWidgets_.GetSize(); i ++)
    {
        if ( ! thisZoneWidgets_.Get(i)->GetHasBeenUsedByUpdate())
        {
            thisZoneWidgets_.Get(i)->SetHasBeenUsedByUpdate();
            RequestUpdateWidget(thisZoneWidgets_.Get(i));
        }
    }
}


void Zone::RequestLearnFXUpdate()
{
    const WDL_TypedBuf<int> &modifiers = zoneManager_->GetSurface()->GetModifiers();
    
    int modifier = 0;
    
    if (modifiers.GetSize() > 0)
        modifier = modifiers.Get()[0];
    

    WDL_StringKeyedArray<LearnFXCell *> *mlist = learnFXCells_.Get(modifier);
    if (mlist)
    {
        for (int ci = 0; ci < mlist->GetSize(); ci ++)
        {
            const LearnFXCell * const cell = mlist->Enumerate(ci);
            if (WDL_NOT_NORMALLY(!cell)) continue;
            bool foundIt = false;
            
            for (int i = 0; i < cell->fxParamWidgets.GetSize(); ++i)
            {
                LearnInfo *info = zoneManager_->GetLearnInfo(cell->fxParamWidgets.Get(i), modifier);
                                
                if (info->isLearned)
                {
                    foundIt = true;

                    WDL_IntKeyedArray<WDL_PtrList<ActionContext> *> *wl = actionContextDictionary_.Get(cell->fxParamNameDisplayWidget);
                    WDL_PtrList<ActionContext> *list = wl ? wl->Get(modifier) : NULL;
                    if (list)
                        for (int j = 0; j < list->GetSize(); ++j)
                            list->Get(j)->RequestUpdate(info->paramNumber);

                    wl = actionContextDictionary_.Get(cell->fxParamValueDisplayWidget);
                    list = wl ? wl->Get(modifier) : NULL;
                    if (list)
                        for (int j = 0; j < list->GetSize(); ++j)
                            list->Get(j)->RequestUpdate(info->paramNumber);
                }
                else
                {
                    WDL_IntKeyedArray<WDL_PtrList<ActionContext> *> *wl = actionContextDictionary_.Get(cell->fxParamWidgets.Get(i));
                    WDL_PtrList<ActionContext> *list = wl ? wl->Get(modifier) : NULL;

                    if (list)
                    {
                        for (int j = 0; j < list->GetSize(); ++j)
                        {
                            list->Get(j)->UpdateWidgetValue(0.0);
                            list->Get(j)->UpdateWidgetValue("");
                        }
                    }
                }
                
                cell->fxParamWidgets.Get(i)->SetHasBeenUsedByUpdate();
            }
            
            if (! foundIt)
            {
                WDL_IntKeyedArray<WDL_PtrList<ActionContext> *> *wl = actionContextDictionary_.Get(cell->fxParamNameDisplayWidget);
                WDL_PtrList<ActionContext> *list = wl ? wl->Get(modifier) : NULL;
                if (list)
                {
                    for (int i = 0; i < list->GetSize(); ++i)
                    {
                        list->Get(i)->UpdateWidgetValue(0.0);
                        list->Get(i)->UpdateWidgetValue("");
                    }
                    
                    cell->fxParamNameDisplayWidget->SetHasBeenUsedByUpdate();
                }

                wl = actionContextDictionary_.Get(cell->fxParamValueDisplayWidget);
                list = wl ? wl->Get(modifier) : NULL;
                if (list)
                {
                    for (int i = 0; i < list->GetSize(); ++i)
                    {
                        list->Get(i)->UpdateWidgetValue(0.0);
                        list->Get(i)->UpdateWidgetValue("");
                    }
                    
                    cell->fxParamValueDisplayWidget->SetHasBeenUsedByUpdate();
                }
            }
        }
    }
}

void Zone::AddNavigatorsForZone(const string &zoneName, WDL_PtrList<Navigator> &navigators)
{
    if (zoneName == "MasterTrack")
        navigators.Add(zoneManager_->GetMasterTrackNavigator());
    else if (zoneName == "Track" || zoneName == "VCA" || zoneName == "Folder" || zoneName == "SelectedTracks" || zoneName == "TrackSend" || zoneName == "TrackReceive" || zoneName == "TrackFXMenu")
    {
        for (int i = 0; i < zoneManager_->GetNumChannels(); i++)
        {
            Navigator *channelNavigator = zoneManager_->GetSurface()->GetPage()->GetNavigatorForChannel(i + zoneManager_->GetSurface()->GetChannelOffset());
            if (channelNavigator)
                navigators.Add(channelNavigator);
        }
    }
    else if (zoneName == "SelectedTrack" || zoneName == "SelectedTrackSend" || zoneName == "SelectedTrackReceive" || zoneName == "SelectedTrackFXMenu")
        for (int i = 0; i < zoneManager_->GetNumChannels(); i++)
            navigators.Add(zoneManager_->GetSelectedTrackNavigator());
    else if (zoneName == "MasterTrackFXMenu")
        for (int i = 0; i < zoneManager_->GetNumChannels(); i++)
            navigators.Add(zoneManager_->GetMasterTrackNavigator());
    else
        navigators.Add(zoneManager_->GetSelectedTrackNavigator());
}

void Zone::SetXTouchDisplayColors(const string &color)
{
    for (int wi = 0; wi < widgets_.GetSize(); wi ++)
    {
        Widget *widget = NULL;
        if (WDL_NOT_NORMALLY(!widgets_.EnumeratePtr(wi,&widget) || !widget)) break;
        widget->SetXTouchDisplayColors(name_, color);
    }
}

void Zone::RestoreXTouchDisplayColors()
{
    for (int wi = 0; wi < widgets_.GetSize(); wi ++)
    {
        Widget *widget = NULL;
        if (WDL_NOT_NORMALLY(!widgets_.EnumeratePtr(wi,&widget) || !widget)) break;
        widget->RestoreXTouchDisplayColors();
    }
}

void Zone::DoAction(Widget *widget, bool &isUsed, double value)
{
    if (! isActive_ || isUsed)
        return;
    
    for (int j = 0; j < subZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = subZones_.Enumerate(j);
        if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->DoAction(widget, isUsed, value);
    }

    for (int j = 0; j < associatedZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = associatedZones_.Enumerate(j);
        if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->DoAction(widget, isUsed, value);
    }

    if (isUsed)
        return;

    if (widgets_.Exists(widget))
    {
        if (csi_->GetSurfaceInDisplay())
        {
            char buffer[250];
            snprintf(buffer, sizeof(buffer), "Zone -- %s\n", sourceFilePath_.c_str());
            DAW::ShowConsoleMsg(buffer);
        }

        isUsed = true;
        
        for (int i = 0; i < GetActionContexts(widget).GetSize(); ++i)
            GetActionContexts(widget).Get(i)->DoAction(value);
    }
    else
    {
        for (int i = 0; i < includedZones_.GetSize(); ++i)
            includedZones_.Get(i)->DoAction(widget, isUsed, value);
    }
}

void Zone::UpdateCurrentActionContextModifiers()
{
    for (int wi = 0; wi < widgets_.GetSize(); wi ++)
    {
        Widget *widget = NULL;
        if (WDL_NOT_NORMALLY(!widgets_.EnumeratePtr(wi,&widget) || !widget)) break;
        UpdateCurrentActionContextModifier(widget);
        widget->Configure(GetActionContexts(widget, currentActionContextModifiers_.Get(widget)));
    }
    
    for (int i = 0; i < includedZones_.GetSize(); ++i)
        includedZones_.Get(i)->UpdateCurrentActionContextModifiers();

    for (int j = 0; j < subZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = subZones_.Enumerate(j);
        if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->UpdateCurrentActionContextModifiers();
    }
    
    for (int j = 0; j < associatedZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = associatedZones_.Enumerate(j);
        if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->UpdateCurrentActionContextModifiers();
    }
}

void Zone::UpdateCurrentActionContextModifier(Widget *widget)
{
    const WDL_TypedBuf<int> &mods = widget->GetSurface()->GetModifiers();
    
    WDL_IntKeyedArray<WDL_PtrList<ActionContext> *> *wl = actionContextDictionary_.Get(widget);
    if (wl != NULL) for (int i = 0; i < mods.GetSize(); ++i)
    {
        if (wl->Get(mods.Get()[i]))
        {
            currentActionContextModifiers_.Delete(widget);
            currentActionContextModifiers_.Insert(widget, mods.Get()[i]);
            break;
        }
    }
}

const WDL_PtrList<ActionContext> &Zone::GetActionContexts(Widget *widget)
{
    if ( ! currentActionContextModifiers_.Exists(widget))
        UpdateCurrentActionContextModifier(widget);
    
    bool isTouched = false;
    bool isToggled = false;
    
    if (widget->GetSurface()->GetIsChannelTouched(widget->GetChannelNumber()))
        isTouched = true;

    if (widget->GetSurface()->GetIsChannelToggled(widget->GetChannelNumber()))
        isToggled = true;
    
    WDL_IntKeyedArray<WDL_PtrList<ActionContext> *> *wl = NULL;
    if (currentActionContextModifiers_.Exists(widget) && (wl = actionContextDictionary_.Get(widget)) != NULL)
    {
        const int modifier = currentActionContextModifiers_.Get(widget);
        
        WDL_PtrList<ActionContext> *ret = NULL;
        if (isTouched && isToggled && !ret) ret = wl->Get(modifier+3);
        if (isTouched && !ret) ret = wl->Get(modifier+1);
        if (isToggled && !ret) ret = wl->Get(modifier+2);
        if (!ret) ret = wl->Get(modifier);
        if (ret) return *ret;
    }

    static WDL_PtrList<ActionContext> empty;
    return empty;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Widget
////////////////////////////////////////////////////////////////////////////////////////////////////////
ZoneManager *Widget::GetZoneManager()
{
    return surface_->GetZoneManager();
}

void Widget::Configure(const WDL_PtrList<ActionContext> &contexts)
{
    for (int i = 0; i < feedbackProcessors_.GetSize(); ++i)
        feedbackProcessors_.Get(i)->Configure(contexts);
}

void  Widget::UpdateValue(const PropertyList &properties, double value)
{
    for (int i = 0; i < feedbackProcessors_.GetSize(); ++i)
        feedbackProcessors_.Get(i)->SetValue(properties, value);
}

void  Widget::UpdateValue(const PropertyList &properties, string value)
{
    for (int i = 0; i < feedbackProcessors_.GetSize(); ++i)
        feedbackProcessors_.Get(i)->SetValue(properties, value);
}

void Widget::RunDeferredActions()
{
    for (int i = 0; i < feedbackProcessors_.GetSize(); ++i)
        feedbackProcessors_.Get(i)->RunDeferredActions();
}

void  Widget::UpdateColorValue(rgba_color color)
{
    for (int i = 0; i < feedbackProcessors_.GetSize(); ++i)
        feedbackProcessors_.Get(i)->SetColorValue(color);
}

void Widget::SetXTouchDisplayColors(const string &zoneName, const string &colors)
{
    for (int i = 0; i < feedbackProcessors_.GetSize(); ++i)
        feedbackProcessors_.Get(i)->SetXTouchDisplayColors(zoneName, colors);
}

void Widget::RestoreXTouchDisplayColors()
{
    for (int i = 0; i < feedbackProcessors_.GetSize(); ++i)
        feedbackProcessors_.Get(i)->RestoreXTouchDisplayColors();
}

void  Widget::ForceClear()
{
    for (int i = 0; i < feedbackProcessors_.GetSize(); ++i)
        feedbackProcessors_.Get(i)->ForceClear();
}

void Widget::LogInput(double value)
{
    if (csi_->GetSurfaceInDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %s %f\n", GetSurface()->GetName(), GetName(), value);
        DAW::ShowConsoleMsg(buffer);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi_FeedbackProcessor
////////////////////////////////////////////////////////////////////////////////////////////////////////
void Midi_FeedbackProcessor::SendMidiSysExMessage(MIDI_event_ex_t *midiMessage)
{
    surface_->SendMidiSysExMessage(midiMessage);
}

void Midi_FeedbackProcessor::SendMidiMessage(int first, int second, int third)
{
    if (first != lastMessageSent_->midi_message[0] || second != lastMessageSent_->midi_message[1] || third != lastMessageSent_->midi_message[2])
        ForceMidiMessage(first, second, third);
}

void Midi_FeedbackProcessor::ForceMidiMessage(int first, int second, int third)
{
    lastMessageSent_->midi_message[0] = first;
    lastMessageSent_->midi_message[1] = second;
    lastMessageSent_->midi_message[2] = third;
    surface_->SendMidiMessage(first, second, third);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// OSC_FeedbackProcessor
////////////////////////////////////////////////////////////////////////////////////////////////////////
void OSC_FeedbackProcessor::SetColorValue(rgba_color &color)
{
    if (lastColor_ != color)
    {
        if (lastColor_ != color)
        {
            lastColor_ = color;

            char tmp[32];
            if (surface_->IsX32())
                X32SetColorValue(color);
            else
                surface_->SendOSCMessage(this, oscAddress_ + "/Color", color.to_string(tmp));
        }
    }
}

void OSC_FeedbackProcessor::X32SetColorValue(rgba_color &color)
{
    int surfaceColor = 0;
    int r = color.r;
    int g = color.g;
    int b = color.b;

    if (r == 64 && g == 64 && b == 64)                               surfaceColor = 8;    // BLACK
    else if (r > g && r > b)                                         surfaceColor = 1;    // RED
    else if (g > r && g > b)                                         surfaceColor = 2;    // GREEN
    else if (abs(r - g) < 30 && r > b && g > b)                      surfaceColor = 3;    // YELLOW
    else if (b > r && b > g)                                         surfaceColor = 4;    // BLUE
    else if (abs(r - b) < 30 && r > g && b > g)                      surfaceColor = 5;    // MAGENTA
    else if (abs(g - b) < 30 && g > r && b > r)                      surfaceColor = 6;    // CYAN
    else if (abs(r - g) < 30 && abs(r - b) < 30 && abs(g - b) < 30)  surfaceColor = 7;    // WHITE

    string oscAddress = "/ch/";
    if (widget_->GetChannelNumber() < 10)   oscAddress += '0';
    oscAddress += to_string(widget_->GetChannelNumber()) + "/config/color";
    surface_->SendOSCMessage(this, oscAddress, surfaceColor);
}

void OSC_FeedbackProcessor::ForceValue(const PropertyList &properties, double value)
{
    if (DAW::GetCurrentNumberOfMilliseconds() - GetWidget()->GetLastIncomingMessageTime() < 50) // adjust the 50 millisecond value to give you smooth behaviour without making updates sluggish
        return;

    lastDoubleValue_ = value;
    surface_->SendOSCMessage(this, oscAddress_, value);
}

void OSC_FeedbackProcessor::ForceValue(const PropertyList &properties, const string &value)
{
    lastStringValue_ = value;
    surface_->SendOSCMessage(this, oscAddress_, GetWidget()->GetSurface()->GetRestrictedLengthText(value));
}

void OSC_FeedbackProcessor::ForceClear()
{
    lastDoubleValue_ = 0.0;
    surface_->SendOSCMessage(this, oscAddress_, 0.0);
    
    lastStringValue_ = "";
    surface_->SendOSCMessage(this, oscAddress_, "");
}

void OSC_IntFeedbackProcessor::ForceValue(const PropertyList &properties, double value)
{
    lastDoubleValue_ = value;
    
    if (surface_->IsX32() && oscAddress_.find("/-stat/selidx") != string::npos)
    {
        if (value != 0.0)
            surface_->SendOSCMessage(this, "/-stat/selidx", widget_->GetChannelNumber() -1);
    }
    else
        surface_->SendOSCMessage(this, oscAddress_, (int)value);
}

void OSC_IntFeedbackProcessor::ForceClear()
{
    lastDoubleValue_ = 0.0;
    surface_->SendOSCMessage(this, oscAddress_, 0.0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// ZoneManager
////////////////////////////////////////////////////////////////////////////////////////////////////////
void ZoneManager::Initialize()
{
    PreProcessZones();

    if ( ! zoneFilePaths_.Exists("Home"))
    {
        MessageBox(g_hwnd, (string(surface_->GetName()) + " needs a Home Zone to operate, please recheck your installation").c_str(), ("CSI cannot find Home Zone for " + string(surface_->GetName())).c_str(), MB_OK);
        return;
    }
        
    WDL_PtrList<Navigator> navigators;
    navigators.Add(GetSelectedTrackNavigator());
    WDL_PtrList<Zone> dummy; // Needed to satisfy protcol, Home and FocusedFXParam have special Zone handling
    LoadZoneFile(zoneFilePaths_.Get("Home")->filePath, navigators, dummy, nullptr);
    if (zoneFilePaths_.Exists("FocusedFXParam"))
        LoadZoneFile(zoneFilePaths_.Get("FocusedFXParam")->filePath, navigators, dummy, nullptr);
    if (zoneFilePaths_.Exists("SurfaceFXLayout"))
        ProcessSurfaceFXLayout(zoneFilePaths_.Get("SurfaceFXLayout")->filePath, surfaceFXLayout_, surfaceFXLayoutTemplate_);
    if (zoneFilePaths_.Exists("FXLayouts"))
        ProcessFXLayouts(zoneFilePaths_.Get("FXLayouts")->filePath, fxLayouts_);
    if (zoneFilePaths_.Exists("FXPrologue"))
        ProcessFXBoilerplate(zoneFilePaths_.Get("FXPrologue")->filePath, fxPrologue_);
    if (zoneFilePaths_.Exists("FXEpilogue"))
        ProcessFXBoilerplate(zoneFilePaths_.Get("FXEpilogue")->filePath, fxEpilogue_);
    
    InitializeNoMapZone();
    InitializeFXParamsLearnZone();

    GoHome();
}

void ZoneManager::CheckFocusedFXState()
{
    int trackNumber = 0;
    int itemNumber = 0;
    int fxIndex = 0;
    
    int retval = DAW::GetFocusedFX2(&trackNumber, &itemNumber, &fxIndex);

    if ((retval & 1) && (fxIndex > -1))
    {
        MediaTrack *track = DAW::GetTrack(trackNumber);
        
        char fxName[BUFSZ];
        DAW::TrackFX_GetFXName(track, fxIndex, fxName, sizeof(fxName));

        if (learnFXName_ != "" && learnFXName_ != fxName)
        {
            string alias, learnalias;
            GetAlias(fxName,alias);
            GetAlias(learnFXName_.c_str(),learnalias);
            if (MessageBox(NULL, (string("You have now shifted focus to ") + alias + "\n\n" + learnalias + string(" has parameters that have not been saved\n\n Do you want to save them now ?")).c_str(), "Unsaved Learn FX Params", MB_YESNO) == IDYES)
            {
                SaveLearnedFXParams();
            }
            else
            {
                ClearLearnedFXParams();
                GoHome();
            }
        }
    }
    
    if (! isFocusedFXMappingEnabled_)
        return;
            
    if ((retval & 1) && (fxIndex > -1))
    {
        int lastRetval = -1;

        if (focusedFXDictionary_.Exists(trackNumber) && focusedFXDictionary_.Get(trackNumber)->Exists(fxIndex))
            lastRetval = focusedFXDictionary_.Get(trackNumber)->Get(fxIndex);
        
        if (lastRetval != retval)
        {
            if (retval == 1)
                GoFocusedFX();
            
            else if (retval & 4)
            {
                focusedFXZones_.Empty();
                needGarbageCollect_ = true;
            }
            
            if ( ! focusedFXDictionary_.Exists(trackNumber))
                focusedFXDictionary_.Insert(trackNumber, new WDL_IntKeyedArray<int>());
             
            if (focusedFXDictionary_.Exists(trackNumber))
                focusedFXDictionary_.Get(trackNumber)->Insert(fxIndex, retval);
        }
    }
}

void ZoneManager::AddListener(ControlSurface *surface)
{
    if (WDL_NOT_NORMALLY(!surface)) { return; }
    listeners_.Add(surface->GetZoneManager());
}

void ZoneManager::SetListenerCategories(const string &categoryList)
{
    vector<string> categoryTokens;
    GetTokens(categoryTokens, categoryList);
    
    for (int i = 0; i < (int)categoryTokens.size(); ++i)
    {
        if (categoryTokens[i] == "GoHome")
            listensToGoHome_ = true;
        if (categoryTokens[i] == "Sends")
            listensToSends_ = true;
        if (categoryTokens[i] == "Receives")
            listensToReceives_ = true;
        if (categoryTokens[i] == "FocusedFX")
            listensToFocusedFX_ = true;
        if (categoryTokens[i] == "FocusedFXParam")
            listensToFocusedFXParam_ = true;
        if (categoryTokens[i] == "FXMenu")
            listensToFXMenu_ = true;
        if (categoryTokens[i] == "LocalFXSlot")
            listensToLocalFXSlot_ = true;
        if (categoryTokens[i] == "SelectedTrackFX")
            listensToSelectedTrackFX_ = true;
        if (categoryTokens[i] == "Custom")
            listensToCustom_ = true;
        
        if (categoryTokens[i] == "Modifiers")
            surface_->SetListensToModifiers();
    }
}


void ZoneManager::GoFocusedFX()
{
    focusedFXZones_.Empty();
    
    int trackNumber = 0;
    int itemNumber = 0;
    int fxSlot = 0;
    MediaTrack *focusedTrack = nullptr;
    
    if (DAW::GetFocusedFX2(&trackNumber, &itemNumber, &fxSlot) == 1)
    {
        if (trackNumber > 0)
            focusedTrack = DAW::GetTrack(trackNumber);
        else if (trackNumber == 0)
            focusedTrack = GetMasterTrackNavigator()->GetTrack();
    }
    
    if (focusedTrack)
    {
        char FXName[BUFSZ];
        DAW::TrackFX_GetFXName(focusedTrack, fxSlot, FXName, sizeof(FXName));
        
        if (zoneFilePaths_.Exists(FXName))
        {
            WDL_PtrList<Navigator> navigators;
            navigators.Add(GetSurface()->GetPage()->GetFocusedFXNavigator());
            
            LoadZoneFile(zoneFilePaths_.Get(FXName)->filePath, navigators, focusedFXZones_, nullptr);
            
            for (int i = 0; i < focusedFXZones_.GetSize(); ++i)
            {
                focusedFXZones_.Get(i)->SetXTouchDisplayColors("White");
                focusedFXZones_.Get(i)->SetSlotIndex(fxSlot);
                focusedFXZones_.Get(i)->Activate();
            }
        }
    }
    else
        for (int i = 0; i < focusedFXZones_.GetSize(); ++i)
            focusedFXZones_.Get(i)->RestoreXTouchDisplayColors();

    needGarbageCollect_ = true;
}

void ZoneManager::GoSelectedTrackFX()
{
    if (homeZone_ != nullptr)
    {
        ClearFXMapping();
        ResetOffsets();
                
        homeZone_->GoAssociatedZone("SelectedTrackFX");
    }

    selectedTrackFXZones_.Empty();
    
    if (MediaTrack *selectedTrack = surface_->GetPage()->GetSelectedTrack())
    {
        for (int i = 0; i < DAW::TrackFX_GetCount(selectedTrack); i++)
        {
            char FXName[BUFSZ];
            
            DAW::TrackFX_GetFXName(selectedTrack, i, FXName, sizeof(FXName));
            
            if (zoneFilePaths_.Exists(FXName))
            {
                WDL_PtrList<Navigator> navigators;
                navigators.Add(GetSurface()->GetPage()->GetSelectedTrackNavigator());
                LoadZoneFile(zoneFilePaths_.Get(FXName)->filePath, navigators, selectedTrackFXZones_, nullptr);
                
                selectedTrackFXZones_.Get(selectedTrackFXZones_.GetSize() - 1)->SetSlotIndex(i);
                selectedTrackFXZones_.Get(selectedTrackFXZones_.GetSize() - 1)->Activate();
            }
        }
    }
    needGarbageCollect_ = true;
}

void ZoneManager::AutoMapFocusedFX()
{
    int trackNumber = 0;
    int itemNumber = 0;
    int fxSlot = 0;
    MediaTrack *track = nullptr;
    
    if (DAW::GetFocusedFX2(&trackNumber, &itemNumber, &fxSlot) == 1)
    {
        if (trackNumber > 0)
            track = DAW::GetTrack(trackNumber);
        
        if (track)
        {
            char fxName[BUFSZ];
            DAW::TrackFX_GetFXName(track, fxSlot, fxName, sizeof(fxName));
            if ( ! csi_->HaveFXSteppedValuesBeenCalculated(fxName))
                CalculateSteppedValues(fxName, track, fxSlot);
            AutoMapFX(fxName, track, fxSlot);
        }
    }
}

void ZoneManager::GoLearnFXParams(MediaTrack *track, int fxSlot)
{
    if (homeZone_ != nullptr)
    {
        ClearFXMapping();
        ResetOffsets();
                
        homeZone_->GoAssociatedZone("LearnFXParams");
    }
    
    if (track)
    {
        char fxName[BUFSZ];
        DAW::TrackFX_GetFXName(track, fxSlot, fxName, sizeof(fxName));
        
        if (zoneFilePaths_.Exists(fxName))
        {
            ifstream file(zoneFilePaths_.Get(fxName)->filePath);
             
            string line = "";
            
            if (getline(file, line))
            {
                vector<string> tokens;
                GetTokens(tokens, line);

                if (tokens.size() > 3 && tokens[3] == s_GeneratedByLearn)
                {
                    learnFXName_ = fxName;
                    GetExistingZoneParamsForLearn(fxName, track, fxSlot);
                    file.close();
                }
                else
                {
                    file.close();
                    
                    if (MessageBox(NULL, (zoneFilePaths_.Get(fxName)->alias + " already exists\n\n Do you want to delete it permanently and go into LearnMode ?").c_str(), "Zone Already Exists", MB_YESNO) == IDYES)
                    {
                        ClearLearnedFXParams();
                        RemoveZone(fxName);
                    }
                    else
                    {
                        return;
                    }
                }
            }
        }
    }
}

void ZoneManager::GoFXSlot(MediaTrack *track, Navigator *navigator, int fxSlot)
{
    if (fxSlot > DAW::TrackFX_GetCount(track) - 1)
        return;
    
    char fxName[BUFSZ];
    
    DAW::TrackFX_GetFXName(track, fxSlot, fxName, sizeof(fxName));
    
    if ( ! csi_->HaveFXSteppedValuesBeenCalculated(fxName))
        CalculateSteppedValues(fxName, track, fxSlot);

    if (zoneFilePaths_.Exists(fxName))
    {
        WDL_PtrList<Navigator> navigators;
        navigators.Add(navigator);
        
        LoadZoneFile(zoneFilePaths_.Get(fxName)->filePath, navigators, fxSlotZones_, nullptr);
        
        if (fxSlotZones_.GetSize() > 0)
        {
            fxSlotZones_.Get(fxSlotZones_.GetSize() - 1)->SetSlotIndex(fxSlot);
            fxSlotZones_.Get(fxSlotZones_.GetSize() - 1)->Activate();
        }
    }
    else if (noMapZone_ != nullptr)
    {
        DAW::TrackFX_SetOpen(track, fxSlot, true);
        
        noMapZone_->SetSlotIndex(fxSlot);
        noMapZone_->Activate();
    }
    needGarbageCollect_ = true;
}

void ZoneManager::UpdateCurrentActionContextModifiers()
{    
    if (focusedFXParamZone_ != nullptr)
        focusedFXParamZone_->UpdateCurrentActionContextModifiers();
    
    for (int i = 0; i < focusedFXZones_.GetSize(); ++i)
        focusedFXZones_.Get(i)->UpdateCurrentActionContextModifiers();
    
    for (int i = 0; i < selectedTrackFXZones_.GetSize(); ++i)
        selectedTrackFXZones_.Get(i)->UpdateCurrentActionContextModifiers();
    
    for (int i = 0; i < fxSlotZones_.GetSize(); ++i)
        fxSlotZones_.Get(i)->UpdateCurrentActionContextModifiers();
    
    if (homeZone_ != nullptr)
        homeZone_->UpdateCurrentActionContextModifiers();
}

void ZoneManager::EraseLastTouchedControl()
{
    if (lastTouched_ != nullptr)
    {
        if (fxLayout_ != nullptr && fxLayoutFileLines_.size() > 0)
        {
            Widget *widget = lastTouched_->fxParamWidget;
            if (widget)
            {
                for (int i = 0; i < fxLayout_->GetActionContexts(widget).GetSize(); ++i)
                    SetParamNum(widget, 1);
                
                int modifier = fxLayout_->GetModifier(widget);
                
                if (controlDisplayAssociations_.Exists(modifier) && controlDisplayAssociations_.Get(modifier)->Exists(widget))
                    SetParamNum(controlDisplayAssociations_.Get(modifier)->Get(widget), 1);
            }
        }

        lastTouched_->isLearned = false;
        lastTouched_->paramNumber = 0;
        lastTouched_->paramName = "";
        lastTouched_->params = "";
        lastTouched_->track = nullptr;
        lastTouched_->fxSlotNum = 0;
        
        lastTouched_ = nullptr;
    }
}

void ZoneManager::SaveTemplatedFXParams()
{
    if (learnFXName_ != "" && fxLayout_ != nullptr && fxLayoutFileLines_.size() > 0)
    {
        size_t pos = 0;
        while ((pos = fxLayoutFileLines_[0].find(fxLayout_->GetName(), pos)) != std::string::npos)
        {
            fxLayoutFileLines_[0].replace(pos, (int)strlen(fxLayout_->GetName()), learnFXName_);
            pos += learnFXName_.length();
        }
        
        string alias;
        GetAlias(learnFXName_.c_str(), alias);

        fxLayoutFileLines_[0] += " \"" + alias + "\" \n\n";
        
        string path = "";
         
        if (zoneFilePaths_.Exists(learnFXName_.c_str()))
        {
            path = zoneFilePaths_.Get(learnFXName_.c_str())->filePath;
            alias = zoneFilePaths_.Get(learnFXName_.c_str())->alias;
        }
        else
        {
            path = DAW::GetResourcePath() + string("/CSI/Zones/") + fxZoneFolder_ + "/TemplatedFXZones";
            
            RecursiveCreateDirectory(path.c_str(),0);

            GetAlias(learnFXName_.c_str(),alias);
            
            path += "/" + ReplaceAllWith(learnFXName_, s_BadFileChars, "_") + ".zon";
            
            CSIZoneInfo *info = new CSIZoneInfo();
            info->filePath = path;
            info->alias = alias;
            
            AddZoneFilePath(learnFXName_, info);
            surface_->GetPage()->AddZoneFilePath(surface_, fxZoneFolder_, learnFXName_, info);
        }
        
        ofstream fxZone(path);

        if (fxZone.is_open())
        {
            for (int i = 0; i < (int)fxLayoutFileLines_.size(); ++i)
            {
                string ending = "";
                
                string lineEnding = "\n";

                if (fxLayoutFileLines_[i].length() >= lineEnding.length())
                    ending = fxLayoutFileLines_[i].substr(fxLayoutFileLines_[i].length() - lineEnding.length(), lineEnding.length());

                if (ending[ending.length() - 1] == '\r')
                    fxLayoutFileLines_[i] = fxLayoutFileLines_[i].substr(0, fxLayoutFileLines_[i].length() - 1);
                
                if (ending != lineEnding)
                    fxLayoutFileLines_[i] += "\n";
                
                fxZone << fxLayoutFileLines_[i];
            }
            
            fxZone.close();
        }

        ClearLearnedFXParams();
        GoHome();
    }
}

void ZoneManager::SaveLearnedFXParams()
{
    if (learnFXName_ != "")
    {
        string path = "";
        string alias = "";
        
        if (zoneFilePaths_.Exists(learnFXName_.c_str()))
        {
            path = zoneFilePaths_.Get(learnFXName_.c_str())->filePath;
            alias = zoneFilePaths_.Get(learnFXName_.c_str())->alias;
        }
        else
        {
            path = DAW::GetResourcePath() + string("/CSI/Zones/") + fxZoneFolder_ + "/AutoGeneratedFXZones";
            
            RecursiveCreateDirectory(path.c_str(),0);

            GetAlias(learnFXName_.c_str(),alias);
            
            path += "/" + ReplaceAllWith(learnFXName_, s_BadFileChars, "_") + ".zon";
            
            CSIZoneInfo *info = new CSIZoneInfo();
            info->filePath = path;
            info->alias = alias;
            
            AddZoneFilePath(learnFXName_, info);
            surface_->GetPage()->AddZoneFilePath(surface_, fxZoneFolder_, learnFXName_, info);
        }
        
        string nameDisplayParams = "";
        string valueDisplayParams = "";

        if (surfaceFXLayout_.size() > 2)
        {
            if (surfaceFXLayout_[1].size() > 2)
                for (int i = 2; i < surfaceFXLayout_[1].size(); i++)
                    nameDisplayParams += " " + surfaceFXLayout_[1][i];

            if (surfaceFXLayout_[2].size() > 2)
                for (int i = 2; i < surfaceFXLayout_[2].size(); i++)
                    valueDisplayParams += " " + surfaceFXLayout_[2][i];
        }
        
        ofstream fxZone(path);

        if (fxZone.is_open())
        {
            fxZone << "Zone \"" + learnFXName_ + "\" \"" + alias + "\" \"" + s_GeneratedByLearn + "\"\n";
            
            for (int i = 0; i < (int)fxPrologue_.size(); ++i)
                fxZone << "\t" + fxPrologue_[i] + "\n";
                   
            fxZone << "\n" + s_BeginAutoSection + "\n";

            if (homeZone_->GetLearnFXParamsZone())
            {
                const WDL_IntKeyedArray< WDL_StringKeyedArray<LearnFXCell *> * > &lc = homeZone_->GetLearnFXParamsZone()->GetLearnFXCells();

                for (int wc = 0; wc < lc.GetSize(); wc++)
                {
                    int modifier = 0;
                    const WDL_StringKeyedArray<LearnFXCell *> * const widgetCells = lc.Enumerate(wc,&modifier);
                    if (WDL_NOT_NORMALLY(widgetCells == NULL)) continue;

                    string modifierStr = ModifierManager::GetModifierString(modifier);
                    
                    for (int ci = 0; ci < widgetCells->GetSize(); ci ++)
                    {
                        const LearnFXCell * const cell = widgetCells->Enumerate(ci);
                        if (WDL_NOT_NORMALLY(cell == NULL)) continue;
                        bool cellHasDisplayWidgetsDefined = false;
                        
                        for (int i = 0; i < cell->fxParamWidgets.GetSize(); i++)
                        {
                            LearnInfo *info = GetLearnInfo(cell->fxParamWidgets.Get(i), modifier);
                            
                            if (info == nullptr)
                                continue;
                            
                            if (info->isLearned)
                            {
                                cellHasDisplayWidgetsDefined = true;
                                
                                fxZone << "\t" + modifierStr + cell->fxParamWidgets.Get(i)->GetName() + "\tFXParam " + to_string(info->paramNumber) + " " + info->params + "\n";
                                fxZone << "\t" + modifierStr + cell->fxParamNameDisplayWidget->GetName() + "\tFixedTextDisplay \"" + info->paramName + "\"" + nameDisplayParams + "\n";
                                fxZone << "\t" + modifierStr + cell->fxParamValueDisplayWidget->GetName() + "\tFXParamValueDisplay " + to_string(info->paramNumber) + valueDisplayParams + "\n\n";
                            }
                            else if (i == cell->fxParamWidgets.GetSize() - 1 && ! cellHasDisplayWidgetsDefined)
                            {
                                fxZone << "\t" + modifierStr + cell->fxParamWidgets.Get(i)->GetName() + "\tNoAction\n";
                                fxZone << "\t" + modifierStr + cell->fxParamNameDisplayWidget->GetName() + "\tNoAction\n";
                                fxZone << "\t" + modifierStr + cell->fxParamValueDisplayWidget->GetName() + "\tNoAction\n\n";
                            }
                            else
                            {
                                fxZone << "\t" + modifierStr + cell->fxParamWidgets.Get(i)->GetName() + "\tNoAction\n";
                                fxZone << "\tNullDisplay\tNoAction\n";
                                fxZone << "\tNullDisplay\tNoAction\n\n";
                            }
                        }
                        
                        fxZone << "\n";
                    }
                }
            }
            
            fxZone << s_EndAutoSection + "\n";
                    
            for (int i = 0; i < (int)fxEpilogue_.size(); ++i)
                fxZone << "\t" + fxEpilogue_[i] + "\n";

            fxZone << "ZoneEnd\n\n";
            
            for (int i = 0; i < (int)paramList_.size(); ++i)
                fxZone << paramList_[i] + "\n";
            
            fxZone.close();
        }
        
        ClearLearnedFXParams();
        GoHome();
    }
}

LearnInfo *ZoneManager::GetLearnInfo(Widget *widget)
{
    const WDL_TypedBuf<int> &modifiers = surface_->GetModifiers();

    if (modifiers.GetSize() > 0)
        return GetLearnInfo(widget, modifiers.Get()[0]);
    else
        return NULL;
}

LearnInfo *ZoneManager::GetLearnInfo(Widget *widget, int modifier)
{
    WDL_IntKeyedArray<LearnInfo *> *modifiers = learnedFXParams_.Get(widget);
    return modifiers ? modifiers->Get(modifier) : NULL;
}

void ZoneManager::GetWidgetNameAndModifiers(const string &line, int listSlotIndex, string &cell, string &paramWidgetName, string &paramWidgetFullName, vector<string> &modifiers, int &modifier, vector<FXParamLayoutTemplate> &layoutTemplates)
{
    istringstream modifiersAndWidgetName(line);
    string modifiersAndWidgetNameToken;
    
    while (getline(modifiersAndWidgetName, modifiersAndWidgetNameToken, '+'))
        modifiers.push_back(modifiersAndWidgetNameToken);
        
    modifier = GetModifierValue(modifiers);
    
    paramWidgetFullName = modifiers[modifiers.size() - 1];

    paramWidgetName = paramWidgetFullName.substr(0, paramWidgetFullName.length() - layoutTemplates[listSlotIndex].suffix.length());
    
    cell = layoutTemplates[listSlotIndex].suffix;
}

int ZoneManager::GetModifierValue(const vector<string> &modifierTokens)
{
    ModifierManager modifierManager(csi_);

    return modifierManager.GetModifierValue(modifierTokens);
}

void ZoneManager::InitializeNoMapZone()
{
    if (surfaceFXLayout_.size() != 3)
        return;
    
    if (GetZoneFilePaths().Exists("NoMap"))
    {
        WDL_PtrList<Navigator> navigators;
        navigators.Add(GetSelectedTrackNavigator());
        
        WDL_PtrList<Zone> zones;
        
        LoadZoneFile(GetZoneFilePaths().Get("NoMap")->filePath, navigators, zones, nullptr);
        
        if (zones.GetSize() > 0)
            noMapZone_ = zones.Get(0);
        
        if (noMapZone_ != nullptr)
        {
            const WDL_PointerKeyedArray<Widget*, bool> &wl = noMapZone_->GetWidgets();
            WDL_PointerKeyedArray<Widget*, bool> usedWidgets;
            usedWidgets.CopyContents(wl); // since noMapZone_->GetWidgets() may change during this initialization, make a copy. making the copy might be unnecessary though

            vector<string> paramWidgets;

            for (int i = 0; i < (int)surfaceFXLayoutTemplate_.size(); ++i)
                if (surfaceFXLayoutTemplate_[i].size() > 0 && surfaceFXLayoutTemplate_[i][0] == "WidgetTypes")
                    for (int j = 1; j < surfaceFXLayoutTemplate_[i].size(); j++)
                        paramWidgets.push_back(surfaceFXLayoutTemplate_[i][j]);
            
            string nameDisplayWidget = "";
            if (surfaceFXLayout_[1].size() > 0)
                nameDisplayWidget = surfaceFXLayout_[1][0];
            
            string valueDisplayWidget = "";
            if (surfaceFXLayout_[2].size() > 0)
                valueDisplayWidget = surfaceFXLayout_[2][0];

            for (int i = 0; i < (int)fxLayouts_.size(); ++i)
            {
                int modifier = GetModifierValue(fxLayouts_[i].GetModifierTokens());
                
                if (modifier != 0)
                    continue;
                
                for (int j = 1; j <= fxLayouts_[i].channelCount_; j++)
                {
                    string cellAdress = fxLayouts_[i].suffix_ + to_string(j);
                    
                    Widget *widget = GetSurface()->GetWidgetByName(nameDisplayWidget + cellAdress);
                    if (widget == NULL || usedWidgets.Exists(widget))
                        continue;
                    noMapZone_->AddWidget(widget, widget->GetName());
                    ActionContext *context = csi_->GetActionContext("NoAction", widget, noMapZone_, 0);
                    context->SetProvideFeedback(true);
                    noMapZone_->AddActionContext(widget, modifier, context);

                    widget = GetSurface()->GetWidgetByName(valueDisplayWidget + cellAdress);
                    if (widget == nullptr || usedWidgets.Exists(widget))
                        continue;
                    noMapZone_->AddWidget(widget, widget->GetName());
                    context = csi_->GetActionContext("NoAction", widget, noMapZone_, 0);
                    context->SetProvideFeedback(true);
                    noMapZone_->AddActionContext(widget, modifier, context);
                    
                    for (int k = 0; k < (int)paramWidgets.size(); ++k)
                    {
                        widget = GetSurface()->GetWidgetByName(paramWidgets[k] + cellAdress);
                        if (widget == NULL || usedWidgets.Exists(widget))
                            continue;
                        noMapZone_->AddWidget(widget, widget->GetName());
                        context = csi_->GetActionContext("NoAction", widget, noMapZone_, 0);
                        noMapZone_->AddActionContext(widget, modifier, context);
                    }
                }
            }
        }
    }
}

void ZoneManager::InitializeFXParamsLearnZone()
{
    if (surfaceFXLayout_.size() != 3)
        return;
    
    if (homeZone_ != nullptr)
    {
        Zone *zone = homeZone_->GetLearnFXParamsZone();
        if (zone)
        {
            vector<string> paramWidgets;
            vector<string> widgetParams;

            for (int i = 0; i < (int)surfaceFXLayoutTemplate_.size(); ++i)
                if (surfaceFXLayoutTemplate_[i].size() > 0 && surfaceFXLayoutTemplate_[i][0] == "WidgetTypes")
                    for (int j = 1; j < surfaceFXLayoutTemplate_[i].size(); j++)
                        paramWidgets.push_back(surfaceFXLayoutTemplate_[i][j]);

            if (surfaceFXLayout_[0].size() > 2)
                for (int i = 2; i < surfaceFXLayout_[0].size(); i++)
                    widgetParams.push_back(surfaceFXLayout_[0][i]);
            
            
            string nameDisplayWidget = "";
            vector<string> nameDisplayParams;

            if (surfaceFXLayout_[1].size() > 0)
                nameDisplayWidget = surfaceFXLayout_[1][0];

            if (surfaceFXLayout_[1].size() > 2)
                for (int i = 2; i < surfaceFXLayout_[1].size(); i++)
                    nameDisplayParams.push_back(surfaceFXLayout_[1][i]);

            
            string valueDisplayWidget = "";
            vector<string> valueDisplayParams;

            if (surfaceFXLayout_[2].size() > 0)
                valueDisplayWidget = surfaceFXLayout_[2][0];

            if (surfaceFXLayout_[2].size() > 2)
                for (int i = 2; i < surfaceFXLayout_[2].size(); i++)
                    valueDisplayParams.push_back(surfaceFXLayout_[2][i]);

            if (paramWidgets.size() > 0)
            {
                for (int i = 0; i < (int)fxLayouts_.size(); ++i)
                {
                    int modifier = GetModifierValue(fxLayouts_[i].GetModifierTokens());
                    
                    for (int j = 1; j <= fxLayouts_[i].channelCount_; j++)
                    {
                        LearnFXCell cell;
                        
                        char cellAddress[BUFSZ];
                        snprintf(cellAddress, sizeof(cellAddress), "%s%d",fxLayouts_[i].suffix_.c_str(),j);
                        
                        Widget *widget = GetSurface()->GetWidgetByName(nameDisplayWidget + cellAddress);
                        if (widget == NULL)
                            continue;
                        cell.fxParamNameDisplayWidget = widget;
                        zone->AddWidget(widget, widget->GetName());
                        ActionContext *context = csi_->GetLearnFXActionContext("LearnFXParamNameDisplay", widget, zone, nameDisplayParams);
                        context->SetProvideFeedback(true);
                        context->SetCellAddress(cellAddress);
                        zone->AddActionContext(widget, modifier, context);

                        widget = GetSurface()->GetWidgetByName(valueDisplayWidget + cellAddress);
                        if (widget == NULL)
                            continue;
                        cell.fxParamValueDisplayWidget = widget;
                        zone->AddWidget(widget, widget->GetName());
                        context = csi_->GetLearnFXActionContext("LearnFXParamValueDisplay", widget, zone, valueDisplayParams);
                        context->SetProvideFeedback(true);
                        context->SetCellAddress(cellAddress);
                        zone->AddActionContext(widget, modifier, context);
                        
                        for (int k = 0; k < (int)paramWidgets.size(); ++k)
                        {
                            widget = GetSurface()->GetWidgetByName(paramWidgets[k] + cellAddress);
                            if (widget == NULL)
                                continue;
                            cell.fxParamWidgets.Add(widget);
                            zone->AddWidget(widget, widget->GetName());
                            context = csi_->GetLearnFXActionContext("LearnFXParam", widget, zone, widgetParams);
                            context->SetProvideFeedback(true);
                            zone->AddActionContext(widget, modifier, context);

                            LearnInfo *info = new LearnInfo(widget, cellAddress);
                            WDL_IntKeyedArray<LearnInfo*> *modifiers = learnedFXParams_.Get(widget);
                            if (modifiers == NULL)
                            {
                                modifiers = new WDL_IntKeyedArray<LearnInfo*>(disposeLearnInfo);
                                learnedFXParams_.Insert(widget, modifiers);
                            }
                            modifiers->Insert(modifier,info);
                        }
                        
                        zone->AddLearnFXCell(modifier, cellAddress, cell);
                    }
                }
            }
        }
    }
}

void ZoneManager::GetExistingZoneParamsForLearn(const string &fxName, MediaTrack *track, int fxSlotNum)
{
    zoneDef_.fullPath = zoneFilePaths_.Get(fxName.c_str())->filePath;
    vector<FXParamLayoutTemplate> layoutTemplates = GetFXLayoutTemplates();
        
    UnpackZone(zoneDef_, layoutTemplates);
    
    for (int i = 0; i < (int)zoneDef_.paramDefs.size(); ++i)
    {
        for (int j = 0; j < (int)zoneDef_.paramDefs[i].definitions.size(); ++j)
        {
            Widget *widget = surface_->GetWidgetByName(zoneDef_.paramDefs[i].definitions[j].paramWidgetFullName);
            if (widget)
            {
                if (LearnInfo *info = GetLearnInfo(widget, zoneDef_.paramDefs[i].definitions[j].modifier))
                {
                    if (zoneDef_.paramDefs[i].definitions[j].paramNumber != "" && zoneDef_.paramDefs[i].definitions[j].paramNameDisplayWidget != "NullDisplay")
                    {
                        info->isLearned = true;
                        info->paramName = zoneDef_.paramDefs[i].definitions[j].paramName;
                        info->track = track;
                        info->fxSlotNum =fxSlotNum;
                        info->paramNumber = stoi(zoneDef_.paramDefs[i].definitions[j].paramNumber);

                        if (zoneDef_.paramDefs[i].definitions[j].steps.size() > 0)
                        {
                            info->params = "[ ";
                            
                            for (int k = 0; k < (int)zoneDef_.paramDefs[i].definitions[j].steps.size(); ++k)
                            {
                                char tmp[BUFSZ];
                                info->params += format_number(zoneDef_.paramDefs[i].definitions[j].steps[k], tmp, sizeof(tmp));
                                info->params += "  ";
                            }
                            
                            info->params += "]";
                            
                            Zone *learnZone = homeZone_->GetLearnFXParamsZone();
                            if (learnZone)
                            {
                                const vector<double> &steps = zoneDef_.paramDefs[i].definitions[j].steps;
                                
                                for (int k = 0; k < learnZone->GetActionContexts(widget, zoneDef_.paramDefs[i].definitions[j].modifier).GetSize(); ++k)
                                    learnZone->GetActionContexts(widget, zoneDef_.paramDefs[i].definitions[j].modifier).Get(k)->SetStepValues(steps);
                            }
                        }
                        
                        if (zoneDef_.paramDefs[i].definitions[j].paramWidget.find("Rotary") != string::npos && zoneDef_.paramDefs[i].definitions[j].paramWidget.find("Push") == string::npos)
                        {
                            if (surfaceFXLayout_.size() > 0 && surfaceFXLayout_[0].size() > 2 && surfaceFXLayout_[0][0] == "Rotary")
                                for (int k = 2; k < surfaceFXLayout_[0].size(); k++)
                                    info->params += " " + surfaceFXLayout_[0][k];
                        }
                    }
                }
            }
        }
    }
}

void ZoneManager::GoFXLayoutZone(const char *zoneName, int slotIndex)
{
    if (noMapZone_ != nullptr)
        noMapZone_->Deactivate();

    if (homeZone_ != nullptr)
    {
        ClearFXMapping();

        fxLayoutFileLines_.clear();
        fxLayoutFileLinesOriginal_.clear();

        controlDisplayAssociations_.DeleteAll();

        homeZone_->GoAssociatedZone(zoneName, slotIndex);
        
        fxLayout_ = homeZone_->GetFXLayoutZone(zoneName);
        
        if (zoneFilePaths_.Exists(zoneName) && fxLayout_ != nullptr)
        {
            ifstream file(zoneFilePaths_.Get(zoneName)->filePath);
            
            for (string line; getline(file, line) ; )
            {
                if (line.find("|") != string::npos && fxLayoutFileLines_.size() > 0)
                {
                    vector<string> tokens;
                    GetTokens(tokens, line);

                    if (tokens.size() > 1 && tokens[1] == "FXParamValueDisplay") // This line is a display definition
                    {
                        if (fxLayoutFileLines_.back().find("|") != string::npos)
                        {
                            vector<string> previousLineTokens;
                            GetTokens(previousLineTokens, fxLayoutFileLines_.back());

                            if (previousLineTokens.size() > 1 && previousLineTokens[1] == "FXParam") // The previous line was a control Widget definition
                            {
                                istringstream ControlModifiersAndWidgetName(previousLineTokens[0]);
                                vector<string> modifierTokens;
                                string modifierToken;
                                
                                while (getline(ControlModifiersAndWidgetName, modifierToken, '+'))
                                    modifierTokens.push_back(modifierToken);
                                
                                int modifier = surface_->GetModifierManager()->GetModifierValue(modifierTokens);

                                Widget *controlWidget = surface_->GetWidgetByName(modifierTokens[modifierTokens.size() - 1]);
                                
                                istringstream displayModifiersAndWidgetName(tokens[0]);

                                modifierTokens.clear();
                                
                                while (getline(displayModifiersAndWidgetName, modifierToken, '+'))
                                    modifierTokens.push_back(modifierToken);

                                Widget *displayWidget = surface_->GetWidgetByName(modifierTokens[modifierTokens.size() - 1]);

                                if (controlWidget && displayWidget)
                                {
                                    if ( ! controlDisplayAssociations_.Exists(modifier))
                                        controlDisplayAssociations_.Insert(modifier, new WDL_PointerKeyedArray<Widget*, Widget*>());
                                    
                                    if (controlDisplayAssociations_.Exists(modifier))
                                        controlDisplayAssociations_.Get(modifier)->Insert(controlWidget, displayWidget);
                                }
                            }
                        }
                    }
                }
                
                fxLayoutFileLines_.push_back(line);
                fxLayoutFileLinesOriginal_.push_back(line);
            }
        }
    }
}

void ZoneManager::WidgetMoved(ActionContext *context)
{
    if (fxLayoutFileLines_.size() < 1)
        return;
    
    if (context->GetZone() != fxLayout_)
        return;
    
    MediaTrack *track = nullptr;
    
    LearnInfo *info = GetLearnInfo(context->GetWidget());
    
    if (info == nullptr)
        return;
    
    if (! info->isLearned)
    {
        int trackNum = 0;
        int fxSlotNum = 0;
        int fxParamNum = 0;

        if (DAW::GetLastTouchedFX(&trackNum, &fxSlotNum, &fxParamNum))
        {
            track = DAW::GetTrack(trackNum);
            
            if (track == nullptr)
                return;
            
            char fxName[BUFSZ];
            DAW::TrackFX_GetFXName(track, fxSlotNum, fxName, sizeof(fxName));
            learnFXName_ = fxName;
                                                                    
            string paramStr = "";
            
            if (strstr(context->GetWidget()->GetName(), "Fader") == NULL)
            {
                if (csi_->GetSteppedValueCount(fxName, fxParamNum) == 0)
                    context->GetSurface()->GetZoneManager()->CalculateSteppedValue(fxName, track, fxSlotNum, fxParamNum);
                
                int numSteps = csi_->GetSteppedValueCount(fxName, fxParamNum);
                
                if (strstr(context->GetWidget()->GetName(), "Push") != NULL)
                {
                    if (numSteps == 0)
                        numSteps = 2;
                }
                
                if (numSteps > 1)
                {
                    vector<double> stepValues;
                    GetParamStepsValues(stepValues, numSteps);
                    context->SetStepValues(stepValues);

                    string steps;
                    GetParamStepsString(steps, numSteps);
                    paramStr = "[ " + steps + "]";
                }
            }
           
            Widget *widget = context->GetWidget();
            
            SetParamNum(widget, fxParamNum);
            
            int modifier = fxLayout_->GetModifier(widget);
            
            if (controlDisplayAssociations_.Exists(modifier) && controlDisplayAssociations_.Get(modifier)->Exists(widget))
                SetParamNum(controlDisplayAssociations_.Get(modifier)->Get(widget), fxParamNum);

            info->isLearned = true;
            info->paramNumber = fxParamNum;
            char tmp[BUFSZ];
            info->paramName = DAW::TrackFX_GetParamName(DAW::GetTrack(trackNum), fxSlotNum, fxParamNum, tmp, sizeof(tmp));
            info->params = paramStr;
            info->track = DAW::GetTrack(trackNum);
            info->fxSlotNum = fxSlotNum;
        }
    }
    
    lastTouched_ = info;
}

void ZoneManager::SetParamNum(Widget *widget, int fxParamNum)
{
    fxLayout_->SetFXParamNum(widget, fxParamNum);

    // Now modify the in memory file
    
    int modifier = fxLayout_->GetModifier(widget);

    int index = 0;
    
    for (int ln = 0; ln < (int)fxLayoutFileLines_.size(); ln ++ )
    {
        string &line = fxLayoutFileLines_[ln];
        if (line.find(widget->GetName()) != string::npos)
        {
            istringstream fullLine(line);
            vector<string> tokens;
            string tltoken;
            
            while (getline(fullLine, tltoken, '+'))
                tokens.push_back(tltoken);

            if (tokens.size() < 1)
                continue;
            
            istringstream modifiersAndWidgetName(tokens[0]);
            
            tokens.clear();

            while (getline(modifiersAndWidgetName, tltoken, '+'))
                tokens.push_back(tltoken);

            int lineModifier = surface_->GetModifierManager()->GetModifierValue(tokens);

            if (modifier == lineModifier)
            {
                if (line.find("|") == string::npos)
                {
                    line = fxLayoutFileLinesOriginal_[index];
                }
                else
                {
                    istringstream layoutLine(line);
                    vector<string> lineTokens;
                    string token;
                    
                    while (getline(layoutLine, token, '|'))
                        lineTokens.push_back(token);
                    
                    string replacementString = " " + to_string(fxParamNum) + " ";
                    
                    if (widget && lineTokens.size() > 1)
                    {
                        LearnInfo *info = GetLearnInfo(widget);
                        
                        if (info != nullptr && info->params.length() != 0 && lineTokens[1].find("[") == string::npos)
                            replacementString += " " + info->params + " ";
                    }
                    
                    if (lineTokens.size() > 0)
                        line = lineTokens[0] + replacementString + (lineTokens.size() > 1 ? lineTokens[1] : "");
                }
            }
        }
        
        index++;
    }
}

void ZoneManager::DoLearn(ActionContext *context, double value)
{    
    if (value == 0.0)
        return;
    
    int trackNum = 0;
    int fxSlotNum = 0;
    int fxParamNum = 0;

    MediaTrack *track = nullptr;
    
    LearnInfo *info = GetLearnInfo(context->GetWidget());
    
    if (info == nullptr)
        return;
    
    if (! info->isLearned)
    {
        if (DAW::GetLastTouchedFX(&trackNum, &fxSlotNum, &fxParamNum))
        {
            track = DAW::GetTrack(trackNum);
            
            char fxName[BUFSZ];
            DAW::TrackFX_GetFXName(track, fxSlotNum, fxName, sizeof(fxName));
            
            if (paramList_.size() == 0)
                for (int i = 0; i < DAW::TrackFX_GetNumParams(track, fxSlotNum); i++)
                {
                    char tmp[BUFSZ], tmp2[BUFSZ];
                    snprintf(tmp2,sizeof(tmp2),"%d %s",i,DAW::TrackFX_GetParamName(track, fxSlotNum, i, tmp, sizeof(tmp)));
                    paramList_.push_back(tmp2);
                }
                                            
            string paramStr = "";
            
            if (strstr(context->GetWidget()->GetName(), "Fader") == NULL)
            {
                if (csi_->GetSteppedValueCount(fxName, fxParamNum) == 0)
                    context->GetSurface()->GetZoneManager()->CalculateSteppedValue(fxName, track, fxSlotNum, fxParamNum);
                
                int numSteps = csi_->GetSteppedValueCount(fxName, fxParamNum);
                
                if (strstr(context->GetWidget()->GetName(), "Push") != NULL)
                {
                    if (numSteps == 0)
                        numSteps = 2;
                }

                if (numSteps > 1)
                {
                    vector<double> stepValues;
                    GetParamStepsValues(stepValues, numSteps);
                    context->SetStepValues(stepValues);

                    string steps;
                    GetParamStepsString(steps, numSteps);
                    paramStr = "[ " + steps + "]";
                }
                
                if (strstr(context->GetWidget()->GetName(), "Rotary") != NULL && strstr(context->GetWidget()->GetName(), "Push") == NULL)
                {
                    if (surfaceFXLayout_.size() > 0 && surfaceFXLayout_[0].size() > 2 && surfaceFXLayout_[0][0] == "Rotary")
                        for (int i = 2; i < surfaceFXLayout_[0].size(); i++)
                            paramStr += " " + surfaceFXLayout_[0][i];
                }
            }
           
            int currentModifier = 0;
            
            if (surface_->GetModifiers().GetSize() > 0)
                currentModifier = surface_->GetModifiers().Get()[0];

            for (int i = 0; i < learnedFXParams_.GetSize(); i ++)
            {
                WDL_IntKeyedArray<LearnInfo *> *modifiers = learnedFXParams_.Enumerate(i);
                if (WDL_NORMALLY(modifiers != NULL)) 
                {
                    LearnInfo *widgetInfo = modifiers->Get(currentModifier);
                    if (widgetInfo && widgetInfo->cellAddress == info->cellAddress)
                    {
                        widgetInfo->isLearned = false;
                        widgetInfo->paramNumber = 0;
                        widgetInfo->paramName = "";
                        widgetInfo->params = "";
                        widgetInfo->track = nullptr;
                        widgetInfo->fxSlotNum = 0;
                    }
                }
            }

            info->isLearned = true;
            info->paramNumber = fxParamNum;
            char tmp[BUFSZ];
            info->paramName = DAW::TrackFX_GetParamName(DAW::GetTrack(trackNum), fxSlotNum, fxParamNum, tmp, sizeof(tmp));
            info->params = paramStr;
            info->track = DAW::GetTrack(trackNum);
            info->fxSlotNum = fxSlotNum;
        }
    }
    else
    {
        lastTouched_ = info;
                       
        DAW::TrackFX_SetParam(info->track, info->fxSlotNum, info->paramNumber, value);
    }
}

void ZoneManager::RemapAutoZone()
{
    if (focusedFXZones_.GetSize() == 1)
    {
        if (::RemapAutoZoneDialog(this, focusedFXZones_.Get(0)->GetSourceFilePath()))
        {
            PreProcessZoneFile(focusedFXZones_.Get(0)->GetSourceFilePath(), this);
            GoFocusedFX();
        }
    }
    else if (fxSlotZones_.GetSize() == 1)
    {
        if (::RemapAutoZoneDialog(this, fxSlotZones_.Get(0)->GetSourceFilePath()))
        {
            WDL_PtrList<Navigator> navigators;
            navigators.Add(fxSlotZones_.Get(0)->GetNavigator());
            
            string filePath = fxSlotZones_.Get(0)->GetSourceFilePath();
            int slotNumber = fxSlotZones_.Get(0)->GetSlotIndex();

            fxSlotZones_.Empty();
            
            PreProcessZoneFile(filePath, this);
            LoadZoneFile(filePath, navigators, fxSlotZones_, nullptr);
            
            fxSlotZones_.Get(fxSlotZones_.GetSize() - 1)->SetSlotIndex(slotNumber);
            fxSlotZones_.Get(fxSlotZones_.GetSize() - 1)->Activate();
            needGarbageCollect_ = true;
        }
    }
}

void ZoneManager::PreProcessZones()
{
    vector<string> zoneFilesToProcess;
    listFilesOfType(DAW::GetResourcePath() + string("/CSI/Zones/") + zoneFolder_ + "/", zoneFilesToProcess, ".zon"); // recursively find all .zon files, starting at zoneFolder
       
    if (zoneFilesToProcess.size() == 0)
    {
        MessageBox(g_hwnd, (string("Please check your installation, cannot find Zone files in ") + DAW::GetResourcePath() + string("/CSI/Zones/") + zoneFolder_).c_str(), (string(GetSurface()->GetName()) + " Zone folder is missing or empty").c_str(), MB_OK);

        return;
    }
      
    for (int i = 0; i < (int)zoneFilesToProcess.size(); ++i)
        PreProcessZoneFile(zoneFilesToProcess[i], this);
    
    if (zoneFolder_ != fxZoneFolder_)
    {
        zoneFilesToProcess.clear();
        
        listFilesOfType(DAW::GetResourcePath() + string("/CSI/Zones/") + fxZoneFolder_ + "/", zoneFilesToProcess, ".zon"); // recursively find all .zon files, starting at fxZoneFolder
         
        for (int i = 0; i < (int)zoneFilesToProcess.size(); ++i)
            PreProcessZoneFile(zoneFilesToProcess[i], this);
    }
}

void ZoneManager::CalculateSteppedValue(const string &fxName, MediaTrack *track, int fxIndex, int paramIndex)
{
    // Check for UAD / Plugin Alliance and bail if neither
    if (fxName.find("UAD") == string::npos && fxName.find("Plugin Alliance") == string::npos)
        return;
    
    bool wasMuted = false;
    DAW::GetTrackUIMute(track, &wasMuted);
    
    if ( ! wasMuted)
        DAW::CSurf_SetSurfaceMute(track, DAW::CSurf_OnMuteChange(track, true), NULL);

    double minvalOut = 0.0;
    double maxvalOut = 0.0;

    double currentValue;

    currentValue = DAW::TrackFX_GetParam(track, fxIndex, paramIndex, &minvalOut, &maxvalOut);
    
        int stepCount = 1;
        double stepValue = 0.0;
        
        for (double value = 0.0; value < 1.01; value += .01)
        {
            DAW::TrackFX_SetParam(track, fxIndex, paramIndex, value);
            
            double fxValue = DAW::TrackFX_GetParam(track, fxIndex, paramIndex, &minvalOut, &maxvalOut);
            
            if (stepValue != fxValue)
            {
                stepValue = fxValue;
                stepCount++;
            }
        }
        
    if (stepCount > 1 && stepCount < 31)
        csi_->SetSteppedValueCount(fxName, paramIndex, stepCount);

    DAW::TrackFX_SetParam(track, fxIndex, paramIndex, currentValue);
    
    if ( ! wasMuted)
        DAW::CSurf_SetSurfaceMute(track, DAW::CSurf_OnMuteChange(track, false), NULL);
}

void ZoneManager::CalculateSteppedValues(const string &fxName, MediaTrack *track, int fxIndex)
{
    csi_->SetSteppedValueCount(fxName, -1, 0); // Add dummy value to show the calculation has beeen performed, even though there may be no stepped values for this FX

    // Check for UAD / Plugin Alliance and bail if neither
    if (fxName.find("UAD") == string::npos && fxName.find("Plugin Alliance") == string::npos)
        return;
    
    int totalLayoutCount = 0;
    
    for (int i = 0; i < (int)fxLayouts_.size(); ++i)
        totalLayoutCount += fxLayouts_[i].channelCount_;
    bool wasMuted = false;
    DAW::GetTrackUIMute(track, &wasMuted);
    
    if ( ! wasMuted)
        DAW::CSurf_SetSurfaceMute(track, DAW::CSurf_OnMuteChange(track, true), NULL);

    double minvalOut = 0.0;
    double maxvalOut = 0.0;

    int numParams = DAW::TrackFX_GetNumParams(track, fxIndex);

    vector<double> currentValues;

    for (int i = 0; i < numParams && i <= totalLayoutCount; i++)
        currentValues.push_back(DAW::TrackFX_GetParam(track, fxIndex, i, &minvalOut, &maxvalOut));
    
    for (int i = 0; i < numParams && i <= totalLayoutCount; i++)
    {
        int stepCount = 1;
        double stepValue = 0.0;
        
        for (double value = 0.0; value < 1.01; value += .01)
        {
            DAW::TrackFX_SetParam(track, fxIndex, i, value);
            
            double fxValue = DAW::TrackFX_GetParam(track, fxIndex, i, &minvalOut, &maxvalOut);
            
            if (stepValue != fxValue)
            {
                stepValue = fxValue;
                stepCount++;
            }
        }
        
        if (stepCount > 1 && stepCount < 31)
            csi_->SetSteppedValueCount(fxName, i, stepCount);
    }
    
    for (int i = 0; i < numParams && i <= totalLayoutCount; i++)
        DAW::TrackFX_SetParam(track, fxIndex, i, currentValues[i]);
    
    if ( ! wasMuted)
        DAW::CSurf_SetSurfaceMute(track, DAW::CSurf_OnMuteChange(track, false), NULL);
}

void ZoneManager::AutoMapFX(const string &fxName, MediaTrack *track, int fxIndex)
{    
    if (fxLayouts_.size() == 0)
        return;

    if (surfaceFXLayout_.size() == 0)
        return;
            
    string path = DAW::GetResourcePath() + string("/CSI/Zones/") + fxZoneFolder_ + "/AutoGeneratedFXZones";
    
    RecursiveCreateDirectory(path.c_str(),0);
    
    path += "/" + ReplaceAllWith(fxName, s_BadFileChars, "_") + ".zon";

    string alias;
    GetAlias(fxName.c_str(),alias);

    string paramAction = " FXParam ";
    
    if (fxName.find("JS:") != string::npos)
        paramAction = " JSFXParam ";
    
    CSIZoneInfo *info = new CSIZoneInfo();
    info->filePath = path;
    info->alias = alias;

    int totalAvailableChannels = 0;
    
    for (int i = 0; i < (int)fxLayouts_.size(); ++i)
        totalAvailableChannels += fxLayouts_[i].channelCount_;
        
    AddZoneFilePath(fxName, info);
    surface_->GetPage()->AddZoneFilePath(surface_, fxZoneFolder_, fxName, info);

    ofstream fxZone(path);

    if (fxZone.is_open())
    {
        fxZone << "Zone \"" + fxName + "\" \"" + alias + "\"\n";
        
        for (int i = 0; i < (int)fxPrologue_.size(); ++i)
            fxZone << "\t" + fxPrologue_[i] + "\n";
               
        fxZone << "\n" + s_BeginAutoSection + "\n";
        
        int layoutIndex = 0;
        int channelIndex = 1;
             
        vector<string> actionWidgets;
        
        string actionWidget = surfaceFXLayout_[0][0];
     
        actionWidgets.push_back(actionWidget);
        
        for (int i = 0; i < (int)surfaceFXLayoutTemplate_.size(); ++i)
            if (surfaceFXLayoutTemplate_[i][0] == "WidgetTypes")
                for (int j = 1; j < surfaceFXLayoutTemplate_[i].size(); j++)
                    if (surfaceFXLayoutTemplate_[i][j] != actionWidget)
                        actionWidgets.push_back(surfaceFXLayoutTemplate_[i][j]);

        for (int paramIdx = 0; paramIdx < DAW::TrackFX_GetNumParams(track, fxIndex) && paramIdx < totalAvailableChannels; paramIdx++)
        {
            for (int widgetIdx = 0; widgetIdx < actionWidgets.size(); widgetIdx++)
            {
                for (int lineIdx = 0; lineIdx < surfaceFXLayout_.size(); lineIdx++)
                {
                    for (int tokenIdx = 0; tokenIdx < surfaceFXLayout_[lineIdx].size(); tokenIdx++)
                    {
                        if (tokenIdx == 0)
                        {
                            string modifiers = "";
                            
                            if (fxLayouts_[layoutIndex].modifiers_ != "")
                                modifiers = fxLayouts_[layoutIndex].modifiers_ + "+";
                            
                            if (widgetIdx == 0)
                                fxZone << "\t" + modifiers + surfaceFXLayout_[lineIdx][tokenIdx] + fxLayouts_[layoutIndex].suffix_ + to_string(channelIndex) + "\t";
                            else
                            {
                                if (lineIdx == 0)
                                    fxZone << "\t" + modifiers + actionWidgets[widgetIdx] + fxLayouts_[layoutIndex].suffix_ + to_string(channelIndex) + "\t";
                                else
                                    fxZone << "\t" + string("NullDisplay") + "\t";
                            }
                        }
                        else if (tokenIdx == 1)
                        {
                            if (widgetIdx == 0)
                                fxZone <<  surfaceFXLayout_[lineIdx][tokenIdx];
                            else
                                fxZone <<  string("NoAction");
                            
                            if (widgetIdx == 0 && surfaceFXLayout_[lineIdx][tokenIdx] == "FixedTextDisplay")
                            {
                                char tmp[BUFSZ];
                                fxZone << " \"";
                                fxZone << DAW::TrackFX_GetParamName(track, fxIndex, paramIdx, tmp, sizeof(tmp));
                                fxZone << "\"";
                            }
                            else if (widgetIdx == 0)
                                fxZone << " " + to_string(paramIdx);
                            
                            if (widgetIdx == 0 && surfaceFXLayout_[lineIdx][tokenIdx] == "FXParam")
                            {
                                int steppedValueCount =  csi_->GetSteppedValueCount(fxName, paramIdx);
                                
                                if (steppedValueCount >= g_minNumParamSteps && steppedValueCount <= g_maxNumParamSteps)
                                {
                                    string steps;
                                    GetParamStepsString(steps, steppedValueCount);
                                    steps = " [ " + steps + "]";
                                    fxZone << steps;
                                }
                            }
                        }
                        else if (widgetIdx == 0)
                            fxZone << " " + surfaceFXLayout_[lineIdx][tokenIdx];
                    }
                    
                    fxZone << "\n";
                }
                
                fxZone << "\n";
            }
            
            channelIndex++;
            
            fxZone << "\n";
            
            if (channelIndex > fxLayouts_[layoutIndex].channelCount_)
            {
                channelIndex = 1;
                
                if (layoutIndex < fxLayouts_.size() - 1)
                    layoutIndex++;
                else
                    break;
            }
        }
                
        // GAW -- pad partial rows
        if (channelIndex != 1 && channelIndex <= fxLayouts_[layoutIndex].channelCount_)
        {
            while (channelIndex <= fxLayouts_[layoutIndex].channelCount_)
            {
                for (int widgetIdx = 0; widgetIdx < actionWidgets.size(); widgetIdx++)
                {
                    string modifiers = "";
                    
                    if (fxLayouts_[layoutIndex].modifiers_ != "")
                        modifiers = fxLayouts_[layoutIndex].modifiers_ + "+";
                    
                    fxZone << "\t" + modifiers + actionWidgets[widgetIdx] + fxLayouts_[layoutIndex].suffix_ + to_string(channelIndex) + "\tNoAction\n";
                    
                    if (widgetIdx == 0 && surfaceFXLayout_.size() > 2 && surfaceFXLayout_[1].size() > 0 && surfaceFXLayout_[2].size() > 0)
                    {
                        fxZone << "\t" + modifiers + surfaceFXLayout_[1][0] + fxLayouts_[layoutIndex].suffix_ + to_string(channelIndex) + "\tNoAction";
                        
                        if (surfaceFXLayout_.size() > 1)
                            for (int i = 2; i < surfaceFXLayout_[1].size(); i++)
                                fxZone << " " + surfaceFXLayout_[1][i];
                        
                        fxZone << "\n";
                        
                        fxZone << "\t" + modifiers + surfaceFXLayout_[2][0] + fxLayouts_[layoutIndex].suffix_ + to_string(channelIndex) + "\tNoAction";
                        
                        if (surfaceFXLayout_.size() > 2)
                            for (int i = 2; i < surfaceFXLayout_[2].size(); i++)
                                fxZone << " " + surfaceFXLayout_[2][i];
                        
                        fxZone << "\n\n";
                    }
                    else
                    {
                        fxZone << "\tNullDisplay\tNoAction\n";
                        fxZone << "\tNullDisplay\tNoAction\n\n";
                    }
                }
                
                fxZone << "\n";
                
                channelIndex++;
            }
        }

        layoutIndex++;
        
        // GAW --pad the remaining rows
        while (layoutIndex < fxLayouts_.size())
        {
            for (int index = 1; index <= fxLayouts_[layoutIndex].channelCount_; index++)
            {
                for (int widgetIdx = 0; widgetIdx < actionWidgets.size(); widgetIdx++)
                {
                    string modifiers = "";
                    
                    if (fxLayouts_[layoutIndex].modifiers_ != "")
                        modifiers = fxLayouts_[layoutIndex].modifiers_ + "+";
                    
                    fxZone << "\t" + modifiers + actionWidgets[widgetIdx] + fxLayouts_[layoutIndex].suffix_ + to_string(index) + "\tNoAction\n";
                    
                    if (widgetIdx == 0 && surfaceFXLayout_.size() > 2 && surfaceFXLayout_[1].size() > 0 && surfaceFXLayout_[2].size() > 0)
                    {
                        fxZone << "\t" + modifiers + surfaceFXLayout_[1][0] + fxLayouts_[layoutIndex].suffix_ + to_string(index) + "\tNoAction";
                        
                        if (surfaceFXLayout_.size() > 1)
                            for (int i = 2; i < surfaceFXLayout_[1].size(); i++)
                                fxZone << " " + surfaceFXLayout_[1][i];
                        
                        fxZone << "\n";
                        
                        fxZone << "\t" + modifiers + surfaceFXLayout_[2][0] + fxLayouts_[layoutIndex].suffix_ + to_string(index) + "\tNoAction";
                        
                        if (surfaceFXLayout_.size() > 2)
                            for (int i = 2; i < surfaceFXLayout_[2].size(); i++)
                                fxZone << " " + surfaceFXLayout_[2][i];
                        
                        fxZone << "\n\n";
                    }
                    else
                    {
                        fxZone << "\tNullDisplay\tNoAction\n";
                        fxZone << "\tNullDisplay\tNoAction\n\n";
                    }
                }
                
                fxZone << "\n";
            }
            
            layoutIndex++;
        }
        
        fxZone << s_EndAutoSection + "\n";
                
        for (int i = 0; i < (int)fxEpilogue_.size(); ++i)
            fxZone << "\t" + fxEpilogue_[i] + "\n";

        fxZone << "ZoneEnd\n\n";
        
        for (int i = 0; i < DAW::TrackFX_GetNumParams(track, fxIndex); i++)
        {
            char tmp[BUFSZ], tmp2[BUFSZ];
            snprintf(tmp2, sizeof(tmp2), "%d %s\n", i, DAW::TrackFX_GetParamName(track, fxIndex, i, tmp, sizeof(tmp)));
            fxZone << tmp2;
        }
        
        fxZone.close();
    }
    
    if (zoneFilePaths_.Exists(fxName.c_str()))
    {
        WDL_PtrList<Navigator> navigators;
        navigators.Add(GetSelectedTrackNavigator());
        
        LoadZoneFile(zoneFilePaths_.Get(fxName.c_str())->filePath, navigators, fxSlotZones_, nullptr);
        
        if (fxSlotZones_.GetSize() > 0)
        {
            fxSlotZones_.Get(fxSlotZones_.GetSize() -1)->SetSlotIndex(fxIndex);
            fxSlotZones_.Get(fxSlotZones_.GetSize() - 1)->Activate();
        }
        needGarbageCollect_ = true;
    }
}

void ZoneManager::DoTouch(Widget *widget, double value)
{
    surface_->TouchChannel(widget->GetChannelNumber(), value);
    
    widget->LogInput(value);
    
    bool isUsed = false;
    
    if (focusedFXParamZone_ != nullptr && isFocusedFXParamMappingEnabled_)
        focusedFXParamZone_->DoTouch(widget, widget->GetName(), isUsed, value);
    
    for (int i = 0; i < focusedFXZones_.GetSize(); ++i)
        focusedFXZones_.Get(i)->DoTouch(widget, widget->GetName(), isUsed, value);
    
    if (isUsed)
        return;

    for (int i = 0; i < selectedTrackFXZones_.GetSize(); ++i)
        selectedTrackFXZones_.Get(i)->DoTouch(widget, widget->GetName(), isUsed, value);
    
    if (isUsed)
        return;

    for (int i = 0; i < fxSlotZones_.GetSize(); ++i)
        fxSlotZones_.Get(i)->DoTouch(widget, widget->GetName(), isUsed, value);
    
    if (isUsed)
        return;

    if (homeZone_ != nullptr)
        homeZone_->DoTouch(widget, widget->GetName(), isUsed, value);
}

Navigator *ZoneManager::GetMasterTrackNavigator() { return surface_->GetPage()->GetMasterTrackNavigator(); }
Navigator *ZoneManager::GetSelectedTrackNavigator() { return surface_->GetPage()->GetSelectedTrackNavigator(); }
Navigator *ZoneManager::GetFocusedFXNavigator() { return surface_->GetPage()->GetFocusedFXNavigator(); }
int ZoneManager::GetNumChannels() { return surface_->GetNumChannels(); }

////////////////////////////////////////////////////////////////////////////////////////////////////////
// ModifierManager
////////////////////////////////////////////////////////////////////////////////////////////////////////
void ModifierManager::RecalculateModifiers()
{
    if (surface_ == nullptr && page_ == nullptr)
        return;
    
    if (modifierCombinations_.ResizeOK(1,false))
      modifierCombinations_.Get()[0] =0 ;
           
    Modifiers activeModifierIndices[MaxModifiers];
    int activeModifierIndices_cnt = 0;
    
    for (int i = 0; i < MaxModifiers; i++)
        if (modifiers_[i].isEngaged)
            activeModifierIndices[activeModifierIndices_cnt++] = (Modifiers)i;
    
    if (activeModifierIndices_cnt>0)
    {
        GetCombinations(activeModifierIndices,activeModifierIndices_cnt, modifierCombinations_);
        qsort(modifierCombinations_.Get(), modifierCombinations_.GetSize(), sizeof(modifierCombinations_.Get()[0]), intcmp_rev);
    }
    
    if (surface_ != nullptr)
        surface_->GetZoneManager()->UpdateCurrentActionContextModifiers();
    else if (page_ != nullptr)
        page_->UpdateCurrentActionContextModifiers();
}

void ModifierManager::SetLatchModifier(bool value, Modifiers modifier, int latchTime)
{
    if (value && modifiers_[modifier].isEngaged == false)
    {
        modifiers_[modifier].isEngaged = value;
        modifiers_[modifier].pressedTime = DAW::GetCurrentNumberOfMilliseconds();
    }
    else
    {
        double keyReleasedTime = DAW::GetCurrentNumberOfMilliseconds();
        
        if (keyReleasedTime - modifiers_[modifier].pressedTime > latchTime)
        {
            if (value == 0 && modifiers_[modifier].isEngaged)
            {
                char tmp[256];
                snprintf(tmp,sizeof(tmp), "%s Unlock", stringFromModifier(modifier));
                csi_->Speak(tmp);
            }

            modifiers_[modifier].isEngaged = value;
        }
        else
        {
            char tmp[256];
            snprintf(tmp,sizeof(tmp), "%s Lock", stringFromModifier(modifier));
            csi_->Speak(tmp);
        }
    }
    
    RecalculateModifiers();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// TrackNavigationManager
////////////////////////////////////////////////////////////////////////////////////////////////////////
void TrackNavigationManager::RebuildTracks()
{
    int oldTracksSize = tracks_.GetSize();
    
    tracks_.Empty();
    
    for (int i = 1; i <= GetNumTracks(); i++)
    {
        if (MediaTrack *track = DAW::CSurf_TrackFromID(i, followMCP_))
            if (DAW::IsTrackVisible(track, followMCP_))
                tracks_.Add(track);
    }
    
    if (tracks_.GetSize() < oldTracksSize)
    {
        for (int i = oldTracksSize; i > tracks_.GetSize(); i--)
            page_->ForceClearTrack(i - trackOffset_);
    }
    
    if (tracks_.GetSize() != oldTracksSize)
        page_->ForceUpdateTrackColors();
}

void TrackNavigationManager::RebuildSelectedTracks()
{
    if (currentTrackVCAFolderMode_ != 3)
        return;

    int oldTracksSize = selectedTracks_.GetSize();
    
    selectedTracks_.Empty();
    
    for (int i = 0; i < DAW::CountSelectedTracks(); i++)
        selectedTracks_.Add(DAW::GetSelectedTrack(i));

    if (selectedTracks_.GetSize() < oldTracksSize)
    {
        for (int i = oldTracksSize; i > selectedTracks_.GetSize(); i--)
            page_->ForceClearTrack(i - selectedTracksOffset_);
    }
    
    if (selectedTracks_.GetSize() != oldTracksSize)
        page_->ForceUpdateTrackColors();
}

void TrackNavigationManager::AdjustSelectedTrackBank(int amount)
{
    if (MediaTrack *selectedTrack = GetSelectedTrack())
    {
        int trackNum = GetIdFromTrack(selectedTrack);
        
        trackNum += amount;
        
        if (trackNum < 1)
            trackNum = 1;
        
        if (trackNum > GetNumTracks())
            trackNum = GetNumTracks();
        
        if (MediaTrack *trackToSelect = GetTrackFromId(trackNum))
        {
            DAW::SetOnlyTrackSelected(trackToSelect);
            if (GetScrollLink())
                DAW::SetMixerScroll(trackToSelect);

            page_->OnTrackSelection(trackToSelect);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// ControlSurface
////////////////////////////////////////////////////////////////////////////////////////////////////////
void ControlSurface::Stop()
{
    if (isRewinding_ || isFastForwarding_) // set the cursor to the Play position
        DAW::CSurf_OnPlay();
 
    page_->SignalStop();
    CancelRewindAndFastForward();
    DAW::CSurf_OnStop();
}

void ControlSurface::Play()
{
    page_->SignalPlay();
    CancelRewindAndFastForward();
    DAW::CSurf_OnPlay();
}

void ControlSurface::Record()
{
    page_->SignalRecord();
    CancelRewindAndFastForward();
    DAW::CSurf_OnRecord();
}

void ControlSurface::OnTrackSelection(MediaTrack *track)
{
    Widget *w = widgetsByName_.Get("OnTrackSelection");
    if (w)
    {
        if (DAW::GetMediaTrackInfo_Value(track, "I_SELECTED"))
            zoneManager_->DoAction(w, 1.0);
        else
            zoneManager_->OnTrackDeselection();
        
        zoneManager_->OnTrackSelection();
    }
}

void ControlSurface::ForceClearTrack(int trackNum)
{
    for (int i = 0; i < widgets_.GetSize(); ++i)
        if (widgets_.Get(i)->GetChannelNumber() + channelOffset_ == trackNum)
            widgets_.Get(i)->ForceClear();
}

void ControlSurface::ForceUpdateTrackColors()
{
    for (int i = 0; i < trackColorFeedbackProcessors_.GetSize(); ++i)
        trackColorFeedbackProcessors_.Get(i)->ForceUpdateTrackColors();
}

rgba_color ControlSurface::GetTrackColorForChannel(int channel)
{
    rgba_color white;
    white.r = 255;
    white.g = 255;
    white.b = 255;

    if (channel < 0 || channel >= numChannels_)
        return white;
    
    if (fixedTrackColors_.size() == numChannels_)
        return fixedTrackColors_[channel];
    else
    {
        if (MediaTrack *track = page_->GetNavigatorForChannel(channel + channelOffset_)->GetTrack())
            return DAW::GetTrackColor(track);
        else
            return white;
    }
}

void ControlSurface::RequestUpdate()
{
    for (int i = 0; i < trackColorFeedbackProcessors_.GetSize(); ++i)
        trackColorFeedbackProcessors_.Get(i)->UpdateTrackColors();
    
    for (int i = 0; i < widgets_.GetSize(); ++i)
        widgets_.Get(i)->ClearHasBeenUsedByUpdate();
    
    zoneManager_->RequestUpdate();

    // default is to zero unused Widgets -- for an opposite sense device, you can override this by supplying an inverted NoAction context in the Home Zone
    const PropertyList properties;
    
    for (int i = 0; i < widgets_.GetSize(); ++i)
    {
        Widget *widget =  widgets_.Get(i);
        
        if ( ! widget->GetHasBeenUsedByUpdate())
        {
            widget->SetHasBeenUsedByUpdate();
            
            rgba_color color;
            widget->UpdateValue(properties, 0.0);
            widget->UpdateValue(properties, "");
            widget->UpdateColorValue(color);
        }
    }

    if (isRewinding_)
    {
        if (DAW::GetCursorPosition() == 0)
            StopRewinding();
        else
        {
            DAW::CSurf_OnRew(0);

            if (speedX5_ == true)
            {
                DAW::CSurf_OnRew(0);
                DAW::CSurf_OnRew(0);
                DAW::CSurf_OnRew(0);
                DAW::CSurf_OnRew(0);
            }
        }
    }
        
    else if (isFastForwarding_)
    {
        if (DAW::GetCursorPosition() > DAW::GetProjectLength(nullptr))
            StopFastForwarding();
        else
        {
            DAW::CSurf_OnFwd(0);
            
            if (speedX5_ == true)
            {
                DAW::CSurf_OnFwd(0);
                DAW::CSurf_OnFwd(0);
                DAW::CSurf_OnFwd(0);
                DAW::CSurf_OnFwd(0);
            }
        }
    }
}

bool ControlSurface::GetShift()
{
    if (usesLocalModifiers_ || listensToModifiers_)
        return modifierManager_->GetShift();
    else
        return page_->GetModifierManager()->GetShift();
}

bool ControlSurface::GetOption()
{
    if (usesLocalModifiers_ || listensToModifiers_)
        return modifierManager_->GetOption();
    else
        return page_->GetModifierManager()->GetOption();
}

bool ControlSurface::GetControl()
{
    if (usesLocalModifiers_ || listensToModifiers_)
        return modifierManager_->GetControl();
    else
        return page_->GetModifierManager()->GetControl();
}

bool ControlSurface::GetAlt()
{
    if (usesLocalModifiers_ || listensToModifiers_)
        return modifierManager_->GetAlt();
    else
        return page_->GetModifierManager()->GetAlt();
}

bool ControlSurface::GetFlip()
{
    if (usesLocalModifiers_ || listensToModifiers_)
        return modifierManager_->GetFlip();
    else
        return page_->GetModifierManager()->GetFlip();
}

bool ControlSurface::GetGlobal()
{
    if (usesLocalModifiers_ || listensToModifiers_)
        return modifierManager_->GetGlobal();
    else
        return page_->GetModifierManager()->GetGlobal();
}

bool ControlSurface::GetMarker()
{
    if (usesLocalModifiers_ || listensToModifiers_)
        return modifierManager_->GetMarker();
    else
        return page_->GetModifierManager()->GetMarker();
}

bool ControlSurface::GetNudge()
{
    if (usesLocalModifiers_ || listensToModifiers_)
        return modifierManager_->GetNudge();
    else
        return page_->GetModifierManager()->GetNudge();
}

bool ControlSurface::GetZoom()
{
    if (usesLocalModifiers_ || listensToModifiers_)
        return modifierManager_->GetZoom();
    else
        return page_->GetModifierManager()->GetZoom();
}

bool ControlSurface::GetScrub()
{
    if (usesLocalModifiers_ || listensToModifiers_)
        return modifierManager_->GetScrub();
    else
        return page_->GetModifierManager()->GetScrub();
}

void ControlSurface::SetShift(bool value)
{
    if (zoneManager_->GetIsBroadcaster() && usesLocalModifiers_)
    {
        modifierManager_->SetShift(value, latchTime_);
        
        for (int i = 0; i < zoneManager_->GetListeners().GetSize(); ++i)
            if (zoneManager_->GetListeners().Get(i)->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners().Get(i)->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners().Get(i)->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners().Get(i)->GetSurface()->GetModifierManager()->SetShift(value, latchTime_);
    }
    else if (usesLocalModifiers_)
        modifierManager_->SetShift(value, latchTime_);
    else
        page_->GetModifierManager()->SetShift(value, latchTime_);
}

void ControlSurface::SetOption(bool value)
{
    if (zoneManager_->GetIsBroadcaster() && usesLocalModifiers_)
    {
        modifierManager_->SetOption(value, latchTime_);
        
        for (int i = 0; i < zoneManager_->GetListeners().GetSize(); ++i)
            if (zoneManager_->GetListeners().Get(i)->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners().Get(i)->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners().Get(i)->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners().Get(i)->GetSurface()->GetModifierManager()->SetOption(value, latchTime_);
    }
    else if (usesLocalModifiers_)
        modifierManager_->SetOption(value, latchTime_);
    else
        page_->GetModifierManager()->SetOption(value, latchTime_);
}

void ControlSurface::SetControl(bool value)
{
    if (zoneManager_->GetIsBroadcaster() && usesLocalModifiers_)
    {
        modifierManager_->SetControl(value, latchTime_);
        
        for (int i = 0; i < zoneManager_->GetListeners().GetSize(); ++i)
            if (zoneManager_->GetListeners().Get(i)->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners().Get(i)->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners().Get(i)->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners().Get(i)->GetSurface()->GetModifierManager()->SetControl(value, latchTime_);
    }
    else if (usesLocalModifiers_)
        modifierManager_->SetControl(value, latchTime_);
    else
        page_->GetModifierManager()->SetControl(value, latchTime_);
}

void ControlSurface::SetAlt(bool value)
{
    if (zoneManager_->GetIsBroadcaster() && usesLocalModifiers_)
    {
        modifierManager_->SetAlt(value, latchTime_);
        
        for (int i = 0; i < zoneManager_->GetListeners().GetSize(); ++i)
            if (zoneManager_->GetListeners().Get(i)->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners().Get(i)->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners().Get(i)->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners().Get(i)->GetSurface()->GetModifierManager()->SetAlt(value, latchTime_);
    }
    else if (usesLocalModifiers_)
        modifierManager_->SetAlt(value, latchTime_);
    else
        page_->GetModifierManager()->SetAlt(value, latchTime_);
}

void ControlSurface::SetFlip(bool value)
{
    if (zoneManager_->GetIsBroadcaster() && usesLocalModifiers_)
    {
        modifierManager_->SetFlip(value, latchTime_);
        
        for (int i = 0; i < zoneManager_->GetListeners().GetSize(); ++i)
            if (zoneManager_->GetListeners().Get(i)->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners().Get(i)->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners().Get(i)->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners().Get(i)->GetSurface()->GetModifierManager()->SetFlip(value, latchTime_);
    }
    else if (usesLocalModifiers_)
        modifierManager_->SetFlip(value, latchTime_);
    else
        page_->GetModifierManager()->SetFlip(value, latchTime_);
}

void ControlSurface::SetGlobal(bool value)
{
    if (zoneManager_->GetIsBroadcaster() && usesLocalModifiers_)
    {
        modifierManager_->SetGlobal(value, latchTime_);
        
        for (int i = 0; i < zoneManager_->GetListeners().GetSize(); ++i)
            if (zoneManager_->GetListeners().Get(i)->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners().Get(i)->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners().Get(i)->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners().Get(i)->GetSurface()->GetModifierManager()->SetGlobal(value, latchTime_);
    }
    else if (usesLocalModifiers_)
        modifierManager_->SetGlobal(value, latchTime_);
    else
        page_->GetModifierManager()->SetGlobal(value, latchTime_);
}

void ControlSurface::SetMarker(bool value)
{
    if (zoneManager_->GetIsBroadcaster() && usesLocalModifiers_)
    {
        modifierManager_->SetMarker(value, latchTime_);
        
        for (int i = 0; i < zoneManager_->GetListeners().GetSize(); ++i)
            if (zoneManager_->GetListeners().Get(i)->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners().Get(i)->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners().Get(i)->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners().Get(i)->GetSurface()->GetModifierManager()->SetMarker(value, latchTime_);
    }
    else if (usesLocalModifiers_)
        modifierManager_->SetMarker(value, latchTime_);
    else
        page_->GetModifierManager()->SetMarker(value, latchTime_);
}

void ControlSurface::SetNudge(bool value)
{
    if (zoneManager_->GetIsBroadcaster() && usesLocalModifiers_)
    {
        modifierManager_->SetNudge(value, latchTime_);
        
        for (int i = 0; i < zoneManager_->GetListeners().GetSize(); ++i)
            if (zoneManager_->GetListeners().Get(i)->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners().Get(i)->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners().Get(i)->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners().Get(i)->GetSurface()->GetModifierManager()->SetNudge(value, latchTime_);
    }
    else if (usesLocalModifiers_)
        modifierManager_->SetNudge(value, latchTime_);
    else
        page_->GetModifierManager()->SetNudge(value, latchTime_);
}

void ControlSurface::SetZoom(bool value)
{
    if (zoneManager_->GetIsBroadcaster() && usesLocalModifiers_)
    {
        modifierManager_->SetZoom(value, latchTime_);
        
        for (int i = 0; i < zoneManager_->GetListeners().GetSize(); ++i)
            if (zoneManager_->GetListeners().Get(i)->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners().Get(i)->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners().Get(i)->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners().Get(i)->GetSurface()->GetModifierManager()->SetZoom(value, latchTime_);
    }
    else if (usesLocalModifiers_)
        modifierManager_->SetZoom(value, latchTime_);
    else
        page_->GetModifierManager()->SetZoom(value, latchTime_);
}

void ControlSurface::SetScrub(bool value)
{
    if (zoneManager_->GetIsBroadcaster() && usesLocalModifiers_)
    {
        modifierManager_->SetScrub(value, latchTime_);
        
        for (int i = 0; i < zoneManager_->GetListeners().GetSize(); ++i)
            if (zoneManager_->GetListeners().Get(i)->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners().Get(i)->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners().Get(i)->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners().Get(i)->GetSurface()->GetModifierManager()->SetScrub(value, latchTime_);
    }
    else if (usesLocalModifiers_)
        modifierManager_->SetScrub(value, latchTime_);
    else
        page_->GetModifierManager()->SetScrub(value, latchTime_);
}

const WDL_TypedBuf<int> &ControlSurface::GetModifiers()
{
    if (usesLocalModifiers_ || listensToModifiers_)
        return modifierManager_->GetModifiers();
    else
        return page_->GetModifierManager()->GetModifiers();
}

void ControlSurface::ClearModifier(const string &modifier)
{
    if (zoneManager_->GetIsBroadcaster() && usesLocalModifiers_)
    {
        modifierManager_->ClearModifier(modifier);
        
        for (int i = 0; i < zoneManager_->GetListeners().GetSize(); ++i)
            if (zoneManager_->GetListeners().Get(i)->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners().Get(i)->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners().Get(i)->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners().Get(i)->GetSurface()->GetModifierManager()->ClearModifier(modifier);
    }
    else if (usesLocalModifiers_ || listensToModifiers_)
        modifierManager_->ClearModifier(modifier);
    else
        page_->GetModifierManager()->ClearModifier(modifier);
}

void ControlSurface::ClearModifiers()
{
    if (zoneManager_->GetIsBroadcaster() && usesLocalModifiers_)
    {
        modifierManager_->ClearModifiers();
        
        for (int i = 0; i < zoneManager_->GetListeners().GetSize(); ++i)
            if (zoneManager_->GetListeners().Get(i)->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners().Get(i)->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners().Get(i)->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners().Get(i)->GetSurface()->GetModifierManager()->ClearModifiers();
    }
    else if (usesLocalModifiers_ || listensToModifiers_)
        modifierManager_->ClearModifiers();
    else
        page_->GetModifierManager()->ClearModifiers();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi_ControlSurfaceIO
////////////////////////////////////////////////////////////////////////////////////////////////////////
void Midi_ControlSurfaceIO::HandleExternalInput(Midi_ControlSurface *surface)
{
    if (midiInput_)
    {
        DAW::SwapBufsPrecise(midiInput_);
        MIDI_eventlist *list = midiInput_->GetReadBuf();
        int bpos = 0;
        MIDI_event_t *evt;
        while ((evt = list->EnumItems(&bpos)))
            surface->ProcessMidiMessage((MIDI_event_ex_t*)evt);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi_ControlSurface
////////////////////////////////////////////////////////////////////////////////////////////////////////
Midi_ControlSurface::Midi_ControlSurface(CSurfIntegrator *const csi, Page *page, const string &name, int numChannels, int channelOffset, string templateFilename, string zoneFolder, string fxZoneFolder, Midi_ControlSurfaceIO *surfaceIO)
: ControlSurface(csi, page, name, numChannels, channelOffset), templateFilename_(templateFilename), surfaceIO_(surfaceIO), Midi_CSIMessageGeneratorsByMessage_(disposeAction)
{
    // private:
    // special processing for MCU meters
    hasMCUMeters_ = false;
    displayType_ = 0x14;
    
    zoneManager_ = new ZoneManager(csi_, this, zoneFolder, fxZoneFolder);
    
    ProcessMIDIWidgetFile(string(DAW::GetResourcePath()) + "/CSI/Surfaces/Midi/" + templateFilename, this);
    InitHardwiredWidgets(this);
    InitializeMeters();
    zoneManager_->Initialize();
}

void Midi_ControlSurface::ProcessMidiMessage(const MIDI_event_ex_t *evt)
{
    bool isMapped = false;
    
    int threeByteKey = evt->midi_message[0]  * 0x10000 + evt->midi_message[1]  * 0x100 + evt->midi_message[2];
    int twoByteKey = evt->midi_message[0]  * 0x10000 + evt->midi_message[1]  * 0x100;
    int oneByteKey = evt->midi_message[0] * 0x10000;

    // At this point we don't know how much of the message comprises the key, so try all three
    if (Midi_CSIMessageGeneratorsByMessage_.Exists(threeByteKey))
    {
        isMapped = true;
        Midi_CSIMessageGeneratorsByMessage_.Get(threeByteKey)->ProcessMidiMessage(evt);
    }
    else if (Midi_CSIMessageGeneratorsByMessage_.Exists(twoByteKey))
    {
        isMapped = true;
        Midi_CSIMessageGeneratorsByMessage_.Get(twoByteKey)->ProcessMidiMessage(evt);
    }
    else if (Midi_CSIMessageGeneratorsByMessage_.Exists(oneByteKey))
    {
        isMapped = true;
        Midi_CSIMessageGeneratorsByMessage_.Get(oneByteKey)->ProcessMidiMessage(evt);
    }
    
    if (csi_->GetSurfaceRawInDisplay() || (! isMapped && csi_->GetSurfaceInDisplay()))
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %02x  %02x  %02x \n", name_.c_str(), evt->midi_message[0], evt->midi_message[1], evt->midi_message[2]);
        DAW::ShowConsoleMsg(buffer);
    }
}

void Midi_ControlSurface::SendMidiSysExMessage(MIDI_event_ex_t *midiMessage)
{
    surfaceIO_->SendMidiMessage(midiMessage);
    
    string output = "OUT->" + name_ + " ";
    
    for (int i = 0; i < midiMessage->size; i++)
    {
        char buffer[32];
        
        snprintf(buffer, sizeof(buffer), "%02x ", midiMessage->midi_message[i]);
        
        output += buffer;
    }
    
    output += "\n";

    if (csi_->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(output.c_str());
}

void Midi_ControlSurface::SendMidiMessage(int first, int second, int third)
{
    surfaceIO_->SendMidiMessage(first, second, third);
    
    if (csi_->GetSurfaceOutDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "%s  %02x  %02x  %02x \n", ("OUT->" + name_).c_str(), first, second, third);
        DAW::ShowConsoleMsg(buffer);
    }
}

 ////////////////////////////////////////////////////////////////////////////////////////////////////////
 // OSC_ControlSurfaceIO
 ////////////////////////////////////////////////////////////////////////////////////////////////////////
OSC_ControlSurfaceIO::OSC_ControlSurfaceIO(CSurfIntegrator *const csi, const string &surfaceName, const string &receiveOnPort, const string &transmitToPort, const string &transmitToIpAddress) : csi_(csi), name_(surfaceName)
{
    // private:
    inSocket_ = nullptr;
    outSocket_ = nullptr;
    X32HeartBeatRefreshInterval_ = 5000; // must be less than 10000
    X32HeartBeatLastRefreshTime_ = 0.0;

    if (receiveOnPort != transmitToPort)
    {
        inSocket_  = GetInputSocketForPort(surfaceName, stoi(receiveOnPort));;
        outSocket_ = GetOutputSocketForAddressAndPort(surfaceName, transmitToIpAddress, stoi(transmitToPort));
    }
    else // WHEN INPUT AND OUTPUT SOCKETS ARE THE SAME -- DO MAGIC :)
    {
        oscpkt::UdpSocket *inSocket = GetInputSocketForPort(surfaceName, stoi(receiveOnPort));;

        struct addrinfo hints;
        struct addrinfo *addressInfo;
        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family = AF_INET;      // IPV4
        hints.ai_socktype = SOCK_DGRAM; // UDP
        hints.ai_flags = 0x00000001;    // socket address is intended for bind
        getaddrinfo(transmitToIpAddress.c_str(), transmitToPort.c_str(), &hints, &addressInfo);
        memcpy(&(inSocket->remote_addr), (void*)(addressInfo->ai_addr), addressInfo->ai_addrlen);

        inSocket_  = inSocket;
        outSocket_ = inSocket;
    }
 }

 void OSC_ControlSurfaceIO::HandleExternalInput(OSC_ControlSurface *surface)
 {
    if (inSocket_ != nullptr && inSocket_->isOk())
    {
        while (inSocket_->receiveNextPacket(0))  // timeout, in ms
        {
            packetReader_.init(inSocket_->packetData(), inSocket_->packetSize());
            oscpkt::Message *message;
            
            while (packetReader_.isOk() && (message = packetReader_.popMessage()) != 0)
            {
                if (message->arg().isFloat())
                {
                    float value = 0;
                    message->arg().popFloat(value);
                    surface->ProcessOSCMessage(message->addressPattern(), value);
                }
                else if (message->arg().isInt32())
                {
                    int value;
                    message->arg().popInt32(value);
                    
                    if (surface->IsX32() && message->addressPattern() == "/-stat/selidx")
                    {
                        string x32Select = message->addressPattern() + '/';
                        if (value < 10)
                            x32Select += '0';
                        x32Select += to_string(value);
                        surface->ProcessOSCMessage(x32Select, 1.0);
                    }
                    else
                        surface->ProcessOSCMessage(message->addressPattern(), value);
                }
            }
        }
    }
 }

////////////////////////////////////////////////////////////////////////////////////////////////////////
// OSC_ControlSurface
////////////////////////////////////////////////////////////////////////////////////////////////////////
OSC_ControlSurface::OSC_ControlSurface(CSurfIntegrator *const csi, Page *page, const string &name, int numChannels, int channelOffset, string templateFilename, string zoneFolder, string fxZoneFolder, OSC_ControlSurfaceIO *surfaceIO) : ControlSurface(csi, page, name, numChannels, channelOffset), templateFilename_(templateFilename), surfaceIO_(surfaceIO)

{
    zoneManager_ = new ZoneManager(csi_, this, zoneFolder, fxZoneFolder);

    ProcessOSCWidgetFile(string(DAW::GetResourcePath()) + "/CSI/Surfaces/OSC/" + templateFilename);
    InitHardwiredWidgets(this);
    zoneManager_->Initialize();
}

void OSC_ControlSurface::ProcessOSCMessage(const string &message, double value)
{
    if (CSIMessageGeneratorsByMessage_.Exists(message.c_str()))
        CSIMessageGeneratorsByMessage_.Get(message.c_str())->ProcessMessage(value);
    
    if (csi_->GetSurfaceInDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %s  %f  \n", name_.c_str(), message.c_str(), value);
        DAW::ShowConsoleMsg(buffer);
    }
}

void OSC_ControlSurface::SendOSCMessage(const string &zoneName)
{
    string oscAddress(zoneName);
    oscAddress = ReplaceAllWith(oscAddress, s_BadFileChars, "_");
    oscAddress = "/" + oscAddress;

    surfaceIO_->SendOSCMessage(oscAddress);
        
    if (csi_->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg((zoneName + "->" + "LoadingZone---->" + name_ + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(const string &oscAddress, int value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
        
    if (csi_->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT->" + name_ + " " + oscAddress + " " + to_string(value) + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(const string &oscAddress, double value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
        
    if (csi_->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT->" + name_ + " " + oscAddress + " " + to_string(value) + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(const string &oscAddress, const string &value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
        
    if (csi_->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT->" + name_ + " " + oscAddress + " " + value + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor *feedbackProcessor, const string &oscAddress, double value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
    
    if (csi_->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT->" + name_ + " " + feedbackProcessor->GetWidget()->GetName() + " " + oscAddress + " " + to_string(value) + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor *feedbackProcessor, const string &oscAddress, int value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);

    if (csi_->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT->" + name_ + " " + feedbackProcessor->GetWidget()->GetName() + " " + oscAddress + " " + to_string(value) + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor *feedbackProcessor, const string &oscAddress, const string &value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);

    if (csi_->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT->" + name_ + " " + feedbackProcessor->GetWidget()->GetName() + " " + oscAddress + " " + value + "\n").c_str());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi_ControlSurface
////////////////////////////////////////////////////////////////////////////////////////////////////////

// GAW TBD -- try to come up with something more elegant

static struct
{
    MIDI_event_ex_t evt;
    char data[BUFSZ];
} s_midiSysExData;

void Midi_ControlSurface::SendSysexInitData(int line[], int numElem)
{
    memset(s_midiSysExData.data, 0, sizeof(s_midiSysExData.data));

    s_midiSysExData.evt.frame_offset=0;
    s_midiSysExData.evt.size=0;
    
    for (int i = 0; i < numElem; ++i)
        s_midiSysExData.evt.midi_message[s_midiSysExData.evt.size++] = line[i];
    
    SendMidiSysExMessage(&s_midiSysExData.evt);
}

void Midi_ControlSurface::InitializeMCU()
{
    int line1[] = { 0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7 };
    int line2[] = { 0xF0, 0x00, 0x00, 0x66, 0x14, 0x00, 0xF7 };
    int line3[] = { 0xF0, 0x00, 0x00, 0x66, 0x14, 0x21, 0x01, 0xF7 };
    int line4[] = { 0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x00, 0x01, 0xF7 };
    int line5[] = { 0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x01, 0x01, 0xF7 };
    int line6[] = { 0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x02, 0x01, 0xF7 };
    int line7[] = { 0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x03, 0x01, 0xF7 };
    int line8[] = { 0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x04, 0x01, 0xF7 };
    int line9[] = { 0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x05, 0x01, 0xF7 };
    int line10[] = { 0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x06, 0x01, 0xF7 };
    int line11[] = { 0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x07, 0x01, 0xF7 };

    SendSysexInitData(line1, NUM_ELEM(line1));
    SendSysexInitData(line2, NUM_ELEM(line2));
    SendSysexInitData(line3, NUM_ELEM(line3));
    SendSysexInitData(line4, NUM_ELEM(line4));
    SendSysexInitData(line5, NUM_ELEM(line5));
    SendSysexInitData(line6, NUM_ELEM(line6));
    SendSysexInitData(line7, NUM_ELEM(line7));
    SendSysexInitData(line8, NUM_ELEM(line8));
    SendSysexInitData(line9, NUM_ELEM(line9));
    SendSysexInitData(line10, NUM_ELEM(line10));
    SendSysexInitData(line11, NUM_ELEM(line11));
}

void Midi_ControlSurface::InitializeMCUXT()
{
    vector<vector<int> > sysExLines;
    
    int line1[] = { 0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7 };
    int line2[] = { 0xF0, 0x00, 0x00, 0x66, 0x15, 0x00, 0xF7 };
    int line3[] = { 0xF0, 0x00, 0x00, 0x66, 0x15, 0x21, 0x01, 0xF7 };
    int line4[] = { 0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x00, 0x01, 0xF7 };
    int line5[] = { 0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x01, 0x01, 0xF7 };
    int line6[] = { 0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x02, 0x01, 0xF7 };
    int line7[] = { 0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x03, 0x01, 0xF7 };
    int line8[] = { 0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x04, 0x01, 0xF7 };
    int line9[] = { 0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x05, 0x01, 0xF7 };
    int line10[] = { 0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x06, 0x01, 0xF7 };
    int line11[] = { 0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x07, 0x01, 0xF7 };
    
    SendSysexInitData(line1, NUM_ELEM(line1));
    SendSysexInitData(line2, NUM_ELEM(line2));
    SendSysexInitData(line3, NUM_ELEM(line3));
    SendSysexInitData(line4, NUM_ELEM(line4));
    SendSysexInitData(line5, NUM_ELEM(line5));
    SendSysexInitData(line6, NUM_ELEM(line6));
    SendSysexInitData(line7, NUM_ELEM(line7));
    SendSysexInitData(line8, NUM_ELEM(line8));
    SendSysexInitData(line9, NUM_ELEM(line9));
    SendSysexInitData(line10, NUM_ELEM(line10));
    SendSysexInitData(line11, NUM_ELEM(line11));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// CSurfIntegrator
////////////////////////////////////////////////////////////////////////////////////////////////////////
static const char * const Control_Surface_Integrator = "Control Surface Integrator";

CSurfIntegrator::CSurfIntegrator() : actions_(true, disposeAction), learnFXActions_(true, disposeAction), fxParamSteppedValueCounts_(true, disposeCounts)
{
    g_csiForGui = this;
    
    // private:
    currentPageIndex_ = 0;
    surfaceRawInDisplay_ = false;
    surfaceInDisplay_ = false;
    surfaceOutDisplay_ = false;
    fxParamsWrite_ = false;

    shouldRun_ = true;
    
    InitActionsDictionary();

    int size = 0;
    int index = projectconfig_var_getoffs("projtimemode", &size);
    timeModeOffs_ = size==4 ? index : -1;
    
    index = projectconfig_var_getoffs("projtimemode2", &size);
    timeMode2Offs_ = size==4 ? index : -1;
    
    index = projectconfig_var_getoffs("projmeasoffs", &size);
    measOffsOffs_ = size==4 ? index : - 1;
    
    index = projectconfig_var_getoffs("projtimeoffs", &size);
    timeOffsOffs_ = size==8 ? index : -1;
    
    index = projectconfig_var_getoffs("panmode", &size);
    projectPanModeOffs_ = size==4 ? index : -1;

    // these are supported by ~7.10+, previous versions we fallback to get_config_var() on-demand
    index = projectconfig_var_getoffs("projmetrov1", &size);
    projectMetronomePrimaryVolumeOffs_ = size==8 ? index : -1;

    index = projectconfig_var_getoffs("projmetrov2", &size);
    projectMetronomeSecondaryVolumeOffs_ = size==8 ? index : -1;
            
    //GenerateX32SurfaceFile();
}

CSurfIntegrator::~CSurfIntegrator()
{
    Shutdown();
    ShutdownOSCIO();

    midiSurfacesIO_.Empty(true);
    
    oscSurfacesIO_.Empty(true);
            
    pages_.Empty(true);    
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

int CSurfIntegrator::Extended(int call, void *parm1, void *parm2, void *parm3)
{
    if (call == CSURF_EXT_SUPPORTS_EXTENDED_TOUCH)
    {
        return 1;
    }
    
    if (call == CSURF_EXT_RESET)
    {
       Init();
    }
    
    if (call == CSURF_EXT_SETFXCHANGE)
    {
        // parm1=(MediaTrack*)track, whenever FX are added, deleted, or change order
        TrackFXListChanged((MediaTrack*)parm1);
    }
        
    if (call == CSURF_EXT_SETMIXERSCROLL)
    {
        MediaTrack *leftPtr = (MediaTrack *)parm1;
        
        int offset = DAW::CSurf_TrackToID(leftPtr, true);
        
        offset--;
        
        if (offset < 0)
            offset = 0;
            
        SetTrackOffset(offset);
    }
        
    return 1;
}

static IReaperControlSurface *createFunc(const char *type_string, const char *configString, int *errStats)
{
    return new CSurfIntegrator();
}

static HWND configFunc(const char *type_string, HWND parent, const char *initConfigString)
{
    return CreateDialogParam(g_hInst, MAKEINTRESOURCE(IDD_SURFACEEDIT_CSI), parent, dlgProcMainConfig, (LPARAM)initConfigString);
}

reaper_csurf_reg_t csurf_integrator_reg =
{
    "CSI",
    Control_Surface_Integrator,
    createFunc,
    configFunc,
};


