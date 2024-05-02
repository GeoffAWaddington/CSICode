//
//  control_surface_OSC_widgets.h
//  reaper_csurf_integrator
//

#ifndef control_surface_OSC_widgets_h
#define control_surface_OSC_widgets_h

#include "handy_functions.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_X32FeedbackProcessor : public OSC_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    OSC_X32FeedbackProcessor(CSurfIntegrator *const csi, OSC_ControlSurface *surface, Widget *widget, const char *oscAddress) : OSC_FeedbackProcessor(csi, surface, widget, oscAddress)  {}
    ~OSC_X32FeedbackProcessor() {}

    virtual const char *GetName() override { return "OSC_X32FeedbackProcessor"; }

    virtual void SetColorValue(const rgba_color &color) override
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
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_X32IntFeedbackProcessor : public OSC_IntFeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    OSC_X32IntFeedbackProcessor(CSurfIntegrator *const csi, OSC_ControlSurface *surface, Widget *widget, const char *oscAddress) : OSC_IntFeedbackProcessor(csi, surface, widget, oscAddress) {}
    ~OSC_X32IntFeedbackProcessor() {}

    virtual const char *GetName() override { return "OSC_X32IntFeedbackProcessor"; }
    
    virtual void ForceValue(const PropertyList &properties, double value) override
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

    virtual void SetColorValue(const rgba_color &color) override
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
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class X32_Fader_OSC_MessageGenerator : public CSIMessageGenerator
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    X32_Fader_OSC_MessageGenerator(CSurfIntegrator *const csi, Widget *widget) : CSIMessageGenerator(csi, widget) {}
    ~X32_Fader_OSC_MessageGenerator() {}

    virtual const char *GetName() override { return "X32_Fader_OSC_MessageGenerator"; }

    virtual void ProcessMessage(double value) override
    {
        if      (value >= 0.5)    value = value *  40.0 - 30.0;  // max dB value: +10.
        else if (value >= 0.25)   value = value *  80.0 - 50.0;
        else if (value >= 0.0625) value = value * 160.0 - 70.0;
        else if (value >= 0.0)    value = value * 480.0 - 90.0;  // min dB value: -90 or -oo

        widget_->GetZoneManager()->DoAction(widget_, value);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class OSC_X32FaderFeedbackProcessor : public OSC_FeedbackProcessor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
public:
    OSC_X32FaderFeedbackProcessor(CSurfIntegrator *const csi, OSC_ControlSurface *surface, Widget *widget, const char *oscAddress) : OSC_FeedbackProcessor(csi, surface, widget, oscAddress) {}
    ~OSC_X32FaderFeedbackProcessor() {}

    virtual const char *GetName() override { return "OSC_X32FaderFeedbackProcessor"; }
    
    virtual void ForceValue(const PropertyList &properties, double value) override
    {
        lastDoubleValue_ = value;

        if      (value < -60.0) value = (value + 90.0) / 480.0;
        else if (value < -30.0) value = (value + 70.0) / 160.0;
        else if (value < -10.0) value = (value + 50.0) /  80.0;
        else if (value <= 10.0) value = (value + 30.0) /  40.0;

        surface_->SendOSCMessage(this, oscAddress_.c_str(), value);
    }
};

#endif /* control_surface_OSC_widgets_h */
