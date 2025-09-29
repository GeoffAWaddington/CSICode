# CSI Code Specifically Modified for the Icon V1-M Surface
## The Major Changes

**Four new classes. (control_surface_midi_widgets.h)**

```
	class V1MDisplay_Midi_FeedbackProcessor : public Midi_FeedbackProcessor

	class V1MTrackColors_Midi_FeedbackProcessor : public Midi_FeedbackProcessor

	class V1MDisplay_Midi_FeedbackProcessor : public Midi_FeedbackProcessor

	class V1MVUMeter_Midi_FeedbackProcessor : public Midi_FeedbackProcessor
	    (This handles both master and channel strip VUMeters since there is only one difference in the code.
       channel strip VU = 0xD0 and MasterVU = 0xD1)
```

**Widgets linked to the new classes (control_surface_integrator.cpp)**

```
	    else if (widgetType == "FB_V1MMasterVUMeter" && size == 2)
        {
            int displayType = NULL;
            int code = 0xD1; //Master
            widget->GetFeedbackProcessors().push_back(make_unique<V1MVUMeter_Midi_FeedbackProcessor>(csi_, this, widget, code, displayType, atoi(tokenLines[i][1].c_str())));
        }
		    else if ((widgetType == "FB_V1MVUMeter" || widgetType == "FB_V1MXVUMeter") && size == 2) //Kev Smart
        {
            int displayType = (widgetType == "FB_V1MVUMeter") ? 0x14 : 0x15;
            int code = 0xD0; //Channel Strip
            widget->GetFeedbackProcessors().push_back(make_unique<V1MVUMeter_Midi_FeedbackProcessor>(csi_, this, widget, code, displayType, atoi(tokenLines[i][1].c_str())));
            SetHasMCUMeters(displayType);
        }

        else if (widgetType == "FB_V1MTrackColors")
        {
            widget->GetFeedbackProcessors().push_back(make_unique<V1MTrackColors_Midi_FeedbackProcessor>(csi_, this, widget));
            AddTrackColorFeedbackProcessor(widget->GetFeedbackProcessors().back().get());
        }

        else if ((widgetType == "FB_V1MDisplay1Upper" || widgetType == "FB_V1MDisplay1Lower" || widgetType == "FB_V1MDisplay2Upper" || widgetType == "FB_V1MDisplay2Lower") && size == 2) //Kev Smart
        {
            if (widgetType == "FB_V1MDisplay1Upper")
                widget->GetFeedbackProcessors().push_back(make_unique<V1MDisplay_Midi_FeedbackProcessor>(csi_, this, widget, 1, 0x14, 0x12, atoi(tokenLines[i][1].c_str()), 0x00, 0x66));
            else if (widgetType == "FB_V1MDisplay1Lower")
                widget->GetFeedbackProcessors().push_back(make_unique<V1MDisplay_Midi_FeedbackProcessor>(csi_, this, widget, 0, 0x14, 0x12, atoi(tokenLines[i][1].c_str()), 0x00, 0x66));
            else if (widgetType == "FB_V1MDisplay2Upper")
                widget->GetFeedbackProcessors().push_back(make_unique<V1MDisplay_Midi_FeedbackProcessor>(csi_, this, widget, 1, 0x15, 0x13, atoi(tokenLines[i][1].c_str()), 0x02, 0x4e));
            else if (widgetType == "FB_V1MDisplay2Lower")
                widget->GetFeedbackProcessors().push_back(make_unique<V1MDisplay_Midi_FeedbackProcessor>(csi_, this, widget, 0, 0x15, 0x13, atoi(tokenLines[i][1].c_str()), 0x02, 0x4e));
        }
```

**The New widgets (Surface.txt)**
```
Widget TrackColors
	FB_V1MTrackColors
WidgetEnd

Widget Display1Upper1
    FB_V1MDisplay1Upper 0
WidgetEnd
.............
Widget Display1Upper8
    FB_V1MDisplay1Upper 7
WidgetEnd

Widget Display1Lower1
    FB_V1MDisplay1Lower 0
WidgetEnd
................
Widget Display1Lower8
    FB_V1MDisplay1Lower 7
WidgetEnd

Widget Display2Upper1
    FB_V1MDisplay2Upper 0
WidgetEnd
.............
Widget Display2Upper8
    FB_V1MDisplay2Upper 7
WidgetEnd

Widget Display2Lower1
    FB_V1MDisplay2Lower 0
WidgetEnd
................
Widget Display2Lower8
    FB_V1MDisplay2Lower 7
WidgetEnd

Widget MasterChannelMeterLeft 
	FB_V1MMasterVUMeter 0
WidgetEnd

Widget MasterChannelMeterRight
	FB_V1MMasterVUMeter 1
WidgetEnd

Widget VUMeter1
	FB_V1MVUMeter 0
WidgetEnd
.............
Widget VUMeter8
	FB_V1MVUMeter 7
WidgetEnd
```











```



