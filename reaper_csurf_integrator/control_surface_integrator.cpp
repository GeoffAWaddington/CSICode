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
static map<string, oscpkt::UdpSocket*> inputSockets_;
static map<string, oscpkt::UdpSocket*> outputSockets_;

static oscpkt::UdpSocket* GetInputSocketForPort(string surfaceName, int inputPort)
{
    if(inputSockets_.count(surfaceName) > 0)
        return inputSockets_[surfaceName]; // return existing
    
    // otherwise make new
    oscpkt::UdpSocket* newInputSocket = new oscpkt::UdpSocket();
    
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

static oscpkt::UdpSocket* GetOutputSocketForAddressAndPort(string surfaceName, string address, int outputPort)
{
    if(outputSockets_.count(surfaceName) > 0)
        return outputSockets_[surfaceName]; // return existing
    
    // otherwise make new
    oscpkt::UdpSocket* newOutputSocket = new oscpkt::UdpSocket();
    
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

static void PreProcessZoneFile(string filePath, ZoneManager* zoneManager)
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

static void GetWidgets(ZoneManager* zoneManager, int numChannels, vector<string> tokens, vector<Widget*> &results)
{
    vector<string> widgetLine;
                        
    for(int i = 1; i < tokens.size(); i++)
        widgetLine.push_back(tokens[i]);

    if(widgetLine.size() != numChannels)
        ExpandLine(numChannels, widgetLine);

    if(widgetLine.size() != numChannels)
        return;
    
    vector<Widget*> widgets;
    
    for(auto widgetName : widgetLine)
        if(Widget* widget = zoneManager->GetSurface()->GetWidgetByName(widgetName))
            widgets.push_back(widget);
    
    if(widgets.size() != numChannels)
        return;

    results = widgets;
}

vector<rgba_color> GetColorValues(vector<string> colors)
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

static void BuildFXTemplates(int modifier, string baseControl, string baseNameDisplay, string baseValueDisplay, map<string, map<int, vector<shared_ptr<ActionTemplate>>>> &actionTemplatesDictionary, vector<string> &tokens, ControlSurface* surface)
{
    for(int i = 1; i < tokens.size(); i++)
    {
        string control = baseControl + to_string(i);
        
        if(surface->GetWidgetByName(control))
        {
            shared_ptr<ActionTemplate> fxParamTemplate = make_shared<ActionTemplate>();
            fxParamTemplate->widgetName = control;
            fxParamTemplate->actionName = "FXParam";
            fxParamTemplate->params.push_back(fxParamTemplate->actionName);
            fxParamTemplate->params.push_back(tokens[i]);
            fxParamTemplate->provideFeedback = true;
            actionTemplatesDictionary[control][modifier].push_back(fxParamTemplate);
        }
        
        string nameDisplay = baseNameDisplay + to_string(i);
        
        if(surface->GetWidgetByName(nameDisplay))
        {
            shared_ptr<ActionTemplate> fxParamNameDisplayTemplate = make_shared<ActionTemplate>();
            fxParamNameDisplayTemplate->widgetName = nameDisplay;
            fxParamNameDisplayTemplate->actionName = "FXParamNameDisplay";
            fxParamNameDisplayTemplate->params.push_back(fxParamNameDisplayTemplate->actionName);
            fxParamNameDisplayTemplate->params.push_back(tokens[i]);
            fxParamNameDisplayTemplate->provideFeedback = true;
            actionTemplatesDictionary[nameDisplay][modifier].push_back(fxParamNameDisplayTemplate);
        }

        string valueDisplay = baseValueDisplay + to_string(i);
        
        if(surface->GetWidgetByName(valueDisplay))
        {
            shared_ptr<ActionTemplate> fxParamValueDisplayTemplate = make_shared<ActionTemplate>();
            fxParamValueDisplayTemplate->widgetName = valueDisplay;
            fxParamValueDisplayTemplate->actionName = "FXParamValueDisplay";
            fxParamValueDisplayTemplate->params.push_back(fxParamValueDisplayTemplate->actionName);
            fxParamValueDisplayTemplate->params.push_back(tokens[i]);
            fxParamValueDisplayTemplate->provideFeedback = true;
            actionTemplatesDictionary[valueDisplay][modifier].push_back(fxParamValueDisplayTemplate);
        }
    }
}

static void ProcessZoneFile(string filePath, ZoneManager* zoneManager, vector<Navigator*> &navigators, vector<shared_ptr<Zone>> &zones, shared_ptr<Zone> enclosingZone)
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
    
    shared_ptr<ActionTemplate> currentActionTemplate = nullptr;
    
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
                    currentActionTemplate = nullptr;
                    
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
                            
                            Widget* widget = zoneManager->GetSurface()->GetWidgetByName(surfaceWidgetName);
                                                        
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
                        
                        if(subZones.size() > 0)
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
                 
                else if(tokens[0] == "FXFaders" && tokens.size() > 1)
                    BuildFXTemplates(0, "Fader", "DisplayUpper", "DisplayLower", actionTemplatesDictionary, tokens, zoneManager->GetSurface());

                else if(tokens[0] == "FXRotaries" && tokens.size() > 1)
                    BuildFXTemplates(0, "Rotary", "DisplayUpper", "DisplayLower", actionTemplatesDictionary, tokens, zoneManager->GetSurface());

                else if(tokens[0] == "FXRotariesShift" && tokens.size() > 1)
                    BuildFXTemplates(4, "Rotary", "DisplayUpper", "DisplayLower", actionTemplatesDictionary, tokens, zoneManager->GetSurface());

                else if(tokens[0] == "FXRotariesOption" && tokens.size() > 1)
                    BuildFXTemplates(8, "Rotary", "DisplayUpper", "DisplayLower", actionTemplatesDictionary, tokens, zoneManager->GetSurface());

                else if(tokens[0] == "FXRotariesControl" && tokens.size() > 1)
                    BuildFXTemplates(16, "Rotary", "DisplayUpper", "DisplayLower", actionTemplatesDictionary, tokens, zoneManager->GetSurface());

                else if(tokens[0] == "FXRotariesAlt" && tokens.size() > 1)
                    BuildFXTemplates(32, "Rotary", "DisplayUpper", "DisplayLower", actionTemplatesDictionary, tokens, zoneManager->GetSurface());

                else if(tokens[0] == "FXRotariesA" && tokens.size() > 1)
                    BuildFXTemplates(0, "RotaryA", "DisplayUpperA", "DisplayLowerA", actionTemplatesDictionary, tokens, zoneManager->GetSurface());
                
                else if(tokens[0] == "FXRotariesB" && tokens.size() > 1)
                    BuildFXTemplates(0, "RotaryB", "DisplayUpperB", "DisplayLowerB", actionTemplatesDictionary, tokens, zoneManager->GetSurface());

                else if(tokens[0] == "FXRotariesC" && tokens.size() > 1)
                    BuildFXTemplates(0, "RotaryC", "DisplayUpperC", "DisplayLowerC", actionTemplatesDictionary, tokens, zoneManager->GetSurface());
                
                else if(tokens[0] == "FXRotariesD" && tokens.size() > 1)
                    BuildFXTemplates(0, "RotaryD", "DisplayUpperD", "DisplayLowerD", actionTemplatesDictionary, tokens, zoneManager->GetSurface());
                
                else if(tokens.size() > 1)
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

                    currentActionTemplate = make_shared<ActionTemplate>();
                    
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

void SetColor(vector<string> params, bool &supportsColor, bool &supportsTrackColor, vector<rgba_color> &colorValues)
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

void GetSteppedValues(Widget* widget, string zoneName, int paramNumber, vector<string> params, double &deltaValue, vector<double> &acceleratedDeltaValues, double &rangeMinimum, double &rangeMaximum, vector<double> &steppedValues, vector<int> &acceleratedTickValues)
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
            int baseTickCount = widget->GetSurface()->GetZoneManager()->GetBaseTickCount(steppedValues.size());
            int tickCount = int(baseTickCount / stepSize + 0.5);
            acceleratedTickValues.push_back(tickCount);
        }
    }
}

//////////////////////////////////////////////////////////////////////////////
// Widgets
//////////////////////////////////////////////////////////////////////////////
static void ProcessMidiWidget(int &lineNumber, ifstream &surfaceTemplateFile, vector<string> tokens, Midi_ControlSurface* surface, map<string, double> stepSizes, map<string, map<int, int>> accelerationValuesForDecrement, map<string, map<int, int>> accelerationValuesForIncrement, map<string, vector<double>> accelerationValues)
{
    if(tokens.size() < 2)
        return;
    
    string widgetName = tokens[1];
    
    string widgetClass = "";
    
    if(tokens.size() > 2)
        widgetClass = tokens[2];

    Widget* widget = new Widget(surface, widgetName);
    
    if(tokens[0] == "EWidget")
        widget->SetIsFXAutoMapEligible();
    
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
        
        if(tokens[0] == "WidgetEnd" || tokens[0] == "EWidgetEnd")    // finito baybay - Widget list complete
            break;
        
        tokenLines.push_back(tokens);
    }
    
    if(tokenLines.size() < 1)
        return;
    
    for(int i = 0; i < tokenLines.size(); i++)
    {
        int size = tokenLines[i].size();
        
        string widgetType = tokenLines[i][0];

        // Control Signal Generators
        if(widgetType == "AnyPress" && (size == 4 || size == 7))
            new AnyPress_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        if(widgetType == "Press" && size == 4)
            new PressRelease_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetType == "Press" && size == 7)
            new PressRelease_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), new MIDI_event_ex_t(strToHex(tokenLines[i][4]), strToHex(tokenLines[i][5]), strToHex(tokenLines[i][6])));
        else if(widgetType == "Fader14Bit" && size == 4)
            new Fader14Bit_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetType == "Fader7Bit" && size== 4)
            new Fader7Bit_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetType == "Encoder" && size == 4 && widgetClass == "RotaryWidgetClass")
        {
            if(stepSizes.count(widgetClass) > 0 && accelerationValuesForDecrement.count(widgetClass) > 0 && accelerationValuesForIncrement.count(widgetClass) > 0 && accelerationValues.count(widgetClass) > 0)
                new AcceleratedPreconfiguredEncoder_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), stepSizes[widgetClass], accelerationValuesForDecrement[widgetClass], accelerationValuesForIncrement[widgetClass], accelerationValues[widgetClass]);
        }
        else if(widgetType == "Encoder" && size == 4)
            new Encoder_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetType == "Encoder" && size > 4)
            new AcceleratedEncoder_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), tokenLines[i]);
        else if(widgetType == "MFTEncoder" && size == 4)
            new MFT_AcceleratedEncoder_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), tokenLines[i]);
        else if(widgetType == "EncoderPlain" && size == 4)
            new EncoderPlain_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetType == "Encoder7Bit" && size == 4)
            new Encoder7Bit_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetType == "Touch" && size == 7)
            new Touch_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), new MIDI_event_ex_t(strToHex(tokenLines[i][4]), strToHex(tokenLines[i][5]), strToHex(tokenLines[i][6])));
        // Feedback Processors
        FeedbackProcessor* feedbackProcessor = nullptr;

        if(widgetType == "FB_TwoState" && size == 7)
        {
            feedbackProcessor = new TwoState_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), new MIDI_event_ex_t(strToHex(tokenLines[i][4]), strToHex(tokenLines[i][5]), strToHex(tokenLines[i][6])));
        }
        else if(widgetType == "FB_NovationLaunchpadMiniRGB7Bit" && size == 4)
        {
            feedbackProcessor = new NovationLaunchpadMiniRGB7Bit_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetType == "FB_MFT_RGB" && size == 4)
        {
            feedbackProcessor = new MFT_RGB_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetType == "FB_FaderportRGB" && size == 4)
        {
            feedbackProcessor = new FaderportRGB_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetType == "FB_FaderportTwoStateRGB" && size == 4)
        {
            feedbackProcessor = new FPTwoStateRGB_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetType == "FB_FaderportValueBar"  && size == 2)
        {
                feedbackProcessor = new FPValueBar_Midi_FeedbackProcessor(surface, widget, stoi(tokenLines[i][1]));
        }
        else if((widgetType == "FB_FPVUMeter") && size == 2)
        {
            feedbackProcessor = new FPVUMeter_Midi_FeedbackProcessor(surface, widget, stoi(tokenLines[i][1]));
        }
        else if(widgetType == "FB_Fader14Bit" && size == 4)
        {
            feedbackProcessor = new Fader14Bit_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetType == "FB_Fader7Bit" && size == 4)
        {
            feedbackProcessor = new Fader7Bit_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetType == "FB_Encoder" && size == 4)
        {
            feedbackProcessor = new Encoder_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetType == "FB_ConsoleOneVUMeter" && size == 4)
        {
            feedbackProcessor = new ConsoleOneVUMeter_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetType == "FB_ConsoleOneGainReductionMeter" && size == 4)
        {
            feedbackProcessor = new ConsoleOneGainReductionMeter_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetType == "FB_MCUTimeDisplay" && size == 1)
        {
            feedbackProcessor = new MCU_TimeDisplay_Midi_FeedbackProcessor(surface, widget);
        }
        else if(widgetType == "FB_MCUAssignmentDisplay" && size == 1)
        {
            feedbackProcessor = new FB_MCU_AssignmentDisplay_Midi_FeedbackProcessor(surface, widget);
        }
        else if(widgetType == "FB_QConProXMasterVUMeter" && size == 2)
        {
            feedbackProcessor = new QConProXMasterVUMeter_Midi_FeedbackProcessor(surface, widget, stoi(tokenLines[i][1]));
        }
        else if((widgetType == "FB_MCUVUMeter" || widgetType == "FB_MCUXTVUMeter") && size == 2)
        {
            int displayType = widgetType == "FB_MCUVUMeter" ? 0x14 : 0x15;
            
            feedbackProcessor = new MCUVUMeter_Midi_FeedbackProcessor(surface, widget, displayType, stoi(tokenLines[i][1]));
            
            surface->SetHasMCUMeters(displayType);
        }
        else if(widgetType == "FB_SCE24_Text" && size == 3)
        {
            feedbackProcessor = new SCE24_Text_Midi_FeedbackProcessor(surface, widget, stoi(tokenLines[i][1]), stoi(tokenLines[i][2]));
        }
        else if(widgetType == "FB_SCE24_Bar" && size == 3)
        {
            feedbackProcessor = new SCE24_Bar_Midi_FeedbackProcessor(surface, widget, stoi(tokenLines[i][1]), stoi(tokenLines[i][2]));
        }
        else if(widgetType == "FB_SCE24_OLEDButton" && size == 3)
        {
            feedbackProcessor = new SCE24_OLEDButton_Midi_FeedbackProcessor(surface, widget, strToHex(tokenLines[i][1]), stoi(tokenLines[i][2]));
        }
        else if(widgetType == "FB_SCE24_LEDButton" && size == 2)
        {
            feedbackProcessor = new SCE24_LEDButton_Midi_FeedbackProcessor(surface, widget, strToHex(tokenLines[i][1]));
        }
        else if(widgetType == "FB_SCE24_Background" && size == 2)
        {
            feedbackProcessor = new SCE24_Background_Midi_FeedbackProcessor(surface, widget, strToHex(tokenLines[i][1]));
        }
        else if(widgetType == "FB_SCE24_Ring" && size == 2)
        {
            feedbackProcessor = new SCE24_Ring_Midi_FeedbackProcessor(surface, widget, stoi(tokenLines[i][1]));
        }
        else if((widgetType == "FB_MCUDisplayUpper" || widgetType == "FB_MCUDisplayLower" || widgetType == "FB_MCUXTDisplayUpper" || widgetType == "FB_MCUXTDisplayLower") && size == 2)
        {
            if(widgetType == "FB_MCUDisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_MCUDisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_MCUXTDisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x15, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_MCUXTDisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x15, 0x12, stoi(tokenLines[i][1]));
        }
        else if((widgetType == "FB_XTouchDisplayUpper" || widgetType == "FB_XTouchDisplayLower" || widgetType == "FB_XTouchXTDisplayUpper" || widgetType == "FB_XTouchXTDisplayLower") && size == 2)
        {
            if(widgetType == "FB_XTouchDisplayUpper")
                feedbackProcessor = new XTouchDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_XTouchDisplayLower")
                feedbackProcessor = new XTouchDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_XTouchXTDisplayUpper")
                feedbackProcessor = new XTouchDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x15, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_XTouchXTDisplayLower")
                feedbackProcessor = new XTouchDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x15, 0x12, stoi(tokenLines[i][1]));
        }
        else if((widgetType == "FB_C4DisplayUpper" || widgetType == "FB_C4DisplayLower") && size == 3)
        {
            if(widgetType == "FB_C4DisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x17, stoi(tokenLines[i][1]) + 0x30, stoi(tokenLines[i][2]));
            else if(widgetType == "FB_C4DisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x17, stoi(tokenLines[i][1]) + 0x30, stoi(tokenLines[i][2]));
        }
        else if((widgetType == "FB_FP8ScribbleLine1" || widgetType == "FB_FP16ScribbleLine1"
                 || widgetType == "FB_FP8ScribbleLine2" || widgetType == "FB_FP16ScribbleLine2"
                 || widgetType == "FB_FP8ScribbleLine3" || widgetType == "FB_FP16ScribbleLine3"
                 || widgetType == "FB_FP8ScribbleLine4" || widgetType == "FB_FP16ScribbleLine4") && size == 2)
        {
            if(widgetType == "FB_FP8ScribbleLine1")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x00);
            else if(widgetType == "FB_FP8ScribbleLine2")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x01);
            else if(widgetType == "FB_FP8ScribbleLine3")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x02);
            else if(widgetType == "FB_FP8ScribbleLine4")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x03);

            else if(widgetType == "FB_FP16ScribbleLine1")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x00);
            else if(widgetType == "FB_FP16ScribbleLine2")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x01);
            else if(widgetType == "FB_FP16ScribbleLine3")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x02);
            else if(widgetType == "FB_FP16ScribbleLine4")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x03);
        }
        else if((widgetType == "FB_FP8ScribbleStripMode" || widgetType == "FB_FP16ScribbleStripMode") && size == 2)
        {
            if(widgetType == "FB_FP8ScribbleStripMode")
                feedbackProcessor = new FPScribbleStripMode_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_FP16ScribbleStripMode")
                feedbackProcessor = new FPScribbleStripMode_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]));
        }
        else if((widgetType == "FB_QConLiteDisplayUpper" || widgetType == "FB_QConLiteDisplayUpperMid" || widgetType == "FB_QConLiteDisplayLowerMid" || widgetType == "FB_QConLiteDisplayLower") && size == 2)
        {
            if(widgetType == "FB_QConLiteDisplayUpper")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_QConLiteDisplayUpperMid")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_QConLiteDisplayLowerMid")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(surface, widget, 2, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetType == "FB_QConLiteDisplayLower")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(surface, widget, 3, 0x14, 0x12, stoi(tokenLines[i][1]));
        }

        if(feedbackProcessor != nullptr)
            widget->AddFeedbackProcessor(feedbackProcessor);
    }
}

static void ProcessOSCWidget(int &lineNumber, ifstream &surfaceTemplateFile, vector<string> tokens,  OSC_ControlSurface* surface)
{
    if(tokens.size() < 2)
        return;
    
    Widget* widget = new Widget(surface, tokens[1]);
    
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
        
        if(tokens[0] == "WidgetEnd" || tokens[0] == "EWidgetEnd")    // finito baybay - Widget list complete
            break;
        
        tokenLines.push_back(tokens);
    }

    for(auto tokenLine : tokenLines)
    {
        if(tokenLine.size() > 1 && tokenLine[0] == "Control")
            new CSIMessageGenerator(widget, tokenLine[1]);
        else if(tokenLine.size() > 1 && tokenLine[0] == "AnyPress")
            new AnyPress_CSIMessageGenerator(widget, tokenLine[1]);
        else if (tokenLine.size() > 1 && tokenLine[0] == "MotorizedFaderWithoutTouch")
            new MotorizedFaderWithoutTouch_CSIMessageGenerator(widget, tokenLine[1]);
        else if(tokenLine.size() > 1 && tokenLine[0] == "Touch")
            new Touch_CSIMessageGenerator(widget, tokenLine[1]);
        else if(tokenLine.size() > 1 && tokenLine[0] == "FB_Processor")
            widget->AddFeedbackProcessor(new OSC_FeedbackProcessor(surface, widget, tokenLine[1]));
        else if(tokenLine.size() > 1 && tokenLine[0] == "FB_IntProcessor")
            widget->AddFeedbackProcessor(new OSC_IntFeedbackProcessor(surface, widget, tokenLine[1]));
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

static void ProcessWidgetFile(string filePath, ControlSurface* surface)
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

            if(tokens.size() > 0 && (tokens[0] == "Widget" || tokens[0] == "EWidget"))
            {
                if(filePath[filePath.length() - 3] == 'm')
                    ProcessMidiWidget(lineNumber, file, tokens, (Midi_ControlSurface*)surface, stepSizes, accelerationValuesForDecrement, accelerationValuesForIncrement, accelerationValues);
                if(filePath[filePath.length() - 3] == 'o')
                    ProcessOSCWidget(lineNumber, file, tokens, (OSC_ControlSurface*)surface);
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

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Manager
////////////////////////////////////////////////////////////////////////////////////////////////////////
void Manager::InitActionsDictionary()
{
    actions_["MetronomePrimaryVolumeDisplay"] =     new MetronomePrimaryVolumeDisplay();
    actions_["MetronomeSecondaryVolumeDisplay"] =   new MetronomeSecondaryVolumeDisplay();
    actions_["MetronomePrimaryVolume"] =            new MetronomePrimaryVolume();
    actions_["MetronomeSecondaryVolume"] =          new MetronomeSecondaryVolume();
    actions_["Speak"] =                             new SpeakOSARAMessage();
    actions_["SendMIDIMessage"] =                   new SendMIDIMessage();
    actions_["SendOSCMessage"] =                    new SendOSCMessage();
    actions_["SaveProject"] =                       new SaveProject();
    actions_["Undo"] =                              new Undo();
    actions_["Redo"] =                              new Redo();
    actions_["TrackAutoMode"] =                     new TrackAutoMode();
    actions_["GlobalAutoMode"] =                    new GlobalAutoMode();
    actions_["TrackAutoModeDisplay"] =              new TrackAutoModeDisplay();
    actions_["GlobalAutoModeDisplay"] =             new GlobalAutoModeDisplay();
    actions_["CycleTrackInputMonitor"] =            new CycleTrackInputMonitor();
    actions_["TrackInputMonitorDisplay"] =          new TrackInputMonitorDisplay();
    actions_["MCUTimeDisplay"] =                    new MCUTimeDisplay();
    actions_["OSCTimeDisplay"] =                    new OSCTimeDisplay();
    actions_["NoAction"] =                          new NoAction();
    actions_["Reaper"] =                            new ReaperAction();
    actions_["FixedTextDisplay"] =                  new FixedTextDisplay(); ;
    actions_["FixedRGBColorDisplay"] =              new FixedRGBColorDisplay();
    actions_["Rewind"] =                            new Rewind();
    actions_["FastForward"] =                       new FastForward();
    actions_["Play"] =                              new Play();
    actions_["Stop"] =                              new Stop();
    actions_["Record"] =                            new Record();
    actions_["CycleTimeline"] =                     new CycleTimeline();
    actions_["ToggleSynchPageBanking"] =            new ToggleSynchPageBanking();
    actions_["ToggleScrollLink"] =                  new ToggleScrollLink();
    actions_["GoVCA"] =                             new GoVCA();
    actions_["VCAModeActivated"] =                  new VCAModeActivated();
    actions_["VCAModeDeactivated"] =                new VCAModeDeactivated();
    actions_["GoFolder"] =                          new GoFolder();
    actions_["FolderModeActivated"] =               new FolderModeActivated();
    actions_["FolderModeDeactivated"] =             new FolderModeDeactivated();
    actions_["GlobalModeDisplay"] =                 new GlobalModeDisplay();
    actions_["CycleTimeDisplayModes"] =             new CycleTimeDisplayModes();
    actions_["NextPage"] =                          new GoNextPage();
    actions_["GoPage"] =                            new GoPage();
    actions_["PageNameDisplay"] =                   new PageNameDisplay();
    actions_["Broadcast"] =                         new Broadcast();
    actions_["Receive"] =                           new Receive();
    actions_["GoHome"] =                            new GoHome();
    actions_["GoSubZone"] =                         new GoSubZone();
    actions_["LeaveSubZone"] =                      new LeaveSubZone();
    actions_["SetXTouchDisplayColors"] =            new SetXTouchDisplayColors();
    actions_["RestoreXTouchDisplayColors"] =        new RestoreXTouchDisplayColors();
    actions_["GoFXSlot"] =                          new GoFXSlot();
    actions_["ToggleEnableFocusedFXMapping"] =      new ToggleEnableFocusedFXMapping();
    actions_["ToggleEnableFocusedFXParamMapping"] = new ToggleEnableFocusedFXParamMapping();
    actions_["GoSelectedTrackFX"] =                 new GoSelectedTrackFX();
    actions_["GoMasterTrack"] =                     new GoMasterTrack();
    actions_["GoTrackSend"] =                       new GoTrackSend();
    actions_["GoTrackReceive"] =                    new GoTrackReceive();
    actions_["GoTrackFXMenu"] =                     new GoTrackFXMenu();
    actions_["GoSelectedTrack"] =                   new GoSelectedTrack();
    actions_["GoSelectedTrackSend"] =               new GoSelectedTrackSend();
    actions_["GoSelectedTrackReceive"] =            new GoSelectedTrackReceive();
    actions_["GoSelectedTrackFXMenu"] =             new GoSelectedTrackFXMenu();
    actions_["GoSelectedTrackTCPFX"] =              new GoSelectedTrackTCPFX();
    actions_["GoFaderFXMapTemplate"] =              new GoFaderFXMapTemplate();
    actions_["GoRotaryFXMapTemplate"] =             new GoRotaryFXMapTemplate();
    actions_["GoRotaryAFXMapTemplate"] =            new GoRotaryAFXMapTemplate();
    actions_["GoRotaryBFXMapTemplate"] =            new GoRotaryBFXMapTemplate();
    actions_["GoRotaryCFXMapTemplate"] =            new GoRotaryCFXMapTemplate();
    actions_["GoRotaryDFXMapTemplate"] =            new GoRotaryDFXMapTemplate();
    actions_["SaveFXMapTemplateRow"] =              new SaveFXMapTemplateRow();
    actions_["BuildSelectedTrackTCPFXZone"] =       new BuildSelectedTrackTCPFXZone();
    actions_["TrackBank"] =                         new TrackBank();
    actions_["VCABank"] =                           new VCABank();
    actions_["FolderBank"] =                        new FolderBank();
    actions_["TrackSendBank"] =                     new TrackSendBank();
    actions_["TrackReceiveBank"] =                  new TrackReceiveBank();
    actions_["TrackFXMenuBank"] =                   new TrackFXMenuBank();
    actions_["SelectedTrackBank"] =                 new SelectedTrackBank();
    actions_["SelectedTrackSendBank"] =             new SelectedTrackSendBank();
    actions_["SelectedTrackReceiveBank"] =          new SelectedTrackReceiveBank();
    actions_["SelectedTrackFXMenuBank"] =           new SelectedTrackFXMenuBank();
    actions_["Shift"] =                             new SetShift();
    actions_["Option"] =                            new SetOption();
    actions_["Control"] =                           new SetControl();
    actions_["Alt"] =                               new SetAlt();
    actions_["Flip"] =                              new SetFlip();
    actions_["Global"] =                            new SetGlobal();
    actions_["Marker"] =                            new SetMarker();
    actions_["Nudge"] =                             new SetNudge();
    actions_["Zoom"] =                              new SetZoom();
    actions_["Scrub"] =                             new SetScrub();
    actions_["ClearModifiers"] =                    new ClearModifiers();
    actions_["ToggleChannel"] =                     new SetToggleChannel();
    actions_["CycleTrackAutoMode"] =                new CycleTrackAutoMode();
    actions_["TrackVolume"] =                       new TrackVolume();
    actions_["SoftTakeover7BitTrackVolume"] =       new SoftTakeover7BitTrackVolume();
    actions_["SoftTakeover14BitTrackVolume"] =      new SoftTakeover14BitTrackVolume();
    actions_["TrackVolumeDB"] =                     new TrackVolumeDB();
    actions_["TrackToggleVCASpill"] =               new TrackToggleVCASpill();
    actions_["TrackVCALeaderDisplay"] =             new TrackVCALeaderDisplay();
    actions_["TrackToggleFolderSpill"] =            new TrackToggleFolderSpill();
    actions_["TrackFolderParentDisplay"] =          new TrackFolderParentDisplay();
    actions_["TrackSelect"] =                       new TrackSelect();
    actions_["TrackUniqueSelect"] =                 new TrackUniqueSelect();
    actions_["TrackRangeSelect"] =                  new TrackRangeSelect();
    actions_["TrackRecordArm"] =                    new TrackRecordArm();
    actions_["TrackMute"] =                         new TrackMute();
    actions_["TrackSolo"] =                         new TrackSolo();
    actions_["ClearAllSolo"] =                      new ClearAllSolo();
    actions_["TrackInvertPolarity"] =               new TrackInvertPolarity();
    actions_["TrackPan"] =                          new TrackPan();
    actions_["TrackPanPercent"] =                   new TrackPanPercent();
    actions_["TrackPanWidth"] =                     new TrackPanWidth();
    actions_["TrackPanWidthPercent"] =              new TrackPanWidthPercent();
    actions_["TrackPanL"] =                         new TrackPanL();
    actions_["TrackPanLPercent"] =                  new TrackPanLPercent();
    actions_["TrackPanR"] =                         new TrackPanR();
    actions_["TrackPanRPercent"] =                  new TrackPanRPercent();
    actions_["TrackPanAutoLeft"] =                  new TrackPanAutoLeft();
    actions_["TrackPanAutoRight"] =                 new TrackPanAutoRight();
    actions_["TrackNameDisplay"] =                  new TrackNameDisplay();
    actions_["TrackNumberDisplay"] =                new TrackNumberDisplay();
    actions_["TrackVolumeDisplay"] =                new TrackVolumeDisplay();
    actions_["TrackPanDisplay"] =                   new TrackPanDisplay();
    actions_["TrackPanWidthDisplay"] =              new TrackPanWidthDisplay();
    actions_["TrackPanLeftDisplay"] =               new TrackPanLeftDisplay();
    actions_["TrackPanRightDisplay"] =              new TrackPanRightDisplay();
    actions_["TrackPanAutoLeftDisplay"] =           new TrackPanAutoLeftDisplay();
    actions_["TrackPanAutoRightDisplay"] =          new TrackPanAutoRightDisplay();
    actions_["TrackOutputMeter"] =                  new TrackOutputMeter();
    actions_["TrackOutputMeterAverageLR"] =         new TrackOutputMeterAverageLR();
    actions_["TrackOutputMeterMaxPeakLR"] =         new TrackOutputMeterMaxPeakLR();
    actions_["FocusedFXParam"] =                    new FocusedFXParam();
    actions_["FXParam"] =                           new FXParam();
    actions_["TCPFXParam"] =                        new TCPFXParam();
    actions_["FXParamRelative"] =                   new FXParamRelative();
    actions_["ToggleFXBypass"] =                    new ToggleFXBypass();
    actions_["FXBypassDisplay"] =                   new FXBypassDisplay();
    actions_["ToggleFXOffline"] =                   new ToggleFXOffline();
    actions_["FXOfflineDisplay"] =                  new FXOfflineDisplay();
    actions_["FXNameDisplay"] =                     new FXNameDisplay();
    actions_["FXMenuNameDisplay"] =                 new FXMenuNameDisplay();
    actions_["SpeakFXMenuName"] =                   new SpeakFXMenuName();
    actions_["FXParamNameDisplay"] =                new FXParamNameDisplay();
    actions_["TCPFXParamNameDisplay"] =             new TCPFXParamNameDisplay();
    actions_["FXParamValueDisplay"] =               new FXParamValueDisplay();
    actions_["TCPFXParamValueDisplay"] =            new TCPFXParamValueDisplay();
    actions_["FocusedFXParamNameDisplay"] =         new FocusedFXParamNameDisplay();
    actions_["FocusedFXParamValueDisplay"] =        new FocusedFXParamValueDisplay();
    actions_["FXGainReductionMeter"] =              new FXGainReductionMeter();
    actions_["TrackSendVolume"] =                   new TrackSendVolume();
    actions_["TrackSendVolumeDB"] =                 new TrackSendVolumeDB();
    actions_["TrackSendPan"] =                      new TrackSendPan();
    actions_["TrackSendPanPercent"] =               new TrackSendPanPercent();
    actions_["TrackSendMute"] =                     new TrackSendMute();
    actions_["TrackSendInvertPolarity"] =           new TrackSendInvertPolarity();
    actions_["TrackSendStereoMonoToggle"] =         new TrackSendStereoMonoToggle();
    actions_["TrackSendPrePost"] =                  new TrackSendPrePost();
    actions_["TrackSendNameDisplay"] =              new TrackSendNameDisplay();
    actions_["SpeakTrackSendDestination"] =         new SpeakTrackSendDestination();
    actions_["TrackSendVolumeDisplay"] =            new TrackSendVolumeDisplay();
    actions_["TrackSendPanDisplay"] =               new TrackSendPanDisplay();
    actions_["TrackSendPrePostDisplay"] =           new TrackSendPrePostDisplay();
    actions_["TrackReceiveVolume"] =                new TrackReceiveVolume();
    actions_["TrackReceiveVolumeDB"] =              new TrackReceiveVolumeDB();
    actions_["TrackReceivePan"] =                   new TrackReceivePan();
    actions_["TrackReceivePanPercent"] =            new TrackReceivePanPercent();
    actions_["TrackReceiveMute"] =                  new TrackReceiveMute();
    actions_["TrackReceiveInvertPolarity"] =        new TrackReceiveInvertPolarity();
    actions_["TrackReceiveStereoMonoToggle"] =      new TrackReceiveStereoMonoToggle();
    actions_["TrackReceivePrePost"] =               new TrackReceivePrePost();
    actions_["TrackReceiveNameDisplay"] =           new TrackReceiveNameDisplay();
    actions_["SpeakTrackReceiveSource"] =           new SpeakTrackReceiveSource();
    actions_["TrackReceiveVolumeDisplay"] =         new TrackReceiveVolumeDisplay();
    actions_["TrackReceivePanDisplay"] =            new TrackReceivePanDisplay();
    actions_["TrackReceivePrePostDisplay"] =        new TrackReceivePrePostDisplay();
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

void Manager::InitFXParamStepValues()
{
    string fxParamStepValuesFilePath = string(DAW::GetResourcePath()) + "/CSI/FXParamStepValuesCache.txt";
    
    filesystem::path fxParamStepValuesFile { fxParamStepValuesFilePath };

    if (filesystem::exists(fxParamStepValuesFile))
    {
        int lineNumber = 0;

        try
        {
            ifstream fxParamStepValues(fxParamStepValuesFile);
                   
            for (string line; getline(fxParamStepValues, line) ; )
            {
                line = regex_replace(line, regex(TabChars), " ");
                line = regex_replace(line, regex(CRLFChars), "");
                
                
                if(line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
                    continue;
                
                vector<string> tokens(GetTokens(line));
                
                if(tokens.size() < 3)
                    continue;
                
                for(int i = 2; i < tokens.size(); i++)
                    fxParamStepValues_[tokens[0]][atoi(tokens[1].c_str())].push_back(stod(tokens[i]));

                lineNumber++;
            }
        }
        catch (exception &e)
        {
            char buffer[250];
            snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", fxParamStepValuesFilePath.c_str(), lineNumber);
            DAW::ShowConsoleMsg(buffer);
        }
    }
}

void Manager::WriteFXParamStepValues()
{
    string fxParamStepValuesFilePath = string(DAW::GetResourcePath()) + "/CSI/FXParamStepValuesCache.txt";
    
    filesystem::path fxParamStepValuesFile { fxParamStepValuesFilePath };

    int lineNumber = 0;

    try
    {
        ofstream fxParamStepValues(fxParamStepValuesFile);
        
        for (auto [fxName, paramValues] : fxParamStepValues_)
        {
            for(auto [paramIndex, values] : paramValues)
            {
                string line = "";
                
                line += "\"" + fxName + "\" " + to_string(paramIndex);
                
                for(auto value : values)
                    line += " " + to_string(value);
                
                fxParamStepValues << line +  GetLineEnding();
                
                lineNumber++;
            }
        }
        
        fxParamStepValues.close();
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", fxParamStepValuesFilePath.c_str(), lineNumber);
        DAW::ShowConsoleMsg(buffer);
    }
}

void Manager::Init()
{
    pages_.clear();
    
    map<string, Midi_ControlSurfaceIO*> midiSurfaces;
    map<string, OSC_ControlSurfaceIO*> oscSurfaces;

    Page* currentPage = nullptr;
    
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
    bool shouldAutoScan = false;
    
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
                    midiSurfaces[tokens[1]] = new Midi_ControlSurfaceIO(tokens[1], GetMidiInputForPort(atoi(tokens[2].c_str())), GetMidiOutputForPort(atoi(tokens[3].c_str())));
                else if(tokens[0] == OSCSurfaceToken && tokens.size() == 5)
                    oscSurfaces[tokens[1]] = new OSC_ControlSurfaceIO(tokens[1], tokens[2], tokens[3], tokens[4]);
                else if(tokens[0] == PageToken)
                {
                    bool followMCP = true;
                    bool synchPages = true;
                    bool isScrollLinkEnabled = false;
                    
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
                            }
                        }
                            
                        currentPage = new Page(tokens[1], followMCP, synchPages, isScrollLinkEnabled);
                        pages_.push_back(currentPage);
                    }
                }
                else
                {
                    if(currentPage && (tokens.size() == 5 || tokens.size() == 6))
                    {
                        bool useLocalModifiers = false;
                        
                        if(tokens[0] == "LocalModifiers")
                        {
                            useLocalModifiers = true;
                            tokens.erase(tokens.begin()); // pop front
                        }
                        
                        ControlSurface* surface = nullptr;
                        
                        if(midiSurfaces.count(tokens[0]) > 0)
                            surface = new Midi_ControlSurface(useLocalModifiers, currentPage, tokens[0], atoi(tokens[1].c_str()), atoi(tokens[2].c_str()), tokens[3], tokens[4], midiSurfaces[tokens[0]]);
                        else if(oscSurfaces.count(tokens[0]) > 0)
                            surface = new OSC_ControlSurface(useLocalModifiers, currentPage, tokens[0], atoi(tokens[1].c_str()), atoi(tokens[2].c_str()), tokens[3], tokens[4], oscSurfaces[tokens[0]]);

                        if(surface != nullptr)
                            currentPage->AddSurface(surface);
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
    InitFXParamStepValues();
    
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
    return manager_->GetTrackFromChannel(channelNum_);
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
ActionContext::ActionContext(Action* action, Widget* widget, shared_ptr<Zone> zone, vector<string> params): action_(action), widget_(widget), zone_(zone)
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
    
    params = nonWidgetPropertyParams;
    
    string actionName = "";
    
    if(params.size() > 0)
        actionName = params[0];
    
    // Action with int param, could include leading minus sign
    if(params.size() > 1 && (isdigit(params[1][0]) ||  params[1][0] == '-'))  // C++ 11 says empty strings can be queried without catastrophe :)
    {
        intParam_= atol(params[1].c_str());
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
        
    if(actionName == "FXParam" && params.size() > 1 && isdigit(params[1][0])) // C++ 11 says empty strings can be queried without catastrophe :)
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
    }
    
    if(params.size() > 1 && (actionName == "Broadcast" || actionName == "Receive" || actionName == "Activate" || actionName == "Deactivate" || actionName == "ToggleActivation"))
    {
        for(int i = 1; i < params.size(); i++)
            zoneNames_.push_back(params[i]);
    }

    if(params.size() > 0)
        SetColor(params, supportsColor_, supportsTrackColor_, colorValues_);
    
    GetSteppedValues(widget, GetZone()->GetName(), paramIndex_, params, deltaValue_, acceleratedDeltaValues_, rangeMinimum_, rangeMaximum_, steppedValues_, acceleratedTickValues_);

    if(acceleratedTickValues_.size() < 1)
        acceleratedTickValues_.push_back(10);
}

Page* ActionContext::GetPage()
{
    return widget_->GetSurface()->GetPage();
}

ControlSurface* ActionContext::GetSurface()
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
        if(steppedValues_.size() > 1)
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
    if(steppedValues_.size() == 0)
        TheManager->GetSteppedValues(GetZone()->GetName(), GetTrack(), GetSlotIndex(), GetParamIndex(), steppedValues_);

    if(steppedValues_.size() > 1)
        DoSteppedValueAction(delta);
    else
        DoRangeBoundAction(action_->GetCurrentNormalizedValue(this) + (deltaValue_ != 0.0 ? (delta > 0 ? deltaValue_ : -deltaValue_) : delta));
}

void ActionContext::DoRelativeAction(int accelerationIndex, double delta)
{
    if(steppedValues_.size() == 0)
        TheManager->GetSteppedValues(GetZone()->GetName(), GetTrack(), GetSlotIndex(), GetParamIndex(), steppedValues_);
    
    if(steppedValues_.size() > 1)
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
int Zone::GetSlotIndex()
{
    if(name_ == "TrackSend")
        return zoneManager_->GetTrackSendOffset();
    if(name_ == "TrackReceive")
        return zoneManager_->GetTrackReceiveOffset();
    if(name_ == "TrackFXMenu")
        return zoneManager_->GetTrackFXMenuOffset();
    if(name_ == "SelectedTrack")
        return slotIndex_ + zoneManager_->GetSelectedTrackOffset();
    if(name_ == "SelectedTrackSend")
        return slotIndex_ + zoneManager_->GetSelectedTrackSendOffset();
    if(name_ == "SelectedTrackReceive")
        return slotIndex_ + zoneManager_->GetSelectedTrackReceiveOffset();
    if(name_ == "SelectedTrackFXMenu")
        return slotIndex_ + zoneManager_->GetSelectedTrackFXMenuOffset();
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

Zone::Zone(ZoneManager* const zoneManager, Navigator* navigator, int slotIndex, string name, string alias, string sourceFilePath, vector<string> includedZones, vector<string> associatedZones): zoneManager_(zoneManager), navigator_(navigator), slotIndex_(slotIndex), name_(name), alias_(alias), sourceFilePath_(sourceFilePath)
{
    if(name == "Home")
    {
        for(auto zoneName : associatedZones)
        {
            if(zoneManager_->GetZoneFilePaths().count(zoneName) > 0)
            {
                vector<Navigator*> navigators;
                AddNavigatorsForZone(zoneName, navigators);

                associatedZones_[zoneName] = vector<shared_ptr<Zone>>();
                
                ProcessZoneFile(zoneManager_->GetZoneFilePaths()[zoneName].filePath, zoneManager_, navigators, associatedZones_[zoneName], nullptr);
            
                // GAW -- all of this to ensure VCA and Folder Zones can have radio button behaviour :)
                
                Widget* onZoneActivation = nullptr;
                Widget* onZoneDeactivation = nullptr;

                if(zoneName == "VCA" || zoneName == "Folder")
                {
                    onZoneActivation = zoneManager->GetSurface()->GetWidgetByName("OnZoneActivation");
                    onZoneDeactivation = zoneManager->GetSurface()->GetWidgetByName("OnZoneDeactivation");
                    
                    for(auto zone : associatedZones_[zoneName])
                    {
                        if(zoneName == "VCA")
                        {
                            if(onZoneActivation != nullptr)
                            {
                                zone->AddWidget(onZoneActivation, onZoneActivation->GetName());
                                shared_ptr<ActionContext> context = TheManager->GetActionContext("VCAModeActivated", onZoneActivation, zone, "");
                                zone->AddActionContext(onZoneActivation, 0, context);
                            }
                            if(onZoneDeactivation != nullptr)
                            {
                                zone->AddWidget(onZoneDeactivation, onZoneDeactivation->GetName());
                                shared_ptr<ActionContext> context = TheManager->GetActionContext("VCAModeDeactivated", onZoneDeactivation, zone, "");
                                zone->AddActionContext(onZoneDeactivation, 0, context);
                            }
                        }
                        else if(zoneName == "Folder")
                        {
                            if(onZoneActivation != nullptr)
                            {
                                zone->AddWidget(onZoneActivation, onZoneActivation->GetName());
                                shared_ptr<ActionContext> context = TheManager->GetActionContext("FolderModeActivated", onZoneActivation, zone, "");
                                zone->AddActionContext(onZoneActivation, 0, context);
                            }
                            if(onZoneDeactivation != nullptr)
                            {
                                zone->AddWidget(onZoneDeactivation, onZoneDeactivation->GetName());
                                shared_ptr<ActionContext> context = TheManager->GetActionContext("FolderModeDeactivated", onZoneDeactivation, zone, "");
                                zone->AddActionContext(onZoneDeactivation, 0, context);
                            }
                        }
                    }
                }
                
                // GAW -- all this to ensure VCA and Folder Zones can have radio button behaviour :)
            }
        }
    }
    
    for(auto zoneName : includedZones)
    {
        if(zoneManager_->GetZoneFilePaths().count(zoneName) > 0)
        {
            vector<Navigator*> navigators;
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
            vector<Navigator*> navigators;
            navigators.push_back(GetNavigator());

            subZones_[zoneName] = vector<shared_ptr<Zone>>();
        
            ProcessZoneFile(zoneManager_->GetZoneFilePaths()[zoneName].filePath, zoneManager_, navigators, subZones_[zoneName], enclosingZone);
        }
    }
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

void Zone::AddNavigatorsForZone(string zoneName, vector<Navigator*> &navigators)
{
    if(zoneName == "MasterTrack")
        navigators.push_back(zoneManager_->GetMasterTrackNavigator());
    else if(zoneName == "Track" || zoneName == "VCA" || zoneName == "Folder" || zoneName == "TrackSend" || zoneName == "TrackReceive" || zoneName == "TrackFXMenu" )
        for(int i = 0; i < zoneManager_->GetNumChannels(); i++)
            navigators.push_back(zoneManager_->GetSurface()->GetPage()->GetNavigatorForChannel(i + zoneManager_->GetSurface()->GetChannelOffset()));
    else if(zoneName == "SelectedTrack" || zoneName == "SelectedTrackSend" || zoneName == "SelectedTrackReceive" || zoneName == "SelectedTrackFXMenu")
        for(int i = 0; i < zoneManager_->GetNumChannels(); i++)
            navigators.push_back(zoneManager_->GetSelectedTrackNavigator());
    else
        navigators.push_back(zoneManager_->GetSelectedTrackNavigator());
}

void Zone::SetXTouchDisplayColors(string color)
{
    for(auto [widget, isUsed] : widgets_)
        widget->SetXTouchDisplayColors(color);
}

void Zone::RestoreXTouchDisplayColors()
{
    for(auto [widget, isUsed] : widgets_)
        widget->RestoreXTouchDisplayColors();
}

void Zone::Activate()
{
    UpdateCurrentActionContextModifiers();
    
    for(auto [widget, isUsed] : widgets_)
        if(widget->GetName() == "OnZoneActivation")
            for(auto context : GetActionContexts(widget))
                context->DoAction(1.0);

    isActive_ = true;
    
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

void Zone::GoTrack()
{
    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            if(zone->GetName() == "VCA" || zone->GetName() == "Folder")
                zone->Deactivate();
}

void Zone::GoVCA()
{
    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            if(zone->GetName() == "Folder")
                zone->Deactivate();
        
    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            if(zone->GetName() == "VCA")
                zone->Activate();
}

void Zone::GoFolder()
{
    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            if(zone->GetName() == "VCA")
                zone->Deactivate();
    
    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            if(zone->GetName() == "Folder")
                zone->Activate();
}

void Zone::OnTrackDeselection()
{
    isActive_ = true;
    
    for(auto zone : includedZones_)
        zone->Activate();
   
    for(auto [key, zones] : associatedZones_)
        if(key == "SelectedTrack" || key == "SelectedTrackSend" || key == "SelectedTrackReceive" || key == "SelectedTrackFXMenu")
            for(auto zone : zones)
                zone->Deactivate();
}

void Zone::Deactivate()
{
    for(auto [widget, isUsed] : widgets_)
        if(widget->GetName() == "OnZoneDeactivation")
            for(auto context : GetActionContexts(widget))
                context->DoAction(1.0);

    isActive_ = false;
    
    for(auto zone : includedZones_)
        zone->Deactivate();

    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            zone->Deactivate();

    for(auto [key, zones] : subZones_)
        for(auto zone : zones)
            zone->Deactivate();
    
    for(auto [widget, isUsed] : widgets_)
        widget->Clear();
}

void Zone::RequestUpdateWidget(Widget* widget)
{
    for(auto context : GetActionContexts(widget))
    {
        context->RunDeferredActions();
        context->RequestUpdate();
    }
}

void Zone::RequestUpdate(map<Widget*, bool> &usedWidgets)
{
    if(! isActive_)
        return;
  
    for(auto [key, zones] : subZones_)
        for(auto zone : zones)
            zone->RequestUpdate(usedWidgets);
    
    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            zone->RequestUpdate(usedWidgets);

    for(auto zone : includedZones_)
        zone->RequestUpdate(usedWidgets);
    
    for(auto [widget, value] : GetWidgets())
    {
        if(usedWidgets[widget] == false)
        {
            usedWidgets[widget] = true;
            RequestUpdateWidget(widget);
        }
    }
}

void Zone::DoAction(Widget* widget, bool &isUsed, double value)
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
   
void Zone::DoRelativeAction(Widget* widget, bool &isUsed, double delta)
{
    if(! isActive_ || isUsed)
        return;
    
    for(auto [key, zones] : subZones_)
        for(auto zone : zones)
            zone->DoRelativeAction(widget, isUsed, delta);

    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            zone->DoRelativeAction(widget, isUsed, delta);

    if(isUsed)
        return;

    if(widgets_.count(widget) > 0)
    {
        isUsed = true;

        for(auto context : GetActionContexts(widget))
            context->DoRelativeAction(delta);
    }
    else
    {
        for(auto zone : includedZones_)
            zone->DoRelativeAction(widget, isUsed, delta);
    }
}

void Zone::DoRelativeAction(Widget* widget, bool &isUsed, int accelerationIndex, double delta)
{
    if(! isActive_ || isUsed)
        return;

    for(auto [key, zones] : subZones_)
        for(auto zone : zones)
            zone->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
    
    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            zone->DoRelativeAction(widget, isUsed, accelerationIndex, delta);

    if(isUsed)
        return;

    if(widgets_.count(widget) > 0)
    {
        isUsed = true;

        for(auto context : GetActionContexts(widget))
            context->DoRelativeAction(accelerationIndex, delta);
    }
    else
    {
        for(auto zone : includedZones_)
            zone->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
    }
}

void Zone::DoTouch(Widget* widget, string widgetName, bool &isUsed, double value)
{
    if(! isActive_ || isUsed)
        return;

    for(auto [key, zones] : subZones_)
        for(auto zone : zones)
            zone->DoTouch(widget, widgetName, isUsed, value);
    
    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            zone->DoTouch(widget, widgetName, isUsed, value);

    if(isUsed)
        return;

    if(widgets_.count(widget) > 0)
    {
        isUsed = true;

        for(auto context : GetActionContexts(widget))
            context->DoTouch(value);
    }
    else
    {
        for(auto zone : includedZones_)
            zone->DoTouch(widget, widgetName, isUsed, value);
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

void Zone::UpdateCurrentActionContextModifier(Widget* widget)
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

vector<shared_ptr<ActionContext>> &Zone::GetActionContexts(Widget* widget)
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
Widget::~Widget()
{
    for(auto feedbackProcessor : feedbackProcessors_)
    {
        delete feedbackProcessor;
        feedbackProcessor = nullptr;
    }
}

ZoneManager* Widget::GetZoneManager()
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

void Widget::SetXTouchDisplayColors(string color)
{
    for(auto processor : feedbackProcessors_)
        processor->SetXTouchDisplayColors(color);
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CSIMessageGenerator::CSIMessageGenerator(Widget* widget, string message) : widget_(widget)
{
    widget->GetSurface()->AddCSIMessageGenerator(message, this);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi_FeedbackProcessor
////////////////////////////////////////////////////////////////////////////////////////////////////////
void Midi_FeedbackProcessor::SendMidiMessage(MIDI_event_ex_t* midiMessage)
{
    surface_->SendMidiMessage(midiMessage);
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
        
    vector<Navigator*> navigators;
    navigators.push_back(GetSelectedTrackNavigator());
    vector<shared_ptr<Zone>> dummy; // Needed to satify protcol, Home and FocusedFXParam have special Zone handling
    ProcessZoneFile(zoneFilePaths_["Home"].filePath, this, navigators, dummy, nullptr);
    if(zoneFilePaths_.count("FocusedFXParam") > 0)
        ProcessZoneFile(zoneFilePaths_["FocusedFXParam"].filePath, this, navigators, dummy, nullptr);
    GoHome();
}

void ZoneManager::UpdateCurrentActionContextModifiers()
{
    if(focusedFXParamZone_ != nullptr)
        focusedFXParamZone_->UpdateCurrentActionContextModifiers();
    
    for(auto zone : focusedFXZones_)
        zone->UpdateCurrentActionContextModifiers();
    
    for(auto zone : selectedTrackFXZones_)
        zone->UpdateCurrentActionContextModifiers();
    
    for(auto zone : fxSlotZones_)
        zone->UpdateCurrentActionContextModifiers();
    
    if(homeZone_ != nullptr)
        homeZone_->UpdateCurrentActionContextModifiers();
}

void ZoneManager::RequestUpdate()
{
    CheckFocusedFXState();
        
    for(auto &[key, value] : usedWidgets_)
        value = false;
    
    if(focusedFXParamZone_ != nullptr && isFocusedFXParamMappingEnabled_)
        focusedFXParamZone_->RequestUpdate(usedWidgets_);

    for(auto zone : focusedFXZones_)
        zone->RequestUpdate(usedWidgets_);
    
    for(auto zone : selectedTrackFXZones_)
        zone->RequestUpdate(usedWidgets_);
    
    for(auto zone : fxSlotZones_)
        zone->RequestUpdate(usedWidgets_);
    
    if(homeZone_ != nullptr)
        homeZone_->RequestUpdate(usedWidgets_);
    
    // default is to zero unused Widgets -- for an opposite sense device, you can override this by supplying an inverted NoAction context in the Home Zone
    map<string, string> properties;
    
    for(auto &[key, value] : usedWidgets_)
    {
        if(value == false)
        {
            rgba_color color;
            key->UpdateValue(properties, 0.0);
            key->UpdateValue(properties, "");
            key->UpdateColorValue(color);
        }
    }
}

void ZoneManager::GoFocusedFX()
{
    focusedFXZones_.clear();
    
    int trackNumber = 0;
    int itemNumber = 0;
    int fxSlot = 0;
    MediaTrack* focusedTrack = nullptr;
    
    if(DAW::GetFocusedFX2(&trackNumber, &itemNumber, &fxSlot) == 1)
        if(trackNumber > 0)
            focusedTrack = DAW::GetTrack(trackNumber);
    
    if(focusedTrack)
    {
        char FXName[BUFSZ];
        DAW::TrackFX_GetFXName(focusedTrack, fxSlot, FXName, sizeof(FXName));
        
        if(zoneFilePaths_.count(FXName) > 0)
        {
            vector<Navigator*> navigators;
            navigators.push_back(GetSurface()->GetPage()->GetFocusedFXNavigator());
            
            ProcessZoneFile(zoneFilePaths_[FXName].filePath, this, navigators, focusedFXZones_, nullptr);
            
            for(auto zone :focusedFXZones_)
            {
                zone->SetSlotIndex(fxSlot);
                zone->Activate();
            }
        }
    }
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
                vector<Navigator*> navigators;
                navigators.push_back(GetSurface()->GetPage()->GetSelectedTrackNavigator());
                
                ProcessZoneFile(zoneFilePaths_[FXName].filePath, this, navigators, selectedTrackFXZones_, nullptr);
                
                selectedTrackFXZones_.back()->SetSlotIndex(i);
                selectedTrackFXZones_.back()->Activate();
            }
        }
    }
}

void ZoneManager::GoTrackFXSlot(MediaTrack* track, Navigator* navigator, int fxSlot)
{
    if((navigator->GetName() == "TrackNavigator" && broadcast_.count("TrackFXMenu") > 0) ||
       (navigator->GetName() == "SelectedTrackNavigator" && broadcast_.count("SelectedTrackFXMenu")) > 0)
        GetSurface()->GetPage()->SignalGoTrackFXSlot(GetSurface(), track, navigator, fxSlot);
    
    ActivateTrackFXSlot(track, navigator, fxSlot);
}

void ZoneManager::ActivateTrackFXSlot(MediaTrack* track, Navigator* navigator, int fxSlot)
{
    char FXName[BUFSZ];
    
    DAW::TrackFX_GetFXName(track, fxSlot, FXName, sizeof(FXName));
    
    if(zoneFilePaths_.count(FXName) > 0)
    {
        vector<Navigator*> navigators;
        navigators.push_back(navigator);
        
        ProcessZoneFile(zoneFilePaths_[FXName].filePath, this, navigators, fxSlotZones_, nullptr);
        
        fxSlotZones_.back()->SetSlotIndex(fxSlot);
        fxSlotZones_.back()->Activate();
    }
}

int numFXZones = 0;

void ZoneManager::PreProcessZones()
{
    vector<string> zoneFilesToProcess;
    listZoneFiles(DAW::GetResourcePath() + string("/CSI/Zones/") + zoneFolder_ + "/", zoneFilesToProcess); // recursively find all .zon files, starting at zoneFolder
       
    if(zoneFilesToProcess.size() == 0)
    {
        MessageBox(g_hwnd, (string("Please check your installation, cannot find Zone files in ") + DAW::GetResourcePath() + string("/CSI/Zones/") + zoneFolder_).c_str(), (GetSurface()->GetName() + " Zone folder is missing or empty").c_str(), MB_OK);

        return;
    }
       
    for(auto zoneFilename : zoneFilesToProcess)
        PreProcessZoneFile(zoneFilename, this);
}

void ZoneManager::EnsureZoneAvailable(string fxName, MediaTrack* track, int fxIndex)
{
    if(zoneFilePaths_.count(fxName) > 0)
        return;

    string path = DAW::GetResourcePath() + string("/CSI/Zones/") + zoneFolder_ + "/AutoGeneratedFXZones";
    
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
        "VST: TDR ",
        "VST: UAD UA ",
        "VST: UAD Pultec ",
        "VST: UAD Tube-Tech ",
        "VST: UAD Softube ",
        "VST: UAD Teletronix ",
        "VST: UAD ",
        "VST3: UADx ",
        "VST: ",
        "VST3: ",
        "JS: ",
    };
    
    string alias = "";
    
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

    ControlSurface* surface = GetSurface();
    
    string fxRotaries = "";
    string baseRotary = "";
    string baseDisplayUpper = "";
    string baseDisplayLower = "";

    if(surface->GetWidgetByName("Rotary1") || surface->GetWidgetByName("DisplayUpper1") || surface->GetWidgetByName("DisplayLower1"))
    {
        fxRotaries = "\tFXRotaries";
        baseRotary = "Rotary";
        baseDisplayUpper = "DisplayUpper";
        baseDisplayLower = "DisplayLower";
    }
    else if(surface->GetWidgetByName("RotaryA1") || surface->GetWidgetByName("DisplayUpperA1") || surface->GetWidgetByName("DisplayLowerA1"))
    {
        fxRotaries = "\tFXRotariesA";
        baseRotary = "RotaryA";
        baseDisplayUpper = "DisplayUpperA";
        baseDisplayLower = "DisplayLowerA";
    }
    else if(surface->GetWidgetByName("RotaryB1") || surface->GetWidgetByName("DisplayUpperB1") || surface->GetWidgetByName("DisplayLowerB1"))
    {
        fxRotaries = "\tFXRotariesB";
        baseRotary = "RotaryB";
        baseDisplayUpper = "DisplayUpperB";
        baseDisplayLower = "DisplayLowerB";
    }
    else if(surface->GetWidgetByName("RotaryC1") || surface->GetWidgetByName("DisplayUpperC1") || surface->GetWidgetByName("DisplayLowerC1"))
    {
        fxRotaries = "\tFXRotariesC";
        baseRotary = "RotaryC";
        baseDisplayUpper = "DisplayUpperC";
        baseDisplayLower = "DisplayLowerC";
    }
    else if(surface->GetWidgetByName("RotaryD1") || surface->GetWidgetByName("DisplayUpperD1") || surface->GetWidgetByName("DisplayLowerD1"))
    {
        fxRotaries = "\tFXRotariesD";
        baseRotary = "RotaryD";
        baseDisplayUpper = "DisplayUpperD";
        baseDisplayLower = "DisplayLowerD";
    }

    AddZoneFilePath(fxName, info);
    
    ofstream fxZone(path);

    if(fxZone.is_open())
    {
        fxZone << "Zone \"" + fxName + "\" \"" + alias + "\"" + GetLineEnding();
        
        for(int i = 0; i < DAW::TrackFX_GetNumParams(track, fxIndex); i++)
        {
            string rotary = baseRotary + to_string(i + 1);
            string nameDisplay = baseDisplayUpper + to_string(i + 1);
            string valueDisplay = baseDisplayLower + to_string(i + 1);
            
            if(surface->GetWidgetByName(rotary) != nullptr || surface->GetWidgetByName(nameDisplay) != nullptr || surface->GetWidgetByName(valueDisplay) != nullptr)
                fxRotaries += " " + to_string(i);
        }
        
        fxZone << fxRotaries + GetLineEnding();
        
        fxZone << "ZoneEnd" + GetLineEnding();
        
        fxZone.close();
    }
}

void ZoneManager::SaveFXMapTemplateRow(string rowType)
{
    if(homeZone_ != nullptr && surface_->GetPage()->GetSelectedTrack() != nullptr && DAW::TrackFX_GetCount(surface_->GetPage()->GetSelectedTrack()) == 1)
    {
        MediaTrack* track = surface_->GetPage()->GetSelectedTrack();
        
        int fxIndex = 0;
        int paramIndex = 0;
        
        if(DAW::GetTCPFXParm(track, 0, &fxIndex, &paramIndex))
        {
            char fxName[BUFSZ];
            DAW::TrackFX_GetNamedConfigParm(track, fxIndex, "fx_name", fxName, sizeof(fxName));
            
            TCPFXZoneRows_[fxName][rowType].indices += rowType;
              
            for(int i = 0; i < DAW::CountTCPFXParms(track); i++ )
            {
                if(DAW::GetTCPFXParm(track, i, &fxIndex, &paramIndex))
                {
                    TCPFXParamInfo info;
                    
                    TCPFXZoneRows_[fxName][rowType].indices += " " + to_string(paramIndex);
                    
                    char fxParamAlias[BUFSZ];
                    DAW::TrackFX_GetParamName(track, fxIndex, paramIndex, fxParamAlias, sizeof(fxParamAlias));
                    TCPFXZoneRows_[fxName][rowType].aliases += " \"" + string(fxParamAlias) + "\"";
                }
            }
        }
    }
}

void ZoneManager::BuildSelectedTrackTCPFXZone()
{
    if(homeZone_ != nullptr && surface_->GetPage()->GetSelectedTrack() != nullptr && DAW::TrackFX_GetCount(surface_->GetPage()->GetSelectedTrack()) == 1)
    {
        if(TCPFXZoneRows_.size() == 0)
        {
            MessageBox(g_hwnd, "Please make sure you have at least one row defined and saved", "No Rows", MB_OK);
            return;
        }
    
        MediaTrack* track = surface_->GetPage()->GetSelectedTrack();
        
        int fxIndex = 0;

        char fxAlias[BUFSZ];
        DAW::TrackFX_GetFXName(track, fxIndex, fxAlias, sizeof(fxAlias));
        
        char fxName[BUFSZ];
        DAW::TrackFX_GetNamedConfigParm(track, fxIndex, "fx_name", fxName, sizeof(fxName));
        
        string path = DAW::GetResourcePath() + string("/CSI/Zones/") + zoneFolder_ + "/TCPFXGeneratedZones";
        
        if(! filesystem::exists(path) || ! filesystem::is_directory(path))
            filesystem::create_directory(path);
        
        path += "/" + regex_replace(fxName, regex(BadFileChars), "_") + ".zon";

        CSIZoneInfo info;
        info.filePath = path;
        info.alias = fxAlias;

        AddZoneFilePath(fxName, info);
        
        ofstream fxZone(path);

        if(fxZone.is_open())
        {
            fxZone << "Zone \"" + string(fxName) + "\" \"" + string(fxAlias) + "\"" + GetLineEnding();
            
            for(auto [fxType, row] : TCPFXZoneRows_[fxName])
            {
                fxZone << GetLineEnding() + row.indices + GetLineEnding();
                fxZone << row.aliases + GetLineEnding() + GetLineEnding();;
            }
            
            fxZone << "ZoneEnd" + GetLineEnding();
            
            fxZone.close();
        }
        
        TCPFXZoneRows_.clear();
    }
}

void ZoneManager::HandleActivation(string zoneName)
{
    if(receive_.count(zoneName) > 0  && homeZone_ != nullptr)
    {
        ClearFXMapping();
        ResetOffsets();
        
        if(zoneName == "Home")
            homeZone_->Activate();
        else
            homeZone_->GoAssociatedZone(zoneName);
    }
}

void ZoneManager::GoAssociatedZone(string associatedZoneName)
{
    if(homeZone_ != nullptr)
    {
        if(broadcast_.count(associatedZoneName) > 0)
            GetSurface()->GetPage()->SignalActivation(GetSurface(), associatedZoneName);
        
        ClearFXMapping();
        ResetOffsets();
        
        homeZone_->GoAssociatedZone(associatedZoneName);
    }
}

void ZoneManager::GoHome()
{   
    string zoneName = "Home";
    
    if(broadcast_.count(zoneName) > 0)
        GetSurface()->GetPage()->SignalActivation(GetSurface(), zoneName);
    
    ClearFXMapping();

    if(homeZone_ != nullptr)
    {
        ResetOffsets();
        homeZone_->Activate();
    }
}

void ZoneManager::OnTrackSelection()
{
    fxSlotZones_.clear();
}

void ZoneManager::OnTrackDeselection()
{
    if(homeZone_ != nullptr)
    {
        ResetSelectedTrackOffsets();
        
        selectedTrackFXZones_.clear();
        
        homeZone_->OnTrackDeselection();
    }
}

void ZoneManager::ToggleEnableFocusedFXMapping()
{
    if(broadcast_.count("ToggleEnableFocusedFXMapping") > 0)
        GetSurface()->GetPage()->SignalToggleEnableFocusedFXMapping(GetSurface());
    
    ToggleEnableFocusedFXMappingImpl();
}

void ZoneManager::AdjustTrackSendBank(int amount)
{
    if(broadcast_.count("TrackSend") > 0)
        GetSurface()->GetPage()->SignalTrackSendBank(GetSurface(), amount);
    
    AdjustTrackSendOffset(amount);
}

void ZoneManager::AdjustTrackReceiveBank(int amount)
{
    if(broadcast_.count("TrackReceive") > 0)
        GetSurface()->GetPage()->SignalTrackReceiveBank(GetSurface(), amount);
    
    AdjustTrackReceiveOffset(amount);
}

void ZoneManager::AdjustTrackFXMenuBank(int amount)
{
    if(broadcast_.count("TrackFXMenu") > 0)
        GetSurface()->GetPage()->SignalTrackFXMenuBank(GetSurface(), amount);
    
    AdjustTrackFXMenuOffset(amount);
}

void ZoneManager::AdjustSelectedTrackSendBank(int amount)
{
    if(broadcast_.count("SelectedTrackSend") > 0)
        GetSurface()->GetPage()->SignalSelectedTrackSendBank(GetSurface(), amount);
    
    AdjustSelectedTrackSendOffset(amount);
}

void ZoneManager::AdjustSelectedTrackReceiveBank(int amount)
{
    if(broadcast_.count("SelectedTrackReceive") > 0)
        GetSurface()->GetPage()->SignalSelectedTrackReceiveBank(GetSurface(), amount);
    
    AdjustTrackReceiveOffset(amount);
}

void ZoneManager::AdjustSelectedTrackFXMenuBank(int amount)
{
    if(broadcast_.count("SelectedTrackFXMenu") > 0)
        GetSurface()->GetPage()->SignalSelectedTrackFXMenuBank(GetSurface(), amount);
    
    AdjustSelectedTrackFXMenuOffset(amount);
}

void ZoneManager::DoTouch(Widget* widget, double value)
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

Navigator* ZoneManager::GetMasterTrackNavigator() { return surface_->GetPage()->GetMasterTrackNavigator(); }
Navigator* ZoneManager::GetSelectedTrackNavigator() { return surface_->GetPage()->GetSelectedTrackNavigator(); }
Navigator* ZoneManager::GetFocusedFXNavigator() { return surface_->GetPage()->GetFocusedFXNavigator(); }
Navigator* ZoneManager::GetDefaultNavigator() { return surface_->GetPage()->GetDefaultNavigator(); }
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

vector<int> ControlSurface::GetModifiers()
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
void Midi_ControlSurface::Initialize(string templateFilename, string zoneFolder)
{
    ProcessWidgetFile(string(DAW::GetResourcePath()) + "/CSI/Surfaces/Midi/" + templateFilename, this);
    InitHardwiredWidgets();
    InitializeMeters();
    zoneManager_->Initialize();
    GetPage()->ForceRefreshTimeDisplay();
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

void Midi_ControlSurface::SendMidiMessage(MIDI_event_ex_t* midiMessage)
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
        oscpkt::UdpSocket* inSocket = GetInputSocketForPort(surfaceName, stoi(receiveOnPort));;

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
void OSC_ControlSurface::Initialize(string templateFilename, string zoneFolder)
{
    ProcessWidgetFile(string(DAW::GetResourcePath()) + "/CSI/Surfaces/OSC/" + templateFilename, this);
    InitHardwiredWidgets();
    zoneManager_->Initialize();
    GetPage()->ForceRefreshTimeDisplay();
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
        
        SendMidiMessage(&midiSysExData.evt);
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
        
        SendMidiMessage(&midiSysExData.evt);
    }

}

