//
//  control_surface_midi_widgets.h
//  reaper_csurf_integrator
//
//

#ifndef control_surface_midi_widgets_h
#define control_surface_midi_widgets_h

#include "control_surface_integrator.h"
#include "handy_functions.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CSIMessageGenerators
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class PressRelease_Midi_CSIMessageGenerator : public Midi_CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    shared_ptr<MIDI_event_ex_t> press_;
    shared_ptr<MIDI_event_ex_t> release_;
    
public:
    virtual ~PressRelease_Midi_CSIMessageGenerator() {}
    PressRelease_Midi_CSIMessageGenerator(shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> press) : Midi_CSIMessageGenerator(widget), press_(press) {}
    
    PressRelease_Midi_CSIMessageGenerator(shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> press, shared_ptr<MIDI_event_ex_t> release) : Midi_CSIMessageGenerator(widget), press_(press), release_(release) {}
    
    virtual void ProcessMidiMessage(const MIDI_event_ex_t* midiMessage) override
    {
        widget_->GetZoneManager()->DoAction(widget_, midiMessage->IsEqualTo(press_) ? 1 : 0);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Touch_Midi_CSIMessageGenerator : public Midi_CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    shared_ptr<MIDI_event_ex_t> press_;
    shared_ptr<MIDI_event_ex_t> release_;

public:
    virtual ~Touch_Midi_CSIMessageGenerator() {}
    
    Touch_Midi_CSIMessageGenerator(shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> press, shared_ptr<MIDI_event_ex_t> release) : Midi_CSIMessageGenerator(widget), press_(press), release_(release) {}
    
    virtual void ProcessMidiMessage(const MIDI_event_ex_t* midiMessage) override
    {
        widget_->GetZoneManager()->DoTouch(widget_, midiMessage->IsEqualTo(press_) ? 1 : 0);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class AnyPress_Midi_CSIMessageGenerator : public Midi_CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    shared_ptr<MIDI_event_ex_t> press_;

public:
    virtual ~AnyPress_Midi_CSIMessageGenerator() {}
    AnyPress_Midi_CSIMessageGenerator(shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> press) : Midi_CSIMessageGenerator(widget), press_(press) {}
    
    virtual void ProcessMidiMessage(const MIDI_event_ex_t* midiMessage) override
    {
        // Doesn't matter what value was sent, just do it
        widget_->GetZoneManager()->DoAction(widget_, 1);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Fader14Bit_Midi_CSIMessageGenerator : public Midi_CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual ~Fader14Bit_Midi_CSIMessageGenerator() {}
    Fader14Bit_Midi_CSIMessageGenerator(shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> message) : Midi_CSIMessageGenerator(widget) {}
    
    virtual void ProcessMidiMessage(const MIDI_event_ex_t* midiMessage) override
    {
        widget_->GetZoneManager()->DoAction(widget_, int14ToNormalized(midiMessage->midi_message[2], midiMessage->midi_message[1]));
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Fader7Bit_Midi_CSIMessageGenerator : public Midi_CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual ~Fader7Bit_Midi_CSIMessageGenerator() {}
    Fader7Bit_Midi_CSIMessageGenerator(shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> message) : Midi_CSIMessageGenerator(widget) {}
    
    virtual void ProcessMidiMessage(const MIDI_event_ex_t* midiMessage) override
    {
        widget_->GetZoneManager()->DoAction(widget_, midiMessage->midi_message[2] / 127.0);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class AcceleratedPreconfiguredEncoder_Midi_CSIMessageGenerator : public Midi_CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    map<int, int> accelerationValuesForIncrement_;
    map<int, int> accelerationValuesForDecrement_;
    
public:
    virtual ~AcceleratedPreconfiguredEncoder_Midi_CSIMessageGenerator() {}
    AcceleratedPreconfiguredEncoder_Midi_CSIMessageGenerator(shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> message, double stepSize, map<int, int> accelerationValuesForDecrement, map<int, int> accelerationValuesForIncrement, vector<double> accelerationValues) :  Midi_CSIMessageGenerator(widget)
    {
        accelerationValuesForDecrement_ = accelerationValuesForDecrement;
        accelerationValuesForIncrement_ = accelerationValuesForIncrement;
        
        widget->SetStepSize(stepSize);
        widget->SetAccelerationValues(accelerationValues);
    }
    
    virtual void ProcessMidiMessage(const MIDI_event_ex_t* midiMessage) override
    {
        int val = midiMessage->midi_message[2];
        
        if(accelerationValuesForIncrement_.count(val) > 0)
            widget_->GetZoneManager()->DoRelativeAction(widget_, accelerationValuesForIncrement_[val], 0.001);
        else if(accelerationValuesForDecrement_.count(val) > 0)
            widget_->GetZoneManager()->DoRelativeAction(widget_, accelerationValuesForDecrement_[val], -0.001);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class AcceleratedEncoder_Midi_CSIMessageGenerator : public Midi_CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    map<int, int> accelerationValuesForIncrement_;
    map<int, int> accelerationValuesForDecrement_;

public:
    virtual ~AcceleratedEncoder_Midi_CSIMessageGenerator() {}
    AcceleratedEncoder_Midi_CSIMessageGenerator(shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> message, vector<string> params) : Midi_CSIMessageGenerator(widget)
    {       
        auto openSquareBrace = find(params.begin(), params.end(), "[");
        auto closeCurlyBrace = find(params.begin(), params.end(), "]");
        
        if(openSquareBrace != params.end() && closeCurlyBrace != params.end())
        {
            vector<int> incValues;
            vector<int> decValues;

            bool inDec = false;
            
            for(auto it = openSquareBrace + 1; it != closeCurlyBrace; ++it)
            {
                string strVal = *(it);
                
                if(strVal == "<")
                    inDec = true;
                else if(strVal == ">")
                    inDec = false;
                else if(regex_match(strVal, regex("[0-9A-Fa-f]+[-][0-9A-Fa-f]+")))
                {
                    istringstream range(strVal);
                    vector<string> range_tokens;
                    string range_token;
                    
                    while (getline(range, range_token, '-'))
                        range_tokens.push_back(range_token);
                    
                    if(range_tokens.size() == 2)
                    {
                        int firstVal = strtol(range_tokens[0].c_str(), nullptr, 16);
                        int lastVal = strtol(range_tokens[1].c_str(), nullptr, 16);

                        if(firstVal < lastVal)
                        {
                            if(inDec == false)
                            {
                                for(int i = firstVal; i <= lastVal; i++)
                                    incValues.push_back(i);
                            }
                            else
                            {
                                for(int i = firstVal; i <= lastVal; i++)
                                    decValues.push_back(i);
                            }
                        }
                        else
                        {
                            if(inDec == false)
                            {
                                for(int i = firstVal; i >= lastVal; i--)
                                    incValues.push_back(i);
                            }
                            else
                            {
                                for(int i = firstVal; i >= lastVal; i--)
                                    decValues.push_back(i);
                            }
                        }
                    }
                }
                else if(inDec == false)
                    incValues.push_back(strtol(strVal.c_str(), nullptr, 16));
                else
                    decValues.push_back(strtol(strVal.c_str(), nullptr, 16));
            }
            
            if(incValues.size() > 0)
            {
                if(incValues.size() == 1)
                    accelerationValuesForIncrement_[incValues[0]] = 0;
                else
                    for(int i = 0; i < incValues.size(); i++)
                        accelerationValuesForIncrement_[incValues[i]] = i;
            }
            
            if(decValues.size() > 0)
            {
                if(decValues.size() == 1)
                    accelerationValuesForDecrement_[decValues[0]] = 0;
                else
                    for(int i = 0; i < decValues.size(); i++)
                        accelerationValuesForDecrement_[decValues[i]] = i;
            }
        }
    }
    
    virtual void ProcessMidiMessage(const MIDI_event_ex_t* midiMessage) override
    {
        int val = midiMessage->midi_message[2];

        double delta = (midiMessage->midi_message[2] & 0x3f) / 63.0;
        
        if (midiMessage->midi_message[2] & 0x40)
            delta = -delta;
        
        delta = delta / 2.0;

        if(accelerationValuesForIncrement_.count(val) > 0)
            widget_->GetZoneManager()->DoRelativeAction(widget_, accelerationValuesForIncrement_[val], delta);
        else if(accelerationValuesForDecrement_.count(val) > 0)
            widget_->GetZoneManager()->DoRelativeAction(widget_, accelerationValuesForDecrement_[val], delta);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MFT_AcceleratedEncoder_Midi_CSIMessageGenerator : public Midi_CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    map<int, int> accelerationIndicesForIncrement_;
    map<int, int> accelerationIndicesForDecrement_;
    
public:
    virtual ~MFT_AcceleratedEncoder_Midi_CSIMessageGenerator() {}
    MFT_AcceleratedEncoder_Midi_CSIMessageGenerator(shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> message, vector<string> params) : Midi_CSIMessageGenerator(widget)
    {
        accelerationIndicesForDecrement_[0x3f] = 0;
        accelerationIndicesForDecrement_[0x3e] = 1;
        accelerationIndicesForDecrement_[0x3d] = 2;
        accelerationIndicesForDecrement_[0x3c] = 3;
        accelerationIndicesForDecrement_[0x3b] = 4;
        accelerationIndicesForDecrement_[0x3a] = 5;
        accelerationIndicesForDecrement_[0x39] = 6;
        accelerationIndicesForDecrement_[0x38] = 7;
        accelerationIndicesForDecrement_[0x36] = 8;
        accelerationIndicesForDecrement_[0x33] = 9;
        accelerationIndicesForDecrement_[0x2f] = 10;

        accelerationIndicesForIncrement_[0x41] = 0;
        accelerationIndicesForIncrement_[0x42] = 1;
        accelerationIndicesForIncrement_[0x43] = 2;
        accelerationIndicesForIncrement_[0x44] = 3;
        accelerationIndicesForIncrement_[0x45] = 4;
        accelerationIndicesForIncrement_[0x46] = 5;
        accelerationIndicesForIncrement_[0x47] = 6;
        accelerationIndicesForIncrement_[0x48] = 7;
        accelerationIndicesForIncrement_[0x4a] = 8;
        accelerationIndicesForIncrement_[0x4d] = 9;
        accelerationIndicesForIncrement_[0x51] = 10;
    }
    
    virtual void ProcessMidiMessage(const MIDI_event_ex_t* midiMessage) override
    {
        int val = midiMessage->midi_message[2];
        
        if(accelerationIndicesForIncrement_.count(val) > 0)
            widget_->GetZoneManager()->DoRelativeAction(widget_, accelerationIndicesForIncrement_[val], 0.001);
        else if(accelerationIndicesForDecrement_.count(val) > 0)
            widget_->GetZoneManager()->DoRelativeAction(widget_, accelerationIndicesForDecrement_[val], -0.001);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Encoder_Midi_CSIMessageGenerator : public Midi_CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual ~Encoder_Midi_CSIMessageGenerator() {}
    Encoder_Midi_CSIMessageGenerator(shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> message) : Midi_CSIMessageGenerator(widget) {}
    
    virtual void ProcessMidiMessage(const MIDI_event_ex_t* midiMessage) override
    {
        double delta = (midiMessage->midi_message[2] & 0x3f) / 63.0;
        
        if (midiMessage->midi_message[2] & 0x40)
            delta = -delta;
        
        delta = delta / 2.0;

        widget_->GetZoneManager()->DoRelativeAction(widget_, delta);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class EncoderPlain_Midi_CSIMessageGenerator : public Midi_CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual ~EncoderPlain_Midi_CSIMessageGenerator() {}
    EncoderPlain_Midi_CSIMessageGenerator(shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> message) : Midi_CSIMessageGenerator(widget) {}
    
    virtual void ProcessMidiMessage(const MIDI_event_ex_t* midiMessage) override
    {
        double delta = 1.0 / 64.0;
        
        if (midiMessage->midi_message[2] & 0x40)
            delta = -delta;
        
        widget_->GetZoneManager()->DoRelativeAction(widget_, delta);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Encoder7Bit_Midi_CSIMessageGenerator : public Midi_CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    int lastMessage = -1;
    
public:
    virtual ~Encoder7Bit_Midi_CSIMessageGenerator() {}
    Encoder7Bit_Midi_CSIMessageGenerator(shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> message) : Midi_CSIMessageGenerator(widget) {}
    
    virtual void ProcessMidiMessage(const MIDI_event_ex_t* midiMessage) override
    {
        int currentMessage = midiMessage->midi_message[2];
        double delta = 1.0 / 64.0;
        
        if(lastMessage > currentMessage || (lastMessage == 0 && currentMessage == 0))
            delta = -delta;
            
        lastMessage = currentMessage;
        
        widget_->GetZoneManager()->DoRelativeAction(widget_, delta);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FeedbackProcessors
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TwoState_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual ~TwoState_Midi_FeedbackProcessor() {}
    TwoState_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> feedback1, shared_ptr<MIDI_event_ex_t> feedback2) : Midi_FeedbackProcessor(surface, widget, feedback1, feedback2) { }
    
    virtual string GetName() override { return "TwoState_Midi_FeedbackProcessor"; }
    
    virtual void SetValue(map<string, string> &properties, double value) override
    {
        if(value == 0.0)
        {
            if(midiFeedbackMessage2_)
                SendMidiMessage(midiFeedbackMessage2_->midi_message[0], midiFeedbackMessage2_->midi_message[1], midiFeedbackMessage2_->midi_message[2]);
            else if(midiFeedbackMessage1_)
                SendMidiMessage(midiFeedbackMessage1_->midi_message[0], midiFeedbackMessage1_->midi_message[1], 0x00);
        }
        else if(midiFeedbackMessage1_)
            SendMidiMessage(midiFeedbackMessage1_->midi_message[0], midiFeedbackMessage1_->midi_message[1], midiFeedbackMessage1_->midi_message[2]);
    }

    virtual void ForceValue(map<string, string> &properties, double value) override
    {
        if(value == 0.0)
        {
            if(midiFeedbackMessage2_)
                ForceMidiMessage(midiFeedbackMessage2_->midi_message[0], midiFeedbackMessage2_->midi_message[1], midiFeedbackMessage2_->midi_message[2]);
            else if(midiFeedbackMessage1_)
                ForceMidiMessage(midiFeedbackMessage1_->midi_message[0], midiFeedbackMessage1_->midi_message[1], 0x00);
        }
        else if(midiFeedbackMessage1_)
            ForceMidiMessage(midiFeedbackMessage1_->midi_message[0], midiFeedbackMessage1_->midi_message[1], midiFeedbackMessage1_->midi_message[2]);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FPTwoStateRGB_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    double active_ = 0.0;
    
public:
    virtual ~FPTwoStateRGB_Midi_FeedbackProcessor() {}
    FPTwoStateRGB_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> feedback1) : Midi_FeedbackProcessor(surface, widget, feedback1) { }
    
    virtual string GetName() override { return "FPTwoStateRGB_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, double active) override
    {
        active_ = active;
    }
    
    virtual void SetColorValue(rgba_color color) override
    {
        int RGBIndexDivider = 1 * 2;
        
        if (active_ == false)
            RGBIndexDivider = 9 * 2;
        
        color.r = color.r / RGBIndexDivider;
        color.g = color.g / RGBIndexDivider;
        color.b = color.b / RGBIndexDivider;
        
        if(color != lastColor_)
            ForceColorValue(color);
    }

    virtual void ForceColorValue(rgba_color color) override
    {
        lastColor_ = color;
        
        SendMidiMessage(0x90, midiFeedbackMessage1_->midi_message[1], 0x7f);
        SendMidiMessage(0x91, midiFeedbackMessage1_->midi_message[1],  color.r);  // only 127 bit allowed in Midi byte 3
        SendMidiMessage(0x92, midiFeedbackMessage1_->midi_message[1],  color.g);
        SendMidiMessage(0x93, midiFeedbackMessage1_->midi_message[1],  color.b);
    }
};

static rgba_color GetColorValue(string hexColor)
{
    rgba_color colorValue;
    
    if(hexColor.length() == 7)
    {
        regex pattern("#([0-9a-fA-F]{6})");
        smatch match;
        if (regex_match(hexColor, match, pattern))
            sscanf(match.str(1).c_str(), "%2x%2x%2x", &colorValue.r, &colorValue.g, &colorValue.b);
    }
    else if(hexColor.length() == 9)
    {
        regex pattern("#([0-9a-fA-F]{8})");
        smatch match;
        if (regex_match(hexColor, match, pattern))
            sscanf(match.str(1).c_str(), "%2x%2x%2x%2x", &colorValue.r, &colorValue.g, &colorValue.b, &colorValue.a);
    }
 
    return colorValue;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SCE24TwoStateLED_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    double lastValue_ = 0.0;

public:
    virtual ~SCE24TwoStateLED_Midi_FeedbackProcessor() {}
    SCE24TwoStateLED_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> feedback1) : Midi_FeedbackProcessor(surface, widget, feedback1) { }
    
    virtual string GetName() override { return "SCE24TwoStateLED_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, double value) override
    {
        if(lastValue_ != value)
            ForceValue(properties, value);
    }
    
    virtual void ForceValue(map<string, string> &properties, double value) override
    {
        lastValue_ = value;
     
        rgba_color color;

        if(value == 0 && properties.count("OffColor") > 0)
            color = GetColorValue(properties["OffColor"]);
        else if(value == 1 && properties.count("OnColor") > 0)
            color = GetColorValue(properties["OnColor"]);

        struct
        {
            MIDI_event_ex_t evt;
            char data[512];
        } midiSysExData;
         
        midiSysExData.evt.frame_offset=0;
        midiSysExData.evt.size=0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x02;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x38;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x01;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = midiFeedbackMessage1_->midi_message[1];
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = color.r / 2;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = color.g / 2;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = color.b / 2;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF7;
         
        SendMidiSysExMessage(&midiSysExData.evt);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SCE24OLED_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    double lastValue_ = 0.0;

public:
    virtual ~SCE24OLED_Midi_FeedbackProcessor() {}
    SCE24OLED_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> feedback1) : Midi_FeedbackProcessor(surface, widget, feedback1) { }
    
    virtual string GetName() override { return "SCE24OLED_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, double value) override
    {
        if(lastValue_ != value)
            ForceValue(properties, value);
    }
    
    virtual void SetInitialValues(map<string, string> &properties) override
    {
        ForceValue(properties, 0.0);
    }
    
    virtual void ForceValue(map<string, string> &properties, double value) override
    {
        lastValue_ = value;
                
        int topMargin = 0;
        int bottomMargin = 0;
        int font = 0;
        rgba_color background;
        rgba_color foreground;

        if(properties.count("TopMargin") > 0)
            topMargin = atoi(properties["TopMargin"].c_str());
        if(properties.count("BottomMargin") > 0)
            bottomMargin = atoi(properties["BottomMargin"].c_str());
        if(properties.count("Font") > 0)
            font = atoi(properties["Font"].c_str());

        if(value == 0)
        {
            if(properties.count("BackgroundOff") > 0)
                background = GetColorValue(properties["BackgroundOff"]);
            if(properties.count("ForegroundOff") > 0)
                foreground = GetColorValue(properties["ForegroundOff"]);
        }
        else
        {
            if(properties.count("BackgroundOn") > 0)
                background = GetColorValue(properties["BackgroundOn"]);
            if(properties.count("ForegroundOn") > 0)
                foreground = GetColorValue(properties["ForegroundOn "]);
        }
        
        string displayText = "";
        
        if(properties.count("DisplayText") > 0)
            displayText = properties["DisplayText"];
        
        struct
        {
            MIDI_event_ex_t evt;
            char data[512];
        } midiSysExData;
         
        midiSysExData.evt.frame_offset=0;
        midiSysExData.evt.size=0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x02;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x38;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x01;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = midiFeedbackMessage1_->midi_message[1];
        
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = topMargin;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = bottomMargin;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = font;

        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = background.r / 2;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = background.g / 2;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = background.b / 2;
        
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = foreground.r / 2;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = foreground.g / 2;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = foreground.b / 2;
        
        for(int i = 0; i < displayText.length(); i++)
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = displayText[i];
        
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF7;
         
        SendMidiSysExMessage(&midiSysExData.evt);

    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SCE24Text_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    string lastStringSent_ = "";
    
public:
    virtual ~SCE24Text_Midi_FeedbackProcessor() {}
    SCE24Text_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> feedback1) : Midi_FeedbackProcessor(surface, widget, feedback1) { }
    
    virtual string GetName() override { return "SCE24Text_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, string value) override
    {
        if(lastStringSent_ != value)
            ForceValue(properties, value);
    }
    
    virtual void ForceValue(map<string, string> &properties, string displayText) override
    {
        lastStringSent_ = displayText;
        
        displayText = GetWidget()->GetSurface()->GetRestrictedLengthText(displayText);
        
        int topMargin = 0;
        int bottomMargin = 0;
        int font = 0;
        rgba_color background;
        rgba_color foreground;

        if(properties.count("TopMargin") > 0)
            topMargin = atoi(properties["TopMargin"].c_str());
        if(properties.count("BottomMargin") > 0)
            bottomMargin = atoi(properties["BottomMargin"].c_str());
        if(properties.count("Font") > 0)
            font = atoi(properties["Font"].c_str());

        if(properties.count("Background") > 0)
            background = GetColorValue(properties["Background"]);
        if(properties.count("Foreground") > 0)
            foreground = GetColorValue(properties["Foreground"]);

        struct
        {
            MIDI_event_ex_t evt;
            char data[512];
        } midiSysExData;
         
        midiSysExData.evt.frame_offset=0;
        midiSysExData.evt.size=0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x02;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x38;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x01;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = midiFeedbackMessage1_->midi_message[1];
        
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = topMargin;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = bottomMargin;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = font;

        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = background.r / 2;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = background.g / 2;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = background.b / 2;
        
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = foreground.r / 2;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = foreground.g / 2;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = foreground.b / 2;
        
        for(int i = 0; i < displayText.length(); i++)
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = displayText[i];
        
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF7;
         
        SendMidiSysExMessage(&midiSysExData.evt);
    }
};

struct LEDColor
{
    rgba_color ringColor;
    int ringRangeHigh = 0;
    int ringRangeMedium = 0;
    int ringRangeLow = 0;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SCE24Encoder_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual ~SCE24Encoder_Midi_FeedbackProcessor() {}
    SCE24Encoder_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> feedback1) : Midi_FeedbackProcessor(surface, widget, feedback1) { }
    
    virtual string GetName() override { return "SCE24Encoder_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, double value) override
    {
        SendMidiMessage(midiFeedbackMessage1_->midi_message[0], midiFeedbackMessage1_->midi_message[1], GetMidiValue(properties, value));
    }

    virtual void ForceValue(map<string, string> &properties, double value) override
    {
        ForceMidiMessage(midiFeedbackMessage1_->midi_message[0], midiFeedbackMessage1_->midi_message[1], GetMidiValue(properties, value));
    }
    
    int GetMidiValue(map<string, string> &properties, double value)
    {
        int valueInt = value * 127;
        
        int displayMode = 0;
        
        if(properties.count("RingStyle") > 0)
        {
            if(properties["RingStyle"] == "Dot")
                displayMode = 0;
            else if(properties["RingStyle"] == "BoostCut")
                displayMode = 1;
            else if(properties["RingStyle"] == "Fill")
                displayMode = 2;
            else if(properties["RingStyle"] == "Spread")
                displayMode = 3;
        }

        int val = 0;
        
        if(displayMode == 3)
            val = (1+((valueInt*15)>>8)) | (displayMode << 4);
        else
            val = (1+((valueInt*15)>>7)) | (displayMode << 4);

        //if(displayMode) // Should light up lower middle light
        //val |= 0x40;

        return val + 64;
    }
    
    virtual void SetInitialValues(map<string, string> &properties) override
    {
        if(properties.size() == 0)
            return;
        
        vector<LEDColor> colors;
        
        if(properties.count("Push") > 0 && properties.count("PushColor") > 0 )
        {
            LEDColor color;
            
            color.ringColor = GetColorValue(properties["PushColor"]);
            color.ringRangeLow = 7;
            color.ringRangeMedium = 0;
            color.ringRangeHigh = 0;
            
            colors.push_back(color);
            
            color.ringColor.r = 0;
            color.ringColor.g = 0;
            color.ringColor.b = 0;
            color.ringRangeLow = 120;
            color.ringRangeMedium = 127;
            color.ringRangeHigh = 15;
            
            colors.push_back(color);
        }
        else
        {
            LEDColor color;
            
            if(properties.count("LEDRingColor") > 0)
                color.ringColor = GetColorValue(properties["LEDRingColor"]);

            color.ringRangeLow = 120;
            color.ringRangeMedium = 127;
            color.ringRangeHigh = 15;

            colors.push_back(color);
            
            color.ringColor.r = 0;
            color.ringColor.g = 0;
            color.ringColor.b = 0;
            color.ringRangeLow = 7;
            color.ringRangeMedium = 0;
            color.ringRangeHigh = 0;

            colors.push_back(color);
        }
        
        struct
        {
            MIDI_event_ex_t evt;
            char data[512];
        } midiSysExData;
         
        for(auto color : colors)
        {
            midiSysExData.evt.frame_offset=0;
            midiSysExData.evt.size=0;
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF0;
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x02;
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x38;
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x01;
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = midiFeedbackMessage1_->midi_message[1];
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = color.ringRangeLow;
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = color.ringRangeMedium;
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = color.ringRangeHigh;
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = color.ringColor.r / 2;
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = color.ringColor.g / 2;
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = color.ringColor.b / 2;
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF7;
            
            SendMidiSysExMessage(&midiSysExData.evt);
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class NovationLaunchpadMiniRGB7Bit_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual ~NovationLaunchpadMiniRGB7Bit_Midi_FeedbackProcessor() {}
    NovationLaunchpadMiniRGB7Bit_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> feedback1) : Midi_FeedbackProcessor(surface, widget, feedback1) { }
    
    virtual string GetName() override { return "NovationLaunchpadMiniRGB7Bit_Midi_FeedbackProcessor"; }

    virtual void SetColorValue(rgba_color color) override
    {
        if(color != lastColor_)
            ForceColorValue(color);
    }

    virtual void ForceColorValue(rgba_color color) override
    {
        lastColor_ = color;
        
        struct
        {
            MIDI_event_ex_t evt;
            char data[64];
        } midiSysExData;
        
        midiSysExData.evt.frame_offset = 0;
        midiSysExData.evt.size = 0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x20;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x29;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x02;;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x0d;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x03;
        
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x03;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = midiFeedbackMessage1_->midi_message[1] ;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = color.r / 2; // only 127 bit max for this device
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = color.g / 2;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = color.b / 2;
        
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF7;
        
        SendMidiSysExMessage(&midiSysExData.evt);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FaderportRGB_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual ~FaderportRGB_Midi_FeedbackProcessor() {}
    FaderportRGB_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> feedback1) : Midi_FeedbackProcessor(surface, widget, feedback1) { }
    
    virtual string GetName() override { return "FaderportRGB_Midi_FeedbackProcessor"; }
    
    virtual void SetColorValue(rgba_color color) override
    {
        if(color != lastColor_)
            ForceColorValue(color);
    }

    virtual void ForceColorValue(rgba_color color) override
    {
        lastColor_ = color;
        
        SendMidiMessage(0x90, midiFeedbackMessage1_->midi_message[1], 0x7f);
        SendMidiMessage(0x91, midiFeedbackMessage1_->midi_message[1], color.r / 2);  // only 127 bit allowed in Midi byte 3
        SendMidiMessage(0x92, midiFeedbackMessage1_->midi_message[1], color.g / 2);
        SendMidiMessage(0x93, midiFeedbackMessage1_->midi_message[1], color.b / 2);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Fader14Bit_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual ~Fader14Bit_Midi_FeedbackProcessor() {}
    Fader14Bit_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> feedback1) : Midi_FeedbackProcessor(surface, widget, feedback1) { }
    
    virtual string GetName() override { return "Fader14Bit_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, double value) override
    {
        int volint = value * 16383.0;
        SendMidiMessage(midiFeedbackMessage1_->midi_message[0], volint&0x7f, (volint>>7)&0x7f);
    }
    
    virtual void ForceValue(map<string, string> &properties, double value) override
    {
        int volint = value * 16383.0;
        ForceMidiMessage(midiFeedbackMessage1_->midi_message[0], volint&0x7f, (volint>>7)&0x7f);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Fader7Bit_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual ~Fader7Bit_Midi_FeedbackProcessor() {}
    Fader7Bit_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> feedback1) : Midi_FeedbackProcessor(surface, widget, feedback1) { }
    
    virtual string GetName() override { return "Fader7Bit_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, double value) override
    {
        SendMidiMessage(midiFeedbackMessage1_->midi_message[0], midiFeedbackMessage1_->midi_message[1], value * 127.0);
    }
    
    virtual void ForceValue(map<string, string> &properties, double value) override
    {
        ForceMidiMessage(midiFeedbackMessage1_->midi_message[0], midiFeedbackMessage1_->midi_message[1], value * 127.0);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Encoder_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual ~Encoder_Midi_FeedbackProcessor() {}
    Encoder_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> feedback1) : Midi_FeedbackProcessor(surface, widget, feedback1) { }
    
    virtual string GetName() override { return "Encoder_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, double value) override
    {
        SendMidiMessage(midiFeedbackMessage1_->midi_message[0], midiFeedbackMessage1_->midi_message[1] + 0x20, GetMidiValue(properties, value));
    }

    virtual void ForceValue(map<string, string> &properties, double value) override
    {
        ForceMidiMessage(midiFeedbackMessage1_->midi_message[0], midiFeedbackMessage1_->midi_message[1] + 0x20, GetMidiValue(properties, value));
    }
    
    int GetMidiValue(map<string, string> &properties, double value)
    {
        int valueInt = value * 127;
        
        int displayMode = 0;
        
        if(properties.count("RingStyle") > 0)
        {
            if(properties["RingStyle"] == "Dot")
                displayMode = 0;
            else if(properties["RingStyle"] == "BoostCut")
                displayMode = 1;
            else if(properties["RingStyle"] == "Fill")
                displayMode = 2;
            else if(properties["RingStyle"] == "Spread")
                displayMode = 3;
        }

        int val = 0;
        
        if(displayMode == 3)
            val = (1+((valueInt*11)>>8)) | (displayMode << 4);
        else
            val = (1+((valueInt*11)>>7)) | (displayMode << 4);

        //if(displayMode) // Should light up lower middle light
        //val |= 0x40;

        return val;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ConsoleOneVUMeter_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual ~ConsoleOneVUMeter_Midi_FeedbackProcessor() {}
    ConsoleOneVUMeter_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> feedback1) : Midi_FeedbackProcessor(surface, widget, feedback1) { }
    
    virtual string GetName() override { return "ConsoleOneVUMeter_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, double value) override
    {
        SendMidiMessage(midiFeedbackMessage1_->midi_message[0], midiFeedbackMessage1_->midi_message[1], GetMidiValue(value));
    }

    virtual void ForceValue(map<string, string> &properties, double value) override
    {
        ForceMidiMessage(midiFeedbackMessage1_->midi_message[0], midiFeedbackMessage1_->midi_message[1], GetMidiValue(value));
    }
    
    double GetMidiValue(double value)
    {
        double dB = VAL2DB(normalizedToVol(value)) + 2.5;
        
        double midiVal = 0;
        
        if(dB < 0)
            midiVal = pow(10.0, dB / 48) * 96;
        else
            midiVal = pow(10.0, dB / 60) * 96;

        return midiVal;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ConsoleOneGainReductionMeter_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    double minDB_ = 0.0;
    double maxDB_ = 24.0;
    
public:
    virtual ~ConsoleOneGainReductionMeter_Midi_FeedbackProcessor() {}
    ConsoleOneGainReductionMeter_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> feedback1) : Midi_FeedbackProcessor(surface, widget, feedback1) { }
    
    virtual string GetName() override { return "ConsoleOneGainReductionMeter_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, double value) override
    {
        SendMidiMessage(midiFeedbackMessage1_->midi_message[0], midiFeedbackMessage1_->midi_message[1], fabs(1.0 - value) * 127.0);
    }

    virtual void ForceValue(map<string, string> &properties, double value) override
    {
        ForceMidiMessage(midiFeedbackMessage1_->midi_message[0], midiFeedbackMessage1_->midi_message[1], fabs(1.0 - value) * 127.0);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class QConProXMasterVUMeter_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    double minDB_ = 0.0;
    double maxDB_ = 24.0;
    int param_ = 0;
    
public:
    virtual ~QConProXMasterVUMeter_Midi_FeedbackProcessor() {}
    QConProXMasterVUMeter_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, int param) : Midi_FeedbackProcessor(surface, widget), param_(param) { }
    
    virtual string GetName() override { return "QConProXMasterVUMeter_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, double value) override
    {
        //Master Channel:
        //Master Level 1 : 0xd1, 0x0L
        //L = 0x0 – 0xD = Meter level 0% thru 100% (does not affect peak indicator)
        
        //Master Level 2 : 0xd1, 0x1L
        //L = 0x0 – 0xD = Meter level 0% thru 100% (does not affect peak indicator)
        
        int midiValue = value * 0x0f;
        
        if(midiValue > 0x0d)
            midiValue = 0x0d;
        
        SendMidiMessage(0xd1, (param_ << 4) | midiValue, 0);
    }

    virtual void ForceValue(map<string, string> &properties, double value) override
    {
        //Master Channel:
        //Master Level 1 : 0xd1, 0x0L
        //L = 0x0 – 0xD = Meter level 0% thru 100% (does not affect peak indicator)
        
        //Master Level 2 : 0xd1, 0x1L
        //L = 0x0 – 0xD = Meter level 0% thru 100% (does not affect peak indicator)
        
        int midiValue = value * 0x0f;
        
        if(midiValue > 0x0d)
            midiValue = 0x0d;
        
        ForceMidiMessage(0xd1, (param_ << 4) | midiValue, 0);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MCUVUMeter_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    int displayType_ = 0x14;
    int channelNumber_ = 0;
    int lastMidiValue_ = 0;
    bool isClipOn_ = false;

public:
    virtual ~MCUVUMeter_Midi_FeedbackProcessor() {}
    MCUVUMeter_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, int displayType, int channelNumber) : Midi_FeedbackProcessor(surface, widget), displayType_(displayType), channelNumber_(channelNumber) {}
    
    virtual string GetName() override { return "MCUVUMeter_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, double value) override
    {
        SendMidiMessage(0xd0, (channelNumber_ << 4) | GetMidiValue(value), 0);
    }

    virtual void ForceValue(map<string, string> &properties, double value) override
    {
        ForceMidiMessage(0xd0, (channelNumber_ << 4) | GetMidiValue(value), 0);
    }
    
    int GetMidiValue(double value)
    {
        //D0 yx    : update VU meter, y=channel, x=0..d=volume, e=clip on, f=clip off
        int midiValue = value * 0x0f;
        if(midiValue > 0x0d)
            midiValue = 0x0d;

        return midiValue;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FPVUMeter_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    int channelNumber_ = 0;
    int lastMidiValue_ = 0;
    bool isClipOn_ = false;

public:
    virtual ~FPVUMeter_Midi_FeedbackProcessor() {}
    FPVUMeter_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, int channelNumber) : Midi_FeedbackProcessor(surface, widget), channelNumber_(channelNumber) {}
    
    virtual string GetName() override { return "FPVUMeter_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, double value) override
    {
        if(lastMidiValue_ == value || GetMidiValue(value) < 7)
        {
            return;
        }
        
        if(channelNumber_ < 8)
        {
            SendMidiMessage(0xd0 + channelNumber_, GetMidiValue(value), 0);
        } else {
            SendMidiMessage(0xc0 + channelNumber_ - 8, GetMidiValue(value), 0);
        }
    }

    virtual void ForceValue(map<string, string> &properties, double value) override
    {
        lastMidiValue_ = value;
        if(channelNumber_ < 8)
        {
            ForceMidiMessage(0xd0 + channelNumber_, GetMidiValue(value), 0);
        } else {
            ForceMidiMessage(0xc0 + channelNumber_ - 8, GetMidiValue(value), 0);
        }
    }
    
    int GetMidiValue(double value)
    {
        //Dn, vv   : n meter address, vv meter value (0...7F)
        int midiValue = value * 0xa0;

        return midiValue;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FPValueBar_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    double lastValue_ = 0;
    int channel_ = 0;
    
    int GetValueBarType(map<string, string> &properties)
    {
        // 0: Normal, 1: Bipolar, 2: Fill, 3: Spread, 4: Off

        if(properties.count("BarStyle") > 0)
        {
            if(properties["BarStyle"] == "Normal")
                return 0;
            else if(properties["BarStyle"] == "BiPolar")
                return 1;
            else if(properties["BarStyle"] == "Fill")
                return 2;
            else if(properties["BarStyle"] == "Spread")
                return 3;
        }

        return 4;
    }
    
public:
    virtual ~FPValueBar_Midi_FeedbackProcessor() {}
    FPValueBar_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, int channel) : Midi_FeedbackProcessor(surface, widget), channel_(channel) { }

    virtual string GetName() override { return "FPValueBar_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, double value) override
    {
        if(value == lastValue_)
            return;
        
        ForceValue(properties, value);
    }

    virtual void ForceValue(map<string, string> &properties, double value) override
    {
        lastValue_ = value;
        
        if (channel_ < 8)
        {
            SendMidiMessage(0xb0, channel_ + 0x30, lastValue_ * 127.0);
            SendMidiMessage(0xb0, channel_ + 0x38, GetValueBarType(properties));
        }
        else
        {
            SendMidiMessage(0xb0, channel_ - 8 + 0x40, lastValue_ * 127.0);
            SendMidiMessage(0xb0, channel_ - 8 + 0x48, GetValueBarType(properties));
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MCUDisplay_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    int offset_ = 0;
    int displayType_ = 0x10;
    int displayRow_ = 0x12;
    int channel_ = 0;
    string lastStringSent_ = "";

public:
    virtual ~MCUDisplay_Midi_FeedbackProcessor() {}
    MCUDisplay_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, int displayUpperLower, int displayType, int displayRow, int channel) : Midi_FeedbackProcessor(surface, widget), offset_(displayUpperLower * 56), displayType_(displayType), displayRow_(displayRow), channel_(channel) { }
    
    virtual string GetName() override { return "MCUDisplay_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, string displayText) override
    {
        if(displayText != lastStringSent_) // changes since last send
            ForceValue(properties, displayText);
    }

    virtual void ForceValue(map<string, string> &properties, string displayText) override
    {
        lastStringSent_ = displayText;
        
        displayText = GetWidget()->GetSurface()->GetRestrictedLengthText(displayText);
        
        if(displayText == "" || displayText == "-150.00")
            displayText = "       ";

        int pad = 7;
        const char* text = displayText.c_str();
        
        struct
        {
            MIDI_event_ex_t evt;
            char data[512];
        } midiSysExData;
        midiSysExData.evt.frame_offset=0;
        midiSysExData.evt.size=0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x66;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = displayType_;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = displayRow_;
        
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = channel_ * 7 + offset_;
        
        int l = strlen(text);
        if (pad < l)
            l = pad;
        if (l > 200)
            l = 200;
        
        int cnt = 0;
        while (cnt < l)
        {
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = *text++;
            cnt++;
        }
        
        while (cnt++ < pad)
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = ' ';
        
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF7;
        
        SendMidiSysExMessage(&midiSysExData.evt);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class XTouchDisplay_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    int offset_ = 0;
    int displayType_ = 0x10;
    int displayRow_ = 0x12;
    int channel_ = 0;
    int preventUpdateTrackColors_ = false;
    string lastStringSent_ = "";
    vector<rgba_color> currentTrackColors_;
    map<string, int> availableColors =
    {
        { "Black", 0 },
        { "Red", 1 },
        { "Green", 2 },
        { "Yellow", 3 },
        { "Blue", 4 },
        { "Magenta", 5 },
        { "Cyan", 6 },
        { "White", 7 }
    };
    
    map<int, rgba_color> availableRGBColors_;
    
        
public:
    virtual ~XTouchDisplay_Midi_FeedbackProcessor() {}
    XTouchDisplay_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, int displayUpperLower, int displayType, int displayRow, int channel) : Midi_FeedbackProcessor(surface, widget), offset_(displayUpperLower * 56), displayType_(displayType), displayRow_(displayRow), channel_(channel)
    {
        rgba_color color;
        
        for(int i = 0; i < surface_->GetNumChannels(); i++)
            currentTrackColors_.push_back(color);
        
        availableRGBColors_[0] = color; // Black
        
        color.r = 255;
        availableRGBColors_[1] = color; // Red

        color.r = 0;
        color.g = 255;
        availableRGBColors_[2] = color; // Green
        
        color.r = 255;
        color.g = 255;
        availableRGBColors_[3] = color; // Yellow
        
        color.r = 0;
        color.g = 0;
        color.b = 255;
        availableRGBColors_[4] = color; // Blue
        
        color.r = 255;
        availableRGBColors_[5] = color; // Magenta

        color.r = 0;
        color.g = 255;
        availableRGBColors_[6] = color; // Cyan

        color.r = 255;
        availableRGBColors_[7] = color; // White
    }
        
    virtual string GetName() override { return "XTouchDisplay_Midi_FeedbackProcessor"; }

    virtual void SetXTouchDisplayColors(string zoneName, string colors) override
    {
        preventUpdateTrackColors_ = true;
        
        vector<string> currentColors = GetTokens(colors);
        
        struct
        {
            MIDI_event_ex_t evt;
            char data[512];
        } midiSysExData;
        midiSysExData.evt.frame_offset=0;
        midiSysExData.evt.size=0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x66;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = displayType_;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x72;
        
        vector<rgba_color> trackColors;
        
        for(int i = 0; i < surface_->GetNumChannels(); i++)
        {
            int surfaceColor = 7; // White
            
            if(currentColors.size() == 1 && availableColors.count(currentColors[0]) > 0)
                surfaceColor = availableColors[currentColors[0]];
            else if(currentColors.size() == 8 && availableColors.count(currentColors[i]) > 0)
                surfaceColor = availableColors[currentColors[i]];

            if(zoneName == "Track")
                trackColors.push_back(availableRGBColors_[surfaceColor]);
            
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = surfaceColor;
        }
        
        if(trackColors.size() > 0)
            surface_->SetFixedTrackColors(trackColors);
        
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF7;
        
        SendMidiSysExMessage(&midiSysExData.evt);
    }
    
    virtual void RestoreXTouchDisplayColors() override
    {
        preventUpdateTrackColors_ = false;
    }
    
    virtual void SetValue(map<string, string> &properties, string displayText) override
    {
        if(displayText != lastStringSent_) // changes since last send
            ForceValue(properties, displayText);
    }

    virtual void ForceValue(map<string, string> &properties, string displayText) override
    {
        lastStringSent_ = displayText;
                
        displayText = GetWidget()->GetSurface()->GetRestrictedLengthText(displayText);
        
        if(displayText == "" || displayText == "-150.00")
            displayText = "       ";

        int pad = 7;
        const char* text = displayText.c_str();
        
        struct
        {
            MIDI_event_ex_t evt;
            char data[512];
        } midiSysExData;
        midiSysExData.evt.frame_offset=0;
        midiSysExData.evt.size=0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x66;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = displayType_;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = displayRow_;
        
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = channel_ * 7 + offset_;
        
        int l = strlen(text);
        if (pad < l)
            l = pad;
        if (l > 200)
            l = 200;
        
        int cnt = 0;
        while (cnt < l)
        {
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = *text++;
            cnt++;
        }
        
        while (cnt++ < pad)
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = ' ';
        
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF7;
        
        SendMidiSysExMessage(&midiSysExData.evt);
        
        ForceUpdateTrackColors();
    }

    virtual void UpdateTrackColors() override
    {
        bool shouldUpdate = false;
          
        vector<rgba_color> trackColors = surface_->GetTrackColors();
        
        for(int i = 0; i < trackColors.size(); i++)
        {
            if(trackColors[i] != currentTrackColors_[i])
            {
                shouldUpdate = true;
                break;
            }
        }
        
        if(shouldUpdate)
            ForceUpdateTrackColors();
    }
    
    virtual void ForceUpdateTrackColors() override
    {
        if(preventUpdateTrackColors_)
            return;
        
        struct
        {
            MIDI_event_ex_t evt;
            char data[512];
        } midiSysExData;
        midiSysExData.evt.frame_offset=0;
        midiSysExData.evt.size=0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x66;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = displayType_;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x72;

        vector<rgba_color> trackColors = surface_->GetTrackColors();
        
        for(int i = 0; i < trackColors.size(); i++)
        {
            if(lastStringSent_ == "")
            {
                midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x07; // White
            }
            else
            {
                rgba_color color = trackColors[i];
                
                currentTrackColors_[i] = color;
                
                int r = color.r;
                int g = color.g;
                int b = color.b;

                int surfaceColor = 0;
                
                if(abs(r - g) < 30 && abs(r - b) < 30 && abs(g - b) < 30)  // White
                    surfaceColor = 7;
                else if(abs(r - g) < 30 && r > b && g > b)  // Yellow r + g
                    surfaceColor = 3;
                else if(abs(r - b) < 30 && r > g && b > g)  // Magenta r + b
                    surfaceColor = 5;
                else if(abs(g - b) < 30 && g > r && b > r)  // Cyan g + b
                    surfaceColor = 6;
                else if(r > g && r > b) // Red
                    surfaceColor = 1;
                else if(g > r && g > b) // Green
                    surfaceColor = 2;
                else if(b > r && b > g) // Blue
                    surfaceColor = 4;


                midiSysExData.evt.midi_message[midiSysExData.evt.size++] = surfaceColor;
            }
        }

        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF7;
        
        SendMidiSysExMessage(&midiSysExData.evt);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FPDisplay_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    int displayType_ = 0x02;
    int displayRow_ = 0x00;
    int channel_ = 0;
    string lastStringSent_ = " ";
    
    int GetTextAlign(map<string, string> &properties)
    {
        // Center: 0, Left: 1, Right: 2
        if(properties.count("TextAlign") > 0)
        {
            if(properties["TextAlign"] == "Left")
                return 1;
            else if(properties["TextAlign"] == "Right")
                return 2;
        }

        return 0;
    }
    
    int GetTextInvert(map<string, string> &properties)
    {
        if(properties.count("TextInvert") > 0)
            if(properties["TextInvert"] == "Yes")
                return 4;

        return 0;
    }
    
public:
    virtual ~FPDisplay_Midi_FeedbackProcessor() {}
    FPDisplay_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, int displayType, int channel, int displayRow) : Midi_FeedbackProcessor(surface, widget), displayType_(displayType), channel_(channel), displayRow_(displayRow) { }
    
    virtual string GetName() override { return "FPDisplay_Midi_FeedbackProcessor"; }

    virtual void ClearCache() override
    {
        lastStringSent_ = " ";
    }
    
    virtual void SetValue(map<string, string> &properties, string displayText) override
    {
        if(displayText == lastStringSent_) // changes since last send
            return;

        ForceValue(properties, displayText);
    }
    
    virtual void ForceValue(map<string, string> &properties, string displayText) override
    {
        lastStringSent_ = displayText;
        
        displayText = GetWidget()->GetSurface()->GetRestrictedLengthText(displayText);

        if(displayText == "")
            displayText = "                            ";
        
        const char* text = displayText.c_str();
    
        int invert = lastStringSent_ == "" ? 0 : GetTextInvert(properties); // prevent empty inverted lines
        int align = 0x0000000 + invert + GetTextAlign(properties);

        struct
        {
            MIDI_event_ex_t evt;
            char data[512];
        }
        
        midiSysExData;
        
        midiSysExData.evt.frame_offset = 0;
        midiSysExData.evt.size = 0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x01;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x06;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = displayType_; // Faderport8=0x02, Faderport16=0x16
        
        // <SysExHdr> 12, xx, yy, zz, tx,tx,tx,... F7
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x12;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = channel_;      // xx channel_ id
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = displayRow_;   // yy line number 0-3
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = align;         // zz alignment flag 0000000=centre, see manual for other setups.
        
        int length = strlen(text);
        
        if (length > 30)
            length = 30;
        
        int count = 0;
        
        while (count < length)
        {
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = *text++;                // tx text in ASCII format
            count++;
        }
        
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF7;
        
        SendMidiSysExMessage(&midiSysExData.evt);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FPScribbleStripMode_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    int displayType_ = 0x02;
    int channel_ = 0;
    int lastMode_ = 0;

    int GetMode(map<string, string> &properties)
    {
        int param = 2;

        if(properties.count("Mode") > 0)
            param = stoi(properties["Mode"]);

        if (param >= 0 && param < 9)
            return param;
        
        return 2;
    }
    
public:
    virtual ~FPScribbleStripMode_Midi_FeedbackProcessor() {}
    FPScribbleStripMode_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, int displayType, int channel) : Midi_FeedbackProcessor(surface, widget), displayType_(displayType), channel_(channel) { }
    
    virtual string GetName() override { return "FPScribbleStripMode_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, double value) override
    {
        if(lastMode_ == GetMode(properties))
            return;
            
        ForceValue(properties, value);
    }
    
    virtual void ForceValue(map<string, string> &properties, double value) override
    {
        lastMode_ = GetMode(properties);
        
        struct
        {
            MIDI_event_ex_t evt;
            char data[512];
        }
        midiSysExData;
        
        midiSysExData.evt.frame_offset = 0;
        midiSysExData.evt.size = 0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x01;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x06;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = displayType_; // Faderport8=0x02, Faderport16=0x16
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x13;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = channel_;     // xx channel_ id
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00 + lastMode_; //    0x00 + value; // type of display layout

        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF7;
        SendMidiSysExMessage(&midiSysExData.evt);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class QConLiteDisplay_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    int offset_ = 0;
    int displayType_ = 0x14;
    int displayRow_ = 0x12;
    int channel_ = 0;
    string lastStringSent_ = "";
    
public:
    virtual ~QConLiteDisplay_Midi_FeedbackProcessor() {}
    QConLiteDisplay_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, int displayUpperLower, int displayType, int displayRow, int channel) : Midi_FeedbackProcessor(surface, widget), offset_(displayUpperLower * 28), displayType_(displayType), displayRow_(displayRow), channel_(channel) { }
    
    virtual string GetName() override { return "QConLiteDisplay_Midi_FeedbackProcessor"; }

    virtual void ClearCache() override
    {
        lastStringSent_ = " ";
    }
    
    virtual void SetValue(map<string, string> &properties, string displayText) override
    {
        if(displayText != lastStringSent_) // changes since last send
            ForceValue(properties, displayText);
    }
    
    virtual void ForceValue(map<string, string> &properties, string displayText) override
    {
        lastStringSent_ = displayText;
        
        displayText = GetWidget()->GetSurface()->GetRestrictedLengthText(displayText);

        if(displayText == "")
            displayText = "       ";
        
        int pad = 7;
        const char* text = displayText.c_str();
        
        struct
        {
            MIDI_event_ex_t evt;
            char data[512];
        } midiSysExData;
        midiSysExData.evt.frame_offset=0;
        midiSysExData.evt.size=0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF0;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x00;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0x66;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = displayType_;
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = displayRow_;
        
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = channel_ * 7 + offset_;
        
        int l = strlen(text);
        if (pad < l)
            l = pad;
        if (l > 200)
            l = 200;
        
        int cnt = 0;
        while (cnt < l)
        {
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = *text++;
            cnt++;
        }
        
        while (cnt++ < pad)
            midiSysExData.evt.midi_message[midiSysExData.evt.size++] = ' ';
        
        midiSysExData.evt.midi_message[midiSysExData.evt.size++] = 0xF7;
        
        SendMidiSysExMessage(&midiSysExData.evt);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FB_MCU_AssignmentDisplay_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    int lastFirstLetter_ = 0x00;

public:
    FB_MCU_AssignmentDisplay_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget) : Midi_FeedbackProcessor(surface, widget) {}
    
    virtual string GetName() override { return "FB_MCU_AssignmentDisplay_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, double value) override
    {
        if(value == 0.0) // Selected Track
        {
            if(lastFirstLetter_ != 0x13)
            {
                lastFirstLetter_ = 0x13;
                SendMidiMessage(0xB0, 0x4B, 0x13); // S
                SendMidiMessage(0xB0, 0x4A, 0x05); // E
            }
        }
        else if(value == 1.0) // Track
        {
            if(lastFirstLetter_ != 0x07)
            {
                lastFirstLetter_ = 0x07;
                SendMidiMessage(0xB0, 0x4B, 0x07); // G
                SendMidiMessage(0xB0, 0x4A, 0x0C); // L
            }
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MCU_TimeDisplay_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
private:
    char m_mackie_lasttime[10];
    int m_mackie_lasttime_mode;
    DWORD m_mcu_timedisp_lastforce, m_mcu_meter_lastrun;
    
public:
    MCU_TimeDisplay_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget) : Midi_FeedbackProcessor(surface, widget) {}
    
    virtual string GetName() override { return "MCU_TimeDisplay_Midi_FeedbackProcessor"; }

    virtual void SetValue(map<string, string> &properties, double value) override
    {
        
#ifndef timeGetTime
        DWORD now = GetTickCount();
#else
        DWORD now = timeGetTime();
#endif
        
        double pp=(DAW::GetPlayState()&1) ? DAW::GetPlayPosition() : DAW::GetCursorPosition();
        unsigned char bla[10];
        
        memset(bla,0,sizeof(bla));
        
        int *tmodeptr = TheManager->GetTimeMode2Ptr();
        
        int tmode=0;
        
        if (tmodeptr && (*tmodeptr)>=0) tmode = *tmodeptr;
        else
        {
            tmodeptr = TheManager->GetTimeModePtr();
            if (tmodeptr)
                tmode=*tmodeptr;
        }
        
        if (tmode==3) // seconds
        {
            double *toptr = TheManager->GetTimeOffsPtr();
            
            if (toptr) pp+=*toptr;
            char buf[64];
            snprintf(buf, sizeof(buf),"%d %02d",(int)pp, ((int)(pp*100.0))%100);
            if (strlen(buf)>sizeof(bla)) memcpy(bla,buf+strlen(buf)-sizeof(bla),sizeof(bla));
            else
                memcpy(bla+sizeof(bla)-strlen(buf),buf,strlen(buf));
            
        }
        else if (tmode==4) // samples
        {
            char buf[128];
            DAW::format_timestr_pos(pp,buf,sizeof(buf),4);
            if (strlen(buf)>sizeof(bla)) memcpy(bla,buf+strlen(buf)-sizeof(bla),sizeof(bla));
            else
                memcpy(bla+sizeof(bla)-strlen(buf),buf,strlen(buf));
        }
        else if (tmode==5) // frames
        {
            char buf[128];
            DAW::format_timestr_pos(pp,buf,sizeof(buf),5);
            char *p=buf;
            char *op=buf;
            int ccnt=0;
            while (*p)
            {
                if (*p == ':')
                {
                    ccnt++;
                    if (ccnt!=3)
                    {
                        p++;
                        continue;
                    }
                    *p=' ';
                }
                
                *op++=*p++;
            }
            *op=0;
            if (strlen(buf)>sizeof(bla)) memcpy(bla,buf+strlen(buf)-sizeof(bla),sizeof(bla));
            else
                memcpy(bla+sizeof(bla)-strlen(buf),buf,strlen(buf));
        }
        else if (tmode>0)
        {
            int num_measures=0;
            int currentTimeSignatureNumerator=0;
            double beats=DAW::TimeMap2_timeToBeats(NULL,pp,&num_measures,&currentTimeSignatureNumerator,NULL,NULL)+ 0.000000000001;
            double nbeats = floor(beats);
            
            beats -= nbeats;
            
            if (num_measures <= 0 && pp < 0.0)
                --num_measures;
            
            int *measptr = TheManager->GetMeasOffsPtr();
            int nm=num_measures+1+(measptr ? *measptr : 0);
            
            // Here we display a '-' minus sign so we make it clearer that we are on "count down".
            // Corner case: if the measure is less than -99 this won't work...
            if (nm < 0)
                bla[0] = '-';
            
            nm=std::abs(nm);
            
            if (nm >= 100) bla[0]='0'+(nm/100)%10;  //bars hundreds
            if (nm >= 10) bla[1]='0'+(nm/10)%10;    //bars tens
            bla[2]='0'+(nm)%10;//bars
            
            int nb=(pp < 0.0 ? currentTimeSignatureNumerator : 0) + (int)nbeats +1;
            if (nb >= 10) bla[3]='0'+(nb/10)%10;    //beats tens
            bla[4]='0'+(nb)%10;                     //beats
            
            const int fracbeats = (int) (1000.0 * beats);
            bla[7]='0' + (fracbeats/100)%10;
            bla[8]='0' + (fracbeats/10)%10;
            bla[9]='0' + (fracbeats%10);            // frames
        }
        else
        {
            double *toptr = TheManager->GetTimeOffsPtr();
            if (toptr) pp+=(*toptr);
            
            int ipp=(int)pp;
            int fr=(int)((pp-ipp)*1000.0);
            
            if (ipp >= 360000) bla[0]='0'+(ipp/360000)%10;//hours hundreds
            if (ipp >= 36000) bla[1]='0'+(ipp/36000)%10;//hours tens
            if (ipp >= 3600) bla[2]='0'+(ipp/3600)%10;//hours
            
            bla[3]='0'+(ipp/600)%6;//min tens
            bla[4]='0'+(ipp/60)%10;//min
            bla[5]='0'+(ipp/10)%6;//sec tens
            bla[6]='0'+(ipp%10);//sec
            bla[7]='0' + (fr/100)%10;
            bla[8]='0' + (fr/10)%10;
            bla[9]='0' + (fr%10); // frames
        }
        
        if (m_mackie_lasttime_mode != tmode)
        {
            m_mackie_lasttime_mode=tmode;
            SendMidiMessage(0x90, 0x71, tmode==5?0x7F:0); // set smpte light
            SendMidiMessage(0x90, 0x72, m_mackie_lasttime_mode>0 && tmode<3?0x7F:0); // set beats light
            
            // Blank display on mode change
            for (int x = 0 ; x < sizeof(bla) ; x++)
                SendMidiMessage(0xB0,0x40+x,0x20);
            
        }
        
        if (memcmp(m_mackie_lasttime,bla,sizeof(bla)))
        {
            bool force=false;
            if (now > m_mcu_timedisp_lastforce)
            {
                m_mcu_timedisp_lastforce=now+2000;
                force=true;
            }
            
            for (int x =0 ; x < sizeof(bla) ; x ++)
            {
                int idx=sizeof(bla)-x-1;
                if (bla[idx]!=m_mackie_lasttime[idx]||force)
                {
                    SendMidiMessage(0xB0,0x40+x,bla[idx]);
                    m_mackie_lasttime[idx]=bla[idx];
                }
            }
        }
    }
};



// Color maps are stored in Blue Green Red format
uint8_t colorMap7[128][3] = { {0, 0, 0},    // 0
    {255, 0, 0},    // 1 - Blue
    {255, 21, 0},    // 2 - Blue (Green Rising)
    {255, 34, 0},
    {255, 46, 0},
    {255, 59, 0},
    {255, 68, 0},
    {255, 80, 0},
    {255, 93, 0},
    {255, 106, 0},
    {255, 119, 0},
    {255, 127, 0},
    {255, 140, 0},
    {255, 153, 0},
    {255, 165, 0},
    {255, 178, 0},
    {255, 191, 0},
    {255, 199, 0},
    {255, 212, 0},
    {255, 225, 0},
    {255, 238, 0},
    {255, 250, 0},    // 21 - End of Blue's Reign
    
    {250, 255, 0}, // 22 - Green (Blue Fading)
    {237, 255, 0},
    {225, 255, 0},
    {212, 255, 0},
    {199, 255, 0},
    {191, 255, 0},
    {178, 255, 0},
    {165, 255, 0},
    {153, 255, 0},
    {140, 255, 0},
    {127, 255, 0},
    {119, 255, 0},
    {106, 255, 0},
    {93, 255, 0},
    {80, 255, 0},
    {67, 255, 0},
    {59, 255, 0},
    {46, 255, 0},
    {33, 255, 0},
    {21, 255, 0},
    {8, 255, 0},
    {0, 255, 0},    // 43 - Green
    
    {0, 255, 12},    // 44 - Green/ Red Rising
    {0, 255, 25},
    {0, 255, 38},
    {0, 255, 51},
    {0, 255, 63},
    {0, 255, 72},
    {0, 255, 84},
    {0, 255, 97},
    {0, 255, 110},
    {0, 255, 123},
    {0, 255, 131},
    {0, 255, 144},
    {0, 255, 157},
    {0, 255, 170},
    {0, 255, 182},
    {0, 255, 191},
    {0, 255, 203},
    {0, 255, 216},
    {0, 255, 229},
    {0, 255, 242},
    {0, 255, 255},    // 64 - Green + Red (Yellow)
    
    {0, 246, 255},    // 65 - Red, Green Fading
    {0, 233, 255},
    {0, 220, 255},
    {0, 208, 255},
    {0, 195, 255},
    {0, 187, 255},
    {0, 174, 255},
    {0, 161, 255},
    {0, 148, 255},
    {0, 135, 255},
    {0, 127, 255},
    {0, 114, 255},
    {0, 102, 255},
    {0, 89, 255},
    {0, 76, 255},
    {0, 63, 255},
    {0, 55, 255},
    {0, 42, 255},
    {0, 29, 255},
    {0, 16, 255},
    {0, 4, 255},    // 85 - End Red/Green Fading
    
    {4, 0, 255},    // 86 - Red/ Blue Rising
    {16, 0, 255},
    {29, 0, 255},
    {42, 0, 255},
    {55, 0, 255},
    {63, 0, 255},
    {76, 0, 255},
    {89, 0, 255},
    {102, 0, 255},
    {114, 0, 255},
    {127, 0, 255},
    {135, 0, 255},
    {148, 0, 255},
    {161, 0, 255},
    {174, 0, 255},
    {186, 0, 255},
    {195, 0, 255},
    {208, 0, 255},
    {221, 0, 255},
    {233, 0, 255},
    {246, 0, 255},
    {255, 0, 255},    // 107 - Blue + Red
    
    {255, 0, 242},    // 108 - Blue/ Red Fading
    {255, 0, 229},
    {255, 0, 216},
    {255, 0, 204},
    {255, 0, 191},
    {255, 0, 182},
    {255, 0, 169},
    {255, 0, 157},
    {255, 0, 144},
    {255, 0, 131},
    {255, 0, 123},
    {255, 0, 110},
    {255, 0, 97},
    {255, 0, 85},
    {255, 0, 72},
    {255, 0, 63},
    {255, 0, 50},
    {255, 0, 38},
    {255, 0, 25},    // 126 - Blue-ish
    {225, 240, 240}    // 127 - White ?
};

int GetColorIntFromRGB(int r, int g, int b)
{
    if(b == 0 && g == 0 && r == 0)
        return 0;
    else if(b > 224 && g > 239 && r > 239)
        return 127;
    else if(b == 255 && r == 0)
    {
        for(int i = 1; i < 22; i++)
            if(g > colorMap7[i - 1][1] && g <= colorMap7[i][1])
                return i;
    }
    else if(g == 255 && r == 0)
    {
        for(int i = 22; i < 44; i++)
            if(b < colorMap7[i - 1][0] && b >= colorMap7[i][0])
                return i;
    }
    else if(b == 0 && g == 255)
    {
        for(int i = 44; i < 65; i++)
            if(r > colorMap7[i - 1][2] && r <= colorMap7[i][2])
                return i;
    }
    else if(b == 0 && r == 255)
    {
        for(int i = 65; i < 86; i++)
            if(g < colorMap7[i - 1][1] && g >= colorMap7[i][1])
                return i;
    }
    else if(g == 0 && r == 255)
    {
        for(int i = 86; i < 108; i++)
            if(b > colorMap7[i - 1][0] && b <= colorMap7[i][0])
                return i;
    }
    else if(b == 255 && g == 0)
    {
        for(int i = 108; i < 127; i++)
            if(r < colorMap7[i - 1][2] && r >= colorMap7[i][2])
                return i;
    }
    
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MFT_RGB_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    virtual ~MFT_RGB_Midi_FeedbackProcessor() {}
    MFT_RGB_Midi_FeedbackProcessor(shared_ptr<Midi_ControlSurface> surface, shared_ptr<Widget> widget, shared_ptr<MIDI_event_ex_t> feedback1) : Midi_FeedbackProcessor(surface, widget, feedback1) { }
    
    virtual void ForceColorValue(rgba_color color) override
    {
        lastColor_ = color;
        
        if((color.r == 177 || color.r == 181) && color.g == 31) // this sets the different MFT modes
            SendMidiMessage(color.r, color.g, color.b);
        else
        {
            int colour = GetColorIntFromRGB(color.r, color.g, color.b);
            if (colour == 0)
                SendMidiMessage(midiFeedbackMessage1_->midi_message[0] + 1, midiFeedbackMessage1_->midi_message[1], 17); // turn off led
            else
            {
                SendMidiMessage(midiFeedbackMessage1_->midi_message[0], midiFeedbackMessage1_->midi_message[1], colour); // set color
                SendMidiMessage(midiFeedbackMessage1_->midi_message[0] + 1, midiFeedbackMessage1_->midi_message[1], 47);  // turn on led max brightness
            }
        }
    }

    virtual void SetColorValue(rgba_color color) override
    {
        if(color != lastColor_)
            ForceColorValue(color);
    }
};

#endif /* control_surface_midi_widgets_h */


