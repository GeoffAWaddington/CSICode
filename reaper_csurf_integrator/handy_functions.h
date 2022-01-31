//
//  handy_reaper_functions.h
//  reaper_csurf_integrator
//
//

#ifndef handy_functions_h
#define handy_functions_h

#include "reaper_plugin_functions.h"
#include "WDL/db2val.h"

//slope = (output_end - output_start) / (input_end - input_start)
//output = output_start + slope * (input - input_start)

static double int14ToNormalized(unsigned char msb, unsigned char lsb)
{
    int val = lsb | (msb<<7);
    double normalizedVal = val/16383.0;
    
    normalizedVal = normalizedVal < 0.0 ? 0.0 : normalizedVal;
    normalizedVal = normalizedVal > 1.0 ? 1.0 : normalizedVal;
    
    return normalizedVal;
}

static double normalizedToVol(double val)
{
    double pos=val*1000.0;
    pos=SLIDER2DB(pos);
    return DB2VAL(pos);
}

static double volToNormalized(double vol)
{
    double d=(DB2SLIDER(VAL2DB(vol))/1000.0);
    if (d<0.0)d=0.0;
    else if (d>1.0)d=1.0;
    
    return d;
}

static double normalizedToPan(double val)
{
    return 2.0 * val - 1.0;
}

static double panToNormalized(double val)
{
    return 0.5 * (val + 1.0);
}
/*
static double charToVol(unsigned char val)
{
    double pos=((double)val*1000.0)/127.0;
    pos=SLIDER2DB(pos);
    return DB2VAL(pos);
}

static double int14ToNormal(unsigned char msb, unsigned char lsb)
{
    int val=lsb | (msb<<7);
    return val/16383.0;
}

static double int14ToDB(unsigned char msb, unsigned char lsb)
{
    int val=lsb | (msb<<7);
    double pos=((double)val*1000.0)/16383.0;
    return SLIDER2DB(pos);
}

static double int14ToVol(unsigned char msb, unsigned char lsb)
{
    int val=lsb | (msb<<7);
    double pos=((double)val*1000.0)/16383.0;
    pos=SLIDER2DB(pos);
    return DB2VAL(pos);
}

static double int14ToPan(unsigned char msb, unsigned char lsb)
{
    int val=lsb | (msb<<7);
    return 1.0 - (val/(16383.0*0.5));
}

static int volToInt14(double vol)
{
    double d=(DB2SLIDER(VAL2DB(vol))*16383.0/1000.0);
    if (d<0.0)d=0.0;
    else if (d>16383.0)d=16383.0;
    
    return (int)(d+0.5);
}

static  int panToInt14(double pan)
{
    double d=((1.0-pan)*16383.0*0.5);
    if (d<0.0)d=0.0;
    else if (d>16383.0)d=16383.0;
    
    return (int)(d+0.5);
}

static  unsigned char volToChar(double vol)
{
    double d=(DB2SLIDER(VAL2DB(vol))*127.0/1000.0);
    if (d<0.0)d=0.0;
    else if (d>127.0)d=127.0;
    
    return (unsigned char)(d+0.5);
}

static double charToPan(unsigned char val)
{
    double pos=((double)val*1000.0+0.5)/127.0;
    
    pos=(pos-500.0)/500.0;
    if (fabs(pos) < 0.08) pos=0.0;
    
    return pos;
}

static unsigned char panToChar(double pan)
{
    pan = (pan+1.0)*63.5;
    
    if (pan<0.0)pan=0.0;
    else if (pan>127.0)pan=127.0;
    
    return (unsigned char)(pan+0.5);
}

static double clampedAndNormalized(double value, double valueMax, double valueMin)
{
    value = value > valueMax ? valueMax : value;
    value = value < valueMin ? valueMin : value;
    
    return 1.0 / (valueMax - valueMin) * (value - valueMin);
}

static double normalizedToVol(double value, double valueMaxDB, double valueMinDB)
{
    //slope = (output_end - output_start) / (input_end - input_start)
    //output = output_start + slope * (input - input_start)
    
    double slope = (valueMaxDB - valueMinDB) / (1.0 - 0.0);
    return DB2VAL(valueMinDB + slope * (value - 0.0));
}

static double volToNormalized(double value, double valueMaxDB, double valueMinDB)
{
    value = VAL2DB(value);
    
    //slope = (output_end - output_start) / (input_end - input_start)
    //output = output_start + slope * (input - input_start)
    
    double slope = (1.0 - 0.0) / (valueMaxDB - valueMinDB);
    return 0.0 + slope * (value - valueMinDB);
}
*/

#endif /* handy_functions_h */
