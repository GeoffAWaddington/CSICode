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
    string actionName;
    vector<string> params;
    vector<vector<string>> properties;
    bool isFeedbackInverted;
    double holdDelayAmount;
    
    ActionTemplate(string action, vector<string> prams, bool isInverted, double amount) : actionName(action), params(prams), isFeedbackInverted(isInverted), holdDelayAmount(amount) {}
};

static void listZoneFiles(const string &path, vector<string> &results)
{
    filesystem::path zonePath { path };
    
    if(filesystem::exists(path) && filesystem::is_directory(path))
        for(auto& file : filesystem::recursive_directory_iterator(path))
            if(file.path().extension() == ".zon")
                results.push_back(file.path().string());
}

static void GetWidgetNameAndProperties(string line, string &widgetName, string &modifier, string &touchId, bool &isFeedbackInverted, double &holdDelayAmount, bool &isProperty)
{
    istringstream modified_role(line);
    vector<string> modifier_tokens;
    vector<string> modifierSlots = { "", "", "", "", "", ""};
    string modifier_token;
    
    while (getline(modified_role, modifier_token, '+'))
        modifier_tokens.push_back(modifier_token);
    
    if(modifier_tokens.size() > 1)
    {
        for(int i = 0; i < modifier_tokens.size() - 1; i++)
        {
            if(modifier_tokens[i].find("Touch") != string::npos)
            {
                touchId = modifier_tokens[i];
                modifierSlots[0] = modifier_tokens[i] + "+";
            }
            else if(modifier_tokens[i] == ShiftToken)
                modifierSlots[1] = ShiftToken + "+";
            else if(modifier_tokens[i] == OptionToken)
                modifierSlots[2] = OptionToken + "+";
            else if(modifier_tokens[i] == ControlToken)
                modifierSlots[3] = ControlToken + "+";
            else if(modifier_tokens[i] == AltToken)
                modifierSlots[4] = AltToken + "+";
            else if(modifier_tokens[i] == FlipToken)
                modifierSlots[5] = FlipToken + "+";

            else if(modifier_tokens[i] == "InvertFB")
                isFeedbackInverted = true;
            else if(modifier_tokens[i] == "Hold")
                holdDelayAmount = 1.0;
            else if(modifier_tokens[i] == "Property")
                isProperty = true;
        }
    }

    widgetName = modifier_tokens[modifier_tokens.size() - 1];
    
    modifier = modifierSlots[0] + modifierSlots[1] + modifierSlots[2] + modifierSlots[3] + modifierSlots[4] + modifierSlots[5];
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

static void ProcessZoneFile(string filePath, ZoneManager* zoneManager, vector<Navigator*> &navigators, vector<shared_ptr<Zone>> &zones, shared_ptr<Zone> enclosingZone)
{
    bool isInIncludedZonesSection = false;
    vector<string> includedZones;
    bool isInSubZonesSection = false;
    vector<string> subZones;
    bool isInAssociatedZonesSection = false;
    vector<string> associatedZones;
    map<string, string> touchIds;
    
    map<string, map<string, vector<shared_ptr<ActionTemplate>>>> widgetActions;
    
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
                        
                        if(navigators.size() > 1)
                        {
                            for(auto [key, value] : touchIds)
                            {
                                string expandedKey = regex_replace(key, regex("[|]"), numStr);
                                string expandedValue = regex_replace(value, regex("[|]"), numStr);

                                expandedTouchIds[expandedKey] = expandedValue;
                            }
                        }
                        else
                        {
                            expandedTouchIds = touchIds;
                        }
                        
                        shared_ptr<Zone> zone;
                        
                        if(enclosingZone == nullptr)
                            zone = make_shared<Zone>(zoneManager, navigators[i], i, expandedTouchIds, zoneName, zoneAlias, filePath, includedZones, associatedZones);
                        else
                            zone = make_shared<SubZone>(zoneManager, navigators[i], i, expandedTouchIds, zoneName, zoneAlias, filePath, includedZones, associatedZones, enclosingZone);
                        
                        if(subZones.size() > 0)
                            zone->InitSubZones(subZones, zone);
                        
                        if(zoneName == "Home")
                            zoneManager->SetHomeZone(zone);
                        
                        if(zoneName == "FocusedFXParam")
                            zoneManager->SetFocusedFXParamZone(zone);
                        
                        zones.push_back(zone);
                        
                        for(auto [widgetName, modifierActions] : widgetActions)
                        {
                            string surfaceWidgetName = widgetName;
                            
                            if(navigators.size() > 1)
                                surfaceWidgetName = regex_replace(surfaceWidgetName, regex("[|]"), to_string(i + 1));
                            
                            Widget* widget = zoneManager->GetSurface()->GetWidgetByName(surfaceWidgetName);
                            
                            if(widget == nullptr)
                                continue;
                            
                            if(actionName == ShiftToken || actionName == OptionToken || actionName == ControlToken || actionName == AltToken)
                                widget->SetIsModifier();
                            
                            zone->AddWidget(widget);
                            
                            for(auto [modifier, actions] : modifierActions)
                            {
                                for(auto action : actions)
                                {
                                    string actionName = regex_replace(action->actionName, regex("[|]"), numStr);
                                    vector<string> memberParams;
                                    for(int j = 0; j < action->params.size(); j++)
                                        memberParams.push_back(regex_replace(action->params[j], regex("[|]"), numStr));
                                    
                                    shared_ptr<ActionContext> context = TheManager->GetActionContext(actionName, widget, zone, memberParams, action->properties);
                                                                        
                                    if(action->isFeedbackInverted)
                                        context->SetIsFeedbackInverted();
                                    
                                    if(action->holdDelayAmount != 0.0)
                                        context->SetHoldDelayAmount(action->holdDelayAmount);
                                    
                                    string expandedModifier = regex_replace(modifier, regex("[|]"), numStr);
                                    
                                    zone->AddActionContext(widget, expandedModifier, context);
                                    
                                }
                            }
                        }
                    }
                                    
                    includedZones.clear();
                    subZones.clear();
                    associatedZones.clear();
                    widgetActions.clear();
                    touchIds.clear();
                    
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
                {
                    actionName = tokens[1];
                    
                    string widgetName = "";
                    string modifier = "";
                    string touchId = "";
                    bool isFeedbackInverted = false;
                    double holdDelayAmount = 0.0;
                    bool isProperty = false;
                    
                    GetWidgetNameAndProperties(tokens[0], widgetName, modifier, touchId, isFeedbackInverted, holdDelayAmount, isProperty);
                    
                    if(touchId != "")
                        touchIds[widgetName] = touchId;
                    
                    vector<string> params;
                    for(int i = 1; i < tokens.size(); i++)
                        params.push_back(tokens[i]);
                    
                    if(isProperty)
                    {
                        if(currentActionTemplate != nullptr)
                            currentActionTemplate->properties.push_back(params);
                    }
                    else
                    {
                        currentActionTemplate = make_shared<ActionTemplate>(actionName, params, isFeedbackInverted, holdDelayAmount);
                        widgetActions[widgetName][modifier].push_back(currentActionTemplate);
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

void SetRGB(vector<string> params, bool &supportsRGB, bool &supportsTrackColor, vector<rgb_color> &RGBValues)
{
    vector<int> rawValues;
    
    auto openCurlyBrace = find(params.begin(), params.end(), "{");
    auto closeCurlyBrace = find(params.begin(), params.end(), "}");
    
    if(openCurlyBrace != params.end() && closeCurlyBrace != params.end())
    {
        for(auto it = openCurlyBrace + 1; it != closeCurlyBrace; ++it)
        {
            string strVal = *(it);
            
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
        
        if(rawValues.size() % 3 == 0 && rawValues.size() > 2)
        {
            supportsRGB = true;
            
            for(int i = 0; i < rawValues.size(); i += 3)
            {
                rgb_color color;
                
                color.r = rawValues[i];
                color.g = rawValues[i + 1];
                color.b = rawValues[i + 2];
                
                RGBValues.push_back(color);
            }
        }
    }
}

void SetSteppedValues(vector<string> params, double &deltaValue, vector<double> &acceleratedDeltaValues, double &rangeMinimum, double &rangeMaximum, vector<double> &steppedValues, vector<int> &acceleratedTickValues)
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
}

//////////////////////////////////////////////////////////////////////////////
// Widgets
//////////////////////////////////////////////////////////////////////////////
static int strToHex(string valueStr)
{
    return strtol(valueStr.c_str(), nullptr, 16);
}

static void ProcessMidiWidget(int &lineNumber, ifstream &surfaceTemplateFile, vector<string> tokens, Midi_ControlSurface* surface)
{
    if(tokens.size() < 2)
        return;
    
    string widgetName = tokens[1];

    Widget* widget = new Widget(surface, widgetName);
    
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
        
        string widgetClass = tokenLines[i][0];

        // Control Signal Generators
        if(widgetClass == "AnyPress" && (size == 4 || size == 7))
            new AnyPress_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        if(widgetClass == "Press" && size == 4)
            new PressRelease_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetClass == "Press" && size == 7)
            new PressRelease_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), new MIDI_event_ex_t(strToHex(tokenLines[i][4]), strToHex(tokenLines[i][5]), strToHex(tokenLines[i][6])));
        else if(widgetClass == "Fader14Bit" && size == 4)
            new Fader14Bit_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetClass == "Fader7Bit" && size== 4)
            new Fader7Bit_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetClass == "Encoder" && size == 4)
            new Encoder_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetClass == "Encoder" && size > 4)
            new AcceleratedEncoder_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), tokenLines[i]);
        else if(widgetClass == "MFTEncoder" && size > 4)
            new MFT_AcceleratedEncoder_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), tokenLines[i]);
        else if(widgetClass == "EncoderPlain" && size == 4)
            new EncoderPlain_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        else if(widgetClass == "Touch" && size == 7)
            new Touch_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), new MIDI_event_ex_t(strToHex(tokenLines[i][4]), strToHex(tokenLines[i][5]), strToHex(tokenLines[i][6])));
        // Feedback Processors
        FeedbackProcessor* feedbackProcessor = nullptr;

        if(widgetClass == "FB_TwoState" && size == 7)
        {
            feedbackProcessor = new TwoState_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])), new MIDI_event_ex_t(strToHex(tokenLines[i][4]), strToHex(tokenLines[i][5]), strToHex(tokenLines[i][6])));
        }
        else if(widgetClass == "FB_NovationLaunchpadMiniRGB7Bit" && size == 4)
        {
            feedbackProcessor = new NovationLaunchpadMiniRGB7Bit_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_MFT_RGB" && size == 4)
        {
            feedbackProcessor = new MFT_RGB_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_FaderportRGB7Bit" && size == 4)
        {
            feedbackProcessor = new FaderportRGB7Bit_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_Fader14Bit" && size == 4)
        {
            feedbackProcessor = new Fader14Bit_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_Fader7Bit" && size == 4)
        {
            feedbackProcessor = new Fader7Bit_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_Encoder" && size == 4)
        {
            feedbackProcessor = new Encoder_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_VUMeter" && size == 4)
        {
            feedbackProcessor = new VUMeter_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_GainReductionMeter" && size == 4)
        {
            feedbackProcessor = new GainReductionMeter_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_MCUTimeDisplay" && size == 1)
        {
            feedbackProcessor = new MCU_TimeDisplay_Midi_FeedbackProcessor(surface, widget);
        }
        else if(widgetClass == "FB_QConProXMasterVUMeter" && size == 2)
        {
            feedbackProcessor = new QConProXMasterVUMeter_Midi_FeedbackProcessor(surface, widget, stoi(tokenLines[i][1]));
        }
        else if((widgetClass == "FB_MCUVUMeter" || widgetClass == "FB_MCUXTVUMeter") && size == 2)
        {
            int displayType = widgetClass == "FB_MCUVUMeter" ? 0x14 : 0x15;
            
            feedbackProcessor = new MCUVUMeter_Midi_FeedbackProcessor(surface, widget, displayType, stoi(tokenLines[i][1]));
            
            surface->SetHasMCUMeters(displayType);
        }
        else if(widgetClass == "FB_SCE24_Text" && size == 3)
        {
            feedbackProcessor = new SCE24_Text_Midi_FeedbackProcessor(surface, widget, stoi(tokenLines[i][1]), stoi(tokenLines[i][2]));
        }
        else if(widgetClass == "FB_SCE24_Bar" && size == 3)
        {
            feedbackProcessor = new SCE24_Bar_Midi_FeedbackProcessor(surface, widget, stoi(tokenLines[i][1]), stoi(tokenLines[i][2]));
        }
        else if(widgetClass == "FB_SCE24_OLEDButton" && size == 3)
        {
            feedbackProcessor = new SCE24_OLEDButton_Midi_FeedbackProcessor(surface, widget, strToHex(tokenLines[i][1]), stoi(tokenLines[i][2]));
        }
        else if(widgetClass == "FB_SCE24_LEDButton" && size == 2)
        {
            feedbackProcessor = new SCE24_LEDButton_Midi_FeedbackProcessor(surface, widget, strToHex(tokenLines[i][1]));
        }
        else if(widgetClass == "FB_SCE24_Background" && size == 2)
        {
            feedbackProcessor = new SCE24_Background_Midi_FeedbackProcessor(surface, widget, strToHex(tokenLines[i][1]));
        }
        else if(widgetClass == "FB_SCE24_Ring" && size == 2)
        {
            feedbackProcessor = new SCE24_Ring_Midi_FeedbackProcessor(surface, widget, stoi(tokenLines[i][1]));
        }
        else if((widgetClass == "FB_MCUDisplayUpper" || widgetClass == "FB_MCUDisplayLower" || widgetClass == "FB_MCUXTDisplayUpper" || widgetClass == "FB_MCUXTDisplayLower") && size == 2)
        {
            if(widgetClass == "FB_MCUDisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_MCUDisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_MCUXTDisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x15, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_MCUXTDisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x15, 0x12, stoi(tokenLines[i][1]));
        }
        
        else if((widgetClass == "FB_C4DisplayUpper" || widgetClass == "FB_C4DisplayLower") && size == 3)
        {
            if(widgetClass == "FB_C4DisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x17, stoi(tokenLines[i][1]) + 0x30, stoi(tokenLines[i][2]));
            else if(widgetClass == "FB_C4DisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x17, stoi(tokenLines[i][1]) + 0x30, stoi(tokenLines[i][2]));
        }
        
        else if((widgetClass == "FB_FP8Display" || widgetClass == "FB_FP16Display"
                 || widgetClass == "FB_FP8DisplayUpper" || widgetClass == "FB_FP16DisplayUpper"
                 || widgetClass == "FB_FP8DisplayUpperMiddle" || widgetClass == "FB_FP16DisplayUpperMiddle"
                 || widgetClass == "FB_FP8DisplayLowerMiddle" || widgetClass == "FB_FP16DisplayLowerMiddle"
                 || widgetClass == "FB_FP8DisplayLower" || widgetClass == "FB_FP16DisplayLower") && size == 2)
        {
            if(widgetClass == "FB_FP8Display" || widgetClass == "FB_FP8DisplayUpper")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x00);
            else if(widgetClass == "FB_FP8DisplayUpperMiddle")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x01);
            else if(widgetClass == "FB_FP8DisplayLowerMiddle")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x02);
            else if(widgetClass == "FB_FP8DisplayLower")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x03);

            else if(widgetClass == "FB_FP16Display" ||  widgetClass == "FB_FP16DisplayUpper")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x00);
            else if(widgetClass == "FB_FP16DisplayUpperMiddle")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x01);
            else if(widgetClass == "FB_FP16DisplayLowerMiddle")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x02);
            else if(widgetClass == "FB_FP16DisplayLower")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x03);
        }
        
        else if((widgetClass == "FB_QConLiteDisplayUpper" || widgetClass == "FB_QConLiteDisplayUpperMid" || widgetClass == "FB_QConLiteDisplayLowerMid" || widgetClass == "FB_QConLiteDisplayLower") && size == 2)
        {
            if(widgetClass == "FB_QConLiteDisplayUpper")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_QConLiteDisplayUpperMid")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_QConLiteDisplayLowerMid")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(surface, widget, 2, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_QConLiteDisplayLower")
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
        
        if(tokens[0] == "WidgetEnd")    // finito baybay - Widget list complete
            break;
        
        tokenLines.push_back(tokens);
    }

    for(auto tokenLine : tokenLines)
    {
        if(tokenLine.size() > 1 && tokenLine[0] == "Control")
            new CSIMessageGenerator(widget, tokenLine[1]);
        else if(tokenLine.size() > 1 && tokenLine[0] == "Touch")
            new Touch_CSIMessageGenerator(widget, tokenLine[1]);
        else if(tokenLine.size() > 1 && tokenLine[0] == "FB_Processor")
            widget->AddFeedbackProcessor(new OSC_FeedbackProcessor(surface, widget, tokenLine[1]));
        else if(tokenLine.size() > 1 && tokenLine[0] == "FB_IntProcessor")
            widget->AddFeedbackProcessor(new OSC_IntFeedbackProcessor(surface, widget, tokenLine[1]));
    }
}

static void ProcessWidgetFile(string filePath, ControlSurface* surface)
{
    int lineNumber = 0;
    
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
            
            if(tokens.size() > 0 && tokens[0] == "Widget")
            {
                if(filePath[filePath.length() - 3] == 'm')
                    ProcessMidiWidget(lineNumber, file, tokens, (Midi_ControlSurface*)surface);
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
    actions_["SaveProject"] =                       new SaveProject();
    actions_["Undo"] =                              new Undo();
    actions_["Redo"] =                              new Redo();
    actions_["TrackAutoMode"] =                     new TrackAutoMode();
    actions_["GlobalAutoMode"] =                    new GlobalAutoMode();
    actions_["TrackAutoModeDisplay"] =              new TrackAutoModeDisplay();
    actions_["TimeDisplay"] =                       new TimeDisplay();
    actions_["EuConTimeDisplay"] =                  new EuConTimeDisplay();
    actions_["NoAction"] =                          new NoAction();
    actions_["Reaper"] =                            new ReaperAction();
    actions_["FixedTextDisplay"] =                  new FixedTextDisplay(); ;
    actions_["FixedRGBColourDisplay"] =             new FixedRGBColourDisplay();
    actions_["Rewind"] =                            new Rewind();
    actions_["FastForward"] =                       new FastForward();
    actions_["Play"] =                              new Play();
    actions_["Stop"] =                              new Stop();
    actions_["Record"] =                            new Record();
    actions_["CycleTimeline"] =                     new CycleTimeline();
    actions_["ToggleScrollLink"] =                  new ToggleScrollLink();
    actions_["ForceScrollLink"] =                   new ForceScrollLink();
    actions_["ToggleVCAMode"] =                     new ToggleVCAMode();
    actions_["CycleTimeDisplayModes"] =             new CycleTimeDisplayModes();
    actions_["NextPage"] =                          new GoNextPage();
    actions_["GoPage"] =                            new GoPage();
    actions_["PageNameDisplay"] =                   new PageNameDisplay();
    actions_["Broadcast"] =                         new Broadcast();
    actions_["Receive"] =                           new Receive();
    actions_["GoHome"] =                            new GoHome();
    actions_["GoSubZone"] =                         new GoSubZone();
    actions_["LeaveSubZone"] =                      new LeaveSubZone();
    actions_["GoFXSlot"] =                          new GoFXSlot();
    actions_["PreventFocusedFXMapping"] =           new PreventFocusedFXMapping();
    actions_["ToggleEnableFocusedFXMapping"] =      new ToggleEnableFocusedFXMapping();
    actions_["PreventFocusedFXParamMapping"] =      new PreventFocusedFXParamMapping();
    actions_["ToggleEnableFocusedFXParamMapping"] = new ToggleEnableFocusedFXParamMapping();
    actions_["GoSelectedTrackFX"] =                 new GoSelectedTrackFX();
    actions_["GoTrackSend"] =                       new GoTrackSend();
    actions_["GoTrackReceive"] =                    new GoTrackReceive();
    actions_["GoTrackFXMenu"] =                     new GoTrackFXMenu();
    actions_["GoSelectedTrack"] =                   new GoSelectedTrack();
    actions_["GoSelectedTrackSend"] =               new GoSelectedTrackSend();
    actions_["GoSelectedTrackReceive"] =            new GoSelectedTrackReceive();
    actions_["GoSelectedTrackFXMenu"] =             new GoSelectedTrackFXMenu();
    actions_["TrackBank"] =                         new TrackBank();
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
    actions_["CycleTrackAutoMode"] =                new CycleTrackAutoMode();
    actions_["TrackVolume"] =                       new TrackVolume();
    actions_["SoftTakeover7BitTrackVolume"] =       new SoftTakeover7BitTrackVolume();
    actions_["SoftTakeover14BitTrackVolume"] =      new SoftTakeover14BitTrackVolume();
    actions_["TrackVolumeDB"] =                     new TrackVolumeDB();
    actions_["TrackToggleVCASpill"] =               new TrackToggleVCASpill();
    actions_["TrackSelect"] =                       new TrackSelect();
    actions_["TrackUniqueSelect"] =                 new TrackUniqueSelect();
    actions_["TrackRangeSelect"] =                  new TrackRangeSelect();
    actions_["TrackRecordArm"] =                    new TrackRecordArm();
    actions_["TrackMute"] =                         new TrackMute();
    actions_["TrackSolo"] =                         new TrackSolo();
    actions_["ClearAllSolo"] =                      new ClearAllSolo();
    actions_["TrackInvertPolarity"] =               new TrackInvertPolarity();
    actions_["MCUTrackPan"] =                       new MCUTrackPan();
    actions_["ToggleMCUTrackPanWidth"] =            new ToggleMCUTrackPanWidth();
    actions_["TrackPan"] =                          new TrackPan();
    actions_["TrackPanPercent"] =                   new TrackPanPercent();
    actions_["TrackPanWidth"] =                     new TrackPanWidth();
    actions_["TrackPanWidthPercent"] =              new TrackPanWidthPercent();
    actions_["TrackPanL"] =                         new TrackPanL();
    actions_["TrackPanLPercent"] =                  new TrackPanLPercent();
    actions_["TrackPanR"] =                         new TrackPanR();
    actions_["TrackPanRPercent"] =                  new TrackPanRPercent();
    actions_["TrackNameDisplay"] =                  new TrackNameDisplay();
    actions_["TrackVolumeDisplay"] =                new TrackVolumeDisplay();
    actions_["MCUTrackPanDisplay"] =                new MCUTrackPanDisplay();
    actions_["TrackPanDisplay"] =                   new TrackPanDisplay();
    actions_["TrackPanWidthDisplay"] =              new TrackPanWidthDisplay();
    actions_["TrackPanLeftDisplay"] =               new TrackPanLeftDisplay();
    actions_["TrackPanRightDisplay"] =              new TrackPanRightDisplay();
    actions_["TrackOutputMeter"] =                  new TrackOutputMeter();
    actions_["TrackOutputMeterAverageLR"] =         new TrackOutputMeterAverageLR();
    actions_["TrackOutputMeterMaxPeakLR"] =         new TrackOutputMeterMaxPeakLR();
    actions_["FocusedFXParam"] =                    new FocusedFXParam();
    actions_["FXParam"] =                           new FXParam();
    actions_["FXParamRelative"] =                   new FXParamRelative();
    actions_["ToggleFXBypass"] =                    new ToggleFXBypass();
    actions_["FXNameDisplay"] =                     new FXNameDisplay();
    actions_["FXMenuNameDisplay"] =                 new FXMenuNameDisplay();
    actions_["FXParamNameDisplay"] =                new FXParamNameDisplay();
    actions_["FXParamValueDisplay"] =               new FXParamValueDisplay();
    actions_["FocusedFXParamNameDisplay"] =         new FocusedFXParamNameDisplay();
    actions_["FocusedFXParamValueDisplay"] =        new FocusedFXParamValueDisplay();
    actions_["FXBypassedDisplay"] =                  new FXBypassedDisplay();
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
    actions_["TrackSendVolumeDisplay"] =            new TrackSendVolumeDisplay();
    actions_["TrackSendPanDisplay"] =               new TrackSendPanDisplay();
    actions_["TrackSendPrePostDisplay"] =           new TrackSendPrePostDisplay();
    actions_["TrackReceiveVolume"] =                new TrackReceiveVolume();
    actions_["TrackReceiveVolumeDB"] =              new TrackReceiveVolumeDB();
    actions_["TrackReceivePan"] =                   new TrackReceivePan();
    actions_["TrackReceivePanPercent"] =            new TrackReceivePanPercent();
    actions_["TrackReceiveMute"] =                  new TrackReceiveMute();
    actions_["TrackReceiveInvertPolarity"] =        new TrackReceiveInvertPolarity();
    actions_["TrackReceivePrePost"] =               new TrackReceivePrePost();
    actions_["TrackReceiveNameDisplay"] =           new TrackReceiveNameDisplay();
    actions_["TrackReceiveVolumeDisplay"] =         new TrackReceiveVolumeDisplay();
    actions_["TrackReceivePanDisplay"] =            new TrackReceivePanDisplay();
    actions_["TrackReceivePrePostDisplay"] =        new TrackReceivePrePostDisplay();
}

void Manager::Init()
{
    pages_.clear();

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
    
    try
    {
        ifstream iniFile(iniFilePath);
               
        for (string line; getline(iniFile, line) ; )
        {
            line = regex_replace(line, regex(TabChars), " ");
            line = regex_replace(line, regex(CRLFChars), "");
            
            if(lineNumber == 0)
            {
                if(line != "Version 2.0")
                {
                    MessageBox(g_hwnd, "Version mismatch -- Your CSI.ini file is not Version 2.0", "This is CSI Version 2.0", MB_OK);
                    iniFile.close();
                    return;
                }
                else
                {
                    lineNumber++;
                    continue;
                }
            }
            
            vector<string> tokens(GetTokens(line));
            
            if(tokens.size() > 4) // ignore comment lines and blank lines
            {
                if(tokens[0] == PageToken)
                {
                    currentPage = nullptr;
                    
                    if(tokens.size() == 5)
                    {
                        currentPage = new Page(tokens[1], tokens[2] == FollowMCPToken ? true : false, tokens[3] == "SynchPages" ? true : false, tokens[4] == "UseScrollLink" ? true : false);
                        pages_.push_back(currentPage);
                    }
                }
                else if(tokens[0] == MidiSurfaceToken || tokens[0] == OSCSurfaceToken)
                {
                    if(currentPage)
                    {
                        ControlSurface* surface = nullptr;
                        
                        if(tokens[0] == MidiSurfaceToken && tokens.size() == 8)
                            surface = new Midi_ControlSurface(currentPage, tokens[1], tokens[4], tokens[5], atoi(tokens[6].c_str()), atoi(tokens[7].c_str()), GetMidiInputForPort(atoi(tokens[2].c_str())), GetMidiOutputForPort(atoi(tokens[3].c_str())));
                        else if(tokens[0] == OSCSurfaceToken && tokens.size() == 9)
                            surface = new OSC_ControlSurface(currentPage, tokens[1], tokens[4], tokens[5], atoi(tokens[6].c_str()), atoi(tokens[7].c_str()), GetInputSocketForPort(tokens[1], atoi(tokens[2].c_str())), GetOutputSocketForAddressAndPort(tokens[1], tokens[8], atoi(tokens[3].c_str())));

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
        
        // Restore the BankIndex
        result = DAW::GetProjExtState(0, "CSI", "BankIndex", buf, sizeof(buf));
        
        if(result > 0 && pages_.size() > currentPageIndex_)
        {
            if(MediaTrack* leftmosttrack = DAW::GetTrack(atoi(buf) + 1))
                DAW::SetMixerScroll(leftmosttrack);
            
            pages_[currentPageIndex_]->AdjustTrackBank(atoi(buf));
        }
    }
    catch (exception &e)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", iniFilePath.c_str(), lineNumber);
        DAW::ShowConsoleMsg(buffer);
    }
      
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
ActionContext::ActionContext(Action* action, Widget* widget, shared_ptr<Zone> zone, vector<string> params, vector<vector<string>> properties): action_(action), widget_(widget), zone_(zone), properties_(properties)
{
    for(auto property : properties)
    {
        if(property.size() == 0)
            continue;

        if(property[0] == "NoFeedback")
            noFeedback_ = true;
    }
    
    widget->SetProperties(properties);
    
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
   
    if(actionName == "Reaper" && params.size() > 1)
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
        
        if(params.size() > 2 && params[2] != "[" && params[2] != "{" && isdigit(params[2][0]))
        {
            shouldUseDisplayStyle_ = true;
            displayStyle_ = atol(params[2].c_str());
        }
    }
    
    if(actionName == "FXParamNameDisplay" && params.size() > 1 && isdigit(params[1][0]))
    {
        paramIndex_ = atol(params[1].c_str());
        
        if(params.size() > 2 && params[2] != "{" && params[2] != "[")
            fxParamDisplayName_ = params[2];
    }
    
    if(params.size() > 1 && (actionName == "Broadcast" || actionName == "Receive" || actionName == "Activate" || actionName == "Deactivate" || actionName == "ToggleActivation"))
    {
        for(int i = 1; i < params.size(); i++)
            zoneNames_.push_back(params[i]);
    }

    if(params.size() > 0)
    {
        SetRGB(params, supportsRGB_, supportsTrackColor_, RGBValues_);
        SetSteppedValues(params, deltaValue_, acceleratedDeltaValues_, rangeMinimum_, rangeMaximum_, steppedValues_, acceleratedTickValues_);
    }
    
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
    if(noFeedback_)
        return;
    
    action_->RequestUpdate(this);
}

void ActionContext::ClearWidget()
{
    widget_->Clear();
}

void ActionContext::UpdateWidgetValue(double value)
{
    if(steppedValues_.size() > 0)
        SetSteppedValueIndex(value);

    value = isFeedbackInverted_ == false ? value : 1.0 - value;
   
    widget_->UpdateValue(value);

    if(supportsRGB_)
    {
        currentRGBIndex_ = value == 0 ? 0 : 1;
        widget_->UpdateRGBValue(RGBValues_[currentRGBIndex_].r, RGBValues_[currentRGBIndex_].g, RGBValues_[currentRGBIndex_].b);
    }
    else if(supportsTrackColor_)
    {
        if(MediaTrack* track = zone_->GetNavigator()->GetTrack())
        {
            unsigned int* rgb_colour = (unsigned int*)DAW::GetSetMediaTrackInfo(track, "I_CUSTOMCOLOR", NULL);
            
            int r = (*rgb_colour >> 0) & 0xff;
            int g = (*rgb_colour >> 8) & 0xff;
            int b = (*rgb_colour >> 16) & 0xff;
            
            widget_->UpdateRGBValue(r, g, b);
        }
    }
}

void ActionContext::UpdateWidgetValue(int param, double value)
{
    if(steppedValues_.size() > 0)
        SetSteppedValueIndex(value);

    value = isFeedbackInverted_ == false ? value : 1.0 - value;
        
    widget_->UpdateValue(param, value);
    
    currentRGBIndex_ = value == 0 ? 0 : 1;
    
    if(supportsRGB_)
    {
        currentRGBIndex_ = value == 0 ? 0 : 1;
        widget_->UpdateRGBValue(RGBValues_[currentRGBIndex_].r, RGBValues_[currentRGBIndex_].g, RGBValues_[currentRGBIndex_].b);
    }
    else if(supportsTrackColor_)
    {
        if(MediaTrack* track = zone_->GetNavigator()->GetTrack())
        {
            unsigned int* rgb_colour = (unsigned int*)DAW::GetSetMediaTrackInfo(track, "I_CUSTOMCOLOR", NULL);
            
            int r = (*rgb_colour >> 0) & 0xff;
            int g = (*rgb_colour >> 8) & 0xff;
            int b = (*rgb_colour >> 16) & 0xff;
            
            widget_->UpdateRGBValue(r, g, b);
        }
    }
}

void ActionContext::UpdateWidgetValue(string value)
{
    widget_->UpdateValue(value);
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
        DoRangeBoundAction(action_->GetCurrentNormalizedValue(this) + delta);
}

void ActionContext::DoRelativeAction(int accelerationIndex, double delta)
{
    if(steppedValues_.size() > 0)
        DoAcceleratedSteppedValueAction(accelerationIndex, delta);
    else if(acceleratedDeltaValues_.size() > 0)
        DoAcceleratedDeltaValueAction(accelerationIndex, delta);
    else
        DoRangeBoundAction(action_->GetCurrentNormalizedValue(this) + delta);
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

Zone::Zone(ZoneManager* const zoneManager, Navigator* navigator, int slotIndex, map<string, string> touchIds, string name, string alias, string sourceFilePath, vector<string> includedZones, vector<string> associatedZones): zoneManager_(zoneManager), navigator_(navigator), slotIndex_(slotIndex), touchIds_(touchIds), name_(name), alias_(alias), sourceFilePath_(sourceFilePath)
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
    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            zone->Deactivate();
        
    if(associatedZones_.count(zoneName) > 0)
        for(auto zone : associatedZones_[zoneName])
            zone->Activate();
}

void Zone::AddNavigatorsForZone(string zoneName, vector<Navigator*> &navigators)
{
    if(zoneName == "Buttons")
        navigators.push_back(zoneManager_->GetSelectedTrackNavigator());
    else if(zoneName == "MasterTrack")
        navigators.push_back(zoneManager_->GetMasterTrackNavigator());
    else if(zoneName == "Track" || zoneName == "TrackSend" || zoneName == "TrackReceive" || zoneName == "TrackFXMenu" )
        for(int i = 0; i < zoneManager_->GetNumChannels(); i++)
            navigators.push_back(zoneManager_->GetSurface()->GetPage()->GetNavigatorForChannel(i + zoneManager_->GetSurface()->GetChannelOffset()));
    else if(zoneName == "SelectedTrack" || zoneName == "SelectedTrackSend" || zoneName == "SelectedTrackReceive" || zoneName == "SelectedTrackFXMenu")
        for(int i = 0; i < zoneManager_->GetNumChannels(); i++)
            navigators.push_back(zoneManager_->GetSelectedTrackNavigator());
}

void Zone::Activate()
{   
    isActive_ = true;
    
    zoneManager_->GetSurface()->ActivatingZone(GetName());
    
    for(auto zone : includedZones_)
        zone->Activate();
   
    for(auto [key, zones] : associatedZones_)
        for(auto zone : zones)
            zone->Deactivate();
    
    for(auto [key, zones] : subZones_)
        for(auto zone : zones)
            zone->Deactivate();
}

void Zone::OnTrackSelection()
{
    // GAW TBD -- reset FXMenu
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
        context->RunDeferredActions();
    
    if(GetActionContexts(widget).size() > 0)
    {
        shared_ptr<ActionContext> context = GetActionContexts(widget)[0];
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

        activeTouchIds_[widgetName + "Touch"] = value;
        activeTouchIds_[widgetName + "TouchPress"] = value;
        activeTouchIds_[widgetName + "TouchRelease"] = ! value;

        for(auto context : GetActionContexts(widget))
            context->DoTouch(value);
    }
    else
    {
        for(auto zone : includedZones_)
            zone->DoTouch(widget, widgetName, isUsed, value);
    }
}

vector<shared_ptr<ActionContext>> &Zone::GetActionContexts(Widget* widget)
{
    string widgetName = widget->GetName();
    string modifier = "";
    
    if( ! widget->GetIsModifier())
        modifier = widget->GetSurface()->GetPage()->GetModifier();
    
    if(touchIds_.count(widgetName) > 0 && activeTouchIds_.count(touchIds_[widgetName]) > 0 && activeTouchIds_[touchIds_[widgetName]] == true && actionContextDictionary_[widget].count(touchIds_[widgetName] + "+" + modifier) > 0)
        return actionContextDictionary_[widget][touchIds_[widgetName] + "+" + modifier];
    else if(actionContextDictionary_[widget].count(modifier) > 0)
        return actionContextDictionary_[widget][modifier];
    else if(actionContextDictionary_[widget].count("") > 0)
        return actionContextDictionary_[widget][""];
    else
        return defaultContexts_;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// SubZone
////////////////////////////////////////////////////////////////////////////////////////////////////////
void SubZone::GoSubZone(string subZoneName)
{
    Deactivate();
    enclosingZone_->GoSubZone(subZoneName);
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

void Widget::SetProperties(vector<vector<string>> properties)
{
    for(auto processor : feedbackProcessors_)
        processor->SetProperties(properties);
}

void  Widget::UpdateValue(double value)
{
    for(auto processor : feedbackProcessors_)
        processor->SetValue(value);
}

void  Widget::UpdateValue(int mode, double value)
{
    for(auto processor : feedbackProcessors_)
        processor->SetValue(mode, value);
}

void  Widget::UpdateValue(string value)
{
    for(auto processor : feedbackProcessors_)
        processor->SetValue(value);
}

void  Widget::UpdateRGBValue(int r, int g, int b)
{
    for(auto processor : feedbackProcessors_)
        processor->SetRGBValue(r, g, b);
}

void  Widget::Clear()
{
    for(auto processor : feedbackProcessors_)
        processor->Clear();
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
void OSC_FeedbackProcessor::SetRGBValue(int r, int g, int b)
{
    if(lastRValue_ != r)
    {
        lastRValue_ = r;
        surface_->SendOSCMessage(this, oscAddress_ + "/rColor", (double)r);
    }
    
    if(lastGValue_ != g)
    {
        lastGValue_ = g;
        surface_->SendOSCMessage(this, oscAddress_ + "/gColor", (double)g);
    }
    
    if(lastBValue_ != b)
    {
        lastBValue_ = b;
        surface_->SendOSCMessage(this, oscAddress_ + "/bColor", (double)b);
    }
}

void OSC_FeedbackProcessor::ForceValue(double value)
{
    lastDoubleValue_ = value;
    surface_->SendOSCMessage(this, oscAddress_, value);
}

void OSC_FeedbackProcessor::ForceValue(int param, double value)
{
    lastDoubleValue_ = value;
    surface_->SendOSCMessage(this, oscAddress_, value);
}

void OSC_FeedbackProcessor::ForceValue(string value)
{
    lastStringValue_ = value;
    surface_->SendOSCMessage(this, oscAddress_, value);
}

void OSC_IntFeedbackProcessor::ForceValue(double value)
{
    lastDoubleValue_ = value;
    surface_->SendOSCMessage(this, oscAddress_, (int)value);
}

void OSC_IntFeedbackProcessor::ForceValue(int param, double value)
{
    lastDoubleValue_ = value;
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
    for(auto &[key, value] : usedWidgets_)
    {
        if(value == false)
        {
            key->UpdateValue(0.0);
            key->UpdateValue(0, 0.0);
            key->UpdateValue("");
            key->UpdateRGBValue(0, 0, 0);
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
    LockoutFocusedFXMapping();
    
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
    LockoutFocusedFXMapping();
    
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

void ZoneManager::HandleActivation(string zoneName)
{
    if(receive_.count(zoneName) > 0  && homeZone_ != nullptr)
    {
        if(zoneName == "Home")
        {
            ClearFXMapping();
            ResetOffsets();
            homeZone_->Activate();
        }
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
    // GAW TBD -- reset FXMenu
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

Navigator* ZoneManager::GetMasterTrackNavigator() { return surface_->GetPage()->GetMasterTrackNavigator(); }
Navigator* ZoneManager::GetSelectedTrackNavigator() { return surface_->GetPage()->GetSelectedTrackNavigator(); }
Navigator* ZoneManager::GetFocusedFXNavigator() { return surface_->GetPage()->GetFocusedFXNavigator(); }
Navigator* ZoneManager::GetDefaultNavigator() { return surface_->GetPage()->GetDefaultNavigator(); }
int ZoneManager::GetNumChannels() { return surface_->GetNumChannels(); }

////////////////////////////////////////////////////////////////////////////////////////////////////////
// ControlSurface
////////////////////////////////////////////////////////////////////////////////////////////////////////
void ControlSurface::TrackFXListChanged()
{
    OnTrackSelection();
}

void ControlSurface::OnTrackSelection()
{
    if(widgetsByName_.count("OnTrackSelection") > 0)
    {
        if(page_->GetSelectedTrack())
            zoneManager_->DoAction(widgetsByName_["OnTrackSelection"], 1.0);
        else
            zoneManager_->OnTrackDeselection();
        
        zoneManager_->OnTrackSelection();
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
    if(midiOutput_)
        midiOutput_->SendMsg(midiMessage, -1);
    
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
    if(midiOutput_)
        midiOutput_->Send(first, second, third, -1);
    
    if(TheManager->GetSurfaceOutDisplay())
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "%s  %02x  %02x  %02x \n", ("OUT->" + name_).c_str(), first, second, third);
        DAW::ShowConsoleMsg(buffer);
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

void OSC_ControlSurface::ActivatingZone(string zoneName)
{
    string oscAddress(zoneName);
    oscAddress = regex_replace(oscAddress, regex(BadFileChars), "_");
    oscAddress = "/" + oscAddress;

    if(outSocket_ != nullptr && outSocket_->isOk())
    {
        oscpkt::Message message;
        message.init(oscAddress);
        packetWriter_.init().addMessage(message);
        outSocket_->sendPacket(packetWriter_.packetData(), packetWriter_.packetSize());
    }
    
    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg((zoneName + "->" + "LoadingZone---->" + name_ + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, double value)
{
    if(outSocket_ != nullptr && outSocket_->isOk())
    {
        oscpkt::Message message;
        message.init(oscAddress).pushFloat((float)value);
        packetWriter_.init().addMessage(message);
        outSocket_->sendPacket(packetWriter_.packetData(), packetWriter_.packetSize());
    }
    
    if(TheManager->GetSurfaceOutDisplay())
    {
        if(TheManager->GetSurfaceOutDisplay())
            DAW::ShowConsoleMsg(("OUT->" + name_ + " " + feedbackProcessor->GetWidget()->GetName() + " " + oscAddress + " " + to_string(value) + "\n").c_str());
    }
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, int value)
{
    if(outSocket_ != nullptr && outSocket_->isOk())
    {
        oscpkt::Message message;
        message.init(oscAddress).pushInt64(value);
        packetWriter_.init().addMessage(message);
        outSocket_->sendPacket(packetWriter_.packetData(), packetWriter_.packetSize());
    }
    
    if(TheManager->GetSurfaceOutDisplay())
    {
        if(TheManager->GetSurfaceOutDisplay())
            DAW::ShowConsoleMsg(("OUT->" + name_ + " " + feedbackProcessor->GetWidget()->GetName() + " " + oscAddress + " " + to_string(value) + "\n").c_str());
    }
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, string value)
{
    if(outSocket_ != nullptr && outSocket_->isOk())
    {
        oscpkt::Message message;
        message.init(oscAddress).pushStr(value);
        packetWriter_.init().addMessage(message);
        outSocket_->sendPacket(packetWriter_.packetData(), packetWriter_.packetSize());
    }
    
    if(TheManager->GetSurfaceOutDisplay())
    {
        if(TheManager->GetSurfaceOutDisplay())
            DAW::ShowConsoleMsg(("OUT->" + name_ + " " + feedbackProcessor->GetWidget()->GetName() + " " + oscAddress + " " + value + "\n").c_str());
    }
    
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

