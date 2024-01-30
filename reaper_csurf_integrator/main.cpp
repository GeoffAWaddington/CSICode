#define REAPERAPI_IMPLEMENT
#define REAPERAPI_DECL

#include "reaper_plugin_functions.h"
#include "resource.h"

gaccel_register_t acreg_show_raw_input =
{
    {FCONTROL|FALT|FVIRTKEY, '0', 0},
    "CSI Toggle Show Raw Input from Surfaces"
};

int g_registered_command_toggle_show_raw_surface_input = 0;

gaccel_register_t acreg_show_input =
{
    {FCONTROL|FALT|FVIRTKEY, '1', 0},
    "CSI Toggle Show Input from Surfaces"
};

int g_registered_command_toggle_show_surface_input = 0;

gaccel_register_t acreg_show_output =
{
    {FCONTROL|FALT|FVIRTKEY, '2', 0},
    "CSI Toggle Show Output to Surfaces"
};

int g_registered_command_toggle_show_surface_output = 0;

gaccel_register_t acreg_show_FX_params =
{
    {FCONTROL|FALT|FVIRTKEY, '3', 0},
    "CSI Toggle Show Params when FX inserted"
};

int g_registered_command_toggle_show_FX_params = 0;

gaccel_register_t acreg_write_FX_params =
{
    {FCONTROL|FALT|FVIRTKEY, '4', 0},
    "CSI Toggle Write Params to /CSI/Zones/ZoneRawFXFiles when FX inserted"
};

int g_registered_command_toggle_write_FX_params = 0;


extern bool hookCommandProc(int command, int flag);

extern void ShutdownOSCIO();

extern reaper_csurf_reg_t csurf_integrator_reg;

REAPER_PLUGIN_HINSTANCE g_hInst; // used for dialogs, if any
HWND g_hwnd;
reaper_plugin_info_t *g_reaper_plugin_info;

extern "C"
{
REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t *reaper_plugin_info)
{
    g_hInst = hInstance;
    
    if (! reaper_plugin_info)
    {
        ShutdownOSCIO();
        return 0;
    }
    
    if (reaper_plugin_info->caller_version != REAPER_PLUGIN_VERSION || !reaper_plugin_info->GetFunc)
        return 0;

    if (reaper_plugin_info)
    {
        g_hwnd = reaper_plugin_info->hwnd_main;
        g_reaper_plugin_info = reaper_plugin_info;

        // load Reaper API functions
        if (REAPERAPI_LoadAPI(reaper_plugin_info->GetFunc) > 0)
        {
            return 0;
        }
      
        reaper_plugin_info->Register("csurf",&csurf_integrator_reg);
 
        acreg_show_raw_input.accel.cmd = g_registered_command_toggle_show_raw_surface_input = reaper_plugin_info->Register("command_id", (void*)"CSI Toggle Show Raw Input from Surfaces");
        
        if (!g_registered_command_toggle_show_raw_surface_input)
            return 0; // failed getting a command id, fail!
        
        reaper_plugin_info->Register("gaccel", &acreg_show_raw_input);
        
        
        acreg_show_input.accel.cmd = g_registered_command_toggle_show_surface_input = reaper_plugin_info->Register("command_id", (void*)"CSI Toggle Show Input from Surfaces");
        
        if (!g_registered_command_toggle_show_surface_input)
            return 0; // failed getting a command id, fail!
        
        reaper_plugin_info->Register("gaccel", &acreg_show_input);
        
        
        acreg_show_output.accel.cmd = g_registered_command_toggle_show_surface_output = reaper_plugin_info->Register("command_id", (void*)"CSI Toggle Show Output to Surfaces");
        
        if (!g_registered_command_toggle_show_surface_output)
            return 0; // failed getting a command id, fail!
        
        reaper_plugin_info->Register("gaccel", &acreg_show_output);
        
        
        acreg_show_FX_params.accel.cmd = g_registered_command_toggle_show_FX_params = reaper_plugin_info->Register("command_id", (void*)"CSI Toggle Show Params when FX inserted");
        
        if (!g_registered_command_toggle_show_FX_params)
            return 0; // failed getting a command id, fail!
        
        reaper_plugin_info->Register("gaccel", &acreg_show_FX_params);
        
        acreg_write_FX_params.accel.cmd = g_registered_command_toggle_write_FX_params = reaper_plugin_info->Register("command_id", (void*)"CSI Toggle Write Params to /CSI/Zones/ZoneRawFXFiles when FX inserted");
        
        if (!g_registered_command_toggle_write_FX_params)
            return 0; // failed getting a command id, fail!
        
        reaper_plugin_info->Register("gaccel", &acreg_write_FX_params);
        

        reaper_plugin_info->Register("hookcommand", (void*)hookCommandProc);
        
      
        // plugin registered
        return 1;
    }
    else
    {
        return 0;
    }
}

};
    
#ifndef _WIN32 // import the resources. Note: if you do not have these files, run "php WDL/swell/mac_resgen.php res.rc" from this directory
#include "./WDL/swell/swell-dlggen.h"
#include "res.rc_mac_dlg"
#endif
