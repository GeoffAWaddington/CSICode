//
//  control_surface_integrator.cpp
//  reaper_control_surface_integrator
//
//

#include "control_surface_integrator.h"
#include "control_surface_midi_widgets.h"
#include "control_surface_action_contexts.h"
#include "control_surface_Reaper_actions.h"
#include "control_surface_manager_actions.h"
#include "control_surface_integrator_ui.h"

extern reaper_plugin_info_t *g_reaper_plugin_info;

WDL_Mutex WDL_mutex;

string GetLineEnding()
{
#ifdef WIN32
    return "\n";
#else
    return "\r\n" ;
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct MidiInputPort
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    int port_ = 0;
    midi_Input* midiInput_ = nullptr;
    
    MidiInputPort(int port, midi_Input* midiInput) : port_(port), midiInput_(midiInput) {}
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct MidiOutputPort
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    int port_ = 0;
    midi_Output* midiOutput_ = nullptr;
    
    MidiOutputPort(int port, midi_Output* midiOutput) : port_(port), midiOutput_(midiOutput) {}
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi I/O Manager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static map<int, shared_ptr<MidiInputPort>> midiInputs_;
static map<int, shared_ptr<MidiOutputPort>> midiOutputs_;

static midi_Input* GetMidiInputForPort(int inputPort)
{
    if(midiInputs_.count(inputPort) > 0)
        return midiInputs_[inputPort]->midiInput_; // return existing
    
    // otherwise make new
    midi_Input* newInput = DAW::CreateMIDIInput(inputPort);
    
    if(newInput)
    {
        newInput->start();
        midiInputs_[inputPort] = make_shared<MidiInputPort>(inputPort, newInput);
        return newInput;
    }
    
    return nullptr;
}

static midi_Output* GetMidiOutputForPort(int outputPort)
{
    if(midiOutputs_.count(outputPort) > 0)
        return midiOutputs_[outputPort]->midiOutput_; // return existing
    
    // otherwise make new
    midi_Output* newOutput = DAW::CreateMIDIOutput(outputPort, false, NULL);
    
    if(newOutput)
    {
        midiOutputs_[outputPort] = make_shared<MidiOutputPort>(outputPort, newOutput);
        return newOutput;
    }
    
    return nullptr;
}

void ShutdownMidiIO()
{
    for(auto [index, input] : midiInputs_)
        input->midiInput_->stop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OSC I/O Manager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static map<string, shared_ptr<oscpkt::UdpSocket>> inputSockets_;
static map<string, shared_ptr<oscpkt::UdpSocket>> outputSockets_;

static shared_ptr<oscpkt::UdpSocket> GetInputSocketForPort(string surfaceName, int inputPort)
{
    if(inputSockets_.count(surfaceName) > 0)
        return inputSockets_[surfaceName]; // return existing
    
    // otherwise make new
    shared_ptr<oscpkt::UdpSocket> newInputSocket = make_shared<oscpkt::UdpSocket>();
    
    if(newInputSocket)
    {
        newInputSocket->bindTo(inputPort);
        
        if (! newInputSocket->isOk())
        {
            //cerr << "Error opening port " << PORT_NUM << ": " << inSocket_.errorMessage() << "\n";
            return nullptr;
        }
        
        inputSockets_[surfaceName] = newInputSocket;
        
        return inputSockets_[surfaceName];
    }
    
    return nullptr;
}

static shared_ptr<oscpkt::UdpSocket> GetOutputSocketForAddressAndPort(string surfaceName, string address, int outputPort)
{
    if(outputSockets_.count(surfaceName) > 0)
        return outputSockets_[surfaceName]; // return existing
    
    // otherwise make new
    shared_ptr<oscpkt::UdpSocket> newOutputSocket = make_shared<oscpkt::UdpSocket>();
    
    if(newOutputSocket)
    {
        if( ! newOutputSocket->connectTo(address, outputPort))
        {
            //cerr << "Error connecting " << remoteDeviceIP_ << ": " << outSocket_.errorMessage() << "\n";
            return nullptr;
        }
        
        //newOutputSocket->bindTo(outputPort);
        
        if ( ! newOutputSocket->isOk())
        {
            //cerr << "Error opening port " << outPort_ << ": " << outSocket_.errorMessage() << "\n";
            return nullptr;
        }

        outputSockets_[surfaceName] = newOutputSocket;
        
        return outputSockets_[surfaceName];
    }
    
    return nullptr;
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ActionTemplate
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
    string widgetName = "";
    int modifier = 0;
    string actionName = "";
    vector<string> params;
    bool isValueInverted = false;
    bool isFeedbackInverted = false;
    double holdDelayAmount = 0.0;
    bool isDecrease = false;
    bool isIncrease = false;
    bool provideFeedback = false;
};

static void listZoneFiles(const string &path, vector<string> &results)
{
    filesystem::path zonePath { path };
    
    if(filesystem::exists(path) && filesystem::is_directory(path))
        for(auto& file : filesystem::recursive_directory_iterator(path))
            if(file.path().extension() == ".zon")
                results.push_back(file.path().string());
}

static void GetWidgetNameAndModifiers(string line, shared_ptr<ActionTemplate> actionTemplate)
{
    istringstream modifiersAndWidgetName(line);
    vector<string> tokens;
    string token;
    
    ModifierManager modifierManager;
    
    while (getline(modifiersAndWidgetName, token, '+'))
        tokens.push_back(token);
    
    actionTemplate->widgetName = tokens[tokens.size() - 1];
       
    if(tokens.size() > 1)
    {
        for(int i = 0; i < tokens.size() - 1; i++)
        {
            if(tokens[i].find("Touch") != string::npos)
                actionTemplate->modifier += 1;
            else if(tokens[i] == "Toggle")
                actionTemplate->modifier += 2;
            
            else if(tokens[i] == "Shift")
                modifierManager.SetShift(true);
            else if(tokens[i] == "Option")
                modifierManager.SetOption(true);
            else if(tokens[i] == "Control")
                modifierManager.SetControl(true);
            else if(tokens[i] == "Alt")
                modifierManager.SetAlt(true);
            else if(tokens[i] == "Flip")
                modifierManager.SetFlip(true);
            else if(tokens[i] == "Global")
                modifierManager.SetGlobal(true);

            else if(tokens[i] == "Marker")
                modifierManager.SetMarker(true);
            else if(tokens[i] == "Nudge")
                modifierManager.SetNudge(true);
            else if(tokens[i] == "Zoom")
                modifierManager.SetZoom(true);
            else if(tokens[i] == "Scrub")
                modifierManager.SetScrub(true);
            
            else if(tokens[i] == "Invert")
                actionTemplate->isValueInverted = true;
            else if(tokens[i] == "InvertFB")
                actionTemplate->isFeedbackInverted = true;
            else if(tokens[i] == "Hold")
                actionTemplate->holdDelayAmount = 1.0;
            else if(tokens[i] == "Decrease")
                actionTemplate->isDecrease = true;
            else if(tokens[i] == "Increase")
                actionTemplate->isIncrease = true;
        }
    }
    
    actionTemplate->modifier += modifierManager.GetModifierValue();
}

static void BuildActionTemplate(vector<string> tokens, map<string, map<int, vector<shared_ptr<ActionTemplate>>>> &actionTemplatesDictionary)
{
    string feedbackIndicator = "";
    
    vector<string> params;
    for(int i = 1; i < tokens.size(); i++)
    {
        if(tokens[i] == "Feedback=Yes" || tokens[i] == "Feedback=No")
            feedbackIndicator = tokens[i];
        else
            params.push_back(tokens[i]);
    }

    shared_ptr<ActionTemplate> currentActionTemplate = make_shared<ActionTemplate>();
    
    currentActionTemplate->actionName = tokens[1];
    currentActionTemplate->params = params;
    
    GetWidgetNameAndModifiers(tokens[0], currentActionTemplate);

    actionTemplatesDictionary[currentActionTemplate->widgetName][currentActionTemplate->modifier].push_back(currentActionTemplate);
    
    if(actionTemplatesDictionary[currentActionTemplate->widgetName][currentActionTemplate->modifier].size() == 1)
    {
        if(feedbackIndicator == "" || feedbackIndicator == "Feedback=Yes")
            currentActionTemplate->provideFeedback = true;
    }
    else if(feedbackIndicator == "Feedback=Yes")
    {
        for(auto actionTemplate : actionTemplatesDictionary[currentActionTemplate->widgetName][currentActionTemplate->modifier])
            actionTemplate->provideFeedback = false;
        
        actionTemplatesDictionary[currentActionTemplate->widgetName][currentActionTemplate->modifier].back()->provideFeedback = true;
    }
}

static void ProcessSurfaceFXLayout(string filePath, vector<vector<string>> &surfaceFXLayout,  vector<vector<string>> &surfaceFXLayoutTemplate)
{
    try
    {
        ifstream file(filePath);
        
        for (string line; getline(file, line) ; )
        {
            line = regex_replace(line, regex(TabChars), " ");
            line = regex_replace(line, regex(CRLFChars), "");
            
            line = line.substr(0, line.find("//")); // remove trailing commewnts
            
            // Trim leading and trailing spaces
            line = regex_replace(line, regex("^\\s+|\\s+$"), "", regex_constants::format_default);
            
            if(line == "") // ignore blank lines
                continue;
        
            vector<string> tokens = GetTokens(line);
            
            if(tokens[0] != "Zone" && tokens[0] != "ZoneEnd")
            {
                if(tokens[0][0] == '#')
                {
                    tokens[0] = tokens[0].substr(1, tokens[0].length() - 1);
                    surfaceFXLayoutTemplate.push_back(tokens);
                }
                else
                {
                    surfaceFXLayout.push_back(tokens);
                    
                    if(tokens.size() > 1 && tokens[1] == "FXParam")
                    {
                        vector<string> widgetAction;
                        
                        widgetAction.push_back("WidgetAction");
                        widgetAction.push_back(tokens[1]);

                        surfaceFXLayoutTemplate.push_back(widgetAction);
                    }
                    if(tokens.size() > 1 && tokens[1] == "FixedTextDisplay")
                    {
                        vector<string> widgetAction;
                        
                        widgetAction.push_back("AliasDisplayAction");
                        widgetAction.push_back(tokens[1]);

                        surfaceFXLayoutTemplate.push_back(widgetAction);
                    }
                    if(tokens.size() > 1 && tokens[1] == "FXParamValueDisplay")
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

static void ProcessFXLayouts(string filePath, vector<CSILayoutInfo> &fxLayouts)
{
    try
    {
        ifstream file(filePath);
        
        for (string line; getline(file, line) ; )
        {
            line = regex_replace(line, regex(TabChars), " ");
            line = regex_replace(line, regex(CRLFChars), "");
            
            line = line.substr(0, line.find("//")); // remove trailing commewnts
            
            // Trim leading and trailing spaces
            line = regex_replace(line, regex("^\\s+|\\s+$"), "", regex_constants::format_default);
            
            if(line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
        
            if(line.find("Zone") == string::npos)
            {
                vector<string> tokens = GetTokens(line);
                
                CSILayoutInfo info;

                if(tokens.size() == 3)
                {
                    info.modifiers = tokens[0];
                    info.suffix = tokens[1];
                    info.channelCount = atoi(tokens[2].c_str());
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

static void ProcessFXBoilerplate(string filePath, vector<string> &fxBoilerplate)
{
    try
    {
        ifstream file(filePath);
            
        for (string line; getline(file, line) ; )
        {
            line = regex_replace(line, regex(TabChars), " ");
            line = regex_replace(line, regex(CRLFChars), "");
            
            line = line.substr(0, line.find("//")); // remove trailing commewnts
            
            // Trim leading and trailing spaces
            line = regex_replace(line, regex("^\\s+|\\s+$"), "", regex_constants::format_default);
            
            if(line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
        
            if(line.find("Zone") != 0)
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

static void PreProcessZoneFile(string filePath, shared_ptr<ZoneManager> zoneManager)
{
    string zoneName = "";
    
    try
    {
        ifstream file(filePath);
        
        CSIZoneInfo info;
        info.filePath = filePath;
                 
        for (string line; getline(file, line) ; )
        {
            line = regex_replace(line, regex(TabChars), " ");
            line = regex_replace(line, regex(CRLFChars), "");
            
            line = line.substr(0, line.find("//")); // remove trailing commewnts
            
            // Trim leading and trailing spaces
            line = regex_replace(line, regex("^\\s+|\\s+$"), "", regex_constants::format_default);
            
            if(line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
            
            vector<string> tokens(GetTokens(line));
                       
            if(tokens[0] == "Zone" && tokens.size() > 1)
            {
                zoneName = tokens[1];
                info.alias = tokens.size() > 2 ? tokens[2] : zoneName;
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

static void ExpandLine(int numChannels, vector<string> &tokens)
{
    if(tokens.size() == numChannels)
        return;
    else if(tokens.size() != 1)
        return;

    string templateString = tokens[0];
    tokens.pop_back();
    
    for(int i = 1; i <= numChannels; i++)
        tokens.push_back(regex_replace(templateString, regex("[|]"), to_string(i)));
}

static vector<rgba_color> GetColorValues(vector<string> colors)
{
    vector<rgba_color> colorValues;
    
    for(auto color : colors)
    {
        rgba_color colorValue;
        
        if(color.length() == 7)
        {
            regex pattern("#([0-9a-fA-F]{6})");
            smatch match;
            if (regex_match(color, match, pattern))
            {
                sscanf(match.str(1).c_str(), "%2x%2x%2x", &colorValue.r, &colorValue.g, &colorValue.b);
                colorValues.push_back(colorValue);
            }
        }
        else if(color.length() == 9)
        {
            regex pattern("#([0-9a-fA-F]{8})");
            smatch match;
            if (regex_match(color, match, pattern))
            {
                sscanf(match.str(1).c_str(), "%2x%2x%2x%2x", &colorValue.r, &colorValue.g, &colorValue.b, &colorValue.a);
                colorValues.push_back(colorValue);
            }
        }
    }
    
    return colorValues;
}

static void ProcessZoneFile(string filePath, shared_ptr<ZoneManager> zoneManager, vector<shared_ptr<Navigator>> &navigators, vector<shared_ptr<Zone>> &zones, shared_ptr<Zone> enclosingZone)
{
    bool isInIncludedZonesSection = false;
    vector<string> includedZones;
    bool isInSubZonesSection = false;
    vector<string> subZones;
    bool isInAssociatedZonesSection = false;
    vector<string> associatedZones;
    
    map<string, map<int, vector<shared_ptr<ActionTemplate>>>> actionTemplatesDictionary;
    
    string zoneName = "";
    string zoneAlias = "";
    string actionName = "";
    int lineNumber = 0;
   
    try
    {
        ifstream file(filePath);
        
        for (string line; getline(file, line) ; )
        {
            line = regex_replace(line, regex(TabChars), " ");
            line = regex_replace(line, regex(CRLFChars), "");
            
            line = line.substr(0, line.find("//")); // remove trailing commewnts
            
            lineNumber++;
            
            // Trim leading and trailing spaces
            line = regex_replace(line, regex("^\\s+|\\s+$"), "", regex_constants::format_default);
            
            if(line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
            
            if(line == BeginAutoSection || line == EndAutoSection)
                continue;
            
            vector<string> tokens(GetTokens(line));
            
            if(tokens.size() > 0)
            {
                if(tokens[0] == "Zone")
                {
                    zoneName = tokens.size() > 1 ? tokens[1] : "";
                    zoneAlias = tokens.size() > 2 ? tokens[2] : "";
                }
                else if(tokens[0] == "ZoneEnd" && zoneName != "")
                {
                    for(int i = 0; i < navigators.size(); i++)
                    {
                        string numStr = to_string(i + 1);
                        
                        map<string, string> expandedTouchIds;
                                                
                        shared_ptr<Zone> zone;
                        
                        if(enclosingZone == nullptr)
                            zone = make_shared<Zone>(zoneManager, navigators[i], i, zoneName, zoneAlias, filePath, includedZones, associatedZones);
                        else
                            zone = make_shared<SubZone>(zoneManager, navigators[i], i, zoneName, zoneAlias, filePath, includedZones, associatedZones, enclosingZone);
                                                
                        if(zoneName == "Home")
                            zoneManager->SetHomeZone(zone);
                                               
                        if(zoneName == "FocusedFXParam")
                            zoneManager->SetFocusedFXParamZone(zone);
                        
                        zones.push_back(zone);
                        
                        for(auto [widgetName, modifiedActionTemplates] : actionTemplatesDictionary)
                        {
                            string surfaceWidgetName = widgetName;
                            
                            if(navigators.size() > 1)
                                surfaceWidgetName = regex_replace(surfaceWidgetName, regex("[|]"), to_string(i + 1));
                            
                            if(enclosingZone != nullptr && enclosingZone->GetChannelNumber() != 0)
                                surfaceWidgetName = regex_replace(surfaceWidgetName, regex("[|]"), to_string(enclosingZone->GetChannelNumber()));
                            
                            shared_ptr<Widget> widget = zoneManager->GetSurface()->GetWidgetByName(surfaceWidgetName);
                                                        
                            if(widget == nullptr)
                                continue;
 
                            zone->AddWidget(widget, widget->GetName());
                                                        
                            for(auto [modifier, actionTemplates] : modifiedActionTemplates)
                            {
                                for(auto actionTemplate : actionTemplates)
                                {
                                    string actionName = regex_replace(actionTemplate->actionName, regex("[|]"), numStr);

                                    vector<string> memberParams;
                                    for(int j = 0; j < actionTemplate->params.size(); j++)
                                        memberParams.push_back(regex_replace(actionTemplate->params[j], regex("[|]"), numStr));
                                    
                                    shared_ptr<ActionContext> context = TheManager->GetActionContext(actionName, widget, zone, memberParams);
                                        
                                    context->SetProvideFeedback(actionTemplate->provideFeedback);
                                    
                                    if(actionTemplate->isValueInverted)
                                        context->SetIsValueInverted();
                                    
                                    if(actionTemplate->isFeedbackInverted)
                                        context->SetIsFeedbackInverted();
                                    
                                    if(actionTemplate->holdDelayAmount != 0.0)
                                        context->SetHoldDelayAmount(actionTemplate->holdDelayAmount);
                                    
                                    if(actionTemplate->isDecrease)
                                        context->SetRange({ -2.0, 1.0 });
                                    else if(actionTemplate->isIncrease)
                                        context->SetRange({ 0.0, 2.0 });
                                   
                                    zone->AddActionContext(widget, modifier, context);
                                }
                            }
                        }
                    
                        if(enclosingZone == nullptr && subZones.size() > 0)
                            zone->InitSubZones(subZones, zone);
                    }
                                    
                    includedZones.clear();
                    subZones.clear();
                    associatedZones.clear();
                    actionTemplatesDictionary.clear();
                    
                    break;
                }
                                
                else if(tokens[0] == "IncludedZones")
                    isInIncludedZonesSection = true;
                
                else if(tokens[0] == "IncludedZonesEnd")
                    isInIncludedZonesSection = false;
                
                else if(isInIncludedZonesSection)
                    includedZones.push_back(tokens[0]);

                else if(tokens[0] == "SubZones")
                    isInSubZonesSection = true;
                
                else if(tokens[0] == "SubZonesEnd")
                    isInSubZonesSection = false;
                
                else if(isInSubZonesSection)
                    subZones.push_back(tokens[0]);
                 
                else if(tokens[0] == "AssociatedZones")
                    isInAssociatedZonesSection = true;
                
                else if(tokens[0] == "AssociatedZonesEnd")
                    isInAssociatedZonesSection = false;
                
                else if(isInAssociatedZonesSection)
                    associatedZones.push_back(tokens[0]);
                               
                else if(tokens.size() > 1)
                    BuildActionTemplate(tokens, actionTemplatesDictionary);
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

static void SetColor(vector<string> params, bool &supportsColor, bool &supportsTrackColor, vector<rgba_color> &colorValues)
{
    vector<int> rawValues;
    vector<string> hexColors;
    
    auto openCurlyBrace = find(params.begin(), params.end(), "{");
    auto closeCurlyBrace = find(params.begin(), params.end(), "}");
    
    if(openCurlyBrace != params.end() && closeCurlyBrace != params.end())
    {
        for(auto it = openCurlyBrace + 1; it != closeCurlyBrace; ++it)
        {
            string strVal = *(it);
            
            if(strVal.length() > 0 && strVal[0] == '#')
            {
                hexColors.push_back(strVal);
                continue;
            }
            
            if(strVal == "Track")
            {
                supportsTrackColor = true;
                break;
            }
            else
            {
                if(regex_match(strVal, regex("[0-9]+")))
                {
                    int value = stoi(strVal);
                    value = value < 0 ? 0 : value;
                    value = value > 255 ? 255 : value;
                    
                    rawValues.push_back(value);
                }
            }
        }
        
        if(hexColors.size() > 0)
        {
            supportsColor = true;

            vector<rgba_color> colors = GetColorValues(hexColors);
            
            for(auto color : colors)
                colorValues.push_back(color);
        }
        else if(rawValues.size() % 3 == 0 && rawValues.size() > 2)
        {
            supportsColor = true;
            
            for(int i = 0; i < rawValues.size(); i += 3)
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

static void GetSteppedValues(shared_ptr<Widget> widget, shared_ptr<Action> action,  shared_ptr<Zone> zone, int paramNumber, vector<string> params, map<string, string> widgetProperties, double &deltaValue, vector<double> &acceleratedDeltaValues, double &rangeMinimum, double &rangeMaximum, vector<double> &steppedValues, vector<int> &acceleratedTickValues)
{
    auto openSquareBrace = find(params.begin(), params.end(), "[");
    auto closeSquareBrace = find(params.begin(), params.end(), "]");
    
    if(openSquareBrace != params.end() && closeSquareBrace != params.end())
    {
        for(auto it = openSquareBrace + 1; it != closeSquareBrace; ++it)
        {
            string strVal = *(it);
            
            if(regex_match(strVal, regex("-?[0-9]+[.][0-9]+")) || regex_match(strVal, regex("-?[0-9]+")))
                steppedValues.push_back(stod(strVal));
            else if(regex_match(strVal, regex("[(]-?[0-9]+[.][0-9]+[)]")))
                deltaValue = stod(strVal.substr( 1, strVal.length() - 2 ));
            else if(regex_match(strVal, regex("[(]-?[0-9]+[)]")))
                acceleratedTickValues.push_back(stod(strVal.substr( 1, strVal.length() - 2 )));
            else if(regex_match(strVal, regex("[(](-?[0-9]+[.][0-9]+[,])+-?[0-9]+[.][0-9]+[)]")))
            {
                istringstream acceleratedDeltaValueStream(strVal.substr( 1, strVal.length() - 2 ));
                string deltaValue;
                
                while (getline(acceleratedDeltaValueStream, deltaValue, ','))
                    acceleratedDeltaValues.push_back(stod(deltaValue));
            }
            else if(regex_match(strVal, regex("[(](-?[0-9]+[,])+-?[0-9]+[)]")))
            {
                istringstream acceleratedTickValueStream(strVal.substr( 1, strVal.length() - 2 ));
                string tickValue;
                
                while (getline(acceleratedTickValueStream, tickValue, ','))
                    acceleratedTickValues.push_back(stod(tickValue));
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
                    double firstValue = stod(range_tokens[0]);
                    double lastValue = stod(range_tokens[1]);
                    
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
    
    if(deltaValue == 0.0 && widget->GetStepSize() != 0.0)
        deltaValue = widget->GetStepSize();
    
    if(acceleratedDeltaValues.size() == 0 && widget->GetAccelerationValues().size() != 0)
        acceleratedDeltaValues = widget->GetAccelerationValues();
         
    if(steppedValues.size() > 0 && acceleratedTickValues.size() == 0)
    {
        double stepSize = deltaValue;
        
        if(stepSize != 0.0)
        {
            stepSize *= 10000.0;
            int baseTickCount = TheManager->GetBaseTickCount(steppedValues.size());
            int tickCount = int(baseTickCount / stepSize + 0.5);
            acceleratedTickValues.push_back(tickCount);
        }
    }
}

//////////////////////////////////////////////////////////////////////////////
// Widgets
//////////////////////////////////////////////////////////////////////////////
static void ProcessMidiWidget(int &lineNumber, ifstream &surfaceTemplateFile, vector<string> tokens, shared_ptr<Midi_ControlSurface> surface, map<string, double> stepSizes, map<string, map<int, int>> accelerationValuesForDecrement, map<string, map<int, int>> accelerationValuesForIncrement, map<string, vector<double>> accelerationValues)
{
    if(tokens.size() < 2)
        return;
    
    string widgetName = tokens[1];
    
    string widgetClass = "";
    
    if(tokens.size() > 2)
        widgetClass = tokens[2];

    shared_ptr<Widget> widget = make_shared<Widget>(surface, widgetName);
       
    surface->AddWidget(widget);

    vector<vector<string>> tokenLines;
    
    for (string line; getline(surfaceTemplateFile, line) ; )
    {
        line = regex_replace(line, regex(TabChars), " ");
        line = regex_replace(line, regex(CRLFChars), "");

        lineNumber++;
        
        if(line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
            continue;
        
        vector<string> tokens(GetTokens(line));
        
        if(tokens[0] == "WidgetEnd")    // finito baybay - Widget list complete
            break;
        
        tokenLines.push_back(tokens);
    }
    
    if(tokenLines.size() < 1)
        return;
    
    for(int i = 0; i < tokenLines.size(); i++)
    {
        int size = tokenLines[i].size();
        
        string widgetType = tokenLines[i][0];

        shared_ptr<MIDI_event_ex_t> message1 = nullptr;
        shared_ptr<MIDI_event_ex_t> message2 = nullptr;

        int twoByteKey = 0;
        
        if(size > 3)
        {
            message1 = make_shared<MIDI_event_ex_t>(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3]));
            twoByteKey = message1->midi_message[0] * 0x10000 + message1->midi_message[1] * 0x100;
        }
        if(size > 6)
            message2 = make_shared<MIDI_event_ex_t>(strToHex(tokenLines[i][4]), strToHex(tokenLines[i][5]), strToHex(tokenLines[i][6]));
        
        // Control Signal Generators
        
        if(widgetType == "AnyPress" && (size == 4 || size == 7))
            surface->AddCSIMessageGenerator(make_shared<AnyPress_Midi_CSIMessageGenerator>(widget, message1), twoByteKey);
        if(widgetType == "Press" && size == 4)
            surface->AddCSIMessageGenerator(make_shared<PressRelease_Midi_CSIMessageGenerator>(widget, message1), message1->midi_message[0] * 0x10000 + message1->midi_message[1] * 0x100 + message1->midi_message[2]);
        else if(widgetType == "Press" && size == 7)
        {
            shared_ptr<PressRelease_Midi_CSIMessageGenerator> generator = make_shared<PressRelease_Midi_CSIMessageGenerator>(widget, message1, message2);
            
            surface->AddCSIMessageGenerator(generator, message1->midi_message[0] * 0x10000 + message1->midi_message[1] * 0x100 + message1->midi_message[2]);
            surface->AddCSIMessageGenerator(generator, message2->midi_message[0] * 0x10000 + message2->midi_message[1] * 0x100 + message2->midi_message[2]);
        }
        else if(widgetType == "Fader14Bit" && size == 4)
            surface->AddCSIMessageGenerator(make_shared<Fader14Bit_Midi_CSIMessageGenerator>(widget, message1), message1->midi_message[0] * 0x10000);
        else if(widgetType == "Fader7Bit" && size== 4)
            surface->AddCSIMessageGenerator(make_shared<Fader7Bit_Midi_CSIMessageGenerator>(widget, message1), twoByteKey);
        else if(widgetType == "Encoder" && size == 4 && widgetClass == "RotaryWidgetClass")
        {
            if(stepSizes.count(widgetClass) > 0 && accelerationValuesForDecrement.count(widgetClass) > 0 && accelerationValuesForIncrement.count(widgetClass) > 0 && accelerationValues.count(widgetClass) > 0)
                surface->AddCSIMessageGenerator(make_shared<AcceleratedPreconfiguredEncoder_Midi_CSIMessageGenerator>(widget, message1, stepSizes[widgetClass], accelerationValuesForDecrement[widgetClass], accelerationValuesForIncrement[widgetClass], accelerationValues[widgetClass]), twoByteKey);
        }
        else if(widgetType == "Encoder" && size == 4)
            surface->AddCSIMessageGenerator(make_shared<Encoder_Midi_CSIMessageGenerator>(widget, message1), twoByteKey);
        else if(widgetType == "Encoder" && size > 4)
            surface->AddCSIMessageGenerator(make_shared<AcceleratedEncoder_Midi_CSIMessageGenerator>(widget, message1, tokenLines[i]), twoByteKey);
        else if(widgetType == "MFTEncoder" && size > 4)
            surface->AddCSIMessageGenerator(make_shared<MFT_AcceleratedEncoder_Midi_CSIMessageGenerator>(widget, message1, tokenLines[i]), twoByteKey);
        else if(widgetType == "EncoderPlain" && size == 4)
            surface->AddCSIMessageGenerator(make_shared<EncoderPlain_Midi_CSIMessageGenerator>(widget, message1), twoByteKey);
        else if(widgetType == "Encoder7Bit" && size == 4)
            surface->AddCSIMessageGenerator(make_shared<Encoder7Bit_Midi_CSIMessageGenerator>(widget, message1), twoByteKey);
        else if(widgetType == "Touch" && size == 7)
        {
            shared_ptr<Touch_Midi_CSIMessageGenerator> generator = make_shared<Touch_Midi_CSIMessageGenerator>(widget, message1, message2);
            
            surface->AddCSIMessageGenerator(generator, message1->midi_message[0] * 0x10000 + message1->midi_message[1] * 0x100 + message1->midi_message[2]);
            surface->AddCSIMessageGenerator(generator, message2->midi_message[0] * 0x10000 + message2->midi_message[1] * 0x100 + message2->midi_message[2]);
        }
        
        // Feedback Processors
        shared_ptr<FeedbackProcessor> feedbackProcessor = nullptr;

        if(widgetType == "FB_TwoState" && size == 7)
        {
            feedbackProcessor = make_shared<TwoState_Midi_FeedbackProcessor>(surface, widget, message1, message2);
        }
        else if(widgetType == "FB_NovationLaunchpadMiniRGB7Bit" && size == 4)
        {
            feedbackProcessor = make_shared<NovationLaunchpadMiniRGB7Bit_Midi_FeedbackProcessor>(surface, widget, message1);
        }
        else if(widgetType == "FB_MFT_RGB" && size == 4)
        {
            feedbackProcessor = make_shared<MFT_RGB_Midi_FeedbackProcessor>(surface, widget, message1);
        }
        else if(widgetType == "FB_FaderportRGB" && size == 4)
        {
            feedbackProcessor = make_shared<FaderportRGB_Midi_FeedbackProcessor>(surface, widget, message1);
        }
        else if(widgetType == "FB_FaderportTwoStateRGB" && size == 4)
        {
            feedbackProcessor = make_shared<FPTwoStateRGB_Midi_FeedbackProcessor>(surface, widget, message1);
        }
        else if(widgetType == "FB_FaderportValueBar"  && size == 2)
        {
            feedbackProcessor = make_shared<FPValueBar_Midi_FeedbackProcessor>(surface, widget, stoi(tokenLines[i][1]));
        }
        else if((widgetType == "FB_FPVUMeter") && size == 2)
        {
            feedbackProcessor = make_shared<FPVUMeter_Midi_FeedbackProcessor>(surface, widget, stoi(tokenLines[i][1]));
        }
        else if(widgetType == "FB_Fader14Bit" && size == 4)
        {
            feedbackProcessor = make_shared<Fader14Bit_Midi_FeedbackProcessor>(surface, widget, message1);
        }
        else if(widgetType == "FB_Fader7Bit" && size == 4)
        {
            feedbackProcessor = make_shared<Fader7Bit_Midi_FeedbackProcessor>(surface, widget, message1);
        }
        else if(widgetType == "FB_Encoder" && size == 4)
        {
            feedbackProcessor = make_shared<Encoder_Midi_FeedbackProcessor>(surface, widget, message1);
        }
        else if(widgetType == "FB_ConsoleOneVUMeter" && size == 4)
        {
            feedbackProcessor = make_shared<ConsoleOneVUMeter_Midi_FeedbackProcessor>(surface, widget, message1);
        }
        else if(widgetType == "FB_ConsoleOneGainReductionMeter" && size == 4)
        {
            feedbackProcessor = make_shared<ConsoleOneGainReductionMeter_Midi_FeedbackProcessor>(surface, widget, message1);
        }
        else if(widgetType == "FB_MCUTimeDisplay" && size == 1)
        {
            feedbackProcessor = make_shared<MCU_TimeDisplay_Midi_FeedbackProcessor>(surface, widget);
        }
        else if(widgetType == "FB_MCUAssignmentDisplay" && size == 1)
        {
            feedbackProcessor = make_shared<FB_MCU_AssignmentDisplay_Midi_FeedbackProcessor>(surface, widget);
        }
        else if(widgetType == "FB_QConProXMasterVUMeter" && size == 2)
        {
            feedbackProcessor = make_shared<QConProXMasterVUMeter_Midi_FeedbackProcessor>(surface, widget, stoi(tokenLines[i][1]));
        }
        else if((widgetType == "FB_MCUVUMeter" || widgetType == "FB_MCUXTVUMeter") && size == 2)
        {
            int displayType = widgetType == "FB_MCUVUMeter" ? 0x14 : 0x15;
            
            feedbackProcessor = make_shared<MCUVUMeter_Midi_FeedbackProcessor>(surface, widget, displayType, stoi(tokenLines[i][1]));
            
            surface->SetHasMCUMeters(displayType);
        }
        else if(widgetType == "FB_SCE24LEDButton" && size == 4)
        {
            feedbackProcessor = make_shared<SCE24TwoStateLED_Midi_FeedbackProcessor>(surface, widget, make_shared<MIDI_event_ex_t>(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]) + 0x60, strToHex(tokenLines[i][3])));
        }
        else if(widgetType == "FB_SCE24OLEDButton" && size == 4)
        {
            feedbackProcessor = make_shared<SCE24OLED_Midi_FeedbackProcessor>(surface, widget, make_shared<MIDI_event_ex_t>(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]) + 0x60, strToHex(tokenLines[i][3])));
        }
        else if(widgetType == "FB_SCE24Encoder" && size == 4)
        {
            feedbackProcessor = make_shared<SCE24Encoder_Midi_FeedbackProcessor>(surface, widget, message1);
        }
        else if(widgetType == "FB_SCE24EncoderText" && size == 4)
        {
            feedbackProcessor = make_shared<SCE24Text_Midi_FeedbackProcessor>(surface, widget, message1);
        }
        else if((widgetType == "FB_MCUDisplayUpper" || widgetType == "FB_MCUDisplayLower" || widgetType == "FB_MCUXTDisplayUpper" || widgetType == "FB_MCUXTDisplayLower") && size == 2)
        {
            if(widgetType == "FB_MCUDisplayUpper")
                feedbackProcessor = make_shared<MCUDisplay_Midi_FeedbackProcessor>(surface, widget, 0, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_MCUDisplayLower")
                feedbackProcessor = make_shared<MCUDisplay_Midi_FeedbackProcessor>(surface, widget, 1, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_MCUXTDisplayUpper")
                feedbackProcessor = make_shared<MCUDisplay_Midi_FeedbackProcessor>(surface, widget, 0, 0x15, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_MCUXTDisplayLower")
                feedbackProcessor = make_shared<MCUDisplay_Midi_FeedbackProcessor>(surface, widget, 1, 0x15, 0x12, stoi(tokenLines[i][1]));
        }
        else if((widgetType == "FB_XTouchDisplayUpper" || widgetType == "FB_XTouchDisplayLower" || widgetType == "FB_XTouchXTDisplayUpper" || widgetType == "FB_XTouchXTDisplayLower") && size == 2)
        {
            if(widgetType == "FB_XTouchDisplayUpper")
                feedbackProcessor = make_shared<XTouchDisplay_Midi_FeedbackProcessor>(surface, widget, 0, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_XTouchDisplayLower")
                feedbackProcessor = make_shared<XTouchDisplay_Midi_FeedbackProcessor>(surface, widget, 1, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_XTouchXTDisplayUpper")
                feedbackProcessor = make_shared<XTouchDisplay_Midi_FeedbackProcessor>(surface, widget, 0, 0x15, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_XTouchXTDisplayLower")
                feedbackProcessor = make_shared<XTouchDisplay_Midi_FeedbackProcessor>(surface, widget, 1, 0x15, 0x12, stoi(tokenLines[i][1]));
            
            if(feedbackProcessor)
                surface->AddTrackColorFeedbackProcessor(feedbackProcessor);
        }
        else if((widgetType == "FB_C4DisplayUpper" || widgetType == "FB_C4DisplayLower") && size == 3)
        {
            if(widgetType == "FB_C4DisplayUpper")
                feedbackProcessor = make_shared<MCUDisplay_Midi_FeedbackProcessor>(surface, widget, 0, 0x17, stoi(tokenLines[i][1]) + 0x30, stoi(tokenLines[i][2]));
            else if(widgetType == "FB_C4DisplayLower")
                feedbackProcessor = make_shared<MCUDisplay_Midi_FeedbackProcessor>(surface, widget, 1, 0x17, stoi(tokenLines[i][1]) + 0x30, stoi(tokenLines[i][2]));
        }
        else if((widgetType == "FB_FP8ScribbleLine1" || widgetType == "FB_FP16ScribbleLine1"
                 || widgetType == "FB_FP8ScribbleLine2" || widgetType == "FB_FP16ScribbleLine2"
                 || widgetType == "FB_FP8ScribbleLine3" || widgetType == "FB_FP16ScribbleLine3"
                 || widgetType == "FB_FP8ScribbleLine4" || widgetType == "FB_FP16ScribbleLine4") && size == 2)
        {
            if(widgetType == "FB_FP8ScribbleLine1")
                feedbackProcessor = make_shared<FPDisplay_Midi_FeedbackProcessor>(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x00);
            else if(widgetType == "FB_FP8ScribbleLine2")
                feedbackProcessor = make_shared<FPDisplay_Midi_FeedbackProcessor>(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x01);
            else if(widgetType == "FB_FP8ScribbleLine3")
                feedbackProcessor = make_shared<FPDisplay_Midi_FeedbackProcessor>(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x02);
            else if(widgetType == "FB_FP8ScribbleLine4")
                feedbackProcessor = make_shared<FPDisplay_Midi_FeedbackProcessor>(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x03);

            else if(widgetType == "FB_FP16ScribbleLine1")
                feedbackProcessor = make_shared<FPDisplay_Midi_FeedbackProcessor>(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x00);
            else if(widgetType == "FB_FP16ScribbleLine2")
                feedbackProcessor = make_shared<FPDisplay_Midi_FeedbackProcessor>(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x01);
            else if(widgetType == "FB_FP16ScribbleLine3")
                feedbackProcessor = make_shared<FPDisplay_Midi_FeedbackProcessor>(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x02);
            else if(widgetType == "FB_FP16ScribbleLine4")
                feedbackProcessor = make_shared<FPDisplay_Midi_FeedbackProcessor>(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x03);
        }
        else if((widgetType == "FB_FP8ScribbleStripMode" || widgetType == "FB_FP16ScribbleStripMode") && size == 2)
        {
            if(widgetType == "FB_FP8ScribbleStripMode")
                feedbackProcessor = make_shared<FPScribbleStripMode_Midi_FeedbackProcessor>(surface, widget, 0x02, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_FP16ScribbleStripMode")
                feedbackProcessor = make_shared<FPScribbleStripMode_Midi_FeedbackProcessor>(surface, widget, 0x16, stoi(tokenLines[i][1]));
        }
        else if((widgetType == "FB_QConLiteDisplayUpper" || widgetType == "FB_QConLiteDisplayUpperMid" || widgetType == "FB_QConLiteDisplayLowerMid" || widgetType == "FB_QConLiteDisplayLower") && size == 2)
        {
            if(widgetType == "FB_QConLiteDisplayUpper")
                feedbackProcessor = make_shared<QConLiteDisplay_Midi_FeedbackProcessor>(surface, widget, 0, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_QConLiteDisplayUpperMid")
                feedbackProcessor = make_shared<QConLiteDisplay_Midi_FeedbackProcessor>(surface, widget, 1, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_QConLiteDisplayLowerMid")
                feedbackProcessor = make_shared<QConLiteDisplay_Midi_FeedbackProcessor>(surface, widget, 2, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_QConLiteDisplayLower")
                feedbackProcessor = make_shared<QConLiteDisplay_Midi_FeedbackProcessor>(surface, widget, 3, 0x14, 0x12, stoi(tokenLines[i][1]));
        }

        if(feedbackProcessor != nullptr)
            widget->AddFeedbackProcessor(feedbackProcessor);
    }
}

static void ProcessOSCWidget(int &lineNumber, ifstream &surfaceTemplateFile, vector<string> tokens,  shared_ptr<OSC_ControlSurface> surface)
{
    if(tokens.size() < 2)
        return;
    
    shared_ptr<Widget> widget = make_shared<Widget>(surface, tokens[1]);
    
    surface->AddWidget(widget);

    vector<vector<string>> tokenLines;

    for (string line; getline(surfaceTemplateFile, line) ; )
    {
        line = regex_replace(line, regex(TabChars), " ");
        line = regex_replace(line, regex(CRLFChars), "");

        lineNumber++;
        
        if(line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
            continue;
        
        vector<string> tokens(GetTokens(line));
        
        if(tokens[0] == "WidgetEnd")    // finito baybay - Widget list complete
            break;
        
        tokenLines.push_back(tokens);
    }

    for(auto tokenLine : tokenLines)
    {
        if(tokenLine.size() > 1 && tokenLine[0] == "Control")
            surface->AddCSIMessageGenerator(make_shared<CSIMessageGenerator>(widget), tokenLine[1]);
        else if(tokenLine.size() > 1 && tokenLine[0] == "AnyPress")
            surface->AddCSIMessageGenerator(make_shared<AnyPress_CSIMessageGenerator>(widget), tokenLine[1]);
        else if (tokenLine.size() > 1 && tokenLine[0] == "MotorizedFaderWithoutTouch")
            surface->AddCSIMessageGenerator(make_shared<MotorizedFaderWithoutTouch_CSIMessageGenerator>(widget), tokenLine[1]);
        else if(tokenLine.size() > 1 && tokenLine[0] == "Touch")
            surface->AddCSIMessageGenerator(make_shared<Touch_CSIMessageGenerator>(widget), tokenLine[1]);
        else if(tokenLine.size() > 1 && tokenLine[0] == "FB_Processor")
            widget->AddFeedbackProcessor(make_shared<OSC_FeedbackProcessor>(surface, widget, tokenLine[1]));
        else if(tokenLine.size() > 1 && tokenLine[0] == "FB_IntProcessor")
            widget->AddFeedbackProcessor(make_shared<OSC_IntFeedbackProcessor>(surface, widget, tokenLine[1]));
    }
}

static void ProcessValues(vector<vector<string>> lines, map<string, double> &stepSizes, map<string, map<int, int>> &accelerationValuesForDecrement, map<string, map<int, int>> &accelerationValuesForIncrement, map<string, vector<double>> &accelerationValues)
{
    bool inStepSizes = false;
    bool inAccelerationValues = false;
        
    for(auto tokens : lines)
    {
        if(tokens.size() > 0)
        {
            if(tokens[0] == "StepSize")
            {
                inStepSizes = true;
                continue;
            }
            else if(tokens[0] == "StepSizeEnd")
            {
                inStepSizes = false;
                continue;
            }
            else if(tokens[0] == "AccelerationValues")
            {
                inAccelerationValues = true;
                continue;
            }
            else if(tokens[0] == "AccelerationValuesEnd")
            {
                inAccelerationValues = false;
                continue;
            }

            if(tokens.size() > 1)
            {
                if(inStepSizes)
                    stepSizes[tokens[0]] = stod(tokens[1]);
                else if(tokens.size() > 2 && inAccelerationValues)
                {
                    if(tokens[1] == "Dec")
                        for(int i = 2; i < tokens.size(); i++)
                            accelerationValuesForDecrement[tokens[0]][strtol(tokens[i].c_str(), nullptr, 16)] = i - 2;
                    else if(tokens[1] == "Inc")
                        for(int i = 2; i < tokens.size(); i++)
                            accelerationValuesForIncrement[tokens[0]][strtol(tokens[i].c_str(), nullptr, 16)] = i - 2;
                    else if(tokens[1] == "Val")
                        for(int i = 2; i < tokens.size(); i++)
                            accelerationValues[tokens[0]].push_back(stod(tokens[i]));
                }
            }
        }
    }
}

static void ProcessMIDIWidgetFile(string filePath, shared_ptr<Midi_ControlSurface> surface)
{
    int lineNumber = 0;
    vector<vector<string>> valueLines;
    
    map<string, double> stepSizes;
    map<string, map<int, int>> accelerationValuesForDecrement;
    map<string, map<int, int>> accelerationValuesForIncrement;
    map<string, vector<double>> accelerationValues;
    
    try
    {
        ifstream file(filePath);
        
        for (string line; getline(file, line) ; )
        {
            line = regex_replace(line, regex(TabChars), " ");
            line = regex_replace(line, regex(CRLFChars), "");
            
            lineNumber++;
            
            if(line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
                continue;
            
            vector<string> tokens(GetTokens(line));

            if(filePath[filePath.length() - 3] == 'm')
            {
                if(tokens.size() > 0 && tokens[0] != "Widget")
                    valueLines.push_back(tokens);
                
                if(tokens.size() > 0 && tokens[0] == "AccelerationValuesEnd")
                    ProcessValues(valueLines, stepSizes, accelerationValuesForDecrement, accelerationValuesForIncrement, accelerationValues);
            }

            if(tokens.size() > 0 && (tokens[0] == "Widget"))
                ProcessMidiWidget(lineNumber, file, tokens, surface, stepSizes, accelerationValuesForDecrement, accelerationValuesForIncrement, accelerationValues);
        }
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath.c_str(), lineNumber);
        DAW::ShowConsoleMsg(buffer);
    }
}

static void ProcessOSCWidgetFile(string filePath, shared_ptr<OSC_ControlSurface> surface)
{
    int lineNumber = 0;
    vector<vector<string>> valueLines;
    
    map<string, double> stepSizes;
    map<string, map<int, int>> accelerationValuesForDecrement;
    map<string, map<int, int>> accelerationValuesForIncrement;
    map<string, vector<double>> accelerationValues;
    
    try
    {
        ifstream file(filePath);
        
        for (string line; getline(file, line) ; )
        {
            line = regex_replace(line, regex(TabChars), " ");
            line = regex_replace(line, regex(CRLFChars), "");
            
            lineNumber++;
            
            if(line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
                continue;
            
            vector<string> tokens(GetTokens(line));

            if(filePath[filePath.length() - 3] == 'm')
            {
                if(tokens.size() > 0 && tokens[0] != "Widget")
                    valueLines.push_back(tokens);
                
                if(tokens.size() > 0 && tokens[0] == "AccelerationValuesEnd")
                    ProcessValues(valueLines, stepSizes, accelerationValuesForDecrement, accelerationValuesForIncrement, accelerationValues);
            }

            if(tokens.size() > 0 && (tokens[0] == "Widget"))
                ProcessOSCWidget(lineNumber, file, tokens, surface);
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
void Manager::InitActionsDictionary()
{
    actions_["MetronomePrimaryVolumeDisplay"] =     make_shared<MetronomePrimaryVolumeDisplay>();
    actions_["MetronomeSecondaryVolumeDisplay"] =   make_shared<MetronomeSecondaryVolumeDisplay>();
    actions_["MetronomePrimaryVolume"] =            make_shared<MetronomePrimaryVolume>();
    actions_["MetronomeSecondaryVolume"] =          make_shared<MetronomeSecondaryVolume>();
    actions_["Speak"] =                             make_shared<SpeakOSARAMessage>();
    actions_["SendMIDIMessage"] =                   make_shared<SendMIDIMessage>();
    actions_["SendOSCMessage"] =                    make_shared<SendOSCMessage>();
    actions_["SaveProject"] =                       make_shared<SaveProject>();
    actions_["Undo"] =                              make_shared<Undo>();
    actions_["Redo"] =                              make_shared<Redo>();
    actions_["TrackAutoMode"] =                     make_shared<TrackAutoMode>();
    actions_["GlobalAutoMode"] =                    make_shared<GlobalAutoMode>();
    actions_["TrackAutoModeDisplay"] =              make_shared<TrackAutoModeDisplay>();
    actions_["GlobalAutoModeDisplay"] =             make_shared<GlobalAutoModeDisplay>();
    actions_["CycleTrackInputMonitor"] =            make_shared<CycleTrackInputMonitor>();
    actions_["TrackInputMonitorDisplay"] =          make_shared<TrackInputMonitorDisplay>();
    actions_["MCUTimeDisplay"] =                    make_shared<MCUTimeDisplay>();
    actions_["OSCTimeDisplay"] =                    make_shared<OSCTimeDisplay>();
    actions_["NoAction"] =                          make_shared<NoAction>();
    actions_["Reaper"] =                            make_shared<ReaperAction>();
    actions_["FixedTextDisplay"] =                  make_shared<FixedTextDisplay>(); ;
    actions_["FixedRGBColorDisplay"] =              make_shared<FixedRGBColorDisplay>();
    actions_["Rewind"] =                            make_shared<Rewind>();
    actions_["FastForward"] =                       make_shared<FastForward>();
    actions_["Play"] =                              make_shared<Play>();
    actions_["Stop"] =                              make_shared<Stop>();
    actions_["Record"] =                            make_shared<Record>();
    actions_["CycleTimeline"] =                     make_shared<CycleTimeline>();
    actions_["ToggleSynchPageBanking"] =            make_shared<ToggleSynchPageBanking>();
    actions_["ToggleScrollLink"] =                  make_shared<ToggleScrollLink>();
    actions_["ToggleRestrictTextLength"] =          make_shared<ToggleRestrictTextLength>();
    actions_["GlobalModeDisplay"] =                 make_shared<GlobalModeDisplay>();
    actions_["CycleTimeDisplayModes"] =             make_shared<CycleTimeDisplayModes>();
    actions_["NextPage"] =                          make_shared<GoNextPage>();
    actions_["GoPage"] =                            make_shared<GoPage>();
    actions_["PageNameDisplay"] =                   make_shared<PageNameDisplay>();
    actions_["GoHome"] =                            make_shared<GoHome>();
    actions_["AllSurfacesGoHome"] =                 make_shared<AllSurfacesGoHome>();
    actions_["GoSubZone"] =                         make_shared<GoSubZone>();
    actions_["LeaveSubZone"] =                      make_shared<LeaveSubZone>();
    actions_["SetXTouchDisplayColors"] =            make_shared<SetXTouchDisplayColors>();
    actions_["RestoreXTouchDisplayColors"] =        make_shared<RestoreXTouchDisplayColors>();
    actions_["GoFXSlot"] =                          make_shared<GoFXSlot>();
    actions_["ToggleEnableFocusedFXMapping"] =      make_shared<ToggleEnableFocusedFXMapping>();
    actions_["ToggleEnableFocusedFXParamMapping"] = make_shared<ToggleEnableFocusedFXParamMapping>();
    actions_["ToggleAutoFocusedFXMapping"] =        make_shared<ToggleAutoFocusedFXMapping>();
    actions_["ToggleAutoFXMapping"] =               make_shared<ToggleAutoFXMapping>();
    actions_["RemapAutoZone"] =                     make_shared<RemapAutoZone>();
    actions_["GoSelectedTrackFX"] =                 make_shared<GoSelectedTrackFX>();
    actions_["GoAssociatedZone"] =                  make_shared<GoAssociatedZone>();
    actions_["ClearFocusedFXParam"] =               make_shared<ClearFocusedFXParam>();
    actions_["ClearFocusedFX"] =                    make_shared<ClearFocusedFX>();
    actions_["ClearSelectedTrackFX"] =              make_shared<ClearSelectedTrackFX>();
    actions_["ClearFXSlot"] =                       make_shared<ClearFXSlot>();
    actions_["Bank"] =                              make_shared<Bank>();
    actions_["Shift"] =                             make_shared<SetShift>();
    actions_["Option"] =                            make_shared<SetOption>();
    actions_["Control"] =                           make_shared<SetControl>();
    actions_["Alt"] =                               make_shared<SetAlt>();
    actions_["Flip"] =                              make_shared<SetFlip>();
    actions_["Global"] =                            make_shared<SetGlobal>();
    actions_["Marker"] =                            make_shared<SetMarker>();
    actions_["Nudge"] =                             make_shared<SetNudge>();
    actions_["Zoom"] =                              make_shared<SetZoom>();
    actions_["Scrub"] =                             make_shared<SetScrub>();
    actions_["ClearModifiers"] =                    make_shared<ClearModifiers>();
    actions_["ToggleChannel"] =                     make_shared<SetToggleChannel>();
    actions_["CycleTrackAutoMode"] =                make_shared<CycleTrackAutoMode>();
    actions_["TrackVolume"] =                       make_shared<TrackVolume>();
    actions_["SoftTakeover7BitTrackVolume"] =       make_shared<SoftTakeover7BitTrackVolume>();
    actions_["SoftTakeover14BitTrackVolume"] =      make_shared<SoftTakeover14BitTrackVolume>();
    actions_["TrackVolumeDB"] =                     make_shared<TrackVolumeDB>();
    actions_["TrackToggleVCASpill"] =               make_shared<TrackToggleVCASpill>();
    actions_["TrackVCALeaderDisplay"] =             make_shared<TrackVCALeaderDisplay>();
    actions_["TrackToggleFolderSpill"] =            make_shared<TrackToggleFolderSpill>();
    actions_["TrackFolderParentDisplay"] =          make_shared<TrackFolderParentDisplay>();
    actions_["TrackSelect"] =                       make_shared<TrackSelect>();
    actions_["TrackUniqueSelect"] =                 make_shared<TrackUniqueSelect>();
    actions_["TrackRangeSelect"] =                  make_shared<TrackRangeSelect>();
    actions_["TrackRecordArm"] =                    make_shared<TrackRecordArm>();
    actions_["TrackMute"] =                         make_shared<TrackMute>();
    actions_["TrackSolo"] =                         make_shared<TrackSolo>();
    actions_["ClearAllSolo"] =                      make_shared<ClearAllSolo>();
    actions_["TrackInvertPolarity"] =               make_shared<TrackInvertPolarity>();
    actions_["TrackPan"] =                          make_shared<TrackPan>();
    actions_["TrackPanPercent"] =                   make_shared<TrackPanPercent>();
    actions_["TrackPanWidth"] =                     make_shared<TrackPanWidth>();
    actions_["TrackPanWidthPercent"] =              make_shared<TrackPanWidthPercent>();
    actions_["TrackPanL"] =                         make_shared<TrackPanL>();
    actions_["TrackPanLPercent"] =                  make_shared<TrackPanLPercent>();
    actions_["TrackPanR"] =                         make_shared<TrackPanR>();
    actions_["TrackPanRPercent"] =                  make_shared<TrackPanRPercent>();
    actions_["TrackPanAutoLeft"] =                  make_shared<TrackPanAutoLeft>();
    actions_["TrackPanAutoRight"] =                 make_shared<TrackPanAutoRight>();
    actions_["TrackNameDisplay"] =                  make_shared<TrackNameDisplay>();
    actions_["TrackNumberDisplay"] =                make_shared<TrackNumberDisplay>();
    actions_["TrackRecordInputDisplay"] =           make_shared<TrackRecordInputDisplay>();
    actions_["TrackVolumeDisplay"] =                make_shared<TrackVolumeDisplay>();
    actions_["TrackPanDisplay"] =                   make_shared<TrackPanDisplay>();
    actions_["TrackPanWidthDisplay"] =              make_shared<TrackPanWidthDisplay>();
    actions_["TrackPanLeftDisplay"] =               make_shared<TrackPanLeftDisplay>();
    actions_["TrackPanRightDisplay"] =              make_shared<TrackPanRightDisplay>();
    actions_["TrackPanAutoLeftDisplay"] =           make_shared<TrackPanAutoLeftDisplay>();
    actions_["TrackPanAutoRightDisplay"] =          make_shared<TrackPanAutoRightDisplay>();
    actions_["TrackOutputMeter"] =                  make_shared<TrackOutputMeter>();
    actions_["TrackOutputMeterAverageLR"] =         make_shared<TrackOutputMeterAverageLR>();
    actions_["TrackOutputMeterMaxPeakLR"] =         make_shared<TrackOutputMeterMaxPeakLR>();
    actions_["FocusedFXParam"] =                    make_shared<FocusedFXParam>();
    actions_["FXParam"] =                           make_shared<FXParam>();
    actions_["JSFXParam"] =                         make_shared<JSFXParam>();
    actions_["TCPFXParam"] =                        make_shared<TCPFXParam>();
    actions_["ToggleFXBypass"] =                    make_shared<ToggleFXBypass>();
    actions_["FXBypassDisplay"] =                   make_shared<FXBypassDisplay>();
    actions_["ToggleFXOffline"] =                   make_shared<ToggleFXOffline>();
    actions_["FXOfflineDisplay"] =                  make_shared<FXOfflineDisplay>();
    actions_["FXNameDisplay"] =                     make_shared<FXNameDisplay>();
    actions_["FXMenuNameDisplay"] =                 make_shared<FXMenuNameDisplay>();
    actions_["SpeakFXMenuName"] =                   make_shared<SpeakFXMenuName>();
    actions_["FXParamNameDisplay"] =                make_shared<FXParamNameDisplay>();
    actions_["TCPFXParamNameDisplay"] =             make_shared<TCPFXParamNameDisplay>();
    actions_["FXParamValueDisplay"] =               make_shared<FXParamValueDisplay>();
    actions_["TCPFXParamValueDisplay"] =            make_shared<TCPFXParamValueDisplay>();
    actions_["FocusedFXParamNameDisplay"] =         make_shared<FocusedFXParamNameDisplay>();
    actions_["FocusedFXParamValueDisplay"] =        make_shared<FocusedFXParamValueDisplay>();
    actions_["FXGainReductionMeter"] =              make_shared<FXGainReductionMeter>();
    actions_["TrackSendVolume"] =                   make_shared<TrackSendVolume>();
    actions_["TrackSendVolumeDB"] =                 make_shared<TrackSendVolumeDB>();
    actions_["TrackSendPan"] =                      make_shared<TrackSendPan>();
    actions_["TrackSendPanPercent"] =               make_shared<TrackSendPanPercent>();
    actions_["TrackSendMute"] =                     make_shared<TrackSendMute>();
    actions_["TrackSendInvertPolarity"] =           make_shared<TrackSendInvertPolarity>();
    actions_["TrackSendStereoMonoToggle"] =         make_shared<TrackSendStereoMonoToggle>();
    actions_["TrackSendPrePost"] =                  make_shared<TrackSendPrePost>();
    actions_["TrackSendNameDisplay"] =              make_shared<TrackSendNameDisplay>();
    actions_["SpeakTrackSendDestination"] =         make_shared<SpeakTrackSendDestination>();
    actions_["TrackSendVolumeDisplay"] =            make_shared<TrackSendVolumeDisplay>();
    actions_["TrackSendPanDisplay"] =               make_shared<TrackSendPanDisplay>();
    actions_["TrackSendPrePostDisplay"] =           make_shared<TrackSendPrePostDisplay>();
    actions_["TrackReceiveVolume"] =                make_shared<TrackReceiveVolume>();
    actions_["TrackReceiveVolumeDB"] =              make_shared<TrackReceiveVolumeDB>();
    actions_["TrackReceivePan"] =                   make_shared<TrackReceivePan>();
    actions_["TrackReceivePanPercent"] =            make_shared<TrackReceivePanPercent>();
    actions_["TrackReceiveMute"] =                  make_shared<TrackReceiveMute>();
    actions_["TrackReceiveInvertPolarity"] =        make_shared<TrackReceiveInvertPolarity>();
    actions_["TrackReceiveStereoMonoToggle"] =      make_shared<TrackReceiveStereoMonoToggle>();
    actions_["TrackReceivePrePost"] =               make_shared<TrackReceivePrePost>();
    actions_["TrackReceiveNameDisplay"] =           make_shared<TrackReceiveNameDisplay>();
    actions_["SpeakTrackReceiveSource"] =           make_shared<SpeakTrackReceiveSource>();
    actions_["TrackReceiveVolumeDisplay"] =         make_shared<TrackReceiveVolumeDisplay>();
    actions_["TrackReceivePanDisplay"] =            make_shared<TrackReceivePanDisplay>();
    actions_["TrackReceivePrePostDisplay"] =        make_shared<TrackReceivePrePostDisplay>();
}

void Manager::InitFXParamAliases()
{
    string fxParamAliasesFilePath = string(DAW::GetResourcePath()) + "/CSI/FXParamAliasesCache.txt";
    
    filesystem::path fxParamAliasesFile { fxParamAliasesFilePath };

    if (filesystem::exists(fxParamAliasesFile))
    {
        int lineNumber = 0;

        try
        {
            ifstream fxParamAliases(fxParamAliasesFile);
                   
            for (string line; getline(fxParamAliases, line) ; )
            {
                line = regex_replace(line, regex(TabChars), " ");
                line = regex_replace(line, regex(CRLFChars), "");
                
                
                if(line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
                    continue;
                
                vector<string> tokens(GetTokens(line));
                
                if(tokens.size() != 3)
                    continue;
                
                fxParamAliases_[tokens[0]][atoi(tokens[1].c_str())] = tokens[2];
                
                lineNumber++;
            }
        }
        catch (exception &e)
        {
            char buffer[250];
            snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", fxParamAliasesFilePath.c_str(), lineNumber);
            DAW::ShowConsoleMsg(buffer);
        }
    }
}

void Manager::WriteFXParamAliases()
{
    string fxParamAliasesFilePath = string(DAW::GetResourcePath()) + "/CSI/FXParamAliasesCache.txt";
    
    filesystem::path fxParamAliasesFile { fxParamAliasesFilePath };

    int lineNumber = 0;

    try
    {
        ofstream fxParamAliases(fxParamAliasesFile);
               
        for (auto [fxName, paramAlias] : fxParamAliases_)
        {
            for(auto [paramIndex, alias] : paramAlias)
            {
                fxParamAliases << "\"" + fxName + "\" " + to_string(paramIndex) + " \"" + alias + "\"" + GetLineEnding();
                lineNumber++;
            }
        }
        
        fxParamAliases.close();
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", fxParamAliasesFilePath.c_str(), lineNumber);
        DAW::ShowConsoleMsg(buffer);
    }
}

void Manager::Init()
{
    pages_.clear();
    
    map<string, shared_ptr<Midi_ControlSurfaceIO>> midiSurfaces;
    map<string, shared_ptr<OSC_ControlSurfaceIO>> oscSurfaces;

    shared_ptr<Page> currentPage = nullptr;
    
    string CSIFolderPath = string(DAW::GetResourcePath()) + "/CSI";
    
    filesystem::path CSIFolder { CSIFolderPath };
    
    if (! filesystem::exists(CSIFolder) || ! filesystem::is_directory(CSIFolder))
    {       
        MessageBox(g_hwnd, ("Please check your installation, cannot find " + CSIFolderPath).c_str(), "Missing CSI Folder", MB_OK);
        
        return;
    }
    
    string iniFilePath = string(DAW::GetResourcePath()) + "/CSI/CSI.ini";
    
    filesystem::path iniFile { iniFilePath };

    if (! filesystem::exists(iniFile))
    {
        MessageBox(g_hwnd, ("Please check your installation, cannot find " + iniFilePath).c_str(), "Missing CSI.ini", MB_OK);
        
        return;
    }
    
    int lineNumber = 0;
    
    try
    {
        ifstream iniFile(iniFilePath);
               
        for (string line; getline(iniFile, line) ; )
        {
            line = regex_replace(line, regex(TabChars), " ");
            line = regex_replace(line, regex(CRLFChars), "");
            
            if(lineNumber == 0)
            {
                if(line != VersionToken)
                {
                    MessageBox(g_hwnd, ("Version mismatch -- Your CSI.ini file is not " + VersionToken).c_str(), ("This is CSI " + VersionToken).c_str(), MB_OK);
                    iniFile.close();
                    return;
                }
                else
                {
                    lineNumber++;
                    continue;
                }
            }
            
            if(line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
                continue;
            
            vector<string> tokens(GetTokens(line));
                       
            if(tokens.size() > 1) // ignore comment lines and blank lines
            {
                if(tokens[0] == MidiSurfaceToken && tokens.size() == 4)
                    midiSurfaces[tokens[1]] = make_shared<Midi_ControlSurfaceIO>(tokens[1], GetMidiInputForPort(atoi(tokens[2].c_str())), GetMidiOutputForPort(atoi(tokens[3].c_str())));
                else if(tokens[0] == OSCSurfaceToken && tokens.size() == 5)
                    oscSurfaces[tokens[1]] = make_shared<OSC_ControlSurfaceIO>(tokens[1], tokens[2], tokens[3], tokens[4]);
                else if(tokens[0] == PageToken)
                {
                    bool followMCP = true;
                    bool synchPages = true;
                    bool isScrollLinkEnabled = false;
                    bool isScrollSynchEnabled = false;

                    currentPage = nullptr;
                    
                    if(tokens.size() > 1)
                    {
                        if(tokens.size() > 2)
                        {
                            for(int i = 2; i < tokens.size(); i++)
                            {
                                if(tokens[i] == "FollowTCP")
                                    followMCP = false;
                                else if(tokens[i] == "NoSynchPages")
                                    synchPages = false;
                                else if(tokens[i] == "UseScrollLink")
                                    isScrollLinkEnabled = true;
                                else if(tokens[i] == "UseScrollSynch")
                                    isScrollSynchEnabled = true;
                            }
                        }
                            
                        currentPage = Page::GetInstance(tokens[1], followMCP, synchPages, isScrollLinkEnabled, isScrollSynchEnabled);
                        pages_.push_back(currentPage);
                    }
                }
                else
                {
                    if(currentPage && (tokens.size() == 6 || tokens.size() == 7))
                    {
                        bool useLocalModifiers = false;
                        
                        if(tokens[0] == "LocalModifiers")
                        {
                            useLocalModifiers = true;
                            tokens.erase(tokens.begin()); // pop front
                        }
                        
                        string zoneFolder = tokens[4];
                        string fxZoneFolder = tokens[5];
                        
                        if(midiSurfaces.count(tokens[0]) > 0)
                            currentPage->AddSurface(Midi_ControlSurface::GetInstance(useLocalModifiers, currentPage, tokens[0], atoi(tokens[1].c_str()), atoi(tokens[2].c_str()), tokens[3], zoneFolder, fxZoneFolder, midiSurfaces[tokens[0]]));
                        else if(oscSurfaces.count(tokens[0]) > 0)
                            currentPage->AddSurface(OSC_ControlSurface::GetInstance(useLocalModifiers, currentPage, tokens[0], atoi(tokens[1].c_str()), atoi(tokens[2].c_str()), tokens[3], zoneFolder, fxZoneFolder, oscSurfaces[tokens[0]]));
                    }
                }
            }
            
            lineNumber++;
        }
        
        // Restore the PageIndex
        currentPageIndex_ = 0;
        
        char buf[512];
        
        int result = DAW::GetProjExtState(0, "CSI", "PageIndex", buf, sizeof(buf));
        
        if(result > 0)
        {
            currentPageIndex_ = atoi(buf);
 
            if(currentPageIndex_ > pages_.size() - 1)
                currentPageIndex_ = 0;
        }
        
        if(pages_.size() > 0)
            pages_[currentPageIndex_]->ForceClear();               
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", iniFilePath.c_str(), lineNumber);
        DAW::ShowConsoleMsg(buffer);
    }
      
    InitFXParamAliases();
    
    for(auto page : pages_)
        page->OnInitialization();
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
MediaTrack* TrackNavigator::GetTrack()
{
    return trackNavigationManager_->GetTrackFromChannel(channelNum_);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// MasterTrackNavigator
////////////////////////////////////////////////////////////////////////////////////////////////////////
MediaTrack* MasterTrackNavigator::GetTrack()
{
    return DAW::GetMasterTrack();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// SelectedTrackNavigator
////////////////////////////////////////////////////////////////////////////////////////////////////////
MediaTrack* SelectedTrackNavigator::GetTrack()
{
    return page_->GetSelectedTrack();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// FocusedFXNavigator
////////////////////////////////////////////////////////////////////////////////////////////////////////
MediaTrack* FocusedFXNavigator::GetTrack()
{
    int trackNumber = 0;
    int itemNumber = 0;
    int fxIndex = 0;
    
    if(DAW::GetFocusedFX2(&trackNumber, &itemNumber, &fxIndex) == 1) // Track FX
        return DAW::GetTrack(trackNumber);
    else
        return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ActionContext
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ActionContext::ActionContext(shared_ptr<Action> action, shared_ptr<Widget> widget, shared_ptr<Zone> zone, vector<string> params): action_(action), widget_(widget), zone_(zone)
{
    // GAW -- Hack to get rid of the widgetProperties, so we don't break existing logic
    vector<string> nonWidgetPropertyParams;
    
    for(auto param : params)
    {
        if(param.find("=") != string::npos)
        {
            istringstream widgetProperty(param);
            vector<string> kvp;
            string token;
            
            while(getline(widgetProperty, token, '='))
                kvp.push_back(token);

            if(kvp.size() == 2)
                widgetProperties_[kvp[0]] = kvp[1];
        }
        else
            nonWidgetPropertyParams.push_back(param);
    }
    
    widget_->SetColorValue(widgetProperties_);
    
    params = nonWidgetPropertyParams;
    
    string actionName = "";
    
    if(params.size() > 0)
        actionName = params[0];
    
    // Action with int param, could include leading minus sign
    if(params.size() > 1 && (isdigit(params[1][0]) ||  params[1][0] == '-'))  // C++ 11 says empty strings can be queried without catastrophe :)
    {
        intParam_= atol(params[1].c_str());
    }
    
    if(actionName == "Bank" && (params.size() > 2 && (isdigit(params[2][0]) ||  params[2][0] == '-')))  // C++ 11 says empty strings can be queried without catastrophe :)
    {
        stringParam_ = params[1];
        intParam_= atol(params[2].c_str());
    }
    
    // Action with param index, must be positive
    if(params.size() > 1 && isdigit(params[1][0]))  // C++ 11 says empty strings can be queried without catastrophe :)
    {
        paramIndex_ = atol(params[1].c_str());
    }
    
    // Action with string param
    if(params.size() > 1)
        stringParam_ = params[1];
    
    if(actionName == "TrackVolumeDB" || actionName == "TrackSendVolumeDB")
    {
        rangeMinimum_ = -144.0;
        rangeMaximum_ = 24.0;
    }
    
    if(actionName == "TrackPanPercent" || actionName == "TrackPanWidthPercent" || actionName == "TrackPanLPercent" || actionName == "TrackPanRPercent")
    {
        rangeMinimum_ = -100.0;
        rangeMaximum_ = 100.0;
    }
   
    if((actionName == "Reaper" || actionName == "ReaperDec" || actionName == "ReaperInc") && params.size() > 1)
    {
        if (isdigit(params[1][0]))
        {
            commandId_ =  atol(params[1].c_str());
        }
        else // look up by string
        {
            commandId_ = DAW::NamedCommandLookup(params[1].c_str());
            
            if(commandId_ == 0) // can't find it
                commandId_ = 65535; // no-op
        }
    }
        
    if((actionName == "FXParam" || actionName == "JSFXParam") && params.size() > 1 && isdigit(params[1][0])) // C++ 11 says empty strings can be queried without catastrophe :)
    {
        paramIndex_ = atol(params[1].c_str());
    }
    
    if(actionName == "FXParamValueDisplay" && params.size() > 1 && isdigit(params[1][0]))
    {
        paramIndex_ = atol(params[1].c_str());
    }
    
    if(actionName == "FXParamNameDisplay" && params.size() > 1 && isdigit(params[1][0]))
    {
        paramIndex_ = atol(params[1].c_str());
        
        if(params.size() > 2 && params[2] != "{" && params[2] != "[")
               fxParamDisplayName_ = params[2];
    }
    
    if(params.size() > 1 && (actionName == "Activate" || actionName == "Deactivate" || actionName == "ToggleActivation"))
    {
        for(int i = 1; i < params.size(); i++)
            zoneNames_.push_back(params[i]);
    }

    if(params.size() > 0)
        SetColor(params, supportsColor_, supportsTrackColor_, colorValues_);
    
    GetSteppedValues(widget, action_, zone_, paramIndex_, params, widgetProperties_, deltaValue_, acceleratedDeltaValues_, rangeMinimum_, rangeMaximum_, steppedValues_, acceleratedTickValues_);

    if(acceleratedTickValues_.size() < 1)
        acceleratedTickValues_.push_back(10);
}

shared_ptr<Page> ActionContext::GetPage()
{
    return widget_->GetSurface()->GetPage();
}

shared_ptr<ControlSurface> ActionContext::GetSurface()
{
    return widget_->GetSurface();
}

MediaTrack* ActionContext::GetTrack()
{
    return zone_->GetNavigator()->GetTrack();
}

int ActionContext::GetSlotIndex()
{
    return zone_->GetSlotIndex();
}

string ActionContext::GetName()
{
    return zone_->GetNameOrAlias();
}

void ActionContext::RunDeferredActions()
{
    if(holdDelayAmount_ != 0.0 && delayStartTime_ != 0.0 && DAW::GetCurrentNumberOfMilliseconds() > (delayStartTime_ + holdDelayAmount_))
    {
        if(steppedValues_.size() > 0)
        {
            if(deferredValue_ != 0.0) // ignore release messages
            {
                if(steppedValuesIndex_ == steppedValues_.size() - 1)
                {
                    if(steppedValues_[0] < steppedValues_[steppedValuesIndex_]) // GAW -- only wrap if 1st value is lower
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
    if(provideFeedback_)
        action_->RequestUpdate(this);
}

void ActionContext::ClearWidget()
{
    widget_->Clear();
}

void ActionContext::UpdateColorValue(double value)
{
    if(supportsColor_)
    {
        currentColorIndex_ = value == 0 ? 0 : 1;
        if(colorValues_.size() > currentColorIndex_)
            widget_->UpdateColorValue(colorValues_[currentColorIndex_]);
    }
}

void ActionContext::UpdateWidgetValue(double value)
{
    if(steppedValues_.size() > 0)
        SetSteppedValueIndex(value);

    value = isFeedbackInverted_ == false ? value : 1.0 - value;
   
    widget_->UpdateValue(widgetProperties_, value);

    UpdateColorValue(value);
    
    if(supportsTrackColor_)
        UpdateTrackColor();
}

void ActionContext::UpdateJSFXWidgetSteppedValue(double value)
{
    if(steppedValues_.size() > 0)
        SetSteppedValueIndex(value);
}

void ActionContext::UpdateTrackColor()
{
    if(MediaTrack* track = zone_->GetNavigator()->GetTrack())
    {
        rgba_color color = DAW::GetTrackColor(track);
        widget_->UpdateColorValue(color);
    }
}

void ActionContext::UpdateWidgetValue(string value)
{
    widget_->UpdateValue(widgetProperties_, value);
}

void ActionContext::DoAction(double value)
{
    if(holdDelayAmount_ != 0.0)
    {
        if(value == 0.0)
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
        if(steppedValues_.size() > 0)
        {
            if(value != 0.0) // ignore release messages
            {
                if(steppedValuesIndex_ == steppedValues_.size() - 1)
                {
                    if(steppedValues_[0] < steppedValues_[steppedValuesIndex_]) // GAW -- only wrap if 1st value is lower
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
    if(steppedValues_.size() > 0)
        DoSteppedValueAction(delta);
    else
        DoRangeBoundAction(action_->GetCurrentNormalizedValue(this) + (deltaValue_ != 0.0 ? (delta > 0 ? deltaValue_ : -deltaValue_) : delta));
}

void ActionContext::DoRelativeAction(int accelerationIndex, double delta)
{
    if(steppedValues_.size() > 0)
        DoAcceleratedSteppedValueAction(accelerationIndex, delta);
    else if(acceleratedDeltaValues_.size() > 0)
        DoAcceleratedDeltaValueAction(accelerationIndex, delta);
    else
        DoRangeBoundAction(action_->GetCurrentNormalizedValue(this) +  (deltaValue_ != 0.0 ? (delta > 0 ? deltaValue_ : -deltaValue_) : delta));
}

void ActionContext::DoRangeBoundAction(double value)
{
    if(value > rangeMaximum_)
        value = rangeMaximum_;
    
    if(value < rangeMinimum_)
        value = rangeMinimum_;
    
    if(isValueInverted_)
        value = 1.0 - value;
    
    action_->Do(this, value);
}

void ActionContext::DoSteppedValueAction(double delta)
{
    if(delta > 0)
    {
        steppedValuesIndex_++;
        
        if(steppedValuesIndex_ > steppedValues_.size() - 1)
            steppedValuesIndex_ = steppedValues_.size() - 1;
        
        DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
    }
    else
    {
        steppedValuesIndex_--;
        
        if(steppedValuesIndex_ < 0 )
            steppedValuesIndex_ = 0;
        
        DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
    }
}

void ActionContext::DoAcceleratedSteppedValueAction(int accelerationIndex, double delta)
{
    if(delta > 0)
    {
        accumulatedIncTicks_++;
        accumulatedDecTicks_ = accumulatedDecTicks_ - 1 < 0 ? 0 : accumulatedDecTicks_ - 1;
    }
    else if(delta < 0)
    {
        accumulatedDecTicks_++;
        accumulatedIncTicks_ = accumulatedIncTicks_ - 1 < 0 ? 0 : accumulatedIncTicks_ - 1;
    }
    
    accelerationIndex = accelerationIndex > acceleratedTickValues_.size() - 1 ? acceleratedTickValues_.size() - 1 : accelerationIndex;
    accelerationIndex = accelerationIndex < 0 ? 0 : accelerationIndex;
    
    if(delta > 0 && accumulatedIncTicks_ >= acceleratedTickValues_[accelerationIndex])
    {
        accumulatedIncTicks_ = 0;
        accumulatedDecTicks_ = 0;
        
        steppedValuesIndex_++;
        
        if(steppedValuesIndex_ > steppedValues_.size() - 1)
            steppedValuesIndex_ = steppedValues_.size() - 1;
        
        DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
    }
    else if(delta < 0 && accumulatedDecTicks_ >= acceleratedTickValues_[accelerationIndex])
    {
        accumulatedIncTicks_ = 0;
        accumulatedDecTicks_ = 0;
        
        steppedValuesIndex_--;
        
        if(steppedValuesIndex_ < 0 )
            steppedValuesIndex_ = 0;
        
        DoRangeBoundAction(steppedValues_[steppedValuesIndex_]);
    }
}

void ActionContext::DoAcceleratedDeltaValueAction(int accelerationIndex, double delta)
{
    accelerationIndex = accelerationIndex > acceleratedDeltaValues_.size() - 1 ? acceleratedDeltaValues_.size() - 1 : accelerationIndex;
    accelerationIndex = accelerationIndex < 0 ? 0 : accelerationIndex;
    
    if(delta > 0.0)
        DoRangeBoundAction(action_->GetCurrentNormalizedValue(this) + acceleratedDeltaValues_[accelerationIndex]);
    else
        DoRangeBoundAction(action_->GetCurrentNormalizedValue(this) - acceleratedDeltaValues_[accelerationIndex]);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Zone
////////////////////////////////////////////////////////////////////////////////////////////////////////
Zone::Zone(shared_ptr<ZoneManager> const zoneManager, shared_ptr<Navigator> navigator, int slotIndex, string name, string alias, string sourceFilePath, vector<string> includedZones, vector<string> associatedZones): zoneManager_(zoneManager), navigator_(navigator), slotIndex_(slotIndex), name_(name), alias_(alias), sourceFilePath_(sourceFilePath)
{
    if(name == "Home")
    {
        for(auto zoneName : associatedZones)
        {
            if(zoneManager_->GetZoneFilePaths().count(zoneName) > 0)
            {
                vector<shared_ptr<Navigator>> navigators;
                AddNavigatorsForZone(zoneName, navigators);

                associatedZones_[zoneName] = vector<shared_ptr<Zone>>();
                
                ProcessZoneFile(zoneManager_->GetZoneFilePaths()[zoneName].filePath, zoneManager_, navigators, associatedZones_[zoneName], nullptr);
            }
        }
    }
    
    for(auto zoneName : includedZones)
    {
        if(zoneManager_->GetZoneFilePaths().count(zoneName) > 0)
        {
            vector<shared_ptr<Navigator>> navigators;
            AddNavigatorsForZone(zoneName, navigators);
            
            ProcessZoneFile(zoneManager_->GetZoneFilePaths()[zoneName].filePath, zoneManager_, navigators, includedZones_, nullptr);
        }
    }
}

void Zone::InitSubZones(vector<string> subZones, shared_ptr<Zone> enclosingZone)
{
    for(auto zoneName : subZones)
    {
        if(zoneManager_->GetZoneFilePaths().count(zoneName) > 0)
        {
            vector<shared_ptr<Navigator>> navigators;
            navigators.push_back(GetNavigator());

            subZones_[zoneName] = vector<shared_ptr<Zone>>();
        
            ProcessZoneFile(zoneManager_->GetZoneFilePaths()[zoneName].filePath, zoneManager_, navigators, subZones_[zoneName], enclosingZone);
        }
    }
}

int Zone::GetSlotIndex()
{
    if(name_ == "TrackSend")
        return zoneManager_->GetTrackSendOffset();
    if(name_ == "TrackReceive")
        return zoneManager_->GetTrackReceiveOffset();
    if(name_ == "TrackFXMenu")
        return zoneManager_->GetTrackFXMenuOffset();
    if(name_ == "SelectedTrack")
        return slotIndex_;
    if(name_ == "SelectedTrackSend")
        return slotIndex_ + zoneManager_->GetSelectedTrackSendOffset() + zoneManager_->GetSurface()->GetChannelOffset();
    if(name_ == "SelectedTrackReceive")
        return slotIndex_ + zoneManager_->GetSelectedTrackReceiveOffset() + zoneManager_->GetSurface()->GetChannelOffset();
    if(name_ == "SelectedTrackFXMenu")
        return slotIndex_ + zoneManager_->GetSelectedTrackFXMenuOffset() + zoneManager_->GetSurface()->GetChannelOffset();
    if(name_ == "MasterTrackFXMenu")
        return slotIndex_ + zoneManager_->GetMasterTrackFXMenuOffset();
    else return slotIndex_;
}

int Zone::GetChannelNumber()
{
    int channelNumber = 0;
    
    for(auto [widget, isUsed] : widgets_)
        if(channelNumber < widget->GetChannelNumber())
            channelNumber = widget->GetChannelNumber();
    
    return channelNumber;
}

void Zone::GoAssociatedZone(string zoneName)
{
    if(zoneName == "Track")
    {
        for(auto [key, zones] : associatedZones_)
            for(auto zone : zones)
                zone->Deactivate();
        
        return;
    }
    
    if(associatedZones_.count(zoneName) > 0 && associatedZones_[zoneName].size() > 0 && associatedZones_[zoneName][0]->GetIsActive())
    {
        for(auto zone : associatedZones_[zoneName])
            zone->Deactivate();
        
        zoneManager_->GoHome();
        
        return;
    }
    
    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            zone->Deactivate();
        
    if(associatedZones_.count(zoneName) > 0)
        for(auto zone : associatedZones_[zoneName])
            zone->Activate();
}

void Zone::ReactivateFXMenuZone()
{
    if(associatedZones_.count("TrackFXMenu") > 0 && associatedZones_["TrackFXMenu"][0]->GetIsActive())
        for(auto zone : associatedZones_["TrackFXMenu"])
            zone->Activate();
    else if(associatedZones_.count("SelectedTrackFXMenu") > 0 && associatedZones_["SelectedTrackFXMenu"][0]->GetIsActive())
        for(auto zone : associatedZones_["SelectedTrackFXMenu"])
            zone->Activate();
}

void Zone::Activate()
{
    UpdateCurrentActionContextModifiers();
    
    for(auto [widget, isUsed] : widgets_)
        if(widget->GetName() == "OnZoneActivation")
            for(auto context : GetActionContexts(widget))
                context->DoAction(1.0);

    isActive_ = true;
    
    if(GetName() == "VCA")
        zoneManager_->GetSurface()->GetPage()->VCAModeActivated();
    else if(GetName() == "Folder")
        zoneManager_->GetSurface()->GetPage()->FolderModeActivated();
    else if(GetName() == "SelectedTracks")
        zoneManager_->GetSurface()->GetPage()->SelectedTracksModeActivated();

    zoneManager_->GetSurface()->SendOSCMessage(GetName());
       
    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            zone->Deactivate();
    
    for(auto [key, zones] : subZones_)
        for(auto zone : zones)
            zone->Deactivate();
    
    for(auto zone : includedZones_)
        zone->Activate();
}

void Zone::Deactivate()
{
    for(auto [widget, isUsed] : widgets_)
        if(widget->GetName() == "OnZoneDeactivation")
            for(auto context : GetActionContexts(widget))
                context->DoAction(1.0);

    isActive_ = false;
    
    if(GetName() == "VCA")
        zoneManager_->GetSurface()->GetPage()->VCAModeDeactivated();
    else if(GetName() == "Folder")
        zoneManager_->GetSurface()->GetPage()->FolderModeDeactivated();
    else if(GetName() == "SelectedTracks")
        zoneManager_->GetSurface()->GetPage()->SelectedTracksModeDeactivated();
    
    for(auto zone : includedZones_)
        zone->Deactivate();

    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            zone->Deactivate();

    for(auto [key, zones] : subZones_)
        for(auto zone : zones)
            zone->Deactivate();
}

void Zone::ClearWidgets()
{
    for(auto [widget, isUsed] : widgets_)
        widget->Clear();
}

void Zone::AddNavigatorsForZone(string zoneName, vector<shared_ptr<Navigator>> &navigators)
{
    if(zoneName == "MasterTrack")
        navigators.push_back(zoneManager_->GetMasterTrackNavigator());
    else if(zoneName == "Track" || zoneName == "VCA" || zoneName == "Folder" || zoneName == "SelectedTracks" || zoneName == "TrackSend" || zoneName == "TrackReceive" || zoneName == "TrackFXMenu" )
    {
        for(int i = 0; i < zoneManager_->GetNumChannels(); i++)
        {
            shared_ptr<Navigator> channelNavigator = zoneManager_->GetSurface()->GetPage()->GetNavigatorForChannel(i + zoneManager_->GetSurface()->GetChannelOffset());
            if(channelNavigator)
                navigators.push_back(channelNavigator);
        }
    }
    else if(zoneName == "SelectedTrack" || zoneName == "SelectedTrackSend" || zoneName == "SelectedTrackReceive" || zoneName == "SelectedTrackFXMenu")
        for(int i = 0; i < zoneManager_->GetNumChannels(); i++)
            navigators.push_back(zoneManager_->GetSelectedTrackNavigator());
    else if(zoneName == "MasterTrackFXMenu")
        for(int i = 0; i < zoneManager_->GetNumChannels(); i++)
            navigators.push_back(zoneManager_->GetMasterTrackNavigator());
    else
        navigators.push_back(zoneManager_->GetSelectedTrackNavigator());
}

void Zone::SetXTouchDisplayColors(string color)
{
    for(auto [widget, isUsed] : widgets_)
        widget->SetXTouchDisplayColors(name_, color);
}

void Zone::RestoreXTouchDisplayColors()
{
    for(auto [widget, isUsed] : widgets_)
        widget->RestoreXTouchDisplayColors();
}

void Zone::DoAction(shared_ptr<Widget> widget, bool &isUsed, double value)
{
    if(! isActive_ || isUsed)
        return;
    
    for(auto [key, zones] : subZones_)
        for(auto zone : zones)
            zone->DoAction(widget, isUsed, value);

    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            zone->DoAction(widget, isUsed, value);
    
    if(isUsed)
        return;

    if(widgets_.count(widget) > 0)
    {
        if(TheManager->GetSurfaceInDisplay())
        {
            char buffer[250];
            snprintf(buffer, sizeof(buffer), "Zone -- %s\n", sourceFilePath_.c_str());
            DAW::ShowConsoleMsg(buffer);
        }

        isUsed = true;
        
        for(auto context : GetActionContexts(widget))
            context->DoAction(value);
    }
    else
    {
        for(auto zone : includedZones_)
            zone->DoAction(widget, isUsed, value);
    }
}

void Zone::UpdateCurrentActionContextModifiers()
{
    for(auto [widget, isUsed] : widgets_)
        UpdateCurrentActionContextModifier(widget);
    
    for(auto zone : includedZones_)
        zone->UpdateCurrentActionContextModifiers();

    for(auto [key, zones] : subZones_)
        for(auto zone : zones)
            zone->UpdateCurrentActionContextModifiers();
    
    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            zone->UpdateCurrentActionContextModifiers();
}

void Zone::UpdateCurrentActionContextModifier(shared_ptr<Widget> widget)
{
    for(auto modifier : widget->GetSurface()->GetModifiers())
    {
        if(actionContextDictionary_[widget].count(modifier) > 0)
        {
            currentActionContextModifiers_[widget] = modifier;
            break;
        }
    }
}

vector<shared_ptr<ActionContext>> &Zone::GetActionContexts(shared_ptr<Widget> widget)
{
    if(currentActionContextModifiers_.count(widget) == 0)
        UpdateCurrentActionContextModifier(widget);
    
    bool isTouched = false;
    bool isToggled = false;
    
    if(widget->GetSurface()->GetIsChannelTouched(widget->GetChannelNumber()))
        isTouched = true;

    if(widget->GetSurface()->GetIsChannelToggled(widget->GetChannelNumber()))
        isToggled = true;
    
    if(currentActionContextModifiers_.count(widget) > 0 && actionContextDictionary_.count(widget) > 0)
    {
        int modifer = currentActionContextModifiers_[widget];
        
        if(isTouched && isToggled && actionContextDictionary_[widget].count(modifer + 3) > 0)
            return actionContextDictionary_[widget][modifer + 3];
        else if(isTouched && actionContextDictionary_[widget].count(modifer + 1) > 0)
            return actionContextDictionary_[widget][modifer + 1];
        else if(isToggled && actionContextDictionary_[widget].count(modifer + 2) > 0)
            return actionContextDictionary_[widget][modifer + 2];
        else if(actionContextDictionary_[widget].count(modifer) > 0)
            return actionContextDictionary_[widget][modifer];
    }

    return defaultContexts_;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Widget
////////////////////////////////////////////////////////////////////////////////////////////////////////
shared_ptr<ZoneManager> Widget::GetZoneManager()
{
    return surface_->GetZoneManager();
}

void  Widget::UpdateValue(map<string, string> &properties, double value)
{
    for(auto processor : feedbackProcessors_)
        processor->SetValue(properties, value);
}

void  Widget::UpdateValue(map<string, string> &properties,string value)
{
    for(auto processor : feedbackProcessors_)
        processor->SetValue(properties, value);
}

void  Widget::UpdateColorValue(rgba_color color)
{
    for(auto processor : feedbackProcessors_)
        processor->SetColorValue(color);
}

void  Widget::SetColorValue(map<string, string> &properties)
{
    for(auto processor : feedbackProcessors_)
        processor->SetInitialValues(properties);
}

void Widget::SetXTouchDisplayColors(string zoneName, string color)
{
    for(auto processor : feedbackProcessors_)
        processor->SetXTouchDisplayColors(zoneName, color);
}

void Widget::RestoreXTouchDisplayColors()
{
    for(auto processor : feedbackProcessors_)
        processor->RestoreXTouchDisplayColors();
}

void  Widget::Clear()
{
    for(auto processor : feedbackProcessors_)
        processor->Clear();
}

void  Widget::ForceClear()
{
    for(auto processor : feedbackProcessors_)
        processor->ForceClear();
}

void Widget::LogInput(double value)
{
    if(TheManager->GetSurfaceInDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %s %f\n", GetSurface()->GetName().c_str(), GetName().c_str(), value);
        DAW::ShowConsoleMsg(buffer);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi_FeedbackProcessor
////////////////////////////////////////////////////////////////////////////////////////////////////////
void Midi_FeedbackProcessor::SendMidiSysExMessage(MIDI_event_ex_t* midiMessage)
{
    surface_->SendMidiSysExMessage(midiMessage);
}

void Midi_FeedbackProcessor::SendMidiMessage(int first, int second, int third)
{
    if(first != lastMessageSent_->midi_message[0] || second != lastMessageSent_->midi_message[1] || third != lastMessageSent_->midi_message[2])
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
void OSC_FeedbackProcessor::SetColorValue(rgba_color color)
{
    if(lastColor_ != color)
    {
        if(lastColor_ != color)
        {
            lastColor_ = color;

            if (surface_->IsX32())
                X32SetColorValue(color);
            else
                surface_->SendOSCMessage(this, oscAddress_ + "/Color", color.to_string());
        }
    }
}

void OSC_FeedbackProcessor::X32SetColorValue(rgba_color color)
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

void OSC_FeedbackProcessor::ForceValue(map<string, string> &properties, double value)
{
    if(DAW::GetCurrentNumberOfMilliseconds() - GetWidget()->GetLastIncomingMessageTime() < 50) // adjust the 50 millisecond value to give you smooth behaviour without making updates sluggish
        return;

    lastDoubleValue_ = value;
    surface_->SendOSCMessage(this, oscAddress_, value);
}

void OSC_FeedbackProcessor::ForceValue(map<string, string> &properties, string value)
{
    lastStringValue_ = value;
    surface_->SendOSCMessage(this, oscAddress_, value);
}

void OSC_IntFeedbackProcessor::ForceValue(map<string, string> &properties, double value)
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

////////////////////////////////////////////////////////////////////////////////////////////////////////
// ZoneManager
////////////////////////////////////////////////////////////////////////////////////////////////////////
void ZoneManager::Initialize()
{
    PreProcessZones();

    if(zoneFilePaths_.count("Home") < 1)
    {
        MessageBox(g_hwnd, (surface_->GetName() + " needs a Home Zone to operate, please recheck your installation").c_str(), ("CSI cannot find Home Zone for " + surface_->GetName()).c_str(), MB_OK);
        return;
    }
        
    vector<shared_ptr<Navigator>> navigators;
    navigators.push_back(GetSelectedTrackNavigator());
    vector<shared_ptr<Zone>> dummy; // Needed to satisfy protcol, Home and FocusedFXParam have special Zone handling
    if(sharedThisPtr_ != nullptr)
        ProcessZoneFile(zoneFilePaths_["Home"].filePath, sharedThisPtr_, navigators, dummy, nullptr);
    if(zoneFilePaths_.count("FocusedFXParam") > 0 && sharedThisPtr_ != nullptr)
        ProcessZoneFile(zoneFilePaths_["FocusedFXParam"].filePath, sharedThisPtr_, navigators, dummy, nullptr);
    if(zoneFilePaths_.count("SurfaceFXLayout") > 0)
        ProcessSurfaceFXLayout(zoneFilePaths_["SurfaceFXLayout"].filePath, surfaceFXLayout_, surfaceFXLayoutTemplate_);
    if(zoneFilePaths_.count("FXLayouts") > 0)
        ProcessFXLayouts(zoneFilePaths_["FXLayouts"].filePath, fxLayouts_);
    if(zoneFilePaths_.count("FXPrologue") > 0)
        ProcessFXBoilerplate(zoneFilePaths_["FXPrologue"].filePath, fxPrologue_);
    if(zoneFilePaths_.count("FXEpilogue") > 0)
        ProcessFXBoilerplate(zoneFilePaths_["FXEpilogue"].filePath, fxEpilogue_);
    GoHome();
}

void ZoneManager::GoFocusedFX()
{
    focusedFXZones_.clear();
    
    int trackNumber = 0;
    int itemNumber = 0;
    int fxSlot = 0;
    MediaTrack* focusedTrack = nullptr;
    
    if(DAW::GetFocusedFX2(&trackNumber, &itemNumber, &fxSlot) == 1)
    {
        if(trackNumber > 0)
            focusedTrack = DAW::GetTrack(trackNumber);
        else if(trackNumber == 0)
            focusedTrack = GetMasterTrackNavigator()->GetTrack();
    }
    
    if(focusedTrack)
    {
        char FXName[BUFSZ];
        DAW::TrackFX_GetFXName(focusedTrack, fxSlot, FXName, sizeof(FXName));
        
        if(isAutoFocusedFXMappingEnabled_)
        {
            if( ! TheManager->HaveFXSteppedValuesBeenCalculated(FXName))
                CalculateSteppedValues(FXName, focusedTrack, fxSlot);

            if( ! EnsureZoneAvailable(FXName, focusedTrack, fxSlot))
                return;
        }

        if(zoneFilePaths_.count(FXName) > 0)
        {
            vector<shared_ptr<Navigator>> navigators;
            navigators.push_back(GetSurface()->GetPage()->GetFocusedFXNavigator());
            
            if(sharedThisPtr_ != nullptr)
                ProcessZoneFile(zoneFilePaths_[FXName].filePath, sharedThisPtr_, navigators, focusedFXZones_, nullptr);
            
            for(auto zone :focusedFXZones_)
            {
                zone->SetXTouchDisplayColors("White");
                zone->SetSlotIndex(fxSlot);
                zone->Activate();
            }
        }
    }
    else
        for(auto zone :focusedFXZones_)
            zone->RestoreXTouchDisplayColors();
}

void ZoneManager::GoSelectedTrackFX()
{
    selectedTrackFXZones_.clear();
    
    if(MediaTrack* selectedTrack = surface_->GetPage()->GetSelectedTrack())
    {
        for(int i = 0; i < DAW::TrackFX_GetCount(selectedTrack); i++)
        {
            char FXName[BUFSZ];
            
            DAW::TrackFX_GetFXName(selectedTrack, i, FXName, sizeof(FXName));
            
            if(zoneFilePaths_.count(FXName) > 0)
            {
                vector<shared_ptr<Navigator>> navigators;
                navigators.push_back(GetSurface()->GetPage()->GetSelectedTrackNavigator());
                if(sharedThisPtr_ != nullptr)
                    ProcessZoneFile(zoneFilePaths_[FXName].filePath, sharedThisPtr_, navigators, selectedTrackFXZones_, nullptr);
                
                selectedTrackFXZones_.back()->SetSlotIndex(i);
                selectedTrackFXZones_.back()->Activate();
            }
        }
    }
}

void ZoneManager::GoTrackFXSlot(MediaTrack* track, shared_ptr<Navigator> navigator, int fxSlot)
{
    char FXName[BUFSZ];
    
    DAW::TrackFX_GetFXName(track, fxSlot, FXName, sizeof(FXName));
    
    EnsureZoneAvailable(FXName, track, fxSlot);
    
    if(zoneFilePaths_.count(FXName) > 0)
    {
        vector<shared_ptr<Navigator>> navigators;
        navigators.push_back(navigator);
        
        if(sharedThisPtr_ != nullptr)
            ProcessZoneFile(zoneFilePaths_[FXName].filePath, sharedThisPtr_, navigators, fxSlotZones_, nullptr);
        
        if(fxSlotZones_.size() > 0)
        {
            fxSlotZones_.back()->SetSlotIndex(fxSlot);
            fxSlotZones_.back()->Activate();
        }
    }
}

void ZoneManager::RemapAutoZone()
{
    if(focusedFXZones_.size() == 1)
    {
        if(::RemapAutoZoneDialog(sharedThisPtr_, focusedFXZones_[0]->GetSourceFilePath(), fxPrologue_, fxEpilogue_))
        {
            PreProcessZoneFile(focusedFXZones_[0]->GetSourceFilePath(), sharedThisPtr_);
            GoFocusedFX();
        }
    }
    else if(fxSlotZones_.size() == 1)
    {
        if(::RemapAutoZoneDialog(sharedThisPtr_, fxSlotZones_[0]->GetSourceFilePath(), fxPrologue_, fxEpilogue_))
        {
            vector<shared_ptr<Navigator>> navigators;
            navigators.push_back(fxSlotZones_[0]->GetNavigator());
            
            string filePath = fxSlotZones_[0]->GetSourceFilePath();
            int slotNumber = fxSlotZones_[0]->GetSlotIndex();

            fxSlotZones_.clear();
            
            PreProcessZoneFile(filePath, sharedThisPtr_);
            ProcessZoneFile(filePath, sharedThisPtr_, navigators, fxSlotZones_, nullptr);
            
            fxSlotZones_.back()->SetSlotIndex(slotNumber);
            fxSlotZones_.back()->Activate();
        }
    }
}

void ZoneManager::PreProcessZones()
{
    vector<string> zoneFilesToProcess;
    listZoneFiles(DAW::GetResourcePath() + string("/CSI/Zones/") + zoneFolder_ + "/", zoneFilesToProcess); // recursively find all .zon files, starting at zoneFolder
       
    if(zoneFilesToProcess.size() == 0)
    {
        MessageBox(g_hwnd, (string("Please check your installation, cannot find Zone files in ") + DAW::GetResourcePath() + string("/CSI/Zones/") + zoneFolder_).c_str(), (GetSurface()->GetName() + " Zone folder is missing or empty").c_str(), MB_OK);

        return;
    }
      
    if(sharedThisPtr_ != nullptr)
        for(auto zoneFilename : zoneFilesToProcess)
            PreProcessZoneFile(zoneFilename, sharedThisPtr_);
    
    if(zoneFolder_ != fxZoneFolder_)
    {
        zoneFilesToProcess.clear();
        
        listZoneFiles(DAW::GetResourcePath() + string("/CSI/Zones/") + fxZoneFolder_ + "/", zoneFilesToProcess); // recursively find all .zon files, starting at fxZoneFolder
         
        if(sharedThisPtr_ != nullptr)
            for(auto zoneFilename : zoneFilesToProcess)
                PreProcessZoneFile(zoneFilename, sharedThisPtr_);
    }
}

bool ZoneManager::EnsureFXZoneAvailable(string fxName, MediaTrack* track, int fxIndex)
{
    if( ! TheManager->HaveFXSteppedValuesBeenCalculated(fxName))
        CalculateSteppedValues(fxName, track, fxIndex);
    
    if(zoneFilePaths_.count(fxName) > 0)
        return true;
    else if(isAutoFXMappingEnabled_)
        return EnsureZoneAvailable(fxName, track, fxIndex);
    else
        return false;
}

void ZoneManager::CalculateSteppedValues(string fxName, MediaTrack* track, int fxIndex)
{
    TheManager->SetSteppedValueCount(fxName, -1, 0); // Add dummy value to show the calculation has beeen performed, even though there may be no stepped values for this FX

    // Check for UAD / Plugin Alliance and bail if neither
    if(fxName.find("UAD") == string::npos && fxName.find("Plugin Alliance") == string::npos)
        return;
    
    int totalLayoutCount = 0;
    
    for(auto layout : fxLayouts_)
        totalLayoutCount += layout.channelCount;
    bool wasMuted = false;
    DAW::GetTrackUIMute(track, &wasMuted);
    
    if( ! wasMuted)
        DAW::CSurf_SetSurfaceMute(track, DAW::CSurf_OnMuteChange(track, true), NULL);

    double minvalOut = 0.0;
    double maxvalOut = 0.0;

    int numParams = DAW::TrackFX_GetNumParams(track, fxIndex);

    vector<double> currentValues;

    for(int i = 0; i < numParams && i <= totalLayoutCount; i++)
        currentValues.push_back(DAW::TrackFX_GetParam(track, fxIndex, i, &minvalOut, &maxvalOut));
    
    for(int i = 0; i < numParams && i <= totalLayoutCount; i++)
    {
        int stepCount = 1;
        double stepValue = 0.0;
        
        for(double value = 0.0; value < 1.01; value += .01)
        {
            DAW::TrackFX_SetParam(track, fxIndex, i, value);
            
            double fxValue = DAW::TrackFX_GetParam(track, fxIndex, i, &minvalOut, &maxvalOut);
            
            if(stepValue != fxValue)
            {
                stepValue = fxValue;
                stepCount++;
            }
        }
        
        if(stepCount > 1 && stepCount < 31)
            TheManager->SetSteppedValueCount(fxName, i, stepCount);
    }
    
    for(int i = 0; i < numParams && i <= totalLayoutCount; i++)
        DAW::TrackFX_SetParam(track, fxIndex, i, currentValues[i]);
    
    if( ! wasMuted)
        DAW::CSurf_SetSurfaceMute(track, DAW::CSurf_OnMuteChange(track, false), NULL);
}

bool ZoneManager::EnsureZoneAvailable(string fxName, MediaTrack* track, int fxIndex)
{    
    if(zoneFilePaths_.count(fxName) > 0)
        return true;

    if(fxLayouts_.size() == 0)
        return false;

    if(surfaceFXLayout_.size() == 0)
        return false;
            
    string path = DAW::GetResourcePath() + string("/CSI/Zones/") + fxZoneFolder_ + "/AutoGeneratedFXZones";
    
    if(! filesystem::exists(path) || ! filesystem::is_directory(path))
        filesystem::create_directory(path);
    
    path += "/" + regex_replace(fxName, regex(BadFileChars), "_") + ".zon";

    vector<string> prefixes =
    {
        "AU: Tube-Tech ",
        "AU: AU ",
        "AU: UAD UA ",
        "AU: UAD Pultec ",
        "AU: UAD Tube-Tech ",
        "AU: UAD Softube ",
        "AU: UAD Teletronix ",
        "AU: UADx ",
        "AU: UAD ",
        "AU: ",
        "AUi: ",
        "VST: TDR ",
        "VST: UAD UA ",
        "VST: UAD Pultec ",
        "VST: UAD Tube-Tech ",
        "VST: UAD Softube ",
        "VST: UAD Teletronix ",
        "VST: UAD ",
        "VST3: UADx ",
        "VST3i: UADx ",
        "VST: ",
        "VSTi: ",
        "VST3: ",
        "VST3i: ",
        "JS: ",
        "Rewire: ",
        "CLAP: ",
        "CLAPi: ",
    };
    
    string paramAction = " FXParam ";
    
    if(fxName.find("JS:") != string::npos)
        paramAction = " JSFXParam ";
    
    string alias = fxName;
    
    for(auto prefix : prefixes)
    {
        if(fxName.find(prefix) == 0)
        {
            alias = fxName.substr(prefix.length(), fxName.length());
            break;
        }
    }
           
    alias = alias.substr(0, alias.find(" ("));
    
    CSIZoneInfo info;
    info.filePath = path;
    info.alias = alias;

    int totalAvailableChannels = 0;
    
    for(auto info : fxLayouts_)
        totalAvailableChannels += info.channelCount;
        
    AddZoneFilePath(fxName, info);
    surface_->GetPage()->AddZoneFilePath(surface_, fxZoneFolder_, fxName, info);

    ofstream fxZone(path);

    if(fxZone.is_open())
    {
        fxZone << "Zone \"" + fxName + "\" \"" + alias + "\"" + GetLineEnding();
        
        for(auto line : fxPrologue_)
            fxZone << "\t" + line + GetLineEnding();
               
        fxZone << "\n" + BeginAutoSection + GetLineEnding();
        
        int layoutIndex = 0;
        int channelIndex = 1;
             
        vector<string> actionWidgets;
        
        string actionWidget = surfaceFXLayout_[0][0];
     
        actionWidgets.push_back(actionWidget);
        
        for(auto tag : surfaceFXLayoutTemplate_)
            if(tag[0] == "WidgetTypes")
                for(int i = 1; i < tag.size(); i++)
                    if(tag[i] != actionWidget)
                        actionWidgets.push_back(tag[i]);

        for(int paramIdx = 0; paramIdx < DAW::TrackFX_GetNumParams(track, fxIndex) && paramIdx < totalAvailableChannels; paramIdx++)
        {
            for(int widgetIdx = 0; widgetIdx < actionWidgets.size(); widgetIdx++)
            {
                for(int lineIdx = 0; lineIdx < surfaceFXLayout_.size(); lineIdx++)
                {
                    for(int tokenIdx = 0; tokenIdx < surfaceFXLayout_[lineIdx].size(); tokenIdx++)
                    {
                        if(tokenIdx == 0)
                        {
                            string modifiers = "";
                            
                            if(fxLayouts_[layoutIndex].modifiers != "")
                                modifiers = fxLayouts_[layoutIndex].modifiers + "+";
                            
                            if(widgetIdx == 0)
                                fxZone << "\t" + modifiers + surfaceFXLayout_[lineIdx][tokenIdx] + fxLayouts_[layoutIndex].suffix + to_string(channelIndex) + "\t";
                            else
                            {
                                if(lineIdx == 0)
                                    fxZone << "\t" + modifiers + actionWidgets[widgetIdx] + fxLayouts_[layoutIndex].suffix + to_string(channelIndex) + "\t";
                                else
                                    fxZone << "\t" + string("NullDisplay") + "\t";
                            }
                        }
                        else if(tokenIdx == 1)
                        {
                            if(widgetIdx == 0)
                                fxZone <<  surfaceFXLayout_[lineIdx][tokenIdx];
                            else
                                fxZone <<  string("NoAction");
                            
                            if(widgetIdx == 0 && surfaceFXLayout_[lineIdx][tokenIdx] == "FixedTextDisplay")
                                fxZone << " \"" + TheManager->GetTCPFXParamName(track, fxIndex, paramIdx) + "\"";
                            else if(widgetIdx == 0)
                                fxZone << " " + to_string(paramIdx);
                            
                            if(widgetIdx == 0 && surfaceFXLayout_[lineIdx][tokenIdx] == "FXParam")
                            {
                                int steppedValueCount =  TheManager->GetSteppedValueCount(fxName, paramIdx);
                                
                                if(steppedValueCount > 1 && steppedValueCount < SteppedValueDictionary.size())
                                {
                                    fxZone << " [ ";
                                    
                                    for(auto step : SteppedValueDictionary[steppedValueCount])
                                    {
                                        ostringstream stepStr;
                                        stepStr << std::setprecision(2) << step;
                                        fxZone << stepStr.str();
                                        fxZone << " ";
                                    }
                                    
                                    fxZone << "]";
                                }
                            }
                        }
                        else if(widgetIdx == 0)
                            fxZone << " " + surfaceFXLayout_[lineIdx][tokenIdx];
                    }
                    
                    fxZone << GetLineEnding();
                }
                
                fxZone << GetLineEnding();
            }
            
            channelIndex++;
            
            fxZone << GetLineEnding();
            
            if(channelIndex > fxLayouts_[layoutIndex].channelCount)
            {
                channelIndex = 1;
                
                if(layoutIndex < fxLayouts_.size() - 1)
                    layoutIndex++;
                else
                    break;
            }
        }
                
        // GAW -- pad partial rows
        if(channelIndex != 1 && channelIndex <= fxLayouts_[layoutIndex].channelCount)
        {
            while(channelIndex <= fxLayouts_[layoutIndex].channelCount)
            {
                for(int widgetIdx = 0; widgetIdx < actionWidgets.size(); widgetIdx++)
                {
                    string modifiers = "";
                    
                    if(fxLayouts_[layoutIndex].modifiers != "")
                        modifiers = fxLayouts_[layoutIndex].modifiers + "+";
                    
                    fxZone << "\t" + modifiers + actionWidgets[widgetIdx] + fxLayouts_[layoutIndex].suffix + to_string(channelIndex) + "\tNoAction" + GetLineEnding();
                    
                    if(widgetIdx == 0 && surfaceFXLayout_.size() > 2 && surfaceFXLayout_[1].size() > 0 && surfaceFXLayout_[2].size() > 0)
                    {
                        fxZone << "\t" + modifiers + surfaceFXLayout_[1][0] + fxLayouts_[layoutIndex].suffix + to_string(channelIndex) + "\tNoAction" + GetLineEnding();
                        fxZone << "\t" + modifiers + surfaceFXLayout_[2][0] + fxLayouts_[layoutIndex].suffix + to_string(channelIndex) + "\tNoAction" + GetLineEnding() + GetLineEnding();
                    }
                    else
                    {
                        fxZone << "\tNullDisplay\tNoAction" + GetLineEnding();
                        fxZone << "\tNullDisplay\tNoAction" + GetLineEnding() + GetLineEnding();
                    }
                }
                
                fxZone << GetLineEnding();
                
                channelIndex++;
            }
        }

        layoutIndex++;
        
        // GAW --pad the remaining rows
        while(layoutIndex < fxLayouts_.size())
        {
            for(int index = 1; index <= fxLayouts_[layoutIndex].channelCount; index++)
            {
                for(int widgetIdx = 0; widgetIdx < actionWidgets.size(); widgetIdx++)
                {
                    string modifiers = "";
                    
                    if(fxLayouts_[layoutIndex].modifiers != "")
                        modifiers = fxLayouts_[layoutIndex].modifiers + "+";
                    
                    fxZone << "\t" + modifiers + actionWidgets[widgetIdx] + fxLayouts_[layoutIndex].suffix + to_string(index) + "\tNoAction" + GetLineEnding();
                    
                    if(widgetIdx == 0 && surfaceFXLayout_.size() > 2 && surfaceFXLayout_[1].size() > 0 && surfaceFXLayout_[2].size() > 0)
                    {
                        fxZone << "\t" + modifiers + surfaceFXLayout_[1][0] + fxLayouts_[layoutIndex].suffix + to_string(index) + "\tNoAction" + GetLineEnding();
                        fxZone << "\t" + modifiers + surfaceFXLayout_[2][0] + fxLayouts_[layoutIndex].suffix + to_string(index) + "\tNoAction" + GetLineEnding() + GetLineEnding();
                    }
                    else
                    {
                        fxZone << "\tNullDisplay\tNoAction" + GetLineEnding();
                        fxZone << "\tNullDisplay\tNoAction" + GetLineEnding() + GetLineEnding();
                    }
                }
                
                fxZone << GetLineEnding();
            }
            
            layoutIndex++;
        }
        
        fxZone << EndAutoSection + GetLineEnding();
                
        for(auto line : fxEpilogue_)
            fxZone << "\t" + line + GetLineEnding();

        fxZone << "ZoneEnd" + GetLineEnding() + GetLineEnding();
        
        for(int i = 0; i < DAW::TrackFX_GetNumParams(track, fxIndex); i++)
            fxZone << to_string(i) + " " + TheManager->GetTCPFXParamName(track, fxIndex, i) + GetLineEnding();
        
        fxZone.close();
    }
    
    return true;
}

void ZoneManager::DoTouch(shared_ptr<Widget> widget, double value)
{
    surface_->TouchChannel(widget->GetChannelNumber(), value);
    
    widget->LogInput(value);
    
    bool isUsed = false;
    
    if(focusedFXParamZone_ != nullptr && isFocusedFXParamMappingEnabled_)
        focusedFXParamZone_->DoTouch(widget, widget->GetName(), isUsed, value);
    
    for(auto zone : focusedFXZones_)
        zone->DoTouch(widget, widget->GetName(), isUsed, value);
    
    if(isUsed)
        return;

    for(auto zone : selectedTrackFXZones_)
        zone->DoTouch(widget, widget->GetName(), isUsed, value);
    
    if(isUsed)
        return;

    for(auto zone : fxSlotZones_)
        zone->DoTouch(widget, widget->GetName(), isUsed, value);
    
    if(isUsed)
        return;

    if(homeZone_ != nullptr)
        homeZone_->DoTouch(widget, widget->GetName(), isUsed, value);
}

shared_ptr<Navigator> ZoneManager::GetMasterTrackNavigator() { return surface_->GetPage()->GetMasterTrackNavigator(); }
shared_ptr<Navigator> ZoneManager::GetSelectedTrackNavigator() { return surface_->GetPage()->GetSelectedTrackNavigator(); }
shared_ptr<Navigator> ZoneManager::GetFocusedFXNavigator() { return surface_->GetPage()->GetFocusedFXNavigator(); }
int ZoneManager::GetNumChannels() { return surface_->GetNumChannels(); }

////////////////////////////////////////////////////////////////////////////////////////////////////////
// ModifierManager
////////////////////////////////////////////////////////////////////////////////////////////////////////
void ModifierManager::RecalculateModifiers()
{
    if(surface_ == nullptr && page_ == nullptr)
        return;
    
    modifierCombinations_.clear();
    modifierCombinations_.push_back(0);
           
    vector<int> activeModifierIndices;
    
    for(int i = 0; i < modifiers_.size(); i++)
        if(modifiers_[i].isEngaged)
            activeModifierIndices.push_back(i);
    
    if(activeModifierIndices.size() > 0)
    {
        for(auto combination : GetCombinations(activeModifierIndices))
        {
            int modifier = 0;
            
            for(int i = 0; i < combination.size(); i++)
                modifier += modifiers_[combination[i]].value;

            modifierCombinations_.push_back(modifier);
        }
        
        sort(modifierCombinations_.begin(), modifierCombinations_.end(), [](const int & a, const int & b) { return a > b; });
    }
    
    if(surface_ != nullptr)
        surface_->GetZoneManager()->UpdateCurrentActionContextModifiers();
    else if(page_ != nullptr)
        page_->UpdateCurrentActionContextModifiers();
}

void ModifierManager::SetLatchModifier(bool value, Modifiers modifier)
{
    if(value && modifiers_[modifier].isEngaged == false)
    {
        modifiers_[modifier].isEngaged = value;
        modifiers_[modifier].pressedTime = DAW::GetCurrentNumberOfMilliseconds();
    }
    else
    {
        double keyReleasedTime = DAW::GetCurrentNumberOfMilliseconds();
        
        if(keyReleasedTime - modifiers_[modifier].pressedTime > 100)
        {
            if(value == 0 && modifiers_[modifier].isEngaged)
                TheManager->Speak(modifierNames_[modifier] + " Unlock");

            modifiers_[modifier].isEngaged = value;
        }
        else
            TheManager->Speak(modifierNames_[modifier] + " Lock");
    }
    
    RecalculateModifiers();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// TrackNavigationManager
////////////////////////////////////////////////////////////////////////////////////////////////////////
void TrackNavigationManager::RebuildTracks()
{
    int oldTracksSize = tracks_.size();
    
    tracks_.clear();
    
    for (int i = 1; i <= GetNumTracks(); i++)
    {
        if(MediaTrack* track = DAW::CSurf_TrackFromID(i, followMCP_))
            if(DAW::IsTrackVisible(track, followMCP_))
                tracks_.push_back(track);
    }
    
    if(tracks_.size() < oldTracksSize)
    {
        for(int i = oldTracksSize; i > tracks_.size(); i--)
            page_->ForceClearTrack(i - trackOffset_);
    }
    
    if(tracks_.size() != oldTracksSize)
        page_->ForceUpdateTrackColors();
}

void TrackNavigationManager::RebuildSelectedTracks()
{
    if(currentTrackVCAFolderMode_ != 3)
        return;

    int oldTracksSize = selectedTracks_.size();
    
    selectedTracks_.clear();
    
    for(int i = 0; i < DAW::CountSelectedTracks(); i++)
        selectedTracks_.push_back(DAW::GetSelectedTrack(i));

    if(selectedTracks_.size() < oldTracksSize)
    {
        for(int i = oldTracksSize; i > selectedTracks_.size(); i--)
            page_->ForceClearTrack(i - selectedTracksOffset_);
    }
    
    if(selectedTracks_.size() != oldTracksSize)
        page_->ForceUpdateTrackColors();
}

void TrackNavigationManager::AdjustSelectedTrackBank(int amount)
{
    if(MediaTrack* selectedTrack = GetSelectedTrack())
    {
        int trackNum = GetIdFromTrack(selectedTrack);
        
        trackNum += amount;
        
        if(trackNum < 1)
            trackNum = 1;
        
        if(trackNum > GetNumTracks())
            trackNum = GetNumTracks();
        
        if(MediaTrack* trackToSelect = GetTrackFromId(trackNum))
        {
            DAW::SetOnlyTrackSelected(trackToSelect);
            if(GetScrollLink())
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
    if(isRewinding_ || isFastForwarding_) // set the cursor to the Play position
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

void ControlSurface::OnTrackSelection(MediaTrack* track)
{
    if(widgetsByName_.count("OnTrackSelection") > 0)
    {
        if(DAW::GetMediaTrackInfo_Value(track, "I_SELECTED"))
            zoneManager_->DoAction(widgetsByName_["OnTrackSelection"], 1.0);
        else
            zoneManager_->OnTrackDeselection();
        
        zoneManager_->OnTrackSelection();
    }
}

void ControlSurface::ForceClearTrack(int trackNum)
{
    for(auto widget : widgets_)
        if(widget->GetChannelNumber() + channelOffset_ == trackNum)
            widget->ForceClear();
}

void ControlSurface::ForceUpdateTrackColors()
{
    for( auto processor : trackColorFeedbackProcessors_)
        processor->ForceUpdateTrackColors();
}

vector<rgba_color> ControlSurface::GetTrackColors()
{
    if(fixedTrackColors_.size() == numChannels_)
        return fixedTrackColors_;
    else
    {
        rgba_color white;
        white.r = 255;
        white.g = 255;
        white.b = 255;

        vector<rgba_color> colors;
        
        for(int i = 0; i < numChannels_; i++)
        {
            if(MediaTrack* track = page_->GetNavigatorForChannel(i + channelOffset_)->GetTrack())
                colors.push_back(DAW::GetTrackColor(track));
            else
                colors.push_back(white);
        }
        
        return colors;
    }
}

void ControlSurface::RequestUpdate()
{
    for( auto processor : trackColorFeedbackProcessors_)
        processor->UpdateTrackColors();
    
    zoneManager_->RequestUpdate();
    
    if(isRewinding_)
    {
        if(DAW::GetCursorPosition() == 0)
            StopRewinding();
        else
        {
            DAW::CSurf_OnRew(0);

            if(speedX5_ == true)
            {
                DAW::CSurf_OnRew(0);
                DAW::CSurf_OnRew(0);
                DAW::CSurf_OnRew(0);
                DAW::CSurf_OnRew(0);
            }
        }
    }
        
    else if(isFastForwarding_)
    {
        if(DAW::GetCursorPosition() > DAW::GetProjectLength(nullptr))
            StopFastForwarding();
        else
        {
            DAW::CSurf_OnFwd(0);
            
            if(speedX5_ == true)
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
    if(modifierManager_ != nullptr)
        return modifierManager_->GetShift();
    else
        return page_->GetModifierManager()->GetShift();
}

bool ControlSurface::GetOption()
{
    if(modifierManager_ != nullptr)
        return modifierManager_->GetOption();
    else
        return page_->GetModifierManager()->GetOption();
}

bool ControlSurface::GetControl()
{
    if(modifierManager_ != nullptr)
        return modifierManager_->GetControl();
    else
        return page_->GetModifierManager()->GetControl();
}

bool ControlSurface::GetAlt()
{
    if(modifierManager_ != nullptr)
        return modifierManager_->GetAlt();
    else
        return page_->GetModifierManager()->GetAlt();
}

bool ControlSurface::GetFlip()
{
    if(modifierManager_ != nullptr)
        return modifierManager_->GetFlip();
    else
        return page_->GetModifierManager()->GetFlip();
}

bool ControlSurface::GetGlobal()
{
    if(modifierManager_ != nullptr)
        return modifierManager_->GetGlobal();
    else
        return page_->GetModifierManager()->GetGlobal();
}

bool ControlSurface::GetMarker()
{
    if(modifierManager_ != nullptr)
        return modifierManager_->GetMarker();
    else
        return page_->GetModifierManager()->GetMarker();
}

bool ControlSurface::GetNudge()
{
    if(modifierManager_ != nullptr)
        return modifierManager_->GetNudge();
    else
        return page_->GetModifierManager()->GetNudge();
}

bool ControlSurface::GetZoom()
{
    if(modifierManager_ != nullptr)
        return modifierManager_->GetZoom();
    else
        return page_->GetModifierManager()->GetZoom();
}

bool ControlSurface::GetScrub()
{
    if(modifierManager_ != nullptr)
        return modifierManager_->GetScrub();
    else
        return page_->GetModifierManager()->GetScrub();
}

void ControlSurface::SetShift(bool value)
{
    if(modifierManager_ != nullptr)
        modifierManager_->SetShift(value);
    else
        page_->GetModifierManager()->SetShift(value);
}

void ControlSurface::SetOption(bool value)
{
    if(modifierManager_ != nullptr)
        modifierManager_->SetOption(value);
    else
        page_->GetModifierManager()->SetOption(value);
}

void ControlSurface::SetControl(bool value)
{
    if(modifierManager_ != nullptr)
        modifierManager_->SetControl(value);
    else
        page_->GetModifierManager()->SetControl(value);
}

void ControlSurface::SetAlt(bool value)
{
    if(modifierManager_ != nullptr)
        modifierManager_->SetAlt(value);
    else
        page_->GetModifierManager()->SetAlt(value);
}

void ControlSurface::SetFlip(bool value)
{
    if(modifierManager_ != nullptr)
        modifierManager_->SetShift(value);
    else
        page_->GetModifierManager()->SetFlip(value);
}

void ControlSurface::SetGlobal(bool value)
{
    if(modifierManager_ != nullptr)
        modifierManager_->SetGlobal(value);
    else
        page_->GetModifierManager()->SetGlobal(value);
}

void ControlSurface::SetMarker(bool value)
{
    if(modifierManager_ != nullptr)
        modifierManager_->SetMarker(value);
    else
        page_->GetModifierManager()->SetMarker(value);
}

void ControlSurface::SetNudge(bool value)
{
    if(modifierManager_ != nullptr)
        modifierManager_->SetNudge(value);
    else
        page_->GetModifierManager()->SetNudge(value);
}

void ControlSurface::SetZoom(bool value)
{
    if(modifierManager_ != nullptr)
        modifierManager_->SetZoom(value);
    else
        page_->GetModifierManager()->SetZoom(value);
}

void ControlSurface::SetScrub(bool value)
{
    if(modifierManager_ != nullptr)
        modifierManager_->SetScrub(value);
    else
        page_->GetModifierManager()->SetScrub(value);
}

vector<int> &ControlSurface::GetModifiers()
{
    if(modifierManager_ != nullptr)
        return modifierManager_->GetModifiers();
    else
        return page_->GetModifierManager()->GetModifiers();
}

void ControlSurface::ClearModifiers()
{
    if(modifierManager_ != nullptr)
        modifierManager_->ClearModifiers();
    else
        page_->GetModifierManager()->ClearModifiers();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi_ControlSurfaceIO
////////////////////////////////////////////////////////////////////////////////////////////////////////
void Midi_ControlSurfaceIO::HandleExternalInput(Midi_ControlSurface* surface)
{
    if(midiInput_)
    {
        DAW::SwapBufsPrecise(midiInput_);
        MIDI_eventlist* list = midiInput_->GetReadBuf();
        int bpos = 0;
        MIDI_event_t* evt;
        while ((evt = list->EnumItems(&bpos)))
            surface->ProcessMidiMessage((MIDI_event_ex_t*)evt);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi_ControlSurface
////////////////////////////////////////////////////////////////////////////////////////////////////////
shared_ptr<Midi_ControlSurface> Midi_ControlSurface::GetInstance(bool useLocalmodifiers, shared_ptr<Page> page, const string name, int numChannels, int channelOffset, string templateFilename, string zoneFolder, string fxZoneFolder, shared_ptr<Midi_ControlSurfaceIO> surfaceIO)
{
    shared_ptr<Midi_ControlSurface> surface = make_shared<Midi_ControlSurface>(page, name, numChannels, channelOffset, templateFilename, surfaceIO, ProtectedTag{});
    
    surface->zoneManager_ = make_shared<ZoneManager>(surface, zoneFolder, fxZoneFolder);
    surface->zoneManager_->SetSharedThisPtr(surface->zoneManager_);
    
    ProcessMIDIWidgetFile(string(DAW::GetResourcePath()) + "/CSI/Surfaces/Midi/" + templateFilename, surface);
    surface->InitHardwiredWidgets(surface);
    surface->InitializeMeters();
    surface->zoneManager_->Initialize();

    if(useLocalmodifiers)
        surface->modifierManager_ = make_shared<ModifierManager>(surface);
            
    return surface;
}

void Midi_ControlSurface::ProcessMidiMessage(const MIDI_event_ex_t* evt)
{
    bool isMapped = false;
    
    // At this point we don't know how much of the message comprises the key, so try all three
    if(Midi_CSIMessageGeneratorsByMessage_.count(evt->midi_message[0] * 0x10000 + evt->midi_message[1] * 0x100 + evt->midi_message[2]) > 0)
    {
        isMapped = true;
        for( auto generator : Midi_CSIMessageGeneratorsByMessage_[evt->midi_message[0] * 0x10000 + evt->midi_message[1] * 0x100 + evt->midi_message[2]])
            generator->ProcessMidiMessage(evt);
    }
    else if(Midi_CSIMessageGeneratorsByMessage_.count(evt->midi_message[0] * 0x10000 + evt->midi_message[1] * 0x100) > 0)
    {
        isMapped = true;
        for( auto generator : Midi_CSIMessageGeneratorsByMessage_[evt->midi_message[0] * 0x10000 + evt->midi_message[1] * 0x100])
            generator->ProcessMidiMessage(evt);
    }
    else if(Midi_CSIMessageGeneratorsByMessage_.count(evt->midi_message[0] * 0x10000) > 0)
    {
        isMapped = true;
        for( auto generator : Midi_CSIMessageGeneratorsByMessage_[evt->midi_message[0] * 0x10000])
            generator->ProcessMidiMessage(evt);
    }
    
    if(TheManager->GetSurfaceRawInDisplay() || (! isMapped && TheManager->GetSurfaceInDisplay()))
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %02x  %02x  %02x \n", name_.c_str(), evt->midi_message[0], evt->midi_message[1], evt->midi_message[2]);
        DAW::ShowConsoleMsg(buffer);
    }
}

void Midi_ControlSurface::SendMidiSysExMessage(MIDI_event_ex_t* midiMessage)
{
    surfaceIO_->SendMidiMessage(midiMessage);
    
    string output = "OUT->" + name_ + " ";
    
    for(int i = 0; i < midiMessage->size; i++)
    {
        char buffer[32];
        
        snprintf(buffer, sizeof(buffer), "%02x ", midiMessage->midi_message[i]);
        
        output += buffer;
    }
    
    output += "\n";

    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(output.c_str());
}

void Midi_ControlSurface::SendMidiMessage(int first, int second, int third)
{
    surfaceIO_->SendMidiMessage(first, second, third);
    
    if(TheManager->GetSurfaceOutDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "%s  %02x  %02x  %02x \n", ("OUT->" + name_).c_str(), first, second, third);
        DAW::ShowConsoleMsg(buffer);
    }
}

 ////////////////////////////////////////////////////////////////////////////////////////////////////////
 // OSC_ControlSurfaceIO
 ////////////////////////////////////////////////////////////////////////////////////////////////////////
OSC_ControlSurfaceIO::OSC_ControlSurfaceIO(string surfaceName, string receiveOnPort, string transmitToPort, string transmitToIpAddress) : name_(surfaceName)
{
    if (receiveOnPort != transmitToPort)
    {
        inSocket_  = GetInputSocketForPort(surfaceName, stoi(receiveOnPort));;
        outSocket_ = GetOutputSocketForAddressAndPort(surfaceName, transmitToIpAddress, stoi(transmitToPort));
    }
    else // WHEN INPUT AND OUTPUT SOCKETS ARE THE SAME -- DO MAGIC :)
    {
        shared_ptr<oscpkt::UdpSocket> inSocket = GetInputSocketForPort(surfaceName, stoi(receiveOnPort));;

        struct addrinfo hints;
        struct addrinfo* addressInfo;
        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family = AF_INET;      // IPV4
        hints.ai_socktype = SOCK_DGRAM; // UDP
        hints.ai_flags = 0x00000001;    // socket address is intended for bind
        getaddrinfo(transmitToIpAddress.c_str(), transmitToPort.c_str(), &hints, &addressInfo);
        memcpy(&(inSocket->remote_addr), (void*)(addressInfo->ai_addr), addressInfo->ai_addrlen);

        inSocket_  = inSocket;
        outSocket_ = inSocket;
        outputSockets_[surfaceName] = outSocket_;
    }
 }

 void OSC_ControlSurfaceIO::HandleExternalInput(OSC_ControlSurface* surface)
 {
    if(inSocket_ != nullptr && inSocket_->isOk())
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
shared_ptr<OSC_ControlSurface> OSC_ControlSurface::GetInstance(bool useLocalmodifiers, shared_ptr<Page> page, const string name, int numChannels, int channelOffset, string templateFilename, string zoneFolder, string fxZoneFolder, shared_ptr<OSC_ControlSurfaceIO> surfaceIO)
{
    shared_ptr<OSC_ControlSurface> surface = make_shared<OSC_ControlSurface>(page, name, numChannels, channelOffset, templateFilename, surfaceIO, ProtectedTag{});
    
    surface->zoneManager_ = make_shared<ZoneManager>(surface, zoneFolder, fxZoneFolder);
    surface->zoneManager_->SetSharedThisPtr(surface->zoneManager_);

    ProcessOSCWidgetFile(string(DAW::GetResourcePath()) + "/CSI/Surfaces/OSC/" + templateFilename, surface);
    surface->InitHardwiredWidgets(surface);
    surface->zoneManager_->Initialize();
    
    if(useLocalmodifiers)
        surface->modifierManager_ = make_shared<ModifierManager>(surface);
            
    return surface;
}

void OSC_ControlSurface::ProcessOSCMessage(string message, double value)
{
    if(CSIMessageGeneratorsByMessage_.count(message) > 0)
        CSIMessageGeneratorsByMessage_[message]->ProcessMessage(value);
    
    if(TheManager->GetSurfaceInDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %s  %f  \n", name_.c_str(), message.c_str(), value);
        DAW::ShowConsoleMsg(buffer);
    }
}

void OSC_ControlSurface::SendOSCMessage(string zoneName)
{
    string oscAddress(zoneName);
    oscAddress = regex_replace(oscAddress, regex(BadFileChars), "_");
    oscAddress = "/" + oscAddress;

    surfaceIO_->SendOSCMessage(oscAddress);
        
    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg((zoneName + "->" + "LoadingZone---->" + name_ + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(string oscAddress, int value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
        
    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT->" + name_ + " " + oscAddress + " " + to_string(value) + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(string oscAddress, double value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
        
    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT->" + name_ + " " + oscAddress + " " + to_string(value) + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(string oscAddress, string value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
        
    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT->" + name_ + " " + oscAddress + " " + value + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, double value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
    
    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT->" + name_ + " " + feedbackProcessor->GetWidget()->GetName() + " " + oscAddress + " " + to_string(value) + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, int value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);

    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT->" + name_ + " " + feedbackProcessor->GetWidget()->GetName() + " " + oscAddress + " " + to_string(value) + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, string value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);

    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg(("OUT->" + name_ + " " + feedbackProcessor->GetWidget()->GetName() + " " + oscAddress + " " + value + "\n").c_str());
}

void Midi_ControlSurface::InitializeMCU()
{
    vector<vector<int>> sysExLines;
    
    sysExLines.push_back({0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x21, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x00, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x01, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x02, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x03, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x04, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x05, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x06, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x07, 0x01, 0xF7});
    
    struct
    {
        MIDI_event_ex_t evt;
        char data[BUFSZ];
    } midiSysExData;
    
    for(auto line : sysExLines)
    {
        memset(midiSysExData.data, 0, sizeof(midiSysExData.data));
        
        midiSysExData.evt.frame_offset=0;
        midiSysExData.evt.size=0;

        for(auto value : line)
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = value;
        
        SendMidiSysExMessage(&midiSysExData.evt);
    }
}

void Midi_ControlSurface::InitializeMCUXT()
{
    vector<vector<int>> sysExLines;
    
    sysExLines.push_back({0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x00, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x21, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x00, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x01, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x02, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x03, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x04, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x05, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x06, 0x01, 0xF7});
    sysExLines.push_back({0xF0, 0x00, 0x00, 0x66, 0x15, 0x20, 0x07, 0x01, 0xF7});
    
    struct
    {
        MIDI_event_ex_t evt;
        char data[BUFSZ];
    } midiSysExData;
    
    for(auto line : sysExLines)
    {
        midiSysExData.evt.frame_offset=0;
        midiSysExData.evt.size=0;
        
        for(auto value : line)
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = value;
        
        SendMidiSysExMessage(&midiSysExData.evt);
    }

}

