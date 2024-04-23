//
//  control_surface_integrator.cpp
//  reaper_control_surface_integrator
//
//

#define INCLUDE_LOCALIZE_IMPORT_H
#include "control_surface_integrator.h"
#include "control_surface_midi_widgets.h"
#include "control_surface_Reaper_actions.h"
#include "control_surface_manager_actions.h"

#include "../WDL/dirscan.h"
#include "resource.h"

WDL_DLGRET dlgProcMainConfig(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

extern reaper_plugin_info_t *g_reaper_plugin_info;

int g_minNumParamSteps = 2;
int g_maxNumParamSteps = 30;

bool g_surfaceRawInDisplay;
bool g_surfaceInDisplay;
bool g_surfaceOutDisplay;
bool g_fxParamsWrite;

Widget* g_FocusedWidget;

void GetPropertiesFromTokens(int start, int finish, const string_list &tokens, PropertyList &properties)
{
    for (int i = start; i < finish; i++)
    {
        const char *tok = tokens.get(i);
        const char *eq = strstr(tok,"=");
        if (eq != NULL && strstr(eq+1, "=") == NULL /* legacy behavior, don't allow = in value */)
        {
            char tmp[128];
            int pnlen = (int) (eq-tok) + 1; // +1 is for the null character to be added to tmp
            if (WDL_NOT_NORMALLY(pnlen > sizeof(tmp))) // if this asserts on a valid property name, increase tmp size
                pnlen = sizeof(tmp);
            lstrcpyn_safe(tmp, tok, pnlen);

            PropertyType prop = PropertyList::prop_from_string(tmp);
            if (prop != PropertyType_Unknown)
            {
                properties.set_prop(prop, eq+1);
            }
            else
            {
                properties.set_prop(prop, tok); // unknown properties are preserved as Unknown, key=value pair

                char buffer[250];
                snprintf(buffer, sizeof(buffer), "CSI does not support property named %s\n", tok);
                ShowConsoleMsg(buffer);
                
               // WDL_ASSERT(false);
            }
        }
    }
}

void GetSteppedValues(const string_list &params, int start_idx, double &deltaValue, vector<double> &acceleratedDeltaValues, double &rangeMinimum, double &rangeMaximum, vector<double> &steppedValues, vector<int> &acceleratedTickValues)
{
    int openSquareIndex = -1, closeSquareIndex = -1;
    
    for (int i = start_idx; i < params.size(); ++i)
        if (params[i] == "[")
        {
            openSquareIndex = i;
            break;
        }

    if (openSquareIndex < 0) return;
    
    for (int i = openSquareIndex + 1; i < params.size(); ++i)
        if (params[i] == "]")
        {
            closeSquareIndex = i;
            break;
        }
    
    if (closeSquareIndex > 0)
    {
        for (int i = openSquareIndex + 1; i < closeSquareIndex; ++i)
        {
            const char *str = params.get(i);

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

void GetParamStepsString(string &outputString, int numSteps) // appends to string
{
    for (int i = 0; i < numSteps; i++)
    {
        char tmp[128];
        snprintf(tmp,sizeof(tmp), "%.2f", EnumSteppedValues(numSteps, i));
        WDL_remove_trailing_decimal_zeros(tmp, 0);
        lstrcatn(tmp, " ", sizeof(tmp));
        outputString += tmp;
    }
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

string int_to_string(int value)
{
    char buf[64];
    sprintf(buf, "%d", value);
    
    return string(buf);
}

string double_to_string(double value)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "%f", value);
    
    return string(buf);
}


bool getline(fpistream &fp, string &str) // mimic C++ getline()
{
    if (fp.handle == NULL)
        return false;
    
    str.clear();
    if (WDL_NOT_NORMALLY(!fp.handle)) return false;
    for (;;)
    {
        char buf[512];
        if (!fgets(buf, sizeof(buf), fp.handle) || WDL_NOT_NORMALLY(!buf[0]))
            return str.length()>0;
        str.append(buf);

        const size_t sz = str.length();
        if (WDL_NORMALLY(sz>0) && str.c_str()[sz-1] == '\n')
        {
            if (sz>1 && str.c_str()[sz-2] == '\r')
                str.resize(sz - 2);
            else
                str.resize(sz - 1);
            return true;
        }
    }
}


void ReplaceAllWith(string &output, const char *charsToReplace, const char *replacement)
{
    // replace all occurences of
    // any char in charsToReplace
    // with replacement string
    const string tmp = output;
    const char *p = tmp.c_str();
    output.clear();

    while (*p)
    {
        if (strchr(charsToReplace, *p) != NULL)
        {
            output.append(replacement);
        }
        else
            output.append(p,1);
        p++;
    }
}

void GetSubTokens(string_list &tokens, const char *line, char delim)
{
    while (*line)
    {
        const char *np = line;
        while (*np && *np != delim) np++;

        tokens.add_raw(line, (int) (np-line));
        if (!*np) break;
        line = np + 1;
    }
}

void GetTokens(string_list &tokens, const char *line)
{
    const char *rd = line;
    for (;;)
    {
        while (*rd > 0 && isspace(*rd)) rd++;
        if (!*rd) break;

        if (*rd == '\"')
        {
            rd++;
            char *wr = tokens.add_raw(NULL, strlen(rd));
            if (WDL_NOT_NORMALLY(!wr)) break;
            // wr will be all 0 bytes
            while (*rd)
            {
                if (*rd == '\"')
                {
                    rd++;
                    break;
                }
                if (*rd == '\\' && (rd[1] == '\\' || rd[1] == '\"')) // if \\ or \", passthrough second character
                    rd++;
                *wr++ = *rd++;
            }
            tokens.trim_last(); // remove any trailing 0 bytes
        }
        else
        {
            const char *sp = rd;
            while (*rd && (*rd<0 || !isspace(*rd))) rd++;
            tokens.add_raw(sp, rd-sp);
        }
    }
}

void string_list::clear()
{
    buf_.Resize(0);
    offsets_.Resize(0);
}
char *string_list::add_raw(const char *str, size_t len)
{
    offsets_.Add(buf_.GetSize());
    char *ret = buf_.Add(str, (int)len + 1);
    if (WDL_NORMALLY(ret)) ret[len]=0;
    return ret;
}

void string_list::trim_last()
{
    if (WDL_NOT_NORMALLY(!offsets_.GetSize())) return;
    const int lastidx = offsets_.Get()[offsets_.GetSize()-1];

    int sz = buf_.GetSize();
    if (WDL_NOT_NORMALLY(lastidx<0 || lastidx >= sz)) return;

    while (sz > lastidx+1 && buf_.Get()[sz-1] == 0 && buf_.Get()[sz-2] == 0)
        sz--;
    buf_.Resize(sz);
}

void string_list::update(int idx, const char *value)
{
    string tmp;
    if (value >= buf_.Get() && value < buf_.Get() + buf_.GetSize())
    {
        tmp = value;
        value = tmp.c_str();
    }
    if (WDL_NORMALLY(idx >= 0 && idx < offsets_.GetSize()))
    {
        const int o = offsets_.Get()[idx];
        if (WDL_NORMALLY(o >= 0 && o < buf_.GetSize()))
        {
            const int newl = (int) strlen(value) + 1, oldl = (int) strlen(buf_.Get() + o) + 1;
            if (newl != oldl)
            {
                const int trail = buf_.GetSize() - (o + oldl);
                const int dsize = newl - oldl;
                buf_.Resize(buf_.GetSize() + dsize, false);
                if (trail > 0) memmove(buf_.Get() + o + newl, buf_.Get() + o + oldl, trail);
                for (int i = 0; i < offsets_.GetSize(); i ++)
                    if (offsets_.Get()[i] > o)
                        offsets_.Get()[i] += dsize;
            }
            memcpy(buf_.Get() + o, value, newl + 1);
        }
    }
}

const char *string_list::get(int idx) const
{
    if (WDL_NORMALLY(idx >= 0 && idx < offsets_.GetSize()))
    {
        const int o = offsets_.Get()[idx];
        if (WDL_NORMALLY(o >= 0 && o < buf_.GetSize()))
            return buf_.Get() + o;
    }
    return "";
}

int strToHex(const char *valueStr)
{
    return strtol(valueStr, NULL, 16);
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
    
    midi_Input *newInput = CreateMIDIInput(inputPort);
    
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
    
    midi_Output *newOutput = CreateMIDIOutput(outputPort, false, NULL);
    
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
    int refcnt;
    
    OSCSurfaceSocket(const string &name, oscpkt::UdpSocket *s)
    {
        surfaceName = name;
        socket = s;
        refcnt = 1;
    }
    ~OSCSurfaceSocket()
    {
      delete socket;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OSC I/O Manager
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static WDL_PtrList<OSCSurfaceSocket> s_inputSockets;
static WDL_PtrList<OSCSurfaceSocket> s_outputSockets;

static oscpkt::UdpSocket *GetInputSocketForPort(string surfaceName, int inputPort)
{
    for (int i = 0; i < s_inputSockets.GetSize(); ++i)
        if (s_inputSockets.Get(i)->surfaceName == surfaceName)
        {
            s_inputSockets.Get(i)->refcnt++;
            return s_inputSockets.Get(i)->socket; // return existing
        }
    
    // otherwise make new
    oscpkt::UdpSocket *newInputSocket = new oscpkt::UdpSocket();
    
    if (newInputSocket)
    {
        newInputSocket->bindTo(inputPort);
        
        if (! newInputSocket->isOk())
        {
            //cerr << "Error opening port " << PORT_NUM << ": " << inSocket_.errorMessage() << "\n";
            return NULL;
        }
        
        s_inputSockets.Add(new OSCSurfaceSocket(surfaceName, newInputSocket));
        return newInputSocket;
    }
    
    return NULL;
}

static oscpkt::UdpSocket *GetOutputSocketForAddressAndPort(const string &surfaceName, const string &address, int outputPort)
{
    for (int i = 0; i < s_outputSockets.GetSize(); ++i)
        if (s_outputSockets.Get(i)->surfaceName == surfaceName)
        {
            s_outputSockets.Get(i)->refcnt++;
            return s_outputSockets.Get(i)->socket; // return existing
        }
    
    // otherwise make new
    oscpkt::UdpSocket *newOutputSocket = new oscpkt::UdpSocket();
    
    if (newOutputSocket)
    {
        if ( ! newOutputSocket->connectTo(address, outputPort))
        {
            //cerr << "Error connecting " << remoteDeviceIP_ << ": " << outSocket_.errorMessage() << "\n";
            return NULL;
        }
        
        if ( ! newOutputSocket->isOk())
        {
            //cerr << "Error opening port " << outPort_ << ": " << outSocket_.errorMessage() << "\n";
            return NULL;
        }

        s_outputSockets.Add(new OSCSurfaceSocket(surfaceName, newOutputSocket));
        return newOutputSocket;
    }
    
    return NULL;
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
static void listFilesOfType(const string &path, string_list &results, const char *type)
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

static void PreProcessZoneFile(const char *filePath, ZoneManager *zoneManager)
{
    try
    {
        fpistream file(filePath);
        
        CSIZoneInfo *info = new CSIZoneInfo();
        info->filePath = filePath;
                 
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            if (line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
            
            string_list tokens;
            GetTokens(tokens, line.c_str());

            if (tokens[0] == "Zone" && tokens.size() > 1)
            {
                info->alias = tokens.size() > 2 ? tokens[2] : tokens[1];
                zoneManager->AddZoneFilePath(tokens[1].c_str(), info);
            }

            break;
        }
    }
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath, 1);
        ShowConsoleMsg(buffer);
    }
}

void ZoneManager::GetWidgetNameAndModifiers(const char *line, ActionTemplate *actionTemplate)
{
    string_list tokens;
    GetSubTokens(tokens, line, '+');
    ModifierManager modifierManager(NULL);
    
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
    
    tokens.erase(tokens.size() - 1);
    
    actionTemplate->modifier += modifierManager.GetModifierValue(tokens);
}

void ZoneManager::BuildActionTemplate(const string_list &tokens, WDL_StringKeyedArray<WDL_IntKeyedArray<WDL_PtrList<ActionTemplate>* >* > &actionTemplatesDictionary, bool isInAutoGeneratedSection)
{
    string feedbackIndicator;
    
    string_list params;
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

    WDL_IntKeyedArray<WDL_PtrList<ActionTemplate>* > *wr = actionTemplatesDictionary.Get(currentActionTemplate->widgetName.c_str());
    if (!wr)
    {
        wr = new WDL_IntKeyedArray<WDL_PtrList<ActionTemplate>* >(disposeActionTemplateList);
        actionTemplatesDictionary.Insert(currentActionTemplate->widgetName.c_str(), wr);
    }

    WDL_PtrList<ActionTemplate> *ml = wr->Get(currentActionTemplate->modifier);
    if (!ml)
    {
        ml = new WDL_PtrList<ActionTemplate>;
        wr->Insert(currentActionTemplate->modifier, ml);
    }

    ml->Add(currentActionTemplate);
    
    if(isInAutoGeneratedSection)
    {
        currentActionTemplate->provideFeedback = true;
    }
    else
    {
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
}

void ZoneManager::BuildFXTemplate(const string &layoutPath, const string &cellPath)
{
    ptrvector<FXCellLayoutInfo> fxLayouts;
    
    int numFXColumns_ = 0;

    try
    {
        fpistream file(layoutPath.c_str());
        
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
                    if (tokens[0] == "Channels")
                        numFXColumns_ = atoi(tokens[1].c_str());
                    else
                    {
                        for (int i = 1; i <= numFXColumns_; ++i )
                        {
                            FXCellLayoutInfo info;
                            
                            info.modifiers = tokens[0];
                            if (info.modifiers != "")
                                info.modifiers += "+";
                            info.address = tokens[1];
                            info.address += int_to_string(i);
                            fxLayouts.push_back(info);
                        }
                    }
                }
            }
        }
    }
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", layoutPath.c_str(), 1);
        ShowConsoleMsg(buffer);
    }

    if (fxLayouts.size() == 0) // didn't find any layouts
        return;
    
    paramWidgets_.clear();
    displayRows_.clear();
    ringStyles_.clear();
    fonts_.clear();
    
    string paramWidget = "";
    string nameWidget = "";
    string valueWidget = "";
    string widgetParams = "";
    string nameParams = "";
    string valueParams = "";
    
    try
    {
        fpistream file(cellPath.c_str());
        
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            if (line == "") // ignore blank lines
                continue;
        
            string_list tokens;
            GetTokens(tokens, line.c_str());

            if (tokens[0] != "Zone" && tokens[0] != "ZoneEnd")
            {
                if (tokens[0][0] == '#')
                {
                    if (tokens[0] == "#WidgetTypes")
                    {
                        for (int i = 1; i < tokens.size(); ++i)
                            paramWidgets_.push_back(tokens[i]);
                    }
                    else if (tokens[0] == "#DisplayRows")
                    {
                        for (int i = 1; i < tokens.size(); ++i)
                            displayRows_.push_back(tokens[i]);
                    }

                    else if (tokens[0] == "#RingStyles")
                    {
                        for (int i = 1; i < tokens.size(); ++i)
                            ringStyles_.push_back(tokens[i]);
                    }

                    else if (tokens[0] == "#DisplayFonts")
                    {
                        for (int i = 1; i < tokens.size(); ++i)
                            fonts_.push_back(tokens[i]);
                    }
                    else if (tokens[0] == "#SupportsColor")
                    {
                        hasColor_ = true;
                    }
                }
                else
                {
                    if (tokens.size() > 1 && tokens[1] == "FXParam")
                    {
                        paramWidget = tokens[0];
                        
                        if (tokens.size() > 2)
                            widgetParams = line.substr(line.find(tokens[2]), line.length() - 1);
                    }
                    if (tokens.size() > 1 && tokens[1] == "FixedTextDisplay")
                    {
                        nameWidget = tokens[0];
                        
                        if (tokens.size() > 2)
                            nameParams = line.substr(line.find(tokens[2]), line.length() - 1);
                    }
                    if (tokens.size() > 1 && tokens[1] == "FXParamValueDisplay")
                    {
                        valueWidget = tokens[0];
                        
                        if (tokens.size() > 2)
                            valueParams = line.substr(line.find(tokens[2]), line.length() - 1);
                    }
                }
            }
        }
    }
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", cellPath.c_str(), 1);
        ShowConsoleMsg(buffer);
    }
        
    fxRows_.clear();
    
    for (int i = 0; i < fxLayouts.size(); ++i)
    {
        FXCellRow row;
        
        row.modifiers = fxLayouts[i].modifiers;
        string_list modifierTokens;
        GetSubTokens(modifierTokens, row.modifiers.c_str(), '+');
        row.modifier = GetModifierValue(modifierTokens);

        row.address = fxLayouts[i].address;
        
        FXCell cell;

        cell.control = paramWidget;
        cell.controlParams = widgetParams;
        cell.nameDisplay = nameWidget;
        cell.nameDisplayAction = "FixedTextDisplay";
        cell.nameDisplayParams = nameParams;
        cell.valueDisplay = valueWidget;
        cell.valueDisplayAction = "FXParamValueDisplay";
        cell.valueDisplayParams = valueParams;

        row.cells.push_back(cell);
        
        for (int j = 0; j < paramWidgets_.size(); ++j)
        {
            if (paramWidgets_[j] != paramWidget)
            {
                FXCell cell2;
                
                cell2.control = paramWidgets_[j];

                if (displayRows_.size() > 1)
                {
                    cell2.nameDisplay = displayRows_[0];
                    cell2.nameDisplayAction = "NullDisplay";
                    cell2.valueDisplay = displayRows_[1];
                    cell2.valueDisplayAction = "NullDisplay";
                }
                
                row.cells.push_back(cell2);
            }
        }
        
        fxRows_.push_back(row);
    }
}

void ZoneManager::ProcessFXBoilerplate(const string &filePath, string_list &fxBoilerplate)
{
    try
    {
        fpistream file(filePath.c_str());
            
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            if (line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
        
            if (line.find("Zone") != 0)
                fxBoilerplate.push_back(line);
        }
    }
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath.c_str(), 1);
        ShowConsoleMsg(buffer);
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

    Zone::GCTagZone(homeZone_);
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

void ZoneManager::LoadZoneFile(const char *filePath, const WDL_PtrList<Navigator> &navigators, WDL_PtrList<Zone> &zones, Zone *enclosingZone)
{
    WDL_StringKeyedArray<WDL_IntKeyedArray<WDL_PtrList<ActionTemplate>* >* > actionTemplatesDictionary(true, disposeActionTemplates);

    bool isInAutoGeneratedSection = false;
    bool isInIncludedZonesSection = false;
    string_list includedZones;
    bool isInSubZonesSection = false;
    string_list subZones;
    bool isInAssociatedZonesSection = false;
    string_list associatedZones;
    
    string zoneName;
    string zoneAlias;
    int lineNumber = 0;
   
    try
    {
        fpistream file(filePath);
        
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            lineNumber++;
            
            if (line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
            
            if (line == s_BeginAutoSection)
            {
                isInAutoGeneratedSection = true;
                continue;
            }
                
            if (line == s_EndAutoSection)
            {
                isInAutoGeneratedSection = false;
                continue;
            }
            
            string_list tokens;
            GetTokens(tokens, line.c_str());

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
                        string numStr = int_to_string(i + 1);
                                                
                        Zone *zone;
                        
                        if (enclosingZone == NULL)
                            zone = new Zone(csi_, this, navigators.Get(i), i, zoneName, zoneAlias, filePath, includedZones, associatedZones);
                        else
                            zone = new SubZone(csi_, this, navigators.Get(i), i, zoneName, zoneAlias, filePath, includedZones, associatedZones, enclosingZone);

                        if (zoneName == "Home")
                            SetHomeZone(zone);
                                               
                        if (zoneName == "FocusedFXParam")
                            SetFocusedFXParamZone(zone);
                        
                        zones.Add(zone);
                        
                        for (int tde = 0; tde < actionTemplatesDictionary.GetSize(); tde++)
                        {
                            const char *widgetName = NULL;
                            WDL_IntKeyedArray<WDL_PtrList<ActionTemplate>* > *modifiedActionTemplates = actionTemplatesDictionary.Enumerate(tde,&widgetName);
                            if (WDL_NOT_NORMALLY(modifiedActionTemplates == NULL || widgetName == NULL)) continue;

                            string surfaceWidgetName = widgetName;
                            
                            if (navigators.GetSize() > 1)
                                ReplaceAllWith(surfaceWidgetName, "|", int_to_string(i + 1).c_str());
                            
                            if (enclosingZone != NULL && enclosingZone->GetChannelNumber() != 0)
                                ReplaceAllWith(surfaceWidgetName, "|", int_to_string(enclosingZone->GetChannelNumber()).c_str());
                            
                            Widget *widget = GetSurface()->GetWidgetByName(surfaceWidgetName.c_str());
                                                        
                            if (widget == NULL)
                                continue;
 
                            zone->AddWidget(widget);
                                                        
                            for (int ti = 0; ti < modifiedActionTemplates->GetSize(); ti ++)
                            {
                                int modifier=0;
                                WDL_PtrList<ActionTemplate> *actionTemplates = modifiedActionTemplates->Enumerate(ti,&modifier);
                                if (WDL_NOT_NORMALLY(actionTemplates == NULL)) continue;
                                for (int j = 0; j < actionTemplates->GetSize(); ++j)
                                {
                                    string actionName = actionTemplates->Get(j)->actionName;
                                    
                                    if (actionName == "NullDisplay")
                                        continue;                                    
                                    
                                    ReplaceAllWith(actionName, "|", numStr.c_str());

                                    string_list memberParams;
                                    for (int k = 0; k < actionTemplates->Get(j)->params.size(); k++)
                                    {
                                        string params = string(actionTemplates->Get(j)->params[k]);
                                        ReplaceAllWith(params, "|", numStr.c_str());
                                        memberParams.push_back(params);
                                    }
                                    
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
                                   
                                    if (learnFXTrack_ && ! (actionName == "NoAction" || actionName == "NullDisplay"))
                                        AddLearnedWidget(widget, actionTemplates->Get(j)->modifier, context->GetParamIndex());
                                    
                                    zone->AddActionContext(widget, modifier, context);
                                }
                            }
                        }
                    
                        if (enclosingZone == NULL && subZones.size() > 0)
                            zone->InitSubZones(subZones, zone);
                        allZonesNeedFree_.Add(zone);
                    }
                                    
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
                    BuildActionTemplate(tokens, actionTemplatesDictionary, isInAutoGeneratedSection);
            }
        }
    }
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath, lineNumber);
        ShowConsoleMsg(buffer);
    }
}

void ActionContext::GetColorValues(vector<rgba_color> &colorValues, const string_list &colors)
{
    for (int i = 0; i < (int)colors.size(); ++i)
    {
        rgba_color colorValue;
        
        if (GetColorValue(colors[i], colorValue))
            colorValues.push_back(colorValue);
    }
}

void ActionContext::SetColor(const string_list &params, bool &supportsColor, bool &supportsTrackColor, vector<rgba_color> &colorValues)
{
    vector<int> rawValues;
    string_list hexColors;

    int openCurlyIndex = 0;
    int closeCurlyIndex = 0;
    
    for (int i = 0; i < params.size(); ++i)
        if (params[i] == "{")
        {
            openCurlyIndex = i;
            break;
        }
    
    for (int i = 0; i < params.size(); ++i)
        if (params[i] == "}")
        {
            closeCurlyIndex = i;
            break;
        }
   
    if (openCurlyIndex != 0 && closeCurlyIndex != 0)
    {
        for (int i = openCurlyIndex + 1; i < closeCurlyIndex; ++i)
        {
            const char *strVal = params[i];
            
            if (strVal[0] == '#')
            {
                hexColors.push_back(strVal);
                continue;
            }
            
            if (!strcmp(strVal, "Track"))
            {
                supportsTrackColor = true;
                break;
            }
            else if (strVal[0])
            {
                char *ep = NULL;
                const int value = strtol(strVal, &ep, 10);
                if (ep && !*ep)
                    rawValues.push_back(wdl_clamp(value, 0, 255));
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

void ActionContext::GetSteppedValues(Widget *widget, Action *action,  Zone *zone, int paramNumber, const string_list &params, const PropertyList &widgetProperties, double &deltaValue, vector<double> &acceleratedDeltaValues, double &rangeMinimum, double &rangeMaximum, vector<double> &steppedValues, vector<int> &acceleratedTickValues)
{
    ::GetSteppedValues(params, 0, deltaValue, acceleratedDeltaValues, rangeMinimum, rangeMaximum, steppedValues, acceleratedTickValues);
    
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
void Midi_ControlSurface::ProcessMidiWidget(int &lineNumber, fpistream &surfaceTemplateFile, const string_list &in_tokens)
{
    if (in_tokens.size() < 2)
        return;
    
    const char *widgetName = in_tokens[1];
    
    string widgetClass;
    
    if (in_tokens.size() > 2)
        widgetClass = in_tokens[2];

    Widget *widget = new Widget(csi_, this, widgetName);
       
    AddWidget(widget);

    ptrvector<string_list> tokenLines;
    
    for (string line; getline(surfaceTemplateFile, line) ; )
    {
        TrimLine(line);
        
        lineNumber++;
        
        if (line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
            continue;
        
        string_list tokens;
        GetTokens(tokens, line.c_str());

        if (tokens[0] == "WidgetEnd")    // finito baybay - Widget list complete
            break;
        
        tokenLines.push_back(tokens);
    }
    
    if (tokenLines.size() < 1)
        return;
    
    for (int i = 0; i < tokenLines.size(); i++)
    {
        int size = (int)tokenLines[i].size();
        
        const string_list::string_ref widgetType = tokenLines[i][0];

        MIDI_event_ex_t *message1 = NULL;
        MIDI_event_ex_t *message2 = NULL;

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
        else if (widgetType == "Fader14Bit" && size == 4)
            AddCSIMessageGenerator(oneByteKey, new Fader14Bit_Midi_CSIMessageGenerator(csi_, widget));
        else if (widgetType == "FaderportClassicFader14Bit" && size == 7 && message1 && message2)
            AddCSIMessageGenerator(oneByteKey, new FaderportClassicFader14Bit_Midi_CSIMessageGenerator(csi_, widget, message1, message2));
        else if (widgetType == "Fader7Bit" && size== 4)
            AddCSIMessageGenerator(twoByteKey, new Fader7Bit_Midi_CSIMessageGenerator(csi_, widget));
        else if (widgetType == "Encoder" && widgetClass == "RotaryWidgetClass")
            AddCSIMessageGenerator(twoByteKey, new AcceleratedPreconfiguredEncoder_Midi_CSIMessageGenerator(csi_, widget));
        else if (widgetType == "Encoder" && size == 4)
            AddCSIMessageGenerator(twoByteKey, new Encoder_Midi_CSIMessageGenerator(csi_, widget));
        else if (widgetType == "MFTEncoder" && size > 4)
            AddCSIMessageGenerator(twoByteKey, new MFT_AcceleratedEncoder_Midi_CSIMessageGenerator(csi_, widget, tokenLines[i]));
        else if (widgetType == "EncoderPlain" && size == 4)
            AddCSIMessageGenerator(twoByteKey, new EncoderPlain_Midi_CSIMessageGenerator(csi_, widget));
        else if (widgetType == "Encoder7Bit" && size == 4)
            AddCSIMessageGenerator(twoByteKey, new Encoder7Bit_Midi_CSIMessageGenerator(csi_, widget));
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
            feedbackProcessor = new FPValueBar_Midi_FeedbackProcessor(csi_, this, widget, atoi(tokenLines[i][1].c_str()));
        }
        else if ((widgetType == "FB_FPVUMeter") && size == 2)
        {
            feedbackProcessor = new FPVUMeter_Midi_FeedbackProcessor(csi_, this, widget, atoi(tokenLines[i][1].c_str()));
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
            feedbackProcessor = new QConProXMasterVUMeter_Midi_FeedbackProcessor(csi_, this, widget, atoi(tokenLines[i][1].c_str()));
        }
        else if ((widgetType == "FB_MCUVUMeter" || widgetType == "FB_MCUXTVUMeter") && size == 2)
        {
            int displayType = widgetType == "FB_MCUVUMeter" ? 0x14 : 0x15;
            
            feedbackProcessor = new MCUVUMeter_Midi_FeedbackProcessor(csi_, this, widget, displayType, atoi(tokenLines[i][1].c_str()));
            
            SetHasMCUMeters(displayType);
        }
        else if ((widgetType == "FB_AsparionVUMeterL" || widgetType == "FB_AsparionVUMeterR") && size == 2)
        {
            bool isRight = widgetType == "FB_AsparionVUMeterR" ? true : false;
            
            feedbackProcessor = new AsparionVUMeter_Midi_FeedbackProcessor(csi_, this, widget, 0x14, atoi(tokenLines[i][1].c_str()), isRight);
            
            SetHasMCUMeters(0x14);
        }
        else if (widgetType == "FB_SCE24LEDButton" && size == 4)
        {
            feedbackProcessor = new SCE24TwoStateLED_Midi_FeedbackProcessor(csi_, this, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]) + 0x60, strToHex(tokenLines[i][3])));
        }
        else if (widgetType == "FB_SCE24OLEDButton" && size == 7)
        {
            feedbackProcessor = new SCE24OLED_Midi_FeedbackProcessor(csi_, this, widget, new MIDI_event_ex_t(strToHex(tokenLines[i][1]), strToHex(tokenLines[i][2]) + 0x60, strToHex(tokenLines[i][3])), atoi(tokenLines[i][4]), atoi(tokenLines[i][5]), atoi(tokenLines[i][6]));
        }
        else if (widgetType == "FB_SCE24Encoder" && size == 4 && message1)
        {
            feedbackProcessor = new SCE24Encoder_Midi_FeedbackProcessor(csi_, this, widget, message1);
        }
        else if (widgetType == "FB_SCE24EncoderText" && size == 7 && message1)
        {
            feedbackProcessor = new SCE24Text_Midi_FeedbackProcessor(csi_, this, widget, message1, atoi(tokenLines[i][4]), atoi(tokenLines[i][5]), atoi(tokenLines[i][6]));
        }
        else if ((widgetType == "FB_MCUDisplayUpper" || widgetType == "FB_MCUDisplayLower" || widgetType == "FB_MCUXTDisplayUpper" || widgetType == "FB_MCUXTDisplayLower") && size == 2)
        {
            if (widgetType == "FB_MCUDisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0, 0x14, 0x12, atoi(tokenLines[i][1].c_str()));
            else if (widgetType == "FB_MCUDisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(csi_, this, widget, 1, 0x14, 0x12, atoi(tokenLines[i][1].c_str()));
            else if (widgetType == "FB_MCUXTDisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0, 0x15, 0x12, atoi(tokenLines[i][1].c_str()));
            else if (widgetType == "FB_MCUXTDisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(csi_, this, widget, 1, 0x15, 0x12, atoi(tokenLines[i][1].c_str()));
        }
        else if ((widgetType == "FB_IconDisplay1Upper" || widgetType == "FB_IconDisplay1Lower" || widgetType == "FB_IconDisplay2Upper" || widgetType == "FB_IconDisplay2Lower") && size == 2)
        {
            if (widgetType == "FB_IconDisplay1Upper")
                feedbackProcessor = new IconDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0, 0x14, 0x12, atoi(tokenLines[i][1].c_str()), 0x00, 0x66);
            else if (widgetType == "FB_IconDisplay1Lower")
                feedbackProcessor = new IconDisplay_Midi_FeedbackProcessor(csi_, this, widget, 1, 0x14, 0x12, atoi(tokenLines[i][1].c_str()), 0x00, 0x66);
            else if (widgetType == "FB_IconDisplay2Upper")
                feedbackProcessor = new IconDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0, 0x15, 0x13, atoi(tokenLines[i][1].c_str()), 0x02, 0x4e);
            else if (widgetType == "FB_IconDisplay2Lower")
                feedbackProcessor = new IconDisplay_Midi_FeedbackProcessor(csi_, this, widget, 1, 0x15, 0x13, atoi(tokenLines[i][1].c_str()), 0x02, 0x4e);
        }
        else if ((widgetType == "FB_AsparionDisplayUpper" || widgetType == "FB_AsparionDisplayLower" || widgetType == "FB_AsparionDisplayEncoder") && size == 2)
        {
            if (widgetType == "FB_AsparionDisplayUpper")
                feedbackProcessor = new AsparionDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x01, 0x14, 0x1A, atoi(tokenLines[i][1].c_str()));
            else if (widgetType == "FB_AsparionDisplayLower")
                feedbackProcessor = new AsparionDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x02, 0x14, 0x1A, atoi(tokenLines[i][1].c_str()));
            else if (widgetType == "FB_AsparionDisplayEncoder")
                feedbackProcessor = new AsparionDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x03, 0x14, 0x19, atoi(tokenLines[i][1].c_str()));
        }
        else if ((widgetType == "FB_XTouchDisplayUpper" || widgetType == "FB_XTouchDisplayLower" || widgetType == "FB_XTouchXTDisplayUpper" || widgetType == "FB_XTouchXTDisplayLower") && size == 2)
        {
            if (widgetType == "FB_XTouchDisplayUpper")
                feedbackProcessor = new XTouchDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0, 0x14, 0x12, atoi(tokenLines[i][1].c_str()));
            else if (widgetType == "FB_XTouchDisplayLower")
                feedbackProcessor = new XTouchDisplay_Midi_FeedbackProcessor(csi_, this, widget, 1, 0x14, 0x12, atoi(tokenLines[i][1].c_str()));
            else if (widgetType == "FB_XTouchXTDisplayUpper")
                feedbackProcessor = new XTouchDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0, 0x15, 0x12, atoi(tokenLines[i][1].c_str()));
            else if (widgetType == "FB_XTouchXTDisplayLower")
                feedbackProcessor = new XTouchDisplay_Midi_FeedbackProcessor(csi_, this, widget, 1, 0x15, 0x12, atoi(tokenLines[i][1].c_str()));
            
            if (feedbackProcessor)
                AddTrackColorFeedbackProcessor(feedbackProcessor);
        }
        else if ((widgetType == "FB_C4DisplayUpper" || widgetType == "FB_C4DisplayLower") && size == 3)
        {
            if (widgetType == "FB_C4DisplayUpper")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0, 0x17, atoi(tokenLines[i][1].c_str()) + 0x30, atoi(tokenLines[i][2].c_str()));
            else if (widgetType == "FB_C4DisplayLower")
                feedbackProcessor = new MCUDisplay_Midi_FeedbackProcessor(csi_, this, widget, 1, 0x17, atoi(tokenLines[i][1].c_str()) + 0x30, atoi(tokenLines[i][2].c_str()));
        }
        else if ((widgetType == "FB_FP8ScribbleLine1" || widgetType == "FB_FP16ScribbleLine1"
                 || widgetType == "FB_FP8ScribbleLine2" || widgetType == "FB_FP16ScribbleLine2"
                 || widgetType == "FB_FP8ScribbleLine3" || widgetType == "FB_FP16ScribbleLine3"
                 || widgetType == "FB_FP8ScribbleLine4" || widgetType == "FB_FP16ScribbleLine4") && size == 2)
        {
            if (widgetType == "FB_FP8ScribbleLine1")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x02, atoi(tokenLines[i][1].c_str()), 0x00);
            else if (widgetType == "FB_FP8ScribbleLine2")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x02, atoi(tokenLines[i][1].c_str()), 0x01);
            else if (widgetType == "FB_FP8ScribbleLine3")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x02, atoi(tokenLines[i][1].c_str()), 0x02);
            else if (widgetType == "FB_FP8ScribbleLine4")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x02, atoi(tokenLines[i][1].c_str()), 0x03);

            else if (widgetType == "FB_FP16ScribbleLine1")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x16, atoi(tokenLines[i][1].c_str()), 0x00);
            else if (widgetType == "FB_FP16ScribbleLine2")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x16, atoi(tokenLines[i][1].c_str()), 0x01);
            else if (widgetType == "FB_FP16ScribbleLine3")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x16, atoi(tokenLines[i][1].c_str()), 0x02);
            else if (widgetType == "FB_FP16ScribbleLine4")
                feedbackProcessor = new FPDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0x16, atoi(tokenLines[i][1].c_str()), 0x03);
        }
        else if ((widgetType == "FB_FP8ScribbleStripMode" || widgetType == "FB_FP16ScribbleStripMode") && size == 2)
        {
            if (widgetType == "FB_FP8ScribbleStripMode")
                feedbackProcessor = new FPScribbleStripMode_Midi_FeedbackProcessor(csi_, this, widget, 0x02, atoi(tokenLines[i][1].c_str()));
            else if (widgetType == "FB_FP16ScribbleStripMode")
                feedbackProcessor = new FPScribbleStripMode_Midi_FeedbackProcessor(csi_, this, widget, 0x16, atoi(tokenLines[i][1].c_str()));
        }
        else if ((widgetType == "FB_QConLiteDisplayUpper" || widgetType == "FB_QConLiteDisplayUpperMid" || widgetType == "FB_QConLiteDisplayLowerMid" || widgetType == "FB_QConLiteDisplayLower") && size == 2)
        {
            if (widgetType == "FB_QConLiteDisplayUpper")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(csi_, this, widget, 0, 0x14, 0x12, atoi(tokenLines[i][1].c_str()));
            else if (widgetType == "FB_QConLiteDisplayUpperMid")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(csi_, this, widget, 1, 0x14, 0x12, atoi(tokenLines[i][1].c_str()));
            else if (widgetType == "FB_QConLiteDisplayLowerMid")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(csi_, this, widget, 2, 0x14, 0x12, atoi(tokenLines[i][1].c_str()));
            else if (widgetType == "FB_QConLiteDisplayLower")
                feedbackProcessor = new QConLiteDisplay_Midi_FeedbackProcessor(csi_, this, widget, 3, 0x14, 0x12, atoi(tokenLines[i][1].c_str()));
        }

        if (feedbackProcessor != NULL)
            widget->AddFeedbackProcessor(feedbackProcessor);
    }
}

void OSC_ControlSurface::ProcessOSCWidget(int &lineNumber, fpistream &surfaceTemplateFile, const string_list &in_tokens)
{
    if (in_tokens.size() < 2)
        return;
    
    Widget *widget = new Widget(csi_, this, in_tokens[1]);
    
    AddWidget(widget);

    ptrvector<string_list> tokenLines;

    for (string line; getline(surfaceTemplateFile, line) ; )
    {
        lineNumber++;
        
        if (line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
            continue;
        
        string_list tokens;
        GetTokens(tokens, line.c_str());

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
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "FB_X32Processor")
            widget->AddFeedbackProcessor(new OSC_X32FeedbackProcessor(csi_, this, widget, tokenLines[i][1]));
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "FB_X32IntProcessor")
            widget->AddFeedbackProcessor(new OSC_X32IntFeedbackProcessor(csi_, this, widget, tokenLines[i][1]));
    }
}

void ControlSurface::ProcessValues(const ptrvector<string_list> &lines)
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
                const char *widgetClass = lines[i].get(0);
                
                if (inStepSizes)
                    stepSize_.Insert(widgetClass, atof(lines[i][1].c_str()));
                else if (lines[i].size() > 2 && inAccelerationValues)
                {
                    
                    if (lines[i][1] == "Dec")
                        for (int j = 2; j < lines[i].size(); j++)
                        {
                            if ( ! accelerationValuesForDecrement_.Exists(widgetClass))
                                accelerationValuesForDecrement_.Insert(widgetClass, new WDL_IntKeyedArray<int>());
                            
                            if (accelerationValuesForDecrement_.Exists(widgetClass))
                                accelerationValuesForDecrement_.Get(widgetClass)->Insert(strtol(lines[i][j].c_str(), NULL, 16), j - 2);
                        }
                    else if (lines[i][1] == "Inc")
                        for (int j = 2; j < lines[i].size(); j++)
                        {
                            if ( ! accelerationValuesForIncrement_.Exists(widgetClass))
                                accelerationValuesForIncrement_.Insert(widgetClass, new WDL_IntKeyedArray<int>());
                            
                            if (accelerationValuesForIncrement_.Exists(widgetClass))
                                accelerationValuesForIncrement_.Get(widgetClass)->Insert(strtol(lines[i][j].c_str(), NULL, 16), j - 2);
                        }
                    else if (lines[i][1] == "Val")
                        for (int j = 2; j < lines[i].size(); j++)
                        {
                            if ( ! accelerationValues_.Exists(widgetClass))
                                accelerationValues_.Insert(widgetClass, new vector<double>());
                            
                            accelerationValues_.Get(widgetClass)->push_back(atof(lines[i][j].c_str()));
                        }
                }
            }
        }
    }
}

void Midi_ControlSurface::ProcessMIDIWidgetFile(const string &filePath, Midi_ControlSurface *surface)
{
    int lineNumber = 0;
    ptrvector<string_list> valueLines;
    
    stepSize_.DeleteAll();
    accelerationValuesForDecrement_.DeleteAll();
    accelerationValuesForIncrement_.DeleteAll();
    accelerationValues_.DeleteAll();

    try
    {
        fpistream file(filePath.c_str());
        
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            lineNumber++;
            
            if (line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
                continue;
            
            string_list tokens;
            GetTokens(tokens, line.c_str());

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
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath.c_str(), lineNumber);
        ShowConsoleMsg(buffer);
    }
}

void OSC_ControlSurface::ProcessOSCWidgetFile(const string &filePath)
{
    int lineNumber = 0;
    ptrvector<string_list> valueLines;
    
    stepSize_.DeleteAll();
    accelerationValuesForDecrement_.DeleteAll();
    accelerationValuesForIncrement_.DeleteAll();
    accelerationValues_.DeleteAll();

    try
    {
        fpistream file(filePath.c_str());
        
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            lineNumber++;
            
            if (line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
                continue;
            
            string_list tokens;
            GetTokens(tokens, line.c_str());

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
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", filePath.c_str(), lineNumber);
        ShowConsoleMsg(buffer);
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
    actions_.Insert("RemapZone", new RemapZone());
    actions_.Insert("AutoLearnFocusedFX", new AutoLearnFocusedFX());
    actions_.Insert("GoAssociatedZone", new GoAssociatedZone());
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
    actions_.Insert("X32TrackVolume", new X32TrackVolume());
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
    actions_.Insert("TrackInvertPolarityDisplay", new TrackInvertPolarityDisplay());
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
}

void CSurfIntegrator::Init()
{
    pages_.Empty(true);
    
    string currentBroadcaster;
    
    Page *currentPage = NULL;
    
    string CSIFolderPath = string(GetResourcePath()) + "/CSI";
    
    WDL_DirScan ds;
    if (ds.First(CSIFolderPath.c_str()))
    {       
        char tmp[BUFSZ];
        snprintf(tmp, sizeof(tmp), __LOCALIZE_VERFMT("Please check your installation, cannot find %s", "csi_mbox"), CSIFolderPath.c_str());
        MessageBox(g_hwnd, tmp, __LOCALIZE("Missing CSI Folder","csi_mbox"), MB_OK);
        
        return;
    }
    
    string iniFilePath = string(GetResourcePath()) + "/CSI/CSI.ini";
    int lineNumber = 0;
    
    try
    {
        fpistream iniFile(iniFilePath.c_str());
               
        for (string line; getline(iniFile, line) ; )
        {
            TrimLine(line);
            
            if (lineNumber == 0)
            {
                if (line != s_MajorVersionToken)
                {
                    char tmp[BUFSZ];
                    snprintf(tmp, sizeof(tmp), __LOCALIZE_VERFMT("Version mismatch -- Your CSI.ini file is not %s.","csi_mbox"), s_MajorVersionToken);
                    MessageBox(g_hwnd, tmp, __LOCALIZE("CSI.ini version mismatch","csi_mbox"), MB_OK);

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
            
            string_list tokens;
            GetTokens(tokens, line.c_str());
            
            if (tokens.size() > 1) // ignore comment lines and blank lines
            {
                if (tokens[0] == s_MidiSurfaceToken && tokens.size() == 5)
                    midiSurfacesIO_.Add(new Midi_ControlSurfaceIO(this, tokens[1], GetMidiInputForPort(atoi(tokens[2].c_str())), GetMidiOutputForPort(atoi(tokens[3].c_str())), atoi(tokens[4])));
                else if (tokens[0] == s_OSCSurfaceToken && tokens.size() == 6)
                    oscSurfacesIO_.Add(new OSC_ControlSurfaceIO(this, tokens[1], tokens[2], tokens[3], tokens[4], atoi(tokens[5].c_str())));
                else if (tokens[0] == s_OSCX32SurfaceToken && tokens.size() == 6)
                    oscSurfacesIO_.Add(new OSC_X32ControlSurfaceIO(this, tokens[1], tokens[2], tokens[3], tokens[4], atoi(tokens[5].c_str())));
                else if (tokens[0] == s_PageToken)
                {
                    bool followMCP = true;
                    bool synchPages = true;
                    bool isScrollLinkEnabled = false;
                    bool isScrollSynchEnabled = false;

                    currentPage = NULL;
                    
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
                    ControlSurface *broadcaster = NULL;
                    ControlSurface *listener = NULL;

                    const WDL_PtrList<ControlSurface> &list = currentPage->GetSurfaces();
                    
                    string currentSurface = string(tokens[1]);
                    
                    for (int i = 0; i < list.GetSize(); ++i)
                    {
                        if (list.Get(i)->GetName() == currentBroadcaster)
                            broadcaster = list.Get(i);
                        if (list.Get(i)->GetName() == currentSurface)
                            listener = list.Get(i);
                    }
                    
                    if (broadcaster != NULL && listener != NULL)
                    {
                        broadcaster->GetZoneManager()->AddListener(listener);
                        listener->GetZoneManager()->SetListenerCategories(tokens[2]);
                    }
                }
                else
                {
                    if (currentPage && (tokens.size() == 6 || tokens.size() == 7))
                    {
                        string_list::string_ref zoneFolder = tokens[4];
                        string_list::string_ref fxZoneFolder = tokens[5];
                        
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
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", iniFilePath.c_str(), lineNumber);
        ShowConsoleMsg(buffer);
    }
    
    for (int i = 0; i < pages_.GetSize(); ++i)
    {
        for (int j = 0; j < pages_.Get(i)->GetSurfaces().GetSize(); ++j)
            pages_.Get(i)->GetSurfaces().Get(j)->ForceClear();

        pages_.Get(i)->OnInitialization();
    }
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
    return GetMasterTrack(NULL);
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
    int takeNumber = 0;
    int fxIndex = 0;
    int paramIndex = 0;
    
    int retVal = GetTouchedOrFocusedFX(1, &trackNumber, &itemNumber, &takeNumber, &fxIndex, &paramIndex);

    trackNumber++;
    
    if (retVal && ! (paramIndex & 0x01)) // Track FX
    {
        if (trackNumber > 0)
            return DAW::GetTrack(trackNumber);
        else if (trackNumber == 0)
            return GetMasterTrack(NULL);
        else
            return NULL;
    }
    else
        return NULL;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ActionContext
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ActionContext::ActionContext(CSurfIntegrator *const csi, Action *action, Widget *widget, Zone *zone, int paramIndex, const string_list *paramsAndProperties, const string *stringParam): csi_(csi), action_(action), widget_(widget), zone_(zone)
{
    intParam_ = 0;
    supportsColor_ = false;
    supportsTrackColor_ = false;
    provideFeedback_ = false;
    
    if (stringParam != NULL)
        stringParam_ = *stringParam;
    
    paramIndex_ = paramIndex;
    
    commandId_ = 0;
    
    rangeMinimum_ = 0.0;
    rangeMaximum_ = 1.0;
    
    steppedValuesIndex_ = 0;
    
    deltaValue_ = 0.0;
    accumulatedIncTicks_ = 0;
    accumulatedDecTicks_ = 0;
    
    isValueInverted_ = false;
    isFeedbackInverted_ = false;
    holdDelayAmount_ = 0;
    delayStartTimeValid_ = false;
    delayStartTime_ = 0;
    deferredValue_ = 0.0;
    
    supportsColor_ = false;
    currentColorIndex_ = 0;
    
    supportsTrackColor_ = false;
        
    provideFeedback_ = false;
    
    string_list params_wr;
    const string_list &params = params_wr;
    
    if (paramsAndProperties != NULL)
    {
        for (int i = 0; i < (int)(*paramsAndProperties).size(); ++i)
        {
            if ((*paramsAndProperties)[i].find("=") == string::npos)
                params_wr.push_back((*paramsAndProperties)[i]);
        }
        GetPropertiesFromTokens(0, (int)(*paramsAndProperties).size(), *paramsAndProperties, widgetProperties_);
    }

    const char *actionName = "";
    
    if (params.size() > 0)
        actionName = params[0];
    
    // Action with int param, could include leading minus sign
    if (params.size() > 1 && (isdigit(params[1][0]) ||  params[1][0] == '-'))  // C++ 11 says empty strings can be queried without catastrophe :)
    {
        intParam_= atol(params[1].c_str());
    }
    
    if (!strcmp(actionName, "Bank") && (params.size() > 2 && (isdigit(params[2][0]) ||  params[2][0] == '-')))  // C++ 11 says empty strings can be queried without catastrophe :)
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
    
    if (!strcmp(actionName, "TrackVolumeDB") || !strcmp(actionName, "TrackSendVolumeDB"))
    {
        rangeMinimum_ = -144.0;
        rangeMaximum_ = 24.0;
    }
    
    if (!strcmp(actionName, "TrackPanPercent") ||
        !strcmp(actionName, "TrackPanWidthPercent") ||
        !strcmp(actionName, "TrackPanLPercent") ||
        !strcmp(actionName, "TrackPanRPercent"))
    {
        rangeMinimum_ = -100.0;
        rangeMaximum_ = 100.0;
    }
   
    if ((!strcmp(actionName, "Reaper") ||
         !strcmp(actionName, "ReaperDec") ||
         !strcmp(actionName, "ReaperInc")) && params.size() > 1)
    {
        if (isdigit(params[1][0]))
        {
            commandId_ =  atol(params[1].c_str());
        }
        else // look up by string
        {
            commandId_ = NamedCommandLookup(params[1].c_str());
            
            if (commandId_ == 0) // can't find it
                commandId_ = 65535; // no-op
        }
    }
        
    if ((!strcmp(actionName, "FXParam") || !strcmp(actionName, "JSFXParam")) &&
          params.size() > 1 && isdigit(params[1][0]))
    {
        paramIndex_ = atol(params[1].c_str());
    }
    
    if (!strcmp(actionName, "FXParamValueDisplay") && params.size() > 1 && isdigit(params[1][0]))
    {
        paramIndex_ = atol(params[1].c_str());
    }
    
    if (!strcmp(actionName, "FXParamNameDisplay") && params.size() > 1 && isdigit(params[1][0]))
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
    return zone_->GetNameOrAlias();
}

void ActionContext::RunDeferredActions()
{
    if (holdDelayAmount_ != 0 && delayStartTimeValid_ && (GetTickCount() - delayStartTime_) > holdDelayAmount_)
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

        delayStartTimeValid_ = false;
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
    if (holdDelayAmount_ != 0)
    {
        if (value == 0.0)
        {
            deferredValue_ = 0.0;
            delayStartTimeValid_ = false;
        }
        else
        {
            deferredValue_ = value;
            delayStartTime_ = GetTickCount();
            delayStartTimeValid_ = true;
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
Zone::Zone(CSurfIntegrator *const csi, ZoneManager  *const zoneManager, Navigator *navigator, int slotIndex, string name, string alias, string sourceFilePath, string_list includedZones, string_list associatedZones): csi_(csi), zoneManager_(zoneManager), navigator_(navigator), slotIndex_(slotIndex), name_(name), alias_(alias), sourceFilePath_(sourceFilePath), subZones_(true,disposeZoneRefList), associatedZones_(true,disposeZoneRefList), /* learnFXCells_(destroyLearnFXCellList),*/ actionContextDictionary_(destroyActionContextListArray)
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
                    zoneManager_->LoadZoneFile(zoneManager_->GetZoneFilePaths().Get(associatedZones[i].c_str())->filePath.c_str(), navigators, *az, NULL);
                }
                else
                {
                    char buffer[250];
                    snprintf(buffer, sizeof(buffer), "Duplicate definition of %s, it has already been loaded.\n", associatedZones[i].c_str());
                    ShowConsoleMsg(buffer);
                }
            }
        }
    }
    
    for (int i = 0; i < (int)includedZones.size(); ++i)
    {
        if (zoneManager_->GetZoneFilePaths().Exists(includedZones[i].c_str()))
        {
            WDL_PtrList<Navigator> navigators;
            AddNavigatorsForZone(includedZones[i], navigators);
            
            zoneManager_->LoadZoneFile(zoneManager_->GetZoneFilePaths().Get(includedZones[i].c_str())->filePath.c_str(), navigators, includedZones_, NULL);
        }
    }
}

void Zone::InitSubZones(const string_list &subZones, Zone *enclosingZone)
{
    for (int i = 0; i < (int)subZones.size(); ++i)
    {
        if (zoneManager_->GetZoneFilePaths().Exists(subZones[i].c_str()))
        {
            WDL_PtrList<Navigator> navigators;
            navigators.Add(GetNavigator());

            WDL_PtrList<Zone> *sz = subZones_.Get(subZones[i].c_str());
            if (WDL_NORMALLY(sz == NULL))
            {
                sz = new WDL_PtrList<Zone>;
                subZones_.Insert(subZones[i].c_str(), sz);
                zoneManager_->LoadZoneFile(zoneManager_->GetZoneFilePaths().Get(subZones[i].c_str())->filePath.c_str(), navigators, *sz, enclosingZone);
            }
            else
            {
                char buffer[250];
                snprintf(buffer, sizeof(buffer), "Duplicate definition of %s, it has already been loaded.\n", subZones[i].c_str());
                ShowConsoleMsg(buffer);
            }
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

int Zone::GetParamIndex(const char *widgetName)
{
    Widget *w = widgetsByName_.Get(widgetName);
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

void Zone::DeactivateLearnFocusedFXZone()
{
    WDL_PtrList<Zone> *learnFXParams = associatedZones_.Get("LearnFocusedFX");
    for (int i = 0; i < learnFXParams->GetSize(); ++i)
        learnFXParams->Get(i)->Deactivate();
}

void Zone::AddWidget(Widget *widget)
{
    thisZoneWidgets_.Add(widget);
    widgets_.Insert(widget, true);
    widgetsByName_.Insert(widget->GetName(), widget);
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

void Zone::AddNavigatorsForZone(const char *zoneName, WDL_PtrList<Navigator> &navigators)
{
    if (!strcmp(zoneName, "MasterTrack"))
        navigators.Add(zoneManager_->GetMasterTrackNavigator());
    else if (!strcmp(zoneName, "Track") ||
             !strcmp(zoneName, "VCA") || 
             !strcmp(zoneName, "Folder") ||
             !strcmp(zoneName, "SelectedTracks") ||
             !strcmp(zoneName, "TrackSend") ||
             !strcmp(zoneName, "TrackReceive") ||
             !strcmp(zoneName, "TrackFXMenu"))
    {
        for (int i = 0; i < zoneManager_->GetNumChannels(); i++)
        {
            Navigator *channelNavigator = zoneManager_->GetSurface()->GetPage()->GetNavigatorForChannel(i + zoneManager_->GetSurface()->GetChannelOffset());
            if (channelNavigator)
                navigators.Add(channelNavigator);
        }
    }
    else if (!strcmp(zoneName, "SelectedTrack") ||
             !strcmp(zoneName, "SelectedTrackSend") ||
             !strcmp(zoneName, "SelectedTrackReceive") ||
             !strcmp(zoneName, "SelectedTrackFXMenu"))
        for (int i = 0; i < zoneManager_->GetNumChannels(); i++)
            navigators.Add(zoneManager_->GetSelectedTrackNavigator());
    else if (!strcmp(zoneName, "MasterTrackFXMenu"))
        for (int i = 0; i < zoneManager_->GetNumChannels(); i++)
            navigators.Add(zoneManager_->GetMasterTrackNavigator());
    else
        navigators.Add(zoneManager_->GetSelectedTrackNavigator());
}

void Zone::SetXTouchDisplayColors(const char *color)
{
    for (int wi = 0; wi < widgets_.GetSize(); wi ++)
    {
        Widget *widget = NULL;
        if (WDL_NOT_NORMALLY(!widgets_.EnumeratePtr(wi,&widget) || !widget)) break;
        widget->SetXTouchDisplayColors(name_.c_str(), color);
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
        if (g_surfaceInDisplay)
        {
            char buffer[250];
            snprintf(buffer, sizeof(buffer), "Zone -- %s\n\n", sourceFilePath_.c_str());
            ShowConsoleMsg(buffer);
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

void Zone::DoRelativeAction(Widget *widget, bool &isUsed, double delta)
{
    if (! isActive_ || isUsed)
        return;
    
    for (int j = 0; j < subZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = subZones_.Enumerate(j);
        if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->DoRelativeAction(widget, isUsed, delta);
    }

    for (int j = 0; j < associatedZones_.GetSize(); j++)
    {
        WDL_PtrList<Zone> *zones = associatedZones_.Enumerate(j);
        if (WDL_NORMALLY(zones != NULL)) for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->DoRelativeAction(widget, isUsed, delta);
    }

    if (isUsed)
        return;

    if (widgets_.Exists(widget))
    {
        if (g_surfaceInDisplay)
        {
            char buffer[250];
            snprintf(buffer, sizeof(buffer), "Zone -- %s\n\n", sourceFilePath_.c_str());
            ShowConsoleMsg(buffer);
        }

        isUsed = true;

        for (int i = 0; i < GetActionContexts(widget).GetSize(); ++i)
            GetActionContexts(widget).Get(i)->DoRelativeAction(delta);
    }
    else
    {
        for (int i = 0; i < includedZones_.GetSize(); ++i)
            includedZones_.Get(i)->DoRelativeAction(widget, isUsed, delta);
    }
}

void Zone::DoRelativeAction(Widget *widget, bool &isUsed, int accelerationIndex, double delta)
{
    if (! isActive_ || isUsed)
        return;

    for (int j = 0; j < subZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = subZones_.Enumerate(j);
        for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
    }
    
    for (int j = 0; j < associatedZones_.GetSize(); j++)
    {
        WDL_PtrList<Zone> *zones = associatedZones_.Enumerate(j);
        for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
    }

    if (isUsed)
        return;

    if (widgets_.Exists(widget))
    {
        if (g_surfaceInDisplay)
        {
            char buffer[250];
            snprintf(buffer, sizeof(buffer), "Zone -- %s\n\n", sourceFilePath_.c_str());
            ShowConsoleMsg(buffer);
        }

        isUsed = true;

        for (int i = 0; i < GetActionContexts(widget).GetSize(); ++i)
            GetActionContexts(widget).Get(i)->DoRelativeAction(accelerationIndex, delta);
    }
    else
    {
        for (int i = 0; i < includedZones_.GetSize(); ++i)
            includedZones_.Get(i)->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
    }
}

void Zone::DoTouch(Widget *widget, const char *widgetName, bool &isUsed, double value)
{
    if (! isActive_ || isUsed)
        return;

    for (int j = 0; j < subZones_.GetSize(); j ++)
    {
        WDL_PtrList<Zone> *zones = subZones_.Enumerate(j);
        for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->DoTouch(widget, widgetName, isUsed, value);
    }
    
    for (int j = 0; j < associatedZones_.GetSize(); j++)
    {
        WDL_PtrList<Zone> *zones = associatedZones_.Enumerate(j);
        for (int i = 0; i < zones->GetSize(); ++i)
            zones->Get(i)->DoTouch(widget, widgetName, isUsed, value);
    }

    if (isUsed)
        return;

    if (widgets_.Exists(widget))
    {
        if (g_surfaceInDisplay)
        {
            char buffer[250];
            snprintf(buffer, sizeof(buffer), "Zone -- %s\n\n", sourceFilePath_.c_str());
            ShowConsoleMsg(buffer);
        }

        isUsed = true;

        for (int i = 0; i < GetActionContexts(widget).GetSize(); ++i)
            GetActionContexts(widget).Get(i)->DoTouch(value);
    }
    else
    {
        for (int i = 0; i < includedZones_.GetSize(); ++i)
            includedZones_.Get(i)->DoTouch(widget, widgetName, isUsed, value);
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

void  Widget::UpdateValue(const PropertyList &properties, const char * const &value)
{
    for (int i = 0; i < feedbackProcessors_.GetSize(); ++i)
        feedbackProcessors_.Get(i)->SetValue(properties, value);
}

void Widget::RunDeferredActions()
{
    for (int i = 0; i < feedbackProcessors_.GetSize(); ++i)
        feedbackProcessors_.Get(i)->RunDeferredActions();
}

void  Widget::UpdateColorValue(const rgba_color &color)
{
    for (int i = 0; i < feedbackProcessors_.GetSize(); ++i)
        feedbackProcessors_.Get(i)->SetColorValue(color);
}

void Widget::SetXTouchDisplayColors(const char *zoneName, const char *colors)
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
    if (g_surfaceInDisplay)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %s %f\n", GetSurface()->GetName(), GetName(), value);
        ShowConsoleMsg(buffer);
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
void OSC_FeedbackProcessor::SetColorValue(const rgba_color &color)
{
    if (lastColor_ != color)
    {
        lastColor_ = color;
        char tmp[32];
        surface_->SendOSCMessage(this, (oscAddress_ + "/Color").c_str(), color.rgba_to_string(tmp));
    }
}

void OSC_X32FeedbackProcessor::SetColorValue(const rgba_color &color)
{
    if (lastColor_ != color)
    {
        lastColor_ = color;

        
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
        oscAddress += int_to_string(widget_->GetChannelNumber()) + "/config/color";
        surface_->SendOSCMessage(this, oscAddress.c_str(), surfaceColor);
    }
}

void OSC_FeedbackProcessor::ForceValue(const PropertyList &properties, double value)
{
    if ((GetTickCount() - GetWidget()->GetLastIncomingMessageTime()) < 50) // adjust the 50 millisecond value to give you smooth behaviour without making updates sluggish
        return;

    lastDoubleValue_ = value;
    surface_->SendOSCMessage(this, oscAddress_.c_str(), value);
}

void OSC_FeedbackProcessor::ForceValue(const PropertyList &properties, const char * const &value)
{
    lastStringValue_ = value;
    char tmp[BUFSZ];
    surface_->SendOSCMessage(this, oscAddress_.c_str(), GetWidget()->GetSurface()->GetRestrictedLengthText(value,tmp,sizeof(tmp)));
}

void OSC_FeedbackProcessor::ForceClear()
{
    lastDoubleValue_ = 0.0;
    surface_->SendOSCMessage(this, oscAddress_.c_str(), 0.0);
    
    lastStringValue_ = "";
    surface_->SendOSCMessage(this, oscAddress_.c_str(), "");
}

void OSC_IntFeedbackProcessor::ForceClear()
{
    lastDoubleValue_ = 0.0;
    surface_->SendOSCMessage(this, oscAddress_.c_str(), (int)0);
}

void OSC_IntFeedbackProcessor::ForceValue(const PropertyList &properties, double value)
{
    lastDoubleValue_ = value;
    
    surface_->SendOSCMessage(this, oscAddress_.c_str(), (int)value);
}

void OSC_X32IntFeedbackProcessor::ForceValue(const PropertyList &properties, double value)
{
    lastDoubleValue_ = value;
    
    if (oscAddress_.find("/-stat/selidx") != string::npos)
    {
        if (value != 0.0)
            surface_->SendOSCMessage(this, "/-stat/selidx", widget_->GetChannelNumber() -1);
    }
    else
        surface_->SendOSCMessage(this, oscAddress_.c_str(), (int)value);
}

void OSC_X32IntFeedbackProcessor::SetColorValue(const rgba_color &color)
{
    if (lastColor_ != color)
    {
        lastColor_ = color;

        
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
        oscAddress += int_to_string(widget_->GetChannelNumber()) + "/config/color";
        surface_->SendOSCMessage(this, oscAddress.c_str(), surfaceColor);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// ZoneManager
////////////////////////////////////////////////////////////////////////////////////////////////////////
void ZoneManager::Initialize()
{
    PreProcessZones();

    if ( ! zoneFilePaths_.Exists("Home"))
    {
        char tmp[BUFSZ];
        snprintf(tmp, sizeof(tmp), __LOCALIZE_VERFMT("%s needs a Home Zone to operate, please recheck your installation", "csi_mbox"), surface_->GetName());
        MessageBox(g_hwnd, tmp, __LOCALIZE("CSI Missing Home Zone", "csi_mbox"), MB_OK);
        return;
    }
        
    WDL_PtrList<Navigator> navigators;
    navigators.Add(GetSelectedTrackNavigator());
    WDL_PtrList<Zone> dummy; // Needed to satisfy protcol, Home and FocusedFXParam have special Zone handling
    LoadZoneFile(zoneFilePaths_.Get("Home")->filePath.c_str(), navigators, dummy, NULL);
    if (zoneFilePaths_.Exists("FocusedFXParam"))
        LoadZoneFile(zoneFilePaths_.Get("FocusedFXParam")->filePath.c_str(), navigators, dummy, NULL);
    if (zoneFilePaths_.Exists("FXLayouts") && zoneFilePaths_.Exists("SurfaceFXLayout"))
        BuildFXTemplate(zoneFilePaths_.Get("FXLayouts")->filePath, zoneFilePaths_.Get("SurfaceFXLayout")->filePath);
    if (zoneFilePaths_.Exists("FXPrologue"))
        ProcessFXBoilerplate(zoneFilePaths_.Get("FXPrologue")->filePath, fxPrologue_);
    if (zoneFilePaths_.Exists("FXEpilogue"))
        ProcessFXBoilerplate(zoneFilePaths_.Get("FXEpilogue")->filePath, fxEpilogue_);

    GoHome();
}

void ZoneManager::AddListener(ControlSurface *surface)
{
    if (WDL_NOT_NORMALLY(!surface)) { return; }
    listeners_.Add(surface->GetZoneManager());
}

void ZoneManager::SetListenerCategories(const char *categoryList)
{
    string_list categoryTokens;
    GetTokens(categoryTokens, categoryList);
    
    for (int i = 0; i < (int)categoryTokens.size(); ++i)
    {
        if (categoryTokens[i] == "GoHome")
            listensToGoHome_ = true;
        if (categoryTokens[i] == "Sends")
            listensToSends_ = true;
        if (categoryTokens[i] == "Receives")
            listensToReceives_ = true;
        if (categoryTokens[i] == "LearnFocusedFX")
            listensToLearnFocusedFX_ = true;
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

void ZoneManager::CheckFocusedFXState()
{
    if (! isFocusedFXMappingEnabled_)
        return;

    int trackNumber = 0;
    int itemNumber = 0;
    int takeNumber = 0;
    int fxIndex = 0;
    int paramIndex = 0;
    
    bool retVal = GetTouchedOrFocusedFX(1, &trackNumber, &itemNumber, &takeNumber, &fxIndex, &paramIndex);
    
    if ( ! retVal || (retVal && (paramIndex & 0x01)))
    {
        if( focusedFXZones_.GetSize() > 0)
        {
            for (int i = 0; i < focusedFXZones_.GetSize(); ++i)
                focusedFXZones_.Get(i)->RestoreXTouchDisplayColors();
            
            focusedFXZones_.Empty();
            needGarbageCollect_ = true;
        }
        
        return;
    }
    
    if (fxIndex > -1)
    {
        if (focusedFXZones_.GetSize() == 0)
            GoFocusedFX();
    }
}

void ZoneManager::GoFocusedFX()
{
    focusedFXZones_.Empty();
    
    int trackNumber = 0;
    int itemNumber = 0;
    int takeNumber = 0;
    int fxSlot = 0;
    int paramIndex = 0;

    MediaTrack *focusedTrack = NULL;

    int retVal = GetTouchedOrFocusedFX(1, &trackNumber, &itemNumber, &takeNumber, &fxSlot, &paramIndex);
    
    trackNumber++;
    
    if (retVal && ! (paramIndex & 0x01))
    {
        if (trackNumber > 0)
            focusedTrack = DAW::GetTrack(trackNumber);
        else if (trackNumber == 0)
            focusedTrack = GetMasterTrack(NULL);
    }
    
    if (focusedTrack)
    {
        char FXName[BUFSZ];
        TrackFX_GetFXName(focusedTrack, fxSlot, FXName, sizeof(FXName));
        
        if (zoneFilePaths_.Exists(FXName))
        {
            WDL_PtrList<Navigator> navigators;
            navigators.Add(GetSurface()->GetPage()->GetFocusedFXNavigator());
            
            LoadZoneFile(zoneFilePaths_.Get(FXName)->filePath.c_str(), navigators, focusedFXZones_, NULL);
            
            for (int i = 0; i < focusedFXZones_.GetSize(); ++i)
            {
                focusedFXZones_.Get(i)->SetXTouchDisplayColors("White");
                focusedFXZones_.Get(i)->SetSlotIndex(fxSlot);
                focusedFXZones_.Get(i)->Activate();
            }
        }
    }

    needGarbageCollect_ = true;
}

void ZoneManager::GoSelectedTrackFX()
{
    if (homeZone_ != NULL)
    {
        ClearFXMapping();
        ResetOffsets();
                
        homeZone_->GoAssociatedZone("SelectedTrackFX");
    }

    selectedTrackFXZones_.Empty();
    
    if (MediaTrack *selectedTrack = surface_->GetPage()->GetSelectedTrack())
    {
        for (int i = 0; i < TrackFX_GetCount(selectedTrack); i++)
        {
            char FXName[BUFSZ];
            
            TrackFX_GetFXName(selectedTrack, i, FXName, sizeof(FXName));
            
            if (zoneFilePaths_.Exists(FXName))
            {
                WDL_PtrList<Navigator> navigators;
                navigators.Add(GetSurface()->GetPage()->GetSelectedTrackNavigator());
                LoadZoneFile(zoneFilePaths_.Get(FXName)->filePath.c_str(), navigators, selectedTrackFXZones_, NULL);
                
                selectedTrackFXZones_.Get(selectedTrackFXZones_.GetSize() - 1)->SetSlotIndex(i);
                selectedTrackFXZones_.Get(selectedTrackFXZones_.GetSize() - 1)->Activate();
            }
        }
    }
    needGarbageCollect_ = true;
}

void ZoneManager::AutoLearnFocusedFX()
{
    int trackNumber = 0;
    int itemNumber = 0;
    int takeNumber = 0;
    int fxSlot = 0;
    int paramIndex = 0;
    
    MediaTrack *track = NULL;
    
    int retVal = GetTouchedOrFocusedFX(1, &trackNumber, &itemNumber, &takeNumber, &fxSlot, &paramIndex);

    trackNumber++;

    if (retVal && ! (paramIndex & 0x01))
    {
        if (trackNumber > 0)
            track = DAW::GetTrack(trackNumber);
        else if (trackNumber == 0)
            track = GetMasterTrack(NULL);

        if (track)
        {
            char fxName[BUFSZ];
            TrackFX_GetFXName(track, fxSlot, fxName, sizeof(fxName));
            if ( ! csi_->HaveFXSteppedValuesBeenCalculated(fxName))
                CalculateSteppedValues(fxName, track, fxSlot);
            AutoLearnFX(fxName, track, fxSlot);
        }
    }
}

void ZoneManager::GoLearnFocusedFX()
{
    if (learnFXTrack_ != NULL)
        return;
    
    int trackNumber = 0;
    int itemNumber = 0;
    int takeNumber = 0;
    int fxSlot = 0;
    int paramIndex = 0;
    
    MediaTrack *track = NULL;
    
    int retVal = GetTouchedOrFocusedFX(1, &trackNumber, &itemNumber, &takeNumber, &fxSlot, &paramIndex);

    trackNumber++;

    if (retVal && ! (paramIndex & 0x01))
    {
        if (trackNumber > 0)
            track = DAW::GetTrack(trackNumber);
        else if (trackNumber == 0)
            track = GetMasterTrack(NULL);

        if (! track)
            return;
    }
    else
        return;

    if ( ! track)
        return;
    
    learnFXTrack_ = track;
    learnFXSlot_ = fxSlot;
    
    if (homeZone_ != NULL)
    {
        ClearFXMapping();
        ResetOffsets(); // GAW TBD -- do we really need this ?
                
        homeZone_->GoAssociatedZone("LearnFocusedFX");
    }
    
    char fxName[BUFSZ];
    TrackFX_GetFXName(track, fxSlot, fxName, sizeof(fxName));
    
    if (zoneFilePaths_.Exists(fxName))
    {
        WDL_PtrList<Navigator> navs;
        navs.Add(GetFocusedFXNavigator());
        
        WDL_PtrList<Zone> zones;
        
        LoadZoneFile(zoneFilePaths_.Get(fxName)->filePath.c_str(), navs, zones, NULL);
    }
}

void ZoneManager::GoFXSlot(MediaTrack *track, Navigator *navigator, int fxSlot)
{
    if (fxSlot > TrackFX_GetCount(track) - 1)
        return;
    
    char fxName[BUFSZ];
    
    TrackFX_GetFXName(track, fxSlot, fxName, sizeof(fxName));
    
    if ( ! csi_->HaveFXSteppedValuesBeenCalculated(fxName))
        CalculateSteppedValues(fxName, track, fxSlot);

    if (zoneFilePaths_.Exists(fxName))
    {
        WDL_PtrList<Navigator> navigators;
        navigators.Add(navigator);
        
        LoadZoneFile(zoneFilePaths_.Get(fxName)->filePath.c_str(), navigators, fxSlotZones_, NULL);
        
        if (fxSlotZones_.GetSize() > 0)
        {
            fxSlotZones_.Get(fxSlotZones_.GetSize() - 1)->SetSlotIndex(fxSlot);
            fxSlotZones_.Get(fxSlotZones_.GetSize() - 1)->Activate();
        }
    }
    else
        TrackFX_SetOpen(track, fxSlot, true);
    
    needGarbageCollect_ = true;
}

void ZoneManager::UpdateCurrentActionContextModifiers()
{    
    if (focusedFXParamZone_ != NULL)
        focusedFXParamZone_->UpdateCurrentActionContextModifiers();
    
    for (int i = 0; i < focusedFXZones_.GetSize(); ++i)
        focusedFXZones_.Get(i)->UpdateCurrentActionContextModifiers();
    
    for (int i = 0; i < selectedTrackFXZones_.GetSize(); ++i)
        selectedTrackFXZones_.Get(i)->UpdateCurrentActionContextModifiers();
    
    for (int i = 0; i < fxSlotZones_.GetSize(); ++i)
        fxSlotZones_.Get(i)->UpdateCurrentActionContextModifiers();
    
    if (homeZone_ != NULL)
        homeZone_->UpdateCurrentActionContextModifiers();
}

void ZoneManager::GetWidgetNameAndModifiers(const char *line, int listSlotIndex, string &cell, string &paramWidgetName, string &paramWidgetFullName, string_list &modifiers, int &modifier, const ptrvector<FXParamLayoutTemplate> &layoutTemplates)
{
    GetSubTokens(modifiers, line, '+');

    modifier = GetModifierValue(modifiers);
    
    paramWidgetFullName = modifiers[modifiers.size() - 1];

    paramWidgetName = paramWidgetFullName.substr(0, paramWidgetFullName.length() - layoutTemplates[listSlotIndex].suffix.length());
    
    cell = layoutTemplates[listSlotIndex].suffix;
}

int ZoneManager::GetModifierValue(const string_list &modifierTokens)
{
    ModifierManager modifierManager(csi_);

    return modifierManager.GetModifierValue(modifierTokens);
}

void ZoneManager::SaveLearnedFXParams()
{
    if (learnFXTrack_ == NULL)
        return;
    
    char fxName[BUFSZ];
    TrackFX_GetFXName(learnFXTrack_, learnFXSlot_, fxName, sizeof(fxName));
    
    string path = GetResourcePath() + string("/CSI/Zones/") + fxZoneFolder_ + "/AutoGeneratedFXZones";
    
    RecursiveCreateDirectory(path.c_str(),0);
    
    string trimmedFXName = fxName;
    ReplaceAllWith(trimmedFXName, s_BadFileChars, "_");
    
    path += "/" + trimmedFXName + ".zon";
    
    char alias[256];
    GetAlias(fxName, alias, sizeof(alias));
    
    CSIZoneInfo *info = new CSIZoneInfo();
    info->filePath = path;
    info->alias = alias;
    
    AddZoneFilePath(fxName, info);
    surface_->GetPage()->AddZoneFilePath(surface_, fxZoneFolder_.c_str(), fxName, info);
    
    FILE *fxZone = fopenUTF8(path.c_str(),"wb");
    
    if (fxZone)
    {
        fprintf(fxZone, "Zone \"%s\" \"%s\"\n", fxName, alias);
        
        for (int i = 0; i < (int)fxPrologue_.size(); ++i)
            fprintf(fxZone, "\t%s\n", fxPrologue_[i].c_str());
        
        fprintf(fxZone, "\n%s\n\n", s_BeginAutoSection);
        
        for (int i = 0; i < fxRows_.size(); ++i)
        {
            FXCellRow &row = fxRows_[i];

            if (learnedWidgets_.Exists(row.modifier))
            {
                ptrvector<string> learnedWidgets;
                
                for (int j = 0; j < row.cells.size(); ++j)
                {
                    Widget *learnedWidget = surface_->GetWidgetByName((row.cells[j].control + row.address).c_str());

                    if (learnedWidget && learnedWidgets_.Get(row.modifier)->Exists(learnedWidget))
                        learnedWidgets.push_back(row.cells[j].control);
                }
                
                if (learnedWidgets.size() == 0)
                    row.WriteDefaultTemplateToFile(fxZone);
                else
                {
                    fprintf(fxZone, "\t#Cell %s %s \n", row.address.c_str(), row.modifiers.c_str());

                    int learnIdx = 0;
                    
                    for (int j = 0; j < paramWidgets_.size(); ++j)
                    {
                        FXCell cell;
                        
                        string steps;

                        if (learnIdx < learnedWidgets.size())
                        {
                            if (paramWidgets_[j] == learnedWidgets[learnIdx])
                            {
                                // Write params and steps
                                Widget *learnedWidget = surface_->GetWidgetByName((row.cells[j].control + row.address).c_str());

                                if (learnedWidget)
                                {
                                    cell.control = row.cells.Get(j)->control;
                                    cell.nameDisplay = row.cells.Get(j)->nameDisplay;
                                    cell.valueDisplay = row.cells.Get(j)->valueDisplay;

                                    cell.controlAction = "FXParam";
                                    cell.nameDisplayAction = "FixedTextDisplay";
                                    cell.valueDisplayAction = "FXParamValueDisplay";
                                    
                                    int paramNum = learnedWidgets_.Get(row.modifier)->GetPtr(learnedWidget)->paramNum;
                                    
                                    cell.paramNum = int_to_string(paramNum);

                                    char tmp[BUFSZ];
                                    TrackFX_GetParamName(learnFXTrack_, learnFXSlot_, paramNum, tmp, sizeof(tmp));

                                    cell.paramName = tmp;
                                    
                                    int steppedValueCount =  csi_->GetSteppedValueCount(fxName, paramNum);
                                    
                                    if (steppedValueCount >= g_minNumParamSteps && steppedValueCount <= g_maxNumParamSteps)
                                        GetParamStepsString(steps, steppedValueCount);

                                    cell.WriteToFile(fxZone, row.modifiers.c_str(), row.address.c_str(), steps.c_str());
                                }
                            }
                            else
                            {
                                cell.control = row.cells.Get(j)->control;
                                cell.nameDisplay = row.cells.Get(j)->nameDisplay;
                                cell.valueDisplay = row.cells.Get(j)->valueDisplay;

                                cell.nameDisplayAction = "NullDisplay";
                                cell.valueDisplayAction = "NullDisplay";
                                cell.WriteToFile(fxZone, row.modifiers.c_str(), row.address.c_str(), steps.c_str());
                            }
                        }
                        else
                        {
                            cell.control = row.cells.Get(j)->control;
                            cell.nameDisplay = row.cells.Get(j)->nameDisplay;
                            cell.valueDisplay = row.cells.Get(j)->valueDisplay;

                            cell.nameDisplayAction = "NullDisplay";
                            cell.valueDisplayAction = "NullDisplay";
                            cell.WriteToFile(fxZone, row.modifiers.c_str(), row.address.c_str(), steps.c_str());
                        }
                        
                        if (j == paramWidgets_.size() - 1)
                            fprintf(fxZone, "\n");
                    }
                }
            }
            else
                row.WriteDefaultTemplateToFile(fxZone);
        }
        
        fprintf(fxZone, "%s\n", s_EndAutoSection);
        
        for (int i = 0; i < (int)fxEpilogue_.size(); ++i)
            fprintf(fxZone, "\t%s\n", fxEpilogue_[i].c_str());
        
        fprintf(fxZone, "ZoneEnd\n\n");
        
        for (int i = 0; i < TrackFX_GetNumParams(learnFXTrack_, learnFXSlot_); i++)
        {
            char tmp[BUFSZ];
            TrackFX_GetParamName(learnFXTrack_, learnFXSlot_, i, tmp, sizeof(tmp));
            fprintf(fxZone, "%d %s\n", i, tmp);
        }
        
        fclose(fxZone);
    }
    
    if (zoneFilePaths_.Exists(fxName))
    {
        WDL_PtrList<Navigator> navigators;

        navigators.Add(GetFocusedFXNavigator());
        LoadZoneFile(zoneFilePaths_.Get(fxName)->filePath.c_str(), navigators, focusedFXZones_, NULL);
    }
    
    if (homeZone_ != NULL)
    {
        homeZone_->DeactivateLearnFocusedFXZone();
    }

    ClearLearnedFXParams();
}

void ZoneManager::SaveRemappedZone(const FXZoneDefinition &zoneDef)
{
    FILE *fxFile = fopenUTF8(zoneDef.fullPath.c_str(),"wb");
    
    if (fxFile)
    {
        fprintf(fxFile, "Zone \"%s\" \"%s\"\n", zoneDef.fxName.c_str(), zoneDef.fxAlias.c_str());
        
        for (int i = 0; i < (int)zoneDef.prologue.size(); ++i)
            fprintf(fxFile, "%s\n", zoneDef.prologue[i].c_str());
        
        fprintf(fxFile, "%s\n\n", s_BeginAutoSection);
        
        for (int i = 0; i < zoneDef.rows.size(); ++i)
            zoneDef.rows.Get(i)->WriteTemplateToFile(fxFile, paramWidgets_);
        
        fprintf(fxFile, "%s\n", s_EndAutoSection);
        
        for (int i = 0; i < (int)zoneDef.epilogue.size(); ++i)
            fprintf(fxFile, "%s\n", zoneDef.epilogue[i].c_str());
        
        fprintf(fxFile, "ZoneEnd\n");
        
        for (int i = 0; i < (int)zoneDef.rawParams.size(); ++i)
            fprintf(fxFile, "%s\n", zoneDef.rawParams[i].c_str());
        
        fclose(fxFile);
    }
    
    PreProcessZoneFile(zoneDef.fullPath.c_str(), this);
    GoFocusedFX();
}

void ZoneManager::EraseLastTouchedControl()
{
    if (lastTouchedControl_ == NULL)
        return;
    
    WDL_PointerKeyedArray<Widget *, LearnedWidgetParams> *widgetParams = learnedWidgets_.Get(surface_->GetModifiers().Get()[0]);
    
    if (widgetParams->Exists(lastTouchedControl_))
        widgetParams->Delete(lastTouchedControl_);
    
    lastTouchedControl_ = NULL;
}

void ZoneManager::AddLearnedWidget(Widget* widget, int modifier, int paramNum)
{
    if ( ! learnedWidgets_.Exists(modifier))
        learnedWidgets_.Insert(modifier, new WDL_PointerKeyedArray<Widget *, LearnedWidgetParams>());
    
    if ( ! learnedWidgets_.Get(modifier)->Exists(widget))
    {
        LearnedWidgetParams lwp;
        lwp.paramNum = paramNum;
        lwp.control = widget;

        GetParamDisplayWidgets(widget->GetName(), modifier, &lwp);
        
        if (lwp.nameDisplay != NULL && lwp.valueDisplay != NULL)
            learnedWidgets_.Get(modifier)->Insert(widget, lwp);
    }
}

void ZoneManager::GetParamDisplayWidgets(const char *controlName, int modifier, LearnedWidgetParams *learnedWidgetParams)
{
    for (int i = 0; i < fxRows_.size(); ++i)
    {
        if (fxRows_[i].modifier == modifier)
        {
            const char *address = fxRows_[i].address.c_str();
            
            for (int j = 0; j < fxRows_[i].cells.size(); ++j)
            {
                FXCell &cell = fxRows_[i].cells[j];

                char buf[BUFSZ];
                buf[0] = 0;

                sprintf(buf, "%s%s", cell.control.c_str(), address);

                if ( ! strcmp(buf, controlName))
                {
                    buf[0] = 0;
                    
                    const char *nameDisplay = cell.nameDisplay != "" ? cell.nameDisplay.c_str() : fxRows_[i].cells[0].nameDisplay.c_str();
                    sprintf(buf, "%s%s", nameDisplay, address);
                    learnedWidgetParams->nameDisplay = surface_->GetWidgetByName(buf);

                    buf[0] = 0;
                    
                    const char *valueDisplay = cell.valueDisplay != "" ? cell.valueDisplay.c_str() : fxRows_[i].cells[0].valueDisplay.c_str();
                    sprintf(buf, "%s%s", valueDisplay, address);
                    learnedWidgetParams->valueDisplay = surface_->GetWidgetByName(buf);
                    return;
                }
            }
        }
    }
}

void ZoneManager::UpdateLearnedParams()
{
    if ( ! learnFXTrack_)
        return;
    
    double min, max;

    WDL_PointerKeyedArray<Widget *, LearnedWidgetParams> *widgetParams = learnedWidgets_.Get(surface_->GetModifiers().Get()[0]);

    if ( ! widgetParams)
        return;
    
    for (int i = 0; i < widgetParams->GetSize(); ++i)
    {
        LearnedWidgetParams *lwp = widgetParams->EnumeratePtr(i);
        
        double currentValue = TrackFX_GetParam(learnFXTrack_, learnFXSlot_, lwp->paramNum, &min, &max);

        PropertyList properties;
        lwp->lastValueSent = currentValue;
        lwp->control->UpdateValue(properties, currentValue);

        lwp->control->SetHasBeenUsedByUpdate();

        char buf[BUFSZ];
        buf[0] = 0;
        TrackFX_GetParamName(learnFXTrack_, learnFXSlot_, lwp->paramNum, buf, sizeof(buf));
        lwp->nameDisplay->UpdateValue(properties, buf);
        
        lwp->nameDisplay->SetHasBeenUsedByUpdate();

        buf[0] = 0;
        TrackFX_GetFormattedParamValue(learnFXTrack_, learnFXSlot_, lwp->paramNum, buf, sizeof(buf));
        lwp->valueDisplay->UpdateValue(properties, buf);
        
        lwp->valueDisplay->SetHasBeenUsedByUpdate();
    }
}

double ZoneManager::GetNextSteppedValue(MediaTrack *track, Widget *widget, LearnedWidgetParams *lwp, double value)
{
    char buf[BUFSZ];
    TrackFX_GetFXName(track, learnFXSlot_, buf, sizeof(buf));
    
    int stepCount = csi_->GetSteppedValueCount(buf, lwp->paramNum);
    
    double nextVal = -1.0;

    for (int i = 0; i < stepCount; ++i)
    {
        if (lwp->lastValueSent == EnumSteppedValues(stepCount, i))
        {
            if (i == stepCount - 1)
                nextVal = EnumSteppedValues(stepCount, 0);
            else
                nextVal = EnumSteppedValues(stepCount, i + 1);

            break;
        }
    }
    
    if (strstr(widget->GetName(), "Push") && stepCount == 0)
        value = ! lwp->lastValueSent;

    if (nextVal >= 0.0)
        return nextVal;
    else
        return value;
}

void ZoneManager::DoLearn(Widget *widget, bool isUsed, double value)
{
    if (learnFXTrack_ == NULL)
        return;
    
    if (value == 0.0)
        return;
    
    int trackNum = 0;
    int slotNum = 0;
    int paramNum = 0;

    if (GetLastTouchedFX(&trackNum, &slotNum, &paramNum))
    {
        if (DAW::GetTrack(trackNum) != learnFXTrack_ || slotNum != learnFXSlot_)
        {
            lastTouchedControl_ = NULL;
            return;
        }
        
        int modifier = surface_->GetModifiers().Get()[0];
          
        AddLearnedWidget(widget, modifier, paramNum);
        
        LearnedWidgetParams *lwp = learnedWidgets_.Get(modifier)->GetPtr(widget);
        if (lwp)
        {
            lwp->lastValueSent = GetNextSteppedValue(learnFXTrack_, widget, lwp, value);
            TrackFX_SetParam(learnFXTrack_, learnFXSlot_, lwp->paramNum, lwp->lastValueSent);
            lastTouchedControl_ = widget;
        }
        
        isUsed = true;
    }
}

void ZoneManager::DoRelativeLearn(Widget *widget, bool isUsed, double delta)
{
    int trackNum = 0;
    int slotNum = 0;
    int paramNum = 0;
    
    if (GetLastTouchedFX(&trackNum, &slotNum, &paramNum))
    {
        if (DAW::GetTrack(trackNum) != learnFXTrack_ || slotNum != learnFXSlot_)
        {
            lastTouchedControl_ = NULL;
            return;
        }

        int modifier = surface_->GetModifiers().Get()[0];
        
        AddLearnedWidget(widget, modifier, paramNum);
        
        LearnedWidgetParams *lwp = learnedWidgets_.Get(modifier)->GetPtr(widget);
        
        if (lwp)
        {
            double min, max;
            double currentValue = TrackFX_GetParam(learnFXTrack_, learnFXSlot_, lwp->paramNum, &min, &max);
            lwp->lastValueSent = GetNextSteppedValue(learnFXTrack_, widget, lwp, currentValue + delta);
            TrackFX_SetParam(learnFXTrack_, learnFXSlot_, lwp->paramNum, lwp->lastValueSent);
            lastTouchedControl_ = widget;
        }
        
        isUsed = true;
    }
}

void ZoneManager::RemapZone()
{
    int trackNumber = 0;
    int itemNumber = 0;
    int takeNumber = 0;
    int fxSlot = 0;
    int paramIndex = 0;

    MediaTrack *focusedTrack = NULL;

    int retVal = GetTouchedOrFocusedFX(1, &trackNumber, &itemNumber, &takeNumber, &fxSlot, &paramIndex);
    
    trackNumber++;
    
    if (retVal && ! (paramIndex & 0x01))
    {
        if (trackNumber > 0)
            focusedTrack = DAW::GetTrack(trackNumber);
        else if (trackNumber == 0)
            focusedTrack = GetMasterTrack(NULL);
    }
    
    if (focusedTrack)
    {
        char FXName[BUFSZ];
        TrackFX_GetFXName(focusedTrack, fxSlot, FXName, sizeof(FXName));
        
        if (zoneFilePaths_.Exists(FXName))
        {
            const char *path = zoneFilePaths_.Get(FXName)->filePath.c_str();
            
            if (path[0] != 0)
                RemapFXDialog(this, path);
         }
    }
}

void ZoneManager::PreProcessZones()
{
    if (zoneFolder_[0] == 0)
    {
        char tmp[2048];
        snprintf(tmp, sizeof(tmp), __LOCALIZE_VERFMT("Please check your CSI.ini, cannot find Zone folder for %s in:\r\n\r\n%s/CSI/Zones/","csi_mbox"), GetSurface()->GetName(), GetResourcePath());
        MessageBox(g_hwnd, tmp, __LOCALIZE("Zone folder definiton for surface is empty","csi_mbox"), MB_OK);

        return;
    }
    
    string_list zoneFilesToProcess;
    listFilesOfType(GetResourcePath() + string("/CSI/Zones/") + zoneFolder_ + "/", zoneFilesToProcess, ".zon"); // recursively find all .zon files, starting at zoneFolder
       
    if (zoneFilesToProcess.size() == 0)
    {
        char tmp[2048];
        snprintf(tmp, sizeof(tmp), __LOCALIZE_VERFMT("Please check your installation, cannot find Zone files for %s in:\r\n\r\n%s/CSI/Zones/","csi_mbox"), GetSurface()->GetName(), GetResourcePath());
        MessageBox(g_hwnd, tmp, __LOCALIZE("Zone folder is missing or empty","csi_mbox"), MB_OK);

        return;
    }
      
    for (int i = 0; i < (int)zoneFilesToProcess.size(); ++i)
        PreProcessZoneFile(zoneFilesToProcess[i], this);
    
    if (zoneFolder_ != fxZoneFolder_)
    {
        zoneFilesToProcess.clear();
        
        listFilesOfType(GetResourcePath() + string("/CSI/Zones/") + fxZoneFolder_ + "/", zoneFilesToProcess, ".zon"); // recursively find all .zon files, starting at fxZoneFolder
         
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
    GetTrackUIMute(track, &wasMuted);
    
    if ( ! wasMuted)
        CSurf_SetSurfaceMute(track, CSurf_OnMuteChange(track, true), NULL);

    double minvalOut = 0.0;
    double maxvalOut = 0.0;

    double currentValue;

    currentValue = TrackFX_GetParam(track, fxIndex, paramIndex, &minvalOut, &maxvalOut);
    
        int stepCount = 1;
        double stepValue = 0.0;
        
        for (double value = 0.0; value < 1.01; value += .01)
        {
            TrackFX_SetParam(track, fxIndex, paramIndex, value);
            
            double fxValue = TrackFX_GetParam(track, fxIndex, paramIndex, &minvalOut, &maxvalOut);
            
            if (stepValue != fxValue)
            {
                stepValue = fxValue;
                stepCount++;
            }
        }
        
    if (stepCount > 1 && stepCount < 31)
        csi_->SetSteppedValueCount(fxName, paramIndex, stepCount);

    TrackFX_SetParam(track, fxIndex, paramIndex, currentValue);
    
    if ( ! wasMuted)
        CSurf_SetSurfaceMute(track, CSurf_OnMuteChange(track, false), NULL);
}

void ZoneManager::CalculateSteppedValues(const string &fxName, MediaTrack *track, int fxIndex)
{
    csi_->SetSteppedValueCount(fxName, -1, 0); // Add dummy value to show the calculation has beeen performed, even though there may be no stepped values for this FX

    // Check for UAD / Plugin Alliance and bail if neither
    if (fxName.find("UAD") == string::npos && fxName.find("Plugin Alliance") == string::npos)
        return;
    
    int totalLayoutCount = fxRows_.size();
    
    //for (int i = 0; i < (int)surfaceFXLayouts_.size(); ++i)
        //totalLayoutCount += surfaceFXLayouts_[i].channelCount;
    bool wasMuted = false;
    GetTrackUIMute(track, &wasMuted);
    
    if ( ! wasMuted)
        CSurf_SetSurfaceMute(track, CSurf_OnMuteChange(track, true), NULL);

    double minvalOut = 0.0;
    double maxvalOut = 0.0;

    int numParams = TrackFX_GetNumParams(track, fxIndex);

    vector<double> currentValues;

    for (int i = 0; i < numParams && i <= totalLayoutCount; i++)
        currentValues.push_back(TrackFX_GetParam(track, fxIndex, i, &minvalOut, &maxvalOut));
    
    for (int i = 0; i < numParams && i <= totalLayoutCount; i++)
    {
        int stepCount = 1;
        double stepValue = 0.0;
        
        for (double value = 0.0; value < 1.01; value += .01)
        {
            TrackFX_SetParam(track, fxIndex, i, value);
            
            double fxValue = TrackFX_GetParam(track, fxIndex, i, &minvalOut, &maxvalOut);
            
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
        TrackFX_SetParam(track, fxIndex, i, currentValues[i]);
    
    if ( ! wasMuted)
        CSurf_SetSurfaceMute(track, CSurf_OnMuteChange(track, false), NULL);
}

void ZoneManager::AutoLearnFX(const string &fxName, MediaTrack *track, int fxIndex)
{
    int numParams = TrackFX_GetNumParams(track, fxIndex);

    for (int i = 0; i < fxRows_.size() && i < numParams; ++i)
    {
        // Add learned Widget
        if (fxRows_[i].cells.size())
        {
            char buf[BUFSZ];
            sprintf(buf, "%s%s", fxRows_[i].cells[0].control.c_str(), fxRows_[i].address.c_str());
            
            Widget *control = surface_->GetWidgetByName(buf);

            if (control)
                AddLearnedWidget(control, fxRows_[i].modifier, i);
        }
    }
}

void ZoneManager::UnpackFXZone(FXZoneDefinition &zd)
{
    zd.rows.clear();
    zd.prologue.clear();
    zd.epilogue.clear();
    zd.rawParams.clear();

    zd.fxName = "";
    zd.fxAlias = "";

    bool inZone = false;
    bool inAutoZone = false;
    bool pastAutoZone = false;
    
    fpistream autoFXFile(zd.fullPath.c_str());
        
    string_list tokens;
    for (string line; getline(autoFXFile, line) ; )
    {
        ReplaceAllWith(line, "\r\n", "");

        if (inAutoZone && ! pastAutoZone)
        {
            // Trim leading and trailing spaces
            TrimLine(line);
            
            if (line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
        }

        tokens.clear();
        GetTokens(tokens, line.c_str());

        if (line.substr(0, 5) == "Zone ")
        {
            inZone = true;
            
            if (tokens.size() > 1)
                zd.fxName = tokens[1];
            if (tokens.size() > 2)
                zd.fxAlias = tokens[2];
            
            continue;
        }
        else if (line == s_BeginAutoSection)
        {
            inAutoZone = true;
            continue;
        }
        else if (inZone && ! inAutoZone)
        {
            zd.prologue.push_back(line);
            continue;
        }
        else if (line == s_EndAutoSection)
        {
            pastAutoZone = true;
            continue;
        }
        else if (line == "ZoneEnd")
        {
            inZone = false;
            continue;
        }
        else if (inZone && pastAutoZone)
        {
            zd.epilogue.push_back(line);
            continue;
        }
        else if (! inZone && pastAutoZone)
        {
            if (line != "")
                zd.rawParams.push_back(line);
            continue;
        }
        else if (tokens.size() > 0)
        {
            if (tokens[0] == "#Cell" && tokens.size() > 1)
            {
                FXCellRow row;
                
                row.address = string(tokens[1]);
                row.modifiers = tokens.size() > 2 ? string(tokens[2]) : "";
                
                string_list modifierTokens;
                GetSubTokens(modifierTokens, row.modifiers.c_str(), '+');
                row.modifier = GetSurface()->GetModifierManager()->GetModifierValue(modifierTokens);

                zd.rows.push_back(row);
            }
            else
            {
                if (zd.rows.size()== 0)
                    continue;

                FXCellRow &lastRow = zd.rows[zd.rows.size() - 1];
                
                string line2;
                string line3;
                
                if (getline(autoFXFile, line2) && getline(autoFXFile, line3))
                {
                    string_list cellTokens;
                    
                    GetTokens(cellTokens, line.c_str());

                    FXCell cell;

                    string_list widgetTokens;
                    string_list propertyTokens;
                    string_list paramTokens;

                    GetSubTokens(widgetTokens, cellTokens[0], '+');

                    cell.control = widgetTokens[widgetTokens.size() - 1];
                    cell.control = cell.control.substr(0, cell.control.find(lastRow.address));

                    if (cellTokens.size() > 2)
                    {
                        cell.controlAction = cellTokens[1];
                        cell.paramNum = cellTokens[2];
                        if (cellTokens.size() > 3)
                        {
                            cell.controlParams = line.substr(line.find(cellTokens[3]), line.length() - 1);
                            GetTokens(propertyTokens, cell.controlParams.c_str());
                            GetPropertiesFromTokens(0, propertyTokens.size(), propertyTokens, cell.controlProperties);
                          
                            GetTokens(paramTokens, cell.controlParams.c_str());
                            GetSteppedValues(paramTokens, 0, cell.deltaValue, cell.acceleratedDeltaValues, cell.rangeMinimum, cell.rangeMaximum, cell.steppedValues, cell.acceleratedTickValues);
                        }
                    }
                    
                    cellTokens.clear();
                    widgetTokens.clear ();
                    propertyTokens.clear();
                    paramTokens.clear();
                    
                    GetTokens(cellTokens, line2.c_str());

                    GetSubTokens(widgetTokens, cellTokens[0], '+');

                    cell.nameDisplay = widgetTokens[widgetTokens.size() - 1];

                    cell.nameDisplay = cell.nameDisplay.substr(0, cell.nameDisplay.find(lastRow.address));

                    if (cellTokens.size() > 2)
                    {
                        cell.nameDisplayAction = cellTokens[1];
                        cell.paramName = cellTokens[2];
                        if (cellTokens.size() > 3)
                        {
                            cell.nameDisplayParams = line2.substr(line2.find(cellTokens[3]), line2.length() - 1);
                            GetTokens(propertyTokens, cell.nameDisplayParams.c_str());
                            GetPropertiesFromTokens(0, propertyTokens.size(), propertyTokens, cell.nameDisplayProperties);
                        }
                    }

                    cellTokens.clear();
                    widgetTokens.clear ();
                    propertyTokens.clear();
                    paramTokens.clear();

                    GetTokens(cellTokens, line3.c_str());

                    GetSubTokens(widgetTokens, cellTokens[0], '+');

                    cell.valueDisplay = widgetTokens[widgetTokens.size() - 1];

                    cell.valueDisplay = cell.valueDisplay.substr(0, cell.valueDisplay.find(lastRow.address));

                    if (cellTokens.size() > 2)
                    {
                        cell.valueDisplayAction = cellTokens[1];
                        if (cellTokens.size() > 3)
                        {
                            cell.valueDisplayParams = line3.substr(line3.find(cellTokens[3]), line3.length() - 1);
                            GetTokens(propertyTokens, cell.valueDisplayParams.c_str());
                            GetPropertiesFromTokens(0, propertyTokens.size(), propertyTokens, cell.valueDisplayProperties);
                        }
                    }
                    
                    cellTokens.clear();
                    widgetTokens.clear ();
                    propertyTokens.clear();
                    paramTokens.clear();

                    lastRow.cells.push_back(cell);
                }
            }
        }
    }
}

void ZoneManager::DoTouch(Widget *widget, double value)
{
    surface_->TouchChannel(widget->GetChannelNumber(), value != 0);
    
    widget->LogInput(value);
    
    bool isUsed = false;
    
    if (focusedFXParamZone_ != NULL && isFocusedFXParamMappingEnabled_)
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

    if (homeZone_ != NULL)
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
    if (surface_ == NULL && page_ == NULL)
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
    
    if (surface_ != NULL)
        surface_->GetZoneManager()->UpdateCurrentActionContextModifiers();
    else if (page_ != NULL)
        page_->UpdateCurrentActionContextModifiers();
}

void ModifierManager::SetLatchModifier(bool value, Modifiers modifier, int latchTime)
{
    if (value && modifiers_[modifier].isEngaged == false)
    {
        modifiers_[modifier].isEngaged = value;
        modifiers_[modifier].pressedTime = GetTickCount();
    }
    else
    {
        DWORD keyReleasedTime = GetTickCount();
        
        if ((keyReleasedTime - modifiers_[modifier].pressedTime) > (DWORD)latchTime)
        {
            if (value == 0 && modifiers_[modifier].isEngaged)
            {
                char tmp[256];
                snprintf(tmp, sizeof(tmp), "%s Unlock", stringFromModifier(modifier));
                csi_->Speak(tmp);
            }

            modifiers_[modifier].isEngaged = value;
        }
        else
        {
            char tmp[256];
            snprintf(tmp, sizeof(tmp), "%s Lock", stringFromModifier(modifier));
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
        if (MediaTrack *track = CSurf_TrackFromID(i, followMCP_))
            if (IsTrackVisible(track, followMCP_))
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
    
    for (int i = 0; i < CountSelectedTracks2(NULL, false); i++)
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
            SetOnlyTrackSelected(trackToSelect);
            if (GetScrollLink())
                SetMixerScroll(trackToSelect);

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
        CSurf_OnPlay();
 
    page_->SignalStop();
    CancelRewindAndFastForward();
    CSurf_OnStop();
}

void ControlSurface::Play()
{
    page_->SignalPlay();
    CancelRewindAndFastForward();
    CSurf_OnPlay();
}

void ControlSurface::Record()
{
    page_->SignalRecord();
    CancelRewindAndFastForward();
    CSurf_OnRecord();
}

void ControlSurface::OnTrackSelection(MediaTrack *track)
{
    Widget *w = widgetsByName_.Get("OnTrackSelection");
    if (w)
    {
        if (GetMediaTrackInfo_Value(track, "I_SELECTED"))
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
        if (GetCursorPosition() == 0)
            StopRewinding();
        else
        {
            CSurf_OnRew(0);

            if (speedX5_ == true)
            {
                CSurf_OnRew(0);
                CSurf_OnRew(0);
                CSurf_OnRew(0);
                CSurf_OnRew(0);
            }
        }
    }
        
    else if (isFastForwarding_)
    {
        if (GetCursorPosition() > GetProjectLength(NULL))
            StopFastForwarding();
        else
        {
            CSurf_OnFwd(0);
            
            if (speedX5_ == true)
            {
                CSurf_OnFwd(0);
                CSurf_OnFwd(0);
                CSurf_OnFwd(0);
                CSurf_OnFwd(0);
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

void ControlSurface::ClearModifier(const char *modifier)
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
        midiInput_->SwapBufsPrecise(GetTickCount(), GetTickCount());
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
Midi_ControlSurface::Midi_ControlSurface(CSurfIntegrator *const csi, Page *page, const char *name, int numChannels, int channelOffset, const char *templateFilename, const char *zoneFolder, const char *fxZoneFolder, Midi_ControlSurfaceIO *surfaceIO)
: ControlSurface(csi, page, name, numChannels, channelOffset), templateFilename_(templateFilename), surfaceIO_(surfaceIO), Midi_CSIMessageGeneratorsByMessage_(disposeAction)
{
    // private:
    // special processing for MCU meters
    hasMCUMeters_ = false;
    displayType_ = 0x14;
    lastRun_ = 0;
    
    zoneManager_ = new ZoneManager(csi_, this, zoneFolder, fxZoneFolder);
    
    ProcessMIDIWidgetFile(string(GetResourcePath()) + "/CSI/Surfaces/Midi/" + templateFilename, this);
    InitHardwiredWidgets(this);
    InitializeMeters();
    zoneManager_->Initialize();
}

void Midi_ControlSurface::ProcessMidiMessage(const MIDI_event_ex_t *evt)
{
    if (g_surfaceRawInDisplay)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %02x  %02x  %02x \n", name_.c_str(), evt->midi_message[0], evt->midi_message[1], evt->midi_message[2]);
        ShowConsoleMsg(buffer);
    }

    int threeByteKey = evt->midi_message[0]  * 0x10000 + evt->midi_message[1]  * 0x100 + evt->midi_message[2];
    int twoByteKey = evt->midi_message[0]  * 0x10000 + evt->midi_message[1]  * 0x100;
    int oneByteKey = evt->midi_message[0] * 0x10000;

    // At this point we don't know how much of the message comprises the key, so try all three
    if (Midi_CSIMessageGeneratorsByMessage_.Exists(threeByteKey))
        Midi_CSIMessageGeneratorsByMessage_.Get(threeByteKey)->ProcessMidiMessage(evt);
    else if (Midi_CSIMessageGeneratorsByMessage_.Exists(twoByteKey))
        Midi_CSIMessageGeneratorsByMessage_.Get(twoByteKey)->ProcessMidiMessage(evt);
    else if (Midi_CSIMessageGeneratorsByMessage_.Exists(oneByteKey))
        Midi_CSIMessageGeneratorsByMessage_.Get(oneByteKey)->ProcessMidiMessage(evt);
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

    if (g_surfaceOutDisplay)
        ShowConsoleMsg(output.c_str());
}

void Midi_ControlSurface::SendMidiMessage(int first, int second, int third)
{
    surfaceIO_->SendMidiMessage(first, second, third);
    
    if (g_surfaceOutDisplay)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "%s  %02x  %02x  %02x \n", ("OUT->" + name_).c_str(), first, second, third);
        ShowConsoleMsg(buffer);
    }
}

 ////////////////////////////////////////////////////////////////////////////////////////////////////////
 // OSC_ControlSurfaceIO
 ////////////////////////////////////////////////////////////////////////////////////////////////////////

OSC_X32ControlSurfaceIO::OSC_X32ControlSurfaceIO(CSurfIntegrator *const csi, const char *surfaceName, const char *receiveOnPort, const char *transmitToPort, const char *transmitToIpAddress, int maxPacketsPerRun) : OSC_ControlSurfaceIO(csi, surfaceName, receiveOnPort, transmitToPort, transmitToIpAddress, maxPacketsPerRun)
{
    X32HeartBeatRefreshInterval_ = 5000; // must be less than 10000
    X32HeartBeatLastRefreshTime_ = GetTickCount()-30000;
}

OSC_ControlSurfaceIO::OSC_ControlSurfaceIO(CSurfIntegrator *const csi, const char *surfaceName, const char *receiveOnPort, const char *transmitToPort, const char *transmitToIpAddress, int maxPacketsPerRun) : csi_(csi), name_(surfaceName)
{
    // private:
    inSocket_ = NULL;
    outSocket_ = NULL;
    maxBundleSize_ = 0; // could be user configured, possible some networks might enforce a 1500 byte MTU or something
    maxPacketsPerRun_ = maxPacketsPerRun < 0 ? 0 : maxPacketsPerRun;
    sentPacketCount_ = 0;

    if (strcmp(receiveOnPort, transmitToPort))
    {
        inSocket_  = GetInputSocketForPort(surfaceName, atoi(receiveOnPort));
        outSocket_ = GetOutputSocketForAddressAndPort(surfaceName, transmitToIpAddress, atoi(transmitToPort));
    }
    else // WHEN INPUT AND OUTPUT SOCKETS ARE THE SAME -- DO MAGIC :)
    {
        oscpkt::UdpSocket *inSocket = GetInputSocketForPort(surfaceName, atoi(receiveOnPort));

        struct addrinfo hints;
        struct addrinfo *addressInfo;
        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family = AF_INET;      // IPV4
        hints.ai_socktype = SOCK_DGRAM; // UDP
        hints.ai_flags = 0x00000001;    // socket address is intended for bind
        getaddrinfo(transmitToIpAddress, transmitToPort, &hints, &addressInfo);
        memcpy(&(inSocket->remote_addr), (void*)(addressInfo->ai_addr), addressInfo->ai_addrlen);

        inSocket_  = inSocket;
        outSocket_ = inSocket;
    }
 }

OSC_ControlSurfaceIO::~OSC_ControlSurfaceIO()
{
    Sleep(33);
    
    int count = 0;
    while (packetQueue_.GetSize()>=sizeof(int) && count++ < 100)
    {
        BeginRun();
        if (count) Sleep(33);
    }

    if (inSocket_)
    {
        for (int x = 0; x < s_inputSockets.GetSize(); x ++)
        {
            if (s_inputSockets.Get(x)->socket == inSocket_)
            {
                if (!--s_inputSockets.Get(x)->refcnt)
                    s_inputSockets.Delete(x, true);
                break;
            }
        }
    }
    if (outSocket_ && outSocket_ != inSocket_)
    {
        for (int x = 0; x < s_outputSockets.GetSize(); x ++)
        {
            if (s_outputSockets.Get(x)->socket == outSocket_)
            {
                if (!--s_outputSockets.Get(x)->refcnt)
                    s_outputSockets.Delete(x, true);
                break;
            }
        }
    }
}

void OSC_ControlSurfaceIO::HandleExternalInput(OSC_ControlSurface *surface)
{
   if (inSocket_ != NULL && inSocket_->isOk())
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

void OSC_X32ControlSurfaceIO::HandleExternalInput(OSC_ControlSurface *surface)
{
   if (inSocket_ != NULL && inSocket_->isOk())
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
                   
                   if (message->addressPattern() == "/-stat/selidx")
                   {
                       string x32Select = message->addressPattern() + '/';
                       if (value < 10)
                           x32Select += '0';
                       x32Select += int_to_string(value);
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
OSC_ControlSurface::OSC_ControlSurface(CSurfIntegrator *const csi, Page *page, const char *name, int numChannels, int channelOffset, const char *templateFilename, const char *zoneFolder, const char *fxZoneFolder, OSC_ControlSurfaceIO *surfaceIO) : ControlSurface(csi, page, name, numChannels, channelOffset), templateFilename_(templateFilename), surfaceIO_(surfaceIO)

{
    zoneManager_ = new ZoneManager(csi_, this, zoneFolder, fxZoneFolder);

    ProcessOSCWidgetFile(string(GetResourcePath()) + "/CSI/Surfaces/OSC/" + templateFilename);
    InitHardwiredWidgets(this);
    zoneManager_->Initialize();
}

void OSC_ControlSurface::ProcessOSCMessage(const string &message, double value)
{
    if (CSIMessageGeneratorsByMessage_.Exists(message.c_str()))
        CSIMessageGeneratorsByMessage_.Get(message.c_str())->ProcessMessage(value);
    
    if (g_surfaceInDisplay)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "IN <- %s %s  %f  \n", name_.c_str(), message.c_str(), value);
        ShowConsoleMsg(buffer);
    }
}

void OSC_ControlSurface::SendOSCMessage(const char *zoneName)
{
    string oscAddress(zoneName);
    ReplaceAllWith(oscAddress, s_BadFileChars, "_");
    oscAddress = "/" + oscAddress;

    surfaceIO_->SendOSCMessage(oscAddress.c_str());
        
    if (g_surfaceOutDisplay)
        ShowConsoleMsg((string(zoneName) + "->LoadingZone---->" + name_ + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(const char *oscAddress, int value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
        
    if (g_surfaceOutDisplay)
        ShowConsoleMsg(("OUT->" + name_ + " " + string(oscAddress) + " " + int_to_string(value) + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(const char *oscAddress, double value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
        
    if (g_surfaceOutDisplay)
        ShowConsoleMsg(("OUT->" + name_ + " " + string(oscAddress) + " " + double_to_string(value) + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(const char *oscAddress, const char *value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
        
    if (g_surfaceOutDisplay)
        ShowConsoleMsg(("OUT->" + name_ + " " + string(oscAddress) + " " + string(value) + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor *feedbackProcessor, const char *oscAddress, double value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
    
    if (g_surfaceOutDisplay)
        ShowConsoleMsg(("OUT->" + name_ + " " + feedbackProcessor->GetWidget()->GetName() + " " + oscAddress + " " + double_to_string(value) + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor *feedbackProcessor, const char *oscAddress, int value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);

    if (g_surfaceOutDisplay)
        ShowConsoleMsg(("OUT->" + name_ + " " + feedbackProcessor->GetWidget()->GetName() + " " + oscAddress + " " + int_to_string(value) + "\n").c_str());
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor *feedbackProcessor, const char *oscAddress, const char *value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);

    if (g_surfaceOutDisplay)
        ShowConsoleMsg(("OUT->" + name_ + " " + feedbackProcessor->GetWidget()->GetName() + " " + oscAddress + " " + string(value) + "\n").c_str());
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
    // private:
    currentPageIndex_ = 0;

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
        
        int offset = CSurf_TrackToID(leftPtr, true);
        
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


void localize_init(void * (*GetFunc)(const char *name))
{
    *(void **)&importedLocalizeFunc = GetFunc("__localizeFunc");
    *(void **)&importedLocalizeMenu = GetFunc("__localizeMenu");
    *(void **)&importedLocalizeInitializeDialog = GetFunc("__localizeInitializeDialog");
    *(void **)&importedLocalizePrepareDialog = GetFunc("__localizePrepareDialog");
}
