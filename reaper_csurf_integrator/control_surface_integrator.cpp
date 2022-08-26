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
    vector<string> modifierSlots = { "", "", "", "", "", "", ""};
    string modifier_token;
    
    while (getline(modified_role, modifier_token, '+'))
        modifier_tokens.push_back(modifier_token);
    
    widgetName = modifier_tokens[modifier_tokens.size() - 1];
    
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
            else if(modifier_tokens[i] == ToggleToken)
                modifierSlots[6] = ToggleToken + "+";


            else if(modifier_tokens[i] == "InvertFB")
                isFeedbackInverted = true;
            else if(modifier_tokens[i] == "Hold")
                holdDelayAmount = 1.0;
            else if(modifier_tokens[i] == "Property")
                isProperty = true;
        }
    }
    
    modifier = modifierSlots[0] + modifierSlots[1] + modifierSlots[2] + modifierSlots[3] + modifierSlots[4] + modifierSlots[5] + modifierSlots[6];
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
                                                
                        if(zoneName == "Home")
                            zoneManager->SetHomeZone(zone);
                        
                        if(zoneName == "Track" && i == 0)
                            zoneManager->SetFirstTrackZone(zone);
                        
                        if(zoneName == "FocusedFXParam")
                            zoneManager->SetFocusedFXParamZone(zone);
                        
                        zones.push_back(zone);
                        
                        for(auto [widgetName, modifierActions] : widgetActions)
                        {
                            string surfaceWidgetName = widgetName;
                            
                            if(navigators.size() > 1)
                                surfaceWidgetName = regex_replace(surfaceWidgetName, regex("[|]"), to_string(i + 1));
                            
                            if(enclosingZone != nullptr && enclosingZone->GetChannelNumber() != 0)
                                surfaceWidgetName = regex_replace(surfaceWidgetName, regex("[|]"), to_string(enclosingZone->GetChannelNumber()));
                            
                            Widget* widget = zoneManager->GetSurface()->GetWidgetByName(surfaceWidgetName);
                                                        
                            if(widget == nullptr)
                                continue;
 
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
                        
                        if(subZones.size() > 0)
                            zone->InitSubZones(subZones, zone);
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
        else if(widgetClass == "Encoder7Bit" && size == 4)
            new Encoder7Bit_Midi_CSIMessageGenerator(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
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
        else if(widgetClass == "FB_FaderportRGB" && size == 4)
        {
            feedbackProcessor = new FaderportRGB_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_FaderportTwoStateRGB" && size == 4)
        {
            feedbackProcessor = new FPTwoStateRGB_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_FaderportValueBar"  && size == 2)
        {
                feedbackProcessor = new FPValueBar_Midi_FeedbackProcessor(surface, widget, stoi(tokenLines[i][1]));
        }
        else if((widgetClass == "FB_FPVUMeter") && size == 2)
        {
            feedbackProcessor = new FPVUMeter_Midi_FeedbackProcessor(surface, widget, stoi(tokenLines[i][1]));
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
        else if(widgetClass == "FB_ConsoleOneVUMeter" && size == 4)
        {
            feedbackProcessor = new ConsoleOneVUMeter_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_ConsoleOneGainReductionMeter" && size == 4)
        {
            feedbackProcessor = new ConsoleOneGainReductionMeter_Midi_FeedbackProcessor(surface, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]), strToHex(tokenLines[i][3])));
        }
        else if(widgetClass == "FB_MCUTimeDisplay" && size == 1)
        {
            feedbackProcessor = new MCU_TimeDisplay_Midi_FeedbackProcessor(surface, widget);
        }
        else if(widgetClass == "FB_MCUAssignmentDisplay" && size == 1)
        {
            feedbackProcessor = new FB_MCU_AssignmentDisplay_Midi_FeedbackProcessor(surface, widget);
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
        else if((widgetClass == "FB_XTouchDisplayUpper" || widgetClass == "FB_XTouchDisplayLower" || widgetClass == "FB_XTouchXTDisplayUpper" || widgetClass == "FB_XTouchXTDisplayLower") && size == 2)
        {
            if(widgetClass == "FB_XTouchDisplayUpper")
                feedbackProcessor = new XTouchDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_XTouchDisplayLower")
                feedbackProcessor = new XTouchDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x14, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_XTouchXTDisplayUpper")
                feedbackProcessor = new XTouchDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x15, 0x12, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_XTouchXTDisplayLower")
                feedbackProcessor = new XTouchDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x15, 0x12, stoi(tokenLines[i][1]));
        }
        else if((widgetClass == "FB_C4DisplayUpper" || widgetClass == "FB_C4DisplayLower") && size == 3)
        {
            if(widgetClass == "FB_C4DisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 0, 0x17, stoi(tokenLines[i][1]) + 0x30, stoi(tokenLines[i][2]));
            else if(widgetClass == "FB_C4DisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(surface, widget, 1, 0x17, stoi(tokenLines[i][1]) + 0x30, stoi(tokenLines[i][2]));
        }
        else if((widgetClass == "FB_FP8ScribbleLine1" || widgetClass == "FB_FP16ScribbleLine1"
                 || widgetClass == "FB_FP8ScribbleLine2" || widgetClass == "FB_FP16ScribbleLine2"
                 || widgetClass == "FB_FP8ScribbleLine3" || widgetClass == "FB_FP16ScribbleLine3"
                 || widgetClass == "FB_FP8ScribbleLine4" || widgetClass == "FB_FP16ScribbleLine4") && size == 2)
        {
            if(widgetClass == "FB_FP8ScribbleLine1")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x00);
            else if(widgetClass == "FB_FP8ScribbleLine2")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x01);
            else if(widgetClass == "FB_FP8ScribbleLine3")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x02);
            else if(widgetClass == "FB_FP8ScribbleLine4")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]), 0x03);

            else if(widgetClass == "FB_FP16ScribbleLine1")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x00);
            else if(widgetClass == "FB_FP16ScribbleLine2")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x01);
            else if(widgetClass == "FB_FP16ScribbleLine3")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x02);
            else if(widgetClass == "FB_FP16ScribbleLine4")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]), 0x03);
        }
        else if((widgetClass == "FB_FP8ScribbleStripMode" || widgetClass == "FB_FP16ScribbleStripMode") && size == 2)
        {
            if(widgetClass == "FB_FP8ScribbleStripMode")
                feedbackProcessor = new FPScribbleStripMode_Midi_FeedbackProcessor(surface, widget, 0x02, stoi(tokenLines[i][1]));
            else if(widgetClass == "FB_FP16ScribbleStripMode")
                feedbackProcessor = new FPScribbleStripMode_Midi_FeedbackProcessor(surface, widget, 0x16, stoi(tokenLines[i][1]));
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
    actions_["Speak"] =                             new SpeakOSARAMessage();
    actions_["SendMIDIMessage"] =                   new SendMIDIMessage();
    actions_["SendOSCMessage"] =                    new SendOSCMessage();
    actions_["SaveProject"] =                       new SaveProject();
    actions_["Undo"] =                              new Undo();
    actions_["Redo"] =                              new Redo();
    actions_["WidgetMode"] =                        new WidgetMode();
    actions_["SetWidgetMode"] =                     new SetWidgetMode();
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
    actions_["CycleTrackVCAFolderModes"] =          new CycleTrackVCAFolderModes();
    actions_["TrackVCAFolderModeDisplay"] =         new TrackVCAFolderModeDisplay();
    actions_["CycleTimeDisplayModes"] =             new CycleTimeDisplayModes();
    actions_["NextPage"] =                          new GoNextPage();
    actions_["GoPage"] =                            new GoPage();
    actions_["PageNameDisplay"] =                   new PageNameDisplay();
    actions_["Broadcast"] =                         new Broadcast();
    actions_["Receive"] =                           new Receive();
    actions_["GoHome"] =                            new GoHome();
    actions_["GoSubZone"] =                         new GoSubZone();
    actions_["LeaveSubZone"] =                      new LeaveSubZone();
    actions_["SetAllDisplaysColor"] =               new SetAllDisplaysColor();
    actions_["RestoreAllDisplaysColor"] =           new RestoreAllDisplaysColor();
    actions_["GoFXSlot"] =                          new GoFXSlot();
    actions_["ToggleEnableFocusedFXMapping"] =      new ToggleEnableFocusedFXMapping();
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
    actions_["ToggleChannel"] =                     new SetToggleChannel();
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
    actions_["FXParamRelative"] =                   new FXParamRelative();
    actions_["ToggleFXBypass"] =                    new ToggleFXBypass();
    actions_["ToggleFXOffline"] =                   new ToggleFXOffline();
    actions_["FXNameDisplay"] =                     new FXNameDisplay();
    actions_["FXMenuNameDisplay"] =                 new FXMenuNameDisplay();
    actions_["FXParamNameDisplay"] =                new FXParamNameDisplay();
    actions_["FXParamValueDisplay"] =               new FXParamValueDisplay();
    actions_["FocusedFXParamNameDisplay"] =         new FocusedFXParamNameDisplay();
    actions_["FocusedFXParamValueDisplay"] =        new FocusedFXParamValueDisplay();
    actions_["FXBypassedDisplay"] =                 new FXBypassedDisplay();
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
    actions_["TrackReceiveStereoMonoToggle"] =      new TrackReceiveStereoMonoToggle();
    actions_["TrackReceivePrePost"] =               new TrackReceivePrePost();
    actions_["TrackReceiveNameDisplay"] =           new TrackReceiveNameDisplay();
    actions_["TrackReceiveVolumeDisplay"] =         new TrackReceiveVolumeDisplay();
    actions_["TrackReceivePanDisplay"] =            new TrackReceivePanDisplay();
    actions_["TrackReceivePrePostDisplay"] =        new TrackReceivePrePostDisplay();
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
                    oscSurfaces[tokens[1]] = new OSC_ControlSurfaceIO(tokens[1], GetInputSocketForPort(tokens[1], atoi(tokens[2].c_str())), GetOutputSocketForAddressAndPort(tokens[1], tokens[4], atoi(tokens[3].c_str())));
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
                    if(currentPage && tokens.size() == 5)
                    {
                        ControlSurface* surface = nullptr;
                        
                        if(midiSurfaces.count(tokens[0]) > 0)
                            surface = new Midi_ControlSurface(currentPage, tokens[0], atoi(tokens[1].c_str()), atoi(tokens[2].c_str()), tokens[3], tokens[4], midiSurfaces[tokens[0]]);
                        else if(oscSurfaces.count(tokens[0]) > 0)
                            surface = new OSC_ControlSurface(currentPage, tokens[0], atoi(tokens[1].c_str()), atoi(tokens[2].c_str()), tokens[3], tokens[4], oscSurfaces[tokens[0]]);

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

void ActionContext::RequestUpdateWidgetMode()
{
    action_->RequestUpdateWidgetMode(this);
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
        UpdateTrackColor();
}

void ActionContext::UpdateTrackColor()
{
    if(MediaTrack* track = zone_->GetNavigator()->GetTrack())
    {
        rgb_color color = DAW::GetTrackColor(track);
        widget_->UpdateRGBValue(color.r, color.g, color.b);
    }
}

void ActionContext::UpdateWidgetValue(string value)
{
    widget_->UpdateValue(value);
}

void ActionContext::UpdateWidgetMode(string modeParams)
{
    widget_->UpdateMode(modeParams);
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
    if(associatedZones_.count(zoneName) > 0 && associatedZones_[zoneName].size() > 0 && associatedZones_[zoneName][0]->GetIsActive())
    {
        for(auto zone : associatedZones_[zoneName])
            zone->Deactivate();
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

void Zone::SetAllDisplaysColor(string color)
{
    for(auto [widget, isUsed] : widgets_)
        widget->SetAllDisplaysColor(color);
}

void Zone::RestoreAllDisplaysColor()
{
    for(auto [widget, isUsed] : widgets_)
        widget->RestoreAllDisplaysColor();
}

void Zone::Activate()
{
    for(auto [widget, isUsed] : widgets_)
        if(widget->GetName() == "OnZoneActivation")
            for(auto context : GetActionContexts(widget))
                context->DoAction(1.0);

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
        context->RunDeferredActions();
    
    for(auto context : GetActionContexts(widget))
        context->RequestUpdateWidgetMode();
    
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
    bool isToggled = widget->GetSurface()->GetIsChannelToggled(widget->GetChannelNumber());
    
    string modifier = widget->GetSurface()->GetPage()->GetModifier();
    
    if(isToggled && (touchIds_.count(widgetName) > 0 && activeTouchIds_.count(touchIds_[widgetName]) > 0 && activeTouchIds_[touchIds_[widgetName]] == true && actionContextDictionary_[widget].count(touchIds_[widgetName] + "+" + modifier + "Toggle+")) > 0)
        return actionContextDictionary_[widget][touchIds_[widgetName] + "+" + modifier + "Toggle+"];
    if(touchIds_.count(widgetName) > 0 && activeTouchIds_.count(touchIds_[widgetName]) > 0 && activeTouchIds_[touchIds_[widgetName]] == true && actionContextDictionary_[widget].count(touchIds_[widgetName] + "+" + modifier) > 0)
        return actionContextDictionary_[widget][touchIds_[widgetName] + "+" + modifier];
    else if(isToggled && actionContextDictionary_[widget].count(modifier + "Toggle+") > 0)
        return actionContextDictionary_[widget][modifier + "Toggle+"];
    else if(isToggled && actionContextDictionary_[widget].count("Toggle+") > 0)
        return actionContextDictionary_[widget]["Toggle+"];
    else if(actionContextDictionary_[widget].count(modifier) > 0)
        return actionContextDictionary_[widget][modifier];
    else if(actionContextDictionary_[widget].count("") > 0)
        return actionContextDictionary_[widget][""];
    else
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

void  Widget::UpdateValue(string value)
{
    for(auto processor : feedbackProcessors_)
        processor->SetValue(value);
}

void  Widget::UpdateMode(string modeParams)
{
    for(auto processor : feedbackProcessors_)
        processor->SetMode(modeParams);
}

void  Widget::UpdateRGBValue(int r, int g, int b)
{
    for(auto processor : feedbackProcessors_)
        processor->SetRGBValue(r, g, b);
}

void Widget::SetAllDisplaysColor(string color)
{
    for(auto processor : feedbackProcessors_)
        processor->SetAllDisplaysColor(color);
}

void Widget::RestoreAllDisplaysColor()
{
    for(auto processor : feedbackProcessors_)
        processor->RestoreAllDisplaysColor();
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
    surface_->GetPage()->ResetTrackVCAFolderMode();
    
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

Navigator* ZoneManager::GetMasterTrackNavigator() { return surface_->GetPage()->GetMasterTrackNavigator(); }
Navigator* ZoneManager::GetSelectedTrackNavigator() { return surface_->GetPage()->GetSelectedTrackNavigator(); }
Navigator* ZoneManager::GetFocusedFXNavigator() { return surface_->GetPage()->GetFocusedFXNavigator(); }
Navigator* ZoneManager::GetDefaultNavigator() { return surface_->GetPage()->GetDefaultNavigator(); }
int ZoneManager::GetNumChannels() { return surface_->GetNumChannels(); }

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

void TrackNavigationManager::NextTrackVCAFolderMode(string params)
{
    string VCAColor = "White";
    string FolderColor = "White";
    
    vector<string> tokens = GetTokens(params);
    
    if(tokens.size()  == 2)
    {
        VCAColor = tokens[0];
        FolderColor = tokens[1];
    }
    
    currentTrackVCAFolderMode_ += 1;
    
    if(currentTrackVCAFolderMode_ > 2)
    {
        page_->RestoreAllDisplaysColor();
        currentTrackVCAFolderMode_ = 0;
        isVCAModeEnabled_ = false;
        isFolderModeEnabled_ = false;
    }
    else if(currentTrackVCAFolderMode_ == 1)
    {
        page_->SetAllDisplaysColor(VCAColor);
        isVCAModeEnabled_ = true;
        isFolderModeEnabled_ = false;
    }
    else if(currentTrackVCAFolderMode_ == 2)
    {
        page_->SetAllDisplaysColor(FolderColor);
        isFolderModeEnabled_ = true;
        isVCAModeEnabled_ = false;
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

void OSC_ControlSurface::ActivatingZone(string zoneName)
{
    string oscAddress(zoneName);
    oscAddress = regex_replace(oscAddress, regex(BadFileChars), "_");
    oscAddress = "/" + oscAddress;

    surfaceIO_->ActivatingZone(oscAddress);
        
    if(TheManager->GetSurfaceOutDisplay())
        DAW::ShowConsoleMsg((zoneName + "->" + "LoadingZone---->" + name_ + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, double value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
    
    if(TheManager->GetSurfaceOutDisplay())
    {
        if(TheManager->GetSurfaceOutDisplay())
            DAW::ShowConsoleMsg(("OUT->" + name_ + " " + feedbackProcessor->GetWidget()->GetName() + " " + oscAddress + " " + to_string(value) + "\n").c_str());
    }
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, int value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);

    if(TheManager->GetSurfaceOutDisplay())
    {
        if(TheManager->GetSurfaceOutDisplay())
            DAW::ShowConsoleMsg(("OUT->" + name_ + " " + feedbackProcessor->GetWidget()->GetName() + " " + oscAddress + " " + to_string(value) + "\n").c_str());
    }
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor* feedbackProcessor, string oscAddress, string value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);

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

