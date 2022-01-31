//
//  ReportLoggingEtc.h
//  reaper_csurf_integrator
//
//  Created by Geoff Waddington on 2019-12-05.
//  Copyright © 2019 Geoff Waddington. All rights reserved.
//

#ifndef ReportLoggingEtc_h
#define ReportLoggingEtc_h

#include <string>

#include "reaper_plugin_functions.h"

/////////////////////////////////////////////////
class LOG
/////////////////////////////////////////////////
{
public:
    static void InitializationFailure(std::string logEntry)
    {
        // if someConfig->LogInitializationFailureToConsole == true
        ShowConsoleMsg(("INIT_FAIL: " + logEntry).c_str());
        
        // if someConfig->LogInitializationFailureToDisk == true
        // LogInitializationFailureToDisk("INIT: " + logEntry));
        
        // ... etc.
    }
    
    
    /*
     
    static void SomeOtherFailure(std::string logEntry)
    {
        // if someConfig->LogSomeOtherFailureToConsole == true
        ShowConsoleMsg("SOMEOTHER_FAIL: " + logEntry).c_str());
        
        // if someConfig->LogSomeOtherFailureToDisk == true
        // LogSomeOtherFailureToDisk("SOMEOTHER_FAIL: " + logEntry));
        
        // ... etc.
    }
    
     
     
     ... etc.
     
     
     
     
     */
     

    
    
};

#endif /* ReportLoggingEtc_h */
