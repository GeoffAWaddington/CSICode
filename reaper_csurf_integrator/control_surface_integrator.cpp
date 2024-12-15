//
//  control_surface_integrator.cpp
//  reaper_control_surface_integrator
//
//

#define INCLUDE_LOCALIZE_IMPORT_H
#include "control_surface_integrator.h"
#include "control_surface_midi_widgets.h"
#include "control_surface_OSC_widgets.h"
#include "control_surface_Reaper_actions.h"
#include "control_surface_manager_actions.h"

#include "../WDL/dirscan.h"
#include "resource.h"

extern WDL_DLGRET dlgProcMainConfig(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

extern reaper_plugin_info_t *g_reaper_plugin_info;

extern void WidgetMoved(ZoneManager *zoneManager, Widget *widget, int modifier);

int g_minNumParamSteps = 2;
int g_maxNumParamSteps = 30;
bool g_surfaceRawInDisplay;
bool g_surfaceInDisplay;
bool g_surfaceOutDisplay;
bool g_fxParamsWrite;

void GetPropertiesFromTokens(int start, int finish, const string_list &tokens, PropertyList &properties)
{
    for (int i = start; i < finish; ++i)
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
    for (int i = 0; i < numSteps; ++i)
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

    for (int i = 0; i < numSteps; ++i)
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
////////////////////////////////7/////////////////////////////////////////////////////////////////////////////////////////
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

// files
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

//////////////////////////////////////////////////////////////////////////////
// Midi_ControlSurface
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

        if (tokens[0] == "WidgetEnd")    // Widget list complete
            break;
        
        tokenLines.push_back(tokens);
    }
    
    if (tokenLines.size() < 1)
        return;
    
    for (int i = 0; i < tokenLines.size(); ++i)
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

//////////////////////////////////////////////////////////////////////////////
// OSC_ControlSurface
//////////////////////////////////////////////////////////////////////////////
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

        if (tokens[0] == "WidgetEnd")    // Widget list complete
            break;
        
        tokenLines.push_back(tokens);
    }

    for (int i = 0; i < (int)tokenLines.size(); ++i)
    {
        if (tokenLines[i].size() > 1 && tokenLines[i][0] == "Control")
            AddCSIMessageGenerator(tokenLines[i][1], new CSIMessageGenerator(csi_, widget));
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "AnyPress")
            AddCSIMessageGenerator(tokenLines[i][1], new AnyPress_CSIMessageGenerator(csi_, widget));
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "Touch")
            AddCSIMessageGenerator(tokenLines[i][1], new Touch_CSIMessageGenerator(csi_, widget));
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "X32Fader")
            AddCSIMessageGenerator(tokenLines[i][1], new X32_Fader_OSC_MessageGenerator(csi_, widget));
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "X32RotaryToEncoder")
            AddCSIMessageGenerator(tokenLines[i][1], new X32_RotaryToEncoder_OSC_MessageGenerator(csi_, widget));
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "FB_Processor")
            widget->AddFeedbackProcessor(new OSC_FeedbackProcessor(csi_, this, widget, tokenLines[i][1]));
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "FB_IntProcessor")
            widget->AddFeedbackProcessor(new OSC_IntFeedbackProcessor(csi_, this, widget, tokenLines[i][1]));
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "FB_X32Processor")
            widget->AddFeedbackProcessor(new OSC_X32FeedbackProcessor(csi_, this, widget, tokenLines[i][1]));
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "FB_X32IntProcessor")
            widget->AddFeedbackProcessor(new OSC_X32IntFeedbackProcessor(csi_, this, widget, tokenLines[i][1]));
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "FB_X32FaderProcessor")
            widget->AddFeedbackProcessor(new OSC_X32FaderFeedbackProcessor(csi_, this, widget, tokenLines[i][1]));
        else if (tokenLines[i].size() > 1 && tokenLines[i][0] == "FB_X32RotaryToEncoder")
            widget->AddFeedbackProcessor(new OSC_X32_RotaryToEncoderFeedbackProcessor(csi_, this, widget, tokenLines[i][1]));
    }
}

//////////////////////////////////////////////////////////////////////////////
// ControlSurface
//////////////////////////////////////////////////////////////////////////////
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
                        for (int j = 2; j < lines[i].size(); ++j)
                        {
                            if ( ! accelerationValuesForDecrement_.Exists(widgetClass))
                                accelerationValuesForDecrement_.Insert(widgetClass, new WDL_IntKeyedArray<int>());
                            
                            if (accelerationValuesForDecrement_.Exists(widgetClass))
                                accelerationValuesForDecrement_.Get(widgetClass)->Insert(strtol(lines[i][j].c_str(), NULL, 16), j - 2);
                        }
                    else if (lines[i][1] == "Inc")
                        for (int j = 2; j < lines[i].size(); ++j)
                        {
                            if ( ! accelerationValuesForIncrement_.Exists(widgetClass))
                                accelerationValuesForIncrement_.Insert(widgetClass, new WDL_IntKeyedArray<int>());
                            
                            if (accelerationValuesForIncrement_.Exists(widgetClass))
                                accelerationValuesForIncrement_.Get(widgetClass)->Insert(strtol(lines[i][j].c_str(), NULL, 16), j - 2);
                        }
                    else if (lines[i][1] == "Val")
                        for (int j = 2; j < lines[i].size(); ++j)
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

//////////////////////////////////////////////////////////////////////////////
// Midi_ControlSurface
//////////////////////////////////////////////////////////////////////////////
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

            if (tokens.size() > 0 && tokens[0] != "Widget")
                valueLines.push_back(tokens);
            
            if (tokens.size() > 0 && tokens[0] == "AccelerationValuesEnd")
                ProcessValues(valueLines);

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

//////////////////////////////////////////////////////////////////////////////
// OSC_ControlSurface
//////////////////////////////////////////////////////////////////////////////
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

            if (tokens.size() > 0 && tokens[0] != "Widget")
                valueLines.push_back(tokens);
            
            if (tokens.size() > 0 && tokens[0] == "AccelerationValuesEnd")
                ProcessValues(valueLines);

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
// CSurfIntegrator
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
    actions_.Insert("MoveEditCursor", new MoveCursor());
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
    actions_.Insert("SetHoldTime", new SetHoldTime());
    actions_.Insert("ToggleEnableFocusedFXMapping", new ToggleEnableFocusedFXMapping());
    actions_.Insert("ToggleEnableFocusedFXParamMapping", new ToggleEnableFocusedFXParamMapping());
    actions_.Insert("LearnFocusedFX", new LearnFocusedFX());
    actions_.Insert("GoZone", new GoZone());
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
        char tmp[MEDBUF];
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
                PropertyList pList;
                string_list properties;
                properties.push_back(line.c_str());
                GetPropertiesFromTokens(0, 1, properties, pList);

                const char *versionProp = pList.get_prop(PropertyType_Version);
                if (versionProp)
                {
                    if (strcmp(versionProp, s_MajorVersionToken))
                    {
                        char tmp[MEDBUF];
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
                else
                {
                    char tmp[MEDBUF];
                    snprintf(tmp, sizeof(tmp), __LOCALIZE_VERFMT("Cannot read version %s.","csi_mbox"), "");
                    MessageBox(g_hwnd, tmp, __LOCALIZE("CSI.ini no version","csi_mbox"), MB_OK);

                    iniFile.close();
                    return;
                }
            }
            
            if (line == "" || line[0] == '\r' || line[0] == '/') // ignore comment lines and blank lines
                continue;
            
            string_list tokens;
            GetTokens(tokens, line.c_str());
            
            vector<string> tmp;
            
            for (int i = 0; i < tokens.size(); ++i)
                tmp.push_back(string(tokens[i]));
            
            
            if (tokens.size() > 0) // ignore comment lines and blank lines
            {
                PropertyList pList;
                GetPropertiesFromTokens(0, tokens.size(), tokens, pList);
                
                if (const char *typeProp = pList.get_prop(PropertyType_SurfaceType))
                {
                    if (const char *nameProp = pList.get_prop(PropertyType_SurfaceName))
                    {
                        if (const char *channelCountProp = pList.get_prop(PropertyType_SurfaceChannelCount))
                        {
                            int channelCount = atoi(channelCountProp);
                            
                            if ( ! strcmp(typeProp, s_MidiSurfaceToken) && tokens.size() == 7)
                            {
                                if (pList.get_prop(PropertyType_MidiInput) != NULL &&
                                    pList.get_prop(PropertyType_MidiOutput) != NULL &&
                                    pList.get_prop(PropertyType_MIDISurfaceRefreshRate) != NULL &&
                                    pList.get_prop(PropertyType_MaxMIDIMesssagesPerRun) != NULL)
                                {
                                    int midiIn = atoi(pList.get_prop(PropertyType_MidiInput));
                                    int midiOut = atoi(pList.get_prop(PropertyType_MidiOutput));
                                    int surfaceRefreshRate = atoi(pList.get_prop(PropertyType_MIDISurfaceRefreshRate));
                                    int maxMIDIMesssagesPerRun = atoi(pList.get_prop(PropertyType_MaxMIDIMesssagesPerRun));
                                    
                                    midiSurfacesIO_.Add(new Midi_ControlSurfaceIO(this, nameProp, channelCount, GetMidiInputForPort(midiIn), GetMidiOutputForPort(midiOut), surfaceRefreshRate, maxMIDIMesssagesPerRun));
                                }
                            }
                            else if (( ! strcmp(typeProp, s_OSCSurfaceToken) || ! strcmp(typeProp, s_OSCX32SurfaceToken)) && tokens.size() == 7)
                            {
                                if (pList.get_prop(PropertyType_ReceiveOnPort) != NULL &&
                                    pList.get_prop(PropertyType_TransmitToPort) != NULL &&
                                    pList.get_prop(PropertyType_TransmitToIPAddress) != NULL &&
                                    pList.get_prop(PropertyType_MaxPacketsPerRun) != NULL)
                                {
                                    const char *receiveOnPort = pList.get_prop(PropertyType_ReceiveOnPort);
                                    const char *transmitToPort = pList.get_prop(PropertyType_TransmitToPort);
                                    const char *transmitToIPAddress = pList.get_prop(PropertyType_TransmitToIPAddress);
                                    int maxPacketsPerRun = atoi(pList.get_prop(PropertyType_MaxPacketsPerRun));
                                    
                                    if ( ! strcmp(typeProp, s_OSCSurfaceToken))
                                        oscSurfacesIO_.Add(new OSC_ControlSurfaceIO(this, nameProp, channelCount, receiveOnPort, transmitToPort, transmitToIPAddress, maxPacketsPerRun));
                                    else if ( ! strcmp(typeProp, s_OSCX32SurfaceToken))
                                        oscSurfacesIO_.Add(new OSC_X32ControlSurfaceIO(this, nameProp, channelCount, receiveOnPort, transmitToPort, transmitToIPAddress, maxPacketsPerRun));
                                }
                            }
                        }
                    }
                }
                else if (const char *pageNameProp = pList.get_prop(PropertyType_PageName))
                {
                    bool followMCP = true;
                    bool synchPages = true;
                    bool isScrollLinkEnabled = false;
                    bool isScrollSynchEnabled = false;

                    currentPage = NULL;
                    
                    if (tokens.size() > 1)
                    {
                        if (const char *pageFollowsMCPProp = pList.get_prop(PropertyType_PageFollowsMCP))
                        {
                            if ( ! strcmp(pageFollowsMCPProp, "No"))
                                followMCP = false;
                        }
                        
                        if (const char *synchPagesProp = pList.get_prop(PropertyType_SynchPages))
                        {
                            if ( ! strcmp(synchPagesProp, "No"))
                                synchPages = false;
                        }
                        
                        if (const char *scrollLinkProp = pList.get_prop(PropertyType_ScrollLink))
                        {
                            if ( ! strcmp(scrollLinkProp, "Yes"))
                                isScrollLinkEnabled = true;
                        }
                        
                        if (const char *scrollSynchProp = pList.get_prop(PropertyType_ScrollSynch))
                        {
                            if ( ! strcmp(scrollSynchProp, "Yes"))
                                isScrollSynchEnabled = true;
                        }
                        
                        currentPage = new Page(this, pageNameProp, followMCP, synchPages, isScrollLinkEnabled, isScrollSynchEnabled);
                        pages_.Add(currentPage);
                    }
                }
                else if (const char *broadcasterProp = pList.get_prop(PropertyType_Broadcaster))
                {
                    currentBroadcaster = broadcasterProp;
                }
                else if (currentPage && tokens.size() > 2 && currentBroadcaster != "" && pList.get_prop(PropertyType_Listener) != NULL)
                {
                    if (currentPage && tokens.size() > 2 && currentBroadcaster != "")
                    {
                        
                        ControlSurface *broadcaster = NULL;
                        ControlSurface *listener = NULL;
                        
                        const WDL_PtrList<ControlSurface> &list = currentPage->GetSurfaces();
                        
                        string currentSurface = string(pList.get_prop(PropertyType_Listener));
                        
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
                            listener->GetZoneManager()->SetListenerCategories(pList);
                        }
                    }
                }
                else if (currentPage && tokens.size() == 5)
                {
                    if (const char *surfaceProp = pList.get_prop(PropertyType_Surface))
                    {
                        if (const char *surfaceFolderProp = pList.get_prop(PropertyType_SurfaceFolder))
                        {
                            if (const char *startChannelProp = pList.get_prop(PropertyType_StartChannel))
                            {
                                int startChannel = atoi(startChannelProp);
                                
                                string baseDir = string(GetResourcePath()) + string("/CSI/Surfaces/");
                                
                                string surfaceFile = baseDir + surfaceFolderProp + "/Surface.txt";
                                
                                string zoneFolder = baseDir + surfaceFolderProp + "/Zones";
                                if (const char *zoneFolderProp = pList.get_prop(PropertyType_ZoneFolder))
                                    zoneFolder = baseDir + zoneFolderProp + "/Zones";
                                
                                string fxZoneFolder = baseDir + surfaceFolderProp + "/FXZones";
                                if (const char *fxZoneFolderProp = pList.get_prop(PropertyType_FXZoneFolder))
                                    zoneFolder = baseDir + fxZoneFolderProp + "/Zones";

                                bool foundIt = false;
                                
                                for (int i = 0; i < midiSurfacesIO_.GetSize(); ++i)
                                {
                                    Midi_ControlSurfaceIO *const io = midiSurfacesIO_.Get(i);
                                    
                                    if ( ! strcmp(surfaceProp, io->GetName()))
                                    {
                                        foundIt = true;
                                        currentPage->AddSurface(new Midi_ControlSurface(this, currentPage, surfaceFolderProp, startChannel, surfaceFile.c_str(), zoneFolder.c_str(), fxZoneFolder.c_str(), io));
                                        break;
                                    }
                                }
                                
                                if ( ! foundIt)
                                {
                                    for (int i = 0; i < oscSurfacesIO_.GetSize(); ++i)
                                    {
                                        OSC_ControlSurfaceIO *const io = oscSurfacesIO_.Get(i);
                                        
                                        if ( ! strcmp(surfaceProp, io->GetName()))
                                        {
                                            foundIt = true;
                                            currentPage->AddSurface(new OSC_ControlSurface(this, currentPage, surfaceFolderProp, startChannel, surfaceFile.c_str(), zoneFolder.c_str(), fxZoneFolder.c_str(), io));
                                            break;
                                        }
                                    }
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
    
    if (pages_.GetSize() == 0)
        pages_.Add(new Page(this, "Home", false, false, false, false));
    
    for (int i = 0; i < pages_.GetSize(); ++i)
    {
        for (int j = 0; j < pages_.Get(i)->GetSurfaces().GetSize(); ++j)
            pages_.Get(i)->GetSurfaces().Get(j)->ForceClear();

        pages_.Get(i)->OnInitialization();
    }
}

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
    provideFeedback_ = true;
    
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
    
    const char *feedback = widgetProperties_.get_prop(PropertyType_Feedback);
    if (feedback && !strcmp(feedback, "No"))
        provideFeedback_ = false;

    for (int i = 0; i < (int)(*paramsAndProperties).size(); ++i)
        if (!strcmp((*paramsAndProperties)[i], "NoFeedback"))
            provideFeedback_ = false;

    const char *actionName = "";
    
    if (params.size() > 0)
        actionName = params[0];
    
    // Action with int param, could include leading minus sign
    if (params.size() > 1 && (isdigit(params[1][0]) ||  params[1][0] == '-'))  // C++ 2003 says empty strings can be queried without catastrophe :)
    {
        intParam_= atol(params[1].c_str());
    }
    
    if (!strcmp(actionName, "Bank") && (params.size() > 2 && (isdigit(params[2][0]) ||  params[2][0] == '-')))  // C++ 2003 says empty strings can be queried without catastrophe :)
    {
        stringParam_ = params[1];
        intParam_= atol(params[2].c_str());
    }
        
    // Action with param index, must be positive
    if (params.size() > 1 && isdigit(params[1][0]))  // C++ 2003 says empty strings can be queried without catastrophe :)
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
    
    if (!strcmp(actionName, "FixedTextDisplay") && (params.size() > 2 && (isdigit(params[2][0]))))  // C++ 2003 says empty strings can be queried without catastrophe :)
    {
        stringParam_ = params[1];
        paramIndex_= atol(params[2].c_str());
    }
    
    if (params.size() > 0)
        SetColor(params, supportsColor_, supportsTrackColor_, colorValues_);
    
    GetSteppedValues(widget, action_, zone_, paramIndex_, params, widgetProperties_, deltaValue_, acceleratedDeltaValues_, rangeMinimum_, rangeMaximum_, steppedValues_, acceleratedTickValues_);

    if (acceleratedTickValues_.size() < 1)
        acceleratedTickValues_.push_back(0);
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
    return zone_->GetAlias();
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

void ActionContext::ForceWidgetValue(const char *value)
{
    widget_->ForceValue(widgetProperties_, value ? value : "");
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

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Zone
///////////////////////////////////////////////////////////////////////////////////////////////////////
void Zone::InitSubZones(const string_list &subZones, const char *widgetSuffix)
{
    for (int i = 0; i < (int)subZones.size(); ++i)
    {
        const WDL_StringKeyedArray<CSIZoneInfo*> &zfp = zoneManager_->GetZoneInfo();
        
        if (zfp.Exists(subZones[i]))
        {
            SubZone *subZone = new SubZone(csi_, zoneManager_, GetNavigator(), GetSlotIndex(), subZones[i].c_str(), zfp.Get(subZones[i])->alias.c_str(), zfp.Get(subZones[i])->filePath.c_str(), this);
            zoneManager_->LoadZoneFile(subZone, widgetSuffix);
            subZones_.push_back(subZone);
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

void Zone::AddWidget(Widget *widget)
{
    if (widgets_.Find(widget) < 0)
        widgets_.Add(widget);
}

void Zone::Activate()
{
    UpdateCurrentActionContextModifiers();
    
    for (int i = 0; i < widgets_.GetSize(); ++i)
    {
        if (!strcmp(widgets_.Get(i)->GetName(), "OnZoneActivation"))
            for (int j = 0; j < GetActionContexts(widgets_.Get(i)).GetSize(); ++j)
                GetActionContexts(widgets_.Get(i)).Get(j)->DoAction(1.0);
            
        widgets_.Get(i)->Configure(GetActionContexts(widgets_.Get(i)));
    }

    isActive_ = true;
    
    if (!strcmp(GetName(), "VCA"))
        zoneManager_->GetSurface()->GetPage()->VCAModeActivated();
    else if (!strcmp(GetName(), "Folder"))
        zoneManager_->GetSurface()->GetPage()->FolderModeActivated();
    else if (!strcmp(GetName(), "SelectedTracks"))
        zoneManager_->GetSurface()->GetPage()->SelectedTracksModeActivated();

    zoneManager_->GetSurface()->SendOSCMessage(GetName());

    for (int i = 0; i < subZones_.size(); ++i)
        subZones_[i]->Deactivate();

    for (int i = 0; i < includedZones_.size(); ++i)
        includedZones_[i]->Activate();
}

void Zone::Deactivate()
{    
    for (int i = 0; i < widgets_.GetSize(); ++i)
    {
        for (int j = 0; j < GetActionContexts(widgets_.Get(i)).GetSize(); ++j)
        {
            GetActionContexts(widgets_.Get(i)).Get(j)->UpdateWidgetValue(0.0);
            GetActionContexts(widgets_.Get(i)).Get(j)->UpdateWidgetValue("");

            if (!strcmp(widgets_.Get(i)->GetName(), "OnZoneDeactivation"))
                GetActionContexts(widgets_.Get(i)).Get(j)->DoAction(1.0);
        }
    }

    isActive_ = false;
    
    if (!strcmp(GetName(), "VCA"))
        zoneManager_->GetSurface()->GetPage()->VCAModeDeactivated();
    else if (!strcmp(GetName(), "Folder"))
        zoneManager_->GetSurface()->GetPage()->FolderModeDeactivated();
    else if (!strcmp(GetName(), "SelectedTracks"))
        zoneManager_->GetSurface()->GetPage()->SelectedTracksModeDeactivated();
    
    for (int i = 0; i < includedZones_.size(); ++i)
        includedZones_[i]->Deactivate();

    for (int i = 0; i < subZones_.size(); ++i)
        subZones_[i]->Deactivate();
}

void Zone::RequestUpdate()
{
    if (! isActive_)
        return;
    
    for (int i = 0; i < subZones_.size(); ++i)
        subZones_[i]->RequestUpdate();

    for (int i =  0; i < includedZones_.size(); ++i)
        includedZones_[i]->RequestUpdate();
    
    for (int i = 0; i < widgets_.GetSize(); ++i)
    {
        if ( ! widgets_.Get(i)->GetHasBeenUsedByUpdate())
        {
            widgets_.Get(i)->SetHasBeenUsedByUpdate();
            RequestUpdateWidget(widgets_.Get(i));
        }
    }
}

void Zone::SetXTouchDisplayColors(const char *colors)
{
    for (int i = 0; i < widgets_.GetSize(); ++i)
        widgets_.Get(i)->SetXTouchDisplayColors(colors);
}

void Zone::RestoreXTouchDisplayColors()
{
    for (int i = 0; i < widgets_.GetSize(); ++i)
        widgets_.Get(i)->RestoreXTouchDisplayColors();
}

void Zone::DoAction(Widget *widget, bool &isUsed, double value)
{
    if (! isActive_ || isUsed)
        return;
        
    for (int i = 0; i < subZones_.size(); ++i)
        subZones_[i]->DoAction(widget, isUsed, value);

    if (isUsed)
        return;

    if (widgets_.Find(widget) >= 0)
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
        for (int i = 0; i < includedZones_.size(); ++i)
            includedZones_[i]->DoAction(widget, isUsed, value);
    }
}

void Zone::DoRelativeAction(Widget *widget, bool &isUsed, double delta)
{
    if (! isActive_ || isUsed)
        return;
    
    for (int i = 0; i < subZones_.size(); ++i)
        subZones_[i]->DoRelativeAction(widget, isUsed, delta);

    if (isUsed)
        return;

    if (widgets_.Find(widget) >= 0)
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
        for (int i = 0; i < includedZones_.size(); ++i)
            includedZones_[i]->DoRelativeAction(widget, isUsed, delta);
    }
}

void Zone::DoRelativeAction(Widget *widget, bool &isUsed, int accelerationIndex, double delta)
{
    if (! isActive_ || isUsed)
        return;

    for (int i = 0; i < subZones_.size(); ++i)
        subZones_[i]->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
    
    if (isUsed)
        return;

    if (widgets_.Find(widget) >= 0)
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
        for (int i = 0; i < includedZones_.size(); ++i)
            includedZones_[i]->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
    }
}

void Zone::DoTouch(Widget *widget, const char *widgetName, bool &isUsed, double value)
{
    if (! isActive_ || isUsed)
        return;

    for (int i = 0; i < subZones_.size(); ++i)
        subZones_[i]->DoTouch(widget, widgetName, isUsed, value);
    
    if (isUsed)
        return;

    if (widgets_.Find(widget) >= 0)
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
        for (int i = 0; i < includedZones_.size(); ++i)
            includedZones_[i]->DoTouch(widget, widgetName, isUsed, value);
    }
}

void Zone::UpdateCurrentActionContextModifiers()
{
    for (int i = 0; i < widgets_.GetSize(); ++i)
    {
        UpdateCurrentActionContextModifier(widgets_.Get(i));
        widgets_.Get(i)->Configure(GetActionContexts(widgets_.Get(i), currentActionContextModifiers_.Get(widgets_.Get(i))));
    }
    
    for (int i = 0; i < includedZones_.size(); ++i)
        includedZones_[i]->UpdateCurrentActionContextModifiers();

    for (int i = 0; i < subZones_.size(); ++i)
    {
        subZones_[i]->UpdateCurrentActionContextModifiers();
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
            if (currentActionContextModifiers_.Exists(widget))
            {
                if (currentActionContextModifiers_.GetPtr(widget))
                    *currentActionContextModifiers_.GetPtr(widget) = mods.Get()[i];
            }
            else
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

void  Widget::ForceValue(const PropertyList &properties, const char * const &value)
{
    for (int i = 0; i < feedbackProcessors_.GetSize(); ++i)
        feedbackProcessors_.Get(i)->ForceValue(properties, value);
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

void Widget::SetXTouchDisplayColors(const char *colors)
{
    for (int i = 0; i < feedbackProcessors_.GetSize(); ++i)
        feedbackProcessors_.Get(i)->SetXTouchDisplayColors(colors);
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
    char tmp[MEDBUF];
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

////////////////////////////////////////////////////////////////////////////////////////////////////////
// ZoneManager
////////////////////////////////////////////////////////////////////////////////////////////////////////
ZoneManager::ZoneManager(CSurfIntegrator *const csi, ControlSurface *surface, const string &zoneFolder, const string &fxZoneFolder) : csi_(csi), surface_(surface), zoneFolder_(zoneFolder), fxZoneFolder_(fxZoneFolder == "" ? zoneFolder : fxZoneFolder), zoneInfo_(true, disposeAction)
{
    holdDelayAmount_ = 1.0;
    
    homeZone_ = NULL;

    focusedFXZone_ = NULL;
    fxSlotZone_ = NULL;
    learnFocusedFXZone_ = NULL;
    focusedFXParamZone_ = NULL;
    
    listensToGoHome_ = false;
    listensToSends_ = false;
    listensToReceives_ = false;
    listensToFocusedFX_ = false;
    listensToFocusedFXParam_ = false;
    listensToFXMenu_ = false;
    listensToLocalFXSlot_ = false;
    listensToSelectedTrackFX_ = false;

    isFocusedFXParamMappingEnabled_ = false;
    
    isFocusedFXMappingEnabled_ = true;
    
    trackSendOffset_ = 0;
    trackReceiveOffset_ = 0;
    trackFXMenuOffset_ = 0;
    selectedTrackSendOffset_ = 0;
    selectedTrackReceiveOffset_ = 0;
    selectedTrackFXMenuOffset_ = 0;
    masterTrackFXMenuOffset_ = 0;
}

Navigator *ZoneManager::GetNavigatorForTrack(MediaTrack *track) { return surface_->GetPage()->GetNavigatorForTrack(track); }
Navigator *ZoneManager::GetMasterTrackNavigator() { return surface_->GetPage()->GetMasterTrackNavigator(); }
Navigator *ZoneManager::GetSelectedTrackNavigator() { return surface_->GetPage()->GetSelectedTrackNavigator(); }
Navigator *ZoneManager::GetFocusedFXNavigator() { return surface_->GetPage()->GetFocusedFXNavigator(); }
int ZoneManager::GetNumChannels() { return surface_->GetNumChannels(); }

void ZoneManager::Initialize()
{
    PreProcessZones();

    if ( ! zoneInfo_.Exists("Home"))
    {
        char tmp[MEDBUF];
        snprintf(tmp, sizeof(tmp), __LOCALIZE_VERFMT("%s needs a Home Zone to operate, please recheck your installation", "csi_mbox"), surface_->GetName());
        MessageBox(g_hwnd, tmp, __LOCALIZE("CSI Missing Home Zone", "csi_mbox"), MB_OK);
        return;
    }
            
    homeZone_ = new Zone(csi_, this, GetSelectedTrackNavigator(), 0, "Home", "Home", zoneInfo_.Get("Home")->filePath);
    LoadZoneFile(homeZone_, "");
    
    string_list zoneList;
    if (zoneInfo_.Exists("GoZones"))
        LoadZoneMetadata(zoneInfo_.Get("GoZones")->filePath.c_str(), zoneList);
    LoadZones(goZones_, zoneList);
    
    if (zoneInfo_.Exists("FocusedFXParam"))
    {
        focusedFXParamZone_ = new Zone(csi_, this, GetFocusedFXNavigator(), 0, "FocusedFXParam", "FocusedFXParam", zoneInfo_.Get("FocusedFXParam")->filePath);
        LoadZoneFile(focusedFXParamZone_, "");
    }
        
    homeZone_->Activate();
}

void ZoneManager::PreProcessZoneFile(const char *filePath)
{
    try
    {
        fpistream file(filePath);
        
        CSIZoneInfo info;
        info.filePath = filePath;
                 
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            if (line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
            
            string_list tokens;
            GetTokens(tokens, line.c_str());

            if (tokens[0] == "Zone" && tokens.size() > 1)
            {
                info.alias = tokens.size() > 2 ? tokens[2] : tokens[1];
                AddZoneFilePath(tokens[1].c_str(), info);
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

static ModifierManager s_modifierManager(NULL);

void ZoneManager::GetWidgetNameAndModifiers(const char *line, string &baseWidgetName, int &modifier, bool &isValueInverted, bool &isFeedbackInverted, double &holdDelayAmount, bool &isDecrease, bool &isIncrease)
{
    string_list tokens;
    GetSubTokens(tokens, line, '+');
    
    baseWidgetName = tokens[tokens.size() - 1];

    if (tokens.size() > 1)
    {
        for (int i = 0; i < tokens.size() - 1; ++i)
        {
            if (tokens[i].find("Touch") != string::npos)
                modifier += 1;
            else if (tokens[i] == "Toggle")
                modifier += 2;

            else if (tokens[i] == "Invert")
                isValueInverted = true;
            else if (tokens[i] == "InvertFB")
                isFeedbackInverted = true;
            else if (tokens[i] == "Hold")
                holdDelayAmount = holdDelayAmount_;
            else if (tokens[i] == "Decrease")
                isDecrease = true;
            else if (tokens[i] == "Increase")
                isIncrease = true;
        }
    }
    
    tokens.erase(tokens.size() - 1);
    
    modifier += s_modifierManager.GetModifierValue(tokens);
}

void ZoneManager::GetNavigatorsForZone(const char *zoneName, const char *navigatorName, ptrvector<Navigator *> &navigators)
{
    if (!strcmp(navigatorName, "MasterTrackNavigator") || !strcmp(zoneName, "MasterTrack"))
        navigators.push_back(GetMasterTrackNavigator());
    else if (!strcmp(zoneName, "MasterTrackFXMenu"))
        for (int i = 0; i < GetNumChannels(); ++i)
            navigators.push_back(GetMasterTrackNavigator());
    else if (!strcmp(navigatorName, "TrackNavigator") ||
             !strcmp(zoneName, "Track") ||
             !strcmp(zoneName, "VCA") ||
             !strcmp(zoneName, "Folder") ||
             !strcmp(zoneName, "SelectedTracks") ||
             !strcmp(zoneName, "TrackSend") ||
             !strcmp(zoneName, "TrackReceive") ||
             !strcmp(zoneName, "TrackFXMenu"))
        for (int i = 0; i < GetNumChannels(); ++i)
        {
            Navigator *channelNavigator = GetSurface()->GetPage()->GetNavigatorForChannel(i + GetSurface()->GetChannelOffset());
            if (channelNavigator)
                navigators.push_back(channelNavigator);
        }
    else if (!strcmp(zoneName, "SelectedTrack") ||
             !strcmp(zoneName, "SelectedTrackSend") ||
             !strcmp(zoneName, "SelectedTrackReceive") ||
             !strcmp(zoneName, "SelectedTrackFXMenu"))
        for (int i = 0; i < GetNumChannels(); ++i)
            navigators.push_back(GetSelectedTrackNavigator());
    else if (!strcmp(navigatorName, "FocusedFXNavigator"))
        navigators.push_back(GetFocusedFXNavigator());
    else
        navigators.push_back(GetSelectedTrackNavigator());
}

void ZoneManager::LoadZones(ptrvector<Zone *> &zones, string_list &zoneList)
{
    for (int i = 0; i < zoneList.size(); ++i)
    {
        string_list tokens;
        GetTokens(tokens, zoneList[i]);
        
        char zoneName[MEDBUF];
        snprintf(zoneName, sizeof(zoneName), "%s", tokens[0].c_str());
        
        char navigatorName[MEDBUF];
        navigatorName[0] = 0;
        if(tokens.size() > 1)
            snprintf(navigatorName, sizeof(navigatorName), "%s", tokens[1].c_str());
    
        if (zoneInfo_.Exists(zoneName))
        {
            ptrvector<Navigator *> navigators;
            GetNavigatorsForZone(zoneName, navigatorName, navigators);
            
            if (navigators.size() == 1)
            {
                Zone *zone = new Zone(csi_, this, navigators[0], 0, string(zoneName), zoneInfo_.Get(zoneName)->alias, zoneInfo_.Get(zoneName)->filePath);
                if (zone)
                {
                    LoadZoneFile(zone, "");
                    zones.push_back(zone);
                }
            }
            else if (navigators.size() > 1)
            {
                for (int j = 0; j < navigators.size(); ++j)
                {
                    char buf[MEDBUF];
                    snprintf(buf, sizeof(buf), "%s%d", string(zoneName).c_str(), j + 1);
                    
                    Zone *zone = new Zone(csi_, this, navigators[j], j, string(zoneName), string(buf), zoneInfo_.Get(zoneName)->filePath);
                    if (zone)
                    {
                        snprintf(buf, sizeof(buf), "%d", j + 1);
                        LoadZoneFile(zone, buf);
                        zones.push_back(zone);
                    }
                }
            }
        }
    }
}

void ZoneManager::LoadZoneFile(Zone *zone, const char *widgetSuffix)
{
    LoadZoneFile(zone, zone->GetSourceFilePath(), widgetSuffix);
}

void ZoneManager::LoadZoneFile(Zone *zone, const char *filePath, const char *widgetSuffix)
{
    int lineNumber = 0;
    bool isInIncludedZonesSection = false;
    string_list includedZonesList;
    bool isInSubZonesSection = false;
    string_list subZonesList;

    try
    {
        fpistream file(filePath);
        
        for (string line; getline(file, line) ; )
        {
            TrimLine(line);
            
            lineNumber++;
            
            if (line == "" || (line.size() > 0 && line[0] == '/')) // ignore blank lines and comment lines
                continue;
            
            if (line == s_BeginAutoSection || line == s_EndAutoSection)
                continue;
            
            ReplaceAllWith(line, "|", widgetSuffix);
            
            string_list tokens;
            GetTokens(tokens, line.c_str());
            
            if (tokens[0] == "Zone" || tokens[0] == "ZoneEnd")
                continue;
            
            else if (tokens[0] == "SubZones")
                isInSubZonesSection = true;
            else if (tokens[0] == "SubZonesEnd")
            {
                isInSubZonesSection = false;
                zone->InitSubZones(subZonesList, widgetSuffix);
            }
            else if (isInSubZonesSection)
                subZonesList.push_back(tokens[0]);
            
            else if (tokens[0] == "IncludedZones")
                isInIncludedZonesSection = true;
            else if (tokens[0] == "IncludedZonesEnd")
            {
                isInIncludedZonesSection = false;
                LoadZones(zone->GetIncludedZones(), includedZonesList);
            }
            else if (isInIncludedZonesSection)
                includedZonesList.push_back(tokens[0]);
            
            else if (tokens.size() > 1)
            {
                string widgetName;
                int modifier = 0;
                bool isValueInverted = false;
                bool isFeedbackInverted = false;
                double holdDelayAmount = 0.0;
                bool isDecrease = false;
                bool isIncrease = false;
                
                GetWidgetNameAndModifiers(tokens[0], widgetName, modifier, isValueInverted, isFeedbackInverted, holdDelayAmount,isDecrease, isIncrease);
                
                Widget *widget = GetSurface()->GetWidgetByName(widgetName.c_str());
                                            
                if (widget == NULL)
                    continue;

                zone->AddWidget(widget);

                string_list memberParams;

                for (int i = 1; i < tokens.size(); ++i)
                    memberParams.push_back(tokens[i]);
                
                // For legacy .zon definitions
                if (!strcmp(tokens[1], "NullDisplay"))
                    continue;
                
                ActionContext *context = csi_->GetActionContext(tokens[1], widget, zone, memberParams);
                
                if (isValueInverted)
                    context->SetIsValueInverted();
                
                if (isFeedbackInverted)
                    context->SetIsFeedbackInverted();
                
                if (holdDelayAmount != 0.0)
                    context->SetHoldDelayAmount(holdDelayAmount);
                
                vector<double> range;
                
                if (isDecrease)
                {
                    range.push_back(-2.0);
                    range.push_back(1.0);
                    context->SetRange(range);
                }
                else if (isIncrease)
                {
                    range.push_back(0.0);
                    range.push_back(2.0);
                    context->SetRange(range);
                }
                
                zone->AddActionContext(widget, modifier, context);
            }
        }
    }
    catch (exception)
    {
        char buffer[250];
        snprintf(buffer, sizeof(buffer), "Trouble in %s, around line %d\n", zone->GetSourceFilePath(), lineNumber);
        ShowConsoleMsg(buffer);
    }
}

void ZoneManager::AddListener(ControlSurface *surface)
{
    if (WDL_NOT_NORMALLY(!surface)) { return; }
    listeners_.push_back(surface->GetZoneManager());
}

void ZoneManager::SetListenerCategories(PropertyList &pList)
{
    if (const char *property =  pList.get_prop(PropertyType_GoHome))
        if (! strcmp(property, "Yes"))
            listensToGoHome_ = true;
    
    if (const char *property =  pList.get_prop(PropertyType_SelectedTrackSends))
        if (! strcmp(property, "Yes"))
            listensToSends_ = true;
    
    if (const char *property =  pList.get_prop(PropertyType_SelectedTrackReceives))
        if (! strcmp(property, "Yes"))
            listensToReceives_ = true;
    
    if (const char *property =  pList.get_prop(PropertyType_FocusedFX))
        if (! strcmp(property, "Yes"))
            listensToFocusedFX_ = true;
    
    if (const char *property =  pList.get_prop(PropertyType_FocusedFXParam))
        if (! strcmp(property, "Yes"))
            listensToFocusedFXParam_ = true;
    
    if (const char *property =  pList.get_prop(PropertyType_FXMenu))
        if (! strcmp(property, "Yes"))
            listensToFXMenu_ = true;
    
    if (const char *property =  pList.get_prop(PropertyType_LocalFXSlot))
        if (! strcmp(property, "Yes"))
            listensToLocalFXSlot_ = true;
    
    if (const char *property =  pList.get_prop(PropertyType_SelectedTrackFX))
        if (! strcmp(property, "Yes"))
            listensToSelectedTrackFX_ = true;
    
    if (const char *property =  pList.get_prop(PropertyType_Modifiers))
        if (! strcmp(property, "Yes"))
            surface_->SetListensToModifiers();;
}

void ZoneManager::CheckFocusedFXState()
{
    int trackNumber = 0;
    int itemNumber = 0;
    int takeNumber = 0;
    int fxSlot = 0;
    int paramIndex = 0;
    
    bool retVal = GetTouchedOrFocusedFX(1, &trackNumber, &itemNumber, &takeNumber, &fxSlot, &paramIndex);
    
    if (! isFocusedFXMappingEnabled_)
        return;

    if (! retVal || (retVal && (paramIndex & 0x01)))
    {
        if (focusedFXZone_ != NULL)
            ClearFocusedFX();
        
        return;
    }
    
    if (fxSlot < 0)
        return;
    
    MediaTrack *focusedTrack = NULL;

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
        char fxName[MEDBUF];
        TrackFX_GetFXName(focusedTrack, fxSlot, fxName, sizeof(fxName));
        
        if(focusedFXZone_ != NULL && focusedFXZone_->GetSlotIndex() == fxSlot && !strcmp(fxName, focusedFXZone_->GetName()))
            return;
        else
            ClearFocusedFX();
        
        if (zoneInfo_.Exists(fxName))
        {
            focusedFXZone_ = new Zone(csi_, this, GetFocusedFXNavigator(), fxSlot, fxName, zoneInfo_.Get(fxName)->alias, zoneInfo_.Get(fxName)->filePath.c_str());
            LoadZoneFile(focusedFXZone_, "");
            focusedFXZone_->Activate();
        }            
    }
}

void ZoneManager::GoSelectedTrackFX()
{
    selectedTrackFXZones_.clear();
    
    if (MediaTrack *selectedTrack = surface_->GetPage()->GetSelectedTrack())
    {
        for (int i = 0; i < TrackFX_GetCount(selectedTrack); ++i)
        {
            char fxName[MEDBUF];
            
            TrackFX_GetFXName(selectedTrack, i, fxName, sizeof(fxName));
            
            if (zoneInfo_.Exists(fxName))
            {
                Zone *zone = new Zone(csi_, this, GetSelectedTrackNavigator(), i, fxName, zoneInfo_.Get(fxName)->alias, zoneInfo_.Get(fxName)->filePath);
                LoadZoneFile(zone, "");
                selectedTrackFXZones_.push_back(zone);
                zone->Activate();
            }
        }
    }
}

void ZoneManager::GoFXSlot(MediaTrack *track, Navigator *navigator, int fxSlot)
{
    if (fxSlot > TrackFX_GetCount(track) - 1)
        return;
        
    char fxName[MEDBUF];
    
    TrackFX_GetFXName(track, fxSlot, fxName, sizeof(fxName));

    if (zoneInfo_.Exists(fxName))
    {
        ClearFXSlot();        
        fxSlotZone_ = new Zone(csi_, this, navigator, fxSlot, fxName, zoneInfo_.Get(fxName)->alias, zoneInfo_.Get(fxName)->filePath);
        LoadZoneFile(fxSlotZone_, "");
        fxSlotZone_->Activate();
    }
    else
        TrackFX_SetOpen(track, fxSlot, true);
}

void ZoneManager::UpdateCurrentActionContextModifiers()
{  
    if (learnFocusedFXZone_ != NULL)
        learnFocusedFXZone_->UpdateCurrentActionContextModifiers();

    if (focusedFXParamZone_ != NULL)
        focusedFXParamZone_->UpdateCurrentActionContextModifiers();
    
    if (focusedFXZone_ != NULL)
        focusedFXZone_->UpdateCurrentActionContextModifiers();
    
    for (int i = 0; i < selectedTrackFXZones_.size(); ++i)
        selectedTrackFXZones_[i]->UpdateCurrentActionContextModifiers();
    
    if (fxSlotZone_ != NULL)
        fxSlotZone_->UpdateCurrentActionContextModifiers();
    
    if (homeZone_ != NULL)
        homeZone_->UpdateCurrentActionContextModifiers();
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
    listFilesOfType(zoneFolder_ + "/", zoneFilesToProcess, ".zon"); // recursively find all .zon files, starting at zoneFolder
       
    if (zoneFilesToProcess.size() == 0)
    {
        char tmp[2048];
        snprintf(tmp, sizeof(tmp), __LOCALIZE_VERFMT("Please check your installation, cannot find Zone files for %s in:\r\n\r\n%s","csi_mbox"), GetSurface()->GetName(), zoneFolder_.c_str());
        MessageBox(g_hwnd, tmp, __LOCALIZE("Zone folder is missing or empty","csi_mbox"), MB_OK);

        return;
    }
          
    listFilesOfType(fxZoneFolder_ + "/", zoneFilesToProcess, ".zon"); // recursively find all .zon files, starting at fxZoneFolder
     
    for (int i = 0; i < (int)zoneFilesToProcess.size(); ++i)
        PreProcessZoneFile(zoneFilesToProcess[i]);
}

void ZoneManager::DoAction(Widget *widget, double value)
{
    if (WDL_NOT_NORMALLY(!widget)) return;
    widget->LogInput(value);
    
    bool isUsed = false;
    
    DoAction(widget, value, isUsed);
    
    zonesToBeDeleted_.Empty(true);
}
    
void ZoneManager::DoAction(Widget *widget, double value, bool &isUsed)
{
    if (surface_->GetModifiers().GetSize() > 0)
        WidgetMoved(this, widget, surface_->GetModifiers().Get()[0]);
    
    if (learnFocusedFXZone_ != NULL)
        learnFocusedFXZone_->DoAction(widget, isUsed, value);
    
    if (isUsed)
        return;

    if (focusedFXParamZone_ != NULL && isFocusedFXParamMappingEnabled_)
        focusedFXParamZone_->DoAction(widget, isUsed, value);

    if (isUsed)
        return;

    if (focusedFXZone_ != NULL)
        focusedFXZone_->DoAction(widget, isUsed, value);
    
    if (isUsed)
        return;
    
    for (int i = 0; i < selectedTrackFXZones_.size(); ++i)
        selectedTrackFXZones_[i]->DoAction(widget, isUsed, value);
    
    if (isUsed)
        return;

    if (fxSlotZone_ != NULL)
        fxSlotZone_->DoAction(widget, isUsed, value);
    
    if (isUsed)
        return;

    for (int i = 0; i < goZones_.size(); ++i)
        goZones_[i]->DoAction(widget, isUsed, value);

    if (isUsed)
        return;

    if (homeZone_ != NULL)
        homeZone_->DoAction(widget, isUsed, value);
}

void ZoneManager::DoRelativeAction(Widget *widget, double delta)
{
    if (WDL_NOT_NORMALLY(!widget)) return;
    widget->LogInput(delta);
    
    bool isUsed = false;
    
    DoRelativeAction(widget, delta, isUsed);
    
    zonesToBeDeleted_.Empty(true);
}

void ZoneManager::DoRelativeAction(Widget *widget, double delta, bool &isUsed)
{
    if (surface_->GetModifiers().GetSize() > 0)
        WidgetMoved(this, widget, surface_->GetModifiers().Get()[0]);

    if (learnFocusedFXZone_ != NULL)
        learnFocusedFXZone_->DoRelativeAction(widget, isUsed, delta);

    if (isUsed)
        return;

    if (focusedFXParamZone_ != NULL && isFocusedFXParamMappingEnabled_)
        focusedFXParamZone_->DoRelativeAction(widget, isUsed, delta);

    if (isUsed)
        return;

    if (focusedFXZone_ != NULL)
        focusedFXZone_->DoRelativeAction(widget, isUsed, delta);
    
    if (isUsed)
        return;
    
    for (int i = 0; i < selectedTrackFXZones_.size(); ++i)
        selectedTrackFXZones_[i]->DoRelativeAction(widget, isUsed, delta);
    
    if (isUsed)
        return;

    if (fxSlotZone_ != NULL)
        fxSlotZone_->DoRelativeAction(widget, isUsed, delta);
    
    if (isUsed)
        return;

    for (int i = 0; i < goZones_.size(); ++i)
        goZones_[i]->DoRelativeAction(widget, isUsed, delta);
    
    if (isUsed)
        return;

    if (homeZone_ != NULL)
        homeZone_->DoRelativeAction(widget, isUsed, delta);
}

void ZoneManager::DoRelativeAction(Widget *widget, int accelerationIndex, double delta)
{
    if (WDL_NOT_NORMALLY(!widget)) return;
    widget->LogInput(delta);
    
    bool isUsed = false;
    
    DoRelativeAction(widget, accelerationIndex, delta, isUsed);
    
    zonesToBeDeleted_.Empty(true);
}

void ZoneManager::DoRelativeAction(Widget *widget, int accelerationIndex, double delta, bool &isUsed)
{
    if (surface_->GetModifiers().GetSize() > 0)
        WidgetMoved(this, widget, surface_->GetModifiers().Get()[0]);

    if (learnFocusedFXZone_ != NULL)
        learnFocusedFXZone_->DoRelativeAction(widget, isUsed, accelerationIndex, delta);

    if (isUsed)
        return;

    if (focusedFXParamZone_ != NULL && isFocusedFXParamMappingEnabled_)
        focusedFXParamZone_->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
    
    if (isUsed)
        return;

    if (focusedFXZone_ != NULL)
        focusedFXZone_->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
    
    if (isUsed)
        return;
    
    for (int i = 0; i < selectedTrackFXZones_.size(); ++i)
        selectedTrackFXZones_[i]->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
    
    if (isUsed)
        return;

    if (fxSlotZone_ != NULL)
        fxSlotZone_->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
    
    if (isUsed)
        return;

    for (int i = 0; i < goZones_.size(); ++i)
        goZones_[i]->DoRelativeAction(widget, isUsed, accelerationIndex, delta);

    if (isUsed)
        return;

    if (homeZone_ != NULL)
        homeZone_->DoRelativeAction(widget, isUsed, accelerationIndex, delta);
}

void ZoneManager::DoTouch(Widget *widget, double value)
{
    if (WDL_NOT_NORMALLY(!widget)) return;
    widget->LogInput(value);
    
    bool isUsed = false;
    
    DoTouch(widget, value, isUsed);
    
    zonesToBeDeleted_.Empty(true);
}

void ZoneManager::DoTouch(Widget *widget, double value, bool &isUsed)
{
    surface_->TouchChannel(widget->GetChannelNumber(), value != 0);
       
    // GAW -- temporary
    //if (surface_->GetModifiers().GetSize() > 0 && value != 0.0) // ignore touch releases for Learn mode
        //WidgetMoved(this, widget, surface_->GetModifiers().Get()[0]);

    //if (learnFocusedFXZone_ != NULL)
        //learnFocusedFXZone_->DoTouch(widget, widget->GetName(), isUsed, value);

    if (isUsed)
        return;

    if (focusedFXParamZone_ != NULL && isFocusedFXParamMappingEnabled_)
        focusedFXParamZone_->DoTouch(widget, widget->GetName(), isUsed, value);
    
    if (isUsed)
        return;

    if (focusedFXZone_ != NULL)
        focusedFXZone_->DoTouch(widget, widget->GetName(), isUsed, value);
    
    if (isUsed)
        return;

    for (int i = 0; i < selectedTrackFXZones_.size(); ++i)
        selectedTrackFXZones_[i]->DoTouch(widget, widget->GetName(), isUsed, value);
    
    if (isUsed)
        return;

    if (fxSlotZone_ != NULL)
        fxSlotZone_->DoTouch(widget, widget->GetName(), isUsed, value);
    
    if (isUsed)
        return;

    for (int i = 0; i < goZones_.size(); ++i)
        goZones_[i]->DoTouch(widget, widget->GetName(), isUsed, value);

    if (isUsed)
        return;

    if (homeZone_ != NULL)
        homeZone_->DoTouch(widget, widget->GetName(), isUsed, value);
}

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
    
    for (int i = 0; i < MaxModifiers; ++i)
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
    
    for (int i = 1; i <= GetNumTracks(); ++i)
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
    
    for (int i = 0; i < CountSelectedTracks2(NULL, false); ++i)
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
    
    if (MediaTrack *track = page_->GetNavigatorForChannel(channel + channelOffset_)->GetTrack())
        return DAW::GetTrackColor(track);
    else
        return white;
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

void ControlSurface::SetModifierValue(int value)
{
    if (zoneManager_->GetIsBroadcaster() && usesLocalModifiers_)
    {
        modifierManager_->SetModifierValue(value);
        
        for (int i = 0; i < zoneManager_->GetListeners().size(); ++i)
            if (zoneManager_->GetListeners()[i]->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners()[i]->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners()[i]->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners()[i]->GetSurface()->GetModifierManager()->SetShift(value, latchTime_);
    }
    else if (usesLocalModifiers_)
        modifierManager_->SetModifierValue(value);
    else
        page_->GetModifierManager()->SetModifierValue(value);
}

void ControlSurface::SetShift(bool value)
{
    if (zoneManager_->GetIsBroadcaster() && usesLocalModifiers_)
    {
        modifierManager_->SetShift(value, latchTime_);
        
        for (int i = 0; i < zoneManager_->GetListeners().size(); ++i)
            if (zoneManager_->GetListeners()[i]->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners()[i]->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners()[i]->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners()[i]->GetSurface()->GetModifierManager()->SetShift(value, latchTime_);
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
        
        for (int i = 0; i < zoneManager_->GetListeners().size(); ++i)
            if (zoneManager_->GetListeners()[i]->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners()[i]->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners()[i]->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners()[i]->GetSurface()->GetModifierManager()->SetOption(value, latchTime_);
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
        
        for (int i = 0; i < zoneManager_->GetListeners().size(); ++i)
            if (zoneManager_->GetListeners()[i]->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners()[i]->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners()[i]->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners()[i]->GetSurface()->GetModifierManager()->SetControl(value, latchTime_);
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
        
        for (int i = 0; i < zoneManager_->GetListeners().size(); ++i)
            if (zoneManager_->GetListeners()[i]->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners()[i]->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners()[i]->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners()[i]->GetSurface()->GetModifierManager()->SetAlt(value, latchTime_);
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
        
        for (int i = 0; i < zoneManager_->GetListeners().size(); ++i)
            if (zoneManager_->GetListeners()[i]->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners()[i]->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners()[i]->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners()[i]->GetSurface()->GetModifierManager()->SetFlip(value, latchTime_);
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
        
        for (int i = 0; i < zoneManager_->GetListeners().size(); ++i)
            if (zoneManager_->GetListeners()[i]->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners()[i]->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners()[i]->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners()[i]->GetSurface()->GetModifierManager()->SetGlobal(value, latchTime_);
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
        
        for (int i = 0; i < zoneManager_->GetListeners().size(); ++i)
            if (zoneManager_->GetListeners()[i]->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners()[i]->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners()[i]->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners()[i]->GetSurface()->GetModifierManager()->SetMarker(value, latchTime_);
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
        
        for (int i = 0; i < zoneManager_->GetListeners().size(); ++i)
            if (zoneManager_->GetListeners()[i]->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners()[i]->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners()[i]->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners()[i]->GetSurface()->GetModifierManager()->SetNudge(value, latchTime_);
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
        
        for (int i = 0; i < zoneManager_->GetListeners().size(); ++i)
            if (zoneManager_->GetListeners()[i]->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners()[i]->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners()[i]->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners()[i]->GetSurface()->GetModifierManager()->SetZoom(value, latchTime_);
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
        
        for (int i = 0; i < zoneManager_->GetListeners().size(); ++i)
            if (zoneManager_->GetListeners()[i]->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners()[i]->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners()[i]->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners()[i]->GetSurface()->GetModifierManager()->SetScrub(value, latchTime_);
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
        
        for (int i = 0; i < zoneManager_->GetListeners().size(); ++i)
            if (zoneManager_->GetListeners()[i]->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners()[i]->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners()[i]->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners()[i]->GetSurface()->GetModifierManager()->ClearModifier(modifier);
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
        
        for (int i = 0; i < zoneManager_->GetListeners().size(); ++i)
            if (zoneManager_->GetListeners()[i]->GetSurface()->GetListensToModifiers() && ! zoneManager_->GetListeners()[i]->GetSurface()->GetUsesLocalModifiers() && zoneManager_->GetListeners()[i]->GetSurface()->GetName() != name_)
                zoneManager_->GetListeners()[i]->GetSurface()->GetModifierManager()->ClearModifiers();
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
Midi_ControlSurface::Midi_ControlSurface(CSurfIntegrator *const csi, Page *page, const char *name, int channelOffset, const char *surfaceFile, const char *zoneFolder, const char *fxZoneFolder, Midi_ControlSurfaceIO *surfaceIO)
: ControlSurface(csi, page, name, surfaceIO->GetChannelCount(), channelOffset), surfaceIO_(surfaceIO), Midi_CSIMessageGeneratorsByMessage_(disposeAction)
{
    // private:
    // special processing for MCU meters
    hasMCUMeters_ = false;
    displayType_ = 0x14;
    lastRun_ = 0;
    
    ProcessMIDIWidgetFile(surfaceFile, this);
    InitHardwiredWidgets(this);
    InitializeMeters();
    InitZoneManager(csi_, this, zoneFolder, fxZoneFolder);
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
    surfaceIO_->QueueMidiSysExMessage(midiMessage);
    
    if (g_surfaceOutDisplay)
    {
        WDL_FastString output;
        
        output.Set("OUT->");
        output.Append(name_.c_str());
        output.Append(" ");

        char buf[32];

        for (int i = 0; i < midiMessage->size; ++i)
        {
            snprintf(buf, sizeof(buf), "%02x ", midiMessage->midi_message[i]);
            output.Append(buf);
        }
        
        output.Append("\n");

        ShowConsoleMsg(output.Get());
    }
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

OSC_X32ControlSurfaceIO::OSC_X32ControlSurfaceIO(CSurfIntegrator *const csi, const char *surfaceName, int channelCount, const char *receiveOnPort, const char *transmitToPort, const char *transmitToIpAddress, int maxPacketsPerRun) : OSC_ControlSurfaceIO(csi, surfaceName, channelCount, receiveOnPort, transmitToPort, transmitToIpAddress, maxPacketsPerRun)
{
    X32HeartBeatRefreshInterval_ = 5000; // must be less than 10000
    X32HeartBeatLastRefreshTime_ = GetTickCount()-30000;
}

OSC_ControlSurfaceIO::OSC_ControlSurfaceIO(CSurfIntegrator *const csi, const char *surfaceName, int channelCount, const char *receiveOnPort, const char *transmitToPort, const char *transmitToIpAddress, int maxPacketsPerRun) : csi_(csi), name_(surfaceName), channelCount_(channelCount)
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
    while (packetQueue_.GetSize()>=sizeof(int) && ++count < 100)
    {
        BeginRun();
        if (count) Sleep(33);
    }

    if (inSocket_)
    {
        for (int x = 0; x < s_inputSockets.GetSize(); ++x)
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
        for (int x = 0; x < s_outputSockets.GetSize(); ++x)
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
                   surface->ProcessOSCMessage(message->addressPattern().c_str(), value);
               }
               else if (message->arg().isInt32())
               {
                   int value;
                   message->arg().popInt32(value);
                   surface->ProcessOSCMessage(message->addressPattern().c_str(), value);
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
                   surface->ProcessOSCMessage(message->addressPattern().c_str(), value);
               }
               else if (message->arg().isInt32())
               {
                   int value;
                   message->arg().popInt32(value);
                   
                   if (message->addressPattern() == "/-stat/selidx")
                   {
                       WDL_FastString x32Select;
                       
                       x32Select.Set(message->addressPattern().c_str());
                       x32Select.Append("/");
                       if (value < 10)
                           x32Select.Append("0");

                       char buf[64];
                       snprintf(buf, sizeof(buf), "%d", value);
                       x32Select.Append(buf);
                                              
                       surface->ProcessOSCMessage(x32Select.Get(), 1.0);
                   }
                   else
                       surface->ProcessOSCMessage(message->addressPattern().c_str(), value);
               }
           }
       }
   }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// OSC_ControlSurface
////////////////////////////////////////////////////////////////////////////////////////////////////////
OSC_ControlSurface::OSC_ControlSurface(CSurfIntegrator *const csi, Page *page, const char *name, int channelOffset, const char *templateFilename, const char *zoneFolder, const char *fxZoneFolder, OSC_ControlSurfaceIO *surfaceIO) : ControlSurface(csi, page, name, surfaceIO->GetChannelCount(), channelOffset), surfaceIO_(surfaceIO)

{
    ProcessOSCWidgetFile(string(GetResourcePath()) + "/CSI/Surfaces/OSC/" + templateFilename);
    InitHardwiredWidgets(this);
    InitZoneManager(csi_, this, zoneFolder, fxZoneFolder);
}

void OSC_ControlSurface::ProcessOSCMessage(const char *message, double value)
{
    if (CSIMessageGeneratorsByMessage_.Exists(message))
        CSIMessageGeneratorsByMessage_.Get(message)->ProcessMessage(value);
    
    if (g_surfaceInDisplay)
    {
        char buf[MEDBUF];
        snprintf(buf, sizeof(buf), "IN <- %s %s %f\n", name_.c_str(), message, value);
        ShowConsoleMsg(buf);
    }
}

void OSC_ControlSurface::SendOSCMessage(const char *zoneName)
{
    string oscAddress(zoneName);
    ReplaceAllWith(oscAddress, s_BadFileChars, "_");
    oscAddress = "/" + oscAddress;

    surfaceIO_->SendOSCMessage(oscAddress.c_str());
        
    if (g_surfaceOutDisplay)
    {
        char buf[MEDBUF];
        snprintf(buf, sizeof(buf), "->LoadingZone---->%s\n", name_.c_str());
        ShowConsoleMsg(buf);
    }
}

void OSC_ControlSurface::SendOSCMessage(const char *oscAddress, int value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
        
    if (g_surfaceOutDisplay)
    {
        char buf[MEDBUF];
        snprintf(buf, sizeof(buf), "OUT->%s %s %d\n", name_.c_str(), oscAddress, value);
        ShowConsoleMsg(buf);
    }
}

void OSC_ControlSurface::SendOSCMessage(const char *oscAddress, double value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
        
    if (g_surfaceOutDisplay)
    {
        char buf[MEDBUF];
        snprintf(buf, sizeof(buf), "OUT->%s %s %f\n", name_.c_str(), oscAddress, value);
        ShowConsoleMsg(buf);
    }
}

void OSC_ControlSurface::SendOSCMessage(const char *oscAddress, const char *value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
        
    if (g_surfaceOutDisplay)
    {
        char buf[MEDBUF];
        snprintf(buf, sizeof(buf), "OUT->%s %s %s\n", name_.c_str(), oscAddress, value);
        ShowConsoleMsg(buf);
    }
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor *feedbackProcessor, const char *oscAddress, double value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);
    
    if (g_surfaceOutDisplay)
    {
        char buf[MEDBUF];
        snprintf(buf, sizeof(buf), "OUT->%s %s %f\n", feedbackProcessor->GetWidget()->GetName(), oscAddress, value);
        ShowConsoleMsg(buf);
    }
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor *feedbackProcessor, const char *oscAddress, int value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);

    if (g_surfaceOutDisplay)
    {
        char buf[MEDBUF];
        snprintf(buf, sizeof(buf), "OUT->%s %s %s %d\n", name_.c_str(), feedbackProcessor->GetWidget()->GetName(), oscAddress, value);
        ShowConsoleMsg(buf);
    }
}

void OSC_ControlSurface::SendOSCMessage(OSC_FeedbackProcessor *feedbackProcessor, const char *oscAddress, const char *value)
{
    surfaceIO_->SendOSCMessage(oscAddress, value);

    if (g_surfaceOutDisplay)
    {
        char buf[MEDBUF];
        snprintf(buf, sizeof(buf), "OUT->%s %s %s %s\n", name_.c_str(), feedbackProcessor->GetWidget()->GetName(), oscAddress, value);
        ShowConsoleMsg(buf);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Midi_ControlSurface
////////////////////////////////////////////////////////////////////////////////////////////////////////
static struct
{
    MIDI_event_ex_t evt;
    char data[MEDBUF];
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

CSurfIntegrator::CSurfIntegrator() : actions_(true, disposeAction), fxParamSteppedValueCounts_(true, disposeCounts)
{
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
